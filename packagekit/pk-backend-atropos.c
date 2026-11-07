/*
 * PackageKit backend that stages AtropOS updates with transactional-update.
 *
 * Discover talks to PackageKit. A check is zypper dup --dry-run with the
 * options transactional-update uses, reported as one "System updates" row.
 * Update All runs transactional-update dup, which builds a new snapshot and
 * leaves the running system alone until the next reboot. While that
 * snapshot waits, a check returns no packages and asks for a reboot, and
 * the user who started the update is notified once.
 *
 * Package installs and removals are refused; applications come from
 * Flathub. Resolve answers from the RPM database and the last check, so
 * Discover can match names.
 *
 * packagekitd calls every entry point on its main thread. Anything that
 * runs a command does it in a job thread. Otherwise the daemon stops
 * answering D-Bus (progress, Cancel, other clients) until the command ends.
 *
 * A failed command becomes one readable reason: a license, a file
 * conflict, a setup script, a full disk, a dependency, a missing download,
 * an unreachable server, or the last lines of the log.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <packagekit-glib2/packagekit.h>

/*
 * PackageKit installs no backend header. These match src/pk-backend-job.h
 * and src/pk-backend.h in PackageKit 1.4.0. A wrong prototype still links
 * and then crashes at run time, so check them when PackageKit changes
 * version.
 */
typedef struct _PkBackend PkBackend;
typedef struct _PkBackendJob PkBackendJob;

void pk_backend_job_finished (PkBackendJob *job);
void pk_backend_job_package (PkBackendJob *job, PkInfoEnum info, const gchar *package_id, const gchar *summary);
void pk_backend_job_details (PkBackendJob *job, const gchar *package_id, const gchar *summary, const gchar *license, PkGroupEnum group, const gchar *description, const gchar *url, gulong size, guint64 download_size);
void pk_backend_job_update_detail (PkBackendJob *job, const gchar *package_id, gchar **updates, gchar **obsoletes, gchar **vendor_urls, gchar **bugzilla_urls, gchar **cve_urls, PkRestartEnum restart, const gchar *update_text, const gchar *changelog, PkUpdateStateEnum state, const gchar *issued, const gchar *updated);
void pk_backend_job_require_restart (PkBackendJob *job, PkRestartEnum restart, const gchar *package_id);
void pk_backend_job_error_code (PkBackendJob *job, PkErrorEnum code, const gchar *details, ...) G_GNUC_PRINTF (3, 4);
void pk_backend_job_set_status (PkBackendJob *job, PkStatusEnum status);
void pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel);
void pk_backend_job_set_percentage (PkBackendJob *job, guint percentage);
gboolean pk_backend_job_is_cancelled (PkBackendJob *job);
guint pk_backend_job_get_uid (PkBackendJob *job);
typedef void (*PkBackendJobThreadFunc) (PkBackendJob *job, GVariant *params, gpointer user_data);
gboolean pk_backend_job_thread_create (PkBackendJob *job, PkBackendJobThreadFunc func, gpointer user_data, GDestroyNotify destroy_func);
gboolean pk_backend_is_online (PkBackend *backend);

#define ATROPOS_CACHE_DIR "/var/cache/atropos-packagekit"
#define ATROPOS_CACHE_FILE ATROPOS_CACHE_DIR "/updates.tsv"
#define ATROPOS_SIZE_FILE ATROPOS_CACHE_DIR "/download-size"
#define ATROPOS_REBOOT_FILE "/run/reboot-needed"
/* One notification per staged update. /run is cleared on reboot. */
#define ATROPOS_NOTIFIED_FILE "/run/atropos-packagekit-notified"
#define ATROPOS_PERCENTAGE_INVALID 101
/* openSUSE's rpm uses the ndb backend. sqlite is the upstream default. */
static const gchar *const atropos_rpmdb_files[] = {
	"/usr/lib/sysimage/rpm/Packages.db",
	"/usr/lib/sysimage/rpm/rpmdb.sqlite",
	NULL,
};
/* Discover shows this single row instead of one row per RPM. */
#define ATROPOS_BUNDLE_NAME "System updates"
#define ATROPOS_NOTIFY_APP "AtropOS"
#define ATROPOS_NOTIFY_BODY "The update was installed. It will be applied after you reboot."
/*
 * Expire with the server default. 0 means never expire. An untyped 0 is
 * also an int, and Notify's replaces-id is a uint, so the call is rejected
 * when plasmashell has not published introspection yet.
 */
#define ATROPOS_NOTIFY_REPLACES "@u 0"
#define ATROPOS_NOTIFY_ACTIONS "@as []"
#define ATROPOS_NOTIFY_HINTS "@a{sv} {}"
#define ATROPOS_NOTIFY_EXPIRE "@i -1"

typedef struct {
	gchar *section;
	gchar *name;
	gchar *edition;
	gchar *arch;
	gchar *repo;
	gchar *summary;
} AtroposPkg;

static void
atropos_pkg_free (AtroposPkg *pkg)
{
	if (pkg == NULL)
		return;
	g_free (pkg->section);
	g_free (pkg->name);
	g_free (pkg->edition);
	g_free (pkg->arch);
	g_free (pkg->repo);
	g_free (pkg->summary);
	g_free (pkg);
}

static gchar *
atropos_field (const gchar *text)
{
	gchar *copy;
	gchar *p;

	if (text == NULL || text[0] == '\0')
		return g_strdup ("unknown");
	copy = g_strdup (text);
	for (p = copy; *p != '\0'; p++) {
		if (*p == ';' || *p == '\t' || *p == '\n' || *p == '\r')
			*p = ' ';
	}
	return copy;
}

static gboolean
atropos_reboot_pending (void)
{
	return g_file_test (ATROPOS_REBOOT_FILE, G_FILE_TEST_EXISTS);
}

static void
child_set_pgroup (gpointer unused)
{
	(void) unused;
	if (setpgid (0, 0) != 0)
		_exit (127);
}

typedef struct {
	gchar *output;		/* stdout only. The dry-run XML is parsed from this. */
	gchar *stderr_text;	/* failure text. Not part of the XML document. */
	gint exit_code;
	gboolean cancelled;
	gchar *error;		/* spawn failure. The child never ran. */
	GString *line;		/* output after the last newline */
	guint percentage;	/* last value sent. It never goes down. */
	gboolean installing;	/* rpm has started in the snapshot */
	gboolean committed;	/* zypper is done. Only snapshot steps remain. */
} AtroposRun;

static void
atropos_run_clear (AtroposRun *run)
{
	g_free (run->output);
	g_free (run->stderr_text);
	g_free (run->error);
	run->output = NULL;
	run->stderr_text = NULL;
	run->error = NULL;
}

static void
set_progress (PkBackendJob *job, AtroposRun *run, guint percentage)
{
	/* PackageKit rejects a percentage that goes down. */
	if (percentage > 100 || percentage <= run->percentage)
		return;
	run->percentage = percentage;
	pk_backend_job_set_percentage (job, percentage);
}

/*
 * zypper counts downloads and installs separately, each from 1 to N.
 * Downloads fill 0-50 and installs 50-100, so the bar only moves forward.
 * Once zypper has returned after installing, transactional-update is
 * closing the snapshot and setting the default. A kill then can leave a
 * half-finished snapshot, so Cancel is turned off.
 */
static void
note_line (PkBackendJob *job, AtroposRun *run, const gchar *line)
{
	const gchar *p;
	gboolean install;
	gboolean download;

	if (run->installing && strstr (line, "Application returned with exit status") != NULL) {
		run->committed = TRUE;
		pk_backend_job_set_allow_cancel (job, FALSE);
		return;
	}
	install = strstr (line, "Installing:") != NULL || strstr (line, "Removing:") != NULL ||
		strstr (line, "Cleaning up:") != NULL;
	download = !install && (strstr (line, "Retrieving") != NULL ||
				strstr (line, "Downloading") != NULL ||
				strstr (line, "Preloading") != NULL ||
				g_str_has_prefix (line, "In cache"));
	if (!install && !download)
		return;
	if (install && !run->installing) {
		run->installing = TRUE;
		pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	} else if (download && !run->installing) {
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
	}

	for (p = line; (p = strchr (p, '(')) != NULL; p++) {
		guint done = 0;
		guint total = 0;

		if (sscanf (p, "(%u/%u)", &done, &total) == 2 && total > 0 && done <= total) {
			guint half = (done * 50) / total;

			set_progress (job, run, install ? 50 + half : half);
			break;
		}
	}
}

static void
note_output (PkBackendJob *job, AtroposRun *run, const gchar *chunk, gboolean flush)
{
	gchar *nl;

	if (run->line == NULL)
		run->line = g_string_new (NULL);
	if (chunk != NULL)
		g_string_append (run->line, chunk);
	while ((nl = strchr (run->line->str, '\n')) != NULL) {
		*nl = '\0';
		note_line (job, run, run->line->str);
		g_string_erase (run->line, 0, (nl - run->line->str) + 1);
	}
	if (flush && run->line->len > 0) {
		note_line (job, run, run->line->str);
		g_string_truncate (run->line, 0);
	}
}

/*
 * stdout and stderr stay separate buffers. A successful dry-run is XML on
 * stdout, and any stderr warning would make GMarkup reject that document.
 * zypper writes a file conflict on stderr. Failure text joins the two
 * after the command has already failed.
 */
static void
read_pipe (gint fd, GString *buf, PkBackendJob *job, AtroposRun *run, gboolean *eof)
{
	gchar tmp[4096];
	ssize_t n;

	n = read (fd, tmp, sizeof tmp - 1);
	if (n > 0) {
		tmp[n] = '\0';
		g_string_append_len (buf, tmp, n);
		note_output (job, run, tmp, FALSE);
		return;
	}
	if (n == 0 || (n < 0 && errno != EINTR))
		*eof = TRUE;
}

