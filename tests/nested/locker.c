// locker SECS: minimal ext-session-lock-v1 client for the nested tests. Locks the session,
// shows a grey lock surface on every output, logs wl_keyboard keys it receives (the lock
// surface has keyboard focus), then unlocks after SECS seconds.
#define _GNU_SOURCE
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "ext-session-lock-v1-client.h"

static struct wl_compositor *comp; static struct wl_shm *shm; static struct wl_seat *seat;
static struct ext_session_lock_manager_v1 *mgr; static struct ext_session_lock_v1 *lock;
static struct wl_output *outs[8]; static int nout; static int locked;

static void reg(void *d, struct wl_registry *r, uint32_t n, const char *i, uint32_t v)
{
	(void)d;(void)v;
	if (!strcmp(i, wl_compositor_interface.name)) comp = wl_registry_bind(r, n, &wl_compositor_interface, 4);
	else if (!strcmp(i, wl_shm_interface.name)) shm = wl_registry_bind(r, n, &wl_shm_interface, 1);
	else if (!strcmp(i, wl_seat_interface.name)) seat = wl_registry_bind(r, n, &wl_seat_interface, 5);
	else if (!strcmp(i, wl_output_interface.name) && nout < 8) outs[nout++] = wl_registry_bind(r, n, &wl_output_interface, 1);
	else if (!strcmp(i, ext_session_lock_manager_v1_interface.name)) mgr = wl_registry_bind(r, n, &ext_session_lock_manager_v1_interface, 1);
}
static void unreg(void *d, struct wl_registry *r, uint32_t n) { (void)d;(void)r;(void)n; }
static const struct wl_registry_listener rl = { reg, unreg };

static void draw(struct wl_surface *s, int w, int h)
{
	int stride = w * 4, size = stride * h; int fd = memfd_create("lock", MFD_CLOEXEC);
	if (ftruncate(fd, size) < 0) return;
	uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	for (int k = 0; k < w * h; k++) px[k] = 0xff505a64;
	munmap(px, size);
	struct wl_shm_pool *p = wl_shm_create_pool(shm, fd, size);
	struct wl_buffer *b = wl_shm_pool_create_buffer(p, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(p); close(fd);
	wl_surface_attach(s, b, 0, 0); wl_surface_damage_buffer(s, 0, 0, w, h); wl_surface_commit(s);
}
static void ls_conf(void *d, struct ext_session_lock_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h)
{ ext_session_lock_surface_v1_ack_configure(ls, serial); draw(d, (int)w, (int)h); }
static const struct ext_session_lock_surface_v1_listener lsl = { ls_conf };

static void l_locked(void *d, struct ext_session_lock_v1 *l) { (void)d;(void)l; locked = 1; printf("locked\n"); fflush(stdout); }
static void l_finished(void *d, struct ext_session_lock_v1 *l) { (void)d;(void)l; printf("lock finished (denied)\n"); exit(1); }
static const struct ext_session_lock_v1_listener ll = { l_locked, l_finished };

static void k_map(void *d, struct wl_keyboard *k, uint32_t f, int32_t fd, uint32_t sz) { (void)d;(void)k;(void)f;(void)sz; close(fd); }
static void k_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *x, struct wl_array *a) { (void)d;(void)k;(void)s;(void)x;(void)a; printf("keyboard enter\n"); fflush(stdout); }
static void k_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *x) { (void)d;(void)k;(void)s;(void)x; }
static void k_key(void *d, struct wl_keyboard *k, uint32_t s, uint32_t t, uint32_t key, uint32_t st)
{ (void)d;(void)k;(void)s;(void)t; printf("lock key %u %s\n", key, st ? "down" : "up"); fflush(stdout); }
static void k_mods(void *d, struct wl_keyboard *k, uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t g) { (void)d;(void)k;(void)s;(void)a;(void)b;(void)c;(void)g; }
static void k_rep(void *d, struct wl_keyboard *k, int32_t r, int32_t dl) { (void)d;(void)k;(void)r;(void)dl; }
static const struct wl_keyboard_listener kl = { k_map, k_enter, k_leave, k_key, k_mods, k_rep };

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 10;
	struct wl_display *dpy = wl_display_connect(NULL); if (!dpy) return 1;
	wl_registry_add_listener(wl_display_get_registry(dpy), &rl, NULL); wl_display_roundtrip(dpy);
	if (!mgr || !comp || !shm) { fprintf(stderr, "no ext_session_lock_manager_v1\n"); return 1; }
	struct wl_keyboard *kb = wl_seat_get_keyboard(seat); wl_keyboard_add_listener(kb, &kl, NULL);
	lock = ext_session_lock_manager_v1_lock(mgr); ext_session_lock_v1_add_listener(lock, &ll, NULL);
	for (int i = 0; i < nout; i++) {
		struct wl_surface *s = wl_compositor_create_surface(comp);
		struct ext_session_lock_surface_v1 *ls = ext_session_lock_v1_get_lock_surface(lock, s, outs[i]);
		ext_session_lock_surface_v1_add_listener(ls, &lsl, s);
	}
	time_t end = time(NULL) + secs;
	while (time(NULL) < end) {
		wl_display_flush(dpy); struct pollfd p = { wl_display_get_fd(dpy), POLLIN, 0 };
		if (poll(&p, 1, 200) > 0 && wl_display_dispatch(dpy) < 0) return 1;
	}
	if (locked) ext_session_lock_v1_unlock_and_destroy(lock); else ext_session_lock_v1_destroy(lock);
	wl_display_roundtrip(dpy); printf("unlocked\n"); return 0;
}
