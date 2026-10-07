/* render.h - cairo/pango drawing of the keyboard and the settings view. */
#ifndef SLATEKBD_RENDER_H
#define SLATEKBD_RENDER_H

struct app;
struct shm_buffer;
struct render;

struct render *render_create(void);
void render_destroy(struct render *r);
/* Force the cached base layer to be redrawn on the next frame. */
void render_invalidate(struct app *a);
/* Draw one frame into buf (device px) at the given scale. */
void render_frame(struct app *a, struct shm_buffer *buf, double scale);
/* Draw the round lock/login-screen keyboard button (BTN_D x BTN_D logical). */
void render_button(struct app *a, struct shm_buffer *buf, double scale);

#endif
