/* Recomp Hub's look and sound: the striped home screen with a glossy tile per game, the Back button and the white
   card the game pages sit on, and menu music from the Music folder. Included by hub.c (it uses its globals).

   Shapes are drawn pixel by pixel into a 32-bit bitmap from distance functions, which gives smooth edges without
   GDI+; text goes on top with ordinary GDI. All sizes are in 96-DPI units, scaled with S(). */

/* ================= drawing ================= */
typedef struct { int w, h; uint32_t* px; } Canvas; /* 0x00RRGGBB, top row first */
typedef struct { float r, g, b; } Col;

static Col rgb(float r, float g, float b) { Col c = {r, g, b}; return c; }
static const Col WHITE = {255, 255, 255}, BLACK = {0, 0, 0};
static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float smooth(float e0, float e1, float x) { float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f); return t * t * (3.f - 2.f * t); }
static Col mixc(Col a, Col b, float t) { t = clampf(t, 0.f, 1.f); return rgb(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t); }
static float fs(float x) { return (float)S(1) * x; } /* a scaled size, keeping fractions */

/* Blends colour k over the pixel at (x, y) by a (0..1). */
static void put(Canvas* c, int x, int y, Col k, float a) {
    if (a <= 0.f || x < 0 || y < 0 || x >= c->w || y >= c->h) return;
    if (a > 1.f) a = 1.f;
    uint32_t* p = &c->px[y * c->w + x];
    float r = (float)((*p >> 16) & 255), g = (float)((*p >> 8) & 255), b = (float)(*p & 255);
    r += (clampf(k.r, 0, 255) - r) * a;
    g += (clampf(k.g, 0, 255) - g) * a;
    b += (clampf(k.b, 0, 255) - b) * a;
    *p = ((uint32_t)(r + .5f) << 16) | ((uint32_t)(g + .5f) << 8) | (uint32_t)(b + .5f);
}
/* Signed distance from a point (relative to the centre) to a rounded box: negative inside. */
static float sd_round_box(float px, float py, float hw, float hh, float r) {
    float qx = fabsf(px) - hw + r, qy = fabsf(py) - hh + r;
    float ox = fmaxf(qx, 0.f), oy = fmaxf(qy, 0.f);
    return sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.f) - r;
}
/* A five-pointed star of outer radius r, point up (after Inigo Quilez's sdStar5). */
static float sd_star5(float px, float py, float r, float rf) {
    const float k1x = 0.809016994f, k1y = -0.587785252f, k2x = -k1x, k2y = k1y;
    py = -py; /* screen y runs down */
    px = fabsf(px);
    float d = fmaxf(k1x * px + k1y * py, 0.f);
    px -= 2.f * d * k1x; py -= 2.f * d * k1y;
    d = fmaxf(k2x * px + k2y * py, 0.f);
    px -= 2.f * d * k2x; py -= 2.f * d * k2y;
    px = fabsf(px);
    py -= r;
    float bax = rf * -k1y, bay = rf * k1x - 1.f;
    float h = clampf((px * bax + py * bay) / (bax * bax + bay * bay), 0.f, r);
    float dx = px - bax * h, dy = py - bay * h;
    float s = py * bax - px * bay;
    return sqrtf(dx * dx + dy * dy) * (s < 0 ? -1.f : 1.f);
}
/* Distance to the segment a-b, minus a radius: a rounded line. */
static float sd_capsule(float px, float py, float ax, float ay, float bx, float by, float r) {
    float pax = px - ax, pay = py - ay, bax = bx - ax, bay = by - ay;
    float h = clampf((pax * bax + pay * bay) / (bax * bax + bay * bay), 0.f, 1.f);
    float dx = pax - bax * h, dy = pay - bay * h;
    return sqrtf(dx * dx + dy * dy) - r;
}
static void fill_capsule(Canvas* c, float ax, float ay, float bx, float by, float r, Col k) {
    int x0 = (int)fminf(ax, bx) - (int)r - 2, x1 = (int)fmaxf(ax, bx) + (int)r + 2;
    int y0 = (int)fminf(ay, by) - (int)r - 2, y1 = (int)fmaxf(ay, by) + (int)r + 2;
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) put(c, x, y, k, clampf(.5f - sd_capsule(x + .5f, y + .5f, ax, ay, bx, by, r), 0, 1));
}
/* A filled triangle, smoothed by sampling each pixel 4x4. */
static void fill_triangle(Canvas* c, float ax, float ay, float bx, float by, float cx, float cy, Col k) {
    int x0 = (int)fminf(ax, fminf(bx, cx)) - 1, x1 = (int)fmaxf(ax, fmaxf(bx, cx)) + 1;
    int y0 = (int)fminf(ay, fminf(by, cy)) - 1, y1 = (int)fmaxf(ay, fmaxf(by, cy)) + 1;
    float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            int in = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float px = x + (sx + .5f) / 4.f, py = y + (sy + .5f) / 4.f;
                    float w0 = (bx - px) * (cy - py) - (by - py) * (cx - px);
                    float w1 = (cx - px) * (ay - py) - (cy - py) * (ax - px);
                    float w2 = (ax - px) * (by - py) - (ay - py) * (bx - px);
                    if (area > 0 ? (w0 >= 0 && w1 >= 0 && w2 >= 0) : (w0 <= 0 && w1 <= 0 && w2 <= 0)) in++;
                }
            put(c, x, y, k, in / 16.f);
        }
}

