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
    ini_set(ini, "game", NULL);
    raw = read_file(ini, NULL);
    CHECK(raw && !strcmp(raw, "dolphin_tool=C:\\x\\DolphinTool.exe\r\ngfx_backend=3\r\n"), L"NULL removes the key and only that line");
    free(raw);
    wchar_t none[MAX_PATH];
    path_join(none, tmp, L"no-such.ini");
    CHECK(ini_set(none, "relay", NULL) && !file_exists(none), L"removing from a missing file creates nothing");

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
    CHECK(hub_made_folder(2), L"the hub's own Games folder counts as installed by the hub");
    hub_set("dir_Party9Recomp", L"D:\\My own MP9");
    CHECK(!hub_made_folder(2), L"a folder the player chose doesn't (Uninstall then deletes nothing)");
    hub_set("dir_Party9Recomp", NULL);
    game_dir(2, v);
    CHECK(!wcscmp(v, d), L"forgetting the folder goes back to the default");

    wprintf(L"updates\n");
    CHECK(version_newer(L"0.2.0", L"0.1.0") && version_newer(L"v1.10.0", L"1.9.9") && version_newer(L"1.1", L"1.0.5"),
          L"newer versions compare as numbers, with or without v");
    CHECK(!version_newer(L"0.1.0", L"0.1.0") && !version_newer(L"1.0", L"1.0.0") && !version_newer(L"0.9.9", L"1.0.0"),
          L"the same or older isn't newer");
    wchar_t bd[MAX_PATH];
    path_join(bd, d, L"builder");
    CreateDirectoryW(bd, NULL);
    path_join(f, bd, L"version.txt");
    write_file(f, "\xEF\xBB\xBF" "0.1.0\r\n", 10);
    installed_version(2, v, 64);
    CHECK(!wcscmp(v, L"0.1.0"), L"installed version from builder\\version.txt: %s", v);
    path_join(f, d, L"game-build.txt");
    write_file(f, "0.1.0\nabc\nplain\n", 16);
    CHECK(!build_outdated(2), L"a build of the installed version isn't behind");
    wcscpy(g_latest[2], L"0.2.0");
    CHECK(update_available(2), L"0.2.0 on GitHub is an update for 0.1.0");
    path_join(f, bd, L"version.txt");
    write_file(f, "0.2.0", 5);
    CHECK(!update_available(2), L"after updating, no update is left");
    CHECK(build_outdated(2), L"the 0.1.0 build is behind the 0.2.0 files, so it's built again");
    BuildArgs ba;
    CHECK(!build_source(2, &ba), L"no disc to build from yet");
    wchar_t gd[MAX_PATH];
    _snwprintf(gd, MAX_PATH, L"%s\\game", d);
    CreateDirectoryW(gd, NULL);
    wcscat(gd, L"\\DATA");
    CreateDirectoryW(gd, NULL);
    wcscat(gd, L"\\sys");
    CreateDirectoryW(gd, NULL);
    path_join(f, gd, L"main.dol");
    write_file(f, "x", 1);
    CHECK(build_source(2, &ba) && wcsstr(ba.rom, L"\\game") && !ba.tool[0], L"the unpacked disc is built from again, no DolphinTool needed");
    g_latest[2][0] = 0;

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
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    CHECK(running_from(win), L"running_from sees programs in %s", win);
    CHECK(!running_from(tmp), L"running_from: nothing runs from an empty folder");

    wprintf(L"DolphinTool\n");
    wchar_t tool[MAX_PATH] = L"";
    BOOL t = find_dolphin_tool(tool);
    wprintf(L"  info  %s\n", t ? tool : L"not found on this PC (Browse would be needed)");

    wprintf(L"menu music\n");
    wchar_t md[MAX_PATH], f2[MAX_PATH];
    ensure_music_folder();
    music_dir(md);
    path_join(f2, md, L"Put menu music here.txt");
    CHECK(dir_exists(md) && file_exists(f2), L"the Music folder is made, with a note in it");
    /* half a second of a quiet tone, as a .wav: something to play */
    enum { RATE = 22050, SAMPLES = RATE / 2 };
    static unsigned char wav[44 + SAMPLES * 2];
    unsigned char* p = wav;
    #define PUT4(v) do { DWORD x_ = (v); memcpy(p, &x_, 4); p += 4; } while (0)
    #define PUT2(v) do { WORD x_ = (v); memcpy(p, &x_, 2); p += 2; } while (0)
    memcpy(p, "RIFF", 4); p += 4; PUT4(36 + SAMPLES * 2); memcpy(p, "WAVEfmt ", 8); p += 8;
    PUT4(16); PUT2(1); PUT2(1); PUT4(RATE); PUT4(RATE * 2); PUT2(2); PUT2(16);
    memcpy(p, "data", 4); p += 4; PUT4(SAMPLES * 2);
    for (int n = 0; n < SAMPLES; n++) PUT2((WORD)(short)(200 * sin(n * 0.06)));
    path_join(f2, md, L"b song.wav");
    write_file(f2, (const char*)wav, sizeof wav);
    path_join(f2, md, L"a song.mp3"); /* not a real mp3: it must be skipped, not stop the music */
    write_file(f2, "not music", 9);
    path_join(f2, md, L"cover.jpg");
    write_file(f2, "x", 1);
    wchar_t songs[8][MAX_PATH];
    int ns = list_music(md, songs, 8);
    CHECK(ns == 2 && wcsstr(songs[0], L"a song.mp3") && wcsstr(songs[1], L"b song.wav"), L"songs listed by name, other files left out (%d)", ns);
    g_wnd = CreateWindowW(L"STATIC", L"", 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL); /* for "the song ended" messages */
    music_next();
    wchar_t mode[32] = L"";
    mciSendStringW(L"status " MUSIC_ALIAS L" mode", mode, 32, NULL);
    CHECK(g_music_open && g_song == 1 && !wcscmp(mode, L"playing"), L"a broken song is skipped and the next one plays (%s)", mode);
    music_pause();
    mciSendStringW(L"status " MUSIC_ALIAS L" mode", mode, 32, NULL);
    CHECK(!wcscmp(mode, L"paused"), L"pause (%s)", mode);
    music_resume();
    mciSendStringW(L"status " MUSIC_ALIAS L" mode", mode, 32, NULL);
    CHECK(!wcscmp(mode, L"playing"), L"and play on (%s)", mode);
    hub_set("music", L"0");
    CHECK(!music_on(), L"music off is remembered");
    hub_set("music", NULL);
    music_close();
    DestroyWindow(g_wnd);
    g_wnd = NULL;

    wprintf(L"home screen\n");
    int last = tile_x(NGAMES - 1) + tile_w();
    CHECK(tile_x(0) >= 20 && last <= CLIENT_W - 20, L"%d tiles fit across the window (%d..%d)", NGAMES, tile_x(0), last);
    g_page = PAGE_HOME;
    layout_hits();
    BOOL hits_ok = TRUE;
    for (int i = 0; i < NGAMES; i++) hits_ok &= hit_at(S(tile_x(i) + tile_w() / 2), S(TILE_Y + TILE_H / 2)) == i;
    CHECK(hits_ok && hit_at(S(CLIENT_W / 2), S(ONLINE_Y + ONLINE_H / 2)) == HIT_ONLINE && hit_at(S(5), S(5)) == -1,
          L"clicking a tile opens that game; Play online; nothing in a corner");
    CHECK(sd_round_box(0, 0, 50, 30, 10) < 0 && sd_round_box(49.9f, 29.9f, 50, 30, 10) > 0 && fabsf(sd_round_box(50, 0, 50, 30, 10)) < .01f,
          L"rounded box edges");
    CHECK(sd_star5(0, 0, 10, .5f) < 0 && sd_star5(0, -9.9f, 10, .5f) < 0 && sd_star5(9, 9, 10, .5f) > 0, L"star: inside, its top point, outside");
    Canvas cv = {64, 48, calloc(64 * 48, 4)};
    draw_stripes(&cv);
    uint32_t mid_px = cv.px[24 * 64 + 32] & 255, top_px = cv.px[64 + 32] & 255;
    draw_tile(&cv, 8, 6, 48, 36, 8, ART_RESORT, FALSE);
    uint32_t sky = cv.px[14 * 64 + 32];
    CHECK(mid_px > top_px && (sky & 255) > ((sky >> 16) & 255), L"background brightest in the middle; the Resort tile is sky blue");
    free(cv.px);

    remove_tree(tmp);
    wprintf(failures ? L"\n%d FAILED\n" : L"\nall passed\n", failures);
    return failures != 0;
}