static gboolean
run_command (PkBackendJob *job, gchar **argv, AtroposRun *run)
{
	GPid pid = 0;
	gint out_fd = -1;
	gint err_fd = -1;
	GError *error = NULL;
	GString *out_buf;
	GString *err_buf;
	gboolean out_eof = FALSE;
	gboolean err_eof = FALSE;
	gboolean exited = FALSE;
	gboolean termed = FALSE;
	gint status = 0;
	gint64 term_at = 0;
	gint64 exited_at = 0;

	memset (run, 0, sizeof *run);
	run->exit_code = -1;
	pk_backend_job_set_allow_cancel (job, TRUE);
	pk_backend_job_set_percentage (job, ATROPOS_PERCENTAGE_INVALID);

	/*
	 * zypper XML tags are stable, but the "New default snapshot" line is
	 * translated. Force C so that match, and the dry-run parse, stay stable.
	 * The child environment is a copy. packagekitd keeps its own locale.
	 */
	{
		gchar **env = g_get_environ ();

		env = g_environ_setenv (env, "LANG", "C", TRUE);
		env = g_environ_setenv (env, "LC_ALL", "C", TRUE);
		env = g_environ_setenv (env, "LC_MESSAGES", "C", TRUE);
		/*
		 * packagekitd clears its environment after exec. g_get_environ()
		 * is then empty, and bash's default PATH has no sbin, so
		 * transactional-update cannot find btrfs or tukit.
		 */
		env = g_environ_setenv (env, "PATH",
					"/usr/local/sbin:/usr/sbin:/usr/local/bin:/usr/bin:/sbin:/bin",
					TRUE);
		if (!g_spawn_async_with_pipes (NULL, argv, env,
					       G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
					       child_set_pgroup, NULL, &pid, NULL, &out_fd, &err_fd, &error)) {
			run->error = g_strdup (error->message);
			g_error_free (error);
			g_strfreev (env);
			return FALSE;
		}
		g_strfreev (env);
	}
	if (setpgid (pid, pid) != 0 && errno != EACCES)
		g_debug ("setpgid: %s", g_strerror (errno));

	out_buf = g_string_new (NULL);
	err_buf = g_string_new (NULL);
	while (!out_eof || !err_eof || !exited) {
		struct pollfd pfd[2] = { { 0 } };
		gint npfd = 0;
		gint out_slot = -1;
		gint err_slot = -1;

		/*
		 * Count time, not loop turns. A streaming zypper fills a pipe
		 * many times a second, and SIGKILL that soon skips
		 * transactional-update's snapshot cleanup and can leave a
		 * zypp lock. Ten seconds lets the process group exit cleanly.
		 */
		if (!run->committed && pk_backend_job_is_cancelled (job)) {
			run->cancelled = TRUE;
			if (!termed) {
				kill (-pid, SIGTERM);
				termed = TRUE;
				term_at = g_get_monotonic_time ();
			} else if (g_get_monotonic_time () - term_at >= 10 * G_USEC_PER_SEC) {
				kill (-pid, SIGKILL);
			}
		}

		if (!out_eof) {
			pfd[npfd].fd = out_fd;
			pfd[npfd].events = POLLIN;
			pfd[npfd].revents = 0;
			out_slot = npfd++;
		}
		if (!err_eof) {
			pfd[npfd].fd = err_fd;
			pfd[npfd].events = POLLIN;
			pfd[npfd].revents = 0;
			err_slot = npfd++;
		}
		/*
		 * npfd == 0 still has to wait. Otherwise a child that closed
		 * both pipes and has not exited makes this loop spin.
		 */
		if (poll (npfd > 0 ? pfd : NULL, npfd, 200) > 0) {
			if (out_slot >= 0 && (pfd[out_slot].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
				read_pipe (out_fd, out_buf, job, run, &out_eof);
			if (err_slot >= 0 && (pfd[err_slot].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
				read_pipe (err_fd, err_buf, job, run, &err_eof);
		}

		if (!exited) {
			pid_t got = waitpid (pid, &status, WNOHANG);
			if (got == pid) {
				exited = TRUE;
				exited_at = g_get_monotonic_time ();
				if (WIFEXITED (status))
					run->exit_code = WEXITSTATUS (status);
				else if (WIFSIGNALED (status))
					run->exit_code = 128 + WTERMSIG (status);
			} else if (got < 0 && errno == ECHILD) {
				exited = TRUE;
				exited_at = g_get_monotonic_time ();
			}
		}

		if (run->cancelled && exited)
			break;
		/*
		 * Something the command started can keep the pipe open after
		 * the command itself has exited. Waiting for EOF then never
		 * ends and the job hangs. Five seconds is enough to drain it.
		 */
		if (exited && g_get_monotonic_time () - exited_at >= 5 * G_USEC_PER_SEC)
			break;
	}
	note_output (job, run, NULL, TRUE);
	if (run->line != NULL) {
		g_string_free (run->line, TRUE);
		run->line = NULL;
	}

	if (!exited) {
		kill (-pid, SIGKILL);
		waitpid (pid, &status, 0);
	}
	if (out_fd >= 0)
		close (out_fd);
	if (err_fd >= 0)
		close (err_fd);
	g_spawn_close_pid (pid);
	run->output = g_string_free (out_buf, FALSE);
	run->stderr_text = g_string_free (err_buf, FALSE);
	/*
	 * 102 and 103 are zypper's "finished, reboot or restart suggested"
	 * codes. The command did its work. 106 (a repository was skipped)
	 * stays a failure so Discover does not treat a partial refresh as done.
	 */
	return !run->cancelled &&
		(run->exit_code == 0 || run->exit_code == 102 || run->exit_code == 103);
}

static gboolean
text_has (const gchar *lower, const gchar *needle)
{
	return lower != NULL && strstr (lower, needle) != NULL;
}

/*
 * Codes below 100 are errors. 7 is the lock. 106 is a repository that
 * failed to refresh. Code 4 is the generic ZYPP failure, so the message
 * has to separate a dead mirror, a media change, a lock, and a conflict.
 * PackageKit has no PK_ERROR_ENUM_LOCKED; the lock code is CANNOT_GET_LOCK.
 */
static gboolean output_has_hard_404 (const gchar *text);
static gboolean output_has_file_conflict (const gchar *text);
static gboolean output_has_dep_problem (const gchar *text);
static gboolean output_has_license_abort (const gchar *text);

/*
 * transactional-update exits 1 for every zypper failure. The real code is
 * in "failed with exit code N" or "exit status N".
 */
static gint
embedded_command_exit (const gchar *text, gint fallback)
{
	const gchar *hit;
	gint code;

	if (text == NULL)
		return fallback;
	hit = strstr (text, "failed with exit code ");
	if (hit == NULL)
		hit = strstr (text, "exit status ");
	if (hit == NULL)
		return fallback;
	while (*hit != '\0' && !g_ascii_isdigit (*hit))
		hit++;
	if (*hit == '\0')
		return fallback;
	code = (gint) g_ascii_strtoll (hit, NULL, 10);
	if (code <= 0)
		return fallback;
	return code;
}

static PkErrorEnum
zypper_error_code (gint exit_code, const gchar *output)
{
	gchar *lower;
	PkErrorEnum code;
	gint reported;

	if (exit_code < 0)
		return PK_ERROR_ENUM_FAILED_INITIALIZATION;

	reported = embedded_command_exit (output, exit_code);

	switch (reported) {
	case 5: /* ZYPPER_EXIT_ERR_PRIVILEGES */
		return PK_ERROR_ENUM_NOT_AUTHORIZED;
	case 6: /* ZYPPER_EXIT_NO_REPOS */
		return PK_ERROR_ENUM_REPO_CONFIGURATION_ERROR;
	case 7: /* ZYPPER_EXIT_ZYPP_LOCKED */
		return PK_ERROR_ENUM_CANNOT_GET_LOCK;
	case 105: /* ZYPPER_EXIT_ON_SIGNAL */
		return PK_ERROR_ENUM_TRANSACTION_CANCELLED;
	case 106: /* ZYPPER_EXIT_INF_REPOS_SKIPPED */
		return PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
	case 107: /* ZYPPER_EXIT_INF_RPM_SCRIPT_FAILED */
		return PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE;
	default:
		break;
	}

	lower = output != NULL ? g_ascii_strdown (output, -1) : NULL;
	if (output_has_license_abort (output))
		code = PK_ERROR_ENUM_NO_LICENSE_AGREEMENT;
	else if (output_has_hard_404 (output))
		code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
	else if (text_has (lower, "is locked") || text_has (lower, "zypp lock"))
		code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
	else if (output_has_file_conflict (output))
		code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
	else if (output_has_dep_problem (output))
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (text_has (lower, "please insert"))
		code = PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED;
	else if (text_has (lower, "no network") || text_has (lower, "network is unreachable"))
		code = PK_ERROR_ENUM_NO_NETWORK;
	else if (text_has (lower, "valid metadata not found") ||
		 text_has (lower, "not found on medium") ||
		 text_has (lower, "couldn't resolve host") ||
		 text_has (lower, "connection refused") ||
		 text_has (lower, "connection timed out") ||
		 text_has (lower, "timeout exceeded") ||
		 text_has (lower, "no more mirrors") ||
		 text_has (lower, "could not resolve") ||
		 text_has (lower, "name or service not known"))
		code = PK_ERROR_ENUM_REPO_NOT_AVAILABLE;
	else if (text_has (lower, "conflict"))
		code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
	else if (text_has (lower, "unresolved") || text_has (lower, "solver"))
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (text_has (lower, "no space left") || text_has (lower, "not enough space"))
		code = PK_ERROR_ENUM_NO_SPACE_ON_DEVICE;
	else if (text_has (lower, "scriptlet failed"))
		code = PK_ERROR_ENUM_PACKAGE_FAILED_TO_CONFIGURE;
	else if (reported == 8) /* ZYPPER_EXIT_ERR_COMMIT */
		code = PK_ERROR_ENUM_TRANSACTION_ERROR;
	else if (reported == 1 || reported == 2 || reported == 3)
		code = PK_ERROR_ENUM_INTERNAL_ERROR;
	else if (reported > 128)
		code = PK_ERROR_ENUM_PROCESS_KILL;
	else
		code = PK_ERROR_ENUM_TRANSACTION_ERROR;
	g_free (lower);
	return code;
}

static const gchar *
attr_value (const gchar **names, const gchar **values, const gchar *key)
{
	guint i;

	if (names == NULL)
		return NULL;
	for (i = 0; names[i] != NULL; i++) {
		if (g_strcmp0 (names[i], key) == 0)
			return values[i];
	}
	return NULL;
}

typedef struct {
	gchar *section;
	GPtrArray *pkgs;
	gboolean in_error;
	GString *error_text;
	guint64 download_size;
} ParseState;

static void
parse_start (GMarkupParseContext *context, const gchar *name, const gchar **names, const gchar **values, gpointer user, GError **error)
{
	ParseState *state = user;
	const gchar *type;
	AtroposPkg *pkg;

	(void) context;
	(void) error;
	if (g_strcmp0 (name, "to-upgrade") == 0 || g_strcmp0 (name, "to-downgrade") == 0 ||
	    g_strcmp0 (name, "to-install") == 0 || g_strcmp0 (name, "to-reinstall") == 0 ||
	    g_strcmp0 (name, "to-remove") == 0) {
		g_free (state->section);
		state->section = g_strdup (name + 3);
		return;
	}
	if (g_strcmp0 (name, "message") == 0 && g_strcmp0 (attr_value (names, values, "type"), "error") == 0) {
		state->in_error = TRUE;
		return;
	}
	/*
	 * Zypper puts the whole download on install-summary, not on each
	 * solvable. Slowroll does not publish per-package or delta sizes.
	 */
	if (g_strcmp0 (name, "install-summary") == 0) {
		const gchar *download = attr_value (names, values, "download-size");

		if (download != NULL && download[0] != '\0')
			state->download_size = g_ascii_strtoull (download, NULL, 10);
		return;
	}
	if (g_strcmp0 (name, "solvable") != 0 || state->section == NULL)
		return;
	type = attr_value (names, values, "type");
	if (type != NULL && g_strcmp0 (type, "package") != 0)
		return;
	pkg = g_new0 (AtroposPkg, 1);
	pkg->section = g_strdup (state->section);
	pkg->name = atropos_field (attr_value (names, values, "name"));
	pkg->edition = atropos_field (attr_value (names, values, "edition"));
	pkg->arch = atropos_field (attr_value (names, values, "arch"));
	pkg->repo = atropos_field (attr_value (names, values, "repository"));
	pkg->summary = atropos_field (attr_value (names, values, "summary"));
	g_ptr_array_add (state->pkgs, pkg);
}

static void
parse_end (GMarkupParseContext *context, const gchar *name, gpointer user, GError **error)
{
	ParseState *state = user;

	(void) context;
	(void) error;
	if (g_str_has_prefix (name, "to-") && state->section != NULL &&
	    g_strcmp0 (name + 3, state->section) == 0) {
		g_clear_pointer (&state->section, g_free);
	}
	if (g_strcmp0 (name, "message") == 0)
		state->in_error = FALSE;
}

static void
parse_text (GMarkupParseContext *context, const gchar *text, gsize len, gpointer user, GError **error)
{
	ParseState *state = user;

	(void) context;
	(void) error;
	if (state->in_error)
		g_string_append_len (state->error_text, text, len);
}

static GPtrArray *
parse_zypper_xml (const gchar *xml, gchar **error_text, guint64 *download_size)
{
	GMarkupParser vtable = {
		parse_start,
		parse_end,
		parse_text,
		NULL,
		NULL,
	};
	ParseState state;
	GMarkupParseContext *context;
	GError *error = NULL;
	gboolean ok;

	state.section = NULL;
	state.pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify) atropos_pkg_free);
	state.in_error = FALSE;
	state.error_text = g_string_new (NULL);
	state.download_size = 0;
	context = g_markup_parse_context_new (&vtable, 0, &state, NULL);
	ok = g_markup_parse_context_parse (context, xml, -1, &error) &&
	     g_markup_parse_context_end_parse (context, &error);
	g_markup_parse_context_free (context);
	g_free (state.section);
	if (!ok) {
		if (download_size != NULL)
			*download_size = 0;
		if (error_text != NULL)
			*error_text = g_strdup (error->message);
		g_error_free (error);
		g_ptr_array_free (state.pkgs, TRUE);
		g_string_free (state.error_text, TRUE);
		return NULL;
	}
	if (download_size != NULL)
		*download_size = state.download_size;
	if (error_text != NULL) {
		g_strstrip (state.error_text->str);
		if (state.error_text->len > 0 && state.pkgs->len == 0)
			*error_text = g_string_free (state.error_text, FALSE);
		else
			*error_text = NULL;
	}
	if (error_text == NULL || *error_text == NULL)
		g_string_free (state.error_text, TRUE);
	return state.pkgs;
}

static gboolean
write_cache (GPtrArray *pkgs)
{
	GString *text;
	guint i;
	gboolean ok;

	if (g_mkdir_with_parents (ATROPOS_CACHE_DIR, 0755) != 0)
		return FALSE;
	text = g_string_new (NULL);
	for (i = 0; i < pkgs->len; i++) {
		AtroposPkg *pkg = g_ptr_array_index (pkgs, i);
		g_string_append_printf (text, "%s\t%s\t%s\t%s\t%s\t%s\n",
					pkg->section, pkg->name, pkg->edition,
					pkg->arch, pkg->repo, pkg->summary);
	}
	/* Writes a temporary file and renames it over the old one. */
	ok = g_file_set_contents (ATROPOS_CACHE_FILE, text->str, text->len, NULL);
	g_string_free (text, TRUE);
	return ok;
}

static gboolean
write_download_size (guint64 download_size)
{
	gchar *text;
	gboolean ok;

	if (g_mkdir_with_parents (ATROPOS_CACHE_DIR, 0755) != 0)
		return FALSE;
	text = g_strdup_printf ("%" G_GUINT64_FORMAT "\n", download_size);
	ok = g_file_set_contents (ATROPOS_SIZE_FILE, text, -1, NULL);
	g_free (text);
	return ok;
}

static guint64
read_download_size (void)
{
	gchar *text = NULL;
	guint64 download_size = 0;

	if (!g_file_get_contents (ATROPOS_SIZE_FILE, &text, NULL, NULL))
		return 0;
	download_size = g_ascii_strtoull (text, NULL, 10);
	g_free (text);
	return download_size;
}

static GPtrArray *
read_cache (void)
{
	gchar *text = NULL;
	gchar **lines;
	guint i;
	GPtrArray *pkgs;

	if (!g_file_get_contents (ATROPOS_CACHE_FILE, &text, NULL, NULL))
		return NULL;
	pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify) atropos_pkg_free);
	lines = g_strsplit (text, "\n", -1);
	g_free (text);
	for (i = 0; lines[i] != NULL; i++) {
		gchar **fields;
		AtroposPkg *pkg;

		if (lines[i][0] == '\0')
			continue;
		fields = g_strsplit (lines[i], "\t", 6);
		if (g_strv_length (fields) < 6) {
			g_strfreev (fields);
			continue;
		}
		pkg = g_new0 (AtroposPkg, 1);
		pkg->section = fields[0];
		pkg->name = fields[1];
		pkg->edition = fields[2];
		pkg->arch = fields[3];
		pkg->repo = fields[4];
		pkg->summary = fields[5];
		g_free (fields);
		g_ptr_array_add (pkgs, pkg);
	}
	g_strfreev (lines);
	return pkgs;
}

