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

static gboolean
run_command (PkBackendJob *job, gchar **argv, AtroposRun *run)
{
	GPid pid = 0;
	gint fd = -1;
	GError *error = NULL;
	GString *buf;
	gboolean eof = FALSE;
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
					       child_set_pgroup, NULL, &pid, NULL, &fd, NULL, &error)) {
			run->error = g_strdup (error->message);
			g_error_free (error);
			g_strfreev (env);
			return FALSE;
		}
		g_strfreev (env);
	}
	if (setpgid (pid, pid) != 0 && errno != EACCES)
		g_debug ("setpgid: %s", g_strerror (errno));

	buf = g_string_new (NULL);
	while (!eof || !exited) {
		if (pk_backend_job_is_cancelled (job)) {
			run->cancelled = TRUE;
			if (cancel_waits == 0)
				kill (-pid, SIGTERM);
			else if (cancel_waits == 50)
				kill (-pid, SIGKILL);
			cancel_waits++;
		}

		if (!eof) {
			struct pollfd pfd;
			gchar tmp[4096];
			ssize_t n;

			pfd.fd = fd;
			pfd.events = POLLIN;
			pfd.revents = 0;
			if (poll (&pfd, 1, 200) > 0 && (pfd.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
				n = read (fd, tmp, sizeof tmp - 1);
				if (n > 0) {
					tmp[n] = '\0';
					g_string_append_len (buf, tmp, n);
					note_progress (job, tmp);
				} else if (n == 0 || errno != EINTR) {
					eof = TRUE;
				}
			}
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
	close (fd);
	g_spawn_close_pid (pid);
	run->output = g_string_free (buf, FALSE);
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
static PkErrorEnum
zypper_error_code (gint exit_code, const gchar *output)
{
	gchar *lower;
	PkErrorEnum code;

	if (exit_code < 0)
		return PK_ERROR_ENUM_FAILED_INITIALIZATION;

	switch (exit_code) {
	case 1: /* ZYPPER_EXIT_ERR_BUG */
	case 2: /* ZYPPER_EXIT_ERR_SYNTAX */
	case 3: /* ZYPPER_EXIT_ERR_INVALID_ARGS */
		return PK_ERROR_ENUM_INTERNAL_ERROR;
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
	if (text_has (lower, "is locked") || text_has (lower, "zypp lock"))
		code = PK_ERROR_ENUM_CANNOT_GET_LOCK;
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
	else if (exit_code == 8) /* ZYPPER_EXIT_ERR_COMMIT */
		code = PK_ERROR_ENUM_DEP_RESOLUTION_FAILED;
	else if (exit_code > 128)
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
	if (zypper_error_code (7, NULL) != PK_ERROR_ENUM_CANNOT_GET_LOCK ||
	    zypper_error_code (106, NULL) != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
	    zypper_error_code (4, "Timeout exceeded when accessing the mirror") != PK_ERROR_ENUM_REPO_NOT_AVAILABLE ||
	    zypper_error_code (8, "the following conflict was found") != PK_ERROR_ENUM_PACKAGE_CONFLICTS ||
	    zypper_error_code (5, NULL) != PK_ERROR_ENUM_NOT_AUTHORIZED) {
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
