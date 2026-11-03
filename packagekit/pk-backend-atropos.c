/*
 * PackageKit backend that stages AtropOS updates with transactional-update.
 *
 * Discover talks to PackageKit. This backend never commits into the running
 * snapshot. A check is zypper dup --dry-run. Update All runs
 * transactional-update dup, which leaves the booted system untouched and
 * boots the new snapshot on the next reboot.
 *
 * Package installs and removals are refused. Resolve answers from the
 * installed RPM database and the update cache so Discover can ask whether
 * a name is installed. An unknown name is not an error. Flatpak stays on
 * Discover's Flatpak backend.
 *
 * A failed update keeps both command streams. zypper writes a file
 * conflict and a dependency problem on stderr. transactional-update
 * writes the download cache and "Discarding snapshot" on stdout.
 * Discover shows the end of a long error, so the log is rewritten into
 * a short reason: the two packages that own one file, the dependency
 * nothing provides, a package file that returned HTTP 404, a setup
 * script, or a full disk. The snapshot cleanup is not the reason.
 * transactional-update exits 1 for every zypper failure. The zypper
 * code is the number in "failed with exit code".
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>
#include <packagekit-glib2/packagekit.h>

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

#define ATROPOS_CACHE_DIR "/var/cache/atropos-packagekit"
#define ATROPOS_CACHE_FILE ATROPOS_CACHE_DIR "/updates.tsv"
#define ATROPOS_CACHE_TMP ATROPOS_CACHE_FILE ".tmp"
#define ATROPOS_SIZE_FILE ATROPOS_CACHE_DIR "/download-size"
#define ATROPOS_REBOOT_FILE "/run/reboot-needed"
#define ATROPOS_PERCENTAGE_INVALID 101
/* Discover shows this single row instead of one row per RPM. */
#define ATROPOS_BUNDLE_NAME "System updates"

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
	gchar *output;
	gint exit_code;
	gboolean cancelled;
	gchar *error;
} AtroposRun;

static void
atropos_run_clear (AtroposRun *run)
{
	g_free (run->output);
	g_free (run->error);
	run->output = NULL;
	run->error = NULL;
}

static void
note_progress (PkBackendJob *job, const gchar *chunk)
{
	const gchar *p;

	if (chunk == NULL)
		return;
	if (strstr (chunk, "Retrieving") != NULL || strstr (chunk, "Downloading") != NULL)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_DOWNLOAD);
	else if (strstr (chunk, "Installing") != NULL || strstr (chunk, "Updating") != NULL)
		pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);

	for (p = chunk; (p = strchr (p, '(')) != NULL; p++) {
		guint done = 0;
		guint total = 0;
		if (sscanf (p, "(%u/%u)", &done, &total) == 2 && total > 0) {
			pk_backend_job_set_percentage (job, (done * 100) / total);
			break;
		}
	}
}

/*
 * stdout and stderr stay separate until the command exits. Mixing the
 * pipes as the bytes arrive can cut a conflict line in half with a
 * progress line. stderr is appended afterwards, so a reason Discover
 * did not rewrite is still at the end of the text.
 */
