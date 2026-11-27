/* Recomp Hub: one launcher for ResortRecomp (Wii Sports Resort), PartyRecomp (Wii Party) and Party9Recomp
   (Mario Party 9).

   For each game it downloads that recomp's latest release from GitHub, unpacks it into Games\<name> beside
   this program, and builds the game from the player's own disc by running that recomp's own launcher headless
   ("Launcher.exe --test-build <disc>"). Play opens the game's own launcher, which keeps controllers, graphics
   and online rooms.

   Online: "Set up my server for me" runs the relay server (wsr_relay.exe, shipped with every recomp; one relay
   serves all three games) hidden on this PC, adds a Windows Firewall rule (Windows asks first), asks the router
   to open the port with UPnP, finds this PC's addresses, and points every installed game at the server.
   "Join a friend's server" points every installed game at someone else's.

   Settings live in RecompHub.ini beside this program. No game code or data is in here or downloaded: each
   recomp release contains none, and the game is built from the player's own disc.

   RecompHub.exe --server   starts the relay hidden (if it isn't running) and exits; used for "start with Windows".

   Build: see build.ps1 (MinGW-w64 gcc). */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define HUB_VERSION L"1.1.0"
#define RELAY_PORT 51020
#define RELAY_PORT_S L"51020"
#define FIREWALL_RULE L"Recomp Hub relay"
#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"RecompHubServer"
#define NEED_GB 20

typedef struct {
    const wchar_t *title, *repo, *github, *blurb;
} Game;

static const Game GAMES[] = {
    {L"Wii Sports Resort", L"ResortRecomp", L"Hottuck71/ResortRecomp",
     L"Wuhu Island's sports: swordplay, bowling, archery, table tennis and more."},
    {L"Wii Party", L"PartyRecomp", L"Hottuck71/PartyRecomp",
     L"Board games, party modes and minigames for up to four players."},
    {L"Mario Party 9", L"Party9Recomp", L"Hottuck71/Party9Recomp",
     L"Mario Party's boards and minigames for up to four players."},
};
#define NGAMES 3
#define PAGE_ONLINE NGAMES

/* ---- controls ---- */
enum {
    ID_NAV = 100, /* ID_NAV + 0..3 */
    ID_TITLE = 200, ID_BLURB, ID_G1, ID_G2, ID_G3, ID_GETSTATUS, ID_DOWNLOAD, ID_EXISTING, ID_DLPROG,
    ID_ROMLBL, ID_ROM, ID_ROMBROWSE, ID_TOOLLBL, ID_TOOL, ID_TOOLBROWSE, ID_BUILD, ID_BUILDSTATUS, ID_BUILDDETAIL,
    ID_BUILDPROG, ID_PLAY, ID_OPENDIR, ID_PLAYNOTE, ID_UNINSTALL,
    ID_OTITLE = 300, ID_OBLURB, ID_OG1, ID_HOST, ID_STOP, ID_COPY, ID_OSTATUS, ID_AUTOSTART, ID_OG2, ID_JOINLBL,
    ID_JOIN, ID_USEJOIN, ID_JOINNOTE, ID_UNSERVER,
    ID_HEAD = 400, ID_SUBHEAD,
};
#define WM_JOB_STATUS (WM_APP + 1) /* wParam: 0 game status, 1 build status, 2 online text; lParam: malloc'd text */
#define WM_JOB_PROGRESS (WM_APP + 2) /* wParam: 0..100, or (WPARAM)-1 for "busy, no percentage" */
#define WM_JOB_DONE (WM_APP + 3)
#define TIMER_BUILD 1

static HWND g_wnd;
static HINSTANCE g_inst;
static HFONT g_font, g_font_big, g_font_head, g_font_small;
static HBRUSH g_white;
static int g_dpi = 96;
static int g_page = 0;
static volatile LONG g_busy = 0;
static int g_job_game = -1; /* which game a running job is for */
static int g_prog_id = 0;   /* the progress bar a running job moves */
static wchar_t g_msg[NGAMES][2][600]; /* each game's newest job messages: [0] files, [1] build */
static DWORD g_build_started;
static wchar_t g_dir[MAX_PATH], g_ini[MAX_PATH], g_exe[MAX_PATH];
static wchar_t g_friend_addr[128];

#define S(x) MulDiv((x), g_dpi, 96)
static HWND ctl(int id) { return GetDlgItem(g_wnd, id); }

/* ---- small text helpers ---- */
static char* to_utf8(const wchar_t* w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char* s = malloc(n ? n : 1);
    if (n) WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL); else s[0] = 0;
    return s;
}
static wchar_t* from_utf8(const char* s, int len) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, len, NULL, 0);
    wchar_t* w = malloc((n + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, len, w, n);
    w[n] = 0;
    return w;
}
static wchar_t* wdup_printf(const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = _vscwprintf(fmt, a);
    va_end(a);
    wchar_t* out = malloc((n + 1) * sizeof(wchar_t));
    va_start(a, fmt);
    _vsnwprintf(out, n + 1, fmt, a);
    va_end(a);
    out[n] = 0;
    return out;
}
static void post_text(int where, const wchar_t* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    int n = _vscwprintf(fmt, a);
    va_end(a);
    wchar_t* out = malloc((n + 1) * sizeof(wchar_t));
    va_start(a, fmt);
    _vsnwprintf(out, n + 1, fmt, a);
    va_end(a);
    out[n] = 0;
    PostMessageW(g_wnd, WM_JOB_STATUS, where, (LPARAM)out);
}
static void post_progress(int pct) { PostMessageW(g_wnd, WM_JOB_PROGRESS, (WPARAM)pct, 0); }
static void path_join(wchar_t* out, const wchar_t* a, const wchar_t* b) { _snwprintf(out, MAX_PATH, L"%s\\%s", a, b); out[MAX_PATH - 1] = 0; }
static BOOL file_exists(const wchar_t* p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static BOOL dir_exists(const wchar_t* p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }

/* ---- files and key=value settings (UTF-8, like each recomp's launcher.ini) ---- */
static char* read_file(const wchar_t* path, DWORD* len) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD size = GetFileSize(f, NULL), got = 0;
    char* buf = malloc(size + 1);
    ReadFile(f, buf, size, &got, NULL);
    CloseHandle(f);
    buf[got] = 0;
    if (len) *len = got;
    return buf;
}
static BOOL write_file(const wchar_t* path, const char* data, DWORD len) {
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return FALSE;
    DWORD put = 0;
    BOOL ok = WriteFile(f, data, len, &put, NULL) && put == len;
    CloseHandle(f);
    return ok;
}
static BOOL ini_get(const wchar_t* file, const char* key, wchar_t* out, int cap) {
    out[0] = 0;
    char* buf = read_file(file, NULL);
    if (!buf) return FALSE;
    size_t klen = strlen(key);
    BOOL found = FALSE;
    for (char* line = buf; *line;) {
        char* end = strchr(line, '\n');
        size_t n = end ? (size_t)(end - line) : strlen(line);
        if (n > klen && !strncmp(line, key, klen) && line[klen] == '=') {
            size_t vlen = n - klen - 1;
            while (vlen && (line[klen + 1 + vlen - 1] == '\r')) vlen--;
            wchar_t* w = from_utf8(line + klen + 1, (int)vlen);
            wcsncpy(out, w, cap - 1);
            out[cap - 1] = 0;
            free(w);
            found = TRUE;
            break;
        }
        if (!end) break;
        line = end + 1;
    }
    free(buf);
    return found;
}
/* Sets key=value, keeping every other line as it was. A NULL value removes the key. */
static BOOL ini_set(const wchar_t* file, const char* key, const wchar_t* value) {
    DWORD len = 0;
    char* buf = read_file(file, &len);
    if (!value && !buf) return TRUE; /* nothing to remove */
    char* v = to_utf8(value ? value : L"");
    size_t klen = strlen(key), cap = len * 2 + strlen(v) + klen + 8; /* room for a CR on every line */
    char* out = malloc(cap);
    size_t at = 0;
    BOOL done = FALSE;
    if (buf) { /* written with Windows line ends, as the recomp launchers write it */
        for (char* line = buf; *line;) {
            char* end = strchr(line, '\n');
            size_t n = end ? (size_t)(end - line) : strlen(line);
            if (n && line[n - 1] == '\r') n--;
            if (n > klen && !strncmp(line, key, klen) && line[klen] == '=') {
                if (!done && value) at += sprintf(out + at, "%s=%s\r\n", key, v);
                done = TRUE;
            } else if (n) {
                memcpy(out + at, line, n);
                at += n;
                out[at++] = '\r';
                out[at++] = '\n';
            }
            if (!end) break;
            line = end + 1;
        }
    }
    if (!done && value) at += sprintf(out + at, "%s=%s\r\n", key, v);
    BOOL ok = write_file(file, out, (DWORD)at);
    free(buf);
    free(v);
    free(out);
    return ok;
}
static void hub_get(const char* key, wchar_t* out, int cap) { ini_get(g_ini, key, out, cap); }
static void hub_set(const char* key, const wchar_t* value) { ini_set(g_ini, key, value); }
static void hub_key(char* out, const char* prefix, int game) {
    char* r = to_utf8(GAMES[game].repo);
    sprintf(out, "%s_%s", prefix, r);
    free(r);
}

