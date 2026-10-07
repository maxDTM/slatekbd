/* lock.h - lockscreen detection (hyprland_lock_notifier_v1 + CLI hooks) and lock mode. */
#ifndef SLATEKBD_LOCK_H
#define SLATEKBD_LOCK_H

#include <stdbool.h>

struct app;

/* Subscribe to the Hyprland lock notifier if bound. */
void lock_init(struct app *a);
void lock_fini(struct app *a);
/* Enter / leave lock mode (idempotent). */
void lock_enter(struct app *a);
void lock_exit(struct app *a);
/* Called when the lock-rule hyprctl child exits: performs the one pending recreate. */
void lock_rule_done(struct app *a);
/* T_LOCK_RECREATE fallback: waits for the child, then behaves like lock_rule_done. */
void lock_rule_timeout(struct app *a);

#endif
