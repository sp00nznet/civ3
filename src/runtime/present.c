/*
 * The presenter: our own window, showing the game's picture through Direct3D 11.
 *
 * The game runs as it does headless (record.c): its window is real but
 * hidden, its display modes are a fixed list and never switched, and every
 * blit jgl.dll makes to that window also lands in the shadow bitmap. This
 * window is what the player sees. When the game has blitted, it copies the
 * shadow (record_frame), uploads it and draws it letterboxed through a scaling
 * shader; it maps the player's mouse into game coordinates (the cursor jgl
 * reads, input.c) and posts it and the keys to the game's window. The shader,
 * the device code and the settings are Red Alert 2's presenter (redalert2-recomp
 * src/runtime/present.c), which took them from Gunman's.
 *
 *   F11, Alt+Enter   borderless fullscreen on the window's monitor
 *   F12              scaling: sharp-bilinear (default), smooth, CRT, nearest, integer
 *   F10, right-click on the bars   the settings menu (scaling, bars,
 *                    fullscreen, the game's resolution); kept in civ3.ini
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "record.h"
#include "present.h"

static const char* const k_mode_names[] = { "sharp", "smooth", "crt", "nearest", "integer" };
#define NMODES 5

typedef HRESULT (WINAPI *compile_fn)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR,
                                     UINT, UINT, ID3DBlob**, ID3DBlob**);

static const char k_hlsl[] =
"Texture2D t0 : register(t0);\n"
"SamplerState s_lin : register(s0);\n"
"SamplerState s_pt : register(s1);\n"
"cbuffer C : register(b0) { float2 src; float2 dst; int mode; int p0; int p1; int p2; };\n"
"struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
"V vs(uint id : SV_VertexID) {\n"
"  V o; float2 p = float2((id << 1) & 2, id & 2);\n"
"  o.pos = float4(p * float2(2, -2) + float2(-1, 1), 0, 1); o.uv = p; return o;\n"
"}\n"
"float3 sharp(float2 uv) {\n"                       /* sharp-bilinear */
"  float2 scale = max(dst / src, 1.0);\n"
"  float2 texel = uv * src, fl = floor(texel), c = frac(texel) - 0.5;\n"
"  float2 range = 0.5 - 0.5 / scale;\n"
"  float2 f = (c - clamp(c, -range, range)) * scale + 0.5;\n"
"  return t0.Sample(s_lin, (fl + f) / src).rgb;\n"
"}\n"
"float3 crt(float2 uv, float2 px) {\n"
"  float k = saturate((dst.y / src.y - 1.5) / 1.5);\n"  /* scanlines fade in from 1.5x to 3x (moire below) */
"  float3 col = sharp(uv);\n"
"  float d = frac(uv.y * src.y) - 0.5;\n"
"  float beam = lerp(1.0, exp(-d * d * 8.0) * 1.6, k);\n"
"  uint m = (uint)px.x % 3u;\n"
"  float3 mask = m == 0u ? float3(1.25, 0.87, 0.87) : m == 1u ? float3(0.87, 1.25, 0.87)\n"
"                                                  : float3(0.87, 0.87, 1.25);\n"
"  return col * beam * lerp(float3(1, 1, 1), mask, k);\n"
"}\n"
"float3 blur(float2 uv) {\n"                         /* the bars: a soft, dark copy of the picture */
"  float3 c = 0; float r = 0.035;\n"
"  [unroll] for (int y = -2; y <= 2; y++) [unroll] for (int x = -2; x <= 2; x++)\n"
"    c += t0.SampleLevel(s_lin, uv + float2(x, y) * r, 0).rgb;\n"
"  return c / 25.0 * 0.32;\n"
"}\n"
"float4 ps(V i) : SV_Target {\n"
"  float3 c;\n"
"  if (mode == 5) return float4(blur(i.uv), 1);\n"
"  if (mode == 0) c = sharp(i.uv);\n"
"  else if (mode == 1) c = t0.Sample(s_lin, i.uv).rgb;\n"
"  else if (mode == 2) c = crt(i.uv, i.pos.xy);\n"
"  else c = t0.Sample(s_pt, i.uv).rgb;\n"
"  return float4(saturate(c), 1);\n"
"}\n";