/* ---- where each game lives, and how far along it is ---- */
static void game_dir(int i, wchar_t* out) {
    char key[64];
    hub_key(key, "dir", i);
    hub_get(key, out, MAX_PATH);
    if (!out[0]) _snwprintf(out, MAX_PATH, L"%s\\Games\\%s", g_dir, GAMES[i].repo);
}
static BOOL game_file(int i, const wchar_t* name, wchar_t* out) {
    wchar_t d[MAX_PATH];
    game_dir(i, d);
    path_join(out, d, name);
    return file_exists(out);
}
enum { NOT_INSTALLED, INSTALLED, BUILT };
static int game_state(int i) {
    wchar_t p[MAX_PATH], d[MAX_PATH];
    if (!game_file(i, L"Launcher.exe", p)) return NOT_INSTALLED;
    game_dir(i, d);
    path_join(p, d, L"*-Game.exe");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return INSTALLED;
    FindClose(h);
    return BUILT;
}

/* TRUE when the game's folder is the one Recomp Hub made (Games\<recomp>), not one the player chose. */
static BOOL hub_made_folder(int i) {
    wchar_t d[MAX_PATH], mine[MAX_PATH];
    game_dir(i, d);
    _snwprintf(mine, MAX_PATH, L"%s\\Games\\%s", g_dir, GAMES[i].repo);
    return !_wcsicmp(d, mine);
}

/* ---- processes ---- */
/* Runs a command with no window, collecting what it prints. Returns its exit code, or -1 if it didn't start,
   or -2 if it ran past timeout_ms (then it's stopped). */
static int run_hidden(const wchar_t* cmdline, const wchar_t* cwd, char** output, DWORD timeout_ms) {
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd, wr;
    if (output) *output = NULL;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi;
    wchar_t* cmd = _wcsdup(cmdline);
    BOOL started = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, cwd, &si, &pi);
    free(cmd);
    CloseHandle(wr);
    if (!started) { CloseHandle(rd); return -1; }
    size_t cap = 4096, len = 0;
    char* buf = malloc(cap);
    DWORD start = GetTickCount();
    int code = 0;
    for (;;) {
        DWORD avail = 0, got = 0;
        if (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
            if (len + avail + 1 > cap) { cap = (len + avail + 1) * 2; buf = realloc(buf, cap); }
            if (ReadFile(rd, buf + len, avail, &got, NULL)) len += got;
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            while (PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail) {
                if (len + avail + 1 > cap) { cap = (len + avail + 1) * 2; buf = realloc(buf, cap); }
                if (!ReadFile(rd, buf + len, avail, &got, NULL)) break;
                len += got;
            }
            DWORD ec = 0;
            GetExitCodeProcess(pi.hProcess, &ec);
            code = (int)ec;
            break;
        }
        if (timeout_ms != INFINITE && GetTickCount() - start > timeout_ms) {
            TerminateProcess(pi.hProcess, 1);
            code = -2;
            break;
        }
    }
    buf[len] = 0;
    CloseHandle(rd);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (output) *output = buf; else free(buf);
    return code;
}
/* Runs a command as administrator (Windows shows its own prompt first) and waits. FALSE if declined or failed. */
static BOOL run_as_admin(const wchar_t* exe, const wchar_t* args) {
    SHELLEXECUTEINFOW se = {sizeof se};
    se.fMask = SEE_MASK_NOCLOSEPROCESS;
    se.lpVerb = L"runas";
    se.lpFile = exe;
    se.lpParameters = args;
    se.nShow = SW_HIDE;
    if (!ShellExecuteExW(&se) || !se.hProcess) return FALSE;
    WaitForSingleObject(se.hProcess, 60000);
    DWORD ec = 1;
    GetExitCodeProcess(se.hProcess, &ec);
    CloseHandle(se.hProcess);
    return ec == 0;
}
static int count_processes(const wchar_t* exe_name, BOOL kill) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = {sizeof pe};
    int n = 0;
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe)) {
        if (_wcsicmp(pe.szExeFile, exe_name)) continue;
        n++;
        if (kill) {
            HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
            if (p) { TerminateProcess(p, 0); CloseHandle(p); }
        }
    }
    CloseHandle(snap);
    return n;
}

/* TRUE if a program started from inside dir is running (a game's launcher, or the game itself). */
static BOOL running_from(const wchar_t* dir) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return FALSE;
    size_t n = wcslen(dir);
    while (n && dir[n - 1] == L'\\') n--;
    PROCESSENTRY32W pe = {sizeof pe};
    BOOL found = FALSE;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe)) {
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!p) continue;
        wchar_t path[MAX_PATH * 2];
        DWORD sz = MAX_PATH * 2;
        if (QueryFullProcessImageNameW(p, 0, path, &sz) && sz > n && !_wcsnicmp(path, dir, n) && path[n] == L'\\') found = TRUE;
        CloseHandle(p);
    }
    CloseHandle(snap);
    return found;
}

/* ---- HTTPS ---- */
/* GETs url. With out_file, streams the body there (progress gets bytes so far and the total, 0 if unknown);
   otherwise returns it in *body. Returns the HTTP status, or 0 if the connection failed. */
static int http_get(const wchar_t* url, const wchar_t* accept, HANDLE out_file, char** body,
                    void (*progress)(ULONGLONG got, ULONGLONG total)) {
    URL_COMPONENTSW uc = {sizeof uc};
    wchar_t host[256], path[2048];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    if (body) *body = NULL;
    if (!WinHttpCrackUrl(url, 0, 0, &uc)) return 0;
    int status = 0;
    HINTERNET s = WinHttpOpen(L"RecompHub/" HUB_VERSION, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, NULL, NULL, 0);
    if (!s) s = WinHttpOpen(L"RecompHub/" HUB_VERSION, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    HINTERNET c = s ? WinHttpConnect(s, host, uc.nPort, 0) : NULL;
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL,
                                         uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : NULL;
    if (r && accept) {
        wchar_t h[256];
        _snwprintf(h, 256, L"Accept: %s", accept);
        WinHttpAddRequestHeaders(r, h, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    }
    if (r && WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &code, &sz, NULL);
        status = (int)code;
        wchar_t lenbuf[32] = L"";
        DWORD lsz = sizeof lenbuf;
        ULONGLONG total = 0, got = 0;
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, NULL, lenbuf, &lsz, NULL)) total = _wcstoui64(lenbuf, NULL, 10);
        size_t cap = 65536, len = 0;
        char* buf = out_file ? NULL : malloc(cap);
        static char chunk[1 << 16];
        for (;;) {
            DWORD n = 0;
            if (!WinHttpReadData(r, chunk, sizeof chunk, &n)) { status = 0; break; }
            if (!n) break;
            if (out_file) {
                DWORD put = 0;
                if (!WriteFile(out_file, chunk, n, &put, NULL) || put != n) { status = 0; break; }
            } else {
                if (len + n + 1 > cap) { cap = (len + n + 1) * 2; buf = realloc(buf, cap); }
                memcpy(buf + len, chunk, n);
                len += n;
            }
            got += n;
            if (progress) progress(got, total);
        }
        if (buf) { buf[len] = 0; *body = buf; }
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    if (s) WinHttpCloseHandle(s);
    return status;
}