/* The background: light grey with fine horizontal lines, whitest across the middle. */
static void draw_stripes(Canvas* c) {
    int period = S(4) < 3 ? 3 : S(4);
    for (int y = 0; y < c->h; y++) {
        float v = (y + .5f) / c->h, mid = 1.f - 4.f * (v - .5f) * (v - .5f);
        float row = 214.f + 36.f * mid - ((y % period) == 0 ? 7.f : 0.f);
        for (int x = 0; x < c->w; x++) {
            float u = (x + .5f) / c->w, glow = 9.f * expf(-((u - .5f) / .38f) * ((u - .5f) / .38f)) * mid;
            float l = fminf(row + glow, 255.f);
            uint32_t g = (uint32_t)l, b = (uint32_t)fminf(l + 2.f, 255.f);
            c->px[y * c->w + x] = (g << 16) | (g << 8) | b;
        }
    }
}

/* ---- tile pictures: drawn from shapes, one look per game ---- */
enum { ART_RESORT, ART_PARTY, ART_MP9, ART_ONLINE, ART_OTHER };
static int game_art(int i) {
    if (!wcscmp(GAMES[i].repo, L"ResortRecomp")) return ART_RESORT;
    if (!wcscmp(GAMES[i].repo, L"PartyRecomp")) return ART_PARTY;
    if (!wcscmp(GAMES[i].repo, L"Party9Recomp")) return ART_MP9;
    return ART_OTHER;
}
static COLORREF art_shadow(int art) { /* the colour under a tile's white title */
    static const COLORREF k[] = {RGB(16, 48, 132), RGB(196, 78, 0), RGB(112, 0, 14), RGB(0, 92, 62), RGB(30, 34, 96)};
    return k[art];
}
static uint32_t hash32(uint32_t x) { x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; return x; }
static float rnd(uint32_t seed) { return (hash32(seed) & 0xFFFFFF) / 16777216.f; }