static struct {
    HWND hwnd;
    ID3D11Device* dev;
    ID3D11DeviceContext* ctx;
    IDXGISwapChain* sc;
    ID3D11RenderTargetView* rtv;
    ID3D11VertexShader* vs;
    ID3D11PixelShader* ps;
    ID3D11SamplerState* samp[2];
    ID3D11Buffer* cb;
    ID3D11Texture2D* tex;
    ID3D11ShaderResourceView* srv;
    int tw, th;                 /* texture size */
    int cw, ch;                 /* swap chain size */
} d;

static int g_mode;                       /* index into k_mode_names */
static int g_bars = 1;                   /* the bars beside a letterboxed picture: 0 black, 1 blurred */
static volatile LONG g_gw = 1024, g_gh = 768;   /* the game's picture, last frame */
static RECT g_dst;                       /* where it is drawn in the client area */
static int g_fullscreen;
static RECT g_windowed;                  /* the window's rect before fullscreen */
static HWND g_present_hwnd;
static volatile LONG g_buttons;          /* MK_ buttons held over the picture */

typedef void* (WINAPI *set_dpi_ctx_t)(void*);
static set_dpi_ctx_t g_set_dpi_ctx;

/* ---- Direct3D 11 --------------------------------------------------------- */

static int d3d_init(HWND hw) {
    DXGI_SWAP_CHAIN_DESC sd = {0};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hw;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_10_0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &fl, 1,
                                               D3D11_SDK_VERSION, &sd, &d.sc, &d.dev, NULL, &d.ctx);
    if (FAILED(hr))       /* no usable GPU (an RDP session): WARP is still a Direct3D pipeline */
        hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &fl, 1,
                                           D3D11_SDK_VERSION, &sd, &d.sc, &d.dev, NULL, &d.ctx);
    if (FAILED(hr)) { fprintf(stderr, "[present] no Direct3D 11 device: 0x%08lX\n", hr); return 0; }
    IDXGIFactory* f = NULL;     /* Alt+Enter is ours (borderless), not DXGI's exclusive mode */
    if (SUCCEEDED(IDXGISwapChain_GetParent(d.sc, &IID_IDXGIFactory, (void**)&f))) {
        IDXGIFactory_MakeWindowAssociation(f, hw, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
        IDXGIFactory_Release(f);
    }
    HMODULE dc = LoadLibraryA("d3dcompiler_47.dll");
    compile_fn compile = dc ? (compile_fn)GetProcAddress(dc, "D3DCompile") : NULL;
    ID3DBlob *vb = NULL, *pb = NULL, *err = NULL;
    if (!compile) { fprintf(stderr, "[present] d3dcompiler_47.dll missing\n"); return 0; }
    hr = compile(k_hlsl, sizeof k_hlsl - 1, "present", NULL, NULL, "vs", "vs_4_0", 0, 0, &vb, &err);
    if (SUCCEEDED(hr))
        hr = compile(k_hlsl, sizeof k_hlsl - 1, "present", NULL, NULL, "ps", "ps_4_0", 0, 0, &pb, &err);
    if (FAILED(hr)) {
        fprintf(stderr, "[present] shader: %s\n", err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "?");
        return 0;
    }
    ID3D11Device_CreateVertexShader(d.dev, ID3D10Blob_GetBufferPointer(vb), ID3D10Blob_GetBufferSize(vb), NULL, &d.vs);
    ID3D11Device_CreatePixelShader(d.dev, ID3D10Blob_GetBufferPointer(pb), ID3D10Blob_GetBufferSize(pb), NULL, &d.ps);
    ID3D10Blob_Release(vb);
    ID3D10Blob_Release(pb);
    D3D11_SAMPLER_DESC s = {0};
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ID3D11Device_CreateSamplerState(d.dev, &s, &d.samp[0]);
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    ID3D11Device_CreateSamplerState(d.dev, &s, &d.samp[1]);
    D3D11_BUFFER_DESC b = {0};
    b.ByteWidth = 32;
    b.Usage = D3D11_USAGE_DEFAULT;
    b.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ID3D11Device_CreateBuffer(d.dev, &b, NULL, &d.cb);
    d.hwnd = hw;
    return 1;
}

/* Where a gw x gh picture goes in a cw x ch client area: centred, aspect kept,
 * and for "integer" a whole multiple (at least 1x). */