/* ---- finding DolphinTool ---- */
static BOOL search_for(const wchar_t* dir, const wchar_t* name, int depth, wchar_t* out) {
    wchar_t p[MAX_PATH];
    path_join(p, dir, name);
    if (file_exists(p)) { wcscpy(out, p); return TRUE; }
    if (depth <= 0) return FALSE;
    path_join(p, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    BOOL found = FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        wchar_t sub[MAX_PATH];
        path_join(sub, dir, fd.cFileName);
        found = search_for(sub, name, depth - 1, out);
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}
static BOOL find_dolphin_tool(wchar_t* out) {
    wchar_t p[MAX_PATH];
    hub_get("dolphin_tool", p, MAX_PATH);
    if (p[0] && file_exists(p)) { wcscpy(out, p); return TRUE; }
    for (int i = 0; i < NGAMES; i++) { /* a recomp launcher that already knows where it is */
        wchar_t ini[MAX_PATH];
        if (game_file(i, L"launcher.ini", ini) && ini_get(ini, "dolphin_tool", p, MAX_PATH) && file_exists(p)) {
            wcscpy(out, p);
            return TRUE;
        }
    }
    static const int places[] = {CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86, CSIDL_LOCAL_APPDATA, CSIDL_DESKTOPDIRECTORY,
                                 CSIDL_PERSONAL, CSIDL_PROFILE};
    for (int k = 0; k < (int)(sizeof places / sizeof places[0]); k++) {
        wchar_t base[MAX_PATH];
        if (FAILED(SHGetFolderPathW(NULL, places[k], NULL, 0, base))) continue;
        int depth = places[k] == CSIDL_PROFILE ? 2 : 3; /* Downloads\dolphin-x\Dolphin-x64 */
        if (places[k] == CSIDL_LOCAL_APPDATA) wcscat(base, L"\\Programs");
        if (search_for(base, L"DolphinTool.exe", depth, out)) {
            hub_set("dolphin_tool", out); /* search once */
            return TRUE;
        }
    }
    return FALSE;
}

/* ---- network helpers ---- */
static BOOL local_ip(wchar_t* out, int cap) {
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(53);
    a.sin_addr.s_addr = inet_addr("8.8.8.8"); /* connecting a UDP socket sends nothing: it only picks the route */
    BOOL ok = FALSE;
    if (s != INVALID_SOCKET && connect(s, (struct sockaddr*)&a, sizeof a) == 0) {
        struct sockaddr_in me;
        int len = sizeof me;
        if (getsockname(s, (struct sockaddr*)&me, &len) == 0) {
            _snwprintf(out, cap, L"%hs", inet_ntoa(me.sin_addr));
            ok = TRUE;
        }
    }
    if (s != INVALID_SOCKET) closesocket(s);
    return ok;
}
static void base64_utf16(const wchar_t* text, wchar_t* out) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char* b = (const unsigned char*)text;
    size_t n = wcslen(text) * 2, o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = b[i] << 16 | (i + 1 < n ? b[i + 1] << 8 : 0) | (i + 2 < n ? b[i + 2] : 0);
        out[o++] = t[v >> 18 & 63];
        out[o++] = t[v >> 12 & 63];
        out[o++] = i + 1 < n ? t[v >> 6 & 63] : L'=';
        out[o++] = i + 2 < n ? t[v & 63] : L'=';
    }
    out[o] = 0;
}
static int run_powershell(const wchar_t* script, char** output, DWORD timeout_ms) {
    size_t n = wcslen(script) * 3 + 16;
    wchar_t* enc = malloc(n * sizeof(wchar_t));
    base64_utf16(script, enc);
    wchar_t* cmd = wdup_printf(L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand %s", enc);
    int code = run_hidden(cmd, NULL, output, timeout_ms);
    free(enc);
    free(cmd);
    return code;
}

/* ---- the relay server ---- */
static void server_dir(wchar_t* out) { path_join(out, g_dir, L"Server"); }
/* (Not by connecting to the port: a refused connection to this PC takes Windows about 2 seconds.) */
static BOOL relay_running(void) { return count_processes(L"wsr_relay.exe", FALSE) > 0; }
/* Copies wsr_relay.exe from an installed recomp into Server\ (once), so the server doesn't depend on any game. */
static BOOL ensure_relay_exe(wchar_t* exe) {
    wchar_t dir[MAX_PATH];
    server_dir(dir);
    CreateDirectoryW(dir, NULL);
    path_join(exe, dir, L"wsr_relay.exe");
    for (int i = NGAMES - 1; i >= 0; i--) { /* newest recomp first */
        wchar_t src[MAX_PATH];
        if (game_file(i, L"relay\\wsr_relay.exe", src)) {
            if (!file_exists(exe)) CopyFileW(src, exe, FALSE);
            break;
        }
    }
    return file_exists(exe);
}
static BOOL start_relay(void) {
    if (relay_running()) return TRUE;
    wchar_t exe[MAX_PATH], dir[MAX_PATH], log[MAX_PATH];
    if (!ensure_relay_exe(exe)) return FALSE;
    server_dir(dir);
    path_join(log, dir, L"relay-log.txt");
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE out = CreateFileW(log, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_ALWAYS, 0, NULL);
    STARTUPINFOW si = {sizeof si};
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = si.hStdError = out;
    PROCESS_INFORMATION pi;
    wchar_t* cmd = wdup_printf(L"\"%s\" " RELAY_PORT_S, exe);
    BOOL ok = CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW | DETACHED_PROCESS, NULL, dir, &si, &pi);
    free(cmd);
    if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
    if (!ok) return FALSE;
    CloseHandle(pi.hThread);
    DWORD exited = WaitForSingleObject(pi.hProcess, 800); /* it exits at once if the port is taken */
    CloseHandle(pi.hProcess);
    return exited == WAIT_TIMEOUT;
}
static BOOL autostart_on(void) {
    wchar_t v[MAX_PATH * 2];
    DWORD sz = sizeof v;
    return RegGetValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, RRF_RT_REG_SZ, NULL, v, &sz) == ERROR_SUCCESS;
}
static void set_autostart(BOOL on) {
    if (on) {
        wchar_t* v = wdup_printf(L"\"%s\" --server", g_exe);
        RegSetKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE, REG_SZ, v, (DWORD)((wcslen(v) + 1) * sizeof(wchar_t)));
        free(v);
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUN_KEY, RUN_VALUE);
    }
}
/* Points every installed game's launcher at a relay. Returns how many games were set. */
static int point_games_at(const wchar_t* addr) {
    int n = 0;
    for (int i = 0; i < NGAMES; i++) {
        wchar_t ini[MAX_PATH], exe[MAX_PATH];
        if (!game_file(i, L"Launcher.exe", exe)) continue;
        game_file(i, L"launcher.ini", ini);
        if (ini_set(ini, "relay", addr)) n++;
    }
    return n;
}

