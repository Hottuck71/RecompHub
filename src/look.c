/* Recomp Hub's look and sound: the striped home screen with a glossy tile per game (the player's own picture from
   the Tile art folder, box art, or drawn art), its animations and click chime, the Back button and the white card
   the game pages sit on, and menu music from the Music folder. Included by hub.c (it uses its globals).

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
static double g_anim_t;   /* seconds, for things that move on their own */
static float g_ring_phase; /* the online tile's rings, moving out while it's picked */

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
        float f = fmodf(d * 6.f - g_ring_phase, 1.f);
        if (f < 0) f += 1.f;
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

/* ---- tile pictures: the player's own, from the "Tile art" folder next to the hub ---- */
typedef struct {
    int w, h;
    uint32_t* px;   /* 0xAARRGGBB */
    int sw, sh;
    uint32_t* small; /* a few pixels across: stretched, it's a blur, for around a tall cover */
} Picture;
static Picture g_pic[NGAMES];
static BOOL g_pic_own[NGAMES]; /* the player's picture (else a downloaded cover, or none) */
static void free_picture(Picture* p) {
    free(p->px);
    free(p->small);
    memset(p, 0, sizeof *p);
}
/* Averages the picture down to about 12 pixels across. */
static void make_small(Picture* p) {
    p->sw = p->w < 12 ? p->w : 12;
    p->sh = p->h * p->sw / p->w < 1 ? 1 : p->h * p->sw / p->w;
    p->small = malloc((size_t)p->sw * p->sh * 4);
    if (!p->small) return;
    for (int y = 0; y < p->sh; y++)
        for (int x = 0; x < p->sw; x++) {
            int x0 = x * p->w / p->sw, x1 = (x + 1) * p->w / p->sw, y0 = y * p->h / p->sh, y1 = (y + 1) * p->h / p->sh;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            double r = 0, g = 0, b = 0;
            int n = 0;
            for (int yy = y0; yy < y1; yy++)
                for (int xx = x0; xx < x1; xx++, n++) {
                    uint32_t q = p->px[yy * p->w + xx];
                    r += (q >> 16) & 255; g += (q >> 8) & 255; b += q & 255;
                }
            if (!n) n = 1;
            p->small[y * p->sw + x] = 0xFF000000u | ((uint32_t)(r / n) << 16) | ((uint32_t)(g / n) << 8) | (uint32_t)(b / n);
        }
}

/* ---- box art from GameTDB (the cover site Dolphin uses), for a game with no picture of the player's ---- */
static const wchar_t* game_id(int i) { /* the USA disc each recomp is built from */
    if (!wcscmp(GAMES[i].repo, L"ResortRecomp")) return L"RZTE01";
    if (!wcscmp(GAMES[i].repo, L"PartyRecomp")) return L"SUPE01";
    if (!wcscmp(GAMES[i].repo, L"Party9Recomp")) return L"SSQE01";
    return NULL;
}
static void cover_path(int i, wchar_t* out) {
    wchar_t dir[MAX_PATH], name[32];
    path_join(dir, g_dir, L"Tile art\\Covers");
    _snwprintf(name, 32, L"%s.png", game_id(i) ? game_id(i) : L"none");
    path_join(out, dir, name);
}

static void art_dir(wchar_t* out) { path_join(out, g_dir, L"Tile art"); }
static BOOL file_exists(const wchar_t* p);
/* Lower case, letters and digits only: "Wii Sports Resort" -> "wiisportsresort". */
static void norm_name(const wchar_t* s, wchar_t* out, int cap) {
    int n = 0;
    for (; *s && n < cap - 1; s++)
        if (iswalnum(*s)) out[n++] = towlower(*s);
    out[n] = 0;
}
/* A picture is a game's when its name (without the extension) is the game's title or recomp name, with or
   without "Wii" or "Recomp": "Wii Party.png", "party.jpg", "PartyRecomp.jpg", "marioparty9.jpg". */
