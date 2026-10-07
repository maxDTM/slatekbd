/* instance.c - pidfile + flock single-instance handling and CLI signalling. */
#include "instance.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

void instance_path(const char *instance, char *buf, int len)
{
	const char *dir = getenv("XDG_RUNTIME_DIR");
	char fallback[64];
	if (!dir || !*dir) {
		snprintf(fallback, sizeof fallback, "/tmp/slatekbd-%d", (int)getuid());
		mkdir(fallback, 0700);
		/* /tmp is shared: only use the directory if it is really ours and private */
		struct stat st;
		if (lstat(fallback, &st) < 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid() || (st.st_mode & 077)) {
			snprintf(buf, len, "/nonexistent/slatekbd.pid"); /* open fails -> single-instance check disabled */
			return;
		}
		dir = fallback;
	}
	if (instance && *instance) snprintf(buf, len, "%s/slatekbd-%s.pid", dir, instance);
	else snprintf(buf, len, "%s/slatekbd.pid", dir);
}

static pid_t read_pid(int fd)
{
	char buf[32] = { 0 };
	ssize_t n = pread(fd, buf, sizeof buf - 1, 0);
	if (n <= 0) return 0;
	long v = strtol(buf, NULL, 10);
	return v > 0 ? (pid_t)v : 0;
}

int instance_acquire(const char *instance, pid_t *owner)
{
	char path[512];
	instance_path(instance, path, sizeof path);
	for (int tries = 0; tries < 5; tries++) {
		int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0644);
		if (fd < 0) return -2;
		if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
			if (owner) *owner = read_pid(fd);
			close(fd);
			return errno == EWOULDBLOCK ? -1 : -2;
		}
		/* The previous owner may have unlinked the file between our open and flock
		 * (instance_release): then we locked an orphaned inode; start over. */
		struct stat fs, ps;
		if (fstat(fd, &fs) < 0 || stat(path, &ps) < 0 || fs.st_ino != ps.st_ino || fs.st_dev != ps.st_dev) {
			close(fd);
			continue;
		}
		char buf[32];
		int n = snprintf(buf, sizeof buf, "%d\n", (int)getpid());
		if (ftruncate(fd, 0) < 0 || pwrite(fd, buf, n, 0) != n) {
			close(fd);
			return -2;
		}
		return fd;
	}
	return -2;
}

void instance_release(const char *instance, int fd)
{
	if (fd < 0) return;
	char path[512];
	instance_path(instance, path, sizeof path);
	/* Only remove the path if it still names the file we hold locked. */
	struct stat fs, ps;
	if (fstat(fd, &fs) == 0 && stat(path, &ps) == 0 && fs.st_ino == ps.st_ino && fs.st_dev == ps.st_dev)
		unlink(path);
	close(fd);
}

pid_t instance_running(const char *instance)
{
	char path[512];
	instance_path(instance, path, sizeof path);
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) return 0;
	pid_t pid = 0;
	if (flock(fd, LOCK_SH | LOCK_NB) < 0) {
		if (errno == EWOULDBLOCK) pid = read_pid(fd); /* the owner holds the lock */
	} else {
		flock(fd, LOCK_UN); /* stale pidfile */
	}
	close(fd);
	return pid;
}

int instance_signal(const char *instance, int sig)
{
	pid_t pid = instance_running(instance);
	if (pid <= 0) return 1;
	if (kill(pid, sig) < 0) return errno == ESRCH ? 1 : -1;
	return 0;
}

int instance_spawn_detached(char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (setsid() < 0) _exit(1);
		pid_t p2 = fork();
		if (p2 < 0) _exit(1);
		if (p2 > 0) _exit(0);
		sigset_t none;
		sigemptyset(&none);
		sigprocmask(SIG_SETMASK, &none, NULL);
		signal(SIGPIPE, SIG_DFL);
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2) close(fd);
		}
		char self[1024];
		ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
		if (n > 0) {
			self[n] = 0;
			execv(self, argv);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}