/* ================= jobs (each runs on its own thread) ================= */
static ULONGLONG g_last_pct = 999;
static void on_download_progress(ULONGLONG got, ULONGLONG total) {
    if (!total) return;
    ULONGLONG pct = got * 100 / total;
    if (pct == g_last_pct) return;
    g_last_pct = pct;
    post_progress((int)pct);
    post_text(0, L"Downloading... %llu of %llu MB", got >> 20, total >> 20);
}
/* Finds "key": "value" pairs in GitHub's JSON without a JSON library: the release's tag and its .zip. */
static BOOL find_release_zip(const char* json, wchar_t* url, wchar_t* tag) {
    url[0] = tag[0] = 0;
    const char* t = strstr(json, "\"tag_name\"");
    if (t && (t = strchr(t + 10, '"'))) {
        const char* e = strchr(t + 1, '"');
        if (e) { wchar_t* w = from_utf8(t + 1, (int)(e - t - 1)); wcsncpy(tag, w, 63); tag[63] = 0; free(w); }
    }
    for (const char* p = json; (p = strstr(p, "\"browser_download_url\"")); p++) {
        const char* q = strchr(p + 22, '"');
        if (!q) break;
        const char* e = strchr(q + 1, '"');
        if (!e) break;
        if (e - q > 5 && !strncmp(e - 4, ".zip", 4)) {
            wchar_t* w = from_utf8(q + 1, (int)(e - q - 1));
            wcsncpy(url, w, 2047);
            url[2047] = 0;
            free(w);
            return TRUE;
        }
    }
    return FALSE;
}
static BOOL remove_tree(const wchar_t* dir) {
    if (!dir_exists(dir)) return TRUE;
    wchar_t* from = calloc(wcslen(dir) + 2, sizeof(wchar_t)); /* double-terminated */
    wcscpy(from, dir);
    SHFILEOPSTRUCTW op = {0};
    op.wFunc = FO_DELETE;
    op.pFrom = from;
    op.fFlags = FOF_NO_UI;
    int r = SHFileOperationW(&op);
    free(from);
    return r == 0;
}
/* The folder in an unpacked release that holds Launcher.exe: the folder itself, or its one subfolder. */
static BOOL release_root(const wchar_t* unpacked, wchar_t* out) {
    wchar_t p[MAX_PATH];
    path_join(p, unpacked, L"Launcher.exe");
    if (file_exists(p)) { wcscpy(out, unpacked); return TRUE; }
    path_join(p, unpacked, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(p, &fd);
    BOOL found = FALSE;
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == L'.') continue;
        wchar_t sub[MAX_PATH], l[MAX_PATH];
        path_join(sub, unpacked, fd.cFileName);
        path_join(l, sub, L"Launcher.exe");
        if (file_exists(l)) { wcscpy(out, sub); found = TRUE; }
    } while (!found && FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

static DWORD WINAPI download_job(LPVOID arg) {
    int i = (int)(INT_PTR)arg;
    const Game* g = &GAMES[i];
    wchar_t api[256], url[2048], tag[64], games[MAX_PATH], zip[MAX_PATH], unpack[MAX_PATH], root[MAX_PATH], target[MAX_PATH];
    char* json = NULL;
    post_progress(-1);
    post_text(0, L"Asking GitHub for the newest %s...", g->repo);
    _snwprintf(api, 256, L"https://api.github.com/repos/%s/releases/latest", g->github);
    int status = http_get(api, L"application/vnd.github+json", NULL, &json, NULL);
    if (status != 200 || !json || !find_release_zip(json, url, tag)) {
        post_text(0, status ? L"GitHub didn't list a release for %s (HTTP %d)." : L"Couldn't reach GitHub. Check your internet connection.", g->repo, status);
        free(json);
        goto done;
    }
    free(json);

    game_dir(i, target);
    path_join(games, g_dir, L"Games");
    CreateDirectoryW(games, NULL);
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExW(games, &free_bytes, NULL, NULL) && free_bytes.QuadPart < (ULONGLONG)NEED_GB << 30)
        post_text(1, L"Heads up: this drive has %llu GB free. Building a game needs about %d GB.", free_bytes.QuadPart >> 30, NEED_GB);

    _snwprintf(zip, MAX_PATH, L"%s\\%s-download.zip", games, g->repo);
    HANDLE f = CreateFileW(zip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (f == INVALID_HANDLE_VALUE) { post_text(0, L"Couldn't write to %s.", games); goto done; }
    g_last_pct = 999;
    post_text(0, L"Downloading %s %s...", g->repo, tag);
    status = http_get(url, L"application/octet-stream", f, NULL, on_download_progress);
    CloseHandle(f);
    if (status != 200) { post_text(0, L"The download stopped (HTTP %d). Try again.", status); DeleteFileW(zip); goto done; }

    post_progress(-1);
    post_text(0, L"Unpacking...");
    _snwprintf(unpack, MAX_PATH, L"%s\\_unpack_%s", games, g->repo);
    remove_tree(unpack);
    CreateDirectoryW(unpack, NULL);
    wchar_t sys[MAX_PATH];
    GetSystemDirectoryW(sys, MAX_PATH);
    wchar_t* cmd = wdup_printf(L"\"%s\\tar.exe\" -xf \"%s\" -C \"%s\"", sys, zip, unpack);
    int code = run_hidden(cmd, NULL, NULL, INFINITE);
    free(cmd);
    if (code != 0 || !release_root(unpack, root)) {
        post_text(0, L"Couldn't unpack the download (tar exit code %d).", code);
        goto cleanup;
    }

    /* Into place. An update copies over the old folder, keeping its settings, save and build. */
    wchar_t old_ini[MAX_PATH];
    path_join(old_ini, target, L"launcher.ini");
    BOOL keep_ini = file_exists(old_ini);
    if (!dir_exists(target) && MoveFileW(root, target)) {
        code = 0;
    } else {
        cmd = wdup_printf(L"robocopy \"%s\" \"%s\" /E /MOVE /R:1 /W:1 /NFL /NDL /NJH /NJS /NP%s", root, target,
                          keep_ini ? L" /XF launcher.ini" : L"");
        code = run_hidden(cmd, NULL, NULL, INFINITE);
        free(cmd);
        code = code < 8 ? 0 : code;
    }
    if (code) { post_text(0, L"Couldn't move the files into %s (robocopy %d).", target, code); goto cleanup; }
    char key[64];
    hub_key(key, "dir", i);
    hub_set(key, target);
    hub_key(key, "version", i);
    const wchar_t* ver = tag[0] == L'v' ? tag + 1 : tag; /* "v0.1.0" -> "0.1.0", as release.txt has it */
    hub_set(key, ver);
    post_text(0, L"Installed %s %s.", g->repo, ver);
    post_text(1, L"Next: pick your disc image and press Build.");
cleanup:
    remove_tree(unpack);
    DeleteFileW(zip);
done:
    post_progress(0);
    PostMessageW(g_wnd, WM_JOB_DONE, 0, 0);
    return 0;
}

typedef struct { int game; wchar_t rom[MAX_PATH], tool[MAX_PATH]; } BuildArgs;
static DWORD WINAPI build_job(LPVOID arg) {
    BuildArgs* a = arg;
    wchar_t dir[MAX_PATH], launcher[MAX_PATH], ini[MAX_PATH];
    game_dir(a->game, dir);
    game_file(a->game, L"Launcher.exe", launcher);
    game_file(a->game, L"launcher.ini", ini);
    if (a->tool[0]) ini_set(ini, "dolphin_tool", a->tool);
    post_progress(-1);
    post_text(1, L"Building. This takes 30 minutes or more; you can leave it running.");
    wchar_t* cmd = wdup_printf(L"\"%s\" --test-build \"%s\"", launcher, a->rom);
    char* out = NULL;
    int code = run_hidden(cmd, dir, &out, INFINITE);
    free(cmd);
    wchar_t* said = from_utf8(out ? out : "", -1);
    for (size_t n = wcslen(said); n && (said[n - 1] == L'\n' || said[n - 1] == L'\r'); n--) said[n - 1] = 0;
    DWORD mins = (GetTickCount() - g_build_started) / 60000;
    if (code == 0 && game_state(a->game) == BUILT)
        post_text(1, L"Built in %lu min. Press Play!", mins);
    else if (code == -1)
        post_text(1, L"Couldn't start %s.", launcher);
    else
        post_text(1, L"%s", said[0] ? said : L"The build stopped. See build-log.txt in the game's folder.");
    free(said);
    free(out);
    free(a);
    post_progress(0);
    PostMessageW(g_wnd, WM_JOB_DONE, 0, 0);
    return 0;
}

static DWORD WINAPI host_job(LPVOID arg) {
    (void)arg;
    wchar_t exe[MAX_PATH], ip[64] = L"", pub[64] = L"";
    post_progress(-1);
    post_text(2, L"Starting your server...");
    if (!ensure_relay_exe(exe)) {
        post_text(2, L"Install any one of the games first: the server program comes with it.");
        goto done;
    }
    if (!start_relay()) {
        post_text(2, L"The server didn't start. Something else may be using port " RELAY_PORT_S L".\r\nSee Server\\relay-log.txt.");
        goto done;
    }

    /* Windows Firewall: friends' connections come in on this port. */
    post_text(2, L"Server running. Allowing it through Windows Firewall (Windows will ask)...");
    wchar_t* check = wdup_printf(L"netsh advfirewall firewall show rule name=\"%s\"", FIREWALL_RULE);
    BOOL have_rule = run_hidden(check, NULL, NULL, 15000) == 0;
    free(check);
    BOOL firewall_ok = have_rule;
    if (!have_rule) {
        wchar_t* args = wdup_printf(L"advfirewall firewall add rule name=\"%s\" dir=in action=allow protocol=TCP localport=" RELAY_PORT_S, FIREWALL_RULE);
        firewall_ok = run_as_admin(L"netsh.exe", args);
        free(args);
    }

    local_ip(ip, 64);

    /* The router: ask it over UPnP to forward the port to this PC. */
    post_text(2, L"Asking your router to open port " RELAY_PORT_S L"...");
    BOOL upnp_ok = FALSE;
    wchar_t router[64] = L"";
    if (ip[0]) {
        /* Prints the router's address ("GW=..."), for telling the player where its settings are, then asks for the
           port. Windows' UPnP helper can say yes without the router having done anything, so it only counts if
           the router then lists the rule, which the script reports as a line of its own: "RESULT=MAPPED". */
        wchar_t* script = wdup_printf(
            L"$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue';\n"
            L"try{'GW='+(Get-NetRoute -DestinationPrefix 0.0.0.0/0|Sort-Object RouteMetric|Select-Object -First 1).NextHop}catch{}\n"
            L"try{$c=(New-Object -ComObject HNetCfg.NATUPnP).StaticPortMappingCollection}catch{'RESULT=NOUPNP';exit}\n"
            L"if($c -eq $null){'RESULT=NOUPNP';exit}\n"
            L"try{$c.Remove(%d,'TCP')}catch{}\n"
            L"try{$null=$c.Add(%d,'TCP',%d,'%s',$true,'Recomp Hub relay')}catch{'RESULT=REFUSED';exit}\n"
            L"$c=(New-Object -ComObject HNetCfg.NATUPnP).StaticPortMappingCollection\n"
            L"$found=$false;if($c){foreach($m in $c){if($m.ExternalPort -eq %d -and $m.Protocol -eq 'TCP' -and $m.InternalClient -eq '%s'){$found=$true}}}\n"
            L"if($found){'RESULT=MAPPED'}else{'RESULT=NOTLISTED'}\n",
            RELAY_PORT, RELAY_PORT, RELAY_PORT, ip, RELAY_PORT, ip);
        char* out = NULL;
        int code = run_powershell(script, &out, 45000);
        upnp_ok = code == 0 && out && (!strncmp(out, "RESULT=MAPPED\r\n", 15) || strstr(out, "\nRESULT=MAPPED\r\n"));
        wchar_t sdir[MAX_PATH], log[MAX_PATH];
        server_dir(sdir);
        path_join(log, sdir, L"upnp-log.txt"); /* what the router said, for anyone working out why */
        if (out) write_file(log, out, (DWORD)strlen(out));
        const char* gw = out ? strstr(out, "GW=") : NULL;
        if (gw) {
            int n = 0;
            for (gw += 3; n < 63 && (isdigit((unsigned char)gw[n]) || gw[n] == '.'); n++) router[n] = gw[n];
            router[n] = 0;
        }
        free(out);
        free(script);
    }

    post_text(2, L"Finding your internet address...");
    char* body = NULL;
    if (http_get(L"https://api.ipify.org", NULL, NULL, &body, NULL) == 200 && body) {
        wchar_t* w = from_utf8(body, -1);
        wcsncpy(pub, w, 63);
        pub[63] = 0;
        for (wchar_t* c = pub; *c; c++) if (!iswdigit(*c) && *c != L'.' && *c != L':' && !iswxdigit(*c)) { *c = 0; break; }
        free(w);
    }
    free(body);

    int set = ip[0] ? point_games_at(ip) : 0;
    wcsncpy(g_friend_addr, pub[0] ? pub : ip, 127);
    /* Most important first: the box shows about eight lines before it scrolls. */
    wchar_t router_text[400];
    if (upnp_ok)
        _snwprintf(router_text, 400, L"Router: port " RELAY_PORT_S L" opened automatically.");
    else
        _snwprintf(router_text, 400,
                   L"Router: it didn't open port " RELAY_PORT_S L" (UPnP is probably off in the router). People on your Wi-Fi can "
                   L"play already. For friends in other houses, open your router's settings%s%s%s and either turn on UPnP "
                   L"and press Set up again, or forward TCP port " RELAY_PORT_S L" to %s.",
                   router[0] ? L" (http://" : L"", router, router[0] ? L")" : L"", ip[0] ? ip : L"this PC");
    post_text(2,
              L"Your server is running. %d installed game(s) now use it.\r\n"
              L"Friends in other houses type:   %s\r\n"
              L"People on your Wi-Fi type:   %s\r\n"
              L"Then open a game, go to Online, press Host, and send your friends the room code.\r\n\r\n"
              L"Windows Firewall: %s\r\n%s",
              set, pub[0] ? pub : L"(couldn't find it)", ip[0] ? ip : L"(couldn't find it)",
              firewall_ok ? L"allowed." : L"not allowed yet. Press Set up again and say Yes when Windows asks.",
              router_text);
done:
    post_progress(0);
    PostMessageW(g_wnd, WM_JOB_DONE, 0, 0);
    return 0;
}

/* Deletes a game's folder (one Recomp Hub made) and forgets it. */
static DWORD WINAPI uninstall_job(LPVOID arg) {
    int i = (int)(INT_PTR)arg;
    wchar_t d[MAX_PATH];
    char key[64];
    game_dir(i, d);
    post_progress(-1);
    post_text(0, L"Uninstalling %s...", GAMES[i].title);
    remove_tree(d);
    if (dir_exists(d)) {
        post_text(0, L"Some files in %s couldn't be deleted. Close anything using them and press Uninstall again.", d);
    } else {
        hub_key(key, "dir", i);
        hub_set(key, NULL);
        hub_key(key, "version", i);
        hub_set(key, NULL);
        post_text(0, L"%s is uninstalled. Your disc image wasn't touched.", GAMES[i].title);
    }
    post_progress(0);
    PostMessageW(g_wnd, WM_JOB_DONE, 0, 0);
    return 0;
}

/* Undoes "Set up my server": the relay, starting at sign-in, the router port, the firewall rule, Server\, and
   games pointed at this PC. Joining a friend's server is untouched. */
static DWORD WINAPI remove_server_job(LPVOID arg) {
    (void)arg;
    wchar_t ip[64] = L"", dir[MAX_PATH], lines[1200] = L"";
    post_progress(-1);
    post_text(2, L"Uninstalling your server...");
    count_processes(L"wsr_relay.exe", TRUE);
    Sleep(300);
    wcscat(lines, relay_running() ? L"Server: still running (end wsr_relay.exe in Task Manager).\r\n" : L"Server: stopped.\r\n");
    set_autostart(FALSE);
    wcscat(lines, L"Starting at sign-in: off.\r\n");

    /* The router: remove the port rule, only if it's the one Recomp Hub added. */
    post_text(2, L"Asking your router to close port " RELAY_PORT_S L"...");
    char* out = NULL;
    run_powershell(
        L"$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'\n"
        L"try{$c=(New-Object -ComObject HNetCfg.NATUPnP).StaticPortMappingCollection}catch{'RESULT=NONE';exit}\n"
        L"if($c -eq $null){'RESULT=NONE';exit}\n"
        L"$m=$false;foreach($x in $c){if($x.ExternalPort -eq " RELAY_PORT_S L" -and $x.Protocol -eq 'TCP' -and $x.Description -eq 'Recomp Hub relay'){$m=$true}}\n"
        L"if(-not $m){'RESULT=NONE';exit}\n"
        L"try{$c.Remove(" RELAY_PORT_S L",'TCP');'RESULT=REMOVED'}catch{'RESULT=FAILED'}\n",
        &out, 45000);
    wcscat(lines, out && strstr(out, "RESULT=REMOVED") ? L"Router: port " RELAY_PORT_S L" closed.\r\n"
                  : out && strstr(out, "RESULT=FAILED") ? L"Router: it wouldn't close port " RELAY_PORT_S L"; remove it in your router's settings.\r\n"
                  : L"Router: nothing to close.\r\n");
    free(out);

    /* Windows Firewall: removing the rule needs administrator rights, so Windows asks. */
    wchar_t* check = wdup_printf(L"netsh advfirewall firewall show rule name=\"%s\"", FIREWALL_RULE);
    BOOL have_rule = run_hidden(check, NULL, NULL, 15000) == 0;
    free(check);
    if (have_rule) {
        post_text(2, L"Removing the Windows Firewall rule (Windows will ask)...");
        wchar_t* args = wdup_printf(L"advfirewall firewall delete rule name=\"%s\"", FIREWALL_RULE);
        BOOL ok = run_as_admin(L"netsh.exe", args);
        free(args);
        wcscat(lines, ok ? L"Windows Firewall: rule removed.\r\n"
                         : L"Windows Firewall: rule NOT removed (Windows didn't get a Yes). Press Uninstall server again.\r\n");
    } else {
        wcscat(lines, L"Windows Firewall: no rule to remove.\r\n");
    }

    /* Games pointed at this PC's server go back to their own setting. */
    local_ip(ip, 64);
    int unset = 0;
    for (int i = 0; i < NGAMES; i++) {
        wchar_t ini[MAX_PATH], exe[MAX_PATH], v[128];
        if (!game_file(i, L"Launcher.exe", exe)) continue;
        game_file(i, L"launcher.ini", ini);
        ini_get(ini, "relay", v, 128);
        if (v[0] && ((ip[0] && !_wcsicmp(v, ip)) || !_wcsicmp(v, L"127.0.0.1") || !_wcsicmp(v, L"localhost"))) {
            ini_set(ini, "relay", NULL);
            unset++;
        }
    }
    wchar_t games_line[160];
    _snwprintf(games_line, 160, L"Games that used this server: %d set back.\r\n", unset);
    wcscat(lines, games_line);

    server_dir(dir);
    remove_tree(dir);
    wcscat(lines, dir_exists(dir) ? L"Server folder: couldn't delete all of it.\r\n" : L"Server folder: deleted.\r\n");
    g_friend_addr[0] = 0;
    post_text(2, L"Your server is uninstalled.\r\n\r\n%sJoining a friend's server still works.", lines);
    post_progress(0);
    PostMessageW(g_wnd, WM_JOB_DONE, 0, 0);
    return 0;
}

/* ================= the window ================= */
static HWND add(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)(font ? font : g_font), TRUE);
    return c;
}
static const int GAME_IDS[] = {ID_TITLE, ID_BLURB, ID_G1, ID_G2, ID_G3, ID_GETSTATUS, ID_DOWNLOAD, ID_EXISTING, ID_DLPROG,
                               ID_ROMLBL, ID_ROM, ID_ROMBROWSE, ID_TOOLLBL, ID_TOOL, ID_TOOLBROWSE, ID_BUILD, ID_BUILDSTATUS,
                               ID_BUILDDETAIL, ID_BUILDPROG, ID_PLAY, ID_OPENDIR, ID_PLAYNOTE, ID_UNINSTALL};
