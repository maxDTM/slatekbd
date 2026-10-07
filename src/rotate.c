/* rotate.c - rotate-command chain (custom, hyprctl eval, legacy --batch keyword) and the
 * runtime lock layer rule. All commands are fork/exec'd and reaped on SIGCHLD; nothing blocks. */
#include "rotate.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "app.h"
#include "lock.h"
#include "render.h"

#define MAX_CHILDREN 8

struct child {
	pid_t pid;
	enum child_kind kind;
	int transform;
	char output[32];
};

static struct child children[MAX_CHILDREN];

pid_t spawn_argv(char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		sigset_t all;
		sigemptyset(&all);
		sigprocmask(SIG_SETMASK, &all, NULL);
		signal(SIGPIPE, SIG_DFL); /* SIG_IGN would survive execve and break pipelines in rotate_cmd */
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2) close(fd);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	return pid;
}

static bool track(pid_t pid, enum child_kind kind, int transform, const char *output)
{
	for (int i = 0; i < MAX_CHILDREN; i++) {
		if (children[i].pid == 0) {
			children[i].pid = pid;
			children[i].kind = kind;
			children[i].transform = transform;
			snprintf(children[i].output, sizeof children[i].output, "%s", output ? output : "");
			return true;
		}
	}
	return false;
}

static bool have_hyprland(void)
{
	const char *s = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	return s && *s;
}

bool rotate_available(const struct app *a)
{
	return a->cfg.rotate_cmd[0] || have_hyprland();
}

bool rotate_lock_rule_pending(void)
{
	for (int i = 0; i < MAX_CHILDREN; i++)
		if (children[i].pid && children[i].kind == CH_LOCK_RULE) return true;
	return false;
}

/* Output names come from the compositor; refuse anything that could break the quoting. */
static bool safe_name(const char *s)
{
	if (!*s) return false;
	for (; *s; s++)
		if (!((*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '-' || *s == '_' || *s == '.'))
			return false;
	return true;
}

static double current_scale(const struct app *a)
{
	if (a->frac120 > 0) return a->frac120 / 120.0;
	const struct output *o = app_ref_output(a);
	if (o && o->scale > 0) return o->scale;
	return a->scale > 0 ? a->scale : 1.0;
}

static bool run_eval(struct app *a, int t, const char *out)
{
	char lua[512];
	snprintf(lua, sizeof lua,
	         "hl.monitor({output=\"%s\", transform=%d}); "
	         "hl.config({input={touchdevice={output=\"%s\", transform=%d}, tablet={output=\"%s\", transform=%d}}})",
	         out, t, out, t, out, t);
	char *argv[] = { "hyprctl", "eval", lua, NULL };
	pid_t pid = spawn_argv(argv);
	if (pid < 0) return false;
	track(pid, CH_ROTATE_EVAL, t, out);
	LOG(a, "rotate: hyprctl eval transform %d on %s (pid %d)", t, out, (int)pid);
	return true;
}

static bool run_legacy(struct app *a, int t, const char *out)
{
	char batch[512];
	snprintf(batch, sizeof batch,
	         "keyword monitor %s,preferred,auto,%.2f,transform,%d ; keyword input:touchdevice:transform %d ; "
	         "keyword input:touchdevice:output %s",
	         out, current_scale(a), t, t, out);
	char *argv[] = { "hyprctl", "--batch", batch, NULL };
	pid_t pid = spawn_argv(argv);
	if (pid < 0) return false;
	track(pid, CH_ROTATE_LEGACY, t, out);
	LOG(a, "rotate: hyprctl --batch keyword fallback (pid %d)", (int)pid);
	return true;
}

bool rotate_apply(struct app *a, int t)
{
	if (t < 0 || t > 3) return false;
	const char *out = app_output_name(a);
	if (a->cfg.rotate_cmd[0]) {
		char cmd[1024];
		int o = 0;
		for (const char *p = a->cfg.rotate_cmd; *p && o < (int)sizeof cmd - 40; p++) {
			if (p[0] == '%' && p[1] == 'o') {
				o += snprintf(cmd + o, sizeof cmd - o, "%s", safe_name(out) ? out : "");
				p++;
			} else if (p[0] == '%' && p[1] == 't') {
				o += snprintf(cmd + o, sizeof cmd - o, "%d", t);
				p++;
			} else if (p[0] == '%' && p[1] == '%') {
				cmd[o++] = '%';
				p++;
			} else {
				cmd[o++] = *p;
			}
		}
		cmd[o] = 0;
		char *argv[] = { "/bin/sh", "-c", cmd, NULL };
		pid_t pid = spawn_argv(argv);
		if (pid < 0) return false;
		track(pid, CH_ROTATE_CUSTOM, t, out);
		LOG(a, "rotate: custom command '%s' (pid %d)", cmd, (int)pid);
		return true;
	}
	if (!have_hyprland()) {
		WARN(a, "rotate: HYPRLAND_INSTANCE_SIGNATURE unset and rotate_cmd empty");
		return false;
	}
	if (!safe_name(out)) {
		WARN(a, "rotate: refusing unsafe output name");
		return false;
	}
	return run_eval(a, t, out);
}

bool rotate_lock_rule(struct app *a)
{
	if (!have_hyprland()) return false;
	char *argv[] = { "hyprctl", "eval",
		"hl.layer_rule({name=\"slatekbd\", match={namespace=\"^slatekbd$\"}, above_lock=2})", NULL };
	pid_t pid = spawn_argv(argv);
	if (pid < 0) return false;
	track(pid, CH_LOCK_RULE, 0, NULL);
	DBG(a, "lock rule: hyprctl eval (pid %d)", (int)pid);
	return true;
}

void rotate_reap(struct app *a)
{
	for (;;) {
		int status;
		pid_t pid = waitpid(-1, &status, WNOHANG);
		if (pid <= 0) break;
		int rc = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
		for (int i = 0; i < MAX_CHILDREN; i++) {
			if (children[i].pid != pid) continue;
			struct child c = children[i];
			children[i].pid = 0;
			switch (c.kind) {
			case CH_ROTATE_EVAL:
				if (rc != 0) {
					LOG(a, "rotate: eval failed (rc %d), trying legacy keyword", rc);
					run_legacy(a, c.transform, c.output);
				}
				break;
			case CH_ROTATE_LEGACY:
			case CH_ROTATE_CUSTOM:
				if (rc != 0) {
					WARN(a, "rotate: command failed (rc %d)", rc);
					snprintf(a->sv.status, sizeof a->sv.status, "Rotate failed (rc %d)", rc);
					render_invalidate(a);
				}
				break;
			case CH_LOCK_RULE:
				if (rc != 0) DBG(a, "lock rule: hyprctl eval rc %d", rc);
				else a->lock_rule_applied = true;
				lock_rule_done(a);
				break;
			}
		}
	}
}