/*
 * A newer mtime means zypper or transactional-update ran outside Discover.
 * /var/cache/zypp is where refresh writes metadata. The state file and the
 * log are transactional-update's own markers. /run/reboot-needed is handled
 * before this cache is consulted.
 */
static const gchar *const atropos_cache_markers[] = {
	"/var/lib/zypp",
	"/var/cache/zypp",
	"/var/lib/misc/transactional-update.state",
	"/var/log/transactional-update.log",
	NULL,
};

static gboolean
cache_is_fresh (guint max_age)
{
	GStatBuf st;
	gint64 cached;
	guint i;

	if (max_age == 0 || g_stat (ATROPOS_CACHE_FILE, &st) != 0)
		return FALSE;
	cached = (gint64) st.st_mtime;
	if ((g_get_real_time () / G_USEC_PER_SEC) - cached >= (gint64) max_age)
		return FALSE;
	for (i = 0; atropos_cache_markers[i] != NULL; i++) {
		GStatBuf marker;

		if (g_stat (atropos_cache_markers[i], &marker) == 0 &&
		    (gint64) marker.st_mtime > cached)
			return FALSE;
	}
	return TRUE;
}

static gchar *
bundle_package_id (void)
{
	return pk_package_id_build (ATROPOS_BUNDLE_NAME, "pending", "noarch", "slowroll");
}

static gchar *
bundle_summary (guint count, guint64 download_size)
{
	if (download_size > 0) {
		gchar *size_text = g_format_size_full (download_size, G_FORMAT_SIZE_IEC_UNITS);
		gchar *summary = g_strdup_printf ("%u packages, %s download. Applied after the next reboot.",
						  count, size_text);
		g_free (size_text);
		return summary;
	}
	return g_strdup_printf ("%u packages. Applied after the next reboot.", count);
}

/*
 * One Discover row for the whole dup. The cache still lists every RPM so
 * resolve can answer "is this name in the update?".
 */
static void
emit_system_updates (PkBackendJob *job, GPtrArray *pkgs, guint64 download_size)
{
	gchar *package_id;
	gchar *summary;

	if (pkgs == NULL || pkgs->len == 0)
		return;
	summary = bundle_summary (pkgs->len, download_size);
	package_id = bundle_package_id ();
	pk_backend_job_package (job, PK_INFO_ENUM_NORMAL, package_id, summary);
	g_free (summary);
	g_free (package_id);
}

static gboolean
text_is_blank (const gchar *text)
{
	if (text == NULL)
		return TRUE;
	while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
		text++;
	return *text == '\0';
}

/* Discover shows Transaction error: "" when the details string is empty.
 * A dead packagekitd also produces that string. This path at least never
 * sends a blank message of its own. */
static gchar *
atropos_error_text (const gchar *text, gint exit_code)
{
	if (!text_is_blank (text))
		return g_strdup (text);
	return g_strdup_printf ("The update command failed with no output (exit %d).", exit_code);
}

/* "trying next mirror" is a retry. The line that gives up has no such note. */
static gboolean
line_is_hard_404 (const gchar *line)
{
	gchar *lower;
	gboolean hard;

	/*
	 * Only a package file. A missing repomd.xml is a repository that
	 * cannot be read, not a package that is still being published.
	 */
	lower = g_ascii_strdown (line, -1);
	hard = (strstr (lower, "404") != NULL || strstr (lower, "not found on medium") != NULL) &&
		strstr (lower, ".rpm") != NULL &&
		strstr (lower, "trying next mirror") == NULL;
	g_free (lower);
	return hard;
}

static gboolean
output_has_hard_404 (const gchar *text)
{
	gchar **lines;
	guint i;
	gboolean found = FALSE;

	if (text == NULL)
		return FALSE;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		if (line_is_hard_404 (lines[i])) {
			found = TRUE;
			break;
		}
	}
	g_strfreev (lines);
	return found;
}

/* The rpm token on a preload line, without the directory. */
static gchar *
rpm_basename_in (const gchar *line)
{
	const gchar *hit;
	const gchar *start;

	hit = strstr (line, ".rpm");
	if (hit == NULL)
		return NULL;
	start = hit;
	while (start > line && start[-1] != ' ' && start[-1] != '/' &&
	       start[-1] != '\'' && start[-1] != '"')
		start--;
	return g_strndup (start, (gsize) (hit - start) + 4);
}

static gboolean
names_have (GPtrArray *names, const gchar *rpm)
{
	guint i;

	for (i = 0; i < names->len; i++) {
		if (g_strcmp0 (g_ptr_array_index (names, i), rpm) == 0)
			return TRUE;
	}
	return FALSE;
}

/*
 * Discover keeps the tail of a long error. A 404 during preload is then
 * hidden behind "use --interactive" and the snapshot cleanup. Name the
 * missing files instead.
 */
static gchar *
missing_download_text (const gchar *text)
{
	gchar **lines;
	guint i;
	guint shown;
	GPtrArray *names;
	GString *msg;

	if (!output_has_hard_404 (text))
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	names = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *rpm;

		if (!line_is_hard_404 (lines[i]))
			continue;
		rpm = rpm_basename_in (lines[i]);
		if (rpm == NULL || names_have (names, rpm)) {
			g_free (rpm);
			continue;
		}
		g_ptr_array_add (names, rpm);
	}
	g_strfreev (lines);

	msg = g_string_new ("The update was not installed. Some packages are listed "
			    "but not available for download yet (HTTP 404). This happens "
			    "while the repository is still being published. Nothing was "
			    "changed. Wait a few minutes and try the update again.");
	if (names->len > 0)
		g_string_append (msg, " Missing:");
	shown = MIN (names->len, 8);
	for (i = 0; i < shown; i++)
		g_string_append_printf (msg, " %s", (const gchar *) g_ptr_array_index (names, i));
	if (names->len > shown)
		g_string_append_printf (msg, " and %u more", names->len - shown);
	g_ptr_array_free (names, TRUE);
	return g_string_free (msg, FALSE);
}

/*
 * transactional-update dup passes -y --auto-agree-with-product-licenses,
 * never --auto-agree-with-licenses, and dup takes no extra zypper options.
 * A package with a EULA (everything on download.nvidia.com) is then
 * answered "no" and zypper aborts with exit 4. The dry-run check agrees to
 * licenses, so it did not see this. autoAgreeWithLicenses in zypper.conf
 * is the only switch transactional-update honours.
 */
static gboolean
output_has_license_abort (const gchar *text)
{
	return text != NULL &&
		(strstr (text, "Aborting installation due to the need for license confirmation") != NULL ||
		 strstr (text, "use the --auto-agree-with-licenses option") != NULL);
}

static gchar *
license_abort_text (const gchar *text)
{
	if (!output_has_license_abort (text))
		return NULL;
	return g_strdup ("The update was not installed. Nothing was changed.\n"
			 "A package in this update has a license that must be accepted "
			 "(the NVIDIA driver packages do), and transactional-update does "
			 "not accept package licenses on its own.\n"
			 "Set \"autoAgreeWithLicenses = yes\" under [commit] in "
			 "/etc/zypp/zypper.conf, or run \"sudo transactional-update dup\" "
			 "in a terminal once and accept the license there.");
}

/* Download progress and the snapshot cleanup. Discover was showing these. */
static gboolean
failure_line_is_noise (const gchar *line)
{
	return g_str_has_prefix (line, "In cache") ||
		g_str_has_prefix (line, "Retrieving:") ||
		g_str_has_prefix (line, "Preloading:") ||
		g_str_has_prefix (line, "Verifying") ||
		g_str_has_prefix (line, "Preparing") ||
		g_str_has_prefix (line, "Options:") ||
		strstr (line, "Found plugin") != NULL ||
		strstr (line, "tukit ") != NULL ||
		strstr (line, "Transaction completed") != NULL ||
		strstr (line, "transactional-update finished") != NULL ||
		strstr (line, "Use '--interactive'") != NULL ||
		strstr (line, "Removing snapshot") != NULL ||
		strstr (line, "Discarding snapshot") != NULL ||
		strstr (line, "Using 'snapper'") != NULL ||
		g_str_has_prefix (line, "transactional-update ") ||
		g_str_has_prefix (line, "Separate /var detected") ||
		g_str_has_prefix (line, "Using snapshot ") ||
		g_str_has_prefix (line, "ID: ") ||
		g_str_has_prefix (line, "Calling zypper") ||
		g_str_has_prefix (line, "Executing `zypper") ||
		g_str_has_prefix (line, "Refreshing service") ||
		g_str_has_prefix (line, "Loading repository data") ||
		g_str_has_prefix (line, "Reading installed packages") ||
		g_str_has_prefix (line, "Computing distribution upgrade") ||
		g_str_has_prefix (line, "Warning: You are about to do a distribution upgrade");
}

/* Solver menus and the transactional-update summary. Not a reason. */
static gboolean
failure_line_is_wrapper (const gchar *line)
{
	return failure_line_is_noise (line) ||
		g_str_has_prefix (line, "Application returned") ||
		g_str_has_prefix (line, "Please see the above") ||
		g_str_has_prefix (line, "Problem occurred") ||
		g_str_has_prefix (line, "Executing the transaction") ||
		g_str_has_prefix (line, "Checking for ") ||
		g_str_has_prefix (line, "Solution ") ||
		g_str_has_prefix (line, "Choose from") ||
		g_str_has_prefix (line, "Choose the");
}

/*
 * "file PATH from install of NEW conflicts with file from package OLD"
 * rpm prints this when two packages own one path with different metadata.
 */
static gboolean
split_file_conflict (const gchar *line, gchar **path_out, gchar **newer_out, gchar **older_out)
{
	const gchar *from;
	const gchar *mid;
	const gchar *path;
	const gchar *newer;
	const gchar *older;
	gsize path_len;
	gsize newer_len;

	if (!g_str_has_prefix (line, "file "))
		return FALSE;
	from = strstr (line, " from install of ");
	mid = strstr (line, " conflicts with file from package ");
	if (from == NULL || mid == NULL || mid <= from)
		return FALSE;
	path = line + 5;
	path_len = (gsize) (from - path);
	newer = from + strlen (" from install of ");
	newer_len = (gsize) (mid - newer);
	older = mid + strlen (" conflicts with file from package ");
	if (path_len == 0 || newer_len == 0 || older[0] == '\0')
		return FALSE;
	if (path_out != NULL)
		*path_out = g_strstrip (g_strndup (path, path_len));
	if (newer_out != NULL)
		*newer_out = g_strstrip (g_strndup (newer, newer_len));
	if (older_out != NULL)
		*older_out = g_strstrip (g_strdup (older));
	return TRUE;
}