/* The colour at (u, v) in 0..1 across the picture; aspect = width / height, pix = one pixel in v units. */
static Col art_at(int art, float u, float v, float aspect, float pix) {
    Col k;
    float ux = u * aspect;
    switch (art) {
    case ART_RESORT: { /* sky, clouds low down, and the sun's flare top left */
        Col top = rgb(22, 62, 176), mid = rgb(64, 136, 226), bot = rgb(226, 240, 253);
        k = v < .55f ? mixc(top, mid, v / .55f) : mixc(mid, bot, (v - .55f) / .45f);
        static const float cl[][4] = {{.12f, .80f, .30f, .10f}, {.46f, .86f, .36f, .11f}, {.84f, .78f, .30f, .10f},
                                      {.30f, .69f, .20f, .07f}, {.68f, .66f, .24f, .07f}, {.55f, 1.0f, .55f, .12f},
                                      {.02f, .98f, .30f, .10f}, {.98f, .95f, .30f, .10f}, {.62f, .40f, .26f, .03f},
                                      {.22f, .30f, .18f, .025f}};
        float cloud = 0;
        for (size_t n = 0; n < sizeof cl / sizeof *cl; n++) {
            float dx = (u - cl[n][0]) / cl[n][2], dy = (v - cl[n][1]) / cl[n][3];
            cloud = fmaxf(cloud, 1.f - smooth(.55f, 1.f, sqrtf(dx * dx + dy * dy)));
        }
        k = mixc(k, WHITE, cloud * .93f);
        float sx = (u - .1f) * aspect, sy = v - .07f, d2 = sx * sx + sy * sy;
        float sun = expf(-d2 * 70.f) * .95f + expf(-d2 * 7.f) * .22f;
        float ca = .906f, sa = .423f; /* a streak at 25 degrees */
        float along = ca * sx + sa * sy, perp = -sa * sx + ca * sy;
        float streak = along > 0 ? expf(-perp * perp * 2500.f) * expf(-along * 2.4f) * .75f : 0.f;
        k = mixc(k, WHITE, sun + streak);
        break;
    }
    case ART_PARTY: { /* warm orange with confetti */
        k = mixc(rgb(255, 204, 46), rgb(255, 118, 22), v);
        float gx = (u - .5f) * aspect, gy = v - .5f;
        k = mixc(k, rgb(255, 242, 176), .38f * expf(-(gx * gx + gy * gy) * 5.f));
        static const Col conf[] = {{255, 255, 255}, {238, 62, 128}, {38, 178, 245}, {96, 190, 98}, {168, 72, 190}};
        for (uint32_t n = 0; n < 30; n++) {
            float cx = rnd(n * 4 + 1), cy = rnd(n * 4 + 2), r = .016f + .024f * rnd(n * 4 + 3);
            if (fabsf(cy - .5f) < .22f && fabsf(cx - .5f) < .47f) continue; /* keep the title clear */
            float dx = (u - cx) * aspect, dy = v - cy;
            float d = sqrtf(dx * dx + dy * dy) - r;
            k = mixc(k, conf[hash32(n * 4 + 4) % 5], clampf(.5f - d / pix, 0, 1) * .9f);
        }
        break;
    }
    case ART_MP9: { /* red with a soft sunburst and gold stars */
        k = mixc(rgb(234, 46, 54), rgb(146, 12, 28), v);
        float ax = (u - .5f) * aspect, ay = v - .52f;
        float ang = atan2f(ay, ax);
        k = mixc(k, WHITE, (sinf(ang * 12.f) > 0 ? .07f : 0.f) * (1.f - smooth(.1f, 1.1f, sqrtf(ax * ax + ay * ay))));
        static const float st[][3] = {{.17f, .17f, .085f}, {.84f, .13f, .06f}, {.86f, .85f, .10f}, {.14f, .84f, .065f}, {.52f, .07f, .04f}};
        for (size_t n = 0; n < sizeof st / sizeof *st; n++) {
            float d = sd_star5((u - st[n][0]) * aspect, v - st[n][1], st[n][2], .48f);
            k = mixc(k, rgb(150, 70, 0), clampf(.5f - (d - pix * 1.5f) / pix, 0, 1)); /* a darker rim */
            k = mixc(k, mixc(rgb(255, 236, 120), rgb(255, 190, 30), (v - st[n][1]) / st[n][2] * .5f + .5f), clampf(.5f - d / pix, 0, 1));
        }
        break;
    }
    case ART_ONLINE: { /* green with rings spreading out, like a signal */
        k = mixc(rgb(48, 206, 132), rgb(10, 120, 94), v);
        float dx = (u - .5f) * aspect, dy = v - 1.15f, d = sqrtf(dx * dx + dy * dy);
        float f = fmodf(d * 6.f, 1.f);
        k = mixc(k, WHITE, .17f * (1.f - smooth(.07f, .1f, fabsf(f - .5f))));
        break;
    }
    default: { /* any other recomp: deep blue with sparkles */
        k = mixc(rgb(96, 116, 214), rgb(36, 44, 120), v);
        for (uint32_t n = 0; n < 14; n++) {
            float cx = rnd(n * 3 + 7), cy = rnd(n * 3 + 8), r = .01f + .02f * rnd(n * 3 + 9);
            float dx = (u - cx) * aspect, dy = v - cy;
            k = mixc(k, WHITE, clampf(.5f - (sqrtf(dx * dx + dy * dy) - r) / pix, 0, 1) * .6f);
        }
    }
    }
    (void)ux;
    return k;
}

