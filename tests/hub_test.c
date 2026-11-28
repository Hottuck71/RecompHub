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
    for (int i = 0; i < NGAMES; i++) hits_ok &= hit_at(S(tile_x(i) + tile_w() / 2), S(TILE_Y + tile_h() / 2)) == i;
    CHECK(TILE_Y + tile_h() + 34 < ONLINE_Y, L"the line under each tile clears the Play online button");
    CHECK(hits_ok && hit_at(S(CLIENT_W / 2), S(ONLINE_Y + ONLINE_H / 2)) == HIT_ONLINE && hit_at(S(5), S(5)) == -1,
          L"clicking a tile opens that game; Play online; nothing in a corner");
    CHECK(sd_round_box(0, 0, 50, 30, 10) < 0 && sd_round_box(49.9f, 29.9f, 50, 30, 10) > 0 && fabsf(sd_round_box(50, 0, 50, 30, 10)) < .01f,
          L"rounded box edges");
    CHECK(sd_star5(0, 0, 10, .5f) < 0 && sd_star5(0, -9.9f, 10, .5f) < 0 && sd_star5(9, 9, 10, .5f) > 0, L"star: inside, its top point, outside");
    Canvas cv = {64, 48, calloc(64 * 48, 4)};
    draw_stripes(&cv);
    uint32_t mid_px = cv.px[24 * 64 + 32] & 255, top_px = cv.px[64 + 32] & 255;
    draw_tile(&cv, 8, 6, 48, 36, 8, 0, 0.f, -2.f, 1.f); /* game 0, Wii Sports Resort, with no picture: drawn sky */
    uint32_t sky = cv.px[14 * 64 + 32];
    CHECK(mid_px > top_px && (sky & 255) > ((sky >> 16) & 255), L"background brightest in the middle; the Resort tile is sky blue");
    draw_stripes(&cv);
    draw_tile(&cv, 8, 6, 48, 36, 8, 0, 0.f, -2.f, 0.f);
    uint32_t bare = cv.px[14 * 64 + 32];
    draw_stripes(&cv);
    CHECK(bare == cv.px[14 * 64 + 32], L"a tile that hasn't faded in yet draws nothing");
    draw_tile(&cv, 8, 6, 48, 36, 8, 0, 1.f, -2.f, 1.f);
    uint32_t plain = cv.px[24 * 64 + 32];
    draw_stripes(&cv);
    draw_tile(&cv, 8, 6, 48, 36, 8, 0, 1.f, .6f, 1.f); /* the sweep's band through the middle */
    uint32_t swept = cv.px[24 * 64 + 32];
    CHECK(((swept >> 16) & 255) > ((plain >> 16) & 255) + 30, L"the light sweep brightens the tile where it passes");
    free(cv.px);
    CHECK(ease_out_cubic(0) == 0.f && fabsf(ease_out_cubic(1) - 1.f) < 1e-6f && ease_out_back(.7f) > 1.f && fabsf(ease_out_back(1) - 1.f) < 1e-5f,
          L"easing: starts at 0, ends at 1, the drop-in overshoots a little");

    wprintf(L"tile pictures\n");
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    CHECK(picture_matches(0, L"resort.jpg") && picture_matches(0, L"Wii Sports Resort.png") && picture_matches(0, L"ResortRecomp.jpg") &&
          picture_matches(1, L"party.jpg") && picture_matches(1, L"Wii Party.jpeg") && picture_matches(2, L"marioparty9.jpg") &&
          picture_matches(2, L"Mario Party 9.png") && picture_matches(2, L"party9.jpg"),
          L"pictures match by game name, with or without Wii / Recomp");
    CHECK(!picture_matches(1, L"marioparty9.jpg") && !picture_matches(2, L"party.jpg") && !picture_matches(0, L"party.jpg") && !picture_matches(0, L".jpg"),
          L"and not another game's");
    ensure_art_folder();
    wchar_t ad[MAX_PATH], bmp[MAX_PATH];
    art_dir(ad);
    path_join(f2, ad, L"Put tile pictures here.txt");
    CHECK(dir_exists(ad) && file_exists(f2), L"the Tile art folder is made, with a note in it");
    /* an 8 x 6 picture: red on the left half, blue on the right, as a .bmp */
    static unsigned char bm[54 + 8 * 6 * 4];
    memset(bm, 0, sizeof bm);
    p = bm;
    memcpy(p, "BM", 2); p += 2; PUT4(sizeof bm); PUT4(0); PUT4(54);
    PUT4(40); PUT4(8); PUT4(6); PUT2(1); PUT2(32); PUT4(0); PUT4(8 * 6 * 4); PUT4(2835); PUT4(2835); PUT4(0); PUT4(0);
    for (int y = 0; y < 6; y++)
        for (int x = 0; x < 8; x++) { *p++ = x < 4 ? 0 : 255; *p++ = 0; *p++ = x < 4 ? 255 : 0; *p++ = 255; }
    path_join(bmp, ad, L"Wii Party.bmp");
    write_file(bmp, (const char*)bm, sizeof bm);
    BOOL changed = load_tile_pictures();
    CHECK(changed && g_pic[1].px && g_pic[1].w == 8 && g_pic[1].h == 6 && !g_pic[0].px && !g_pic[2].px, L"Wii Party.bmp read for Wii Party only");
    Col left = picture_at(&g_pic[1], .1f, .5f, 8.f / 6, 120.f), right = picture_at(&g_pic[1], .9f, .5f, 8.f / 6, 120.f);
    CHECK(left.r > 200 && left.b < 50 && right.b > 200 && right.r < 50, L"its colours come through (red left, blue right)");
    CHECK(!load_tile_pictures(), L"reading again with nothing new changes nothing");
    DeleteFileW(bmp);
    CHECK(load_tile_pictures() && !g_pic[1].px, L"taking the picture away goes back to the drawn tile");
    path_join(bmp, ad, L"Mario Party 9.png");
    write_file(bmp, "not a picture", 13);
    load_tile_pictures();
    CHECK(!g_pic[2].px, L"a broken picture is ignored");
    DeleteFileW(bmp);

    wprintf(L"box art\n");
    CHECK(!wcscmp(game_id(0), L"RZTE01") && !wcscmp(game_id(1), L"SUPE01") && !wcscmp(game_id(2), L"SSQE01"), L"each recomp's disc ID");
    wchar_t cov[MAX_PATH], cdir[MAX_PATH];
    cover_path(1, cov);
    CHECK(wcsstr(cov, L"\\Tile art\\Covers\\SUPE01.png") != NULL, L"covers go in Tile art\\Covers, named by disc ID");
    wcscpy(cdir, cov);
    *wcsrchr(cdir, L'\\') = 0;
    CreateDirectoryW(cdir, NULL);
    /* a tall 6 x 12 green "cover" */
    static unsigned char tb[54 + 6 * 12 * 4];
    memset(tb, 0, sizeof tb);
    p = tb;
    memcpy(p, "BM", 2); p += 2; PUT4(sizeof tb); PUT4(0); PUT4(54);
    PUT4(40); PUT4(6); PUT4(12); PUT2(1); PUT2(32); PUT4(0); PUT4(6 * 12 * 4); PUT4(2835); PUT4(2835); PUT4(0); PUT4(0);
    for (int n = 0; n < 6 * 12; n++) { *p++ = 40; *p++ = 220; *p++ = 40; *p++ = 255; }
    write_file(cov, (const char*)tb, sizeof tb);
    load_tile_pictures();
    CHECK(g_pic[1].px && g_pic[1].h == 12 && !g_pic_own[1], L"with no picture of yours, a game shows its cover");
    Col mid = picture_at(&g_pic[1], .5f, .5f, 4.f / 3, 180.f), side = picture_at(&g_pic[1], .03f, .5f, 4.f / 3, 180.f);
    CHECK(mid.g > 200 && side.g < mid.g - 40 && side.g > side.r, L"a tall cover is whole in the middle, a dimmed blur of it at the sides (green %d, %d)", (int)mid.g, (int)side.g);
    path_join(bmp, ad, L"party.jpg"); /* a .bmp named .jpg: Windows reads it by what it is */
    write_file(bmp, (const char*)bm, sizeof bm);
    load_tile_pictures();
    CHECK(g_pic[1].w == 8 && g_pic_own[1], L"your own picture wins over the cover");
    DeleteFileW(bmp);
    DeleteFileW(cov);
    load_tile_pictures();
    memset(g_cover_tried, 0, sizeof g_cover_tried);
    int got = fetch_covers_now();
    BOOL all_png = TRUE;
    for (int i = 0; i < NGAMES; i++) { cover_path(i, cov); all_png &= is_png(cov); }
    load_tile_pictures();
    CHECK(got == NGAMES && all_png && g_pic[0].px && g_pic[1].px && g_pic[2].px && g_pic[0].h > g_pic[0].w,
          L"GameTDB: %d covers downloaded and shown (%d x %d)", got, g_pic[0].w, g_pic[0].h);
    memset(g_cover_tried, 0, sizeof g_cover_tried);
    CHECK(fetch_covers_now() == 0, L"covers already there aren't downloaded again");

    remove_tree(tmp);
    wprintf(failures ? L"\n%d FAILED\n" : L"\nall passed\n", failures);
    return failures != 0;
}
