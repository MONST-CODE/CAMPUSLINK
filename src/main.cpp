#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "campuslink/ui.hpp"
#include "campuslink/network.hpp"
#include "campuslink/simulation.hpp"

#include <cstdio>
#include <charconv>
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
    const bool graphics = argc == 2 && std::string_view{argv[1]} == "--graphics-smoke";
    const bool ui_test = argc == 2 && std::string_view{argv[1]} == "--ui-smoke";
    if (argc == 2 && std::string_view{argv[1]} == "--profile") {
        try {
            const campuslink::sim::Budget budget;
            const auto profile = campuslink::sim::profile(budget);
            std::printf("LOGICAL_SIMULATION_CAPACITY | limits: %u clients, %u MiB RSS growth budget, %u ms wall, %u ms process CPU\n",
                budget.max_clients, budget.memory_mib, budget.wall_ms, budget.cpu_ms);
            std::puts("clients,transitions,events_per_second,wall_ms,cpu_ms,batch_p95_us,baseline_rss,sampled_peak_rss,sampled_growth,accounted_bytes,status");
            const auto capacity = profile.completed_clients();
            for (const auto& s : profile.samples) {
                std::printf("%u,%u,%.1f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%llu,%s\n", s.clients, s.events,
                    s.elapsed_ms > 0 ? 1000 * s.events / s.elapsed_ms : 0, s.elapsed_ms, s.cpu_ms, s.p95_us,
                    static_cast<unsigned long long>(s.baseline_rss), static_cast<unsigned long long>(s.sampled_peak_rss),
                    static_cast<unsigned long long>(s.sampled_growth), static_cast<unsigned long long>(s.accounted_bytes), s.within_budget ? "PASS" : "BUDGET_STOP");
            }
            std::printf("maximum_completed_within_budgets=%u (offline workload, not radio capacity)\n", capacity);
            std::printf("profile_stop=%s\n", campuslink::sim::name(profile.end).data());
            return capacity ? 0 : 1;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Profiler: %s\n", e.what()); return 1;
        }
    }
    if (argc == 3 && std::string_view{argv[1]} == "--clp1-loopback") {
        const std::string_view text(argv[2]);
        unsigned port{};
        const auto converted = std::from_chars(text.data(), text.data() + text.size(), port);
        if (converted.ec != std::errc{} || converted.ptr != text.data() + text.size() || !port || port > 65535) return 2;
        if (const auto result = campuslink::net::loopback_protocol(port)) {
            std::fprintf(stderr, "%s: %u (%s)\n", result->operation.c_str(), result->code, result->detail.c_str()); return 1;
        }
        std::puts("PASS: native Winsock CLP1 client round trips for 1, 16 and 64 KiB"); return 0;
    }
    if (argc == 2 && std::string_view{argv[1]} == "--inventory-smoke") {
#ifdef _WIN32
        const auto snapshot = campuslink::net::platform().inspect({});
        std::printf("Native inventory: %zu interfaces, %zu diagnostics\n", snapshot.interfaces.size(), snapshot.errors.size());
        for (const auto& e : snapshot.errors) std::printf("%s: %u\n", e.operation.c_str(), e.code);
        return snapshot.interfaces.empty() ? 1 : 0;
#else
        std::fputs("Physical inventory is not available on this platform\n", stderr);
        return 3;
#endif
    }
    if (argc != 1 && !self_test && !ui_test && !graphics) {
        std::fputs("Usage: CampusLink [--self-test | --ui-smoke | --inventory-smoke | --profile | --graphics-smoke | --clp1-loopback PORT]\n", stderr);
        return 2;
    }
    const int result = self_test ? run(true) : campuslink::ui::run(ui_test || graphics, graphics);
    if (self_test && result == 0) {
        std::printf("PASS: SDL %d runtime, dummy window and quit event\n", SDL_GetVersion());
    }
    if (ui_test && result == 0) std::puts("PASS: SDL dummy + ImGui software rendering, all eleven panels and quit event");
    if (graphics && result == 0) std::puts("PASS: native SDL Vulkan, all eleven panels and quit event");
    return result;
}
