/* rotate.h - asynchronous helper commands (display rotation, lock layer rule). */
#ifndef SLATEKBD_ROTATE_H
#define SLATEKBD_ROTATE_H

#include <stdbool.h>
#include <sys/types.h>

struct app;

enum child_kind { CH_ROTATE_CUSTOM, CH_ROTATE_EVAL, CH_ROTATE_LEGACY, CH_LOCK_RULE };

/* Rotate the output the keyboard is on to transform 0..3. Returns false if not possible. */
bool rotate_apply(struct app *a, int transform);
/* Rotation buttons usable? (Hyprland detected or rotate_cmd set) */
bool rotate_available(const struct app *a);
/* Run the above_lock layer rule via hyprctl eval (async). */
bool rotate_lock_rule(struct app *a);
/* Reap exited children (SIGCHLD) and run fallbacks. */
void rotate_reap(struct app *a);
/* Spawn argv detached-from-loop; returns pid or -1. */
pid_t spawn_argv(char *const argv[]);
/* Is a lock-rule child still running? */
bool rotate_lock_rule_pending(void);

#endif