static const int ONLINE_IDS[] = {ID_OTITLE, ID_OBLURB, ID_OG1, ID_HOST, ID_STOP, ID_COPY, ID_OSTATUS, ID_AUTOSTART,
                                 ID_OG2, ID_JOINLBL, ID_JOIN, ID_USEJOIN, ID_JOINNOTE, ID_UNSERVER};

static void create_controls(void) {
    const DWORD V = WS_VISIBLE;
    add(L"STATIC", L"Recomp Hub", V, 18, 14, 240, 34, ID_HEAD, g_font_head);
    add(L"STATIC", L"Your Wii recomps, in one place", V, 20, 48, 240, 20, ID_SUBHEAD, g_font_small);
    for (int i = 0; i <= NGAMES; i++) {
        HWND b = add(L"BUTTON", i < NGAMES ? GAMES[i].title : L"Play online", V | WS_TABSTOP | BS_COMMANDLINK,
                     12, i < NGAMES ? 80 + i * 78 : 80 + i * 78 + 18, 248, 72, ID_NAV + i, NULL);
        (void)b;
    }
    const int X = 290, W = 572;
    /* a game's page */
    add(L"STATIC", L"", 0, X, 16, W, 36, ID_TITLE, g_font_big);
    add(L"STATIC", L"", 0, X, 54, W, 20, ID_BLURB, NULL);
    add(L"BUTTON", L"1   Get the game files", BS_GROUPBOX, X, 84, W, 112, ID_G1, NULL);
    add(L"STATIC", L"", SS_PATHELLIPSIS, X + 16, 108, W - 32, 20, ID_GETSTATUS, NULL); /* a long folder gets "..." */
    add(L"BUTTON", L"Download && install", WS_TABSTOP, X + 16, 134, 190, 32, ID_DOWNLOAD, NULL);
    add(L"BUTTON", L"Use a folder I already have...", WS_TABSTOP, X + 216, 134, 230, 32, ID_EXISTING, NULL);
    add(L"BUTTON", L"Uninstall", WS_TABSTOP, X + 456, 134, W - 472, 32, ID_UNINSTALL, NULL);
    add(PROGRESS_CLASSW, L"", 0, X + 16, 174, W - 32, 10, ID_DLPROG, NULL);
    add(L"BUTTON", L"2   Build it from your disc", BS_GROUPBOX, X, 206, W, 210, ID_G2, NULL);
    add(L"STATIC", L"Your disc image (.iso, .wbfs, .rvz...)", 0, X + 16, 230, W - 32, 18, ID_ROMLBL, NULL);
    add(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, X + 16, 250, W - 134, 26, ID_ROM, NULL);
    add(L"BUTTON", L"Browse...", WS_TABSTOP, X + W - 110, 249, 94, 28, ID_ROMBROWSE, NULL);
    add(L"STATIC", L"DolphinTool.exe (it comes with Dolphin; only used to unpack the disc)", 0, X + 16, 284, W - 32, 18, ID_TOOLLBL, NULL);
    add(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, X + 16, 304, W - 134, 26, ID_TOOL, NULL);
    add(L"BUTTON", L"Browse...", WS_TABSTOP, X + W - 110, 303, 94, 28, ID_TOOLBROWSE, NULL);
    add(L"BUTTON", L"Build", WS_TABSTOP, X + 16, 344, 190, 34, ID_BUILD, NULL);
    add(PROGRESS_CLASSW, L"", 0, X + 16, 388, 190, 10, ID_BUILDPROG, NULL);
    add(L"STATIC", L"", SS_EDITCONTROL, X + 220, 340, W - 236, 38, ID_BUILDSTATUS, NULL);
    add(L"STATIC", L"", SS_ENDELLIPSIS, X + 220, 382, W - 236, 20, ID_BUILDDETAIL, g_font_small);
    add(L"BUTTON", L"3   Play", BS_GROUPBOX, X, 426, W, 86, ID_G3, NULL);
    add(L"BUTTON", L"Play", WS_TABSTOP | BS_DEFPUSHBUTTON, X + 16, 452, 190, 40, ID_PLAY, g_font_big);
    add(L"BUTTON", L"Open folder", WS_TABSTOP, X + 216, 452, 120, 40, ID_OPENDIR, NULL);
    add(L"STATIC", L"Play opens the game's own launcher: graphics, controllers, Mii Maker and online rooms.", 0, X + 348, 450, W - 362, 48, ID_PLAYNOTE, g_font_small);
    /* the online page */
    add(L"STATIC", L"Play online", 0, X, 16, W, 36, ID_OTITLE, g_font_big);
    add(L"STATIC", L"Online play goes through a small relay server (one works for all three games). One person runs it; "
                   L"everyone else only types its address.", 0, X, 54, W, 36, ID_OBLURB, NULL);
    add(L"BUTTON", L"Host a server on this PC", BS_GROUPBOX, X, 96, W, 268, ID_OG1, NULL);
    add(L"BUTTON", L"Set up my server for me", WS_TABSTOP | BS_DEFPUSHBUTTON, X + 16, 122, 220, 36, ID_HOST, NULL);
    add(L"BUTTON", L"Stop server", WS_TABSTOP, X + 246, 122, 120, 36, ID_STOP, NULL);
    add(L"BUTTON", L"Copy address for friends", WS_TABSTOP, X + 376, 122, W - 392, 36, ID_COPY, NULL);
    add(L"EDIT", L"", WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, X + 16, 168, W - 32, 156, ID_OSTATUS, NULL);
    add(L"BUTTON", L"Start my server automatically when I sign in to Windows", WS_TABSTOP | BS_AUTOCHECKBOX, X + 16, 332, W - 192, 22, ID_AUTOSTART, NULL);
    add(L"BUTTON", L"Uninstall server", WS_TABSTOP, X + W - 166, 328, 150, 30, ID_UNSERVER, NULL);
    add(L"BUTTON", L"Join a friend's server", BS_GROUPBOX, X, 374, W, 118, ID_OG2, NULL);
    add(L"STATIC", L"The server address your friend gave you", 0, X + 16, 398, W - 32, 18, ID_JOINLBL, NULL);
    add(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, X + 16, 418, W - 186, 28, ID_JOIN, NULL);
    add(L"BUTTON", L"Use this server", WS_TABSTOP, X + W - 160, 417, 144, 30, ID_USEJOIN, NULL);
    add(L"STATIC", L"Then open a game, go to Online, and join with the room code the host gives you.", 0, X + 16, 456, W - 32, 20, ID_JOINNOTE, g_font_small);
}

