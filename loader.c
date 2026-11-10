/* AnnoTune Loader. One window, three weight files, then Run.
   The first Run downloads llama.cpp b12180 for this computer. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>

#define IDC_BASE 101
#define IDC_NAME 102
#define IDC_ML 103
#define IDC_RUN 104
#define IDC_STOP 105
#define IDC_STATUS 106

static const wchar_t RELEASE[] =
    L"https://github.com/ggml-org/llama.cpp/releases/download/b12180/";
static HWND g_wnd, g_base, g_name, g_ml, g_status, g_run;
static HANDLE g_proc;
static DWORD g_pid;
static int g_owned, g_wait;
static wchar_t g_config[MAX_PATH];
static wchar_t g_cache[MAX_PATH];

static void set_status(const wchar_t *text) {
    SetWindowTextW(g_status, text);
}

static int health_ok(void) {
    SOCKET s;
    struct sockaddr_in addr;
    char buf[1024];
    int n;
    DWORD wait = 2000;
    const char *req = "GET /health HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return 0;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (char *)&wait, sizeof(wait));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, (char *)&wait, sizeof(wait));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(0x7f000001);
    if (connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(s);
        return 0;
    }
    send(s, req, (int)strlen(req), 0);
    n = recv(s, buf, (int)sizeof(buf) - 1, 0);
    closesocket(s);
    if (n <= 0) return 0;
    buf[n] = 0;
    return strstr(buf, "\"status\":\"ok\"") != NULL || strstr(buf, "\"status\": \"ok\"") != NULL;
}

static void config_dir(void) {
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appdata);
    _snwprintf(g_config, MAX_PATH, L"%s\\AnnoTuneLoader", appdata);
    _snwprintf(g_cache, MAX_PATH, L"%s\\b12180", g_config);
    CreateDirectoryW(g_config, NULL);
    CreateDirectoryW(g_cache, NULL);
}

static void load_paths(void) {
    wchar_t file[MAX_PATH];
    HANDLE h;
    char raw[8192];
    DWORD n = 0;
    wchar_t wide[8192];
    wchar_t *line, *next;
    int row = 0;
    _snwprintf(file, MAX_PATH, L"%s\\paths.txt", g_config);
    h = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    ReadFile(h, raw, sizeof(raw) - 1, &n, NULL);
    CloseHandle(h);
    raw[n] = 0;
    if (!MultiByteToWideChar(CP_UTF8, 0, raw, -1, wide, 8192)) return;
    line = wide;
    while (line && row < 3) {
        next = wcschr(line, L'\n');
        if (next) *next++ = 0;
        while (*line == L'\r' || *line == L' ') line++;
        if (wcslen(line) && line[wcslen(line) - 1] == L'\r') line[wcslen(line) - 1] = 0;
        SetWindowTextW(row == 0 ? g_base : row == 1 ? g_name : g_ml, line);
        line = next;
        row++;
    }
}

static void save_paths(void) {
    wchar_t file[MAX_PATH], a[2048], b[2048], c[2048], all[7000];
    char utf[16000];
    HANDLE h;
    DWORD n;
    int bytes;
    GetWindowTextW(g_base, a, 2048);
    GetWindowTextW(g_name, b, 2048);
    GetWindowTextW(g_ml, c, 2048);
    _snwprintf(all, 7000, L"%s\n%s\n%s\n", a, b, c);
    bytes = WideCharToMultiByte(CP_UTF8, 0, all, -1, utf, sizeof(utf), NULL, NULL);
    if (bytes <= 1) return;
    _snwprintf(file, MAX_PATH, L"%s\\paths.txt", g_config);
    h = CreateFileW(file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, utf, (DWORD)(bytes - 1), &n, NULL);
    CloseHandle(h);
}

static int browse(HWND edit, const wchar_t *title) {
    wchar_t buf[2048];
    OPENFILENAMEW ofn;
    GetWindowTextW(edit, buf, 2048);
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_wnd;
    ofn.lpstrFilter = L"GGUF\0*.gguf\0All files\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = 2048;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    if (!GetOpenFileNameW(&ofn)) return 0;
    SetWindowTextW(edit, buf);
    return 1;
}

static int find_exe(const wchar_t *dir, wchar_t *out, size_t out_n) {
    wchar_t pattern[MAX_PATH], sub[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE h;
    _snwprintf(pattern, MAX_PATH, L"%s\\*", dir);
    h = FindFirstFileW(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
        _snwprintf(sub, MAX_PATH, L"%s\\%s", dir, data.cFileName);
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (find_exe(sub, out, out_n)) {
                FindClose(h);
                return 1;
            }
        } else if (_wcsicmp(data.cFileName, L"llama-server.exe") == 0) {
            wcsncpy(out, sub, out_n - 1);
            out[out_n - 1] = 0;
            FindClose(h);
            return 1;
        }
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return 0;
}

static void pump(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static int download_file(const wchar_t *name) {
    wchar_t url[1024], dest[MAX_PATH], partial[MAX_PATH];
    wchar_t host[256], path[768], extra[512];
    URL_COMPONENTS parts;
    HINTERNET ses = NULL, con = NULL, req = NULL;
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD size = 0, got = 0;
    BYTE buf[65536];
    int ok = 0;
    _snwprintf(url, 1024, L"%s%s", RELEASE, name);
    _snwprintf(dest, MAX_PATH, L"%s\\%s", g_cache, name);
    _snwprintf(partial, MAX_PATH, L"%s.partial", dest);
    if (GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES) return 1;
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 768;
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 512;
    if (!WinHttpCrackUrl(url, 0, 0, &parts)) return 0;
    ses = WinHttpOpen(L"AnnoTuneLoader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) return 0;
    con = WinHttpConnect(ses, host, parts.nPort, 0);
    if (!con) goto done;
    req = WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER,
                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                             parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (!req) goto done;
    size = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &size, sizeof(size));
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
        goto done;
    if (!WinHttpReceiveResponse(req, NULL)) goto done;
    file = CreateFileW(partial, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) goto done;
    for (;;) {
        if (!WinHttpQueryDataAvailable(req, &size)) goto done;
        if (size == 0) break;
        if (size > sizeof(buf)) size = sizeof(buf);
        if (!WinHttpReadData(req, buf, size, &got)) goto done;
        if (got == 0) break;
        if (!WriteFile(file, buf, got, &size, NULL)) goto done;
        pump();
    }
    CloseHandle(file);
    file = INVALID_HANDLE_VALUE;
    ok = MoveFileExW(partial, dest, MOVEFILE_REPLACE_EXISTING);
done:
    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    if (!ok) DeleteFileW(partial);
    return ok;
}

static int run_wait(wchar_t *cmdline) {
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    DWORD code = 1;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    if (!CreateProcessW(NULL, cmdline, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return 0;
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

static int unpack_zip(const wchar_t *name) {
    wchar_t cmd[1024], windir[MAX_PATH];
    GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
    _snwprintf(cmd, 1024, L"\"%s\\System32\\tar.exe\" -xf \"%s\\%s\" -C \"%s\"", windir, g_cache, name, g_cache);
    return run_wait(cmd);
}

static void choose_assets(const wchar_t **a, const wchar_t **b) {
    SYSTEM_INFO info;
    DISPLAY_DEVICEW dev;
    wchar_t names[1024];
    DWORD i;
    int nvidia = 0, other = 0, adreno = 0, arm = 0;
    GetNativeSystemInfo(&info);
    arm = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
    names[0] = 0;
    memset(&dev, 0, sizeof(dev));
    dev.cb = sizeof(dev);
    for (i = 0; EnumDisplayDevicesW(NULL, i, &dev, 0); i++) {
        wcsncat(names, dev.DeviceString, 1023 - wcslen(names));
        wcsncat(names, L" ", 1023 - wcslen(names));
        dev.cb = sizeof(dev);
    }
    if (wcsstr(names, L"NVIDIA")) nvidia = 1;
    if (wcsstr(names, L"Adreno")) adreno = 1;
    if (wcsstr(names, L"AMD") || wcsstr(names, L"Radeon") || wcsstr(names, L"Intel") || wcsstr(names, L"Arc"))
        other = 1;
    *b = NULL;
    if (nvidia && arm) {
        *a = L"llama-b12180-bin-win-cuda-13.5-arm64.zip";
        *b = L"cudart-llama-bin-win-cuda-13.5-arm64.zip";
    } else if (nvidia) {
        *a = L"llama-b12180-bin-win-cuda-12.4-x64.zip";
        *b = L"cudart-llama-bin-win-cuda-12.4-x64.zip";
    } else if (arm && adreno) {
        *a = L"llama-b12180-bin-win-opencl-adreno-arm64.zip";
    } else if (arm) {
        *a = L"llama-b12180-bin-win-vulkan-arm64.zip";
    } else if (other) {
        *a = L"llama-b12180-bin-win-vulkan-x64.zip";
    } else {
        *a = L"llama-b12180-bin-win-cpu-x64.zip";
    }
}

static int ensure_server(wchar_t *binary, size_t binary_n) {
    const wchar_t *a = NULL, *b = NULL;
    if (find_exe(g_cache, binary, binary_n)) return 1;
    choose_assets(&a, &b);
    set_status(L"Downloading the model server. This happens once.");
    pump();
    if (!download_file(a) || !unpack_zip(a)) return 0;
    if (b) {
        set_status(L"Downloading the GPU libraries. This happens once.");
        pump();
        if (!download_file(b) || !unpack_zip(b)) return 0;
    }
    return find_exe(g_cache, binary, binary_n);
}

static void quote_arg(wchar_t *cmd, size_t n, const wchar_t *arg) {
    size_t used = wcslen(cmd);
    int quote = wcschr(arg, L' ') || wcschr(arg, L'\t');
    if (used && used + 1 < n) cmd[used++] = L' ';
    if (quote && used + 1 < n) cmd[used++] = L'"';
    wcsncpy(cmd + used, arg, n - used - 1);
    used = wcslen(cmd);
    if (quote && used + 1 < n) {
        cmd[used++] = L'"';
        cmd[used] = 0;
    }
}

static void stop_owned(void) {
    wchar_t windir[MAX_PATH], cmd[256];
    if (!g_owned || !g_pid) return;
    GetEnvironmentVariableW(L"SystemRoot", windir, MAX_PATH);
    _snwprintf(cmd, 256, L"\"%s\\System32\\taskkill.exe\" /PID %lu /T /F", windir, g_pid);
    run_wait(cmd);
    if (g_proc) CloseHandle(g_proc);
    g_proc = NULL;
    g_pid = 0;
    g_owned = 0;
    KillTimer(g_wnd, 1);
}

static int text_of(HWND edit, wchar_t *buf, int n) {
    GetWindowTextW(edit, buf, n);
    return buf[0] != 0 && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES;
}

static void start_server(void) {
    wchar_t base[2048], name[2048], ml[2048], binary[MAX_PATH], cmd[8192];
    wchar_t logf[MAX_PATH], errf[MAX_PATH], nul[MAX_PATH], path_env[32768], extra[8192];
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE log, err, in;
    if (!text_of(g_base, base, 2048) || !text_of(g_name, name, 2048) || !text_of(g_ml, ml, 2048)) {
        set_status(L"Choose the base model, the name adapter, and the ML adapter.");
        return;
    }
    if (health_ok()) {
        set_status(L"A model server is already running at http://127.0.0.1:8080.");
        return;
    }
    EnableWindow(g_run, FALSE);
    save_paths();
    if (!ensure_server(binary, MAX_PATH)) {
        set_status(L"Could not download llama-server.");
        EnableWindow(g_run, TRUE);
        return;
    }
    extra[0] = 0;
    {
        wchar_t pattern[MAX_PATH], sub[MAX_PATH];
        WIN32_FIND_DATAW data;
        HANDLE h;
        _snwprintf(pattern, MAX_PATH, L"%s\\*", g_cache);
        h = FindFirstFileW(pattern, &data);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0) continue;
                _snwprintf(sub, MAX_PATH, L"%s\\%s", g_cache, data.cFileName);
                wcsncat(extra, sub, 8191 - wcslen(extra));
                wcsncat(extra, L";", 8191 - wcslen(extra));
            } while (FindNextFileW(h, &data));
            FindClose(h);
        }
    }
    GetEnvironmentVariableW(L"PATH", path_env, 24000);
    wcsncat(extra, path_env, 32767 - wcslen(extra));
    SetEnvironmentVariableW(L"PATH", extra);
    _snwprintf(logf, MAX_PATH, L"%s\\server.log", g_config);
    _snwprintf(errf, MAX_PATH, L"%s\\server.err", g_config);
    GetEnvironmentVariableW(L"SystemRoot", nul, MAX_PATH);
    wcsncat(nul, L"\\NUL", MAX_PATH - wcslen(nul) - 1);
    {
        SECURITY_ATTRIBUTES sa;
        memset(&sa, 0, sizeof(sa));
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        log = CreateFileW(logf, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        err = CreateFileW(errf, GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        in = CreateFileW(nul, GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, NULL);
    }
    cmd[0] = 0;
    quote_arg(cmd, 8192, binary);
    quote_arg(cmd, 8192, L"-m");
    quote_arg(cmd, 8192, base);
    quote_arg(cmd, 8192, L"--lora");
    {
        wchar_t lora[4200];
        _snwprintf(lora, 4200, L"%s,%s", name, ml);
        quote_arg(cmd, 8192, lora);
    }
    quote_arg(cmd, 8192, L"--lora-init-without-apply");
    quote_arg(cmd, 8192, L"-c");
    quote_arg(cmd, 8192, L"8192");
    quote_arg(cmd, 8192, L"-ngl");
    quote_arg(cmd, 8192, L"99");
    quote_arg(cmd, 8192, L"-np");
    quote_arg(cmd, 8192, L"1");
    quote_arg(cmd, 8192, L"--no-cont-batching");
    quote_arg(cmd, 8192, L"--no-cache-prompt");
    quote_arg(cmd, 8192, L"--slot-prompt-similarity");
    quote_arg(cmd, 8192, L"0");
    quote_arg(cmd, 8192, L"--no-jinja");
    quote_arg(cmd, 8192, L"--host");
    quote_arg(cmd, 8192, L"127.0.0.1");
    quote_arg(cmd, 8192, L"--port");
    quote_arg(cmd, 8192, L"8080");
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = in;
    si.hStdOutput = log;
    si.hStdError = err;
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
        set_status(L"Could not start llama-server.");
        EnableWindow(g_run, TRUE);
    } else {
        g_proc = pi.hProcess;
        g_pid = pi.dwProcessId;
        g_owned = 1;
        g_wait = 0;
        CloseHandle(pi.hThread);
        set_status(L"Loading the model. This can take a minute.");
        SetTimer(g_wnd, 1, 1000, NULL);
    }
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (err != INVALID_HANDLE_VALUE) CloseHandle(err);
    if (in != INVALID_HANDLE_VALUE) CloseHandle(in);
}

static void on_timer(void) {
    DWORD code = 0;
    g_wait++;
    if (g_proc && GetExitCodeProcess(g_proc, &code) && code != STILL_ACTIVE) {
        KillTimer(g_wnd, 1);
        g_owned = 0;
        set_status(L"The server stopped. See server.err in AppData\\AnnoTuneLoader.");
        EnableWindow(g_run, TRUE);
        return;
    }
    if (health_ok()) {
        KillTimer(g_wnd, 1);
        set_status(L"Ready at http://127.0.0.1:8080. Leave this window open.");
        return;
    }
    if (g_wait >= 180) {
        stop_owned();
        set_status(L"The server did not become ready.");
        EnableWindow(g_run, TRUE);
    }
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case 201: browse(g_base, L"Base model"); break;
        case 202: browse(g_name, L"Name adapter"); break;
        case 203: browse(g_ml, L"ML adapter"); break;
        case IDC_RUN: start_server(); break;
        case IDC_STOP:
            stop_owned();
            EnableWindow(g_run, TRUE);
            set_status(L"Stopped.");
            break;
        }
        return 0;
    case WM_TIMER:
        on_timer();
        return 0;
    case WM_DESTROY:
        stop_owned();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static HWND add_label(const wchar_t *text, int x, int y, int w) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, x, y, w, 22, g_wnd, NULL, NULL, NULL);
}

static HWND add_edit(int id, int x, int y, int w) {
    return CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                           x, y, w, 24, g_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

static HWND add_button(const wchar_t *text, int id, int x, int y, int w) {
    return CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                           x, y, w, 28, g_wnd, (HMENU)(INT_PTR)id, NULL, NULL);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    WNDCLASSW wc;
    MSG msg;
    WSADATA wsa;
    (void)prev;
    (void)cmd;
    (void)show;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    config_dir();
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AnnoTuneLoader";
    RegisterClassW(&wc);
    g_wnd = CreateWindowExW(0, L"AnnoTuneLoader", L"AnnoTune Loader",
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, 560, 250, NULL, NULL, inst, NULL);
    add_label(L"Base model", 16, 18, 100);
    add_label(L"Name adapter", 16, 56, 100);
    add_label(L"ML adapter", 16, 94, 100);
    g_base = add_edit(IDC_BASE, 120, 16, 310);
    g_name = add_edit(IDC_NAME, 120, 54, 310);
    g_ml = add_edit(IDC_ML, 120, 92, 310);
    add_button(L"Browse", 201, 440, 14, 84);
    add_button(L"Browse", 202, 440, 52, 84);
    add_button(L"Browse", 203, 440, 90, 84);
    g_run = add_button(L"Run", IDC_RUN, 120, 134, 90);
    add_button(L"Stop", IDC_STOP, 220, 134, 90);
    g_status = add_label(L"Choose the three weight files, then Run.", 16, 176, 510);
    load_paths();
    ShowWindow(g_wnd, SW_SHOW);
    UpdateWindow(g_wnd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    WSACleanup();
    return 0;
}
