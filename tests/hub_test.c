/* Checks Recomp Hub's pieces without its window: settings files, GitHub's release listing, unpacking layouts,
   running programs, and finding DolphinTool. Run with build.ps1 -Test. Needs the internet for the GitHub check. */
#define wWinMain hub_wWinMain
#include "hub.c"
#undef wWinMain

static int failures;
#define CHECK(cond, ...) do { if (cond) { wprintf(L"  ok    "); } else { wprintf(L"  FAIL  "); failures++; } wprintf(__VA_ARGS__); wprintf(L"\n"); } while (0)

int wmain(void) {
    wchar_t tmp[MAX_PATH], v[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wcscat(tmp, L"recomphub-test");
    remove_tree(tmp);
    CreateDirectoryW(tmp, NULL);
    wcscpy(g_dir, tmp);
    path_join(g_ini, g_dir, L"RecompHub.ini");

    wprintf(L"settings\n");
    wchar_t ini[MAX_PATH];
    path_join(ini, tmp, L"launcher.ini");
    const char* seed = "dolphin_tool=C:\\x\\DolphinTool.exe\r\ngame=game\\DATA\r\ngfx_backend=3\r\n";
    write_file(ini, seed, (DWORD)strlen(seed));
    ini_set(ini, "relay", L"192.168.1.50");
    ini_set(ini, "dolphin_tool", L"D:\\Émulateurs\\DolphinTool.exe");
    ini_get(ini, "relay", v, MAX_PATH);
    CHECK(!wcscmp(v, L"192.168.1.50"), L"new key added: %s", v);
    ini_get(ini, "dolphin_tool", v, MAX_PATH);
    CHECK(!wcscmp(v, L"D:\\Émulateurs\\DolphinTool.exe"), L"key replaced, non-ASCII kept");
    ini_get(ini, "game", v, MAX_PATH);
    CHECK(!wcscmp(v, L"game\\DATA"), L"other keys untouched: %s", v);
    ini_get(ini, "gfx_backend", v, MAX_PATH);
    CHECK(!wcscmp(v, L"3"), L"last key untouched");
    CHECK(!ini_get(ini, "missing", v, MAX_PATH) && !v[0], L"missing key reads empty");
    char* raw = read_file(ini, NULL);
    CHECK(raw && !strstr(raw, "dolphin_tool=C:"), L"old value gone, one line per key");
    free(raw);
    write_file(ini, seed, (DWORD)strlen(seed));
    ini_set(ini, "game", L"game\\DATA"); /* the same value again: the file must not change at all */
    DWORD rl = 0;
    raw = read_file(ini, &rl);
    CHECK(raw && rl == strlen(seed) && !memcmp(raw, seed, rl), L"same value rewrites the file byte for byte (Windows line ends kept)");
    free(raw);

    wprintf(L"game folders\n");
    wchar_t d[MAX_PATH];
    game_dir(2, d);
    CHECK(wcsstr(d, L"\\Games\\Party9Recomp") != NULL, L"default folder: %s", d);
    CHECK(game_state(2) == NOT_INSTALLED, L"empty folder is not installed");
    path_join(v, g_dir, L"Games");
    CreateDirectoryW(v, NULL);
    CreateDirectoryW(d, NULL);
    wchar_t f[MAX_PATH];
    path_join(f, d, L"Launcher.exe");
    write_file(f, "x", 1);
    CHECK(game_state(2) == INSTALLED, L"Launcher.exe present = installed");
    path_join(f, d, L"Party9Recomp-Game.exe");
    write_file(f, "x", 1);
    CHECK(game_state(2) == BUILT, L"*-Game.exe present = built");
    CHECK(point_games_at(L"10.0.0.7") == 1, L"relay written to the one installed game");
    path_join(f, d, L"launcher.ini");
    ini_get(f, "relay", v, MAX_PATH);
    CHECK(!wcscmp(v, L"10.0.0.7"), L"game's launcher.ini has relay=%s", v);

    wprintf(L"unpacked release layouts\n");
    wchar_t u[MAX_PATH], sub[MAX_PATH], root[MAX_PATH];
    path_join(u, tmp, L"unpackA");
    CreateDirectoryW(u, NULL);
    path_join(sub, u, L"Party9Recomp");
    CreateDirectoryW(sub, NULL);
    path_join(f, sub, L"Launcher.exe");
    write_file(f, "x", 1);
    CHECK(release_root(u, root) && !wcscmp(root, sub), L"zip with a top folder");
    path_join(u, tmp, L"unpackB");
    CreateDirectoryW(u, NULL);
    path_join(f, u, L"Launcher.exe");
    write_file(f, "x", 1);
    CHECK(release_root(u, root) && !wcscmp(root, u), L"zip with files at the top");

    wprintf(L"GitHub releases\n");
    for (int i = 0; i < NGAMES; i++) {
        char* json = NULL;
        wchar_t api[256], url[2048], tag[64];
        _snwprintf(api, 256, L"https://api.github.com/repos/%s/releases/latest", GAMES[i].github);
        int st = http_get(api, L"application/vnd.github+json", NULL, &json, NULL);
        BOOL found = st == 200 && json && find_release_zip(json, url, tag);
        CHECK(found, L"%s %s -> %s", GAMES[i].repo, tag, found ? url : L"(none)");
        free(json);
    }
    char* body = NULL;
    int st = http_get(L"https://api.ipify.org", NULL, NULL, &body, NULL);
    CHECK(st == 200 && body && strchr(body, '.'), L"public address lookup answers");
    free(body);

    wprintf(L"programs and the network\n");
    char* out = NULL;
    int code = run_hidden(L"cmd.exe /c echo hello && exit 3", NULL, &out, 10000);
    CHECK(code == 3 && out && strstr(out, "hello"), L"run_hidden: exit code %d, output captured", code);
    free(out);
    code = run_hidden(L"cmd.exe /c ping -n 30 127.0.0.1 >nul", NULL, NULL, 1000);
    CHECK(code == -2, L"run_hidden stops a program past its time limit");
    code = run_powershell(L"Write-Output ('a'+'b'+\"c\")", &out, 30000);
    CHECK(code == 0 && out && strstr(out, "abc"), L"PowerShell via -EncodedCommand (quotes survive)");
    free(out);
    wchar_t ip[64] = L"";
    CHECK(local_ip(ip, 64), L"this PC's network address: %s", ip);
    CHECK(count_processes(L"explorer.exe", FALSE) > 0, L"process list works");

    wprintf(L"DolphinTool\n");
    wchar_t tool[MAX_PATH] = L"";
    BOOL t = find_dolphin_tool(tool);
    wprintf(L"  info  %s\n", t ? tool : L"not found on this PC (Browse would be needed)");

    remove_tree(tmp);
    wprintf(failures ? L"\n%d FAILED\n" : L"\nall passed\n", failures);
    return failures != 0;
}
