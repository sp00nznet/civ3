/* Self-hosted fake encoder. No shell, FFmpeg, or commercial assets required. */
#define _DARWIN_C_SOURCE
#define _DEFAULT_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include "record_pipe.h"
static int failures;
#define CHECK(name, condition) do { if (!(condition)) { fprintf(stderr, "FAIL: %s\n", name); ++failures; } } while (0)
static int encoder(int argc, char **argv) {
    const char *mode = getenv("CIV3_TEST_ENCODER");
    if (mode && !strcmp(mode,"fail")) return 7;
    if (mode && !strcmp(mode,"stall")) { signal(SIGTERM,SIG_IGN); for (;;) sleep(1); }
    if (argc != 21 || strcmp(argv[2], "-n") || strcmp(argv[20], getenv("CIV3_TEST_OUTPUT"))) return 9;
    FILE *out = fopen(argv[20],"wb"); if (!out) return 10;
    char bytes[4096]; ssize_t n;
    while ((n = read(STDIN_FILENO,bytes,sizeof bytes)) > 0)
        if (fwrite(bytes,1,(size_t)n,out) != (size_t)n) return 11;
    return fclose(out) ? 12 : 0;
}
int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "-nostdin")) return encoder(argc,argv);
    signal(SIGPIPE,SIG_IGN);
    char self[4096]; if (!realpath(argv[0],self)) return 2;
    char dir[] = "/tmp/civ3-encoder.XXXXXX"; if (!mkdtemp(dir)) return 2;
    char executable[4096],output[4096];
    snprintf(executable,sizeof executable,"%s/ffmpeg",dir);
    snprintf(output,sizeof output,"%s/A player's ; $(not-a-command) [capture].mp4",dir);
    if (symlink(self,executable)) return 2;
    setenv("PATH",dir,1); setenv("CIV3_TEST_OUTPUT",output,1);
    civ3_record_pipe pipe;
    CHECK("spawn literal args", !civ3_record_start(&pipe,output,16,16,15,0));
    unsigned char frame[1024]; for (int i=0;i<1024;++i) frame[i]=(unsigned char)i;
    CHECK("write a frame", !civ3_record_write(&pipe,frame,sizeof frame,2000));
    int rc = civ3_record_finish(&pipe,2000);
    CHECK("encoder reports success", rc == 0);
    FILE *f = fopen(output,"rb"); unsigned char loaded[1024];
    CHECK("literal quoted filename exists", f != NULL);
    if (f) { CHECK("exact frame bytes", fread(loaded,1,sizeof loaded,f)==sizeof loaded && !memcmp(frame,loaded,sizeof frame)); fclose(f); }
    CHECK("close idempotent", civ3_record_finish(&pipe,100)==0);
    setenv("CIV3_TEST_ENCODER","fail",1);
    CHECK("spawn failing encoder", !civ3_record_start(&pipe,output,16,16,15,0));
    CHECK("encoder failure preserved", civ3_record_finish(&pipe,2000)==7);
    setenv("CIV3_TEST_ENCODER","stall",1);
    CHECK("spawn unresponsive encoder", !civ3_record_start(&pipe,output,16,16,15,0));
    pid_t pid = pipe.pid;
    unsigned char *big = calloc(1,4*1024*1024); if (!big) return 2;
    CHECK("blocked pipe has a deadline", civ3_record_write(&pipe,big,4*1024*1024,200)<0 && errno==ETIMEDOUT);
    free(big);
    CHECK("stalled encoder bounded shutdown", civ3_record_finish(&pipe,200)==124);
    int status; CHECK("child reaped", waitpid(pid,&status,WNOHANG)<0 && errno==ECHILD);
    unlink(executable);
    CHECK("missing FFmpeg fails", civ3_record_start(&pipe,output,16,16,15,0)<0 && errno==ENOENT);
    CHECK("zero fps rejected", civ3_record_start(&pipe,output,16,16,0,0)<0);
    unlink(output); rmdir(dir);
    if (!failures) puts("PASS: shell-free literal filenames, encoder errors, bounded writes/shutdown, child reaping");
    return failures != 0;
}