static BOOL picture_matches(int i, const wchar_t* file_name) {
    wchar_t stem[MAX_PATH], n[MAX_PATH], k[MAX_PATH];
    wcsncpy(stem, file_name, MAX_PATH - 1);
    stem[MAX_PATH - 1] = 0;
    wchar_t* dot = wcsrchr(stem, L'.');
    if (dot) *dot = 0;
    norm_name(stem, n, MAX_PATH);
    if (!n[0]) return FALSE;
    norm_name(GAMES[i].title, k, MAX_PATH);
    if (!wcscmp(n, k) || (!wcsncmp(k, L"wii", 3) && !wcscmp(n, k + 3))) return TRUE;
    norm_name(GAMES[i].repo, k, MAX_PATH);
    if (!wcscmp(n, k)) return TRUE;
    size_t kl = wcslen(k);
    if (kl > 6 && !wcscmp(k + kl - 6, L"recomp")) {
        k[kl - 6] = 0;
        if (!wcscmp(n, k)) return TRUE;
    }
    return FALSE;
}
static BOOL picture_file(const wchar_t* name) {
    const wchar_t* e = wcsrchr(name, L'.');
    return e && (!_wcsicmp(e, L".jpg") || !_wcsicmp(e, L".jpeg") || !_wcsicmp(e, L".png") || !_wcsicmp(e, L".bmp") || !_wcsicmp(e, L".gif"));
}
/* Reads a picture with Windows' own decoders (WIC), shrunk to at most max_w wide. */
static BOOL load_picture(const wchar_t* path, Picture* out, UINT max_w) {
    static const GUID BGRA = {0x6fddc324, 0x4e03, 0x4bfe, {0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x0f}}; /* 32bppBGRA */
    static const GUID FACTORY = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
    static const GUID IFACTORY = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
    IWICImagingFactory* f = NULL;
    IWICBitmapDecoder* dec = NULL;
    IWICBitmapFrameDecode* frame = NULL;
    IWICBitmapScaler* sc = NULL;
    IWICFormatConverter* cv = NULL;
    BOOL ok = FALSE;
    UINT w = 0, h = 0;
    out->px = NULL;
    if (FAILED(CoCreateInstance(&FACTORY, NULL, CLSCTX_INPROC_SERVER, &IFACTORY, (void**)&f))) return FALSE;
    if (FAILED(IWICImagingFactory_CreateDecoderFromFilename(f, path, NULL, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) goto done;
    if (FAILED(IWICBitmapDecoder_GetFrame(dec, 0, &frame)) || FAILED(IWICBitmapFrameDecode_GetSize(frame, &w, &h)) || !w || !h) goto done;
    IWICBitmapSource* src = (IWICBitmapSource*)frame;
    if (w > max_w) {
        UINT nh = (UINT)((ULONGLONG)h * max_w / w);
        if (FAILED(IWICImagingFactory_CreateBitmapScaler(f, &sc)) ||
            FAILED(IWICBitmapScaler_Initialize(sc, src, max_w, nh ? nh : 1, WICBitmapInterpolationModeFant))) goto done;
        src = (IWICBitmapSource*)sc;
        w = max_w;
        h = nh ? nh : 1;
    }
    if (FAILED(IWICImagingFactory_CreateFormatConverter(f, &cv)) ||
        FAILED(IWICFormatConverter_Initialize(cv, src, &BGRA, WICBitmapDitherTypeNone, NULL, 0, WICBitmapPaletteTypeCustom))) goto done;
    out->px = malloc((size_t)w * h * 4);
    if (out->px && SUCCEEDED(IWICFormatConverter_CopyPixels(cv, NULL, w * 4, w * h * 4, (BYTE*)out->px))) {
        out->w = (int)w;
        out->h = (int)h;
        ok = TRUE;
    } else {
        free(out->px);
        out->px = NULL;
    }
done:
    if (cv) IWICFormatConverter_Release(cv);
    if (sc) IWICBitmapScaler_Release(sc);
    if (frame) IWICBitmapFrameDecode_Release(frame);
    if (dec) IWICBitmapDecoder_Release(dec);
    IWICImagingFactory_Release(f);
    return ok;
}
static void ensure_art_folder(void) {
    wchar_t dir[MAX_PATH], note[MAX_PATH];
    art_dir(dir);
    CreateDirectoryW(dir, NULL);
    path_join(note, dir, L"Put tile pictures here.txt");
    if (!file_exists(note)) {
        const char* t = "Put a picture for each game here (.jpg, .png, .bmp or .gif) and Recomp Hub shows it on that game's tile.\r\n"
                        "Name it after the game, for example:\r\n"
                        "  Wii Sports Resort.jpg   (or resort.jpg)\r\n"
                        "  Wii Party.jpg           (or party.jpg)\r\n"
                        "  Mario Party 9.jpg       (or marioparty9.jpg)\r\n"
                        "Wide pictures fit best (about 4:3).\r\n"
                        "A game without a picture here shows its box art, downloaded from GameTDB into the Covers folder.\r\n";
        write_file(note, t, (DWORD)strlen(t));
    }
}
static void invalidate_tile_cache(void);
/* (Re)reads the pictures; TRUE if any game's picture changed. */
static BOOL load_tile_pictures(void) {
    wchar_t dir[MAX_PATH], pat[MAX_PATH], found[NGAMES][MAX_PATH];
    static FILETIME stamp[NGAMES];
    static wchar_t had[NGAMES][MAX_PATH];
    FILETIME when[NGAMES];
    memset(found, 0, sizeof found);
    memset(when, 0, sizeof when);
    art_dir(dir);
    path_join(pat, dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !picture_file(fd.cFileName)) continue;
            for (int i = 0; i < NGAMES; i++)
                if (!found[i][0] && picture_matches(i, fd.cFileName)) {
                    path_join(found[i], dir, fd.cFileName);
                    when[i] = fd.ftLastWriteTime;
                }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    BOOL changed = FALSE;
    for (int i = 0; i < NGAMES; i++) {
        g_pic_own[i] = found[i][0] != 0;
        if (!found[i][0] && game_id(i)) { /* no picture of the player's: the downloaded box art, if there is one */
            WIN32_FILE_ATTRIBUTE_DATA a;
            cover_path(i, found[i]);
            if (GetFileAttributesExW(found[i], GetFileExInfoStandard, &a)) when[i] = a.ftLastWriteTime;
            else found[i][0] = 0;
        }
        if (!wcscmp(found[i], had[i]) && (!found[i][0] || !CompareFileTime(&when[i], &stamp[i]))) continue; /* same as before */
        free_picture(&g_pic[i]);
        if (found[i][0] && load_picture(found[i], &g_pic[i], 1024)) make_small(&g_pic[i]);
        wcscpy(had[i], found[i]);
        stamp[i] = when[i];
        changed = TRUE;
    }
    if (changed) invalidate_tile_cache();
    return changed;
}
static Col sample(const uint32_t* px, int w, int h, float u, float v);
/* The picture at (u, v) of a tile w/h = aspect, ih pixels high. A wide picture fills it (cropped to fit, centred).
   A tall one, like box art, is shown whole in the middle, with a soft shadow, over a dimmed blur of itself. */
static Col picture_at(const Picture* p, float u, float v, float aspect, float ih) {
    float ia = (float)p->w / p->h;
    if (ia < aspect * .75f && p->small) {
        float iw = aspect * ih, bh = ih * .9f, bw = bh * ia; /* the box, in pixels */
        float px = u * iw - iw / 2, py = v * ih - ih / 2;
        float d = sd_round_box(px, py, bw / 2, bh / 2, ih * .015f);
        Col back = mixc(sample(p->small, p->sw, p->sh, .5f + (u - .5f) * aspect / ia * .55f, .5f + (v - .5f) * .55f), BLACK, .3f);
        back = mixc(back, BLACK, .45f * (1.f - smooth(0.f, ih * .07f, sd_round_box(px, py - ih * .015f, bw / 2, bh / 2, ih * .015f))));
        if (d > .5f) return back;
        Col front = sample(p->px, p->w, p->h, (px + bw / 2) / bw, (py + bh / 2) / bh);
        return mixc(back, front, clampf(.5f - d, 0, 1));
    }
    if (ia > aspect) u = .5f + (u - .5f) * aspect / ia;
    else v = .5f + (v - .5f) * ia / aspect;
    return sample(p->px, p->w, p->h, u, v);
}
/* Smooth (bilinear) sampling at (u, v) in 0..1; see-through parts come out on light grey. */
static Col sample(const uint32_t* px, int w, int h, float u, float v) {
    struct { int w, h; const uint32_t* px; } q = {w, h, px}, *p = &q;
    float x = clampf(u * p->w - .5f, 0, (float)p->w - 1), y = clampf(v * p->h - .5f, 0, (float)p->h - 1);
    int x0 = (int)x, y0 = (int)y, x1 = x0 + 1 < p->w ? x0 + 1 : x0, y1 = y0 + 1 < p->h ? y0 + 1 : y0;
    float fx = x - x0, fy = y - y0;
    uint32_t a = p->px[y0 * p->w + x0], b = p->px[y0 * p->w + x1], c = p->px[y1 * p->w + x0], d = p->px[y1 * p->w + x1];
#define CH(q, s) ((float)(((q) >> (s)) & 255))
#define LERP2(s) ((CH(a, s) * (1 - fx) + CH(b, s) * fx) * (1 - fy) + (CH(c, s) * (1 - fx) + CH(d, s) * fx) * fy)
    Col k = rgb(LERP2(16), LERP2(8), LERP2(0));
    float al = LERP2(24) / 255.f;
#undef LERP2
#undef CH
    return mixc(rgb(236, 236, 236), k, al); /* see-through parts on light grey */
}

/* ---- tiles ---- */
/* What's inside a tile's rim at (u, v): the picture or drawn art, with gloss over the top. */
static Col tile_pixel(int who, float u, float v, float aspect, float pix) {
    Col k;
    if (who >= 0 && g_pic[who].px) k = picture_at(&g_pic[who], u, v, aspect, 1.f / pix);
    else k = art_at(who >= 0 ? game_art(who) : ART_ONLINE, u, v, aspect, pix);
    float gx = (u - .5f) / .98f, gy = (v + .62f) / 1.08f;
    return mixc(k, WHITE, (who >= 0 && g_pic[who].px ? .13f : .2f) * (1.f - smooth(.96f, 1.0f, sqrtf(gx * gx + gy * gy))));
}
/* Each game tile's inside, kept at the size it was last drawn (redrawn only when that changes). */
typedef struct { int w, h; uint32_t* px; } TileCache;
static TileCache g_tc[NGAMES][2]; /* the home screen's size and the game page's */
static void invalidate_tile_cache(void) {
    for (int i = 0; i < NGAMES; i++)
        for (int j = 0; j < 2; j++) g_tc[i][j].w = 0;
}
static const uint32_t* tile_inside(int who, int iw, int ih) {
    if (who < 0 || iw <= 0 || ih <= 0) return NULL;
    TileCache* t = &g_tc[who][0];
    if (!(t->w == iw && t->h == ih)) {
        TileCache* o = &g_tc[who][1];
        if (o->w == iw && o->h == ih) return o->px;
        /* replace the one not used last: keep both a resting size and a growing one */
        TileCache tmp = *t; *t = *o; *o = tmp;
        free(t->px);
        t->px = malloc((size_t)iw * ih * 4);
        if (!t->px) { t->w = 0; return NULL; }
        t->w = iw;
        t->h = ih;
        for (int y = 0; y < ih; y++)
            for (int x = 0; x < iw; x++) {
                Col k = tile_pixel(who, (x + .5f) / iw, (y + .5f) / ih, (float)iw / ih, 1.f / ih);
                t->px[y * iw + x] = ((uint32_t)clampf(k.r, 0, 255) << 16) | ((uint32_t)clampf(k.g, 0, 255) << 8) | (uint32_t)clampf(k.b, 0, 255);
            }
    }
    return t->px;
}

/* A tile: soft shadow, a dark rim (blue and glowing as lit goes to 1), a silver bevel, then the picture.
   who: a game, or -1 for Play online. shine: where a sweep of light is across it (< 0: none); op: 0..1. */
static void draw_tile(Canvas* c, float x, float y, float w, float h, float radius, int who, float lit, float shine, float op) {
    if (op <= .01f || w < 16 || h < 16) return;
    x = roundf(x); y = roundf(y); w = roundf(w); h = roundf(h);
    float rim = fs(3.f), bevel = fs(4.f), pad = fs(16.f);
    float cx = x + w / 2, cy = y + h / 2, hw = w / 2, hh = h / 2;
    int ix = (int)roundf(x + rim + bevel), iy = (int)roundf(y + rim + bevel);
    int iw = (int)roundf(w - 2 * (rim + bevel)), ih = (int)roundf(h - 2 * (rim + bevel));
    const uint32_t* inside = tile_inside(who, iw, ih);
    Col rim_col = mixc(rgb(74, 74, 74), rgb(28, 160, 232), lit);
    float glow = lit * (.62f + .2f * sinf((float)g_anim_t * 4.5f));
    for (int py = (int)(y - pad); py < (int)(y + h + pad); py++) {
        if (py < 0 || py >= c->h) continue;
        for (int px = (int)(x - pad); px < (int)(x + w + pad); px++) {
            if (px < 0 || px >= c->w) continue;
            float fx = px + .5f - cx, fy = py + .5f - cy;
            put(c, px, py, BLACK, op * .26f * (1.f - smooth(-fs(2.f), fs(12.f), sd_round_box(fx, fy - fs(5.f), hw, hh, radius))));
            float d = sd_round_box(fx, fy, hw, hh, radius);
            if (glow > 0 && d > 0) put(c, px, py, rgb(40, 180, 245), op * glow * (1.f - smooth(0.f, fs(12.f), d)));
            float cov = clampf(.5f - d, 0, 1) * op;
            if (cov <= 0) continue;
            float in = -d;
            int qx = px - ix, qy = py - iy;
            Col pic = rim_col;
            if (qx >= 0 && qy >= 0 && qx < iw && qy < ih) {
                float u = (qx + .5f) / iw, v = (qy + .5f) / ih;
                if (inside) {
                    uint32_t q = inside[qy * iw + qx];
                    pic = rgb((float)((q >> 16) & 255), (float)((q >> 8) & 255), (float)(q & 255));
                } else {
                    pic = tile_pixel(who, u, v, (float)iw / ih, 1.f / ih);
                }
                if (shine > -1.f) {
                    float s = u * .7f + v * .5f - shine;
                    pic = mixc(pic, WHITE, .5f * expf(-s * s * 60.f));
                }
            }
            pic = mixc(pic, BLACK, .16f * (1.f - smooth(0.f, fs(6.f), in - rim - bevel))); /* inner shade */
            Col silver = mixc(rgb(252, 252, 252), rgb(192, 192, 196), (in - rim) / bevel);
            Col k = mixc(rim_col, silver, clampf(in - rim + .5f, 0, 1));
            k = mixc(k, pic, clampf(in - rim - bevel + .5f, 0, 1));
            put(c, px, py, k, cov);
        }
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

/* ================= the click ================= */
/* Opening a tile plays a short chime: two quick bell notes (E6 then B6) over a soft pop. It's made here, as a .wav
   in memory, so no sound file comes with the hub. */
#define CLICK_RATE 44100
#define CLICK_SAMPLES (CLICK_RATE * 28 / 100)
static unsigned char* g_click_wav;
static DWORD g_click_len;
static float bell(float t, float f) {
    if (t < 0) return 0.f;
    const float tau = 6.2831853f;
    float env = (1.f - expf(-t * 900.f)) * expf(-t * 15.f);
    return env * (sinf(tau * f * t) + .25f * sinf(tau * 2.f * f * t) * expf(-t * 25.f) + .08f * sinf(tau * 3.f * f * t) * expf(-t * 40.f));
}
static float click_sample(float t) {
    float s = bell(t, 1318.5f) + .75f * bell(t - .055f, 1975.5f);
    s += .45f * expf(-t * 70.f) * sinf(6.2831853f * 190.f * t) * (1.f - expf(-t * 2000.f)); /* the pop */
    float fade = t > .24f ? 1.f - (t - .24f) / .04f : 1.f; /* to silence at the very end */
    return s * .24f * clampf(fade, 0.f, 1.f);
}
static void make_click(void) {
    DWORD data = CLICK_SAMPLES * 2;
    unsigned char* w = malloc(44 + data);
    if (!w) return;
    DWORD v;
    WORD h;
    memcpy(w, "RIFF", 4); v = 36 + data; memcpy(w + 4, &v, 4); memcpy(w + 8, "WAVEfmt ", 8);
    v = 16; memcpy(w + 16, &v, 4); h = 1; memcpy(w + 20, &h, 2); memcpy(w + 22, &h, 2); /* PCM, mono */
    v = CLICK_RATE; memcpy(w + 24, &v, 4); v = CLICK_RATE * 2; memcpy(w + 28, &v, 4);
    h = 2; memcpy(w + 32, &h, 2); h = 16; memcpy(w + 34, &h, 2);
    memcpy(w + 36, "data", 4); memcpy(w + 40, &data, 4);
    for (int n = 0; n < CLICK_SAMPLES; n++) {
        short q = (short)(clampf(click_sample((float)n / CLICK_RATE), -1.f, 1.f) * 32767.f);
        memcpy(w + 44 + n * 2, &q, 2);
    }
    g_click_wav = w;
    g_click_len = 44 + data;
}
static void play_click(void) {
    if (!g_click_wav) make_click();
    if (g_click_wav) PlaySoundW((LPCWSTR)g_click_wav, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

/* ================= the screens ================= */
/* g_page: a game (0..NGAMES-1), PAGE_ONLINE, or PAGE_HOME, the "Which game would you like to play?" screen. */
enum { HIT_ONLINE = NGAMES, HIT_BACK = 50, HIT_MUSIC, HIT_MUSICDIR, HIT_AUTOUPDATE, HIT_ARTDIR };
typedef struct { int x, y, w, h, id; } Hit; /* in pixels */
static Hit g_hits[16];
static int g_nhits, g_hover = -1, g_sel = -1, g_pressed = -1, g_last_tile = 0;
static BOOL g_tracking;
static wchar_t g_note[NGAMES + 1][160]; /* the line under each tile, and the online button's */
static BOOL g_note_hot[NGAMES + 1];     /* an update is waiting: shown in blue */
static HBITMAP g_cache;
static void* g_cache_bits;
static uint32_t* g_bg; /* the striped background, made once per window size */
static int g_cache_w, g_cache_h;
static BOOL g_dirty = TRUE;
static void refresh_page(void);
static void anim_kick(void);

static void redraw(void) {
    g_dirty = TRUE;
    if (g_wnd) InvalidateRect(g_wnd, NULL, FALSE);
    anim_kick();
}

/* Layout, in 96-DPI units, of the 880 x 528 window. Tiles are wide (about 4:3), like the pictures. */
#define CLIENT_W 880
#define TILE_Y 104
#define TILE_GAP 28
#define ONLINE_W 320
#define ONLINE_H 62
#define ONLINE_Y 368
#define CARD_X 272
#define CARD_W 600
#define SIDE_X 20
#define SIDE_Y 84
#define SIDE_W 232
#define SIDE_H 170
static int tile_w(void) {
    int w = (CLIENT_W - 56 - (NGAMES - 1) * TILE_GAP) / NGAMES;
    return w > 256 ? 256 : w;
}
static int tile_h(void) { return tile_w() * 72 / 100; }
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
        for (int i = 0; i < NGAMES; i++) add_hit(tile_x(i), TILE_Y, tile_w(), tile_h() + 36, i);
        add_hit((CLIENT_W - ONLINE_W) / 2, ONLINE_Y, ONLINE_W, ONLINE_H, HIT_ONLINE);
        add_hit(22, 484, 250, 26, HIT_AUTOUPDATE);
        add_hit(816, 474, 40, 40, HIT_MUSIC);
        add_hit(676, 496, 132, 20, HIT_MUSICDIR);
        add_hit(CLIENT_W / 2 - 80, 496, 160, 20, HIT_ARTDIR);
    } else {
        add_hit(24, 18, 120, 44, HIT_BACK);
    }
}
static int hit_at(int x, int y) {
    for (int i = 0; i < g_nhits; i++)
        if (x >= g_hits[i].x && y >= g_hits[i].y && x < g_hits[i].x + g_hits[i].w && y < g_hits[i].y + g_hits[i].h) return g_hits[i].id;
    return -1;
}

/* ---- animation ----
   Values ease toward their targets each frame (about 60 a second) while anything moves; when nothing does, the
   timer stops and nothing is drawn until something changes. */
#define TIMER_ANIM 9
typedef struct { float scale, lit; double lit_since; } TileAnim;
static TileAnim g_ta[NGAMES + 1]; /* the games' tiles, then Play online */
static double g_intro_t0 = -100, g_intro_len = .55, g_intro_gap = .09; /* tiles dropping in */
static double g_page_t0 = -100;   /* a page's Back button and tile sliding in */
static double g_launch_t0 = -100; /* a picked tile zooming before its page opens */
static int g_launch = -1;
static double g_anim_last;
static BOOL g_anim_on, g_app_active = TRUE;

static double now_s(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart / (double)f.QuadPart;
}
static float ease_out_cubic(float x) { x = clampf(x, 0, 1); return 1.f - (1.f - x) * (1.f - x) * (1.f - x); }
static float ease_out_back(float x) { x = clampf(x, 0, 1) - 1.f; return 1.f + 2.70158f * x * x * x + 1.70158f * x * x; }
static float tile_scale_target(int i) {
    return 1.f + (g_sel == i ? .06f : 0.f) - (g_pressed == i && g_hover == i ? .05f : 0.f);
}
static float intro_at(int i, double t) { return (float)((t - g_intro_t0 - i * g_intro_gap) / g_intro_len); }
static void start_intro(double len, double gap) {
    g_intro_len = len;
    g_intro_gap = gap;
    g_intro_t0 = now_s();
    for (int i = 0; i <= NGAMES; i++) { g_ta[i].scale = 1.f; g_ta[i].lit = 0.f; }
}
static void set_sel(int s) {
    if (s == g_sel) return;
    g_sel = s;
    if (s >= 0 && s <= NGAMES) g_ta[s].lit_since = now_s();
    if (s >= 0 && s < NGAMES) g_last_tile = s;
}
/* Something moving: tiles dropping in, a page sliding in, a zoom, or a size or glow still easing. */
static BOOL anim_moving(void) {
    double t = now_s();
    if (g_page == PAGE_HOME && intro_at(NGAMES + 1, t) < 1.f) return TRUE;
    if (t - g_page_t0 < .4 || g_launch >= 0) return TRUE;
    for (int i = 0; i <= NGAMES; i++) {
        float lt = g_page == PAGE_HOME && g_sel == i ? 1.f : 0.f;
        if (fabsf(g_ta[i].scale - tile_scale_target(i)) > .002f || fabsf(g_ta[i].lit - lt) > .004f) return TRUE;
    }
    return FALSE;
}
/* A picked tile's glow breathing and light sweeping over it: slower frames are enough. */
static BOOL anim_ambient(void) { return g_page == PAGE_HOME && g_sel >= 0 && g_app_active && !IsIconic(g_wnd); }
static UINT g_anim_ms;
static void anim_kick(void) {
    if (g_anim_on || !g_wnd) return;
    UINT ms = anim_moving() ? 15 : anim_ambient() ? 33 : 0;
    if (!ms) return;
    g_anim_last = now_s();
    g_anim_ms = ms;
    g_anim_on = SetTimer(g_wnd, TIMER_ANIM, ms, NULL) != 0;
}
static void go_page(int p);
/* One step: ease the values, finish a launch, draw, and stop when everything has settled. */
static void anim_frame(void) {
    double t = now_s();
    float dt = (float)clampf((float)(t - g_anim_last), 0.f, .1f);
    g_anim_last = t;
    g_anim_t = t;
    float k = 1.f - expf(-dt * 15.f);
    for (int i = 0; i <= NGAMES; i++) {
        g_ta[i].scale += (tile_scale_target(i) - g_ta[i].scale) * k;
        g_ta[i].lit += ((g_page == PAGE_HOME && g_sel == i ? 1.f : 0.f) - g_ta[i].lit) * k;
    }
    g_ring_phase += dt * (.12f + .55f * g_ta[NGAMES].lit);
    if (g_launch >= 0 && t - g_launch_t0 > .2) {
        int id = g_launch;
        g_launch = -1;
        go_page(id);
    }
    g_dirty = TRUE;
    InvalidateRect(g_wnd, NULL, FALSE);
    UINT ms = anim_moving() ? 15 : anim_ambient() ? 33 : 0;
    if (!ms) {
        KillTimer(g_wnd, TIMER_ANIM);
        g_anim_on = FALSE;
    } else if (ms != g_anim_ms) {
        g_anim_ms = ms;
        SetTimer(g_wnd, TIMER_ANIM, ms, NULL);
    }
}
/* Where the sweep of light is on a picked tile: once every 2.6 seconds, starting soon after it's picked. */
static float shine_at(int i, double t) {
    if (g_ta[i].lit < .3f) return -2.f;
    double s = fmod(t - g_ta[i].lit_since - .15, 2.6);
    if (t - g_ta[i].lit_since < .15 || s > .85) return -2.f;
    return (float)(-.5 + s / .85 * 2.0);
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

/* One tile on the home screen as it is right now: where, how big, how see-through. */
typedef struct { float x, y, w, h, op; BOOL text; } TilePose;
static TilePose home_pose(int i, double t) {
    int bx = i < NGAMES ? tile_x(i) : (CLIENT_W - ONLINE_W) / 2, by = i < NGAMES ? TILE_Y : ONLINE_Y;
    int bw = i < NGAMES ? tile_w() : ONLINE_W, bh = i < NGAMES ? tile_h() : ONLINE_H;
    float in = intro_at(i, t);
    float s = g_ta[i].scale * (.9f + .1f * ease_out_back(in));
    float dy = i < NGAMES ? -(1.f - ease_out_cubic(in)) * 46.f : (1.f - ease_out_cubic(in)) * 34.f;
    float op = smooth(0.f, .45f, in);
    if (g_launch == i) { /* zooms toward you, fading, then its page opens */
        float l = (float)((t - g_launch_t0) / .2);
        s *= 1.f + .22f * ease_out_cubic(l);
        op *= 1.f - .55f * clampf(l, 0, 1);
    }
    TilePose p;
    p.w = (float)S(1) * bw * s;
    p.h = (float)S(1) * bh * s;
    p.x = (float)S(1) * (bx + bw / 2.f) - p.w / 2;
    p.y = (float)S(1) * (by + bh / 2.f + dy) - p.h / 2;
    p.op = op;
    p.text = op > .6f;
    return p;
}

static void render(HDC dc, Canvas* c) {
    const COLORREF BLUE = RGB(0, 103, 192);
    double t = now_s();
    if (g_bg) memcpy(c->px, g_bg, (size_t)c->w * c->h * 4);
    else draw_stripes(c);
    SetBkMode(dc, TRANSPARENT);
    if (g_page == PAGE_HOME) {
        BOOL au = auto_update_on(), mon = music_on();
        /* the picked tile last, so it's on top as it grows */
        int order[NGAMES + 1], n = 0;
        for (int i = 0; i <= NGAMES; i++) if (i != g_sel && i != g_launch) order[n++] = i;
        if (g_sel >= 0 && g_sel != g_launch) order[n++] = g_sel;
        if (g_launch >= 0) order[n++] = g_launch;
        TilePose pose[NGAMES + 1];
        for (int k = 0; k < n; k++) {
            int i = order[k];
            pose[i] = home_pose(i, t);
            TilePose* p = &pose[i];
            float r = i < NGAMES ? fs(22.f) * p->w / S(tile_w()) : p->h / 2;
            draw_tile(c, p->x, p->y, p->w, p->h, r, i < NGAMES ? i : -1, g_ta[i].lit, shine_at(i, t), p->op);
            /* titles: drawn tiles only (a picture has its own), and the button's words */
            if (p->text && (i == NGAMES || !g_pic[i].px)) {
                GdiFlush();
                if (i == NGAMES) {
                    int y = (int)(p->y + p->h / 2) - S(24);
                    text_in(dc, L"Play online", (int)p->x + S(2), y + S(2), (int)p->w, S(30), g_font_pill, art_shadow(ART_ONLINE), DT_CENTER | DT_SINGLELINE);
                    text_in(dc, L"Play online", (int)p->x, y, (int)p->w, S(30), g_font_pill, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE);
                    text_in(dc, g_note[NGAMES], (int)p->x, y + S(29), (int)p->w, S(18), g_font_small, RGB(226, 255, 240), DT_CENTER | DT_SINGLELINE);
                } else {
                    float s = p->w / S(tile_w());
                    tile_title(dc, GAMES[i].title, (int)(p->x + p->w * .09f), (int)(p->y + p->h * .08f), (int)(p->w * .82f), (int)(p->h * .84f),
                               (int)(S(34) * s), art_shadow(game_art(i)));
                }
                GdiFlush();
            }
        }
        /* "Keep games up to date": a rounded check box */
        draw_shape(c, fs(24), fs(487), fs(20), fs(20), fs(5), au ? rgb(30, 144, 230) : WHITE, au ? rgb(0, 104, 196) : rgb(236, 236, 236),
                   g_hover == HIT_AUTOUPDATE ? rgb(28, 160, 232) : rgb(130, 130, 130), fs(1.5f), 0);
        if (au) {
            fill_capsule(c, fs(29), fs(497), fs(33), fs(501.5f), fs(1.4f), WHITE);
            fill_capsule(c, fs(33), fs(501.5f), fs(39.5f), fs(492), fs(1.4f), WHITE);
        }
        draw_shape(c, fs(816), fs(474), fs(40), fs(40), fs(20), WHITE, rgb(226, 226, 226),
                   g_hover == HIT_MUSIC ? rgb(28, 160, 232) : rgb(120, 120, 120), fs(2), .18f);
        GdiFlush();
        text_in(dc, L"Which game would you like to play?", S(1), S(26) + S(1), c->w, S(48), g_font_heading, RGB(255, 255, 255), DT_CENTER | DT_SINGLELINE);
        text_in(dc, L"Which game would you like to play?", 0, S(26), c->w, S(48), g_font_heading, RGB(84, 84, 84), DT_CENTER | DT_SINGLELINE);
        for (int i = 0; i < NGAMES; i++)
            if (pose[i].text)
                text_in(dc, g_note[i], S(tile_x(i) - 14), S(TILE_Y + tile_h() + 12), S(tile_w() + 28), S(22), g_font, g_note_hot[i] ? BLUE : RGB(88, 88, 88),
                        DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        text_in(dc, L"Keep games up to date", S(52), S(484), S(230), S(26), g_font, RGB(56, 56, 56), DT_VCENTER | DT_SINGLELINE);
        text_in(dc, L"Open the Tile art folder", S(CLIENT_W / 2 - 100), S(496), S(200), S(20), g_font_link, BLUE, DT_CENTER | DT_SINGLELINE);
        text_in(dc, g_nsongs ? (mon ? L"Music on" : L"Music off") : L"No music yet", S(560), S(474), S(246), S(22), g_font, RGB(66, 66, 66),
                DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
        text_in(dc, L"Open the Music folder", S(560), S(496), S(246), S(20), g_font_link, BLUE, DT_RIGHT | DT_SINGLELINE);
        text_in(dc, L"\x266A", S(816), S(474) - S(1), S(40), S(40), g_font_sym, mon && g_nsongs ? RGB(48, 48, 48) : RGB(150, 150, 150),
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        GdiFlush(); /* GDI is done with the pixels; the slash goes over the note */
        if (!mon || !g_nsongs) fill_capsule(c, fs(827), fs(485), fs(845), fs(503), fs(1.6f), rgb(196, 56, 56));
    } else {
        BOOL hb = g_hover == HIT_BACK;
        int who = g_page < NGAMES ? g_page : -1, art = g_page < NGAMES ? game_art(g_page) : ART_ONLINE;
        float in = ease_out_cubic((float)((t - g_page_t0) / .32)), dx = -(1.f - in) * fs(260.f); /* slides in from the left */
        draw_shape(c, fs(CARD_X), fs(8), fs(CARD_W), fs(512), fs(16), WHITE, WHITE, rgb(208, 208, 208), fs(1), .16f);
        draw_shape(c, fs(24) + dx, fs(18), fs(120), fs(44), fs(22), WHITE, rgb(224, 224, 224), hb ? rgb(28, 160, 232) : rgb(110, 110, 110), fs(2), .2f);
        fill_triangle(c, fs(44) + dx, fs(40), fs(57) + dx, fs(32), fs(57) + dx, fs(48), hb ? rgb(20, 130, 210) : rgb(78, 78, 78));
        draw_tile(c, fs(SIDE_X) + dx, fs(SIDE_Y), fs(SIDE_W), fs(SIDE_H), fs(20), who, 0.f, -2.f, 1.f);
        int kind = g_page < NGAMES ? 0 : 1, sx = CARD_X + 14, sw = CARD_W - 28;
        for (size_t n = 0; n < sizeof SECTIONS / sizeof *SECTIONS; n++) {
            const Section* q = &SECTIONS[n];
            if (q->page != kind) continue;
            draw_shape(c, fs(sx), fs(q->y), fs(sw), fs(q->h), fs(10), WHITE, WHITE, rgb(214, 219, 226), fs(1), 0);
            if (q->num) draw_shape(c, fs(sx + 10), fs(q->y + 5), fs(18), fs(18), fs(9), rgb(40, 150, 230), rgb(0, 106, 198), rgb(0, 96, 180), fs(1), 0);
        }
        GdiFlush();
        for (size_t n = 0; n < sizeof SECTIONS / sizeof *SECTIONS; n++) {
            const Section* q = &SECTIONS[n];
            if (q->page != kind) continue;
            if (q->num) text_in(dc, q->num, S(sx + 10), S(q->y + 5), S(18), S(18), g_font_small, RGB(255, 255, 255), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            text_in(dc, q->label, S(sx + (q->num ? 34 : 14)), S(q->y + 3), S(sw - 48), S(22), g_font_section, RGB(40, 52, 70), DT_VCENTER | DT_SINGLELINE);
        }
        int ox = (int)dx;
        text_in(dc, L"Back", S(66) + ox, S(18), S(72), S(44), g_font_pill, hb ? RGB(20, 120, 200) : RGB(60, 60, 60), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        if (who < 0 || !g_pic[who].px)
            tile_title(dc, who >= 0 ? GAMES[who].title : L"Play online", S(SIDE_X + 18) + ox, S(SIDE_Y + 12), S(SIDE_W - 36), S(SIDE_H - 24), S(30), art_shadow(art));
        text_in(dc, g_note[g_page], S(12) + ox, S(SIDE_Y + SIDE_H + 14), S(248), S(64), g_font, g_note_hot[g_page] ? BLUE : RGB(76, 76, 76),
                DT_CENTER | DT_WORDBREAK);
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
        free(g_bg);
        g_bg = malloc((size_t)w * h * 4);
        if (g_bg) {
            Canvas b = {w, h, g_bg};
            draw_stripes(&b);
        }
        g_dirty = TRUE;
    }
    HDC mem = CreateCompatibleDC(dc);
    HGDIOBJ old = SelectObject(mem, g_cache);
    if (g_dirty) {
        Canvas c = {w, h, g_cache_bits};
        GdiFlush();
        layout_hits();
        render(mem, &c);
        g_dirty = FALSE;
    }
    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteDC(mem);
}

static void go_page(int p) {
    BOOL from_home = g_page == PAGE_HOME;
    g_page = p;
    g_hover = g_pressed = -1;
    set_sel(-1);
    g_launch = -1;
    if (p == PAGE_HOME) start_intro(.38, .05); /* back home: a quick drop-in */
    else if (from_home) g_page_t0 = now_s();
    layout_hits();
    refresh_page();
    if (p == PAGE_HOME) SetFocus(g_wnd);
}
static void activate(int id) {
    if (id >= 0 && id <= NGAMES) {
        if (g_page != PAGE_HOME || g_launch >= 0) return;
        play_click();
        set_sel(id);
        g_launch = id; /* the tile zooms, then anim_frame opens its page */
        g_launch_t0 = now_s();
        redraw();
    } else if (id == HIT_BACK) {
        go_page(PAGE_HOME);
    } else if (id == HIT_AUTOUPDATE) {
        BOOL on = !auto_update_on();
        hub_set("auto_update", on ? L"1" : L"0");
        if (on) check_updates();
        redraw();
    } else if (id == HIT_ARTDIR) {
        wchar_t d[MAX_PATH];
        ensure_art_folder();
        art_dir(d);
        ShellExecuteW(g_wnd, NULL, d, NULL, NULL, SW_SHOWNORMAL);
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
    if (g_page == PAGE_HOME && g_launch < 0) set_sel(h >= 0 && h <= NGAMES ? h : -1); /* the pointer picks, as on the Wii */
    redraw();
}
static void on_mouse_leave(void) {
    g_tracking = FALSE;
    if (g_hover == -1) return;
    g_hover = -1;
    if (g_page == PAGE_HOME && g_launch < 0) set_sel(-1);
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
    set_sel(s);
    redraw();
    return TRUE;
}

/* ---- fetching box art ---- */
#define WM_COVERS_FETCHED (WM_APP + 20)
static volatile LONG g_cover_fetching;
static BOOL g_cover_tried[NGAMES];
static BOOL is_png(const wchar_t* path) {
    char* b = NULL;
    DWORD n = 0;
    b = read_file(path, &n);
    BOOL ok = b && n > 8 && !memcmp(b, "\x89PNG\r\n\x1a\n", 8);
    free(b);
    return ok;
}
/* Downloads the box art of each game that has neither a picture of the player's nor a cover yet. Returns how many
   it got. Runs on its own thread; tells the window when it got any. */
static int fetch_covers_now(void) {
    int got = 0;
    for (int i = 0; i < NGAMES; i++) {
        wchar_t path[MAX_PATH], part[MAX_PATH], url[128];
        if (!game_id(i) || g_pic_own[i] || g_cover_tried[i]) continue;
        cover_path(i, path);
        if (file_exists(path)) continue;
        g_cover_tried[i] = TRUE; /* once per run, even if GameTDB can't be reached */
        wchar_t dir[MAX_PATH];
        wcscpy(dir, path);
        *wcsrchr(dir, L'\\') = 0;
        CreateDirectoryW(dir, NULL);
        _snwprintf(part, MAX_PATH, L"%s.part", path);
        _snwprintf(url, 128, L"https://art.gametdb.com/wii/cover/US/%s.png", game_id(i));
        HANDLE f = CreateFileW(part, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (f == INVALID_HANDLE_VALUE) continue;
        int status = http_get(url, L"image/png", f, NULL, NULL);
        CloseHandle(f);
        if (status == 200 && is_png(part) && MoveFileExW(part, path, MOVEFILE_REPLACE_EXISTING)) got++;
        else DeleteFileW(part);
    }
    return got;
}
static DWORD WINAPI fetch_covers_job(LPVOID arg) {
    (void)arg;
    if (fetch_covers_now() && g_wnd) PostMessageW(g_wnd, WM_COVERS_FETCHED, 0, 0);
    InterlockedExchange(&g_cover_fetching, 0);
    return 0;
}
static void fetch_covers(void) {
    if (InterlockedCompareExchange(&g_cover_fetching, 1, 0)) return;
    HANDLE t = CreateThread(NULL, 0, fetch_covers_job, NULL, 0, NULL);
    if (t) CloseHandle(t);
    else InterlockedExchange(&g_cover_fetching, 0);
}
