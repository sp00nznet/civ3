/* Shell-free, bounded-lifetime FFmpeg pipe. The caller ignores SIGPIPE. */
#pragma once
#include <stddef.h>
#include <sys/types.h>
typedef struct { int fd; pid_t pid; } civ3_record_pipe;
int civ3_record_start(civ3_record_pipe *pipe, const char *path, int width, int height,
                      unsigned fps, int overwrite);
int civ3_record_write(civ3_record_pipe *pipe, const void *data, size_t length, unsigned timeout_ms);
/* Returns the encoder exit status, 124 on timeout, or -1 on a host error. */
int civ3_record_finish(civ3_record_pipe *pipe, unsigned timeout_ms);