static void refresh_nav(void) {
    for (int i = 0; i < NGAMES; i++) {
        int st = game_state(i);
        wchar_t ver[64] = L"", key_note[160];
        char key[64];
        hub_key(key, "version", i);
        hub_get(key, ver, 64);
        _snwprintf(key_note, 160, L"%s%s%s", st == BUILT ? L"Ready to play" : st == INSTALLED ? L"Installed: needs building" : L"Not installed",
                   ver[0] ? L"  \x00B7  " : L"", ver);
        SendMessageW(ctl(ID_NAV + i), BCM_SETNOTE, 0, (LPARAM)key_note);
    }
    SendMessageW(ctl(ID_NAV + PAGE_ONLINE), BCM_SETNOTE, 0,
                 (LPARAM)(relay_running() ? L"Your server is running" : L"Host a server, or join a friend's"));
}

static void refresh_page(void) {
    BOOL busy = g_busy != 0;
    for (size_t k = 0; k < sizeof GAME_IDS / sizeof *GAME_IDS; k++) ShowWindow(ctl(GAME_IDS[k]), g_page < NGAMES ? SW_SHOW : SW_HIDE);
    for (size_t k = 0; k < sizeof ONLINE_IDS / sizeof *ONLINE_IDS; k++) ShowWindow(ctl(ONLINE_IDS[k]), g_page == PAGE_ONLINE ? SW_SHOW : SW_HIDE);
    for (int i = 0; i <= NGAMES; i++) EnableWindow(ctl(ID_NAV + i), TRUE);
    if (g_page < NGAMES) {
        int i = g_page, st = game_state(i);
        BOOL mine = g_job_game == i; /* this page's game is the one working */
        SetWindowTextW(ctl(ID_TITLE), GAMES[i].title);
        SetWindowTextW(ctl(ID_BLURB), GAMES[i].blurb);
        if (g_msg[i][0][0]) {
            SetWindowTextW(ctl(ID_GETSTATUS), g_msg[i][0]);
        } else {
            wchar_t d[MAX_PATH], ver[64] = L"";
            char key[64];
            game_dir(i, d);
            hub_key(key, "version", i);
            hub_get(key, ver, 64);
            wchar_t* t = st == NOT_INSTALLED ? wdup_printf(L"%s isn't installed yet. About 160 MB from GitHub.", GAMES[i].repo)
                                             : wdup_printf(L"Installed%s%s in %s", ver[0] ? L" " : L"", ver, d);
            SetWindowTextW(ctl(ID_GETSTATUS), t);
            free(t);
        }
        SetWindowTextW(ctl(ID_DOWNLOAD), st == NOT_INSTALLED ? L"Download && install" : L"Update to newest");
        EnableWindow(ctl(ID_DOWNLOAD), !busy);
        EnableWindow(ctl(ID_EXISTING), !busy);
        EnableWindow(ctl(ID_UNINSTALL), !busy && st != NOT_INSTALLED);
        EnableWindow(ctl(ID_BUILD), !busy && st != NOT_INSTALLED);
        EnableWindow(ctl(ID_ROMBROWSE), !busy);
        EnableWindow(ctl(ID_TOOLBROWSE), !busy);
        EnableWindow(ctl(ID_PLAY), st == BUILT && !(busy && mine));
        EnableWindow(ctl(ID_OPENDIR), st != NOT_INSTALLED);
        SetWindowTextW(ctl(ID_BUILD), st == BUILT ? L"Build again" : L"Build");
        if (!(busy && mine)) {
            wchar_t rom[MAX_PATH] = L"", tool[MAX_PATH] = L"";
            char key[64];
            hub_key(key, "rom", i);
            hub_get(key, rom, MAX_PATH);
            SetWindowTextW(ctl(ID_ROM), rom);
            find_dolphin_tool(tool);
            SetWindowTextW(ctl(ID_TOOL), tool);
            SetWindowTextW(ctl(ID_BUILDSTATUS), g_msg[i][1][0] ? g_msg[i][1] : st == BUILT ? L"Built and ready." : st == INSTALLED ? L"Pick your disc image and press Build." : L"Install the game files first (step 1).");
            SetWindowTextW(ctl(ID_BUILDDETAIL), L"");
        }
        ShowWindow(ctl(ID_DLPROG), busy && mine && g_prog_id == ID_DLPROG ? SW_SHOW : SW_HIDE);
        ShowWindow(ctl(ID_BUILDPROG), busy && mine && g_prog_id == ID_BUILDPROG ? SW_SHOW : SW_HIDE);
    } else {
        BOOL running = relay_running();
        EnableWindow(ctl(ID_HOST), !busy);
        EnableWindow(ctl(ID_STOP), !busy && running);
        EnableWindow(ctl(ID_UNSERVER), !busy);
        EnableWindow(ctl(ID_COPY), g_friend_addr[0] != 0);
        SetWindowTextW(ctl(ID_HOST), running ? L"Set up my server again" : L"Set up my server for me");
        SendMessageW(ctl(ID_AUTOSTART), BM_SETCHECK, autostart_on() ? BST_CHECKED : BST_UNCHECKED, 0);
        if (!busy && GetWindowTextLengthW(ctl(ID_OSTATUS)) == 0)
            SetWindowTextW(ctl(ID_OSTATUS), running ? L"Your server is running. Press \"Set up my server again\" to see its addresses."
                                                    : L"Press \"Set up my server for me\". It starts the server, lets it through Windows Firewall, "
                                                      L"asks your router to open the port, and points your games at it.");
        wchar_t join[128];
        hub_get("relay_join", join, 128);
        if (GetWindowTextLengthW(ctl(ID_JOIN)) == 0) SetWindowTextW(ctl(ID_JOIN), join);
        SetWindowTextW(ctl(ID_COPY), L"Copy address for friends");
    }
    InvalidateRect(g_wnd, NULL, FALSE); /* the marker beside the open page */
}

