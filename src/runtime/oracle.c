/*
 * --original: run the shipping Civ3Conquests.exe's own machine code, natively,
 * inside this host, as the reference the recompilation is compared against.
 *
 * The image is mapped at its base exactly as for the lifted run, but .text is
 * made executable and nothing is lifted: the IAT is bound to real functions,
 * and the entry point is called on a thread of its own. Everything the host
 * does to jgl.dll (record.c: the 16-bit colour shim, headless, the WM_PAINT
 * stand-in, --record) and to input (input.c) applies unchanged, because it
 * works through jgl's import table and window. The exe's own imports get the
 * same shims the lifted run has, written here as ordinary stdcall functions.
 *
 * So one script, two machines: the same save, the same keys, and the game's
 * own autosaves to compare (tools/oracle.py, docs/testing.md).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "image_loader.h"
#include "record.h"

#define BASE 0x00400000u

static const char* g_exe_path;      /* what the game is told it is */
static const char* g_cmdline;
static int g_hl, g_nosteam_o;

static HMODULE WINAPI o_GetModuleHandleA(LPCSTR n) {
    return n ? GetModuleHandleA(n) : (HMODULE)(uintptr_t)BASE;
}

static DWORD WINAPI o_GetModuleFileNameA(HMODULE h, LPSTR out, DWORD size) {
    if (h && h != (HMODULE)(uintptr_t)BASE) return GetModuleFileNameA(h, out, size);
    DWORD n = (DWORD)strlen(g_exe_path);
    if (!size) return 0;
    if (n >= size) n = size - 1;
    memcpy(out, g_exe_path, n);
    out[n] = 0;
    return n;
}

static LPSTR WINAPI o_GetCommandLineA(void) { return (LPSTR)g_cmdline; }

static HMODULE WINAPI o_LoadLibraryA(LPCSTR name) {
    HMODULE m = LoadLibraryA(name);
    fprintf(stderr, "[load] %s -> %p\n", name ? name : "(null)", (void*)m);
    if (m) record_hook_module(m, g_hl);
    return m;
}

static BOOL WINAPI o_FreeLibrary(HMODULE m) { (void)m; return TRUE; }     /* host.c, shim_FreeLibrary */

static void WINAPI o_ExitProcess(UINT code) {
    fprintf(stderr, "[game] ExitProcess(%u)\n", code);
    InterlockedExchange(&g_exiting, 1);
    record_close();
    fflush(stderr);
    ExitProcess(code);
}

static HANDLE WINAPI o_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL own, LPCSTR n) {
    char name[96];
    if (n && g_hl) _snprintf(name, sizeof name - 1, "%s-%lu", n, GetCurrentProcessId()), name[sizeof name - 1] = 0;
    return CreateMutexA(sa, own, n && g_hl ? name : n);
}

static int WINAPI o_MessageBoxA(HWND h, LPCSTR text, LPCSTR cap, UINT type) {
    (void)h;
    fprintf(stderr, "[messagebox] %s: %s\n", cap ? cap : "", text ? text : "");
    return (type & MB_TYPEMASK) == MB_YESNO || (type & MB_TYPEMASK) == MB_YESNOCANCEL ? IDNO : IDOK;
}

static BOOL WINAPI o_ShowWindow(HWND h, int cmd) { (void)h; (void)cmd; return FALSE; }

static HWND WINAPI o_CreateWindowExA(DWORD ex, LPCSTR cls, LPCSTR name, DWORD style, int x, int y, int w,
                                     int h, HWND parent, HMENU menu, HINSTANCE inst, LPVOID p) {
    HWND r = CreateWindowExA(ex, cls, name, g_hl ? style & ~WS_VISIBLE : style, x, y, w, h, parent, menu, inst, p);
    if (r && !parent) g_game_hwnd = r;
    return r;
}

static int __cdecl o_SteamAPI_Init(void) { return 0; }
static SHORT WINAPI o_GetKeyState(int vk) { return input_key_state(vk); }

typedef struct { const char* name; void* fn; int headless_only; } oshim_t;
static oshim_t g_oshims[] = {
    { "GetModuleHandleA", (void*)o_GetModuleHandleA, 0 },
    { "GetKeyState", (void*)o_GetKeyState, 0 },
    { "GetAsyncKeyState", (void*)o_GetKeyState, 0 },
    { "GetModuleFileNameA", (void*)o_GetModuleFileNameA, 0 },
    { "GetCommandLineA", (void*)o_GetCommandLineA, 0 },
    { "LoadLibraryA", (void*)o_LoadLibraryA, 0 },
    { "FreeLibrary", (void*)o_FreeLibrary, 0 },
    { "ExitProcess", (void*)o_ExitProcess, 0 },
    { "CreateWindowExA", (void*)o_CreateWindowExA, 0 },
    { "CreateMutexA", (void*)o_CreateMutexA, 1 },
    { "MessageBoxA", (void*)o_MessageBoxA, 1 },
    { "ShowWindow", (void*)o_ShowWindow, 1 },
};

