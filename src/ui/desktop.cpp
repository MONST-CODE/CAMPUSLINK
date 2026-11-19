#include "campuslink/ui.hpp"
#include "campuslink/network.hpp"
#include "campuslink/simulation.hpp"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#define CL_TEXT_I(x) #x
#define CL_TEXT(x) CL_TEXT_I(x)

namespace campuslink::ui {
namespace {
using namespace std::chrono_literals;
namespace fs = std::filesystem;
struct Log { long long when; std::string level, module, event; unsigned code{}; };
struct View {
    std::string status{"Ready"};
    net::Snapshot hardware;
    net::Discovery discovery;
    std::optional<net::Scope> scope;
    std::vector<net::Candidate> candidates;
    std::vector<sim::Client> clients;
    std::vector<sim::Sample> samples;
    std::array<unsigned, static_cast<unsigned>(sim::State::count)> counts{};
    std::vector<Log> logs;
    unsigned client_count{}, events{}, simulated_ms{};
    net::Clock::time_point scan_at{}, last_handoff{};
};
class Worker {
    mutable std::mutex lock_;
    View view_;
    std::atomic_bool busy_{false}, paused_{false};
    net::Clock::time_point last_physical_{net::Clock::now() - 1h};
    net::Clock::time_point last_scan_{net::Clock::now() - 1min};
    fs::path folder_;
    std::jthread thread_; // destroyed first; callbacks cannot outlive their state
public:
    explicit Worker(fs::path folder) : folder_(std::move(folder)) {}
    const fs::path& folder() const { return folder_; }
    bool busy() const { return busy_.load(); }
    bool paused() const { return paused_.load(); }
    void pause() { paused_ = !paused_.load(); }
    void stop() { thread_.request_stop(); }
    View copy() const { const std::lock_guard hold(lock_); return view_; }
    void update(const std::function<void(View&)>& f) { const std::lock_guard hold(lock_); f(view_); }
    void log(std::string level, std::string module, std::string event, unsigned code = 0) {
        const auto sec = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        update([&](View& v) {
            if (v.logs.size() >= 128) v.logs.erase(v.logs.begin());
            v.logs.push_back({sec, std::move(level), std::move(module), std::move(event), code});
        });
    }
    void run(const char* name, std::function<void(std::stop_token)> task) {
        if (busy_.exchange(true)) return;
        paused_ = false;
        try {
            thread_ = std::jthread([this, label = std::string(name), task = std::move(task)](std::stop_token token) {
                try {
                    log("INFO", label, "Started");
                    update([&](View& v) { v.status = label + " running"; });
                    task(token);
                    if (token.stop_requested()) update([](View& v) { v.status = "Stopped by operator"; });
                    log("INFO", label, token.stop_requested() ? "Cancelled" : "Finished");
                } catch (const std::exception& e) {
                    update([&](View& v) { v.status = e.what(); });
                    log("ERROR", label, "Operation failed; see status");
                } catch (...) {
                    update([](View& v) { v.status = "Unexpected worker error"; });
                    log("ERROR", label, "Unexpected non-standard exception");
                }
                busy_ = false;
            });
        } catch (const std::exception& e) {
            busy_ = false;
            update([&](View& v) { v.status = e.what(); });
        }
    }
    std::string read(const char* name, std::size_t limit) const {
        if (folder_.empty()) throw std::runtime_error("SDL user data directory unavailable");
        std::ifstream file(folder_ / name, std::ios::binary);
        if (!file) throw std::runtime_error(std::string("Could not read ") + name + " from user data directory");
        std::string text(limit + 1, '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
        if (file.bad() || text.size() > limit) throw std::runtime_error("Input too large or unreadable");
        return text;
    }
    void write(const char* name, std::string_view text) const {
        if (folder_.empty()) throw std::runtime_error("SDL user data directory unavailable");
        std::ofstream file(folder_ / name, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) throw std::runtime_error("Cannot write export to user data directory");
    }
    void inspect() {
        run("Hardware inventory", [this](auto stop) {
            auto data = net::platform().inspect(stop);
            const auto summary = data.interfaces.empty() ? "No interfaces reported" : "Inventory refreshed";
            for (const auto& e : data.errors) log("WARN", "inventory", e.operation, e.code);
            update([&](View& v) { v.hardware = std::move(data); v.status = summary; });
        });
    }
    void scan() {
        run("Windows WLAN discovery", [this](auto stop) {
            if (net::Clock::now() - last_scan_ < 10s) throw std::runtime_error("Discovery cooldown: 10 seconds");
            last_scan_ = net::Clock::now();
            auto data = net::platform().scan(stop);
            for (const auto& e : data.errors) log("WARN", "discovery", e.operation, e.code);
            update([&](View& v) { v.discovery = std::move(data); v.scan_at = net::Clock::now(); v.status = "Discovery completed; review native status and errors"; });
        });
    }
    void load_scope() {
        run("Load authorized scope", [this](auto) {
            update([](View& v) { v.scope.reset(); });
            auto scope = net::parse_scope(read("test_scope.conf", 4096));
            update([&](View& v) { v.scope = std::move(scope); v.status = "Scope loaded; active tests require confirmation"; });
        });
    }
    void handoff(net::Scope scope, net::Bss bss) {
        run("Physical handoff", [this, scope = std::move(scope), bss = std::move(bss)](auto stop) {
            if (net::Clock::now() - last_physical_ < std::chrono::seconds(scope.cooldown_s)) throw std::runtime_error("Physical test cooldown still active");
            last_physical_ = net::Clock::now();
            auto result = net::select(net::platform(), scope, bss, stop);
            if (result.error) log("WARN", "handoff", result.error->operation, result.error->code);
            auto hardware = net::platform().inspect(stop);
            update([&](View& v) {
                if (result.measured) {
                    auto& items = v.candidates;
                    if (items.size() >= 64) items.erase(items.begin());
                    items.push_back(std::move(*result.measured));
                }
                v.hardware = std::move(hardware); v.last_handoff = last_physical_; v.status = result.state;
                if (result.error) v.status += ": " + result.error->detail;
            });
        });
    }
    void lab(sim::Config config, bool replay, bool export_csv) {
        run("SIMULATED Virtual Lab", [this, config, replay, export_csv](auto stop) mutable {
            if (replay) config = sim::parse(read("last-scenario.txt", 128));
            sim::Lab lab(config);
            update([](View& v) { v.clients.clear(); v.counts = {}; v.events = v.client_count = v.simulated_ms = 0; });
            const auto publish = [&] {
                decltype(View::counts) counts{};
                for (const auto& c : lab.clients()) ++counts[static_cast<unsigned>(c.state)];
                std::vector<sim::Client> preview(lab.clients().begin(), lab.clients().begin() + std::min<std::size_t>(120, lab.clients().size()));
                update([&](View& v) { v.clients = std::move(preview); v.counts = counts; v.events = lab.events(); v.simulated_ms = lab.now(); v.client_count = config.clients; });
            };
            auto last = net::Clock::now() - 1s;
            while (!lab.done() && !stop.stop_requested()) {
                if (!paused()) lab.step(128, stop);
                if (net::Clock::now() - last >= 80ms) { publish(); last = net::Clock::now(); }
                std::this_thread::sleep_for(paused() ? 25ms : 3ms);
            }
            if (stop.stop_requested()) lab.cancel();
            publish();
            if (export_csv && !stop.stop_requested()) {
                write("last-scenario.txt", sim::serialize(config));
                write("virtual-lab.csv", lab.csv());
            }
            update([&](View& v) { v.status = stop.stop_requested() ? "SIMULATED run cancelled" : "SIMULATED run complete"; });
        });
    }
    void profile(sim::Budget budget) {
        run("SIMULATED capacity benchmark", [this, budget](auto stop) {
            auto samples = sim::profile(budget, stop);
            update([&](View& v) { v.samples = std::move(samples); v.status = "Local simulation capacity benchmark completed"; });
        });
    }
    void export_logs() {
        run("Export logs", [this](auto) {
            std::string text{"utc_epoch_seconds\tlevel\tmodule\tevent\tnative_code\n"};
            for (const auto& l : copy().logs) text += std::to_string(l.when) + '\t' + l.level + '\t' + l.module + '\t' + l.event + '\t' + std::to_string(l.code) + '\n';
            write("events.tsv", text);
            update([](View& v) { v.status = "events.tsv written to user data directory"; });
        });
    }
};
struct Display {
    SDL_Window* window;
    SDL_GPUDevice* gpu{};
    SDL_Renderer* renderer{};
    bool claimed{}, platform{}, backend{};
    std::string mode, warning;
    explicit Display(SDL_Window* w) : window(w) {}
    ~Display() { shutdown(); }
    void shutdown() {
        if (gpu) {
            SDL_WaitForGPUIdle(gpu);
            if (backend) ImGui_ImplSDLGPU3_Shutdown();
        } else if (backend) ImGui_ImplSDLRenderer3_Shutdown();
        if (platform) ImGui_ImplSDL3_Shutdown();
        if (claimed) SDL_ReleaseWindowFromGPUDevice(gpu, window);
        if (gpu) SDL_DestroyGPUDevice(gpu);
        if (renderer) SDL_DestroyRenderer(renderer);
        gpu = nullptr; renderer = nullptr; claimed = platform = backend = false;
    }
    bool setup(bool software) {
        if (!software) {
            gpu = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, "vulkan");
            if (gpu) claimed = SDL_ClaimWindowForGPUDevice(gpu, window);
            if (claimed) platform = ImGui_ImplSDL3_InitForSDLGPU(window);
            if (platform) {
                ImGui_ImplSDLGPU3_InitInfo init{};
                init.Device = gpu;
                init.ColorTargetFormat = SDL_GetGPUSwapchainTextureFormat(gpu, window);
                backend = ImGui_ImplSDLGPU3_Init(&init);
            }
            if (backend) { mode = "Vulkan / SDL GPU"; return true; }
            warning = "Vulkan unavailable; switched to SDL software renderer";
            shutdown();
        }
        renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) return false;
        platform = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        if (platform) backend = ImGui_ImplSDLRenderer3_Init(renderer);
        mode = "SDL software renderer";
        return backend;
    }
    void frame() {
        if (gpu) ImGui_ImplSDLGPU3_NewFrame(); else ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
    }
    bool draw() {
        ImGui::Render();
        if (ImGui::GetDrawData()->DisplaySize.x <= 0 || ImGui::GetDrawData()->DisplaySize.y <= 0) return true;
        if (renderer) {
            const auto scale = ImGui::GetDrawData()->FramebufferScale;
            return SDL_SetRenderScale(renderer, scale.x, scale.y) && SDL_SetRenderDrawColor(renderer, 16, 22, 29, 255) &&
                SDL_RenderClear(renderer) && (ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer), SDL_RenderPresent(renderer));
        }
        auto* commands = SDL_AcquireGPUCommandBuffer(gpu);
        if (!commands) return false;
        SDL_GPUTexture* texture{};
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &texture, nullptr, nullptr)) { SDL_CancelGPUCommandBuffer(commands); return false; }
        if (texture) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
            SDL_GPUColorTargetInfo target{};
            target.texture = texture; target.clear_color = {0.06f, 0.08f, 0.11f, 1.f};
            target.load_op = SDL_GPU_LOADOP_CLEAR; target.store_op = SDL_GPU_STOREOP_STORE;
            auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            if (!pass) { SDL_SubmitGPUCommandBuffer(commands); return false; }
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commands, pass);
            SDL_EndGPURenderPass(pass);
        }
        return SDL_SubmitGPUCommandBuffer(commands);
    }
};
constexpr ImVec4 cyan{0.38f, 0.83f, 0.90f, 1.f}, amber{0.98f, 0.73f, 0.37f, 1.f}, muted{0.59f, 0.68f, 0.76f, 1.f};
void text(std::string_view s) { ImGui::TextUnformatted(s.data(), s.data() + s.size()); }
void paragraph(const std::string& s) { ImGui::PushTextWrapPos(0); text(s); ImGui::PopTextWrapPos(); }
void errors(const std::vector<net::Error>& list) {
    for (const auto& e : list) {
        ImGui::TextColored(amber, "%s [%u]", e.operation.c_str(), e.code);
        paragraph(e.detail);
    }
}
void field(const char* name, const std::string& v) { ImGui::Text("%s: %s", name, v.empty() ? "UNKNOWN" : v.c_str()); }
void metric(const char* name, std::optional<double> v, const char* unit) {
    if (v) ImGui::Text("%-18s %8.2f %s", name, *v, unit); else ImGui::Text("%-18s UNVERIFIED", name);
}
void number_input(const char* label, unsigned& v) { ImGui::InputScalar(label, ImGuiDataType_U32, &v); }
struct Screen {
    int page{}, selected_bss{-1};
    bool export_csv{}, consent{}, light{}, warnings_only{};
    sim::Config config;
    sim::Budget budget;
    std::array<double,5> weights{net::base_weights};
    net::Clock::time_point refresh{};
    View view;
    void draw(Worker& worker, const Display& display) {
        if (net::Clock::now() >= refresh) { view = worker.copy(); refresh = net::Clock::now() + 80ms; }
        const auto* active = [&]() -> const net::Interface* {
            for (const auto& i : view.hardware.interfaces) if (i.associated) return &i;
            for (const auto& i : view.hardware.interfaces) if (i.wireless) return &i;
            return nullptr;
        }();
        const auto now = net::Clock::now();
        const auto* latest = view.candidates.empty() ? nullptr : &view.candidates.back();
        const net::Candidate* current{};
        if (active) for (auto it = view.candidates.rbegin(); it != view.candidates.rend(); ++it) {
            if (active->id == it->bss.interface_id && active->bssid == it->bss.bssid && now - it->observed < 5min) { current = &*it; break; }
        }
        const auto* chosen = selected_bss >= 0 && static_cast<std::size_t>(selected_bss) < view.discovery.bss.size() ? &view.discovery.bss[selected_bss] : nullptr;
        const bool fresh_scan = view.scan_at != net::Clock::time_point{} && now - view.scan_at < 2min;
        const auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("CAMPUSLINK", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        constexpr std::array pages{"Overview", "Hardware Lab", "Radio Capabilities", "Virtual Client Lab", "Network Discovery", "Experiment Center",
            "Performance Lab", "Intelligence Engine", "Connection Manager", "Logs & Diagnostics", "Settings"};
        ImGui::BeginChild("Navigation", {218, 0}, ImGuiChildFlags_Borders);
        ImGui::TextColored(cyan, "CAMPUSLINK"); ImGui::TextColored(muted, "NETWORK RESEARCH / " CL_TEXT(CAMPUSLINK_VERSION));
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        for (unsigned i = 0; i < pages.size(); ++i) if (ImGui::Selectable(pages[i], page == static_cast<int>(i), 0, {0, 29})) page = static_cast<int>(i);
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextColored(worker.busy() ? amber : muted, "%s", worker.busy() ? "WORKER ACTIVE" : "WORKER IDLE");
        ImGui::BeginDisabled(!worker.busy()); if (ImGui::Button("Stop task", {-1, 33})) worker.stop(); ImGui::EndDisabled();
        ImGui::EndChild(); ImGui::SameLine();
        ImGui::BeginChild("Panel", {0, 0}, ImGuiChildFlags_Borders);
        ImGui::TextColored(cyan, "%s", pages[page]);
        ImGui::TextColored(page == 3 ? amber : muted, "%s", page == 3 ? "SIMULATED / OFFLINE / NO RADIO CLIENTS" : "OPERATOR CONTROL / WINDOWS FIRST");
        ImGui::Spacing(); paragraph(view.status); ImGui::Separator(); ImGui::Spacing();
        switch (page) {
        case 0:
            field("Renderer", display.mode); if (!display.warning.empty()) paragraph(display.warning);
            field("Active wireless interface", active ? active->description : "");
            field("Associated SSID", active ? active->ssid : "");
            field("Authorized application endpoint", current && current->observation.endpoint_verified ? "Verified" : "");
            metric("Connection score", current && current->rating.coverage ? std::optional<double>(current->rating.points) : std::nullopt, "/ 100");
            ImGui::Text("Interfaces: %zu   BSS: %zu   Offline events: %u", view.hardware.interfaces.size(), view.discovery.bss.size(), view.events);
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Refresh hardware")) worker.inspect(); ImGui::EndDisabled();
            errors(view.hardware.errors); break;
        case 1:
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Refresh hardware")) worker.inspect(); ImGui::EndDisabled();
            errors(view.hardware.errors);
            for (const auto& i : view.hardware.interfaces) {
                ImGui::PushID(i.id.c_str());
                if (ImGui::CollapsingHeader(i.name.empty() ? i.description.c_str() : i.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    field("Description", i.description); field("Interface ID", i.id); field("Adapter class", i.kind); field("PnP hardware ID", i.hardware_id); field("Manufacturer", i.manufacturer);
                    field("Driver provider", i.provider); field("Driver version", i.driver); field("Driver date", i.driver_date);
                    field("IPv4", i.address); field("Gateway", i.gateway); field("DNS configuration", i.dns);
                    ImGui::Text("Operational %s | Associated %s | DHCP %s", i.up ? "yes" : "no", i.associated ? "yes" : "unconfirmed", i.dhcp ? "reported" : "not reported");
                }
                ImGui::PopID();
            } break;
        case 2: {
            unsigned physical{};
            for (const auto& i : view.hardware.interfaces) {
                if (i.kind == "Physical (reported)") ++physical;
                if (!i.wireless) continue;
                ImGui::SeparatorText(i.description.c_str()); field("PHY (DRIVER_REPORTED)", i.phy); field("Current BSSID", i.bssid); field("Driver-reported radio state", i.radio);
                metric("RSSI", i.rssi ? std::optional<double>(*i.rssi) : std::nullopt, "dBm");
                ImGui::Text("Link rates reported: RX %.1f / TX %.1f Mb/s", i.rx_mbps, i.tx_mbps);
            }
            ImGui::Text("Physical interface count (driver-reported classification): %u", physical);
            ImGui::TextColored(amber, "PHYSICAL_WIRELESS_CONCURRENCY : UNKNOWN");
            paragraph("STA association is observable. STA + secondary STA and STA + Wi-Fi Direct role combinations are not confirmed. PHY, streams and link rates do not form a concurrency ceiling.");
            break;
        }
        case 3: {
            ImGui::BeginDisabled(worker.busy());
            number_input("Logical clients", config.clients); number_input("Lease slots", config.pool); number_input("Lease duration (simulated ms)", config.lease_ms); number_input("Seed", config.seed);
            if (ImGui::BeginCombo("Scenario", sim::name(config.scenario).data())) {
                for (unsigned x = 0; x < static_cast<unsigned>(sim::Scenario::count); ++x) {
                    const auto option = static_cast<sim::Scenario>(x);
                    if (ImGui::Selectable(sim::name(option).data(), option == config.scenario)) config.scenario = option;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox("Record scenario and CSV history", &export_csv);
            ImGui::BeginDisabled(!config.valid()); if (ImGui::Button("Start offline lab")) worker.lab(config, false, export_csv); ImGui::EndDisabled();
            ImGui::SameLine(); if (ImGui::Button("Replay last saved seed")) worker.lab(config, true, export_csv);
            ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::BeginDisabled(!worker.busy() || !view.status.starts_with("SIMULATED Virtual Lab"));
            if (ImGui::Button(worker.paused() ? "Resume" : "Pause")) worker.pause();
            ImGui::EndDisabled();
            ImGui::Text("%u clients | %u transitions | simulated time %u ms", view.client_count, view.events, view.simulated_ms);
            for (unsigned j = 0; j < view.counts.size(); ++j) if (view.counts[j]) { ImGui::Text("%s: %u", sim::name(static_cast<sim::State>(j)).data(), view.counts[j]); }
            if (ImGui::BeginTable("clients", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                for (const auto* h : {"Logical ID", "State", "Lease count", "Latency (sim. ms)", "Loss (sim. %)"}) ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                for (const auto& c : view.clients) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%u", c.id); ImGui::TableNextColumn(); text(sim::name(c.state));
                    ImGui::TableNextColumn(); ImGui::Text("%u", c.leases); ImGui::TableNextColumn(); ImGui::Text("%.1f", c.metrics.latency_ms);
                    ImGui::TableNextColumn(); ImGui::Text("%.1f", c.metrics.loss_percent);
                }
                ImGui::EndTable();
            }
            break;
        }
        case 4:
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Scan Windows WLAN")) { selected_bss = -1; consent = false; worker.scan(); } ImGui::EndDisabled();
            paragraph("Explicit scan. No connection is attempted. Select a BSSID to open Connection Manager.");
            if (!fresh_scan && !view.discovery.bss.empty()) ImGui::TextColored(amber, "Scan expired. Refresh before connecting.");
            errors(view.discovery.errors);
            if (ImGui::BeginTable("bss", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                for (const auto* h : {"SSID", "BSSID", "Signal", "MHz / channel", "Security evidence", "Windows profile"}) ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                ImGuiListClipper clip; clip.Begin(static_cast<int>(view.discovery.bss.size()));
                while (clip.Step()) for (auto j = clip.DisplayStart; j < clip.DisplayEnd; ++j) {
                    const auto& b = view.discovery.bss[j];
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(j);
                    if (ImGui::Selectable(b.ssid.empty() ? "(hidden)" : b.ssid.c_str(), selected_bss == j)) { selected_bss = j; consent = false; page = 8; }
                    ImGui::PopID(); ImGui::TableNextColumn(); text(b.bssid); ImGui::TableNextColumn(); ImGui::Text("%d dBm", b.rssi);
                    ImGui::TableNextColumn(); ImGui::Text("%u / %u", b.mhz, b.channel); ImGui::TableNextColumn(); text(b.security); ImGui::TableNextColumn(); text(b.profile);
                }
                ImGui::EndTable();
            } break;
        case 5:
            paragraph("Physical tests are disabled until a valid one-BSSID test_scope.conf is loaded from user data. Use an operator-authorized AP and a controlled CLP1 IPv4 server only.");
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Load test_scope.conf")) { consent = false; worker.load_scope(); } ImGui::EndDisabled();
            if (view.scope) {
                field("Profile", view.scope->profile); field("BSSID", view.scope->bssid); field("IPv4 endpoint", view.scope->endpoint); field("Optional controlled DNS name", view.scope->dns_name);
                ImGui::Text("Port %u | %u ICMP attempts | %u bytes each direction | %u ms transfer timeout | %u s cooldown",
                    view.scope->port, view.scope->repetitions, view.scope->bytes, view.scope->timeout_ms, view.scope->cooldown_s);
            } else ImGui::TextColored(amber, "NO ACTIVE TEST SCOPE");
            if (ImGui::Button("Open Virtual Client Lab")) page = 3;
            break;
        case 6:
            if (latest) {
                const auto& o = latest->observation;
                ImGui::Text("Last physical endpoint experiment | Replies %u / %u | CLP1 payload %u bytes", o.replies, o.attempts, o.bytes);
                metric("Download", o.down_mbps, "Mb/s"); metric("Upload", o.up_mbps, "Mb/s"); metric("Mean RTT", o.latency_ms, "ms");
                metric("Mean abs jitter", o.jitter_ms, "ms"); metric("ICMP misses", o.loss_percent, "%"); metric("DNS", o.dns_ms, "ms");
                if (!o.latency_history.empty()) ImGui::PlotLines("ICMP reply RTT (ms)", o.latency_history.data(), static_cast<int>(o.latency_history.size()), 0, nullptr, 0, 350, {0, 100});
                if (o.error) errors({*o.error});
            } else ImGui::TextColored(muted, "No authorized endpoint observations yet.");
            break;
        case 7: {
            constexpr std::array labels{"Reachability", "Downstream", "RTT", "ICMP misses", "Jitter"};
            for (unsigned x = 0; x < labels.size(); ++x) ImGui::InputDouble(labels[x], &weights[x], 1., 5., "%.0f");
            auto ranked = view.candidates;
            try { for (auto& c : ranked) c.rating = net::score(c.observation, weights); }
            catch (const std::invalid_argument& e) { paragraph(e.what()); break; }
            for (const auto& c : net::rank(std::move(ranked), net::Clock::now())) {
                ImGui::SeparatorText(c.bss.bssid.c_str());
                ImGui::Text("%.1f / 100 | %.0f%% coverage | %u samples | %s", c.rating.points, c.rating.coverage * 100., c.rating.samples, c.rating.eligible ? "ELIGIBLE" : "LOW CONFIDENCE / STALE");
                for (unsigned x = 0; x < labels.size(); ++x) metric(labels[x], c.rating.components[x], "/ 100");
            }
            paragraph("Scoring v1 redistributes missing weights, requires coverage and a verified endpoint. Assisted selection is scoped and operator-triggered.");
            break;
        }
        case 8: {
            field("Current Windows profile", active ? active->profile : ""); field("Selected BSSID", chosen ? chosen->bssid : "");
            field("Selected Windows profile", chosen ? chosen->profile : "");
            paragraph("Windows obtains its own IP. Confirmation covers a possible local disconnect and, if enabled, one rollback to the previous profile.");
            ImGui::Checkbox("I authorize this scope and possible local connection interruption", &consent);
            ImGui::BeginDisabled(worker.busy() || !consent || !fresh_scan || !chosen || !view.scope || !view.scope->allows(*chosen));
            if (ImGui::Button("Connect and validate selected profile")) { worker.handoff(*view.scope, *chosen); consent = false; }
            ImGui::EndDisabled();
            std::optional<net::Candidate> recommended;
            if (current && view.scope && fresh_scan) {
                auto baseline = *current;
                auto measured = view.candidates;
                try {
                    baseline.rating = net::score(baseline.observation, weights);
                    for (auto& c : measured) c.rating = net::score(c.observation, weights);
                    for (const auto& c : net::rank(std::move(measured), now)) {
                        const auto present = [&](const net::Bss& b) { return b.interface_id == c.bss.interface_id && b.bssid == c.bss.bssid && b.profile == c.bss.profile; };
                        if (view.scope->allows(c.bss) && std::any_of(view.discovery.bss.begin(), view.discovery.bss.end(), present) &&
                            net::should_switch(baseline, c, now, view.last_handoff)) { recommended = c; break; }
                    }
                } catch (const std::invalid_argument&) { paragraph("Weights are invalid; assisted selection is unavailable."); }
            }
            ImGui::BeginDisabled(worker.busy() || !consent || !recommended);
            if (ImGui::Button("Apply best measured improvement")) { worker.handoff(*view.scope, recommended->bss); consent = false; }
            ImGui::EndDisabled();
            paragraph("Assisted selection requires a fresh allowed BSSID, two trusted endpoint observations and a 10-point improvement after cooldown. No background switching.");
            break;
        }
        case 9:
            for (const auto& d : net::diagnose(active, !view.discovery.bss.empty(), current ? &current->observation : nullptr)) {
                ImGui::Text("%-17s %s", d.step.c_str(), d.status.c_str()); paragraph(d.detail);
            }
            ImGui::SeparatorText("Recent worker events"); ImGui::Checkbox("Warnings and errors only", &warnings_only);
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Export events.tsv")) worker.export_logs(); ImGui::EndDisabled();
            for (auto it = view.logs.rbegin(); it != view.logs.rend(); ++it) {
                if (warnings_only && it->level == "INFO") continue;
                ImGui::Text("%lld | %s | %s | %s | %u", it->when, it->level.c_str(), it->module.c_str(), it->event.c_str(), it->code);
            } break;
        case 10:
            if (ImGui::Checkbox("Light theme", &light)) { if (light) ImGui::StyleColorsLight(); else ImGui::StyleColorsDark(); }
            field("User data directory", worker.folder().string());
            number_input("Maximum logical clients", budget.max_clients); number_input("Conservative budget (MiB)", budget.memory_mib);
            number_input("Wall time budget (ms)", budget.wall_ms); number_input("Process CPU budget (ms)", budget.cpu_ms);
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button("Measure local simulation capacity")) worker.profile(budget); ImGui::EndDisabled();
            ImGui::SeparatorText("LOGICAL_SIMULATION_CAPACITY (not radio concurrency)");
            for (const auto& s : view.samples) ImGui::Text("%6u | %7u events | %7.2f ms wall / %7.2f ms CPU | p95 %.1f us | %llu B accounted | %s",
                s.clients, s.events, s.elapsed_ms, s.cpu_ms, s.p95_us, static_cast<unsigned long long>(s.accounted_bytes), s.within_budget ? "within budget" : "stopped");
            break;
        }
        ImGui::EndChild(); ImGui::End();
    }
};
}
int run(bool smoke) {
    if (SDL_GetVersion() != SDL_VERSION) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL runtime/header mismatch"); return 1; }
    if (smoke && !SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE)) return 1;
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init(video): %s", SDL_GetError()); return 1; }
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("CAMPUSLINK " CL_TEXT(CAMPUSLINK_VERSION), 1280, 780,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (smoke ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
    if (!window) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(window.get(), 800, 520);
    ImGui::CreateContext();
    struct Context { ~Context() { ImGui::DestroyContext(); } } context;
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark(); auto& style = ImGui::GetStyle();
    style.WindowPadding = {14, 14}; style.FramePadding = {9, 6}; style.ItemSpacing = {10, 7}; style.FrameRounding = 4;
    style.FontScaleDpi = std::max(1.f, SDL_GetWindowDisplayScale(window.get()));
    style.Colors[ImGuiCol_WindowBg] = {0.065f, 0.084f, 0.105f, 1.f};
    style.Colors[ImGuiCol_Header] = {0.14f, 0.27f, 0.34f, 1.f};
    Display display(window.get());
    if (!display.setup(smoke)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer: %s", SDL_GetError()); return 1; }
    const std::unique_ptr<char, decltype(&SDL_free)> folder(smoke ? nullptr : SDL_GetPrefPath("CAMPUSLINK", "CampusLink"), SDL_free);
    Worker worker(folder ? fs::path(std::u8string(reinterpret_cast<const char8_t*>(folder.get()))) : fs::path{});
    Screen screen;
    if (!smoke) worker.inspect();
    bool running = true;
    unsigned frames = 0;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get()))) running = false;
            if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) style.FontScaleDpi = std::max(1.f, SDL_GetWindowDisplayScale(window.get()));
        }
        if (!running) break;
        display.frame(); screen.draw(worker, display);
        if (!display.draw()) {
            if (!display.gpu) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rendering failed: %s", SDL_GetError()); return 1; }
            display.shutdown(); display.warning = "Vulkan rendering failed; using SDL software fallback";
            if (!display.setup(true)) return 1;
        }
        if (smoke && ++frames == 3) { SDL_Event close{}; close.type = SDL_EVENT_QUIT; if (!SDL_PushEvent(&close)) return 1; }
        if (smoke && frames > 3) return 1;
        if (!display.gpu) SDL_Delay(16);
    }
    worker.stop();
    if (smoke && frames != 3) return 1;
    return 0;
}
}
