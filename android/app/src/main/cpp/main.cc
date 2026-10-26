// Phase 1 trivial app (spec §6 Phase 1): SDL2 SOFTWARE renderer only.
//  - colored screen; 1 finger = cycle color; 2 fingers = send diagnostics now;
//    3 fingers = deliberate native crash (tests the crash pipeline);
//    BACK = clean quit.
#include <SDL.h>
#include <SDL_system.h>

#include "diag.h"

#include <unistd.h>

// ---------------------------------------------------------------------------
// minimal 5x7 font (uppercase A-Z, 0-9, space, - : . ( ) /), column encoding
static const char* kFontChars =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -:.()/";
static const unsigned char kFont[47][5] = {
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x7F,0x41,0x41,0x41,0x63}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x49}, // E
    {0x7F,0x09,0x09,0x09,0x09}, // F
    {0x7E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x7E,0x41,0x41,0x41,0x7E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x7E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x3F,0x40,0x38,0x40,0x3F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x00,0x00,0x00,0x00}, // space
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x00,0x5F,0x00,0x00}, // :
    {0x00,0x00,0x60,0x60,0x00}, // .
    {0x3E,0x41,0x41,0x41,0x22}, // (
    {0x22,0x41,0x41,0x41,0x3E}, // )
    {0x20,0x10,0x08,0x04,0x02}, // /
};
static const int kGlyphH = 7;

static void draw_char(SDL_Surface* s, char c, int x, int y, Uint32 col) {
    const char* p = kFontChars;
    int idx = -1;
    for (int i = 0; p[i]; i++) if (p[i] == c) { idx = i; break; }
    if (idx < 0) return;
    for (int cx = 0; cx < 5; cx++) {
        unsigned char bits = kFont[idx][cx];
        for (int cy = 0; cy < kGlyphH; cy++) {
            if (bits & (1u << cy)) {
                SDL_Rect r{ x + cx * 2, y + cy * 2, 2, 2 };
                SDL_FillRect(s, &r, col);
            }
        }
    }
}
static void draw_text(SDL_Surface* s, const char* txt, int x, int y, Uint32 col, int scale_step) {
    (void)scale_step;
    int cx = x;
    for (const char* p = txt; *p; p++) {
        draw_char(s, *p, cx, y, col);
        cx += 14;
    }
}

// ---------------------------------------------------------------------------
static volatile int g_deliberate = 0;

static void deliberate_crash() {
    // Test trigger for the native crash pipeline (handler -> pending report -> issue).
    volatile int* p = (volatile int*)0;
    g_deliberate = 1;
    *p = 42; // SIGSEGV
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    diag::boot_init();
    diag::log("native_main");

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        diag::log("sdl_init_failed");
        return 1;
    }
    diag::log("sdl_init_ok");

    // Same hint as upstream simsys_s2.cc: we handle touch events directly.
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");

    SDL_Window* win = SDL_CreateWindow("simutrans phase 1", 0, 0, 1280, 720,
                                       SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) {
        diag::log("window_failed");
        SDL_Quit();
        return 1;
    }
    diag::log("window_ok");

    SDL_Surface* screen = SDL_GetWindowSurface(win);

    struct Color { const char* name; Uint8 r, g, b; };
    static const Color kColors[] = {
        { "ROJO",    0xD0, 0x1E, 0x1E },
        { "VERDE",   0x1E, 0xA0, 0x2E },
        { "AZUL",    0x1E, 0x5E, 0xD0 },
        { "AMARILLO",0xD8, 0xC0, 0x10 },
        { "BLANCO",  0xE8, 0xE8, 0xE8 },
        { "CIAN",    0x10, 0xB8, 0xC8 },
        { "MAGENTA", 0xC8, 0x30, 0xA8 },
    };
    const int kNColors = (int)(sizeof(kColors) / sizeof(kColors[0]));
    int colorIdx = 0;

    bool quit = false;
    bool firstFrame = false;
    int fingersDown = 0;
    int maxFingers = 0;
    int pulse = 0;
    int frame = 0;
    char statusText[96] = "ENVIO: ESPERA";

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    quit = true;
                    break;
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_AC_BACK) quit = true;
                    break;
                case SDL_FINGERDOWN:
                    fingersDown++;
                    if (fingersDown > maxFingers) maxFingers = fingersDown;
                    break;
                case SDL_FINGERUP:
                    if (fingersDown > 0) fingersDown--;
                    if (fingersDown == 0 && maxFingers > 0) {
                        if (maxFingers == 1) {
                            colorIdx = (colorIdx + 1) % kNColors;
                            diag::log("one_finger_color_change");
                        } else if (maxFingers == 2) {
                            diag::log("two_finger_upload_request");
                            diag::request_upload();
                        } else {
                            diag::log("three_finger_deliberate_crash");
                            deliberate_crash();
                        }
                        maxFingers = 0;
                    }
                    break;
                default:
                    break;
            }
        }

        const Color& c = kColors[colorIdx];
        Uint32 bg = SDL_MapRGB(screen->format, c.r, c.g, c.b);
        Uint32 fg = SDL_MapRGB(screen->format, 0x10, 0x10, 0x10);
        Uint32 panel = SDL_MapRGB(screen->format, 0xF2, 0xF2, 0xF2);
        SDL_FillRect(screen, nullptr, bg);

        // bottom hint panel
        SDL_Rect r{ 0, screen->h - 150, screen->w, 150 };
        SDL_FillRect(screen, &r, panel);

        char l1[96], l2[96], l3[96], l4[96];
        // simple manual formatting (no snprintf to keep it trivially portable)
        {
            char* o1 = l1; const char* i1 = "SIMUTRANS ANDROID - FASE 1: OK";
            while ((*o1++ = *i1++));
            char* o2 = l2; const char* i2 = "1 DEDO: CAMBIAR COLOR";
            while ((*o2++ = *i2++));
            char* o3 = l3; const char* i3 = "2 DEDOS: ENVIAR REPORTE AHORA";
            while ((*o3++ = *i3++));
            char* o4 = l4; const char* i4 = "3 DEDOS: PRUEBA DE CIERRE (LA APP SE CIERRA)";
            while ((*o4++ = *i4++));
        }
        int y = screen->h - 140;
        draw_text(screen, l1, 30, y, fg, 1);
        draw_text(screen, l2, 30, y + 32, fg, 1);
        draw_text(screen, l3, 30, y + 64, fg, 1);
        draw_text(screen, l4, 30, y + 96, fg, 1);

        // color name in top-left corner + moving pulse bar (proves redraw)
        draw_text(screen, c.name, 30, 40, panel, 1);
        SDL_Rect bar{ (int)((pulse++ * 13) % (screen->w - 200)), 20, 180, 8 };
        SDL_FillRect(screen, &bar, panel);

        // upload status, top-right corner (polled once per second via JNI)
        if (frame % 30 == 0) {
            const char* st = diag::upload_status();
            int i = 0;
            for (; i < 95 && st[i]; i++) statusText[i] = st[i];
            statusText[i] = '\0';
        }
        int stLen = 0;
        while (statusText[stLen]) stLen++;
        draw_text(screen, statusText, screen->w - stLen * 14 - 30, 40, panel, 1);
        frame++;

        if (SDL_UpdateWindowSurface(win) == 0 && !firstFrame) {
            firstFrame = true;
            diag::boot_first_frame(); // marks first_frame_drawn + removes boot flag
        }
        SDL_Delay(33); // ~30 fps cap
    }

    diag::log("clean_exit");
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