static int bind(void) {
    BYTE* b = (BYTE*)(uintptr_t)BASE;
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(b + ((IMAGE_DOS_HEADER*)b)->e_lfanew);
    IMAGE_DATA_DIRECTORY dd = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    int native = 0, shimmed = 0, missing = 0;
    for (IMAGE_IMPORT_DESCRIPTOR* d = (IMAGE_IMPORT_DESCRIPTOR*)(b + dd.VirtualAddress); d->Name; d++) {
        HMODULE h = LoadLibraryA((const char*)(b + d->Name));
        uint32_t* ilt = (uint32_t*)(b + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        uint32_t* iat = (uint32_t*)(b + d->FirstThunk);
        for (; *ilt; ilt++, iat++) {
            int by_ord = (*ilt & 0x80000000u) != 0;
            const char* nm = by_ord ? (const char*)(uintptr_t)(*ilt & 0xFFFF) : (const char*)(b + *ilt + 2);
            void* fn = NULL;
            for (int i = 0; !by_ord && i < (int)(sizeof g_oshims / sizeof g_oshims[0]); i++)
                if ((g_hl || !g_oshims[i].headless_only) && !strcmp(nm, g_oshims[i].name)) fn = g_oshims[i].fn;
            if (!by_ord && g_nosteam_o && !strcmp(nm, "SteamAPI_Init")) fn = (void*)o_SteamAPI_Init;
            if (fn) shimmed++;
            else if (h && (fn = (void*)GetProcAddress(h, nm))) native++;
            else missing++;
            *iat = (uint32_t)(uintptr_t)fn;
        }
    }
    printf("[original] imports: %d native, %d shimmed, %d unresolved\n", native, shimmed, missing);
    return missing;
}

/* First-chance exceptions, logged and passed on: the original may handle its
 * own (SEH), so this only reports. */
static LONG CALLBACK note(EXCEPTION_POINTERS* ep) {
    static volatile LONG n;
    EXCEPTION_RECORD* r = ep->ExceptionRecord;
    if ((r->ExceptionCode & 0xF0000000u) == 0xC0000000u && InterlockedIncrement(&n) <= 8) {
        CONTEXT* c = ep->ContextRecord;
        fprintf(stderr, "[original] exception 0x%08lX at %p (%s 0x%08lX) eax=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX esp=%08lX\n",
                r->ExceptionCode, r->ExceptionAddress,
                r->NumberParameters >= 2 && r->ExceptionInformation[0] == 1 ? "write" : "read",
                r->NumberParameters >= 2 ? (unsigned long)r->ExceptionInformation[1] : 0,
                c->Eax, c->Ecx, c->Edx, c->Esi, c->Edi, c->Esp);
        fflush(stderr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI entry(LPVOID va) {
    ((void (*)(void))va)();               /* the CRT's entry: ends in ExitProcess */
    return 0;
}

int oracle_run(const char* exe_full, const char* guest_exe, const char* cmdline, int headless, int nosteam,
               void (*before_entry)(void)) {
    g_exe_path = guest_exe;
    g_cmdline = cmdline;
    g_hl = headless;
    g_nosteam_o = nosteam;
    uint32_t span = recomp_load_image(exe_full, BASE);
    if (!span) { fprintf(stderr, "cannot map %s at 0x%08X\n", exe_full, BASE); return 1; }
    IMAGE_NT_HEADERS32* nt = (IMAGE_NT_HEADERS32*)(uintptr_t)(BASE + ((IMAGE_DOS_HEADER*)(uintptr_t)BASE)->e_lfanew);
    IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, s++) {
        DWORD old;
        if (s->Characteristics & IMAGE_SCN_MEM_EXECUTE)
            VirtualProtect((void*)(uintptr_t)(BASE + s->VirtualAddress), s->Misc.VirtualSize, PAGE_EXECUTE_READ, &old);
    }
    printf("[original] mapped %s: 0x%08X-0x%08X, running the original machine code\n", exe_full, BASE, BASE + span);
    if (bind()) return 1;
    uint32_t ep = BASE + nt->OptionalHeader.AddressOfEntryPoint;
    AddVectoredExceptionHandler(1, note);
    before_entry();
    printf("  entering 0x%08X (original)\n\n", ep);
    fflush(stdout);
    HANDLE t = CreateThread(NULL, 16u << 20, entry, (LPVOID)(uintptr_t)ep, 0, NULL);
    WaitForSingleObject(t, INFINITE);
    return 0;
}