static RECT fit(int cw, int ch, int gw, int gh, int integer) {
    RECT r;
    int w, h;
    if (integer) {
        int k = cw / gw < ch / gh ? cw / gw : ch / gh;
        if (k < 1) k = 1;
        w = gw * k, h = gh * k;
    } else if ((long long)cw * gh > (long long)ch * gw) {
        h = ch, w = (int)((long long)ch * gw / gh);
    } else {
        w = cw, h = (int)((long long)cw * gh / gw);
    }
    r.left = (cw - w) / 2, r.top = (ch - h) / 2;
    r.right = r.left + w, r.bottom = r.top + h;
    return r;
}

static void draw(const uint32_t* frame, int gw, int gh) {
    RECT cr;
    GetClientRect(d.hwnd, &cr);
    int cw = cr.right > 0 ? cr.right : 1, ch = cr.bottom > 0 ? cr.bottom : 1;
    if (!d.tex || d.tw != gw || d.th != gh) {
        if (d.srv) ID3D11ShaderResourceView_Release(d.srv);
        if (d.tex) ID3D11Texture2D_Release(d.tex);
        D3D11_TEXTURE2D_DESC td = {0};
        td.Width = gw; td.Height = gh; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8X8_UNORM; td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        ID3D11Device_CreateTexture2D(d.dev, &td, NULL, &d.tex);
        ID3D11Device_CreateShaderResourceView(d.dev, (ID3D11Resource*)d.tex, NULL, &d.srv);
        d.tw = gw, d.th = gh;
        fprintf(stderr, "[present] picture %dx%d\n", gw, gh);
    }
    ID3D11DeviceContext_UpdateSubresource(d.ctx, (ID3D11Resource*)d.tex, 0, NULL, frame, gw * 4, 0);
    if (!d.rtv || cw != d.cw || ch != d.ch) {
        if (d.rtv) { ID3D11RenderTargetView_Release(d.rtv); d.rtv = NULL; }
        ID3D11DeviceContext_OMSetRenderTargets(d.ctx, 0, NULL, NULL);
        if (FAILED(IDXGISwapChain_ResizeBuffers(d.sc, 0, cw, ch, DXGI_FORMAT_UNKNOWN, 0))) return;
        ID3D11Texture2D* back = NULL;
        IDXGISwapChain_GetBuffer(d.sc, 0, &IID_ID3D11Texture2D, (void**)&back);
        ID3D11Device_CreateRenderTargetView(d.dev, (ID3D11Resource*)back, NULL, &d.rtv);
        ID3D11Texture2D_Release(back);
        d.cw = cw, d.ch = ch;
    }
    RECT r = fit(cw, ch, gw, gh, g_mode == 4);
    g_dst = r;
    struct { float src[2], dst[2]; int mode, p0, p1, p2; } c = {
        { (float)gw, (float)gh }, { (float)(r.right - r.left), (float)(r.bottom - r.top) },
        g_mode == 4 ? 3 : g_mode, 0, 0, 0 };
    ID3D11DeviceContext_UpdateSubresource(d.ctx, (ID3D11Resource*)d.cb, 0, NULL, &c, 0, 0);
    static const float black[4] = { 0, 0, 0, 1 };
    ID3D11DeviceContext_ClearRenderTargetView(d.ctx, d.rtv, black);
    ID3D11DeviceContext_OMSetRenderTargets(d.ctx, 1, &d.rtv, NULL);
    ID3D11DeviceContext_IASetPrimitiveTopology(d.ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_VSSetShader(d.ctx, d.vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(d.ctx, d.ps, NULL, 0);
    ID3D11DeviceContext_PSSetShaderResources(d.ctx, 0, 1, &d.srv);
    ID3D11DeviceContext_PSSetSamplers(d.ctx, 0, 2, d.samp);
    ID3D11DeviceContext_PSSetConstantBuffers(d.ctx, 0, 1, &d.cb);
    /* The bars: the 4:3 game in a 16:9 window leaves two, and a soft dark
     * copy of the picture stretched over the whole client reads as a frame
     * where black reads as a hole. Drawn first; the picture goes on top. */
    if (g_bars && (r.left > 0 || r.top > 0)) {
        struct { float src[2], dst[2]; int mode, p0, p1, p2; } cb = {
            { (float)gw, (float)gh }, { (float)cw, (float)ch }, 5, 0, 0, 0 };
        D3D11_VIEWPORT full = { 0, 0, (float)cw, (float)ch, 0, 1 };
        ID3D11DeviceContext_UpdateSubresource(d.ctx, (ID3D11Resource*)d.cb, 0, NULL, &cb, 0, 0);
        ID3D11DeviceContext_RSSetViewports(d.ctx, 1, &full);
        ID3D11DeviceContext_Draw(d.ctx, 3, 0);
        ID3D11DeviceContext_UpdateSubresource(d.ctx, (ID3D11Resource*)d.cb, 0, NULL, &c, 0, 0);
    }
    D3D11_VIEWPORT vp = { (float)r.left, (float)r.top, (float)(r.right - r.left), (float)(r.bottom - r.top), 0, 1 };
    ID3D11DeviceContext_RSSetViewports(d.ctx, 1, &vp);
    ID3D11DeviceContext_Draw(d.ctx, 3, 0);
    IDXGISwapChain_Present(d.sc, 1, 0);               /* vsync paces this thread */
}

/* ---- input ---------------------------------------------------------------- */

/* A client point of this window in game coordinates, clamped to the picture:
 * in the letterbox bars the cursor sits on the picture's edge, which is where
 * the game's edge scrolling expects a player at the screen's edge. */
static POINT to_game(int x, int y) {
    POINT p;
    int rw = g_dst.right - g_dst.left, rh = g_dst.bottom - g_dst.top;
    if (rw <= 0 || rh <= 0) { p.x = p.y = 0; return p; }
    p.x = (LONG)((long long)(x - g_dst.left) * g_gw / rw);
    p.y = (LONG)((long long)(y - g_dst.top) * g_gh / rh);
    if (p.x < 0) p.x = 0;
    if (p.y < 0) p.y = 0;
    if (p.x >= g_gw) p.x = g_gw - 1;
    if (p.y >= g_gh) p.y = g_gh - 1;
    return p;
}

/* Posted as the game window's own thread is: DPI unaware. Posted from this
 * aware thread to an unaware window, Windows scaled the point by 1/scale
 * (Red Alert 2's presenter at 125%). */
static void post_unaware(UINT m, WPARAM w, LPARAM l) {
    void* was = g_set_dpi_ctx ? g_set_dpi_ctx((void*)(intptr_t)-1) : NULL;   /* DPI_AWARENESS_CONTEXT_UNAWARE */
    if (g_game_hwnd) PostMessageA(g_game_hwnd, m, w, l);
    if (was) g_set_dpi_ctx(was);
}

/* The game has one window: the mouse goes there, in its client coordinates,
 * which are the picture's. jgl also reads the cursor itself (GetCursorPos,
 * input.c), so that follows the same point. */
static void forward_mouse(UINT m, WPARAM w, int x, int y) {
    POINT gp = to_game(x, y);
    input_live_cursor(gp.x, gp.y);
    InterlockedExchange(&g_buttons, (LONG)(w & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)));
    post_unaware(m, w, MAKELPARAM(gp.x, gp.y));
}

static void forward_key(UINT m, WPARAM w, LPARAM l) { post_unaware(m, w, l); }

static void set_fullscreen(HWND hw, int on) {
    if (on == g_fullscreen) return;
    if (on) {
        MONITORINFO mi = { sizeof mi };
        GetWindowRect(hw, &g_windowed);
        GetMonitorInfoA(MonitorFromWindow(hw, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongA(hw, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(hw, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    } else {
        SetWindowLongA(hw, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hw, NULL, g_windowed.left, g_windowed.top, g_windowed.right - g_windowed.left,
                     g_windowed.bottom - g_windowed.top, SWP_FRAMECHANGED | SWP_NOZORDER);
    }
    g_fullscreen = on;
    fprintf(stderr, "[present] %s\n", on ? "fullscreen" : "windowed");
}

/* ---- settings ---------------------------------------------------------------
 * Kept in civ3.ini beside civ3.exe: [present] scale, bars, fullscreen and the
 * window's rect. The game's own settings stay in its conquests.ini, where the
 * resolution choice below goes too. */
static char g_ini[MAX_PATH];

static void settings_path(void) {
    char* slash;
    GetModuleFileNameA(NULL, g_ini, MAX_PATH);
    slash = strrchr(g_ini, '\\');
    strcpy_s(slash ? slash + 1 : g_ini, MAX_PATH - (slash ? (slash + 1 - g_ini) : 0), "civ3.ini");
}

static void settings_save(HWND hw) {
    char v[64];
    WritePrivateProfileStringA("present", "scale", k_mode_names[g_mode], g_ini);
    WritePrivateProfileStringA("present", "bars", g_bars ? "blur" : "black", g_ini);
    WritePrivateProfileStringA("present", "fullscreen", g_fullscreen ? "1" : "0", g_ini);
    if (hw && !g_fullscreen && !IsIconic(hw)) {
        RECT r;
        GetWindowRect(hw, &r);
        _snprintf(v, sizeof v - 1, "%ld,%ld,%ld,%ld", r.left, r.top, r.right - r.left, r.bottom - r.top);
        v[sizeof v - 1] = 0;
        WritePrivateProfileStringA("present", "window", v, g_ini);
    }
}

/* The game's resolution, for its next start: conquests.ini's KeepRes and
 * Video Mode (README, Resolution). The game reads them at start only. The
 * desktop's own size is KeepRes alone; 1024x768 is neither. */
static const int k_res[][2] = { { 1024, 768 }, { 1152, 864 }, { 1280, 1024 }, { 1600, 1200 }, { 0, 0 } };
#define NRES ((int)(sizeof k_res / sizeof k_res[0]))

static int game_resolution(void) {           /* the index in k_res conquests.ini asks for */
    char path[MAX_PATH], v[16];
    GetFullPathNameA("conquests.ini", MAX_PATH, path, NULL);        /* the game runs in its folder */
    if (!GetPrivateProfileIntA("Conquests", "KeepRes", 0, path)) return 0;
    GetPrivateProfileStringA("Conquests", "Video Mode", "", v, sizeof v, path);
    for (int i = 1; i < NRES - 1; i++) if (atoi(v) == k_res[i][0]) return i;
    return NRES - 1;
}
static void set_game_resolution(int i) {
    char path[MAX_PATH], v[16];
    GetFullPathNameA("conquests.ini", MAX_PATH, path, NULL);
    _snprintf(v, sizeof v - 1, "%d", k_res[i][0]), v[sizeof v - 1] = 0;
    WritePrivateProfileStringA("Conquests", "KeepRes", i ? "1" : NULL, path);
    WritePrivateProfileStringA("Conquests", "Video Mode", i && i < NRES - 1 ? v : NULL, path);
    fprintf(stderr, "[present] game resolution: %s (from the next start)\n", i == NRES - 1 ? "the desktop's" : v);
}

enum { ID_SCALE = 100, ID_BARS = 200, ID_FULL = 300, ID_RES = 400 };

static void settings_menu(HWND hw) {             /* at the mouse */
    POINT at;
    HMENU m = CreatePopupMenu(), sc = CreatePopupMenu(), bars = CreatePopupMenu(), res = CreatePopupMenu();
    static const char* const scale_label[NMODES] = { "Sharp (default)", "Smooth", "CRT", "Nearest", "Integer" };
    char lbl[48];
    int cur = game_resolution();
    for (int i = 0; i < NMODES; i++)
        AppendMenuA(sc, MF_STRING | (i == g_mode ? MF_CHECKED : 0), ID_SCALE + i, scale_label[i]);
    AppendMenuA(bars, MF_STRING | (g_bars ? MF_CHECKED : 0), ID_BARS + 1, "Blurred");
    AppendMenuA(bars, MF_STRING | (!g_bars ? MF_CHECKED : 0), ID_BARS + 0, "Black");
    for (int i = 0; i < NRES; i++) {
        if (i == NRES - 1) _snprintf(lbl, sizeof lbl - 1, "The desktop's (%d x %d)", GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
        else _snprintf(lbl, sizeof lbl - 1, "%d x %d", k_res[i][0], k_res[i][1]);
        lbl[sizeof lbl - 1] = 0;
        AppendMenuA(res, MF_STRING | (i == cur ? MF_CHECKED : 0), ID_RES + i, lbl);
    }
    AppendMenuA(m, MF_POPUP, (UINT_PTR)sc, "Scaling\tF12");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)bars, "Bars beside the picture");
    AppendMenuA(m, MF_STRING | (g_fullscreen ? MF_CHECKED : 0), ID_FULL, "Fullscreen\tF11");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)res, "Game resolution (next start)");
    GetCursorPos(&at);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, hw, NULL);
    DestroyMenu(m);                                           /* and its submenus */
    if (cmd >= ID_SCALE && cmd < ID_SCALE + NMODES) g_mode = cmd - ID_SCALE;
    else if (cmd == ID_BARS || cmd == ID_BARS + 1) g_bars = cmd - ID_BARS;
    else if (cmd == ID_FULL) set_fullscreen(hw, !g_fullscreen);
    else if (cmd >= ID_RES && cmd < ID_RES + NRES) set_game_resolution(cmd - ID_RES);
    if (cmd) settings_save(hw);
}

static DWORD WINAPI quit_soon(LPVOID unused) {
    (void)unused;
    Sleep(5000);                 /* the game had its chance to ask, save its settings and go */
    ExitProcess(0);
    return 0;
}

static LRESULT CALLBACK wndproc(HWND hw, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_MOUSEMOVE: case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP:
        /* A right-click on the bars, outside the picture, opens the settings;
         * on the picture it is the game's. */
        if ((m == WM_RBUTTONDOWN || m == WM_RBUTTONUP) && !(g_buttons & MK_RBUTTON)) {   /* not a drag begun on the picture */
            POINT p = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
            if (!PtInRect(&g_dst, p)) {
                if (m == WM_RBUTTONUP) settings_menu(hw);
                return 0;
            }
        }
        if (m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN) SetCapture(hw);
        if ((m == WM_LBUTTONUP || m == WM_RBUTTONUP || m == WM_MBUTTONUP) &&
            !(w & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)))
            ReleaseCapture();
        /* The game window gets no double-click class style from us: a second
         * press is a press. */
        forward_mouse(m == WM_LBUTTONDBLCLK ? WM_LBUTTONDOWN : m == WM_RBUTTONDBLCLK ? WM_RBUTTONDOWN : m,
                      w, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_MOUSEWHEEL:
        forward_key(m, w, l);
        return 0;
    case WM_SETCURSOR:
        /* The cursor the game set (SetCursor, host.c), or none while it hides it. */
        if (LOWORD(l) == HTCLIENT) { SetCursor(input_game_cursor()); return TRUE; }
        break;
    case WM_SYSKEYDOWN:
        if (w == VK_RETURN) { set_fullscreen(hw, !g_fullscreen); settings_save(hw); return 0; }
        if (w == VK_F10) { settings_menu(hw); return 0; }   /* F10 arrives as a system key */
        if (w == VK_F4) break;                               /* Alt+F4: WM_CLOSE */
        forward_key(m, w, l);
        return 0;
    case WM_KEYDOWN:
        if (w == VK_F11) { set_fullscreen(hw, !g_fullscreen); settings_save(hw); return 0; }
        if (w == VK_F12) {
            g_mode = (g_mode + 1) % NMODES;
            fprintf(stderr, "[present] scaling: %s\n", k_mode_names[g_mode]);
            settings_save(hw);
            return 0;
        }
        forward_key(m, w, l);
        return 0;
    case WM_CHAR: case WM_SYSCHAR:
        /* The game's own loop TranslateMessages the key-down forwarded above
         * into its character: forwarded too, every letter came twice (RA2). */
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        if (w == VK_F10 || w == VK_F11 || w == VK_F12) return 0;
        forward_key(m, w, l);
        return 0;
    case WM_ACTIVATE:
        /* The game's window never becomes active itself: it is told, so it
         * pauses and resumes as it would have. */
        post_unaware(WM_ACTIVATEAPP, LOWORD(w) != WA_INACTIVE, 0);
        break;
    case WM_CLOSE:
        settings_save(hw);
        post_unaware(WM_CLOSE, 0, 0);
        CloseHandle(CreateThread(NULL, 0, quit_soon, NULL, 0, NULL));
        return 0;
    }
    return DefWindowProcA(hw, m, w, l);
}

