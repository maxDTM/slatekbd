/* instance.h - single instance via a flock'd pidfile in $XDG_RUNTIME_DIR. */
#ifndef SLATEKBD_INSTANCE_H
#define SLATEKBD_INSTANCE_H

#include <sys/types.h>

void instance_path(const char *instance, char *buf, int len);
/* Take the lock. Returns the fd (kept open for the process lifetime), -1 if another
 * instance holds it (*owner set), -2 on error. */
int instance_acquire(const char *instance, pid_t *owner);
/* Unlink the pidfile (if it is still ours) and drop the lock. */
void instance_release(const char *instance, int fd);
/* pid of the running instance, or 0. */
pid_t instance_running(const char *instance);
/* Send sig to the running instance. 0 = sent, 1 = not running, -1 = error. */
int instance_signal(const char *instance, int sig);
/* Start a detached instance with extra args (double fork + setsid). 0 on success. */
int instance_spawn_detached(char *const argv[]);

#endif
