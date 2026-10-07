/* shm.h - memfd-backed ARGB8888 wl_shm buffers with release tracking. */
#ifndef SLATEKBD_SHM_H
#define SLATEKBD_SHM_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct shm_buffer {
	struct wl_buffer *wl;
	void *data;
	size_t size;
	int width, height, stride;
	bool busy;
};

struct shm_buffer *shm_buffer_create(struct wl_shm *shm, int width, int height);
void shm_buffer_destroy(struct shm_buffer *b);
/* Return a free buffer of the given size from the pair, (re)allocating as needed. NULL if both busy. */
struct shm_buffer *shm_get_buffer(struct wl_shm *shm, struct shm_buffer *pair[2], int width, int height);

#endif
