// winclip.c — Win32 helpers (Windows cross only).
// Kept OUT of main.cpp because <windows.h> clashes with raylib's
// Rectangle/ShowCursor/CloseWindow declarations. Includes windows.h freely.
#ifdef _WIN32

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wchar.h>

// raylib's GetWindowHandle (GLFWwindow*) — declared manually because
// including raylib.h here clashes with windows.h (Rectangle/ShowCursor...).
extern void *GetWindowHandle(void);

// ── GLFW function pointers (resolved at runtime from glfw3.dll) ─────────────
typedef HWND (*GlfwGetWin32WindowFn)(void*);
static GlfwGetWin32WindowFn pfn_glfwGetWin32Window = NULL;
static int glfw_resolved = 0;

static void resolve_glfw(void) {
    if (glfw_resolved) return;
    glfw_resolved = 1;
    HMODULE mods[] = {
        GetModuleHandleA("glfw3.dll"),
        GetModuleHandleA("libraylib.dll"),
        GetModuleHandleA("raylib.dll"),
        GetModuleHandleA("libglfw3.dll"),
        GetModuleHandleA(NULL),
    };
    for (int i = 0; i < 5; i++) {
        if (mods[i]) {
            pfn_glfwGetWin32Window = (GlfwGetWin32WindowFn)GetProcAddress(mods[i], "glfwGetWin32Window");
            if (pfn_glfwGetWin32Window) break;
        }
    }
}
// Resolve HWND from GLFW window handle.
static HWND get_app_hwnd(void) {
    void *gw = GetWindowHandle();
    if (gw) {
        resolve_glfw();
        if (pfn_glfwGetWin32Window) {
            HWND hw = pfn_glfwGetWin32Window(gw);
            if (hw && IsWindow(hw)) return hw;
        }
        if (IsWindow((HWND)gw)) return (HWND)gw;
    }
    HWND act = GetActiveWindow();
    if (act && IsWindow(act)) return act;
    return NULL;
}

// ── Clipboard ───────────────────────────────────────────────────────────────

// Returns the first file path from the clipboard (CF_HDROP, i.e. files copied
// in Explorer), UTF-8 encoded, or NULL when the clipboard holds no files.
// Caller must NOT free; buffer is static (next call overwrites).
// Using CF_HDROP directly avoids GLFW's "Failed to convert clipboard to
// string" error, which fires when the clipboard has files or is empty.
const char* win_clipboard_file_path(void) {
    static char out[4096];
    out[0] = '\0';

    if (!IsClipboardFormatAvailable(CF_HDROP)) return NULL;
    if (!OpenClipboard(NULL)) return NULL;

    HANDLE h = GetClipboardData(CF_HDROP);
    if (!h) { CloseClipboard(); return NULL; }

    HDROP drop = (HDROP)h;
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    if (count == 0) { CloseClipboard(); return NULL; }

    wchar_t buf[2048];
    if (DragQueryFileW(drop, 0, buf, 2048) == 0) { CloseClipboard(); return NULL; }
    CloseClipboard();

    int len = WideCharToMultiByte(CP_UTF8, 0, buf, -1, NULL, 0, NULL, NULL);
    if (len <= 1) return NULL;
    if (len > (int)sizeof(out)) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)sizeof(out), NULL, NULL);
    return out;
}

// Returns clipboard text (UTF-8) ONLY when a text format is actually present,
// or NULL. Avoids GLFW's clipboard-string error on empty/non-text clipboards.
const char* win_clipboard_text(void) {
    static char out[4096];
    out[0] = '\0';

    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) && !IsClipboardFormatAvailable(CF_TEXT))
        return NULL;
    if (!OpenClipboard(NULL)) return NULL;

    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (!h) { CloseClipboard(); return NULL; }

    const wchar_t* w = (const wchar_t*)GlobalLock(h);
    if (!w) { CloseClipboard(); return NULL; }

    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (len > 1 && len <= (int)sizeof(out))
        WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)sizeof(out), NULL, NULL);

    GlobalUnlock(h);
    CloseClipboard();
    return (out[0] != '\0') ? out : NULL;
}

// ── Native file dialog (direct Win32 GetOpenFileNameW) ──────────────────────
// Bypasses tinyfiledialogs entirely; uses the Win32 common dialog with the app
// window as owner so it always opens in front.  Returns UTF-8 path or NULL.
const char* win_open_file_dialog(void) {
    static char result[4096];
    result[0] = '\0';

    HWND hwnd = get_app_hwnd();

    // Initialize COM on the UI thread for the modern shell dialog
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    // Filter: double-null terminated, description\0patterns\0 pairs.
    static const wchar_t filter[] =
        L"Image files (*.png;*.jpg;*.bmp;*.tga;*.gif;*.qoi;*.ico)\0"
        L"*.png;*.jpg;*.jpeg;*.bmp;*.tga;*.gif;*.qoi;*.ico\0"
        L"All files (*.*)\0*.*\0\0";

    wchar_t file_buf[2048];
    file_buf[0] = L'\0';

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = hwnd;
    ofn.lpstrFilter     = filter;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = file_buf;
    ofn.nMaxFile        = sizeof(file_buf) / sizeof(file_buf[0]);
    ofn.lpstrTitle      = L"Open Texture";
    ofn.Flags           = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    BOOL ok = GetOpenFileNameW(&ofn);

    if (SUCCEEDED(hr)) {
        CoUninitialize();
    }

    if (!ok) return NULL;

    int len = WideCharToMultiByte(CP_UTF8, 0, file_buf, -1, result, (int)sizeof(result), NULL, NULL);
    if (len <= 1) return NULL;
    return result;
}

// ── Workarea & screen metrics ───────────────────────────────────────────────
void win_get_workarea(int* out_w, int* out_h) {
    RECT rc;
    if (SystemParametersInfoA(SPI_GETWORKAREA, 0, &rc, 0)) {
        if (out_w) *out_w = (int)(rc.right - rc.left);
        if (out_h) *out_h = (int)(rc.bottom - rc.top);
    } else {
        if (out_w) *out_w = (int)GetSystemMetrics(SM_CXSCREEN);
        if (out_h) *out_h = (int)GetSystemMetrics(SM_CYSCREEN);
    }
}



#endif // _WIN32