static DWORD WINAPI present_thread(LPVOID arg) {
    uint32_t* frame = NULL;
    size_t cap = 0;
    int fullscreen = (int)(intptr_t)arg;
    /* Per-monitor DPI aware, this thread only. Unaware, Windows stretched the
     * window's pixels by the display scale after the shader had scaled them,
     * and sharp-bilinear came out blurred (RA2). The game's windows stay
     * unaware: nothing about how the game sees its screen changes. */
    g_set_dpi_ctx = (set_dpi_ctx_t)GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext");
    if (g_set_dpi_ctx) g_set_dpi_ctx((void*)(intptr_t)-4);   /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */
    WNDCLASSA wc = {0};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = NULL;                                  /* WM_SETCURSOR sets the game's */
    wc.hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
    wc.lpszClassName = "Civ3Presenter";
    RegisterClassA(&wc);

    /* 4:3 at 85% of the work area's height: sharp-bilinear keeps whole texels
     * crisp at any scale, so there is no need to stop at a whole multiple. */
    RECT wa, r = { 0, 0, 0, 0 };
    char saved[64] = "";
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    r.bottom = (wa.bottom - wa.top) * 85 / 100;
    r.right = r.bottom * 4 / 3;
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    int ww = r.right - r.left, wh = r.bottom - r.top;
    int wx = wa.left + ((wa.right - wa.left) - ww) / 2, wy = wa.top + ((wa.bottom - wa.top) - wh) / 2;
    GetPrivateProfileStringA("present", "window", "", saved, sizeof saved, g_ini);
    {
        int sx, sy, sw, sh;
        if (sscanf(saved, "%d,%d,%d,%d", &sx, &sy, &sw, &sh) == 4 && sw > 200 && sh > 150) {
            RECT want = { sx, sy, sx + sw, sy + sh };
            if (MonitorFromRect(&want, MONITOR_DEFAULTTONULL))           /* still on a screen */
                wx = sx, wy = sy, ww = sw, wh = sh;
        }
    }
    HWND hw = CreateWindowExA(0, "Civ3Presenter", "Civilization III: Conquests (recomp)", WS_OVERLAPPEDWINDOW,
                              wx, wy, ww, wh, NULL, NULL, wc.hInstance, NULL);
    g_present_hwnd = hw;
    if (!hw || !d3d_init(hw)) {
        fprintf(stderr, "[present] could not start; run with --classic for the original display\n");
        ExitProcess(5);
    }
    ShowWindow(hw, SW_SHOW);
    SetForegroundWindow(hw);
    if (fullscreen) set_fullscreen(hw, 1);
    GetClientRect(hw, &r);
    fprintf(stderr, "[present] Direct3D 11 presenter, %ldx%ld window, scaling %s (F12), fullscreen F11, settings F10\n",
            r.right, r.bottom, k_mode_names[g_mode]);
    for (;;) {
        MSG msg;
        int gw, gh;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        /* A copy when the game has blitted, and at least every 100 ms
         * besides (a resize, the bars). */
        {
            static long seen = -1;
            static DWORD last;
            long n = g_blits;
            DWORD since = GetTickCount() - last;
            if ((n == seen && since < 100) || since < 16) { Sleep(1); continue; }
            seen = n, last = GetTickCount();
        }
        if (record_frame(&frame, &cap, &gw, &gh)) {
            InterlockedExchange(&g_gw, gw);
            InterlockedExchange(&g_gh, gh);
            draw(frame, gw, gh);
        } else {
            Sleep(16);
        }
    }
}

HWND present_hwnd(void) { return g_present_hwnd; }

int present_mode_from_name(const char* name) {
    for (int i = 0; i < NMODES; i++)
        if (!_stricmp(name, k_mode_names[i])) return i;
    return -1;
}

void present_start(int mode, int fullscreen) {
    char v[16];
    settings_path();
    /* civ3.ini first; a choice on the command line (mode or fullscreen >= 0) wins. */
    GetPrivateProfileStringA("present", "scale", "sharp", v, sizeof v, g_ini);
    g_mode = present_mode_from_name(v) >= 0 ? present_mode_from_name(v) : 0;
    GetPrivateProfileStringA("present", "bars", "blur", v, sizeof v, g_ini);
    g_bars = _stricmp(v, "black") != 0;
    if (fullscreen < 0) fullscreen = GetPrivateProfileIntA("present", "fullscreen", 0, g_ini);
    if (mode >= 0) g_mode = mode;
    CloseHandle(CreateThread(NULL, 0, present_thread, (LPVOID)(intptr_t)fullscreen, 0, NULL));
}