static gboolean
output_has_file_conflict (const gchar *text)
{
	gchar **lines;
	guint i;
	gboolean found = FALSE;

	if (text == NULL)
		return FALSE;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL && !found; i++) {
		gchar *line = g_strdup (lines[i]);
		gchar *lower;

		g_strstrip (line);
		lower = g_ascii_strdown (line, -1);
		found = split_file_conflict (line, NULL, NULL, NULL) ||
			strstr (lower, "file conflict") != NULL ||
			strstr (lower, "conflicts between attempted") != NULL;
		g_free (lower);
		g_free (line);
	}
	g_strfreev (lines);
	return found;
}

static gboolean
line_is_dep_problem (const gchar *line)
{
	gchar *lower;
	gboolean yes;

	lower = g_ascii_strdown (line, -1);
	yes = (g_str_has_prefix (lower, "problem:") &&
	       !g_str_has_prefix (lower, "problem occurred")) ||
		strstr (lower, "nothing provides") != NULL ||
		strstr (lower, "requirement cannot be provided") != NULL ||
		strstr (lower, "conflicting requests") != NULL ||
		strstr (lower, "not installable providers") != NULL ||
		strstr (lower, "unresolved") != NULL;
	g_free (lower);
	return yes;
}

static gboolean
output_has_dep_problem (const gchar *text)
{
	gchar **lines;
	guint i;
	gboolean found = FALSE;

	if (text == NULL)
		return FALSE;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL && !found; i++) {
		gchar *line = g_strdup (lines[i]);

		g_strstrip (line);
		found = line[0] != '\0' && !failure_line_is_wrapper (line) &&
			line_is_dep_problem (line);
		g_free (line);
	}
	g_strfreev (lines);
	return found;
}

static gchar *
replace_literal (const gchar *text, const gchar *from, const gchar *to)
{
	const gchar *hit;

	hit = strstr (text, from);
	if (hit == NULL)
		return g_strdup (text);
	return g_strdup_printf ("%.*s%s%s", (gint) (hit - text), text, to, hit + strlen (from));
}

/* Turn one zypper "Problem:" line into a sentence. */
static gchar *
friendly_problem_line (const gchar *stripped)
{
	const gchar *body = stripped;
	gchar *step;
	gchar *next;
	const gchar *provides;
	const gchar *needed;

	if (g_str_has_prefix (body, "Problem:")) {
		body += strlen ("Problem:");
		while (*body == ' ')
			body++;
	}
	if (g_strcmp0 (body, "conflicting requests") == 0)
		return g_strdup ("The requested packages conflict with each other.");

	provides = strstr (body, "nothing provides ");
	needed = provides != NULL ? strstr (provides, " needed by ") : NULL;
	if (provides != NULL && needed != NULL) {
		gchar *what;
		gchar *who;
		gchar *sentence;

		what = g_strndup (provides + strlen ("nothing provides "),
				  (gsize) (needed - (provides + strlen ("nothing provides "))));
		who = g_strdup (needed + strlen (" needed by "));
		g_strstrip (what);
		g_strstrip (who);
		sentence = g_strdup_printf ("%s needs %s, and no repository provides it.", who, what);
		g_free (what);
		g_free (who);
		return sentence;
	}

	step = replace_literal (body, "the to be installed ", "");
	next = replace_literal (step, "but this requirement cannot be provided",
				"and that requirement cannot be met");
	g_free (step);
	return next;
}

static gchar *
friendly_provider_line (const gchar *stripped)
{
	const gchar *what = stripped;

	if (g_str_has_prefix (what, "not installable providers:")) {
		what += strlen ("not installable providers:");
		while (*what == ' ')
			what++;
		if (what[0] == '\0')
			return g_strdup ("Nothing available provides the missing dependency.");
		return g_strdup_printf ("Nothing available provides %s.", what);
	}
	return g_strdup (stripped);
}

static void
append_capped (GString *msg, GPtrArray *lines)
{
	guint i;
	guint shown;
	guint added = 0;

	shown = MIN (lines->len, 8);
	for (i = 0; i < shown && msg->len < 1700; i++) {
		g_string_append_c (msg, '\n');
		g_string_append (msg, g_ptr_array_index (lines, i));
		added++;
	}
	if (added < lines->len)
		g_string_append_printf (msg, "\nand %u more", lines->len - added);
}

static gchar *
file_conflict_text (const gchar *text)
{
	gchar **lines;
	guint i;
	GPtrArray *rows;
	GString *msg;

	if (!output_has_file_conflict (text))
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	rows = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *line = g_strdup (lines[i]);
		gchar *path = NULL;
		gchar *newer = NULL;
		gchar *older = NULL;
		gchar *sentence;

		g_strstrip (line);
		if (!split_file_conflict (line, &path, &newer, &older)) {
			g_free (line);
			continue;
		}
		sentence = g_strdup_printf ("%s is owned by both %s and %s.", path, newer, older);
		g_free (path);
		g_free (newer);
		g_free (older);
		g_free (line);
		if (names_have (rows, sentence)) {
			g_free (sentence);
			continue;
		}
		g_ptr_array_add (rows, sentence);
	}
	g_strfreev (lines);
	if (rows->len == 0) {
		g_ptr_array_free (rows, TRUE);
		return NULL;
	}
	msg = g_string_new ("The update was not installed. Two packages own the same file, "
			    "so nothing was changed.");
	append_capped (msg, rows);
	g_string_append (msg, "\nUpdating again will fail until those packages are fixed.");
	g_ptr_array_free (rows, TRUE);
	return g_string_free (msg, FALSE);
}

static gchar *
dep_problem_text (const gchar *text)
{
	gchar **lines;
	guint i;
	GPtrArray *rows;
	GString *msg;

	if (text == NULL || !output_has_dep_problem (text))
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	rows = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *line = g_strdup (lines[i]);
		gchar *sentence = NULL;
		gchar *lower;

		g_strstrip (line);
		if (line[0] == '\0' || failure_line_is_wrapper (line)) {
			g_free (line);
			continue;
		}
		lower = g_ascii_strdown (line, -1);
		if (g_str_has_prefix (lower, "problem:") || strstr (lower, "nothing provides") != NULL ||
		    strstr (lower, "conflicting requests") != NULL)
			sentence = friendly_problem_line (line);
		else if (strstr (lower, "not installable providers") != NULL)
			sentence = friendly_provider_line (line);
		g_free (lower);
		g_free (line);
		if (sentence == NULL)
			continue;
		if (names_have (rows, sentence)) {
			g_free (sentence);
			continue;
		}
		g_ptr_array_add (rows, sentence);
	}
	g_strfreev (lines);
	if (rows->len == 0) {
		g_ptr_array_free (rows, TRUE);
		return g_strdup ("The update was not installed. A dependency could not be "
				 "satisfied, so nothing was changed.\n"
				 "Try again in a few minutes. If this dependency is still "
				 "missing, the packages need a fix.");
	}
	msg = g_string_new ("The update was not installed. A dependency could not be "
			    "satisfied, so nothing was changed.");
	append_capped (msg, rows);
	g_string_append (msg, "\nTry again in a few minutes. If this dependency is still "
			      "missing, the packages need a fix.");
	g_ptr_array_free (rows, TRUE);
	return g_string_free (msg, FALSE);
}

static gchar *
scriptlet_failure_text (const gchar *text)
{
	gchar **lines;
	guint i;
	GPtrArray *names;
	GString *msg;

	if (text == NULL || strstr (text, "scriptlet failed") == NULL)
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	names = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		const gchar *open;
		const gchar *close;
		gchar *name;

		if (strstr (lines[i], "scriptlet failed") == NULL)
			continue;
		open = strchr (lines[i], '(');
		close = open != NULL ? strchr (open + 1, ')') : NULL;
		if (open == NULL || close == NULL || close == open + 1)
			continue;
		name = g_strndup (open + 1, (gsize) (close - open - 1));
		g_strstrip (name);
		if (name[0] == '\0' || names_have (names, name)) {
			g_free (name);
			continue;
		}
		g_ptr_array_add (names, name);
	}
	g_strfreev (lines);
	if (names->len == 0) {
		g_ptr_array_free (names, TRUE);
		return g_strdup ("The update was not installed. A setup script failed, so "
				 "nothing was changed.");
	}
	msg = g_string_new ("The update was not installed. A setup script failed, so "
			    "nothing was changed.");
	g_string_append_printf (msg, "\nScript: %s", (const gchar *) g_ptr_array_index (names, 0));
	if (names->len > 1)
		g_string_append_printf (msg, "\nand %u more", names->len - 1);
	g_ptr_array_free (names, TRUE);
	return g_string_free (msg, FALSE);
}

static gboolean
log_is_only_noise (const gchar *text)
{
	gchar **lines;
	guint i;
	gboolean any = FALSE;
	gboolean only = TRUE;

	if (text == NULL)
		return TRUE;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *line = g_strdup (lines[i]);

		g_strstrip (line);
		if (line[0] == '\0') {
			g_free (line);
			continue;
		}
		any = TRUE;
		if (!failure_line_is_wrapper (line))
			only = FALSE;
		g_free (line);
	}
	g_strfreev (lines);
	return !any || only;
}

/*
 * A log that is only progress plus one ERROR: line used to be thrown away,
 * because ERROR: was treated as cleanup. Keep that sentence. A one-line
 * failure such as "mirror down" is already readable, so return NULL and
 * let that line through.
 */
static gchar *
plain_reason_text (const gchar *text)
{
	gchar **lines;
	guint i;
	gboolean noise = FALSE;
	GPtrArray *rows;
	GString *msg;

	if (text == NULL)
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	rows = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		gchar *line = g_strdup (lines[i]);

		g_strstrip (line);
		if (line[0] == '\0') {
			g_free (line);
			continue;
		}
		if (failure_line_is_wrapper (line)) {
			noise = TRUE;
			g_free (line);
			continue;
		}
		if (names_have (rows, line)) {
			g_free (line);
			continue;
		}
		g_ptr_array_add (rows, line);
	}
	g_strfreev (lines);
	if (rows->len == 0 || (!noise && strlen (text) < 400 && rows->len <= 3)) {
		g_ptr_array_free (rows, TRUE);
		return NULL;
	}
	msg = g_string_new ("The update was not installed. Nothing was changed.");
	/*
	 * The reason is at the end of the log. The head is the
	 * transactional-update banner and zypper's package list, and showing
	 * that with "and 21 more" hid the actual error.
	 */
	if (rows->len > 8) {
		guint skipped = rows->len - 8;

		g_ptr_array_remove_range (rows, 0, skipped);
		g_string_append_printf (msg, "\n(%u earlier lines not shown)", skipped);
	}
	append_capped (msg, rows);
	g_ptr_array_free (rows, TRUE);
	return g_string_free (msg, FALSE);
}

/*
 * A long log whose tail is the download cache used to be the whole dialog.
 * Keep a short reason instead.
 */
static gchar *
friendly_failure_text (const gchar *text, gint exit_code)
{
	gchar *shown;
	gchar *lower;
	gint code;

	shown = license_abort_text (text);
	if (shown != NULL)
		return shown;
	shown = file_conflict_text (text);
	if (shown != NULL)
		return shown;
	shown = scriptlet_failure_text (text);
	if (shown != NULL)
		return shown;
	lower = text != NULL ? g_ascii_strdown (text, -1) : NULL;
	if (text_has (lower, "no space left") || text_has (lower, "not enough space")) {
		g_free (lower);
		return g_strdup ("The update was not installed. The disk ran out of space, "
				 "so nothing was changed. Free some space and try the update again.");
	}
	g_free (lower);
	shown = dep_problem_text (text);
	if (shown != NULL)
		return shown;
	shown = plain_reason_text (text);
	if (shown != NULL)
		return shown;
	if (!log_is_only_noise (text))
		return NULL;

	code = embedded_command_exit (text, exit_code);
	if (code > 0)
		return g_strdup_printf ("The update was not installed. Nothing was changed. "
					"The updater failed (exit %d) and did not give a clearer reason.",
					code);
	return g_strdup ("The update was not installed. Nothing was changed. "
			 "The updater failed and did not give a reason.");
}

/*
 * The journal gets the same text. A check that fails at login is
 * otherwise only seen by the notifier, and nobody can tell why later.
 */
static void
fail_job (PkBackendJob *job, PkErrorEnum code, const gchar *text)
{
	gchar *shown = atropos_error_text (text, -1);

	g_warning ("atropos: %s: %s", pk_error_enum_to_string (code), shown);
	pk_backend_job_error_code (job, code, "%s", shown);
	pk_backend_job_finished (job);
	g_free (shown);
}

/* A raw log keeps its end, where the reason is. */
static gchar *
log_tail (const gchar *text, gsize max)
{
	gsize len = text != NULL ? strlen (text) : 0;

	if (len <= max)
		return g_strdup (text != NULL ? text : "");
	return g_strdup (text + (len - max));
}

