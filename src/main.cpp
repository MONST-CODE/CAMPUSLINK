#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "campuslink/ui.hpp"

#include <cstdio>
#include <memory>
#include <string_view>

#ifndef CAMPUSLINK_VERSION
#error The build must define CAMPUSLINK_VERSION from VERSION.
#endif
#define CL_STRINGIFY_IMPL(value) #value
#define CL_STRINGIFY(value) CL_STRINGIFY_IMPL(value)

namespace {
constexpr int width = 960;
constexpr int height = 600;

struct SdlLifetime {
    ~SdlLifetime() { SDL_Quit(); }
};

int sdl_error(const char* operation) {
    std::fprintf(stderr, "%s: %s\n", operation, SDL_GetError());
    return 1;
}

int run(bool self_test) {
    const int runtime_version = SDL_GetVersion();
    if (runtime_version != SDL_VERSION) {
        std::fprintf(stderr, "SDL runtime/header mismatch: %d != %d\n", runtime_version, SDL_VERSION);
        return 1;
    }
    if (self_test && !SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE)) {
        return sdl_error("SDL_SetHintWithPriority");
    }
    const SdlLifetime lifetime;
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return sdl_error("SDL_Init(video)");
    }
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window{
        SDL_CreateWindow("CAMPUSLINK " CL_STRINGIFY(CAMPUSLINK_VERSION) " | SDL3 startup", width, height,
                         SDL_WINDOW_RESIZABLE | (self_test ? SDL_WINDOW_HIDDEN : 0)),
        SDL_DestroyWindow};
    if (!window) {
        return sdl_error("SDL_CreateWindow");
    }

    if (self_test) {
        const char* driver = SDL_GetCurrentVideoDriver();
        if (!driver || std::string_view{driver} != "dummy") {
            std::fputs("Self-test requires the offline SDL dummy driver.\n", stderr);
            return 1;
        }
        int actual_width{};
        int actual_height{};
        if (!SDL_GetWindowSize(window.get(), &actual_width, &actual_height)) {
            return sdl_error("SDL_GetWindowSize");
        }
        if (actual_width != width || actual_height != height ||
            (SDL_GetWindowFlags(window.get()) & SDL_WINDOW_HIDDEN) == 0) {
            std::fputs("Unexpected SDL self-test window geometry or visibility.\n", stderr);
            return 1;
        }
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        if (!SDL_PushEvent(&quit)) {
            return sdl_error("SDL_PushEvent(quit)");
        }
    }

    for (;;) {
        SDL_Event event{};
        const bool received = self_test ? SDL_WaitEventTimeout(&event, 1000) : SDL_WaitEvent(&event);
        if (!received) {
            return sdl_error(self_test ? "Self-test quit event missing" : "SDL_WaitEvent");
        }
        if (event.type == SDL_EVENT_QUIT ||
            (!self_test && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             event.window.windowID == SDL_GetWindowID(window.get()))) {
            break;
        }
    }
    // The window is destroyed before the SDL lifetime ends.
    return 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    const bool self_test = argc == 2 && std::string_view{argv[1]} == "--self-test";
    const bool ui_test = argc == 2 && std::string_view{argv[1]} == "--ui-smoke";
    if (argc != 1 && !self_test && !ui_test) {
        std::fputs("Usage: CampusLink [--self-test | --ui-smoke]\n", stderr);
        return 2;
    }
    const int result = self_test ? run(true) : campuslink::ui::run(ui_test);
    if (self_test && result == 0) {
        std::printf("PASS: SDL %d runtime, dummy window and quit event\n", SDL_GetVersion());
    }
    if (ui_test && result == 0) std::puts("PASS: SDL dummy + ImGui software rendering, three frames and quit event");
    return result;
}
