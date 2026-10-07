/* keymap.h - xkb keymap, virtual keyboard output, authoritative down-set. */
#ifndef SLATEKBD_KEYMAP_H
#define SLATEKBD_KEYMAP_H

#include <stdbool.h>
#include <stdint.h>

struct app;
struct keymap;

struct keymap *keymap_create(struct app *a);
void keymap_destroy(struct keymap *k);
/* Create the virtual keyboard object and upload the keymap (needs seat + manager). */
int keymap_attach(struct keymap *k);
void keymap_key(struct keymap *k, uint16_t code, bool down);
/* Release every keycode still down, send modifiers (Caps lock preserved). */
void keymap_release_all(struct keymap *k);
bool keymap_caps_locked(const struct keymap *k);
int keymap_down_count(const struct keymap *k);

#endif