/* stdout, then stderr. The XML parser must not see this joined buffer. */
static gchar *
command_log (const AtroposRun *run)
{
	if (run->error != NULL)
		return g_strdup (run->error);
	if (run->stderr_text == NULL || run->stderr_text[0] == '\0')
		return g_strdup (run->output != NULL ? run->output : "");
	if (run->output == NULL || run->output[0] == '\0')
		return g_strdup (run->stderr_text);
	return g_strconcat (run->output, "\n", run->stderr_text, NULL);
}

static void
fail_command (PkBackendJob *job, const AtroposRun *run)
{
	gchar *text;
	gchar *raw;

	if (run->cancelled) {
		fail_job (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
		return;
	}
	raw = command_log (run);
	text = missing_download_text (raw);
	if (text == NULL)
		text = friendly_failure_text (raw, run->exit_code);
	if (text == NULL) {
		gchar *tail = log_tail (raw, 2000);

		text = atropos_error_text (tail, run->exit_code);
		g_free (tail);
	}
	fail_job (job, zypper_error_code (run->exit_code, raw), text);
	g_free (text);
	g_free (raw);
}

/* The last line that is not progress or a wrapper. */
static gchar *
last_reason_line (const gchar *text)
{
	gchar **lines;
	gchar *found = NULL;
	gint i;

	if (text == NULL)
		return NULL;
	lines = g_strsplit (text, "\n", -1);
	for (i = (gint) g_strv_length (lines) - 1; i >= 0 && found == NULL; i--) {
		gchar *line = g_strstrip (g_strdup (lines[i]));

		if (line[0] != '\0' && !failure_line_is_wrapper (line) &&
		    !g_str_has_prefix (line, "Use '--interactive'"))
			found = line;
		else
			g_free (line);
	}
	g_strfreev (lines);
	return found;
}

/*
 * At login the notifier checks before the network is up, and zypper
 * fails in under a second. Wait for a route first. If the monitor still
 * says offline, try anyway: it can be wrong behind some VPN and static
 * setups, and zypper gives the real answer.
 */
static void
wait_for_network (PkBackendJob *job, PkBackend *backend)
{
	gint64 until = g_get_monotonic_time () + 45 * G_USEC_PER_SEC;

	while (!pk_backend_is_online (backend) && g_get_monotonic_time () < until &&
	       !pk_backend_job_is_cancelled (job))
		g_usleep (G_USEC_PER_SEC / 2);
}

/*
 * A check that cannot reach the servers says so in one sentence. The
 * zypper log about mirrors and repository aliases is not useful to
 * someone whose Wi-Fi is off.
 */
static void
fail_check (PkBackendJob *job, PkBackend *backend, const AtroposRun *run)
{
	gchar *raw;
	PkErrorEnum code;

	if (run->cancelled || run->error != NULL) {
		fail_command (job, run);
		return;
	}
	raw = command_log (run);
	code = zypper_error_code (run->exit_code, raw);
	if (!pk_backend_is_online (backend)) {
		fail_job (job, PK_ERROR_ENUM_NO_NETWORK,
			  "Could not check for updates because this computer is not "
			  "connected to the internet. Connect to a network and check again.");
	} else if (code == PK_ERROR_ENUM_NO_NETWORK || code == PK_ERROR_ENUM_REPO_NOT_AVAILABLE) {
		gchar *reason = last_reason_line (raw);
		gchar *text = g_strdup_printf ("Could not check for updates because the update "
					       "servers could not be reached. This is usually "
					       "temporary. Try again in a few minutes.%s%s",
					       reason != NULL ? "\nDetails: " : "",
					       reason != NULL ? reason : "");

		fail_job (job, code, text);
		g_free (text);
		g_free (reason);
	} else {
		fail_command (job, run);
	}
	g_free (raw);
}

static gboolean
collect_updates (PkBackendJob *job, PkBackend *backend, GPtrArray **pkgs_out)
{
	AtroposRun refresh;
	AtroposRun dup;
	gchar *refresh_argv[] = {
		"/usr/bin/zypper", "--non-interactive", "--gpg-auto-import-keys", "refresh", NULL,
	};
	/*
	 * The same options transactional-update dup gives zypper, so a
	 * check that passes means the update will too. In particular no
	 * --auto-agree-with-licenses: transactional-update never passes it,
	 * and a package license is then refused here instead of after the
	 * user has clicked Update. -y is a command option on zypper 1.14.
	 */
	gchar *dup_argv[] = {
		"/usr/bin/zypper", "--xmlout", "--non-interactive",
		"--gpg-auto-import-keys", "--no-cd",
		"dup", "--dry-run", "-y", "--auto-agree-with-product-licenses",
		NULL,
	};
	gchar *parse_error = NULL;
	guint64 download_size = 0;
	GPtrArray *pkgs;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	wait_for_network (job, backend);
	if (!run_command (job, refresh_argv, &refresh)) {
		fail_check (job, backend, &refresh);
		atropos_run_clear (&refresh);
		return FALSE;
	}
	atropos_run_clear (&refresh);

	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);
	if (!run_command (job, dup_argv, &dup)) {
		fail_command (job, &dup);
		atropos_run_clear (&dup);
		return FALSE;
	}

	/* stdout only. stderr is a warning or a later failure, not XML. */
	pkgs = parse_zypper_xml (dup.output != NULL ? dup.output : "", &parse_error, &download_size);
	atropos_run_clear (&dup);
	if (pkgs == NULL) {
		fail_job (job, PK_ERROR_ENUM_INTERNAL_ERROR, parse_error);
		g_free (parse_error);
		return FALSE;
	}
	if (parse_error != NULL) {
		fail_job (job, PK_ERROR_ENUM_TRANSACTION_ERROR, parse_error);
		g_free (parse_error);
		g_ptr_array_free (pkgs, TRUE);
		return FALSE;
	}
	write_cache (pkgs);
	write_download_size (download_size);
	*pkgs_out = pkgs;
	return TRUE;
}

typedef struct {
	gchar *stamp;
} AtroposNotify;

/* One notification is in flight. The file is written only after success. */
static GMutex atropos_notify_lock;
static gboolean atropos_notify_inflight;

static void
notify_release (void)
{
	g_mutex_lock (&atropos_notify_lock);
	atropos_notify_inflight = FALSE;
	g_mutex_unlock (&atropos_notify_lock);
}

static gboolean
notify_claim (void)
{
	gboolean claimed;

	g_mutex_lock (&atropos_notify_lock);
	claimed = !atropos_notify_inflight;
	if (claimed)
		atropos_notify_inflight = TRUE;
	g_mutex_unlock (&atropos_notify_lock);
	return claimed;
}

static void
reap_notify (GPid pid, gint status, gpointer user_data)
{
	AtroposNotify *note = user_data;

	if (WIFEXITED (status) && WEXITSTATUS (status) == 0)
		g_file_set_contents (ATROPOS_NOTIFIED_FILE, note->stamp, -1, NULL);
	g_free (note->stamp);
	g_free (note);
	g_spawn_close_pid (pid);
	notify_release ();
}

/*
 * Tell the session that clicked Update. packagekitd is root, and the
 * session bus rejects a root connection, so this is a short-lived
 * process running as that user. A failure leaves the update staged and
 * leaves the stamp unset. Only the update transaction calls this. A
 * later check must not, or the popup appears after Discover is closed.
 */
static void
notify_update_installed (PkBackendJob *job)
{
	guint uid = pk_backend_job_get_uid (job);
	struct passwd *pw;
	struct stat st;
	gchar *stamp = NULL;
	gchar *previous = NULL;
	gchar *username = NULL;
	gchar *home = NULL;
	gchar *bus = NULL;
	gchar *runtime = NULL;
	gchar *env_bus = NULL;
	gchar *env_runtime = NULL;
	gchar *env_home = NULL;
	gchar *argv[24];
	gchar *envp[5];
	AtroposNotify *note = NULL;
	GPid pid = 0;
	GError *error = NULL;
	guint n = 0;
	gboolean claimed = FALSE;

	if (uid < 1000)
		return;
	bus = g_strdup_printf ("/run/user/%u/bus", uid);
	if (!g_file_test (bus, G_FILE_TEST_EXISTS))
		goto out;
	if (stat (ATROPOS_REBOOT_FILE, &st) == 0)
		stamp = g_strdup_printf ("%lld\n", (long long) st.st_mtime);
	else
		stamp = g_strdup ("staged\n");
	if (g_file_get_contents (ATROPOS_NOTIFIED_FILE, &previous, NULL, NULL) &&
	    g_strcmp0 (previous, stamp) == 0)
		goto out;
	if (!notify_claim ())
		goto out;
	claimed = TRUE;
	pw = getpwuid (uid);
	if (pw == NULL || pw->pw_name == NULL || pw->pw_name[0] == '\0')
		goto out;
	username = g_strdup (pw->pw_name);
	home = g_strdup (pw->pw_dir != NULL ? pw->pw_dir : "/");
	runtime = g_strdup_printf ("/run/user/%u", uid);
	env_bus = g_strdup_printf ("DBUS_SESSION_BUS_ADDRESS=unix:path=%s", bus);
	env_runtime = g_strdup_printf ("XDG_RUNTIME_DIR=%s", runtime);
	env_home = g_strdup_printf ("HOME=%s", home);
	argv[n++] = "/usr/sbin/runuser";
	argv[n++] = "-u";
	argv[n++] = username;
	argv[n++] = "-m";
	argv[n++] = "--";
	argv[n++] = "/usr/bin/gdbus";
	argv[n++] = "call";
	argv[n++] = "--session";
	argv[n++] = "--dest";
	argv[n++] = "org.freedesktop.Notifications";
	argv[n++] = "--object-path";
	argv[n++] = "/org/freedesktop/Notifications";
	argv[n++] = "--method";
	argv[n++] = "org.freedesktop.Notifications.Notify";
	argv[n++] = ATROPOS_NOTIFY_APP;
	argv[n++] = ATROPOS_NOTIFY_REPLACES;
	argv[n++] = "system-software-update";
	argv[n++] = "Update installed";
	argv[n++] = ATROPOS_NOTIFY_BODY;
	argv[n++] = ATROPOS_NOTIFY_ACTIONS;
	argv[n++] = ATROPOS_NOTIFY_HINTS;
	argv[n++] = ATROPOS_NOTIFY_EXPIRE;
	argv[n++] = NULL;
	envp[0] = env_bus;
	envp[1] = env_runtime;
	envp[2] = env_home;
	envp[3] = "PATH=/usr/bin:/usr/sbin:/bin";
	envp[4] = NULL;
	if (!g_spawn_async (NULL, argv, envp,
			    G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
			    NULL, NULL, &pid, &error)) {
		g_clear_error (&error);
		goto out;
	}
	note = g_new0 (AtroposNotify, 1);
	note->stamp = stamp;
	stamp = NULL;
	claimed = FALSE;
	g_child_watch_add (pid, reap_notify, note);
out:
	if (claimed)
		notify_release ();
	g_free (stamp);
	g_free (previous);
	g_free (username);
	g_free (home);
	g_free (bus);
	g_free (runtime);
	g_free (env_bus);
	g_free (env_runtime);
	g_free (env_home);
}

static void
finish_reboot_required (PkBackendJob *job, const gchar *package_id)
{
	notify_update_installed (job);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_FINISHED);
	pk_backend_job_require_restart (job, PK_RESTART_ENUM_SYSTEM, package_id);
	pk_backend_job_finished (job);
}

/* Copies of the call arguments, owned by the job thread. */
typedef struct {
	PkBackend *backend;
	PkBitfield filters;
	PkBitfield flags;
	gchar **ids;
} AtroposJob;

static void
atropos_job_free (gpointer data)
{
	AtroposJob *aj = data;

	g_strfreev (aj->ids);
	g_free (aj);
}

static void
start_thread (PkBackendJob *job, PkBackendJobThreadFunc func, PkBackend *backend,
	      PkBitfield filters, PkBitfield flags, gchar **ids)
{
	AtroposJob *aj = g_new0 (AtroposJob, 1);

	aj->backend = backend;
	aj->filters = filters;
	aj->flags = flags;
	aj->ids = g_strdupv (ids);
	if (!pk_backend_job_thread_create (job, func, aj, atropos_job_free)) {
		atropos_job_free (aj);
		fail_job (job, PK_ERROR_ENUM_INTERNAL_ERROR, "Could not start the backend thread.");
	}
}

const gchar *
pk_backend_get_description (PkBackend *backend)
{
	(void) backend;
	return "AtropOS transactional-update";
}

