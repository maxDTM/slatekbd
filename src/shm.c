/* shm.c - double-buffered shm buffers. */
#include "shm.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static void buffer_release(void *data, struct wl_buffer *wl)
{
	(void)wl;
	struct shm_buffer *b = data;
	b->busy = false;
}

static const struct wl_buffer_listener buffer_listener = { .release = buffer_release };

struct shm_buffer *shm_buffer_create(struct wl_shm *shm, int width, int height)
{
	if (width <= 0 || height <= 0) return NULL;
	struct shm_buffer *b = calloc(1, sizeof(*b));
	if (!b) return NULL;
	b->width = width;
	b->height = height;
	b->stride = width * 4;
	b->size = (size_t)b->stride * (size_t)height;
	int fd = memfd_create("slatekbd-shm", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0) goto fail;
	int r;
	do r = ftruncate(fd, (off_t)b->size); while (r < 0 && errno == EINTR);
	if (r < 0) {
		close(fd);
		goto fail;
	}
	b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (b->data == MAP_FAILED) {
		b->data = NULL;
		close(fd);
		goto fail;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)b->size);
	b->wl = wl_shm_pool_create_buffer(pool, 0, width, height, b->stride, WL_SHM_FORMAT_ARGB8888);
	wl_buffer_add_listener(b->wl, &buffer_listener, b);
	wl_shm_pool_destroy(pool);
	close(fd);
	return b;
fail:
	free(b);
	return NULL;
}

void shm_buffer_destroy(struct shm_buffer *b)
{
	if (!b) return;
	if (b->wl) wl_buffer_destroy(b->wl);
	if (b->data) munmap(b->data, b->size);
	free(b);
}

struct shm_buffer *shm_get_buffer(struct wl_shm *shm, struct shm_buffer *pair[2], int width, int height)
{
	for (int i = 0; i < 2; i++) {
		struct shm_buffer *b = pair[i];
		if (b && b->busy) continue;
		if (!b || b->width != width || b->height != height) {
			shm_buffer_destroy(b);
			pair[i] = shm_buffer_create(shm, width, height);
		}
		return pair[i];
	}
	return NULL;
}
