/* wayland.h - registry, outputs, seat input, layer surface lifecycle, scale, frame pacing. */
#ifndef SLATEKBD_WAYLAND_H
#define SLATEKBD_WAYLAND_H

#include <stdbool.h>

struct app;

int wl_init(struct app *a);
void wl_fini(struct app *a);
/* Create the layer surface (show). */
int surface_create(struct app *a);
/* Destroy the layer surface (hide). */
void surface_destroy(struct app *a);
/* Push size / exclusive zone / input region changes and commit. */
void surface_apply_size(struct app *a);
/* Draw and commit a frame if dirty and allowed. */
void surface_maybe_draw(struct app *a);
/* Output that the layer surface should be created on (may be NULL = compositor choice). */
struct output *wl_pick_output(const struct app *a);

#endif