const gchar *
pk_backend_get_author (PkBackend *backend)
{
	(void) backend;
	return "ahjolinna <ahjolinna@yahoo.com>";
}

void
pk_backend_initialize (GKeyFile *conf, PkBackend *backend)
{
	(void) conf;
	(void) backend;
}

void
pk_backend_destroy (PkBackend *backend)
{
	(void) backend;
}

/*
 * PackageKit advertises Cancel only when this symbol exists. It has
 * already cancelled the job's GCancellable by the time it calls here, and
 * run_command polls that, so there is nothing more to do.
 */
void
pk_backend_cancel (PkBackend *backend, PkBackendJob *job)
{
	(void) backend;
	(void) job;
}

static void
get_updates_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	AtroposJob *aj = user_data;
	GPtrArray *pkgs = NULL;

	(void) params;
	if (atropos_reboot_pending ()) {
		gchar *package_id = bundle_package_id ();

		/*
		 * Discover shows its reboot page only when this check
		 * returns no packages. A row here keeps Update All.
		 * require_restart marks the page. This does not reboot,
		 * does not start another dup, and does not notify. The
		 * notifier calls this after Discover has closed.
		 */
		pk_backend_job_set_status (job, PK_STATUS_ENUM_FINISHED);
		pk_backend_job_require_restart (job, PK_RESTART_ENUM_SYSTEM, package_id);
		pk_backend_job_finished (job);
		g_free (package_id);
		return;
	}

	/*
	 * Discover and the notifier both call this. Reuse a successful
	 * solve for ten minutes so the notifier does not dup --dry-run
	 * on every login refresh. A newer zypp directory or
	 * transactional-update marker drops that cache immediately.
	 */
	if (cache_is_fresh (600))
		pkgs = read_cache ();
	if (pkgs == NULL && !collect_updates (job, aj->backend, &pkgs))
		return;
	emit_system_updates (job, pkgs, read_download_size ());
	g_ptr_array_free (pkgs, TRUE);
	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	start_thread (job, get_updates_thread, backend, filters, 0, NULL);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	(void) backend;
	/*
	 * Discover refreshes before every check. Dropping the list each time
	 * made the ten-minute cache useless. A forced refresh, or a newer zypp
	 * marker (see cache_is_fresh), still solves again.
	 */
	if (force) {
		g_unlink (ATROPOS_CACHE_FILE);
		g_unlink (ATROPOS_SIZE_FILE);
	}
	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	pk_backend_job_finished (job);
}

void
pk_backend_get_details (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	GPtrArray *pkgs;
	guint i;

	(void) backend;
	pkgs = read_cache ();
	for (i = 0; package_ids != NULL && package_ids[i] != NULL; i++) {
		gchar **parts = pk_package_id_split (package_ids[i]);
		const gchar *summary = "System update";
		guint j;

		if (parts != NULL && g_strcmp0 (parts[PK_PACKAGE_ID_NAME], ATROPOS_BUNDLE_NAME) == 0) {
			GString *desc;
			guint64 download_size = read_download_size ();
			gchar *bundle_text = NULL;
			const gchar *shown = "System updates";

			desc = g_string_new ("Staged into the next snapshot by transactional-update dup. "
					      "AtropOS is immutable, so this is applied after the next reboot.");
			if (pkgs != NULL && pkgs->len > 0) {
				g_string_append (desc, "\n");
				for (j = 0; j < pkgs->len; j++) {
					AtroposPkg *pkg = g_ptr_array_index (pkgs, j);
					g_string_append_printf (desc, "%s %s (%s)\n",
								pkg->name, pkg->edition, pkg->section);
				}
				bundle_text = bundle_summary (pkgs->len, download_size);
				shown = bundle_text;
			}
			/*
			 * Discover's size column reads the installed-size field and
			 * prints Unknown when it is 0. Zypper only reports the total
			 * download, so that total is what the column shows.
			 */
			pk_backend_job_details (job, package_ids[i], shown, "unknown",
						PK_GROUP_ENUM_UNKNOWN, desc->str, NULL,
						(gulong) download_size, download_size);
			g_string_free (desc, TRUE);
			g_free (bundle_text);
			g_strfreev (parts);
			continue;
		}
		if (pkgs != NULL && parts != NULL) {
			for (j = 0; j < pkgs->len; j++) {
				AtroposPkg *pkg = g_ptr_array_index (pkgs, j);
				if (g_strcmp0 (pkg->name, parts[PK_PACKAGE_ID_NAME]) == 0) {
					summary = pkg->summary;
					break;
				}
			}
		}
		pk_backend_job_details (job, package_ids[i], summary, "unknown",
					PK_GROUP_ENUM_UNKNOWN,
					"Applied in a new snapshot by transactional-update dup. "
					"The running system stays as it is until you reboot.",
					NULL, 0, 0);
		g_strfreev (parts);
	}
	if (pkgs != NULL)
		g_ptr_array_free (pkgs, TRUE);
	pk_backend_job_finished (job);
}

void
pk_backend_get_update_detail (PkBackend *backend, PkBackendJob *job, gchar **package_ids)
{
	guint i;
	static gchar *none[] = { NULL };

	(void) backend;
	for (i = 0; package_ids != NULL && package_ids[i] != NULL; i++) {
		pk_backend_job_update_detail (job, package_ids[i], none, none, none, none, none,
					      PK_RESTART_ENUM_SYSTEM,
					      "Reboot to start the new snapshot.",
					      "Staged with transactional-update dup. The reboot is left to you.",
					      PK_UPDATE_STATE_ENUM_STABLE, "", "");
	}
	pk_backend_job_finished (job);
}

static gchar *
resolve_name (const gchar *token)
{
	gchar **parts;
	gchar *name;

	if (token == NULL || token[0] == '\0')
		return NULL;
	/* Discover sends either an RPM name or a package-id. */
	if (strchr (token, ';') == NULL)
		return g_strdup (token);
	parts = pk_package_id_split (token);
	if (parts == NULL || parts[PK_PACKAGE_ID_NAME] == NULL || parts[PK_PACKAGE_ID_NAME][0] == '\0') {
		g_strfreev (parts);
		return NULL;
	}
	name = g_strdup (parts[PK_PACKAGE_ID_NAME]);
	g_strfreev (parts);
	return name;
}

static gboolean
resolve_wants_installed (PkBitfield filters)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED) &&
	    !pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED))
		return FALSE;
	return TRUE;
}

static gboolean
resolve_wants_available (PkBitfield filters)
{
	if (pk_bitfield_contain (filters, PK_FILTER_ENUM_INSTALLED) &&
	    !pk_bitfield_contain (filters, PK_FILTER_ENUM_NOT_INSTALLED))
		return FALSE;
	return TRUE;
}

typedef struct {
	gchar *evr;
	gchar *arch;
	gchar *summary;
} AtroposInstalled;

static void
atropos_installed_free (gpointer data)
{
	AtroposInstalled *pkg = data;

	g_free (pkg->evr);
	g_free (pkg->arch);
	g_free (pkg->summary);
	g_free (pkg);
}

static void
installed_slist_free (gpointer data)
{
	g_slist_free_full (data, atropos_installed_free);
}

static GHashTable *
parse_rpm_qa (const gchar *text)
{
	GHashTable *table;
	gchar **lines;
	guint i;

	table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, installed_slist_free);
	if (text == NULL)
		return table;
	lines = g_strsplit (text, "\n", -1);
	for (i = 0; lines[i] != NULL; i++) {
		gchar **fields;
		AtroposInstalled *pkg;
		GSList *list;

		if (lines[i][0] == '\0')
			continue;
		fields = g_strsplit (lines[i], "\t", 4);
		if (fields[0] == NULL || fields[0][0] == '\0' || fields[1] == NULL || fields[2] == NULL) {
			g_strfreev (fields);
			continue;
		}
		pkg = g_new0 (AtroposInstalled, 1);
		pkg->evr = g_strdup (fields[1]);
		pkg->arch = g_strdup (fields[2]);
		pkg->summary = g_strdup (fields[3] != NULL && fields[3][0] != '\0' ? fields[3] : "Installed package");
		list = g_hash_table_lookup (table, fields[0]);
		if (list == NULL)
			g_hash_table_insert (table, g_strdup (fields[0]), g_slist_append (NULL, pkg));
		else
			list = g_slist_append (list, pkg);	/* same head; the table keeps it */
		g_strfreev (fields);
	}
	g_strfreev (lines);
	return table;
}

static gboolean
installed_has_edition (GSList *list, const gchar *edition, const gchar *arch)
{
	GSList *link;

	for (link = list; link != NULL; link = link->next) {
		AtroposInstalled *pkg = link->data;

		if (g_strcmp0 (pkg->evr, edition) == 0 && g_strcmp0 (pkg->arch, arch) == 0)
			return TRUE;
	}
	return FALSE;
}

static void
emit_id (PkBackendJob *job, PkInfoEnum info, const gchar *name, const gchar *evr,
	 const gchar *arch, const gchar *data, const gchar *summary)
{
	gchar *package_id;

	package_id = pk_package_id_build (name, evr, arch, data);
	pk_backend_job_package (job, info, package_id, summary != NULL ? summary : "");
	g_free (package_id);
}

/*
 * Discover calls Resolve for AppStream components it has not matched yet.
 * Missing this symbol is the "Resolve not supported by backend" error.
 * Unknown names finish successfully: this backend still refuses install
 * and remove. One rpm -qa covers every name. The update cache is optional.
 */
/*
 * The booted RPM database only changes on reboot here: updates go into the
 * next snapshot. Keep one rpm -qa until the database file changes.
 */
static GMutex atropos_rpm_lock;
static gchar *atropos_rpm_text;
static gint64 atropos_rpm_mtime;
static gint64 atropos_rpm_size;

static gchar *
cached_rpm_qa (PkBackendJob *job, gboolean *cancelled)
{
	AtroposRun rpm;
	GStatBuf st;
	gboolean have_stat;
	gchar *text = NULL;
	gchar *rpm_argv[] = {
		"/usr/bin/rpm", "--query", "--all",
		"--queryformat", "%{NAME}\t%{EVR}\t%{ARCH}\t%{SUMMARY}\n",
		NULL,
	};

	*cancelled = FALSE;
	have_stat = FALSE;
	for (guint i = 0; atropos_rpmdb_files[i] != NULL && !have_stat; i++)
		have_stat = g_stat (atropos_rpmdb_files[i], &st) == 0;
	g_mutex_lock (&atropos_rpm_lock);
	if (have_stat && atropos_rpm_text != NULL &&
	    atropos_rpm_mtime == (gint64) st.st_mtime && atropos_rpm_size == (gint64) st.st_size)
		text = g_strdup (atropos_rpm_text);
	g_mutex_unlock (&atropos_rpm_lock);
	if (text != NULL)
		return text;

	if (!run_command (job, rpm_argv, &rpm)) {
		*cancelled = rpm.cancelled;
		atropos_run_clear (&rpm);
		return NULL;
	}
	text = rpm.output;
	rpm.output = NULL;
	atropos_run_clear (&rpm);
	if (have_stat) {
		g_mutex_lock (&atropos_rpm_lock);
		g_free (atropos_rpm_text);
		atropos_rpm_text = g_strdup (text);
		atropos_rpm_mtime = (gint64) st.st_mtime;
		atropos_rpm_size = (gint64) st.st_size;
		g_mutex_unlock (&atropos_rpm_lock);
	}
	return text;
}

static void
resolve_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	AtroposJob *aj = user_data;
	PkBitfield filters = aj->filters;
	gchar **packages = aj->ids;
	GHashTable *installed = NULL;
	GHashTable *seen;
	GPtrArray *cached;
	gboolean want_installed;
	gboolean want_available;
	guint i;

	(void) params;
	want_installed = resolve_wants_installed (filters);
	want_available = resolve_wants_available (filters);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	if (packages == NULL || packages[0] == NULL) {
		pk_backend_job_finished (job);
		return;
	}

	cached = read_cache ();
	if (want_installed) {
		gboolean cancelled = FALSE;
		gchar *text = cached_rpm_qa (job, &cancelled);

		if (cancelled) {
			fail_job (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
			if (cached != NULL)
				g_ptr_array_free (cached, TRUE);
			return;
		}
		/* If rpm failed, still answer from the update cache. */
		if (text != NULL)
			installed = parse_rpm_qa (text);
		g_free (text);
	}

	seen = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	for (i = 0; packages[i] != NULL; i++) {
		gchar *name = resolve_name (packages[i]);
		GSList *link;
		guint j;

		if (name == NULL)
			continue;
		if (g_hash_table_lookup (seen, name) != NULL) {
			g_free (name);
			continue;
		}
		g_hash_table_insert (seen, g_strdup (name), GINT_TO_POINTER (1));

		if (want_installed && installed != NULL) {
			for (link = g_hash_table_lookup (installed, name); link != NULL; link = link->next) {
				AtroposInstalled *pkg = link->data;

				emit_id (job, PK_INFO_ENUM_INSTALLED, name, pkg->evr, pkg->arch,
					 "installed", pkg->summary);
			}
		}
		if (want_available && cached != NULL) {
			for (j = 0; j < cached->len; j++) {
				AtroposPkg *pkg = g_ptr_array_index (cached, j);

				if (g_strcmp0 (pkg->name, name) != 0)
					continue;
				if (installed != NULL &&
				    installed_has_edition (g_hash_table_lookup (installed, name),
							   pkg->edition, pkg->arch))
					continue;
				emit_id (job, PK_INFO_ENUM_NORMAL, name, pkg->edition, pkg->arch,
					 pkg->repo, pkg->summary);
			}
		}
		g_free (name);
	}

	g_hash_table_destroy (seen);
	if (installed != NULL)
		g_hash_table_destroy (installed);
	if (cached != NULL)
		g_ptr_array_free (cached, TRUE);
	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
	start_thread (job, resolve_thread, backend, filters, 0, packages);
}