static void
read_pipe (gint fd, GString *buf, PkBackendJob *job, gboolean *eof)
{
	gchar tmp[4096];
	ssize_t n;

	n = read (fd, tmp, sizeof tmp - 1);
	if (n > 0) {
		tmp[n] = '\0';
		g_string_append_len (buf, tmp, n);
		note_progress (job, tmp);
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
	gint status = 0;
	gint cancel_waits = 0;

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

		if (pk_backend_job_is_cancelled (job)) {
			run->cancelled = TRUE;
			if (cancel_waits == 0)
				kill (-pid, SIGTERM);
			else if (cancel_waits == 50)
				kill (-pid, SIGKILL);
			cancel_waits++;
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
				read_pipe (out_fd, out_buf, job, &out_eof);
			if (err_slot >= 0 && (pfd[err_slot].revents & (POLLIN | POLLHUP | POLLERR)) != 0)
				read_pipe (err_fd, err_buf, job, &err_eof);
		}

		if (!exited) {
			pid_t got = waitpid (pid, &status, WNOHANG);
			if (got == pid) {
				exited = TRUE;
				if (WIFEXITED (status))
					run->exit_code = WEXITSTATUS (status);
				else if (WIFSIGNALED (status))
					run->exit_code = 128 + WTERMSIG (status);
			} else if (got < 0 && errno == ECHILD) {
				exited = TRUE;
			}
		}

		if (run->cancelled && exited)
			break;
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
	if (err_buf->len > 0) {
		if (out_buf->len > 0 && out_buf->str[out_buf->len - 1] != '\n')
			g_string_append_c (out_buf, '\n');
		g_string_append_len (out_buf, err_buf->str, err_buf->len);
	}
	g_string_free (err_buf, TRUE);
	run->output = g_string_free (out_buf, FALSE);
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
	if (output_has_hard_404 (output))
		code = PK_ERROR_ENUM_PACKAGE_DOWNLOAD_FAILED;
	else if (text_has (lower, "is locked") || text_has (lower, "zypp lock"))
		code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
	else if (output_has_file_conflict (output))
		code = PK_ERROR_ENUM_PACKAGE_CONFLICTS;
	else if (output_has_dep_problem (output))
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (text_has (lower, "please insert") || text_has (lower, "medium"))
		code = PK_ERROR_ENUM_MEDIA_CHANGE_REQUIRED;
	else if (text_has (lower, "no network") || text_has (lower, "network is unreachable"))
		code = PK_ERROR_ENUM_NO_NETWORK;
	else if (text_has (lower, "valid metadata not found") ||
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
	else if (reported == 4)
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
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
	gchar *tmp;

	if (g_mkdir_with_parents (ATROPOS_CACHE_DIR, 0755) != 0)
		return FALSE;
	text = g_string_new (NULL);
	for (i = 0; i < pkgs->len; i++) {
		AtroposPkg *pkg = g_ptr_array_index (pkgs, i);
		g_string_append_printf (text, "%s\t%s\t%s\t%s\t%s\t%s\n",
					pkg->section, pkg->name, pkg->edition,
					pkg->arch, pkg->repo, pkg->summary);
	}
	tmp = g_strdup (ATROPOS_CACHE_TMP);
	/*
	 * g_file_set_contents replaces the .tmp only after its own write
	 * finishes. A failed write or a failed rename must remove that .tmp,
	 * or a partial file stays in the cache directory.
	 */
	if (g_file_set_contents (tmp, text->str, text->len, NULL) &&
	    g_rename (tmp, ATROPOS_CACHE_FILE) == 0) {
		g_free (tmp);
		g_string_free (text, TRUE);
		return TRUE;
	}
	g_unlink (tmp);
	g_free (tmp);
	g_string_free (text, TRUE);
	return FALSE;
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
package_id_for (const AtroposPkg *pkg)
{
	return pk_package_id_build (pkg->name, pkg->edition, pkg->arch, pkg->repo);
}

static gchar *
bundle_package_id (void)
{
	return pk_package_id_build (ATROPOS_BUNDLE_NAME, "pending", "noarch", "slowroll");
}

/* The reboot-pending path synthesizes one row when the cache is missing. */
static gboolean
is_staged_placeholder (GPtrArray *pkgs)
{
	AtroposPkg *pkg;

	if (pkgs == NULL || pkgs->len != 1)
		return FALSE;
	pkg = g_ptr_array_index (pkgs, 0);
	return g_strcmp0 (pkg->repo, "transactional-update") == 0;
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
	if (is_staged_placeholder (pkgs)) {
		AtroposPkg *pkg = g_ptr_array_index (pkgs, 0);

		package_id = package_id_for (pkg);
		pk_backend_job_package (job, PK_INFO_ENUM_NORMAL, package_id, pkg->summary);
		g_free (package_id);
		return;
	}
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

	lower = g_ascii_strdown (line, -1);
	hard = (strstr (lower, "404") != NULL || strstr (lower, "not found on medium") != NULL) &&
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
		strstr (line, "Using 'snapper'") != NULL;
}

/* Solver menus and the transactional-update summary. Not a reason. */
static gboolean
failure_line_is_wrapper (const gchar *line)
{
	return failure_line_is_noise (line) ||
		g_str_has_prefix (line, "ERROR:") ||
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
	gchar *lower_text;

	if (text == NULL)
		return NULL;
	lower_text = g_ascii_strdown (text, -1);
	if (strstr (lower_text, "scriptlet failed") == NULL) {
		g_free (lower_text);
		return NULL;
	}
	g_free (lower_text);
	lines = g_strsplit (text, "\n", -1);
	names = g_ptr_array_new_with_free_func (g_free);
	for (i = 0; lines[i] != NULL; i++) {
		const gchar *open;
		const gchar *close;
		gchar *name;
		gchar *lower_line;

		lower_line = g_ascii_strdown (lines[i], -1);
		if (strstr (lower_line, "scriptlet failed") == NULL) {
			g_free (lower_line);
			continue;
		}
		g_free (lower_line);
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
 * A long log whose tail is the download cache used to be the whole dialog.
 * Keep a short reason instead. A one-line failure such as "mirror down"
 * is already readable, so return NULL and let that line through.
 */
static gchar *
friendly_failure_text (const gchar *text, gint exit_code)
{
	gchar *shown;
	gchar *lower;
	gint code;

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

static void
fail_job (PkBackendJob *job, PkErrorEnum code, const gchar *text)
{
	gchar *shown = atropos_error_text (text, -1);
	gsize len = strlen (shown);
	const gchar *tail = shown;

	if (len > 2000)
		tail = shown + (len - 2000);
	pk_backend_job_error_code (job, code, "%s", tail);
	pk_backend_job_finished (job);
	g_free (shown);
}

static void
fail_command (PkBackendJob *job, const AtroposRun *run)
{
	gchar *text;
	const gchar *raw = run->error != NULL ? run->error : run->output;

	if (run->cancelled) {
		fail_job (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
		return;
	}
	text = missing_download_text (raw);
	if (text == NULL)
		text = friendly_failure_text (raw, run->exit_code);
	if (text == NULL)
		text = atropos_error_text (raw, run->exit_code);
	fail_job (job, zypper_error_code (run->exit_code, raw), text);
	g_free (text);
}

static gboolean
collect_updates (PkBackendJob *job, GPtrArray **pkgs_out)
{
	AtroposRun refresh;
	AtroposRun dup;
	gchar *refresh_argv[] = {
		"/usr/bin/zypper", "--non-interactive", "--gpg-auto-import-keys", "refresh", NULL,
	};
	/*
	 * -y and --auto-agree-with-licenses are dist-upgrade options on
	 * zypper 1.14. Slowroll has no delta RPMs; do not ask for them.
	 */
	gchar *dup_argv[] = {
		"/usr/bin/zypper", "--xmlout", "--non-interactive",
		"--gpg-auto-import-keys",
		"dist-upgrade", "--dry-run", "-y", "--auto-agree-with-licenses",
		NULL,
	};
	gchar *parse_error = NULL;
	guint64 download_size = 0;
	GPtrArray *pkgs;

	pk_backend_job_set_status (job, PK_STATUS_ENUM_REFRESH_CACHE);
	if (!run_command (job, refresh_argv, &refresh)) {
		fail_command (job, &refresh);
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

static void
finish_reboot_required (PkBackendJob *job, const gchar *package_id)
{
	pk_backend_job_set_status (job, PK_STATUS_ENUM_FINISHED);
	pk_backend_job_require_restart (job, PK_RESTART_ENUM_SYSTEM, package_id);
	pk_backend_job_finished (job);
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

void
pk_backend_get_updates (PkBackend *backend, PkBackendJob *job, PkBitfield filters)
{
	GPtrArray *pkgs = NULL;

	(void) backend;
	(void) filters;
	if (atropos_reboot_pending ()) {
		pkgs = read_cache ();
		if (pkgs == NULL) {
			pkgs = g_ptr_array_new_with_free_func ((GDestroyNotify) atropos_pkg_free);
			AtroposPkg *pkg = g_new0 (AtroposPkg, 1);
			pkg->section = g_strdup ("upgrade");
			pkg->name = g_strdup ("AtropOS");
			pkg->edition = g_strdup ("staged");
			pkg->arch = g_strdup ("noarch");
			pkg->repo = g_strdup ("transactional-update");
			pkg->summary = g_strdup ("System update is staged. Reboot to start the new snapshot.");
			g_ptr_array_add (pkgs, pkg);
		}
		emit_system_updates (job, pkgs, read_download_size ());
		/*
		 * Tell Discover a reboot is waiting. This does not reboot.
		 * The user starts the staged snapshot when they choose to.
		 */
		if (pkgs->len > 0) {
			gchar *package_id;

			if (is_staged_placeholder (pkgs))
				package_id = package_id_for (g_ptr_array_index (pkgs, 0));
			else
				package_id = bundle_package_id ();
			pk_backend_job_require_restart (job, PK_RESTART_ENUM_SYSTEM, package_id);
			g_free (package_id);
		}
		g_ptr_array_free (pkgs, TRUE);
		pk_backend_job_finished (job);
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
	if (pkgs == NULL && !collect_updates (job, &pkgs))
		return;
	emit_system_updates (job, pkgs, read_download_size ());
	g_ptr_array_free (pkgs, TRUE);
	pk_backend_job_set_percentage (job, 100);
	pk_backend_job_finished (job);
}

void
pk_backend_refresh_cache (PkBackend *backend, PkBackendJob *job, gboolean force)
{
	(void) backend;
	(void) force;
	g_unlink (ATROPOS_CACHE_FILE);
	g_unlink (ATROPOS_CACHE_TMP);
	g_unlink (ATROPOS_SIZE_FILE);
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
			list = g_slist_append (list, pkg);
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
void
pk_backend_resolve (PkBackend *backend, PkBackendJob *job, PkBitfield filters, gchar **packages)
{
	AtroposRun rpm;
	GHashTable *installed = NULL;
	GHashTable *seen;
	GPtrArray *cached;
	gboolean want_installed;
	gboolean want_available;
	guint i;
	gchar *rpm_argv[] = {
		"/usr/bin/rpm", "--query", "--all",
		"--queryformat", "%{NAME}\t%{EVR}\t%{ARCH}\t%{SUMMARY}\n",
		NULL,
	};

	(void) backend;
	want_installed = resolve_wants_installed (filters);
	want_available = resolve_wants_available (filters);
	pk_backend_job_set_status (job, PK_STATUS_ENUM_QUERY);

	if (packages == NULL || packages[0] == NULL) {
		pk_backend_job_finished (job);
		return;
	}

	cached = read_cache ();
	if (want_installed) {
		memset (&rpm, 0, sizeof rpm);
		if (!run_command (job, rpm_argv, &rpm)) {
			if (rpm.cancelled) {
				fail_job (job, PK_ERROR_ENUM_TRANSACTION_CANCELLED, "Cancelled");
				atropos_run_clear (&rpm);
				if (cached != NULL)
					g_ptr_array_free (cached, TRUE);
				return;
			}
			/* rpm failed. Still answer from the cache, if any. */
		} else {
			installed = parse_rpm_qa (rpm.output);
		}
		atropos_run_clear (&rpm);
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
pk_backend_update_packages (PkBackend *backend, PkBackendJob *job, PkBitfield transaction_flags, gchar **package_ids)
{
	AtroposRun dup;
	/* No "reboot" argument. The new snapshot stays staged until the user reboots. */
	gchar *argv[] = {
		"/usr/sbin/transactional-update", "--non-interactive", "--no-selfupdate",
		"--drop-if-no-change", "dup", NULL,
	};
	const gchar *package_id = "AtropOS;staged;noarch;transactional-update";

	(void) backend;
	if (pk_bitfield_contain (transaction_flags, PK_TRANSACTION_FLAG_ENUM_SIMULATE)) {
		pk_backend_job_finished (job);
		return;
	}
	if (package_ids != NULL && package_ids[0] != NULL)
		package_id = package_ids[0];

	if (atropos_reboot_pending ()) {
		finish_reboot_required (job, package_id);
		return;
	}

	pk_backend_job_set_status (job, PK_STATUS_ENUM_UPDATE);
	if (!run_command (job, argv, &dup)) {
		fail_command (job, &dup);
		atropos_run_clear (&dup);
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
	atropos_run_clear (&dup);
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
void pk_backend_job_error_code (PkBackendJob *job, PkErrorEnum code, const gchar *details, ...) { (void) job; (void) code; (void) details; }
void pk_backend_job_set_status (PkBackendJob *job, PkStatusEnum status) { (void) job; (void) status; }
void pk_backend_job_set_allow_cancel (PkBackendJob *job, gboolean allow_cancel) { (void) job; (void) allow_cancel; }
void pk_backend_job_set_percentage (PkBackendJob *job, guint percentage) { (void) job; (void) percentage; }
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
	g_print ("parse ok\n");
	return 0;
}
#endif
