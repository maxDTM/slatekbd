/* keymap.c - xkb context/keymap (evdev/pc105/us), keymap upload via memfd,
 * zwp_virtual_keyboard_v1 key/modifier output, xkb_state mirror. */
#include "keymap.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include "app.h"
#include "virtual-keyboard-unstable-v1-client-protocol.h"

#define DOWNSET_BITS 512

struct keymap {
	struct app *app;
	struct xkb_context *ctx;
	struct xkb_keymap *map;
	struct xkb_state *state;
	struct zwp_virtual_keyboard_v1 *vk;
	uint64_t down[DOWNSET_BITS / 64];
	uint32_t last_mods[4];
	bool mods_sent;
};

struct keymap *keymap_create(struct app *a)
{
	struct keymap *k = calloc(1, sizeof(*k));
	if (!k) return NULL;
	k->app = a;
	k->ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!k->ctx) goto fail;
	struct xkb_rule_names names = { .rules = "evdev", .model = "pc105", .layout = "us", .variant = "", .options = "" };
	k->map = xkb_keymap_new_from_names(k->ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!k->map) goto fail;
	k->state = xkb_state_new(k->map);
	if (!k->state) goto fail;
	return k;
fail:
	keymap_destroy(k);
	return NULL;
}

void keymap_destroy(struct keymap *k)
{
	if (!k) return;
	if (k->vk) zwp_virtual_keyboard_v1_destroy(k->vk);
	if (k->state) xkb_state_unref(k->state);
	if (k->map) xkb_keymap_unref(k->map);
	if (k->ctx) xkb_context_unref(k->ctx);
	free(k);
}

int keymap_attach(struct keymap *k)
{
	struct app *a = k->app;
	if (!a->vk_mgr || !a->seat) return -1;
	char *str = xkb_keymap_get_as_string(k->map, XKB_KEYMAP_FORMAT_TEXT_V1);
	if (!str) return -1;
	size_t size = strlen(str) + 1;
	int fd = memfd_create("slatekbd-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	if (fd < 0) {
		free(str);
		return -1;
	}
	size_t off = 0;
	while (off < size) {
		ssize_t w = write(fd, str + off, size - off);
		if (w < 0) {
			if (errno == EINTR) continue;
			close(fd);
			free(str);
			return -1;
		}
		off += (size_t)w;
	}
	free(str);
	fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
	k->vk = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(a->vk_mgr, a->seat);
	zwp_virtual_keyboard_v1_keymap(k->vk, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, (uint32_t)size);
	close(fd);
	return 0;
}

static void sync_mods(struct keymap *k, bool force)
{
	uint32_t m[4] = {
		xkb_state_serialize_mods(k->state, XKB_STATE_MODS_DEPRESSED),
		xkb_state_serialize_mods(k->state, XKB_STATE_MODS_LATCHED),
		xkb_state_serialize_mods(k->state, XKB_STATE_MODS_LOCKED),
		xkb_state_serialize_layout(k->state, XKB_STATE_LAYOUT_EFFECTIVE),
	};
	if (!force && k->mods_sent && !memcmp(m, k->last_mods, sizeof m)) return;
	memcpy(k->last_mods, m, sizeof m);
	k->mods_sent = true;
	if (k->vk) zwp_virtual_keyboard_v1_modifiers(k->vk, m[0], m[1], m[2], m[3]);
}

void keymap_key(struct keymap *k, uint16_t code, bool down)
{
	if (code >= DOWNSET_BITS - 8) return;
	uint64_t bit = 1ULL << (code % 64);
	bool is_down = k->down[code / 64] & bit;
	if (down == is_down && !down) return; /* never release what is not down */
	if (down && is_down) {
		/* already down (two fingers on the same modifier code): release first to keep counts sane */
		keymap_key(k, code, false);
	}
	if (down) k->down[code / 64] |= bit;
	else k->down[code / 64] &= ~bit;
	xkb_state_update_key(k->state, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
	if (k->vk) zwp_virtual_keyboard_v1_key(k->vk, now_ms(), code, down ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED);
	if (!k->app->locked) DBG(k->app, "vk key %u %s", code, down ? "down" : "up"); /* never log a password */
	sync_mods(k, false);
}

void keymap_release_all(struct keymap *k)
{
	for (int code = 0; code < DOWNSET_BITS - 8; code++)
		if (k->down[code / 64] & (1ULL << (code % 64))) keymap_key(k, (uint16_t)code, false);
	sync_mods(k, true);
}

bool keymap_caps_locked(const struct keymap *k)
{
	return xkb_state_mod_name_is_active(k->state, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED) > 0;
}

int keymap_down_count(const struct keymap *k)
{
	int n = 0;
	for (int i = 0; i < DOWNSET_BITS / 64; i++) n += __builtin_popcountll(k->down[i]);
	return n;
}
