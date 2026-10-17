/* Headless capture of the game's GDI output; see record.c. */
#pragma once
#include <windows.h>

int  record_arg(int argc, char** argv, int i);   /* args consumed, 0 if not ours */
void record_resolve_path(void);                  /* call before chdir */
void record_start(void);
void record_close(void);
int  record_changed(int snapshot);               /* per mille of pixels changed since a snapshot */

/* Patch a native module's imports for headless and capture (LoadLibraryA hook). */
void record_hook_module(HMODULE m, int headless);
void record_present(void);                       /* the presenter shows the game: run it hidden, keep the shadow */
int  record_frame(uint32_t** buf, size_t* cap, int* w, int* h);   /* the shadow, for the presenter */
void record_blit32(const uint32_t* px, int w, int h, int x, int y);   /* a movie frame into the shadow */

extern HWND g_game_hwnd;
extern volatile LONG g_blits;
extern volatile LONG g_exiting;       /* the game called ExitProcess (host.c, oracle.c) */
extern volatile LONG g_menu_open;     /* input.c: the main menu has opened (the clock for scripts, --play, --record) */

/* input.c: --move / --click / --key scripts for headless runs. */
int  input_arg(int argc, char** argv, int i);
void input_start(void);
BOOL WINAPI input_GetCursorPos(POINT* p);
SHORT input_key_state(int vk);                   /* GetKeyState/GetAsyncKeyState for the guest */
void  input_live_cursor(int x, int y);           /* the presenter's mouse, in game pixels */
HCURSOR input_game_cursor(void);                 /* the cursor the game set, NULL while it hides it */
void  input_set_cursor(HCURSOR c);               /* the game's SetCursor and ShowCursor (host.c) */
int   input_show_cursor(int show);