static BOOL pick_file(const wchar_t* filter, wchar_t* out) {
    OPENFILENAMEW of = {sizeof of};
    wchar_t buf[MAX_PATH];
    wcscpy(buf, out);
    of.hwndOwner = g_wnd;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&of)) return FALSE;
    wcscpy(out, buf);
    return TRUE;
}
static BOOL pick_folder(wchar_t* out) {
    IFileOpenDialog* d = NULL;
    BOOL ok = FALSE;
    if (FAILED(CoCreateInstance(&CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, &IID_IFileOpenDialog, (void**)&d))) return FALSE;
    DWORD opts;
    IFileOpenDialog_GetOptions(d, &opts);
    IFileOpenDialog_SetOptions(d, opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    IFileOpenDialog_SetTitle(d, L"The folder with that recomp's Launcher.exe");
    if (SUCCEEDED(IFileOpenDialog_Show(d, g_wnd))) {
        IShellItem* item;
        if (SUCCEEDED(IFileOpenDialog_GetResult(d, &item))) {
            wchar_t* p;
            if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &p))) {
                wcsncpy(out, p, MAX_PATH - 1);
                out[MAX_PATH - 1] = 0;
                CoTaskMemFree(p);
                ok = TRUE;
            }
            IShellItem_Release(item);
        }
    }
    IFileOpenDialog_Release(d);
    return ok;
}

static void start_job(LPTHREAD_START_ROUTINE fn, void* arg, int game, int prog_id) {
    if (InterlockedCompareExchange(&g_busy, 1, 0)) return;
    g_job_game = game;
    g_prog_id = prog_id;
    if (game >= 0) g_msg[game][0][0] = g_msg[game][1][0] = 0;
    refresh_page();
    HANDLE t = CreateThread(NULL, 0, fn, arg, 0, NULL);
    if (t) CloseHandle(t); else { g_busy = 0; g_job_game = -1; refresh_page(); }
}

/* While building: elapsed time and the newest line of the game's build-log.txt. */
static void show_build_detail(void) {
    if (g_job_game < 0 || g_page != g_job_game) return;
    wchar_t log[MAX_PATH], line[200] = L"";
    game_file(g_job_game, L"build-log.txt", log);
    HANDLE f = CreateFileW(log, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        GetFileSizeEx(f, &size);
        char buf[4097];
        LONGLONG from = size.QuadPart > 4096 ? size.QuadPart - 4096 : 0;
        LARGE_INTEGER at = {.QuadPart = from};
        SetFilePointerEx(f, at, NULL, FILE_BEGIN);
        DWORD got = 0;
        ReadFile(f, buf, 4096, &got, NULL);
        buf[got] = 0;
        CloseHandle(f);
        int end = (int)got;
        while (end > 0 && (buf[end - 1] == '\n' || buf[end - 1] == '\r' || buf[end - 1] == ' ')) end--;
        int start = end;
        while (start > 0 && buf[start - 1] != '\n') start--;
        if (end > start) {
            wchar_t* w = from_utf8(buf + start, end - start);
            wcsncpy(line, w, 199);
            line[199] = 0;
            free(w);
        }
    }
    DWORD s = (GetTickCount() - g_build_started) / 1000;
    wchar_t text[260];
    if (!wcsncmp(line, L"gcc ", 4) || wcsstr(line, L"gcc.exe ")) wcscpy(line, L"Compiling the game's code...");
    _snwprintf(text, 260, L"%lu:%02lu elapsed   %s", s / 60, s % 60, line);
    SetWindowTextW(ctl(ID_BUILDDETAIL), text);
}