/* A tile: soft shadow, dark rim (blue when selected, with a glow), a silver bevel, the picture, and gloss on top. */
static void draw_tile(Canvas* c, float x, float y, float w, float h, float radius, int art, BOOL lit) {
    float rim = fs(3.f), bevel = fs(4.f), pad = fs(16.f);
    float cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    float ix = x + rim + bevel, iy = y + rim + bevel, iw = w - 2 * (rim + bevel), ih = h - 2 * (rim + bevel);
    Col rim_col = lit ? rgb(28, 160, 232) : rgb(74, 74, 74);
    for (int py = (int)(y - pad); py < (int)(y + h + pad); py++)
        for (int px = (int)(x - pad); px < (int)(x + w + pad); px++) {
            float fx = px + .5f - cx, fy = py + .5f - cy;
            float ds = sd_round_box(fx, fy - fs(5.f), hw, hh, radius);
            put(c, px, py, BLACK, .26f * (1.f - smooth(-fs(2.f), fs(12.f), ds)));
            float d = sd_round_box(fx, fy, hw, hh, radius);
            if (lit && d > 0) put(c, px, py, rgb(40, 180, 245), .75f * (1.f - smooth(0.f, fs(11.f), d)));
            float cov = clampf(.5f - d, 0, 1);
            if (cov <= 0) continue;
            float in = -d; /* how far inside the edge */
            float u = (px + .5f - ix) / iw, v = (py + .5f - iy) / ih;
            Col pic = art_at(art, u, v, iw / ih, 1.f / ih);
            /* gloss: a bright arc over the top part */
            float gx = (u - .5f) / .98f, gy = (v + .62f) / 1.08f;
            pic = mixc(pic, WHITE, .2f * (1.f - smooth(.96f, 1.0f, sqrtf(gx * gx + gy * gy))));
            pic = mixc(pic, BLACK, .16f * (1.f - smooth(0.f, fs(6.f), in - rim - bevel))); /* inner shade */
            Col silver = mixc(rgb(252, 252, 252), rgb(192, 192, 196), (in - rim) / bevel);
            Col k = mixc(rim_col, silver, clampf(in - rim + .5f, 0, 1));
            k = mixc(k, pic, clampf(in - rim - bevel + .5f, 0, 1));
            put(c, px, py, k, cov);
        }
}

/* A plain rounded shape: optional shadow, top-to-bottom gradient, border. */
static void draw_shape(Canvas* c, float x, float y, float w, float h, float radius, Col top, Col bot, Col border, float bw, float shadow) {
    float pad = fs(10.f), cx = x + w / 2, cy = y + h / 2;
    for (int py = (int)(y - pad); py < (int)(y + h + pad); py++)
        for (int px = (int)(x - pad); px < (int)(x + w + pad); px++) {
            float fx = px + .5f - cx, fy = py + .5f - cy;
            if (shadow > 0) put(c, px, py, BLACK, shadow * (1.f - smooth(-fs(1.f), fs(8.f), sd_round_box(fx, fy - fs(3.f), w / 2, h / 2, radius))));
            float d = sd_round_box(fx, fy, w / 2, h / 2, radius), cov = clampf(.5f - d, 0, 1);
            if (cov <= 0) continue;
            Col k = mixc(top, bot, (py + .5f - y) / h);
            if (bw > 0) k = mixc(border, k, clampf(-d - bw + .5f, 0, 1));
            put(c, px, py, k, cov);
        }
}