static void
update_thread (PkBackendJob *job, GVariant *params, gpointer user_data)
{
	AtroposJob *aj = user_data;
	gchar **package_ids = aj->ids;
	AtroposRun dup;
	/* No "reboot" argument. The new snapshot stays staged until the user reboots. */
	gchar *argv[] = {
		"/usr/sbin/transactional-update", "--non-interactive", "--no-selfupdate",
		"--drop-if-no-change", "dup", NULL,
	};
	gchar *owned_id = NULL;
	const gchar *package_id = NULL;

	(void) params;
	if (package_ids != NULL && package_ids[0] != NULL)
		package_id = package_ids[0];
	else {
		owned_id = bundle_package_id ();
		package_id = owned_id;
	}

	if (atropos_reboot_pending ()) {
		finish_reboot_required (job, package_id);
		g_free (owned_id);
		return;
	}

	/*
	 * Report the row being updated. A transaction with no package
	 * signal reads as "There are no updates available" to pkcon, which
	 * also exits 5, even when the update was staged.
	 */
	pk_backend_job_package (job, PK_INFO_ENUM_UPDATING, package_id,
				"Installing the update into a new snapshot");
	pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	if (!run_command (job, argv, &dup)) {
		fail_command (job, &dup);
		atropos_run_clear (&dup);
		g_free (owned_id);
		return;
	}
	/*
	 * transactional-update writes /run/reboot-needed when the new snapshot
	 * is the default. Either that file or the C-locale success line means
	 * the update is staged. Nothing here calls reboot.
	 */
	if (atropos_reboot_pending () ||
	    (dup.output != NULL && strstr (dup.output, "New default snapshot") != NULL))
		finish_reboot_required (job, package_id);
	else
		pk_backend_job_finished (job);
	g_free (owned_id);
	atropos_run_clear (&dup);
}

void
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	/* Discover's simulate call finishes with no packages; the next call is the real dup. */
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_finished (job);
		return;
	}
	start_thread (job, update_thread, backend, 0, transaction_flags, package_ids);
}

void
pk_backend_install_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	(void) backend;
	(void) transaction_flags;
	(void) package_ids;
	fail_job (job, PK_ERROR_ENUM_NOT_SUPPORTED,
		  "AtropOS installs applications from Flathub. System updates use transactional-update dup.");
}

void
pk_backend_remove_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids, gboolean allow_deps, gboolean autoremove)
{
	(void) backend;
	(void) transaction_flags;
	(void) package_ids;
	(void) allow_deps;
	(void) autoremove;
	fail_job (job, PK_ERROR_ENUM_NOT_SUPPORTED,
		  "AtropOS does not remove RPM packages from Discover.");
}

#ifdef ATROPOS_PK_TEST
/* packagekitd provides these. The parser test only needs them to link. */
void pk_backend_job_finished (PkBackendJob *job) { (void) job; }
void pk_backend_job_package (PkBackendJob *job, PkInfoEnum info, const gchar *package_id, const gchar *summary) { (void) job; (void) info; (void) package_id; (void) summary; }
void pk_backend_job_details (PkBackendJob *job, const gchar *package_id, const gchar *summary, const gchar *license, PkGroupEnum group, const gchar *description, const gchar *url, gulong size, guint64 download_size) { (void) job; (void) package_id; (void) summary; (void) license; (void) group; (void) description; (void) url; (void) size; (void) download_size; }
void pk_backend_job_update_detail (PkBackendJob *job, const gchar *package_id, gchar **updates, gchar **obsoletes, gchar **vendor_urls, gchar **bugzilla_urls, gchar **cve_urls, PkRestartEnum restart, const gchar *update_text, const gchar *changelog, PkUpdateStateEnum state, const gchar *issued, const gchar *updated) { (void) job; (void) package_id; (void) updates; (void) obsoletes; (void) vendor_urls; (void) bugzilla_urls; (void) cve_urls; (void) restart; (void) update_text; (void) changelog; (void) state; (void) issued; (void) updated; }
void pk_backend_job_require_restart (PkBackendJob *job, PkRestartEnum restart, const gchar *package_id) { (void) job; (void) restart; (void) package_id; }
guint pk_backend_job_get_uid (PkBackendJob *job) { (void) job; return 0; }
static PkErrorEnum test_error_code;
static gchar *test_error_text;
void pk_backend_job_error_code (PkBackendJob *job, PkErrorEnum code, const gchar *details, ...)
{
	va_list args;

	(void) job;
	test_error_code = code;
	g_free (test_error_text);
	va_start (args, details);
	test_error_text = g_strdup_vprintf (details, args);
	va_end (args);
}
void pk_backend_job_set_status (PkBackendJob *job, PkStatusEnum status) { (void) job; (void) status; }
static gboolean test_allow_cancel = TRUE;
static guint test_percentage;
void pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel) { (void) job; test_allow_cancel = allow_cancel; }
void pk_backend_job_set_percentage (PkBackendJob *job, guint percentage) { (void) job; test_percentage = percentage; }
gboolean pk_backend_job_thread_create (PkBackendJob *job, PkBackendJobThreadFunc func, gpointer user_data, GDestroyNotify destroy_func) { func (job, NULL, user_data); if (destroy_func != NULL) destroy_func (user_data); return TRUE; }
static gboolean test_online = TRUE;
gboolean pk_backend_is_online (PkBackend *backend) { (void) backend; return test_online; }
gboolean pk_backend_job_is_cancelled (PkBackendJob *job) { (void) job; return FALSE; }

