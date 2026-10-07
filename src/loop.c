/* loop.c - signalfd dispatch, one timerfd driving a small deadline table, poll loop. */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "rotate.h"
#include "wayland.h"

static void block_signals(sigset_t *set)
{
	sigemptyset(set);
	sigaddset(set, SIGUSR1);
	sigaddset(set, SIGUSR2);
	sigaddset(set, SIGRTMIN);
	sigaddset(set, SIGRTMIN + 1);
	sigaddset(set, SIGRTMIN + 2);
	sigaddset(set, SIGTERM);
	sigaddset(set, SIGINT);
	sigaddset(set, SIGHUP);
	sigaddset(set, SIGCHLD);
	sigprocmask(SIG_BLOCK, set, NULL);
}

int loop_init(struct app *a)
{
	sigset_t set;
	block_signals(&set);
	signal(SIGPIPE, SIG_IGN);
	a->sig_fd = signalfd(-1, &set, SFD_CLOEXEC | SFD_NONBLOCK);
	if (a->sig_fd < 0) return -1;
	a->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (a->timer_fd < 0) return -1;
	memset(a->timer_deadline, 0, sizeof a->timer_deadline);
	return 0;
}

void loop_fini(struct app *a)
{
	if (a->sig_fd >= 0) close(a->sig_fd);
	if (a->timer_fd >= 0) close(a->timer_fd);
	a->sig_fd = a->timer_fd = -1;
}

static uint64_t mono_ms64(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void rearm(struct app *a)
{
	if (a->timer_fd < 0) return;
	uint64_t next = 0;
	for (int i = 0; i < T_COUNT; i++)
		if (a->timer_deadline[i] && (!next || a->timer_deadline[i] < next)) next = a->timer_deadline[i];
	struct itimerspec its;
	memset(&its, 0, sizeof its);
	if (next) {
		its.it_value.tv_sec = (time_t)(next / 1000);
		its.it_value.tv_nsec = (long)(next % 1000) * 1000000L;
		if (!its.it_value.tv_sec && !its.it_value.tv_nsec) its.it_value.tv_nsec = 1;
	}
	timerfd_settime(a->timer_fd, TFD_TIMER_ABSTIME, &its, NULL);
}

void timer_arm(struct app *a, enum timer_id id, uint32_t ms)
{
	a->timer_deadline[id] = mono_ms64() + ms;
	if (!a->timer_deadline[id]) a->timer_deadline[id] = 1;
	rearm(a);
}

void timer_cancel(struct app *a, enum timer_id id)
{
	if (!a->timer_deadline[id]) return;
	a->timer_deadline[id] = 0;
	rearm(a);
}

bool timer_armed(const struct app *a, enum timer_id id)
{
	return a->timer_deadline[id] != 0;
}

void loop_handle_timers(struct app *a)
{
	uint64_t exp;
	while (read(a->timer_fd, &exp, sizeof exp) > 0) {}
	uint64_t now = mono_ms64();
	for (int i = 0; i < T_COUNT; i++) {
		if (a->timer_deadline[i] && a->timer_deadline[i] <= now) {
			a->timer_deadline[i] = 0;
			app_on_timer(a, (enum timer_id)i);
		}
	}
	rearm(a);
}

void loop_handle_signals(struct app *a)
{
	struct signalfd_siginfo si;
	while (read(a->sig_fd, &si, sizeof si) == sizeof si) {
		int s = (int)si.ssi_signo;
		if (s == SIGUSR1) {
			LOG(a, "signal: hide");
			app_manual(a, OV_HIDE);
		} else if (s == SIGUSR2) {
			LOG(a, "signal: show");
			app_manual(a, OV_SHOW);
		} else if (s == SIGRTMIN) {
			LOG(a, "signal: toggle");
			app_toggle(a);
		} else if (s == SIGRTMIN + 1) {
			LOG(a, "signal: lock");
			app_lock(a, true);
		} else if (s == SIGRTMIN + 2) {
			LOG(a, "signal: unlock");
			app_lock(a, false);
		} else if (s == SIGTERM || s == SIGINT || s == SIGHUP) {
			LOG(a, "signal %d: quitting", s);
			app_quit(a);
		} else if (s == SIGCHLD) {
			rotate_reap(a);
		}
	}
}

int loop_run(struct app *a)
{
	struct wl_display *d = a->display;
	while (!a->quit) {
		/* deferred work */
		if (a->need_recreate) {
			a->need_recreate = false;
			if (a->surface) {
				app_release_all(a);
				surface_destroy(a);
			}
			if (a->visible) surface_create(a);
		}
		surface_maybe_draw(a);

		while (wl_display_prepare_read(d) != 0) {
			if (wl_display_dispatch_pending(d) < 0) return -1;
		}
		struct pollfd fds[3] = {
			{ wl_display_get_fd(d), POLLIN, 0 },
			{ a->sig_fd, POLLIN, 0 },
			{ a->timer_fd, POLLIN, 0 },
		};
		if (wl_display_flush(d) < 0 && errno == EAGAIN) fds[0].events |= POLLOUT;
		int r = poll(fds, 3, -1);
		if (r < 0) {
			wl_display_cancel_read(d);
			if (errno == EINTR) continue;
			return -1;
		}
		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(d) < 0) return -1;
		} else {
			wl_display_cancel_read(d);
		}
		if (fds[0].revents & (POLLERR | POLLHUP)) {
			WARN(a, "Wayland connection lost");
			return -1;
		}
		if (wl_display_dispatch_pending(d) < 0) {
			WARN(a, "Wayland dispatch error");
			return -1;
		}
		if (fds[1].revents & POLLIN) loop_handle_signals(a);
		if (fds[2].revents & POLLIN) loop_handle_timers(a);
	}
	return 0;
}
