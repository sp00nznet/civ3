#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "game_lock.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
int civ3_game_lock(const char *directory) {
    char path[PATH_MAX];
    int n = snprintf(path, sizeof path, "%s/.civ3-native.lock", directory);
    if (n < 0 || (size_t)n >= sizeof path) { errno = ENAMETOOLONG; return -1; }
    int fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return -1;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1) {
        close(fd); errno = EINVAL; return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB)) { int saved = errno; close(fd); errno = saved; return -1; }
    /* A stale PID is harmless: ownership is the kernel lock, not this text. */
    if (ftruncate(fd, 0) || dprintf(fd, "%ld\n", (long)getpid()) < 0) {
        int saved = errno; close(fd); errno = saved; return -1;
    }
    return fd;
}