int
main (void)
{
	const gchar *xml =
		"<?xml version='1.0'?>\n"
		"<stream>\n"
		"<message type=\"error\">solver broke</message>\n"
		"<install-summary download-size=\"18651390\" packages-to-change=\"2\">\n"
		"<to-upgrade>\n"
		"<solvable type=\"package\" name=\"bash\" edition=\"5.2-1.1\" arch=\"x86_64\" repository=\"repo-oss\" summary=\"The shell\"/>\n"
		"<solvable type=\"pattern\" name=\"base\" edition=\"1\" arch=\"x86_64\" repository=\"repo-oss\" summary=\"skip me\"/>\n"
		"</to-upgrade>\n"
		"<to-remove>\n"
		"<solvable type=\"package\" name=\"oldpkg\" edition=\"1-1\" arch=\"noarch\" repository=\"repo-oss\" summary=\"Gone\"/>\n"
		"</to-remove>\n"
		"</install-summary>\n"
		"</stream>\n";
	gchar *error_text = NULL;
	guint64 download_size = 1;
	GPtrArray *pkgs = parse_zypper_xml (xml, &error_text, &download_size);

	if (pkgs == NULL || pkgs->len != 2 || download_size != 18651390) {
		g_printerr ("expected 2 packages, error=%s\n", error_text != NULL ? error_text : "none");
		return 1;
	}
	if (error_text != NULL) {
		g_printerr ("unexpected error with packages present: %s\n", error_text);
		return 1;
	}
	g_ptr_array_free (pkgs, TRUE);

	{
		gchar *blank = atropos_error_text ("", 4);
		gchar *spaces = atropos_error_text (" \n\t", 9);
		gchar *kept = atropos_error_text ("mirror down", 4);

		if (strstr (blank, "no output") == NULL || strstr (blank, "exit 4") == NULL ||
		    strstr (spaces, "exit 9") == NULL || strcmp (kept, "mirror down") != 0) {
			g_printerr ("blank error text mapping failed\n");
			return 1;
		}
		g_free (blank);
		g_free (spaces);
		g_free (kept);
	}
	{
		const gchar *log =
			"Preloading: zsh-autosuggestions-0.7.1-1.1.noarch.rpm "
			"[Error: \"The requested URL returned error: 404\", trying next mirror.]\n"
			"Preloading: zsh-autosuggestions-0.7.1-1.1.noarch.rpm [done]\n"
			"Preloading: patterns-atropos-base-0.4.0-23.1.x86_64.rpm "
			"[The requested URL returned error: 404]\n"
			"Preloading: zsh-completions-0.36.0-1.1.noarch.rpm "
			"[The requested URL returned error: 404]\n"
			"ERROR: zypper failed with exit code 4!\n"
			"Use '--interactive' for manual problem resolution.\n"
			"Removing snapshot #5...\n";
		gchar *missing = missing_download_text (log);

		if (missing == NULL || strstr (missing, "HTTP 404") == NULL ||
		    strstr (missing, "patterns-atropos-base-0.4.0-23.1.x86_64.rpm") == NULL ||
		    strstr (missing, "zsh-completions-0.36.0-1.1.noarch.rpm") == NULL ||
		    strstr (missing, "zsh-autosuggestions") != NULL ||
		    strlen (missing) > 2000) {
			g_printerr ("404 error text mapping failed: %s\n",
				    missing != NULL ? missing : "none");
			return 1;
		}
		if (zypper_error_code (4, log) != PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED) {
			g_printerr ("404 exit code mapping failed\n");
			return 1;
		}
		g_free (missing);
		if (missing_download_text ("mirror down\n") != NULL) {
			g_printerr ("404 error text matched a normal failure\n");
			return 1;
		}
	}
	{
		const gchar *log =
			"In cache libavutil60-8.1.2-5.1.2.1.sr20261001.x86_64.rpm (16/32), 377.6 KiB\n"
			"In cache libcjson1-1.7.19-3.1.2.1.sr20261001.x86_64.rpm (17/32),  41.3 KiB\n"
			"Verifying [....done]\n"
			"Preparing [...done]\n"
			"Problem occurred during or after installation or removal of packages:\n"
			"Executing the transaction failed because of the following problems:\n"
			"    file /etc/sudoers.d from install of atychia-0.19-9.1.x86_64 "
			"conflicts with file from package sudo-1.9.17p2-4.1.x86_64\n"
			"Please see the above error message for a hint.\n"
			"Application returned with exit status 8.\n"
			"ERROR: zypper dup on /.snapshots/3/snapshot failed with exit code 8!\n"
			"Found plugin \"/usr/lib/tukit/plugins/10-sdbootutil.tukit\"\n"
			"Discarding snapshot 3.\n"
			"transactional-update finished\n";
		gchar *shown = friendly_failure_text (log, 1);
		gchar *status_only;
		gchar *noise;
		const gchar *cache_only =
			"In cache foo.rpm (1/1), 1 KiB\n"
			"Discarding snapshot 3.\n"
			"transactional-update finished\n";
		const gchar *dep =
			"Computing distribution upgrade...\n"
			"Problem: the to be installed atychia-0.19-12.1.x86_64 requires "
			"libfoo.so.1()(64bit), but this requirement cannot be provided\n"
			"  not installable providers: libfoo.so.1()(64bit)\n"
			" Solution 1: do not install atychia-0.19-12.1.x86_64\n"
			" Solution 2: break atychia-0.19-12.1.x86_64 by ignoring some of its dependencies\n"
			"Choose from above solutions by number or cancel [1/2/c/d/?] (c): c\n"
			"ERROR: zypper dup on /.snapshots/7/snapshot failed with exit code 4!\n"
			"Discarding snapshot 7.\n";

		if (shown == NULL || strstr (shown, "/etc/sudoers.d") == NULL ||
		    strstr (shown, "is owned by both") == NULL ||
		    strstr (shown, "atychia-0.19-9.1.x86_64") == NULL ||
		    strstr (shown, "sudo-1.9.17p2-4.1.x86_64") == NULL ||
		    strstr (shown, "was not installed") == NULL ||
		    strstr (shown, "nothing was changed") == NULL ||
		    strstr (shown, "In cache") != NULL ||
		    strstr (shown, "Discarding snapshot") != NULL ||
		    strstr (shown, "exit status") != NULL ||
		    strlen (shown) > 2000) {
			g_printerr ("conflict error text mapping failed: %s\n",
				    shown != NULL ? shown : "none");
			g_free (shown);
			return 1;
		}
		g_free (shown);
		if (zypper_error_code (8, log) != PK_ERROR_ENUM_PACKAGE_CONFLICTS ||
		    zypper_error_code (1, log) != PK_ERROR_ENUM_PACKAGE_CONFLICTS) {
			g_printerr ("conflict exit code mapping failed\n");
			return 1;
		}
		if (friendly_failure_text ("mirror down\n", 4) != NULL) {
			g_printerr ("short error was rewritten\n");
			return 1;
		}
		noise = friendly_failure_text (cache_only, 1);
		if (noise == NULL || strstr (noise, "was not installed") == NULL ||
		    strstr (noise, "Nothing was changed") == NULL ||
		    strstr (noise, "In cache") != NULL ||
		    strstr (noise, "Discarding") != NULL) {
			g_printerr ("noise-only error text mapping failed: %s\n",
				    noise != NULL ? noise : "none");
			g_free (noise);
			return 1;
		}
		g_free (noise);
		status_only = friendly_failure_text (
			"In cache foo.rpm (1/1), 1 KiB\n"
			"Application returned with exit status 8.\n"
			"Discarding snapshot 3.\n", 1);
		if (status_only == NULL || strstr (status_only, "exit 8") == NULL ||
		    strstr (status_only, "Nothing was changed") == NULL ||
		    strstr (status_only, "In cache") != NULL ||
		    strstr (status_only, "Application returned") != NULL) {
			g_printerr ("exit-status fallback failed: %s\n",
				    status_only != NULL ? status_only : "none");
			g_free (status_only);
			return 1;
		}
		g_free (status_only);
		shown = friendly_failure_text (dep, 1);
		if (shown == NULL || strstr (shown, "dependency could not be satisfied") == NULL ||
		    strstr (shown, "atychia-0.19-12.1.x86_64 requires") == NULL ||
		    strstr (shown, "Nothing available provides libfoo") == NULL ||
		    strstr (shown, "the to be installed") != NULL ||
		    strstr (shown, "Solution") != NULL ||
		    strstr (shown, "ignoring") != NULL ||
		    strstr (shown, "Discarding") != NULL ||
		    strlen (shown) > 2000) {
			g_printerr ("dependency error text mapping failed: %s\n",
				    shown != NULL ? shown : "none");
			g_free (shown);
			return 1;
		}
		g_free (shown);
		if (zypper_error_code (1, dep) != PK_ERROR_ENUM_DEP_RESOLUTION_FAILED) {
			g_printerr ("dependency exit code mapping failed\n");
			return 1;
		}
		shown = friendly_failure_text (
			"In cache foo.rpm (1/1), 1 KiB\n"
			"ERROR: snapper create failed: no space in metadata\n"
			"Discarding snapshot 9.\n", 1);
		if (shown == NULL || strstr (shown, "snapper create failed") == NULL ||
		    strstr (shown, "In cache") != NULL ||
		    strstr (shown, "did not give a clearer reason") != NULL ||
		    strstr (shown, "Discarding") != NULL) {
			g_printerr ("ERROR line was dropped: %s\n",
				    shown != NULL ? shown : "none");
			g_free (shown);
			return 1;
		}
		g_free (shown);
		if (zypper_error_code (4, "mirror down") != PK_ERROR_ENUM_TRANSACTION_ERROR) {
			g_printerr ("generic exit 4 was reported as a dependency error\n");
			return 1;
		}
	}
	{
		/* The 2026-11-05 Discover failure, from transactional-update.log. */
		const gchar *log =
			"transactional-update 6.1.3 started\n"
			"Separate /var detected.\n"
			"Using snapshot 6 as base for new snapshot 7.\n"
			"ID: 7\n"
			"Calling zypper --gpg-auto-import-keys --no-cd dup\n"
			"Executing `zypper -R /tmp/transactional-update-pyoXnO --gpg-auto-import-keys --no-cd dup -y --auto-agree-with-product-licenses`:\n"
			"Refreshing service 'openSUSE'.\n"
			"Loading repository data...\n"
			"Reading installed packages...\n"
			"Computing distribution upgrade...\n"
			"The following 8 packages are going to be upgraded:\n"
			"curl libcurl4 libheif1 libimobiledevice-1_0-6 libjpeg8 libnfs16 nvidia-open-driver-G07-signed-kmp-longterm nvidia-open-driver-G07-signed-kmp-meta\n"
			"8 packages to upgrade, 3 new.\n"
			"Package download size:   234.5 MiB\n"
			"Continue? [y/n/v/...? shows all options] (y): y\n"
			"Do you agree with the terms of the license? [yes/no] (no): no\n"
			"Aborting installation due to the need for license confirmation.\n"
			"Please restart the operation in interactive mode and confirm your agreement with required licenses, or use the --auto-agree-with-licenses option.\n"
			"Application returned with exit status 4.\n"
			"ERROR: zypper --gpg-auto-import-keys --no-cd dup on /.snapshots/7/snapshot failed with exit code 4!\n"
			"Use '--interactive' for manual problem resolution.\n"
			"Removing snapshot #7...\n"
			"Discarding snapshot 7.\n"
			"transactional-update finished\n";
		gchar *shown = friendly_failure_text (log, 1);
		gchar *tail;
		GString *long_log = g_string_new (NULL);
		guint k;

		if (shown == NULL || strstr (shown, "autoAgreeWithLicenses") == NULL ||
		    strstr (shown, "Separate /var") != NULL ||
		    zypper_error_code (1, log) != PK_ERROR_ENUM_NO_LICENSE_AGREEMENT) {
			g_printerr ("license abort mapping failed: %s\n", shown != NULL ? shown : "none");
			g_free (shown);
			return 1;
		}
		g_free (shown);
		for (k = 0; k < 20; k++)
			g_string_append_printf (long_log, "package line %u\n", k);
		g_string_append (long_log, "ERROR: snapper close failed: the real reason\n"
					   "Discarding snapshot 7.\n");
		tail = plain_reason_text (long_log->str);
		if (tail == NULL || strstr (tail, "the real reason") == NULL ||
		    strstr (tail, "package line 0\n") != NULL ||
		    strstr (tail, "earlier lines not shown") == NULL) {
			g_printerr ("tail selection failed: %s\n", tail != NULL ? tail : "none");
			g_free (tail);
			g_string_free (long_log, TRUE);
			return 1;
		}
		g_free (tail);
		g_string_free (long_log, TRUE);
	}
	{
		/* Progress only moves forward, and Cancel stops after zypper returns. */
		AtroposRun run;
		guint after_download;

		memset (&run, 0, sizeof run);
		note_output (NULL, &run, "Retrieving: a (repo-oss) (1/2), 1 KiB\nRetrieving: b (repo-oss) (2/", FALSE);
		note_output (NULL, &run, "2), 1 KiB\n", FALSE);
		after_download = test_percentage;
		note_output (NULL, &run, "( 1/2) Installing: a [..done]\n", FALSE);
		if (after_download != 50 || test_percentage != 75 || !test_allow_cancel) {
			g_printerr ("progress mapping failed: %u then %u\n", after_download, test_percentage);
			return 1;
		}
		note_output (NULL, &run, "( 2/2) Installing: b [..done]\nApplication returned with exit status 0.", TRUE);
		if (test_percentage != 100 || test_allow_cancel || !run.committed) {
			g_printerr ("commit detection failed\n");
			return 1;
		}
		g_string_free (run.line, TRUE);
	}
	{
		/* A missing repomd.xml is a repository problem, not an unpublished package. */
		const gchar *repo = "File './repodata/repomd.xml' not found on medium 'http://download.opensuse.org/slowroll/repo/oss/'\n";
		gchar *reason = last_reason_line ("Retrieving repository 'x' metadata [error]\n"
						  "Repository 'x' is invalid.\n"
						  "Use '--interactive' for manual problem resolution.\n");

		if (zypper_error_code (4, repo) != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
		    missing_download_text (repo) != NULL ||
		    g_strcmp0 (reason, "Repository 'x' is invalid.") != 0) {
			g_printerr ("repository failure mapping failed: %s\n", reason != NULL ? reason : "none");
			g_free (reason);
			return 1;
		}
		g_free (reason);
	}
	{
		/* A check without a network says so in one sentence. */
		AtroposRun run;

		memset (&run, 0, sizeof run);
		run.exit_code = 4;
		run.stderr_text = g_strdup ("Retrieving repository 'repo-oss' metadata [error]\n"
					    "Download (curl) error for 'http://download.opensuse.org/slowroll/repo/oss/repodata/repomd.xml':\n"
					    "Error code: Connection failed\n"
					    "Error message: Could not resolve host: download.opensuse.org\n");
		test_online = FALSE;
		fail_check (NULL, NULL, &run);
		if (test_error_code != PK_ERROR_ENUM_NO_NETWORK ||
		    strstr (test_error_text, "not connected to the internet") == NULL) {
			g_printerr ("offline check failed: %s\n", test_error_text);
			return 1;
		}
		test_online = TRUE;
		fail_check (NULL, NULL, &run);
		if (test_error_code != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
		    strstr (test_error_text, "servers could not be reached") == NULL ||
		    strstr (test_error_text, "Could not resolve host") == NULL) {
			g_printerr ("unreachable server check failed: %s\n", test_error_text);
			return 1;
		}
		atropos_run_clear (&run);
	}
	if (zypper_error_code (7, NULL) != PK_ERROR_ENUM_CANNOT_GET_LOCK ||
	    zypper_error_code (106, NULL) != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
	    zypper_error_code (4, "Timeout exceeded when accessing the mirror") != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
	    zypper_error_code (8, "the following conflict was found") != PK_ERROR_ENUM_PACKAGE_CONFLICTS ||
	    zypper_error_code (5, NULL) != PK_ERROR_ENUM_NOT_AUTHORIZED ||
	    zypper_error_code (1, NULL) != PK_ERROR_ENUM_INTERNAL_ERROR ||
	    zypper_error_code (1, "No space left on device") != PK_ERROR_ENUM_NO_SPACE_ON_DEVICE) {
		g_printerr ("exit code mapping failed\n");
		return 1;
	}
	{
		GHashTable *table = parse_rpm_qa ("bash\t5.2-1.1\tx86_64\tThe shell\n"
						  "bash\t5.2-1.1\ti586\tThe shell 32\n"
						  "\n"
						  "broken-line\n"
						  "onlyname\tone\n");
		GSList *hits = g_hash_table_lookup (table, "bash");
		PkBitfield installed_only = pk_bitfield_value (PK_FILTER_ENUM_INSTALLED);
		PkBitfield available_only = pk_bitfield_value (PK_FILTER_ENUM_NOT_INSTALLED);

		if (hits == NULL || g_slist_length (hits) != 2) {
			g_printerr ("rpm parse failed\n");
			return 1;
		}
		if (!installed_has_edition (hits, "5.2-1.1", "x86_64") ||
		    installed_has_edition (hits, "5.2-1.1", "noarch")) {
			g_printerr ("edition match failed\n");
			return 1;
		}
		g_hash_table_destroy (table);
		if (!resolve_wants_installed (0) || !resolve_wants_available (0) ||
		    !resolve_wants_installed (installed_only) ||
		    resolve_wants_available (installed_only) ||
		    resolve_wants_installed (available_only) ||
		    !resolve_wants_available (available_only)) {
			g_printerr ("filter match failed\n");
			return 1;
		}
		{
			gchar *plain = resolve_name ("bash");
			if (g_strcmp0 (plain, "bash") != 0) {
				g_printerr ("plain name failed\n");
				g_free (plain);
				return 1;
			}
			g_free (plain);
		}
		{
			gchar *from_id = resolve_name ("bash;5.2-1.1;x86_64;installed");
			if (g_strcmp0 (from_id, "bash") != 0) {
				g_printerr ("package-id name failed\n");
				g_free (from_id);
				return 1;
			}
			g_free (from_id);
		}
	}
	if (strstr (ATROPOS_NOTIFY_BODY, "will be applied after you reboot") == NULL ||
	    g_strcmp0 (ATROPOS_NOTIFY_APP, "AtropOS") != 0 ||
	    g_strcmp0 (ATROPOS_NOTIFY_EXPIRE, "@i -1") != 0 ||
	    g_strcmp0 (ATROPOS_NOTIFY_REPLACES, "@u 0") != 0) {
		g_printerr ("notification body failed\n");
		return 1;
	}
	g_print ("parse ok\n");
	return 0;
}
#endif