static void on_command(int id) {
    if (id >= ID_NAV && id <= ID_NAV + NGAMES) {
        g_page = id - ID_NAV;
        refresh_page();
        return;
    }
    int i = g_page;
    char key[64];
    wchar_t buf[MAX_PATH];
    switch (id) {
    case ID_DOWNLOAD:
        start_job(download_job, (void*)(INT_PTR)i, i, ID_DLPROG);
        break;
    case ID_EXISTING:
        if (pick_folder(buf)) {
            wchar_t l[MAX_PATH];
            path_join(l, buf, L"Launcher.exe");
            if (!file_exists(l)) {
                MessageBoxW(g_wnd, L"There's no Launcher.exe in that folder. Pick the folder you unpacked the recomp into.", L"Recomp Hub", MB_ICONWARNING);
                break;
            }
            hub_key(key, "dir", i);
            hub_set(key, buf);
            wchar_t rel[MAX_PATH];
            path_join(rel, buf, L"release.txt");
            char* v = read_file(rel, NULL);
            if (v) {
                char* nl = strpbrk(v, "\r\n");
                if (nl) *nl = 0;
                wchar_t* w = from_utf8(v, -1);
                hub_key(key, "version", i);
                hub_set(key, w);
                free(w);
                free(v);
            }
            refresh_nav();
            refresh_page();
        }
        break;
    case ID_ROMBROWSE:
        GetWindowTextW(ctl(ID_ROM), buf, MAX_PATH);
        if (pick_file(L"Wii disc images\0*.iso;*.wbfs;*.rvz;*.wia;*.gcz;*.ciso\0All files\0*.*\0", buf)) {
            SetWindowTextW(ctl(ID_ROM), buf);
            hub_key(key, "rom", i);
            hub_set(key, buf);
        }
        break;
    case ID_TOOLBROWSE:
        GetWindowTextW(ctl(ID_TOOL), buf, MAX_PATH);
        if (pick_file(L"DolphinTool\0DolphinTool.exe\0Programs\0*.exe\0", buf)) {
            SetWindowTextW(ctl(ID_TOOL), buf);
            hub_set("dolphin_tool", buf);
        }
        break;
    case ID_BUILD: {
        if (game_state(i) == NOT_INSTALLED) break; /* the button is off then, but a message can still reach it */
        BuildArgs* a = calloc(1, sizeof *a);
        a->game = i;
        GetWindowTextW(ctl(ID_ROM), a->rom, MAX_PATH);
        GetWindowTextW(ctl(ID_TOOL), a->tool, MAX_PATH);
        if (!a->rom[0] || (!file_exists(a->rom) && !dir_exists(a->rom))) {
            MessageBoxW(g_wnd, L"Pick your disc image first.", L"Recomp Hub", MB_ICONINFORMATION);
            free(a);
            break;
        }
        if (!dir_exists(a->rom) && !file_exists(a->tool)) {
            MessageBoxW(g_wnd, L"Recomp Hub needs DolphinTool.exe to unpack the disc. It's in your Dolphin folder: install Dolphin "
                               L"from dolphin-emu.org if you don't have it, then press Browse next to DolphinTool.", L"Recomp Hub", MB_ICONINFORMATION);
            free(a);
            break;
        }
        hub_key(key, "rom", i);
        hub_set(key, a->rom);
        if (a->tool[0]) hub_set("dolphin_tool", a->tool);
        g_build_started = GetTickCount();
        SetTimer(g_wnd, TIMER_BUILD, 1000, NULL);
        start_job(build_job, a, i, ID_BUILDPROG);
        break;
    }
    case ID_PLAY:
    case ID_OPENDIR: {
        wchar_t d[MAX_PATH], exe[MAX_PATH];
        game_dir(i, d);
        path_join(exe, d, L"Launcher.exe");
        ShellExecuteW(g_wnd, L"open", id == ID_PLAY ? exe : d, NULL, d, SW_SHOWNORMAL);
        break;
    }
    case ID_UNINSTALL: {
        if (game_state(i) == NOT_INSTALLED) break;
        wchar_t d[MAX_PATH];
        game_dir(i, d);
        if (running_from(d)) {
            wchar_t* m = wdup_printf(L"%s or its launcher is still open. Close it first, then press Uninstall again.", GAMES[i].title);
            MessageBoxW(g_wnd, m, L"Recomp Hub", MB_ICONINFORMATION);
            free(m);
            break;
        }
        if (hub_made_folder(i)) {
            wchar_t* m = wdup_printf(L"Uninstall %s?\n\nThis deletes %s: the game files, the built game, and its save data "
                                     L"(your Miis and records). To keep those, first export them with Save data in the "
                                     L"game's launcher (press Play).\n\nYour disc image isn't touched.", GAMES[i].title, d);
            int yes = MessageBoxW(g_wnd, m, L"Uninstall", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
            free(m);
            if (yes) start_job(uninstall_job, (void*)(INT_PTR)i, i, ID_DLPROG);
        } else {
            /* A folder the player chose with "Use a folder I already have": it may hold their own work, so the hub
               only lets go of it and deletes nothing. */
            wchar_t* m = wdup_printf(L"Recomp Hub didn't install %s; you chose this folder:\n\n%s\n\nRemove it from Recomp Hub? "
                                     L"Nothing is deleted: the folder stays where it is.", GAMES[i].title, d);
            int yes = MessageBoxW(g_wnd, m, L"Uninstall", MB_YESNO | MB_ICONQUESTION) == IDYES;
            free(m);
            if (yes) {
                hub_key(key, "dir", i);
                hub_set(key, NULL);
                hub_key(key, "version", i);
                hub_set(key, NULL);
                _snwprintf(g_msg[i][0], 600, L"Recomp Hub no longer uses %s (nothing was deleted).", d);
                g_msg[i][1][0] = 0;
                refresh_nav();
                refresh_page();
            }
        }
        break;
    }
    case ID_UNSERVER:
        if (MessageBoxW(g_wnd, L"Uninstall your server from this PC?\n\nThis stops it, turns off starting at sign-in, closes the "
                               L"router port if Recomp Hub opened it, removes the Windows Firewall rule (Windows will ask), "
                               L"and deletes the Server folder. Joining a friend's server still works.",
                        L"Uninstall server", MB_YESNO | MB_ICONQUESTION) != IDYES)
            break;
        SetWindowTextW(ctl(ID_OSTATUS), L"");
        start_job(remove_server_job, NULL, -1, 0);
        break;
    case ID_HOST:
        SetWindowTextW(ctl(ID_OSTATUS), L"");
        start_job(host_job, NULL, -1, 0);
        break;
    case ID_STOP:
        count_processes(L"wsr_relay.exe", TRUE);
        Sleep(200);
        SetWindowTextW(ctl(ID_OSTATUS), L"Server stopped.");
        refresh_nav();
        refresh_page();
        break;
    case ID_COPY:
        if (g_friend_addr[0] && OpenClipboard(g_wnd)) {
            EmptyClipboard();
            size_t n = (wcslen(g_friend_addr) + 1) * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, n);
            memcpy(GlobalLock(h), g_friend_addr, n);
            GlobalUnlock(h);
            SetClipboardData(CF_UNICODETEXT, h);
            CloseClipboard();
            SetWindowTextW(ctl(ID_COPY), L"Copied!");
        }
        break;
    case ID_AUTOSTART:
        set_autostart(SendMessageW(ctl(ID_AUTOSTART), BM_GETCHECK, 0, 0) == BST_CHECKED);
        break;
    case ID_USEJOIN: {
        wchar_t addr[128];
        GetWindowTextW(ctl(ID_JOIN), addr, 128);
        wchar_t* p = addr;
        while (*p == L' ') p++;
        for (size_t n = wcslen(p); n && p[n - 1] == L' '; n--) p[n - 1] = 0;
        if (!*p) { MessageBoxW(g_wnd, L"Type the address your friend gave you.", L"Recomp Hub", MB_ICONINFORMATION); break; }
        hub_set("relay_join", p);
        int n = point_games_at(p);
        wchar_t* msg = n ? wdup_printf(L"Done: %d installed game(s) now use %s.\n\nOpen a game, go to Online, and join with the host's room code.", n, p)
                         : wdup_printf(L"Saved %s. Install a game and press Use this server again.", p);
        MessageBoxW(g_wnd, msg, L"Recomp Hub", MB_ICONINFORMATION);
        free(msg);
        break;
    }
    }
}

static LRESULT CALLBACK wnd_proc(HWND w, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE:
        g_wnd = w;
        create_controls();
        refresh_nav();
        refresh_page();
        return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == BN_CLICKED) on_command(LOWORD(wp));
        return 0;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, RGB(255, 255, 255));
        int id = GetDlgCtrlID((HWND)lp);
        SetTextColor(dc, id == ID_SUBHEAD || id == ID_BUILDDETAIL || id == ID_PLAYNOTE || id == ID_JOINNOTE ? RGB(96, 96, 96) : RGB(20, 20, 20));
        return (LRESULT)g_white;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(w, &ps);
        RECT r;
        GetClientRect(w, &r);
        RECT line = {S(272), 0, S(273), r.bottom};
        HBRUSH b = CreateSolidBrush(RGB(225, 225, 225));
        FillRect(dc, &line, b);
        DeleteObject(b);
        RECT nav;
        RECT clear = {0, 0, S(11), r.bottom};
        FillRect(dc, &clear, g_white);
        GetWindowRect(ctl(ID_NAV + g_page), &nav);
        MapWindowPoints(NULL, w, (POINT*)&nav, 2);
        RECT mark = {S(4), nav.top + S(8), S(8), nav.bottom - S(8)};
        b = CreateSolidBrush(RGB(0, 103, 192));
        FillRect(dc, &mark, b);
        DeleteObject(b);
        EndPaint(w, &ps);
        return 0;
    }
    case WM_JOB_STATUS: {
        wchar_t* text = (wchar_t*)lp;
        if (wp == 2) {
            SetWindowTextW(ctl(ID_OSTATUS), text);
            SendMessageW(ctl(ID_OSTATUS), EM_SETSEL, 0, 0); /* show the top: the addresses */
            SendMessageW(ctl(ID_OSTATUS), EM_SCROLLCARET, 0, 0);
        } else if (g_job_game >= 0) {
            wcsncpy(g_msg[g_job_game][wp], text, 599);
            g_msg[g_job_game][wp][599] = 0;
            if (g_page == g_job_game) SetWindowTextW(ctl(wp == 0 ? ID_GETSTATUS : ID_BUILDSTATUS), text);
        }
        free(text);
        return 0;
    }
    case WM_JOB_PROGRESS: {
        HWND bar = g_prog_id ? ctl(g_prog_id) : NULL;
        if (!bar) return 0;
        int pct = (int)wp;
        LONG_PTR st = GetWindowLongPtrW(bar, GWL_STYLE);
        if (pct < 0) {
            if (!(st & PBS_MARQUEE)) SetWindowLongPtrW(bar, GWL_STYLE, st | PBS_MARQUEE);
            SendMessageW(bar, PBM_SETMARQUEE, TRUE, 30);
        } else {
            SendMessageW(bar, PBM_SETMARQUEE, FALSE, 0);
            if (st & PBS_MARQUEE) SetWindowLongPtrW(bar, GWL_STYLE, st & ~PBS_MARQUEE);
            SendMessageW(bar, PBM_SETPOS, pct, 0);
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == TIMER_BUILD) show_build_detail();
        return 0;
    case WM_JOB_DONE:
        KillTimer(w, TIMER_BUILD);
        g_busy = 0;
        g_job_game = -1;
        refresh_nav();
        refresh_page();
        return 0;
    case WM_CLOSE:
        if (g_busy && MessageBoxW(w, L"Recomp Hub is still working. Close it anyway? (A build in progress keeps running until it finishes.)",
                                  L"Recomp Hub", MB_YESNO | MB_ICONQUESTION) != IDYES)
            return 0;
        DestroyWindow(w);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev;
    g_inst = inst;
    GetModuleFileNameW(NULL, g_exe, MAX_PATH);
    wcscpy(g_dir, g_exe);
    *wcsrchr(g_dir, L'\\') = 0;
    path_join(g_ini, g_dir, L"RecompHub.ini");
    if (cmd && wcsstr(cmd, L"--server")) return start_relay() ? 0 : 1;

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icc);
    HDC screen = GetDC(NULL);
    g_dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    g_font = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_small = CreateFontW(-S(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_big = CreateFontW(-S(24), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_head = CreateFontW(-S(26), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_white = CreateSolidBrush(RGB(255, 255, 255));

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_white;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"RecompHub";
    RegisterClassW(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, S(880), S(528)};
    AdjustWindowRect(&r, style, FALSE);
    HWND w = CreateWindowW(L"RecompHub", L"Recomp Hub " HUB_VERSION, style, CW_USEDEFAULT, CW_USEDEFAULT,
                           r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    ShowWindow(w, show);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (IsDialogMessageW(w, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return 0;
}
