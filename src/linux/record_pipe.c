#define _POSIX_C_SOURCE 200809L
#include "record_pipe.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;
static int64_t milliseconds(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now)) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static void pause_ms(unsigned ms) {
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&t, &t) && errno == EINTR) {}
}
/* Keep pipe descriptors away from stdin/stdout/stderr, even with closed stdio. */
static int high_fd(int fd) {
    int result = fcntl(fd, F_DUPFD_CLOEXEC, 3), saved = errno;
    close(fd); errno = saved; return result;
}
int civ3_record_start(civ3_record_pipe *p, const char *path, int w, int h, unsigned fps, int overwrite) {
    if (!p || !path || !*path || w < 1 || h < 1 || !fps || fps > 240) { errno = EINVAL; return -1; }
    p->fd = -1; p->pid = -1;
    int fds[2];
    if (pipe(fds)) return -1;
    fds[0] = high_fd(fds[0]); fds[1] = high_fd(fds[1]);
    if (fds[0] < 0 || fds[1] < 0) {
        int saved = errno; if (fds[0] >= 0) close(fds[0]); if (fds[1] >= 0) close(fds[1]); errno = saved; return -1;
    }
    int flags = fcntl(fds[1], F_GETFL);
    if (flags < 0 || fcntl(fds[1], F_SETFL, flags | O_NONBLOCK)) {
        int saved = errno; close(fds[0]); close(fds[1]); errno = saved; return -1;
    }
    char dimensions[48], rate[16];
    snprintf(dimensions, sizeof dimensions, "%dx%d", w, h);
    snprintf(rate, sizeof rate, "%u", fps);
    char *args[] = {"ffmpeg", "-nostdin", overwrite ? "-y" : "-n", "-loglevel", "error",
        "-f", "rawvideo", "-pix_fmt", "bgr0", "-s", dimensions, "-r", rate, "-i", "pipe:0",
        "-an", "-c:v", "libx264", "-pix_fmt", "yuv420p", (char *)path, NULL};
    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (!error) {
        error = posix_spawn_file_actions_adddup2(&actions, fds[0], STDIN_FILENO);
        if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[0]);
        if (!error) error = posix_spawn_file_actions_addclose(&actions, fds[1]);
        if (!error) error = posix_spawnp(&p->pid, "ffmpeg", &actions, NULL, args, environ);
        posix_spawn_file_actions_destroy(&actions);
    }
    close(fds[0]);
    if (error) { close(fds[1]); p->pid = -1; errno = error; return -1; }
    p->fd = fds[1]; return 0;
}
int civ3_record_write(civ3_record_pipe *p, const void *data, size_t length, unsigned timeout_ms) {
    if (!p || p->fd < 0 || !data || !timeout_ms) { errno = EINVAL; return -1; }
    int64_t now = milliseconds();
    if (now < 0) return -1;
    int64_t deadline = now + timeout_ms;
    const unsigned char *bytes = data;
    while (length) {
        now = milliseconds();
        if (now < 0) return -1;
        if (now >= deadline) { errno = ETIMEDOUT; return -1; }
        struct pollfd fd = {p->fd, POLLOUT, 0};
        int wait = (int)(deadline - now);
        int ready = poll(&fd, 1, wait > 100 ? 100 : wait);
        if (ready < 0) { if (errno == EINTR) continue; return -1; }
        if (!ready) continue;
        if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) { errno = EPIPE; return -1; }
        ssize_t wrote = write(p->fd, bytes, length);
        if (wrote < 0) { if (errno == EINTR || errno == EAGAIN) continue; return -1; }
        if (!wrote) { errno = EIO; return -1; }
        bytes += wrote; length -= (size_t)wrote;
    }
    return 0;
}
static int wait_child(pid_t pid, int *status, unsigned timeout_ms) {
    int64_t now = milliseconds();
    if (now < 0) return -1;
    int64_t deadline = now + timeout_ms;
    for (;;) {
        pid_t done = waitpid(pid, status, WNOHANG);
        if (done == pid) return 1;
        if (done < 0 && errno != EINTR) return -1;
        now = milliseconds();
        if (now < 0) return -1;
        if (now >= deadline) return 0;
        pause_ms(10);
    }
}
int civ3_record_finish(civ3_record_pipe *p, unsigned timeout_ms) {
    if (!p) { errno = EINVAL; return -1; }
    int failed = 0, status = 0;
    if (p->fd >= 0) { failed = close(p->fd) != 0; p->fd = -1; }
    if (p->pid <= 0) return failed ? -1 : 0;
    pid_t pid = p->pid; p->pid = -1;
    int done = wait_child(pid, &status, timeout_ms);
    if (done < 0) return -1;
    if (!done) {
        kill(pid, SIGTERM);
        if (wait_child(pid, &status, 250) == 0) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }
        return 124;
    }
    if (failed) return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : -1;
}
