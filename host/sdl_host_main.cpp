/**
 * @file host/sdl_host_main.cpp
 * @brief SDL2 interactive host -- desktop window for eyeballing widgets (P3 demo)
 *
 * Opens an SDL2 window (scaled up from the 320x240 staging), renders the widget
 * tree each frame, and feeds mouse motion/down/up into Desktop::dispatch_pointer
 * so you can click buttons and drag the slider for real. Built opt-in
 * (-DCINUX_HOST_SDL=ON) since SDL2 is an external host-side dependency (host
 * layer; core stays SDL-free).
 *
 * Run:  cmake -DCINUX_HOST_SDL=ON -S . -B build && cmake --build build -- sdl-host
 *       ./build/sdl-host      (WSL2: shows via WSLg on the Windows desktop)
 */

#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdio>

#include "compositor.hpp"
#include "event_payload.hpp"
#include "font.hpp"
#include "paint_list.hpp"
#include "region.hpp"
#include "swraster.hpp"
#include "theme.hpp"
#include "widget.hpp"
#include "widget/button.hpp"
#include "widget/container.hpp"
#include "widget/label.hpp"
#include "widget/slider.hpp"

namespace {
using namespace cinux::gui;
}  // namespace

int main() {
    constexpr uint32_t kW = 320, kH = 240;
    constexpr int      kScale = 2;  // window 640x480 (320x240 is tiny on a desktop)

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::printf("SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* win =
        SDL_CreateWindow("Cinux-GUI demo", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         static_cast<int>(kW) * kScale, static_cast<int>(kH) * kScale, 0);
    if (win == nullptr) {
        std::printf("window failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (ren == nullptr) {
        ren = SDL_CreateRenderer(win, -1, 0);
    }
    if (ren == nullptr) {
        std::printf("renderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Texture* tex =
        SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
    if (tex == nullptr) {
        std::printf("texture failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(ren, kW, kH);  // keep aspect; mouse coords map 1:1

    PsfFont font;
    font.init();
    if (!font.ready()) {
        std::printf("font init failed\n");
        return 1;
    }
    Theme t    = material_light();
    bool  dark = false;  // P5-b: toggle light/dark at runtime (press T)

    VBox root;
    root.set_rect(0, 0, kW, kH);
    root.set_bg(t.background);
    root.set_padding(16);
    root.set_spacing(8);

    Label title;
    title.set_text("Cinux-GUI");
    title.set_color(t.on_surface);
    Label vol_label;
    vol_label.set_text("Volume");
    vol_label.set_color(t.on_surface);

    HBox actions;
    actions.set_spacing(8);
    Button bnew;
    bnew.set_text("New");
    bnew.set_theme(&t);
    Button bopen;
    bopen.set_text("Open");
    bopen.set_theme(&t);
    Button bsave;
    bsave.set_text("Save");
    bsave.set_theme(&t);
    actions.add_child(&bnew);
    actions.add_child(&bopen);
    actions.add_child(&bsave);

    Slider vol;
    vol.set_range(100);
    vol.set_value(60);
    vol.set_theme(&t);

    HBox footer;
    footer.set_spacing(8);
    Button ok;
    ok.set_text("OK");
    ok.set_theme(&t);
    Button cancel;
    cancel.set_text("Cancel");
    cancel.set_theme(&t);
    footer.add_child(&ok);
    footer.add_child(&cancel);

    root.add_child(&title);
    root.add_child(&actions);
    root.add_child(&vol_label);
    root.add_child(&vol);
    root.add_child(&footer);

    Desktop desktop;
    desktop.set_root(&root);

    uint8_t* buf = new uint8_t[kW * kH * 4u];
    Surface  staging{buf, kW, kH, kW * 4u, PixelFormat::kXrgb8888};

    std::printf(
        "sdl-host: running -- click buttons, drag the slider. "
        "Close window or Ctrl-C to exit.\n");
    std::fflush(stdout);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                running = false;
            } else if (e.type == SDL_MOUSEMOTION) {
                PointerPayload p{};
                p.kind    = kPointerKindMove;
                p.x       = e.motion.x;
                p.y       = e.motion.y;
                p.buttons = (e.motion.state & SDL_BUTTON_LMASK) ? 1u : 0u;
                desktop.dispatch_pointer(p);
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                PointerPayload p{};
                p.kind    = kPointerKindDown;
                p.x       = e.button.x;
                p.y       = e.button.y;
                p.buttons = 1u;
                desktop.dispatch_pointer(p);
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                PointerPayload p{};
                p.kind    = kPointerKindUp;
                p.x       = e.button.x;
                p.y       = e.button.y;
                p.buttons = 0u;
                desktop.dispatch_pointer(p);
            } else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_t) {
                // P5-b: runtime light/dark toggle; Desktop::render repaints next frame.
                dark = !dark;
                t    = dark ? material_dark() : material_light();
                title.set_color(t.on_surface);
                vol_label.set_color(t.on_surface);
                bnew.set_theme(&t);
                bopen.set_theme(&t);
                bsave.set_theme(&t);
                vol.set_theme(&t);
                ok.set_theme(&t);
                cancel.set_theme(&t);
                root.set_bg(t.background);
                root.invalidate();  // P5-c: colours changed -> repaint
            }
        }

        Region dirty;
        desktop.render(staging, font, &dirty);  // widget tree -> staging

        SDL_UpdateTexture(tex, nullptr, buf, static_cast<int>(kW) * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        SDL_Delay(16);  // ~60 fps cap
    }

    delete[] buf;
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