/* ---- text on top ---- */
static HFONT g_font_heading, g_font_pill, g_font_sym, g_font_link;
static void text_in(HDC dc, const wchar_t* s, int x, int y, int w, int h, HFONT f, COLORREF k, UINT fmt) {
    SelectObject(dc, f);
    SetTextColor(dc, k);
    RECT r = {x, y, x + w, y + h};
    DrawTextW(dc, s, -1, &r, fmt | DT_NOPREFIX);
}
/* A tile's title: big and white, on one line or split over two, shrunk until it fits. */
static void tile_title(HDC dc, const wchar_t* title, int x, int y, int w, int h, int max_px, COLORREF shadow) {
    wchar_t lines[2][96] = {{0}};
    int nl = 1, size = max_px;
    HFONT f = NULL;
    for (; size >= 14; size -= 2) {
        if (f) DeleteObject(f);
        f = CreateFontW(-size, 0, 0, 0, FW_BLACK, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI Black");
        SelectObject(dc, f);
        SIZE sz;
        GetTextExtentPoint32W(dc, title, (int)wcslen(title), &sz);
        if (sz.cx <= w) { nl = 1; wcsncpy(lines[0], title, 95); break; }
        /* the split with the shortest longer line */
        int best = -1, best_w = 1 << 30;
        for (int i = 0; title[i]; i++) {
            if (title[i] != L' ') continue;
            SIZE a, b;
            GetTextExtentPoint32W(dc, title, i, &a);
            GetTextExtentPoint32W(dc, title + i + 1, (int)wcslen(title + i + 1), &b);
            int m = a.cx > b.cx ? a.cx : b.cx;
            if (m < best_w) { best_w = m; best = i; }
        }
        if (best >= 0 && best_w <= w) {
            nl = 2;
            wcsncpy(lines[0], title, best < 95 ? best : 95);
            lines[0][best < 95 ? best : 95] = 0;
            wcsncpy(lines[1], title + best + 1, 95);
            break;
        }
    }
    if (size < 14) { nl = 1; wcsncpy(lines[0], title, 95); }
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    int lh = tm.tmHeight - tm.tmInternalLeading / 2, top = y + (h - lh * nl) / 2, off = S(2);
    for (int i = 0; i < nl; i++) {
        text_in(dc, lines[i], x + off, top + i * lh + off, w, lh + S(4), f, shadow, DT_CENTER | DT_SINGLELINE);
        text_in(dc, lines[i], x, top + i * lh, w, lh + S(4), f, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE);
    }
    DeleteObject(f);
}

/* ================= menu music ================= */
#define MUSIC_ALIAS L"recomphub_bgm"
static wchar_t g_songs[64][MAX_PATH];
static int g_nsongs, g_song = -1;
static BOOL g_music_open, g_music_paused, g_music_away;

static void music_dir(wchar_t* out) { path_join(out, g_dir, L"Music"); }
static BOOL music_file(const wchar_t* name) {
    const wchar_t* e = wcsrchr(name, L'.');
    return e && (!_wcsicmp(e, L".mp3") || !_wcsicmp(e, L".wav") || !_wcsicmp(e, L".wma"));
}
static int cmp_path(const void* a, const void* b) { return _wcsicmp((const wchar_t*)a, (const wchar_t*)b); }
/* The songs in a folder (.mp3, .wav, .wma), sorted by name. */
static int list_music(const wchar_t* dir, wchar_t (*out)[MAX_PATH], int cap) {
    wchar_t pat[MAX_PATH];
    path_join(pat, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    int n = 0;
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && music_file(fd.cFileName) && n < cap) path_join(out[n++], dir, fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    qsort(out, n, sizeof *out, cmp_path);
    return n;
}
/* The Music folder next to the hub, with a note saying what it's for. */
static void ensure_music_folder(void) {
    wchar_t dir[MAX_PATH], note[MAX_PATH];
    music_dir(dir);
    CreateDirectoryW(dir, NULL);
    path_join(note, dir, L"Put menu music here.txt");
    if (!file_exists(note)) {
        const char* t = "Put songs in this folder (.mp3, .wav or .wma) and Recomp Hub plays them as menu music.\r\n"
                        "With more than one, they play in turn, by name. The music button on the home screen turns it on and off.\r\n";
        write_file(note, t, (DWORD)strlen(t));
    }
}
static BOOL music_on(void) {
    wchar_t v[8];
    hub_get("music", v, 8);
    return wcscmp(v, L"0") != 0;
}
static void music_close(void) {
    if (g_music_open) mciSendStringW(L"close " MUSIC_ALIAS, NULL, 0, NULL);
    g_music_open = g_music_paused = FALSE;
}
static BOOL music_open_song(int i) {
    music_close();
    wchar_t cmd[MAX_PATH + 64];
    _snwprintf(cmd, MAX_PATH + 64, L"open \"%s\" type mpegvideo alias " MUSIC_ALIAS, g_songs[i]);
    if (mciSendStringW(cmd, NULL, 0, NULL)) return FALSE;
    g_music_open = TRUE;
    g_song = i;
    mciSendStringW(L"setaudio " MUSIC_ALIAS L" volume to 450", NULL, 0, NULL);
    return mciSendStringW(L"play " MUSIC_ALIAS L" notify", NULL, 0, g_wnd) == 0;
}
/* Starts the next song (the first, the first time); with one song, that's the same one again. */
static void music_next(void) {
    music_close();
    wchar_t dir[MAX_PATH];
    music_dir(dir);
    g_nsongs = list_music(dir, g_songs, 64);
    for (int tries = 0; tries < g_nsongs; tries++)
        if (music_open_song((g_song + 1 + tries) % g_nsongs)) return;
    music_close();
}
static void music_pause(void) {
    if (g_music_open && !g_music_paused) mciSendStringW(L"pause " MUSIC_ALIAS, NULL, 0, NULL);
    g_music_paused = g_music_open;
}
/* Carries on where it paused (play, not resume, so it still says when the song ends), or starts a song. */
static void music_resume(void) {
    if (!music_on()) return;
    if (g_music_open && g_music_paused) {
        mciSendStringW(L"play " MUSIC_ALIAS L" notify", NULL, 0, g_wnd);
        g_music_paused = FALSE;
    } else if (!g_music_open) {
        music_next();
    }
}

/* ================= the screens ================= */
/* g_page: a game (0..NGAMES-1), PAGE_ONLINE, or PAGE_HOME, the "Which game would you like to play?" screen. */
enum { HIT_ONLINE = NGAMES, HIT_BACK = 50, HIT_MUSIC, HIT_MUSICDIR, HIT_AUTOUPDATE };
typedef struct { int x, y, w, h, id; } Hit; /* in pixels */
static Hit g_hits[16];
static int g_nhits, g_hover = -1, g_sel = -1, g_pressed = -1, g_last_tile = 0;
static BOOL g_tracking;
static wchar_t g_note[NGAMES + 1][160]; /* the line under each tile, and the online button's */
static BOOL g_note_hot[NGAMES + 1];     /* an update is waiting: shown in blue */
static HBITMAP g_cache;
static void* g_cache_bits;
static int g_cache_w, g_cache_h;
static BOOL g_dirty = TRUE;
static void refresh_page(void);

static void redraw(void) {
    g_dirty = TRUE;
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
}

/* Layout, in 96-DPI units, of the 880 x 528 window. */
#define CLIENT_W 880
#define TILE_H 262
#define TILE_Y 88
#define TILE_GAP 30
#define ONLINE_W 320
#define ONLINE_H 62
#define ONLINE_Y 398
#define CARD_X 272
#define CARD_W 600
static int tile_w(void) {
    int w = (CLIENT_W - 80 - (NGAMES - 1) * TILE_GAP) / NGAMES;
    return w > 236 ? 236 : w;
}
static int tile_x(int i) {
    int tw = tile_w(), total = NGAMES * tw + (NGAMES - 1) * TILE_GAP;
    return (CLIENT_W - total) / 2 + i * (tw + TILE_GAP);
}
static void add_hit(int x, int y, int w, int h, int id) {
    if (g_nhits < 16) g_hits[g_nhits++] = (Hit){S(x), S(y), S(w), S(h), id};
}
static void layout_hits(void) {
    g_nhits = 0;
    if (g_page == PAGE_HOME) {
        for (int i = 0; i < NGAMES; i++) add_hit(tile_x(i), TILE_Y, tile_w(), TILE_H + 36, i);
        add_hit((CLIENT_W - ONLINE_W) / 2, ONLINE_Y, ONLINE_W, ONLINE_H, HIT_ONLINE);
        add_hit(22, 484, 250, 26, HIT_AUTOUPDATE);
        add_hit(816, 474, 40, 40, HIT_MUSIC);
        add_hit(676, 496, 132, 20, HIT_MUSICDIR);
    } else {
        add_hit(24, 18, 120, 44, HIT_BACK);
    }
}
static int hit_at(int x, int y) {
    for (int i = 0; i < g_nhits; i++)
        if (x >= g_hits[i].x && y >= g_hits[i].y && x < g_hits[i].x + g_hits[i].w && y < g_hits[i].y + g_hits[i].h) return g_hits[i].id;
    return -1;
}

/* The boxed sections on a page's card (96-DPI units; x relative to the card's controls). */
typedef struct { int page; int y, h; const wchar_t *num, *label; } Section;
static const Section SECTIONS[] = {
    {0, 84, 112, L"1", L"Get the game files"},
    {0, 206, 210, L"2", L"Build it from your disc"},
    {0, 426, 86, L"3", L"Play"},
    {1, 96, 268, NULL, L"Host a server on this PC"},
    {1, 374, 118, NULL, L"Join a friend's server"},
};
static HFONT g_font_section;

static void render(HDC dc, Canvas* c) {
    const COLORREF BLUE = RGB(0, 103, 192);
    draw_stripes(c);
    SetBkMode(dc, TRANSPARENT);
    if (g_page == PAGE_HOME) {
        int tw = tile_w(), ox = (CLIENT_W - ONLINE_W) / 2;
        BOOL au = auto_update_on(), mon = music_on();
        for (int i = 0; i < NGAMES; i++)
            draw_tile(c, (float)S(tile_x(i)), (float)S(TILE_Y), (float)S(tw), (float)S(TILE_H), fs(26.f), game_art(i), g_sel == i);
        draw_tile(c, (float)S(ox), (float)S(ONLINE_Y), (float)S(ONLINE_W), (float)S(ONLINE_H), fs(ONLINE_H / 2.f), ART_ONLINE, g_sel == HIT_ONLINE);
        /* "Keep games up to date": a rounded check box */
        draw_shape(c, fs(24), fs(487), fs(20), fs(20), fs(5), au ? rgb(30, 144, 230) : WHITE, au ? rgb(0, 104, 196) : rgb(236, 236, 236),
                   g_hover == HIT_AUTOUPDATE ? rgb(28, 160, 232) : rgb(130, 130, 130), fs(1.5f), 0);
        if (au) {
            fill_capsule(c, fs(29), fs(497), fs(33), fs(501.5f), fs(1.4f), WHITE);
            fill_capsule(c, fs(33), fs(501.5f), fs(39.5f), fs(492), fs(1.4f), WHITE);
        }
        draw_shape(c, fs(816), fs(474), fs(40), fs(40), fs(20), WHITE, rgb(226, 226, 226),
                   g_hover == HIT_MUSIC ? rgb(28, 160, 232) : rgb(120, 120, 120), fs(2), .18f);

        text_in(dc, L"Which game would you like to play?", S(1), S(26) + S(1), c->w, S(48), g_font_heading, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE);
        text_in(dc, L"Which game would you like to play?", 0, S(26), c->w, S(48), g_font_heading, RGB(84, 84, 84), DT_CENTER | DT_SINGLELINE);
        for (int i = 0; i < NGAMES; i++) {
            int x = tile_x(i);
            tile_title(dc, GAMES[i].title, S(x + 22), S(TILE_Y + 16), S(tw - 44), S(TILE_H - 32), S(36), art_shadow(game_art(i)));
            text_in(dc, g_note[i], S(x - 14), S(TILE_Y + TILE_H + 10), S(tw + 28), S(22), g_font, g_note_hot[i] ? BLUE : RGB(88, 88, 88),
                    DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        text_in(dc, L"Play online", S(ox) + S(2), S(ONLINE_Y + 7) + S(2), S(ONLINE_W), S(30), g_font_pill, art_shadow(ART_ONLINE), DT_CENTER | DT_SINGLELINE);
        text_in(dc, L"Play online", S(ox), S(ONLINE_Y + 7), S(ONLINE_W), S(30), g_font_pill, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE);
        text_in(dc, g_note[NGAMES], S(ox), S(ONLINE_Y + 36), S(ONLINE_W), S(18), g_font_small, RGB(226, 255, 240), DT_CENTER | DT_SINGLELINE);
        text_in(dc, L"Keep games up to date", S(52), S(484), S(230), S(26), g_font, RGB(56, 56, 56), DT_VCENTER | DT_SINGLELINE);
        text_in(dc, g_nsongs ? (mon ? L"Music on" : L"Music off") : L"No music yet", S(560), S(474), S(246), S(22), g_font, RGB(66, 66, 66),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        text_in(dc, L"Open the Music folder", S(560), S(496), S(246), S(20), g_font_link, BLUE, DT_RIGHT | DT_SINGLELINE);
        text_in(dc, L"\x266A", S(816), S(474) - S(1), S(40), S(40), g_font_sym, mon && g_nsongs ? RGB(48, 48, 48) : RGB(150, 150, 150),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        GdiFlush(); /* GDI is done with the pixels; the slash goes over the note */
        if (!mon || !g_nsongs) fill_capsule(c, fs(827), fs(485), fs(845), fs(503), fs(1.6f), rgb(196, 56, 56));
    } else {
        BOOL hb = g_hover == HIT_BACK;
        int art = g_page < NGAMES ? game_art(g_page) : ART_ONLINE;
        draw_shape(c, fs(24), fs(18), fs(120), fs(44), fs(22), WHITE, rgb(224, 224, 224), hb ? rgb(28, 160, 232) : rgb(110, 110, 110), fs(2), .2f);
        fill_triangle(c, fs(44), fs(40), fs(57), fs(32), fs(57), fs(48), hb ? rgb(20, 130, 210) : rgb(78, 78, 78));
        draw_tile(c, fs(24), fs(84), fs(224), fs(250), fs(24), art, FALSE);
        draw_shape(c, fs(CARD_X), fs(8), fs(CARD_W), fs(512), fs(16), WHITE, WHITE, rgb(208, 208, 208), fs(1), .16f);
        int kind = g_page < NGAMES ? 0 : 1, sx = CARD_X + 14, sw = CARD_W - 28;
        for (size_t n = 0; n < sizeof SECTIONS / sizeof *SECTIONS; n++) {
            const Section* q = &SECTIONS[n];
            if (q->page != kind) continue;
            draw_shape(c, fs(sx), fs(q->y), fs(sw), fs(q->h), fs(10), WHITE, WHITE, rgb(214, 219, 226), fs(1), 0);
            if (q->num) draw_shape(c, fs(sx + 10), fs(q->y + 5), fs(18), fs(18), fs(9), rgb(40, 150, 230), rgb(0, 106, 198), rgb(0, 96, 180), fs(1), 0);
        }
        for (size_t n = 0; n < sizeof SECTIONS / sizeof *SECTIONS; n++) {
            const Section* q = &SECTIONS[n];
            if (q->page != kind) continue;
            if (q->num) text_in(dc, q->num, S(sx + 10), S(q->y + 5), S(18), S(18), g_font_small, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            text_in(dc, q->label, S(sx + (q->num ? 34 : 14)), S(q->y + 3), S(sw - 48), S(22), g_font_section, RGB(40, 52, 70), DT_VCENTER | DT_SINGLELINE);
        }
        text_in(dc, L"Back", S(66), S(18), S(72), S(44), g_font_pill, hb ? RGB(20, 120, 200) : RGB(60, 60, 60), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        tile_title(dc, g_page < NGAMES ? GAMES[g_page].title : L"Play online", S(44), S(98), S(184), S(222), S(34), art_shadow(art));
        text_in(dc, g_note[g_page], S(16), S(346), S(240), S(64), g_font, g_note_hot[g_page] ? BLUE : RGB(76, 76, 76), DT_CENTER | DT_WORDBREAK);
    }
    GdiFlush();
}

static void paint_window(HDC dc) {
    RECT rc;
    GetClientRect(g_wnd, &rc);
    int w = rc.right, h = rc.bottom;
    if (w <= 0 || h <= 0) return;
    if (!g_cache || g_cache_w != w || g_cache_h != h) {
        if (g_cache) DeleteObject(g_cache);
        BITMAPINFO bi = {{sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB, 0, 0, 0, 0, 0}};
        g_cache = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &g_cache_bits, NULL, 0);
        if (!g_cache) return;
        g_cache_w = w;
        g_cache_h = h;
        g_dirty = TRUE;
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_cache);
    if (g_dirty) {
        Canvas c = {w, h, g_cache_bits};
        layout_hits();
        render(mem, &c);
        g_dirty = FALSE;
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void go_page(int p) {
    g_page = p;
    g_hover = g_pressed = -1;
    g_sel = -1;
    layout_hits();
    refresh_page();
    if (p == PAGE_HOME) SetFocus(g_wnd);
}
static void activate(int id) {
    if (id >= 0 && id <= NGAMES) {
        go_page(id);
    } else if (id == HIT_BACK) {
        go_page(PAGE_HOME);
    } else if (id == HIT_AUTOUPDATE) {
        BOOL on = !auto_update_on();
        hub_set("auto_update", on ? L"1" : L"0");
        if (on) check_updates();
        redraw();
    } else if (id == HIT_MUSIC || id == HIT_MUSICDIR) {
        wchar_t d[MAX_PATH];
        music_dir(d);
        g_nsongs = list_music(d, g_songs, 64);
        if (id == HIT_MUSICDIR || !g_nsongs) { /* nothing to play yet: show where songs go */
            ensure_music_folder();
            ShellExecuteW(g_wnd, NULL, d, NULL, NULL, SW_SHOWNORMAL);
        } else if (music_on() && g_music_open && !g_music_paused) {
            hub_set("music", L"0");
            music_pause();
        } else {
            hub_set("music", L"1");
            music_resume();
        }
        redraw();
    }
}
static void on_mouse_move(int x, int y) {
    if (!g_tracking) {
        TRACKMOUSEEVENT t = {sizeof t, TME_LEAVE, g_wnd, 0};
        g_tracking = TrackMouseEvent(&t);
    }
    int h = hit_at(x, y);
    if (h == g_hover) return;
    g_hover = h;
    if (g_page == PAGE_HOME) g_sel = h >= 0 && h <= NGAMES ? h : -1; /* the pointer picks, as on the Wii */
    if (g_sel >= 0 && g_sel < NGAMES) g_last_tile = g_sel;
    redraw();
}
static void on_mouse_leave(void) {
    g_tracking = FALSE;
    if (g_hover == -1) return;
    g_hover = -1;
    if (g_page == PAGE_HOME) g_sel = -1;
    redraw();
}
/* Arrow keys pick a tile on the home screen (Down: Play online), Enter or Space opens it. */
static BOOL home_key(WPARAM k) {
    if (g_page != PAGE_HOME) return FALSE;
    int s = g_sel;
    switch (k) {
    case VK_LEFT: s = s < 0 ? g_last_tile : s == HIT_ONLINE ? s : s > 0 ? s - 1 : 0; break;
    case VK_RIGHT: s = s < 0 ? g_last_tile : s == HIT_ONLINE ? s : s < NGAMES - 1 ? s + 1 : s; break;
    case VK_DOWN: s = HIT_ONLINE; break;
    case VK_UP: s = g_last_tile; break;
    case VK_RETURN:
    case VK_SPACE:
        if (s >= 0) activate(s);
        return TRUE;
    default: return FALSE;
    }
    if (s >= 0 && s < NGAMES) g_last_tile = s;
    g_sel = s;
    redraw();
    return TRUE;
}
