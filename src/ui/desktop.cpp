#include "campuslink/ui.hpp"
#include "campuslink/build_info.hpp"
#include "campuslink/path_lab.hpp"
#include "campuslink/network.hpp"
#include "campuslink/simulation.hpp"
#include "campuslink/ui_text.hpp"
#include "appearance.hpp"
#include "widgets.hpp"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#define CL_TEXT_I(x) #x
#define CL_TEXT(x) CL_TEXT_I(x)

namespace campuslink::ui {
namespace {
using namespace std::chrono_literals;
namespace w = widget;
thread_local Language language{Language::fr};
thread_local Theme color_theme{Theme::light};
const char* tr(Key key) { return translate(language, key); }
std::string message(std::string_view value) { return localize(language, value); }
std::string format(const char* pattern, ...) {
    char buffer[512];
    va_list args;
    va_start(args, pattern);
    std::vsnprintf(buffer, sizeof buffer, pattern, args);
    va_end(args);
    return buffer;
}
namespace fs = std::filesystem;
struct Log { long long when; std::string level, module, event; unsigned code{}; };
struct View {
    std::string status{english(Key::Ready)};
    net::Snapshot hardware;
    net::Discovery discovery;
    std::vector<std::pair<std::string, net::Reachability>> reach; // Windows report per associated adapter
    std::optional<net::WifiResult> wifi;
    std::optional<net::WifiProgress> wifi_progress;               // engine-confirmed stage while running
    net::WifiMode wifi_mode{net::WifiMode::normal};
    std::vector<net::WifiHistory> wifi_history;
    std::optional<std::array<lab::Result, 3>> path_lab;
    int path_lab_scenario{};
    std::vector<net::Scope> scopes;
    struct Outcome { std::string bssid, status, detail; };
    std::vector<Outcome> outcomes;
    std::vector<net::Candidate> candidates, paths;
    std::optional<net::Error> path_error;
    std::vector<sim::Client> clients;
    std::shared_ptr<const std::vector<sim::Client>> completed_clients;
    std::optional<sim::Profile> capacity;
    std::array<unsigned, static_cast<unsigned>(sim::State::count)> counts{};
    std::vector<Log> logs;
    unsigned client_count{}, events{}, simulated_ms{}, overlapping_validated_windows{};
    net::Clock::time_point scan_at{}, last_handoff{}, inspected_at{}, wifi_ready{}, scan_ready{};
    bool scanning{};
};
constexpr auto wifi_cooldown = 15s;
constexpr auto scan_cooldown = 10s;
std::vector<std::pair<std::string, net::Reachability>> reachability(const net::Snapshot& s) {
    std::vector<std::pair<std::string, net::Reachability>> out;
    for (const auto& i : s.interfaces) if (i.wireless && i.associated) out.emplace_back(i.id, net::platform().reachability(i));
    return out;
}
class Worker {
    mutable std::mutex lock_;
    View view_;
    std::size_t revision_{1};
    std::atomic_bool busy_{false}, paused_{false};
    net::Clock::time_point last_physical_{net::Clock::now() - 1h};
    net::Clock::time_point last_wifi_{net::Clock::now() - 1min};
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
    void copy_if_changed(View& destination, std::size_t& seen) const {
        const std::lock_guard hold(lock_);
        if (seen != revision_) { destination = view_; seen = revision_; }
    }
    void update(const std::function<void(View&)>& f) { const std::lock_guard hold(lock_); ++revision_; f(view_); }
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
                    log("INFO", label, english(Key::Started));
                    update([&](View& v) { v.status = label + english(Key::Running); });
                    task(token);
                    if (token.stop_requested()) update([](View& v) { v.status = english(Key::StoppedByOperator); });
                    log("INFO", label, token.stop_requested() ? english(Key::Cancelled) : english(Key::Finished));
                } catch (const std::exception& e) {
                    update([&](View& v) { v.status = e.what(); });
                    log("ERROR", label, english(Key::OperationFailedSeeStatus));
                } catch (...) {
                    update([](View& v) { v.status = english(Key::UnexpectedWorkerError); });
                    log("ERROR", label, english(Key::UnexpectedNonStandardException));
                }
                update([](View& v) { v.scanning = false; v.wifi_progress.reset(); });
                busy_ = false;
            });
        } catch (const std::exception& e) {
            busy_ = false;
            update([&](View& v) { v.status = e.what(); });
        }
    }
    std::string read(const char* name, std::size_t limit) const {
        if (folder_.empty()) throw std::runtime_error(english(Key::SdlUserDataDirectoryUnavailable));
        std::ifstream file(folder_ / name, std::ios::binary);
        if (!file) throw std::runtime_error(std::string(english(Key::CouldNotRead)) + name + english(Key::FromUserDataDirectory));
        std::string text(limit + 1, '\0');
        file.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(file.gcount()));
        if (file.bad() || text.size() > limit) throw std::runtime_error(english(Key::InputTooLargeOrUnreadable));
        return text;
    }
    void write(const char* name, std::string_view text) const {
        if (folder_.empty()) throw std::runtime_error(english(Key::SdlUserDataDirectoryUnavailable));
        std::ofstream file(folder_ / name, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file) throw std::runtime_error(english(Key::CannotWriteExportToUser));
    }
    void inspect() {
        run(english(Key::HardwareInventory), [this](auto stop) {
            auto data = net::platform().inspect(stop);
            auto reach = reachability(data);
            const auto summary = data.interfaces.empty() ? english(Key::NoInterfacesReported) : english(Key::InventoryRefreshed);
            for (const auto& e : data.errors) log("WARN", "inventory", e.operation, e.code);
            update([&](View& v) {
                v.hardware = std::move(data); v.reach = std::move(reach); v.inspected_at = net::Clock::now(); v.status = summary;
            });
        });
    }
    void scan() {
        run(english(Key::WindowsWlanDiscovery), [this](auto stop) {
            if (net::Clock::now() - last_scan_ < scan_cooldown) throw std::runtime_error(english(Key::DiscoveryCooldown10Seconds));
            last_scan_ = net::Clock::now();
            update([&](View& v) { v.scanning = true; v.scan_ready = last_scan_ + scan_cooldown; });
            auto hardware = net::platform().inspect(stop);
            auto data = net::platform().scan(stop);
            auto reach = reachability(hardware);
            for (const auto& e : data.errors) log("WARN", "discovery", e.operation, e.code);
            update([&](View& v) {
                v.hardware = std::move(hardware); v.discovery = std::move(data); v.reach = std::move(reach);
                v.inspected_at = net::Clock::now();
                v.scan_at = stop.stop_requested() ? net::Clock::time_point{} : net::Clock::now();
                v.status = english(Key::DiscoveryCompletedReviewNativeStatus);
            });
        });
    }
    void wifi(net::Bss selected, net::WifiMode mode) {
        run(english(mode == net::WifiMode::normal ? Key::WifiNormal : Key::WifiAssist), [this, selected = std::move(selected), mode](auto stop) {
            const auto now = net::Clock::now();
            if (now - last_wifi_ < wifi_cooldown) throw std::runtime_error(english(Key::WifiCooldown));
            const auto saved = copy();
            if (saved.scan_at == net::Clock::time_point{} || now - saved.scan_at >= net::wifi_scan_ttl)
                throw std::runtime_error(english(Key::WifiCannotStart));
            last_wifi_ = now;
            update([&](View& v) {
                v.wifi.reset(); v.wifi_mode = mode; v.wifi_ready = now + wifi_cooldown;
                v.wifi_progress = net::WifiProgress{net::WifiStage::preparing, 0, 0, selected.bssid};
            });
            net::WifiPolicy policy{net::wifi_default_attempts, saved.scan_at, saved.wifi_history};
            policy.progress = [this](const net::WifiProgress& p) { update([&](View& v) { v.wifi_progress = p; }); };
            auto result = net::connect_wifi(net::platform(), selected, saved.discovery, mode, stop, policy);
            for (const auto& attempt : result.attempts) if (attempt.error) log("WARN", "handoff", attempt.error->operation, attempt.error->code);
            auto hardware = net::platform().inspect({});
            auto reach = reachability(hardware);
            update([&](View& v) {
                // Only association-level outcomes describe an access point; address and
                // authentication problems are network-wide and must not demote a BSSID.
                if (mode == net::WifiMode::assisted && result.status != net::WifiStatus::cancelled &&
                    result.status != net::WifiStatus::request_unsettled && result.status != net::WifiStatus::recovery_failed && !stop.stop_requested()) {
                    for (const auto& a : result.attempts) {
                        if (a.error && !net::retry_elsewhere(a.error->wifi_failure)) continue;
                        if (v.wifi_history.size() >= 64) v.wifi_history.erase(v.wifi_history.begin());
                        v.wifi_history.push_back({a.candidate, result.finished, !a.error});
                    }
                }
                v.wifi = std::move(result); v.hardware = std::move(hardware); v.reach = std::move(reach);
                v.inspected_at = net::Clock::now(); v.wifi_progress.reset();
                v.status = english(Key::WifiResults);
            });
        });
    }
    void clear_wifi_history() { update([](View& v) { v.wifi_history.clear(); }); }
    void run_path_lab(int scenario) {
        run(english(Key::PathLabRun), [this, scenario](auto stop) {
            auto model = lab::Scenario{{lab::Path{23, 1900, 0, 5}, lab::Path{110, 1450, 0, 20}}, 0, 128};
            if (scenario == 1) model.shared_kib_per_second = 1500;
            if (scenario == 2) { model.paths[0].loss_every = 7; model.paths[0].outage_start_ms = 220; model.paths[0].outage_end_ms = 1350; }
            std::array<lab::Result, 3> results{};
            constexpr std::array policies{lab::Scheduler::best_single, lab::Scheduler::round_robin, lab::Scheduler::earliest_finish};
            for (std::size_t i = 0; i < results.size(); ++i) {
                if (stop.stop_requested()) return;
                results[i] = lab::compare(model, policies[i]);
            }
            update([&](View& v) { v.path_lab = results; v.path_lab_scenario = scenario; v.status = english(Key::PathSimulated); });
        });
    }
    void measure_path(std::string id, net::Scope scope) {
        run(english(Key::PathMeasure), [this, id = std::move(id), scope = std::move(scope)](auto stop) {
            const auto gap = std::chrono::seconds(scope.cooldown_s);
            if (net::Clock::now() - last_physical_ < gap) throw std::runtime_error(english(Key::PhysicalBatchCooldownStillActive));
            last_physical_ = net::Clock::now();
            auto result = net::observe_path(net::platform(), scope, id, stop);
            last_physical_ = net::Clock::now();
            update([&](View& v) {
                v.path_error = std::move(result.error);
                if (result.measured) {
                    if (v.paths.size() >= 16) v.paths.erase(v.paths.begin());
                    v.paths.push_back(std::move(*result.measured));
                }
                v.status = std::move(result.state);
            });
        });
    }
    void load_scope() {
        run(english(Key::LoadAuthorizedScope), [this](auto) {
            update([](View& v) { v.scopes.clear(); });
            auto scopes = net::parse_scopes(read("test_scope.conf", 4096 * 4));
            update([&](View& v) { v.scopes = std::move(scopes); v.status = english(Key::ScopesLoadedActiveTestsRequire); });
        });
    }
    void experiments(std::vector<net::Experiment> requested) {
        run(english(Key::ScopedPhysicalExperiments), [this, requested = std::move(requested)](auto stop) {
            unsigned cooldown{};
            for (const auto& request : requested) cooldown = std::max(cooldown, request.scope.cooldown_s);
            if (net::Clock::now() - last_physical_ < std::chrono::seconds(cooldown)) throw std::runtime_error(english(Key::PhysicalBatchCooldownStillActive));
            auto hardware = net::platform().inspect(stop);
            const auto waves = net::experiment_waves(requested, hardware);
            update([](View& v) { v.outcomes.clear(); });
            for (const auto& wave : waves) {
                if (stop.stop_requested()) break;
                const auto prior = net::Clock::now() + (wave.front() ? 1100ms : 0ms);
                while (!stop.stop_requested() && net::Clock::now() < prior) std::this_thread::sleep_for(25ms);
                if (stop.stop_requested()) break;
                last_physical_ = net::Clock::now();
                update([&](View& v) { v.status = std::string(english(Key::PhysicalExperiment)) + std::to_string(wave.front() + 1) + " / " +
                    std::to_string(requested.size()) + (wave.size() > 1 ? english(Key::TwoIndependentAdapters) : english(Key::Serialized)); });
                std::vector<std::future<net::Handoff>> jobs;
                for (const auto id : wave) jobs.push_back(std::async(std::launch::async, [&requested, id, stop] {
                    const auto& experiment = requested[id];
                    return net::select(net::platform(), experiment.scope, experiment.candidate, stop);
                }));
                bool unsafe_state{};
                std::vector<std::pair<net::Clock::time_point, net::Clock::time_point>> validated;
                for (std::size_t j = 0; j < jobs.size(); ++j) {
                    auto result = jobs[j].get();
                    if (result.error) log("WARN", "handoff", result.error->operation, result.error->code);
                    unsafe_state = unsafe_state || result.state == "ROLLBACK_FAILED" || result.state == "ROLLBACK_UNVERIFIED";
                    update([&](View& v) {
                        if (!result.measured && result.state != "CANCELLED" && result.state != "SCOPE_BLOCKED") {
                            const auto& attempted = requested[wave[j]];
                            net::Observation failure;
                            failure.error = result.error;
                            result.measured = net::Candidate{attempted.candidate, std::move(failure), {}, net::Clock::now(), attempted.scope.method()};
                        }
                        if (result.measured) {
                            if (result.state == "HOST_VALIDATED" && result.measured->observation.sample_complete)
                                validated.emplace_back(result.measured->measurement_start, result.measured->observed);
                            auto& items = v.candidates;
                            if (items.size() >= 64) items.erase(items.begin());
                            items.push_back(std::move(*result.measured));
                        }
                        v.last_handoff = last_physical_;
                        v.outcomes.emplace_back(requested[wave[j]].candidate.bssid, result.state, result.error ? result.error->detail : "");
                    });
                }
                if (validated.size() == 2 && std::max(validated[0].first, validated[1].first) < std::min(validated[0].second, validated[1].second))
                    update([](View& v) { ++v.overlapping_validated_windows; });
                if (unsafe_state) break;
            }
            if (!stop.stop_requested()) hardware = net::platform().inspect(stop);
            update([&](View& v) {
                if (!stop.stop_requested()) { v.hardware = std::move(hardware); v.inspected_at = net::Clock::now(); }
                v.status = std::string(english(Key::PhysicalResults)) + std::to_string(v.outcomes.size()) + " / " + std::to_string(requested.size()) + english(Key::CompletedReviewEachOutcome);
            });
        });
    }
    void clear_lab() {
        if (!busy()) update([](View& v) {
            v.clients.clear(); v.completed_clients.reset(); v.counts = {}; v.events = v.client_count = v.simulated_ms = 0;
            v.status = english(Key::InMemoryLogicalClientsRemoved);
        });
    }
    void lab(sim::Config config, bool replay, bool export_csv) {
        run(english(Key::SimulatedVirtualLab), [this, config, replay, export_csv](auto stop) mutable {
            if (replay) config = sim::parse(read("last-scenario.txt", 128));
            sim::Lab lab(config);
            update([](View& v) { v.clients.clear(); v.completed_clients.reset(); v.counts = {}; v.events = v.client_count = v.simulated_ms = 0; });
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
            auto completed = std::make_shared<const std::vector<sim::Client>>(lab.clients());
            update([&](View& v) {
                v.completed_clients = std::move(completed);
                v.status = stop.stop_requested() ? english(Key::SimulatedRunCancelled) : english(Key::SimulatedRunComplete);
            });
        });
    }
    void profile(sim::Budget budget) {
        run(english(Key::SimulatedCapacityBenchmark), [this, budget](auto stop) {
            auto capacity = sim::profile(budget, stop);
            update([&](View& v) { v.status = std::string(english(Key::SimulationCapacity)) + std::string(sim::name(capacity.end)); v.capacity = std::move(capacity); });
        });
    }
    void export_logs() {
        run(english(Key::ExportLogs), [this](auto) {
            std::string text{"utc_epoch_seconds\tlevel\tmodule\tevent\tnative_code\n"};
            for (const auto& l : copy().logs) text += std::to_string(l.when) + '\t' + l.level + '\t' + l.module + '\t' + l.event + '\t' + std::to_string(l.code) + '\n';
            write("events.tsv", text);
            update([](View& v) { v.status = english(Key::EventsTsvWrittenToUser); });
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
            if (backend) { mode = english(Key::VulkanSdlGpu); return true; }
            warning = english(Key::VulkanUnavailableSwitchedToSdl);
            shutdown();
        }
        renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) return false;
        platform = ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
        if (platform) backend = ImGui_ImplSDLRenderer3_Init(renderer);
        mode = english(Key::SdlSoftwareRenderer);
        return backend;
    }
    void frame() {
        if (gpu) ImGui_ImplSDLGPU3_NewFrame(); else ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
    }
    bool draw(const char* capture = nullptr) {
        ImGui::Render();
        if (ImGui::GetDrawData()->DisplaySize.x <= 0 || ImGui::GetDrawData()->DisplaySize.y <= 0) return true;
        if (renderer) {
            const auto scale = ImGui::GetDrawData()->FramebufferScale;
            const auto c = palette(color_theme).canvas;
            if (!SDL_SetRenderScale(renderer, scale.x, scale.y) || !SDL_SetRenderDrawColorFloat(renderer, c.x, c.y, c.z, c.w) || !SDL_RenderClear(renderer)) return false;
            ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
            if (capture) {
                const std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> pixels(SDL_RenderReadPixels(renderer, nullptr), SDL_DestroySurface);
                if (!pixels || !SDL_SaveBMP(pixels.get(), capture)) return false;
            }
            return SDL_RenderPresent(renderer);
        }
        auto* commands = SDL_AcquireGPUCommandBuffer(gpu);
        if (!commands) return false;
        SDL_GPUTexture* texture{};
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &texture, nullptr, nullptr)) { SDL_CancelGPUCommandBuffer(commands); return false; }
        if (texture) {
            ImGui_ImplSDLGPU3_PrepareDrawData(ImGui::GetDrawData(), commands);
            SDL_GPUColorTargetInfo target{};
            target.texture = texture;
            const auto c = palette(color_theme).canvas; target.clear_color = {c.x, c.y, c.z, c.w};
            target.load_op = SDL_GPU_LOADOP_CLEAR; target.store_op = SDL_GPU_STOREOP_STORE;
            auto* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            if (!pass) { SDL_SubmitGPUCommandBuffer(commands); return false; }
            ImGui_ImplSDLGPU3_RenderDrawData(ImGui::GetDrawData(), commands, pass);
            SDL_EndGPURenderPass(pass);
        }
        return SDL_SubmitGPUCommandBuffer(commands);
    }
};

// ---- small helpers shared by lab panels ----------------------------------------------------
void text(std::string_view s) { w::text(s); }
std::string radio_roles(std::string_view list) {
    std::string result;
    while (!list.empty()) {
        const auto split = list.find(", ");
        if (!result.empty()) result += ", ";
        result += message(list.substr(0, split));
        if (split == list.npos) break;
        list.remove_prefix(split + 2);
    }
    return result;
}
void paragraph(const std::string& s) { w::wrapped(s); }
const Palette& colors() { return palette(color_theme); }
void errors(const std::vector<net::Error>& list) {
    for (const auto& e : list) {
        ImGui::TextColored(colors().warning.strong, "%s [%u]", message(e.operation).c_str(), e.code);
        paragraph(message(e.detail));
    }
}
void field(const char* name, const std::string& v) {
    ImGui::TextColored(colors().muted, "%s", name); ImGui::SameLine();
    text(v.empty() ? tr(Key::Unknown) : v);
}
std::string reported(std::optional<bool> value) { return !value ? "" : *value ? tr(Key::Yes) : tr(Key::No); }
void metric(const char* name, std::optional<double> v, const char* unit) {
    if (v) ImGui::Text("%-18s %8.2f %s", name, *v, unit); else ImGui::Text(tr(Key::Value18sUnverified), name);
}
void number_input(const char* label, unsigned& v) { ImGui::InputScalar(label, ImGuiDataType_U32, &v); }
// Checkbox whose label wraps instead of being clipped at narrow widths.
bool consent_box(const char* id, const char* label, bool* value) {
    const bool changed = ImGui::Checkbox(id, value);
    ImGui::SameLine();
    w::wrapped(label);
    return changed;
}

enum class Page { connect, diagnostics, hardware, radio, discovery, virtual_lab, experiments, trials, performance, intelligence, paths, settings, about, count };
constexpr auto page_count = static_cast<unsigned>(Page::count);
constexpr std::array<Key, page_count> page_keys{Key::NavConnect, Key::LogsDiagnostics, Key::HardwareLab, Key::RadioCapabilities, Key::NetworkDiscovery,
    Key::VirtualClientLab, Key::ExperimentCenter, Key::LabTrialsTitle, Key::PerformanceLab, Key::IntelligenceEngine, Key::PathSpace, Key::Settings, Key::About};
struct NavGroup { Key title; Page first, last; };
constexpr std::array nav_groups{NavGroup{Key::NavSectionConnect, Page::connect, Page::diagnostics},
    NavGroup{Key::NavSectionLab, Page::hardware, Page::paths}, NavGroup{Key::NavSectionApp, Page::settings, Page::about}};

void language_control(Preferences& prefs) {
    ImGui::SetNextItemWidth(w::px(64));
    if (ImGui::BeginCombo("##language", prefs.language == Language::fr ? "FR" : "EN")) {
        if (ImGui::Selectable(tr(Key::UseFrench), prefs.language == Language::fr)) prefs.language = Language::fr;
        if (ImGui::Selectable(tr(Key::UseEnglish), prefs.language == Language::en)) prefs.language = Language::en;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(Key::Language));
}
void theme_control(Preferences& prefs) {
    if (w::button(colors(), tr(prefs.theme == Theme::light ? Key::Light : Key::Dark), w::Kind::secondary))
        prefs.theme = prefs.theme == Theme::light ? Theme::dark : Theme::light;
    ImGui::SetItemTooltip("%s", tr(Key::Theme));
}
std::string age_text(net::Clock::time_point at, Key seconds, Key minutes) {
    const auto age = std::chrono::duration_cast<std::chrono::seconds>(net::Clock::now() - at).count();
    return age < 120 ? format(tr(seconds), static_cast<long long>(std::max<long long>(0, age))) : format(tr(minutes), static_cast<long long>(age / 60));
}
const char* band_name(unsigned mhz) { return mhz >= 5925 ? "6 GHz" : mhz >= 4900 ? "5 GHz" : mhz >= 2400 ? "2,4 GHz" : ""; }
std::string band_label(unsigned mhz) {
    std::string b = band_name(mhz);
    if (language == Language::en && b == "2,4 GHz") b = "2.4 GHz";
    return b;
}
Key failure_key(net::WifiFailure f) {
    switch (f) {
    case net::WifiFailure::authentication: return Key::CauseAuth;
    case net::WifiFailure::profile: return Key::CauseProfile;
    case net::WifiFailure::radio: return Key::CauseRadio;
    case net::WifiFailure::association_unconfirmed: return Key::CauseAssociation;
    case net::WifiFailure::ip_unconfirmed: return Key::CauseAddress;
    case net::WifiFailure::request: return Key::CauseRequest;
    case net::WifiFailure::unspecified: break;
    }
    return Key::CauseUnknown;
}
Key failure_advice(net::WifiFailure f) {
    switch (f) {
    case net::WifiFailure::authentication: return Key::FailAuth;
    case net::WifiFailure::profile: return Key::FailProfile;
    case net::WifiFailure::radio: return Key::FailRadio;
    case net::WifiFailure::association_unconfirmed: return Key::FailAssociation;
    case net::WifiFailure::ip_unconfirmed: return Key::FailAddress;
    case net::WifiFailure::request: return Key::FailRequest;
    case net::WifiFailure::unspecified: break;
    }
    return Key::WifiFailed;
}

// A network as people think of it: one name, possibly many access points and adapters.
struct Network {
    std::string key, ssid, label, security;
    std::vector<std::size_t> members; // indices into Discovery::bss, strongest first
    int best_rssi{-127};
    unsigned bands{}; // 1 = 2.4 GHz, 2 = 5 GHz, 4 = 6 GHz
    bool saved{}, secured{}, connected{};
    std::size_t access_points{};
};
std::vector<Network> group_networks(const View& v) {
    std::map<std::string, Network> groups;
    for (std::size_t i = 0; i < v.discovery.bss.size(); ++i) {
        const auto& b = v.discovery.bss[i];
        if (!b.infrastructure) continue;
        auto key = b.ssid + '\n' + b.security;
        auto& n = groups[key];
        if (n.members.empty()) {
            n.key = key; n.ssid = b.ssid; n.security = b.security;
            n.label = b.ssid.empty() ? std::string(tr(Key::HiddenNetwork)) : net::display_ssid(b.ssid);
            n.secured = b.security.starts_with("Privacy bit set");
        }
        n.members.push_back(i);
        n.best_rssi = std::max(n.best_rssi, b.rssi);
        n.bands |= b.mhz >= 5925 ? 4u : b.mhz >= 4900 ? 2u : 1u;
        n.saved = n.saved || !b.profile.empty();
    }
    std::vector<Network> out;
    out.reserve(groups.size());
    for (auto& [key, n] : groups) {
        std::sort(n.members.begin(), n.members.end(), [&](auto a, auto b) { return v.discovery.bss[a].rssi > v.discovery.bss[b].rssi; });
        std::set<std::string> distinct;
        for (const auto m : n.members) distinct.insert(v.discovery.bss[m].bssid);
        n.access_points = distinct.size();
        for (const auto& i : v.hardware.interfaces)
            if (i.wireless && i.associated && !n.ssid.empty() && i.ssid == n.ssid) n.connected = true;
        out.push_back(std::move(n));
    }
    std::sort(out.begin(), out.end(), [](const Network& a, const Network& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.saved != b.saved) return a.saved;
        if (a.best_rssi != b.best_rssi) return a.best_rssi > b.best_rssi;
        return a.label < b.label;
    });
    return out;
}

struct Screen {
    Page page{Page::connect};
    int selected_bss{-1};
    bool export_csv{}, consent{}, warnings_only{};
    bool unreadable_preferences{}, unsaved_preferences{}, reset_display{};
    std::vector<net::Experiment> confirmation;
    bool confirmation_queue{};
    bool wifi_settings_failed{};
    std::optional<net::Bss> wifi_confirmation;
    net::WifiMode wifi_mode{net::WifiMode::normal};
    unsigned logical_selected{}, logical_page{1};
    std::vector<net::Bss> batch;
    int measurement_offset{};
    ImGuiTextFilter discovery_filter, network_filter;
    std::string selected_network;   // Network::key
    std::string start_bssid;        // assisted start access point (bssid + interface)
    std::string start_interface;
    bool fixture{};                 // smoke rendering with sample data; never set in normal use

    sim::Config config;
    sim::Budget budget;
    std::array<double,5> weights{net::base_weights};
    net::Clock::time_point refresh{};
    std::size_t seen_revision{};
    int path_scenario{};
    bool path_consent{};
    View view;
    void request(std::vector<net::Experiment> experiments, bool from_queue = false) {
        confirmation_queue = from_queue;
        confirmation = std::move(experiments);
        ImGui::OpenPopup("###physical_confirmation");
        consent = false;
    }
    void open_windows_wifi() {
#ifdef _WIN32
        wifi_settings_failed = !SDL_OpenURL("ms-settings:network-wifi");
#endif
    }
    // Windows' own captive-portal probe target: it redirects to the sign-in page when one exists.
    void open_sign_in() { SDL_OpenURL("http://www.msftconnecttest.com/redirect"); }
    const net::Interface* active_wifi() const {
        for (const auto& i : view.hardware.interfaces) if (i.wireless && i.associated) return &i;
        return nullptr;
    }
    std::optional<net::Reachability> reach_of(const net::Interface& i) const {
        for (const auto& [id, r] : view.reach) if (id == i.id) return r;
        return std::nullopt;
    }
    void start_wifi(Worker& worker, const net::Bss& target, net::WifiMode mode) {
        const auto* active = active_wifi();
        const bool disruptive = active && active->id == target.interface_id && !(active->ssid == target.ssid && active->profile == target.profile);
        wifi_mode = mode;
        if (disruptive) { wifi_confirmation = target; ImGui::OpenPopup("###wifi_switch"); return; }
        worker.wifi(target, mode);
    }

    // ---- page chrome ------------------------------------------------------------------------
    void page_header(Key title, const char* subtitle = nullptr) {
        w::display(colors(), tr(title));
        if (subtitle) w::caption(colors(), subtitle);
        w::gap(space::md);
    }
    void top_bar(Worker& worker, Preferences& prefs) {
        const auto& c = colors();
        const float height = ImGui::GetFrameHeight() + w::px(space::md) * 2;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.surface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {w::px(space::lg), w::px(space::md)});
        ImGui::BeginChild("TopBar", {0, height}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(nullptr, w::body_size() * type::heading);
        ImGui::TextColored(c.accent_text, "CAMPUSLINK");
        ImGui::PopFont();
        const bool narrow = ImGui::GetContentRegionAvail().x < w::px(640);
        if (!narrow) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); ImGui::TextColored(c.muted, "%s", tr(Key::AppTagline)); }
        // Right-aligned controls.
        const float stop_w = worker.busy() ? ImGui::CalcTextSize(tr(Key::StopTask)).x + w::px(space::lg) * 2 + w::px(space::sm) : 0;
        const float busy_w = worker.busy() ? ImGui::CalcTextSize(tr(Key::Busy)).x * type::caption + w::px(24) : 0;
        const float theme_w = ImGui::CalcTextSize(tr(prefs.theme == Theme::light ? Key::Light : Key::Dark)).x + w::px(space::lg) * 2;
        const float right = w::px(64) + theme_w + stop_w + busy_w + w::px(space::sm) * 3;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + w::px(space::lg), ImGui::GetWindowContentRegionMax().x - right));
        if (worker.busy()) {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight() * type::caption - w::px(6)) * 0.5f);
            w::badge(c.info, tr(Key::Busy)); ImGui::SameLine();
            ImGui::SetCursorPosY(w::px(space::md));
            if (w::button(c, tr(Key::StopTask), w::Kind::danger)) worker.stop();
            ImGui::SameLine();
        }
        language_control(prefs); ImGui::SameLine(); theme_control(prefs);
        ImGui::EndChild();
        ImGui::PopStyleVar(); ImGui::PopStyleColor();
    }
    void navigation(float width) {
        const auto& c = colors();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, c.surface);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {w::px(space::sm), w::px(space::md)});
        ImGui::BeginChild("Navigation", {width, 0}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding);
        for (const auto& group : nav_groups) {
            ImGui::Indent(w::px(space::sm));
            w::section(c, tr(group.title));
            ImGui::Unindent(w::px(space::sm));
            for (auto p = static_cast<unsigned>(group.first); p <= static_cast<unsigned>(group.last); ++p) {
                const bool selected = page == static_cast<Page>(p);
                const auto label = std::string(tr(page_keys[p])) + "###nav" + std::to_string(p);
                ImGui::PushStyleColor(ImGuiCol_Header, c.accent_soft);
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ? c.accent_soft : c.raised);
                ImGui::PushStyleColor(ImGuiCol_HeaderActive, c.accent_soft);
                ImGui::PushStyleColor(ImGuiCol_Text, selected ? c.accent_text : c.text);
                ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, {0, 0.5f});
                const auto pos = ImGui::GetCursorScreenPos();
                ImGui::Indent(w::px(space::sm));
                if (ImGui::Selectable(label.c_str(), selected, 0, {0, ImGui::GetFrameHeight()})) page = static_cast<Page>(p);
                ImGui::Unindent(w::px(space::sm));
                if (selected) ImGui::GetWindowDrawList()->AddRectFilled({pos.x, pos.y + w::px(6)}, {pos.x + w::px(3), pos.y + ImGui::GetFrameHeight() - w::px(6)},
                    ImGui::GetColorU32(c.accent), w::px(2));
                ImGui::PopStyleVar(); ImGui::PopStyleColor(4);
            }
        }
        ImGui::Dummy({0, w::px(space::lg)});
        ImGui::Indent(w::px(space::sm));
        ImGui::TextColored(c.muted, "v%s", build::version);
        ImGui::Unindent(w::px(space::sm));
        ImGui::EndChild();
        ImGui::PopStyleVar(); ImGui::PopStyleColor();
    }

    // ---- connection workspace ---------------------------------------------------------------
    void current_connection_card(Worker& worker) {
        const auto& c = colors();
        if (!w::begin_card(c, "current")) { w::end_card(); return; }
        const auto* active = active_wifi();
        const bool any_wifi = std::any_of(view.hardware.interfaces.begin(), view.hardware.interfaces.end(), [](const auto& i) { return i.wireless; });
        // Header: caption on the left, age + refresh on the right.
        ImGui::AlignTextToFramePadding();
        w::caption(c, tr(Key::CurrentConnection), false);
        {
            const auto age = view.inspected_at != net::Clock::time_point{} ? age_text(view.inspected_at, Key::UpdatedSeconds, Key::UpdatedMinutes) : std::string{};
            ImGui::PushFont(nullptr, w::body_size() * type::caption);
            const float age_w = age.empty() ? 0 : ImGui::CalcTextSize(age.c_str()).x + w::px(space::sm);
            ImGui::PopFont();
            const float refresh_w = ImGui::CalcTextSize(tr(Key::Refresh)).x + w::px(space::lg) * 2;
            ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - age_w - refresh_w));
            if (!age.empty()) { ImGui::AlignTextToFramePadding(); w::caption(c, age, false); ImGui::SameLine(0, w::px(space::sm)); }
            ImGui::BeginDisabled(worker.busy());
            if (w::button(c, tr(Key::Refresh), w::Kind::ghost)) worker.inspect();
            ImGui::EndDisabled();
        }
        if (!active) {
            w::title(c, tr(Key::NotConnected));
            w::caption(c, tr(any_wifi || view.hardware.interfaces.empty() ? Key::NotConnectedHint : Key::NoWifiAdapter));
            w::end_card();
            return;
        }
        // Network name with the Windows Internet report beside it.
        w::title(c, net::display_ssid(active->ssid));
        const auto reach = reach_of(*active);
        const auto tone = !reach ? c.neutral : *reach == net::Reachability::internet ? c.success :
            *reach == net::Reachability::portal || *reach == net::Reachability::local_only ? c.warning : c.neutral;
        const auto label = !reach ? Key::ReachUnknown : *reach == net::Reachability::internet ? Key::InternetReported :
            *reach == net::Reachability::portal ? Key::PortalReported : *reach == net::Reachability::local_only ? Key::LocalOnlyReported : Key::ReachUnknown;
        w::badge(tone, tr(label));
        w::gap(space::xs);
        std::string band;
        for (const auto& b : view.discovery.bss) if (b.bssid == active->bssid && b.interface_id == active->id) { band = band_label(b.mhz); break; }
        // Channel numbers overlap between 2.4 GHz and 6 GHz: without the scanned frequency, show the channel only.
        const bool band_known = !band.empty();
        if (active->channel && *active->channel) band += (band.empty() ? "" : " · ") + std::to_string(*active->channel);
        // Two label/value pairs per row when there is room, one otherwise.
        const bool grid = ImGui::GetContentRegionAvail().x >= w::px(440);
        const int columns = grid ? 4 : 2;
        if (ImGui::BeginTable("current-fields", columns, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings)) {
            for (int col = 0; col < columns; ++col)
                ImGui::TableSetupColumn("##c", col % 2 ? ImGuiTableColumnFlags_WidthStretch : ImGuiTableColumnFlags_WidthFixed, col % 2 ? 1.f : w::px(grid ? 92 : 120));
            int cell{};
            const auto put = [&](const char* name, const std::function<void()>& value) {
                if (cell % columns == 0) ImGui::TableNextRow();
                ImGui::TableNextColumn(); w::colored(c.muted, name, false);
                ImGui::TableNextColumn(); value();
                cell += 2;
            };
            if (active->rssi) put(tr(Key::SignalLabel), [&] { w::signal_bars(c, *active->rssi); ImGui::SameLine(); ImGui::Text("%d dBm", *active->rssi); });
            if (!band.empty()) put(tr(band_known ? Key::BandChannel : Key::ChannelLabel), [&] { text(band); });
            put(tr(Key::Ipv4), [&] { text(active->address.empty() ? tr(Key::Unknown) : active->address); });
            put(tr(Key::Gateway), [&] { text(active->gateway.empty() ? tr(Key::Unknown) : active->gateway); });
            ImGui::EndTable();
        }
        w::caption(c, active->description.empty() ? active->name : active->description, false);
        w::end_card();
    }
    void network_row(const Network& n, bool selected) {
        const auto& c = colors();
        const float row_h = ImGui::GetTextLineHeight() * 2 + w::px(space::md) * 2;
        ImGui::PushID(n.key.c_str());
        const auto origin = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleColor(ImGuiCol_Header, c.accent_soft);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, selected ? c.accent_soft : c.raised);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, c.accent_soft);
        if (ImGui::Selectable("##network", selected, ImGuiSelectableFlags_AllowOverlap, {width, row_h})) {
            selected_network = n.key;
            start_bssid.clear(); start_interface.clear();
        }
        ImGui::PopStyleColor(3);
        // Content drawn over the selectable.
        const float pad = w::px(space::md);
        ImGui::SetCursorScreenPos({origin.x + pad, origin.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f});
        w::signal_bars(c, n.best_rssi);
        ImGui::SetCursorScreenPos({origin.x + pad + w::px(28), origin.y + pad - w::px(1)});
        ImGui::PushStyleColor(ImGuiCol_Text, selected ? c.accent_text : c.text);
        w::text(n.label);
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos({origin.x + pad + w::px(28), origin.y + pad + ImGui::GetTextLineHeight() + w::px(2)});
        std::string sub = tr(n.secured ? Key::Secured : Key::OpenNetwork);
        for (unsigned bit = 1; bit <= 4; bit <<= 1) if (n.bands & bit) sub += std::string(" · ") + band_label(bit == 1 ? 2412 : bit == 2 ? 5180 : 5955);
        sub += " · ";
        sub += n.access_points == 1 ? std::string(tr(Key::OneAccessPoint)) : format(tr(Key::AccessPointsCount), static_cast<unsigned>(n.access_points));
        w::caption(c, sub, false);
        // Status badge, right aligned.
        const auto badge_key = n.connected ? Key::Connected : n.saved ? Key::Saved : Key::NotSaved;
        const auto badge_tone = n.connected ? c.success : n.saved ? Tone{c.accent_text, c.accent_soft} : c.neutral;
        ImGui::PushFont(nullptr, w::body_size() * type::caption);
        const float bw = ImGui::CalcTextSize(tr(badge_key)).x + w::px(16);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos({origin.x + width - bw - pad, origin.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f - w::px(1)});
        w::badge(badge_tone, tr(badge_key));
        // Re-submit the row extent so the layout grows by the row, not by the overlaid cursor moves.
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy({width, row_h});
        ImGui::PopID();
    }
    void networks_card(Worker& worker, const std::vector<Network>& networks, bool fresh) {
        const auto& c = colors();
        if (!w::begin_card(c, "networks")) { w::end_card(); return; }
        const auto now = net::Clock::now();
        w::heading(c, tr(Key::Networks));
        if (view.scanning) w::caption(c, tr(Key::Scanning), false);
        else if (view.scan_at != net::Clock::time_point{}) w::caption(c, age_text(view.scan_at, Key::ScanAgeSeconds, Key::ScanAgeMinutes), false);
        else w::caption(c, tr(Key::ScanNever));
        w::gap(space::sm);
        const auto wait = std::chrono::duration_cast<std::chrono::seconds>(view.scan_ready - now).count();
        ImGui::BeginDisabled(worker.busy() || wait > 0);
        const auto scan_label = std::string(tr(view.scan_at == net::Clock::time_point{} ? Key::ScanNetworks : Key::Rescan)) + "###scan";
        if (w::button(c, scan_label.c_str(), view.scan_at == net::Clock::time_point{} || !fresh ? w::Kind::primary : w::Kind::secondary)) worker.scan();
        ImGui::EndDisabled();
        if (wait > 0 && !worker.busy()) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); w::caption(c, format(tr(Key::AvailableIn), static_cast<int>(wait)), false); }
        if (networks.size() > 6) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(std::max(w::px(120), ImGui::GetContentRegionAvail().x));
            network_filter.Draw("##filter", 0);
            if (!network_filter.IsActive()) {
                const auto r = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText({r.x + ImGui::GetStyle().FramePadding.x, r.y + ImGui::GetStyle().FramePadding.y},
                    ImGui::GetColorU32(c.muted), tr(Key::FilterNetworks));
            }
        }
        errors(view.discovery.errors);
        if (view.discovery.cached && !view.scanning) { w::gap(space::xs); w::banner(c.info, c, tr(Key::ScanCached)); }
        w::gap(space::sm);
        if (networks.empty() && view.scan_at != net::Clock::time_point{} && !view.scanning) w::caption(c, tr(Key::WifiEmpty));
        unsigned shown{};
        for (const auto& n : networks) {
            if (network_filter.IsActive() && !network_filter.PassFilter(n.label.c_str())) continue;
            network_row(n, n.key == selected_network);
            ++shown;
        }
        if (!networks.empty() && !shown) w::caption(c, tr(Key::NoFilterMatch));
        w::end_card();
    }
    void selection_card(Worker& worker, const Network& n, bool fresh) {
        const auto& c = colors();
        if (!w::begin_card(c, "selection")) { w::end_card(); return; }
        w::caption(c, tr(Key::WifiNetwork), false);
        w::title(c, n.label);
        // Normal mode: the strongest saved row (Windows still picks the BSSID). Assisted: chosen start AP.
        const net::Bss* normal_target{};
        for (const auto m : n.members) if (!view.discovery.bss[m].profile.empty()) { normal_target = &view.discovery.bss[m]; break; }
        std::vector<net::Bss> members;
        for (const auto m : n.members) members.push_back(view.discovery.bss[m]);
        const auto recommended = net::recommend_start(members);
        const net::Bss* recommended_bss = recommended ? &view.discovery.bss[n.members[recommended->index]] : normal_target;
        const net::Bss* assisted_target = recommended_bss;
        for (const auto m : n.members) {
            const auto& b = view.discovery.bss[m];
            if (b.bssid == start_bssid && b.interface_id == start_interface && !b.profile.empty()) { assisted_target = &b; break; }
        }
        const auto* active = active_wifi();
        const bool here = active && n.connected && normal_target && active->profile == normal_target->profile;
        const auto ready_in = std::chrono::duration_cast<std::chrono::seconds>(view.wifi_ready - net::Clock::now()).count();
        if (!n.saved) {
            w::gap(space::sm);
            w::banner(c.warning, c, tr(Key::NotSaved), tr(Key::NotSavedBody));
            w::gap(space::sm);
            if (w::button(c, tr(Key::WifiSettings), w::Kind::primary)) open_windows_wifi();
        } else {
            if (here) { w::gap(space::xs); w::badge(c.success, tr(Key::AlreadyHere)); }
            if (!fresh) { w::gap(space::sm); w::banner(c.warning, c, tr(Key::ScanExpiredRefreshBeforeConnecting), tr(Key::StaleBody)); }
            const bool can = fresh && !worker.busy() && ready_in <= 0 && normal_target;
            w::gap(space::md);
            // Option 1: normal.
            w::heading(c, tr(Key::WifiNormal));
            w::caption(c, tr(Key::NormalDesc));
            ImGui::BeginDisabled(!can);
            if (w::button(c, (std::string(tr(Key::WifiNormal)) + "###normal").c_str(), w::Kind::primary)) start_wifi(worker, *normal_target, net::WifiMode::normal);
            ImGui::EndDisabled();
            w::gap(space::md);
            ImGui::Separator();
            w::gap(space::sm);
            // Option 2: assisted, with an explicit starting access point.
            w::heading(c, tr(Key::WifiAssist));
            w::caption(c, tr(Key::AssistDesc));
            w::section(c, tr(Key::StartAccessPoint));
            const bool many_adapters = std::count_if(view.hardware.interfaces.begin(), view.hardware.interfaces.end(), [](const auto& i) { return i.wireless; }) > 1;
            if (ImGui::BeginTable("aps", many_adapters ? 4 : 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_RowBg)) {
                unsigned rows{};
                for (const auto m : n.members) {
                    const auto& b = view.discovery.bss[m];
                    if (b.profile.empty() || ++rows > 6) continue;
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(m));
                    const bool chosen = assisted_target == &b;
                    if (ImGui::RadioButton("##ap", chosen)) { start_bssid = b.bssid; start_interface = b.interface_id; }
                    ImGui::SameLine(); ImGui::PushFont(nullptr, w::body_size() * 0.95f); text(b.bssid); ImGui::PopFont();
                    if (&b == recommended_bss) { ImGui::SameLine(); w::badge({c.accent_text, c.accent_soft}, tr(Key::Recommended)); }
                    ImGui::TableNextColumn(); w::signal_bars(c, b.rssi); ImGui::SameLine(); ImGui::TextColored(c.muted, "%d dBm", b.rssi);
                    ImGui::TableNextColumn(); ImGui::TextColored(c.muted, "%s · %u", band_label(b.mhz).c_str(), b.channel);
                    if (many_adapters) {
                        ImGui::TableNextColumn();
                        const auto adapter = std::find_if(view.hardware.interfaces.begin(), view.hardware.interfaces.end(), [&](const auto& i) { return i.id == b.interface_id; });
                        ImGui::TextColored(c.muted, "%s", adapter != view.hardware.interfaces.end() ? adapter->name.c_str() : "");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (recommended) w::caption(c, tr(recommended->reason == net::StartReason::wider_band ? Key::StartWiderBand : Key::StartStrongest));
            w::gap(space::xs);
            ImGui::BeginDisabled(!can || !assisted_target);
            if (w::button(c, (std::string(tr(Key::WifiAssist)) + "###assist").c_str(), w::Kind::secondary)) start_wifi(worker, *assisted_target, net::WifiMode::assisted);
            ImGui::EndDisabled();
            if (ready_in > 0 && !worker.busy()) { ImGui::SameLine(); ImGui::AlignTextToFramePadding(); w::caption(c, format(tr(Key::AvailableIn), static_cast<int>(ready_in)), false); }
        }
        switch_dialog(worker);
        w::end_card();
    }
    void switch_dialog(Worker& worker) {
        const auto& c = colors();
        const std::string title = std::string(tr(Key::SwitchTitle)) + "###wifi_switch";
        const auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSizeConstraints({std::min(w::px(420), vp->WorkSize.x), 0}, {std::min(w::px(560), vp->WorkSize.x), vp->WorkSize.y});
        if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (wifi_confirmation) {
                w::heading(c, net::display_ssid(wifi_confirmation->ssid));
                w::caption(c, tr(wifi_mode == net::WifiMode::normal ? Key::WifiNormal : Key::WifiAssist));
                w::gap(space::sm);
                w::wrapped(tr(Key::SwitchBody));
                w::gap(space::md);
            }
            ImGui::BeginDisabled(worker.busy() || !wifi_confirmation);
            if (w::button(c, tr(Key::Continue), w::Kind::primary)) { worker.wifi(*wifi_confirmation, wifi_mode); wifi_confirmation.reset(); ImGui::CloseCurrentPopup(); }
            ImGui::EndDisabled(); ImGui::SameLine();
            if (w::button(c, tr(Key::Cancel)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) { wifi_confirmation.reset(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }
    void progress_card(Worker& worker) {
        const auto& c = colors();
        if (!view.wifi_progress) return;
        const auto& p = *view.wifi_progress;
        if (!w::begin_card(c, "progress")) { w::end_card(); return; }
        w::heading(c, tr(view.wifi_mode == net::WifiMode::normal ? Key::ProgressNormal : Key::ProgressAssist));
        if (p.attempts > 1 || (p.attempt > 0 && view.wifi_mode == net::WifiMode::assisted))
            w::caption(c, format(tr(Key::ProgressAttempt), p.attempt, std::max(p.attempts, p.attempt), p.bssid.c_str()));
        w::gap(space::md);
        const std::array<const char*, 4> labels{tr(Key::StepPrepare), tr(Key::StepAssociate), tr(Key::StepAddress), tr(Key::StepInternet)};
        const int at = p.stage == net::WifiStage::preparing ? 0 : p.stage == net::WifiStage::associating ? 1 :
            p.stage == net::WifiStage::obtaining_address ? 2 : p.stage == net::WifiStage::checking_internet ? 3 : 1;
        std::array<w::StepState, 4> states{};
        for (int i = 0; i < 4; ++i) states[i] = i < at ? w::StepState::done : i == at ? w::StepState::active : w::StepState::pending;
        w::stepper(c, labels, states);
        const auto hint = p.stage == net::WifiStage::preparing ? Key::StagePrepareHint : p.stage == net::WifiStage::associating ? Key::StageAssociateHint :
            p.stage == net::WifiStage::obtaining_address ? Key::StageAddressHint : p.stage == net::WifiStage::checking_internet ? Key::StageInternetHint : Key::StageRestoreHint;
        w::caption(c, tr(hint));
        w::gap(space::sm);
        if (w::button(c, tr(Key::Cancel))) worker.stop();
        w::end_card();
    }
    void result_card(Worker& worker, const net::WifiResult& r, const Network* network, bool fresh) {
        const auto& c = colors();
        if (!w::begin_card(c, "result")) { w::end_card(); return; }
        const net::WifiAttempt* last = r.attempts.empty() ? nullptr : &r.attempts.back();
        const auto cause = last && last->error ? last->error->wifi_failure : net::WifiFailure::unspecified;
        switch (r.status) {
        case net::WifiStatus::internet_reported: w::banner(c.success, c, tr(Key::ResultInternetTitle), tr(Key::ResultInternetBody)); break;
        case net::WifiStatus::portal: w::banner(c.warning, c, tr(Key::ResultPortalTitle), tr(Key::ResultPortalBody)); break;
        case net::WifiStatus::local_link: w::banner(c.warning, c, tr(Key::ResultLocalTitle), tr(Key::ResultLocalBody)); break;
        case net::WifiStatus::ip_pending: w::banner(c.info, c, tr(Key::ResultPendingTitle), tr(Key::ResultPendingBody)); break;
        case net::WifiStatus::failed: w::banner(c.danger, c, tr(Key::ResultFailedTitle), tr(failure_advice(cause))); break;
        case net::WifiStatus::cancelled: w::banner(c.neutral, c, tr(Key::ResultCancelledTitle), tr(Key::WifiCancel)); break;
        case net::WifiStatus::recovery_failed: w::banner(c.danger, c, tr(Key::ResultRecoveryTitle), tr(Key::WifiRecovery)); break;
        case net::WifiStatus::request_unsettled: w::banner(c.danger, c, tr(Key::ResultUnsettledTitle), tr(Key::WifiUnsettled)); break;
        case net::WifiStatus::invalid: w::banner(c.warning, c, tr(Key::ResultInvalidTitle), tr(Key::WifiCannotStart)); break;
        }
        if (r.restored) { w::gap(space::xs); w::caption(c, tr(Key::RestoredNote)); }
        if (last && last->error && !last->error->os_message.empty()) {
            w::gap(space::xs);
            if (w::begin_fields("os", 150)) { w::field(c, tr(Key::WindowsMessage), last->error->os_message); w::end_fields(); }
        }
        w::gap(space::sm);
        // Next actions.
        bool first = true;
        const auto action = [&](Key k, w::Kind kind) { if (!first) ImGui::SameLine(); first = false; return w::button(c, tr(k), kind); };
        if (r.status == net::WifiStatus::portal || r.status == net::WifiStatus::local_link) { if (action(Key::OpenSignIn, w::Kind::primary)) open_sign_in(); }
        if (r.status == net::WifiStatus::ip_pending || r.status == net::WifiStatus::local_link || r.status == net::WifiStatus::portal) {
            ImGui::BeginDisabled(worker.busy());
            if (action(Key::CheckAgain, w::Kind::secondary)) worker.inspect();
            ImGui::EndDisabled();
        }
        if (r.status == net::WifiStatus::failed && view.wifi_mode == net::WifiMode::normal && net::retry_elsewhere(cause) && network && fresh && last) {
            const auto ready_in = std::chrono::duration_cast<std::chrono::seconds>(view.wifi_ready - net::Clock::now()).count();
            ImGui::BeginDisabled(worker.busy() || ready_in > 0);
            if (action(Key::TryAssisted, w::Kind::primary)) start_wifi(worker, last->candidate, net::WifiMode::assisted);
            ImGui::EndDisabled();
        }
        if (r.status == net::WifiStatus::failed || r.status == net::WifiStatus::recovery_failed || r.status == net::WifiStatus::request_unsettled) {
            if (action(Key::WifiSettings, w::Kind::secondary)) open_windows_wifi();
        }
        if (wifi_settings_failed) w::caption(c, tr(Key::WifiSettingsFailed));
        if (!r.attempts.empty() || r.elapsed.count()) {
            w::gap(space::sm);
            ImGui::PushStyleColor(ImGuiCol_Header, c.surface); ImGui::PushStyleColor(ImGuiCol_HeaderHovered, c.raised);
            const bool open = ImGui::CollapsingHeader(tr(Key::Details));
            ImGui::PopStyleColor(2);
            if (open) {
                if (w::begin_fields("summary", 190)) {
                    w::field(c, tr(Key::DurationLabel), format("%.1f s", static_cast<double>(r.elapsed.count()) / 1000.));
                    w::end_fields();
                }
                unsigned n{};
                constexpr std::array reasons{Key::WifiReasonSelected, Key::WifiReasonIndependent, Key::WifiReasonSuccess, Key::WifiReasonFailure, Key::WifiReasonSignal};
                for (const auto& a : r.attempts) {
                    ImGui::PushID(static_cast<int>(n));
                    w::section(c, format(tr(Key::AttemptNumber), ++n));
                    if (w::begin_fields("attempt", 190)) {
                        w::field(c, tr(Key::Bssid), a.candidate.bssid);
                        w::field(c, tr(Key::AttemptReason), tr(reasons[static_cast<unsigned>(a.reason)]));
                        if (a.timing.association) w::field(c, tr(Key::WifiAssociationTime), format("%lld ms", static_cast<long long>(a.timing.association->count())));
                        if (a.timing.usable) w::field(c, tr(Key::WifiUsableTime), format("%lld ms", static_cast<long long>(a.timing.usable->count())));
                        if (a.timing.internet_reported) w::field(c, tr(Key::WifiInternetTime), format("%lld ms", static_cast<long long>(a.timing.internet_reported->count())));
                        w::field(c, tr(Key::CauseLabel), a.error ? tr(failure_key(a.error->wifi_failure)) : tr(Key::Succeeded));
                        if (a.error && a.error->wlan_reason) w::field(c, tr(Key::ReasonCode), format("0x%05X", a.error->wlan_reason));
                        if (a.error) w::field(c, message(a.error->operation), message(a.error->detail));
                        w::end_fields();
                    }
                    ImGui::PopID();
                }
                w::caption(c, tr(Key::PathTimingSource));
            }
        }
        w::end_card();
    }
    void connect_page(Worker& worker) {
        const auto& c = colors();
        page_header(Key::NavConnect, tr(Key::ConnectSubtitle));
        const bool fresh = view.scan_at != net::Clock::time_point{} && net::Clock::now() - view.scan_at < net::wifi_scan_ttl;
        const auto networks = group_networks(view);
        const Network* chosen{};
        for (const auto& n : networks) if (n.key == selected_network) chosen = &n;
        const float avail = ImGui::GetContentRegionAvail().x;
        const bool two_columns = avail >= w::px(860);
        const float left_w = two_columns ? std::floor(avail * 0.56f) : avail;
        const auto side = [&] {
            current_connection_card(worker);
            w::gap(space::md);
            if (view.wifi_progress) { progress_card(worker); w::gap(space::md); }
            else if (view.wifi) { result_card(worker, *view.wifi, chosen, fresh); w::gap(space::md); }
            if (chosen) selection_card(worker, *chosen, fresh);
            else if (!networks.empty()) { if (w::begin_card(c, "hint", 0, true)) w::caption(c, tr(Key::SelectNetworkHint)); w::end_card(); }
        };
        if (two_columns) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
            ImGui::BeginChild("connect-left", {left_w, 0}, ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            networks_card(worker, networks, fresh);
            ImGui::EndChild();
            ImGui::SameLine(0, w::px(space::lg));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0, 0, 0, 0});
            ImGui::BeginChild("connect-right", {0, 0}, ImGuiChildFlags_None);
            ImGui::PopStyleColor();
            side();
            ImGui::EndChild();
        } else {
            current_connection_card(worker);
            w::gap(space::md);
            if (view.wifi_progress) { progress_card(worker); w::gap(space::md); }
            else if (view.wifi) { result_card(worker, *view.wifi, chosen, fresh); w::gap(space::md); }
            networks_card(worker, networks, fresh);
            w::gap(space::md);
            if (chosen) selection_card(worker, *chosen, fresh);
        }
    }

    void about() {
        const auto& c = colors();
        if (w::begin_card(c, "about-intro")) { paragraph(tr(Key::AboutIntro)); }
        w::end_card();
        w::gap(space::md);
        if (w::begin_card(c, "about-modules")) {
            w::heading(c, tr(Key::Modules));
            constexpr std::array<std::pair<Page, Key>, 9> modules{{{Page::hardware, Key::ModuleHardware}, {Page::radio, Key::ModuleRadio},
                {Page::virtual_lab, Key::ModuleVirtual}, {Page::discovery, Key::ModuleDiscovery}, {Page::experiments, Key::ModuleExperiment},
                {Page::performance, Key::ModulePerformance}, {Page::intelligence, Key::ModuleIntelligence}, {Page::trials, Key::ModuleConnection},
                {Page::diagnostics, Key::ModuleDiagnostics}}};
            for (const auto& [target, description] : modules) {
                w::gap(space::xs);
                ImGui::PushID(static_cast<int>(target));
                if (w::button(c, tr(page_keys[static_cast<unsigned>(target)]), w::Kind::link)) page = target;
                ImGui::PopID();
                w::caption(c, tr(description));
            }
            w::gap(space::sm);
            w::caption(c, tr(Key::SimulationVersusPhysical));
        }
        w::end_card();
        w::gap(space::md);
        if (w::begin_card(c, "about-build")) {
            w::heading(c, tr(Key::BuildInformation));
            if (w::begin_fields("build", 170)) {
                w::field(c, tr(Key::Product), "CAMPUSLINK"); w::field(c, tr(Key::Version), build::version); w::field(c, tr(Key::Build), build::revision);
                w::field(c, tr(Key::Architecture), build::architecture); w::field(c, tr(Key::Target), message(build::system));
                w::field(c, tr(Key::Configuration), message(build::configuration));
                w::field(c, tr(Key::Compiler), build::compiler);
                const auto v = SDL_GetVersion();
                w::field(c, "SDL3", std::to_string(SDL_VERSIONNUM_MAJOR(v)) + '.' + std::to_string(SDL_VERSIONNUM_MINOR(v)) + '.' + std::to_string(SDL_VERSIONNUM_MICRO(v)));
                w::field(c, "Dear ImGui", ImGui::GetVersion());
                w::end_fields();
            }
            w::gap(space::sm);
            w::caption(c, tr(Key::Credits));
        }
        w::end_card();
    }

    // ---- frame ------------------------------------------------------------------------------
    void draw(Worker& worker, const Display& display, Preferences& prefs, const Fonts& fonts) {
        if (!fixture && net::Clock::now() >= refresh) { worker.copy_if_changed(view, seen_revision); refresh = net::Clock::now() + 80ms; }
        const auto& tokens = colors();
        const auto* active = [&]() -> const net::Interface* {
            for (const auto& i : view.hardware.interfaces) if (i.associated) return &i;
            for (const auto& i : view.hardware.interfaces) if (i.wireless) return &i;
            return nullptr;
        }();
        const auto now = net::Clock::now();
        const auto* latest = view.candidates.empty() ? nullptr : &view.candidates.back();
        const net::Candidate* current{};
        if (active) for (auto it = view.candidates.rbegin(); it != view.candidates.rend(); ++it) {
            if (active->id == it->bss.interface_id && active->bssid == it->bss.bssid && active->profile == it->bss.profile && now - it->observed < 5min) { current = &*it; break; }
        }
        const auto* chosen = selected_bss >= 0 && static_cast<std::size_t>(selected_bss) < view.discovery.bss.size() ? &view.discovery.bss[selected_bss] : nullptr;
        const auto authorized = [&](const net::Bss& b) -> const net::Scope* {
            const auto match = std::find_if(view.scopes.begin(), view.scopes.end(), [&](const auto& scope) { return scope.allows(b); });
            return match == view.scopes.end() ? nullptr : &*match;
        };
        const auto present = [&](const net::Bss& b) {
            return std::any_of(view.discovery.bss.begin(), view.discovery.bss.end(), [&](const auto& seen) {
                return seen.interface_id == b.interface_id && seen.bssid == b.bssid && seen.profile == b.profile;
            });
        };
        const bool fresh_scan = view.scan_at != net::Clock::time_point{} && now - view.scan_at < net::wifi_scan_ttl;
        const auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {w::px(space::lg), w::px(space::lg)});
        ImGui::Begin("CAMPUSLINK", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::PopStyleVar();
        const float dpi = ImGui::GetStyle().FontScaleDpi;
        top_bar(worker, prefs);
        w::gap(space::sm);
        const bool narrow = ImGui::GetContentRegionAvail().x < w::px(900);
        if (!narrow) {
            navigation(w::px(232));
            ImGui::SameLine(0, w::px(space::lg));
        } else {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##navigation", tr(page_keys[static_cast<unsigned>(page)]))) {
                for (const auto& group : nav_groups) {
                    ImGui::SeparatorText(tr(group.title));
                    for (auto p = static_cast<unsigned>(group.first); p <= static_cast<unsigned>(group.last); ++p)
                        if (ImGui::Selectable(tr(page_keys[p]), page == static_cast<Page>(p))) page = static_cast<Page>(p);
                }
                ImGui::EndCombo();
            }
            w::gap(space::sm);
        }
        ImGui::PushID(static_cast<int>(page));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.canvas);
        ImGui::BeginChild("Panel", {0, 0}, ImGuiChildFlags_None);
        ImGui::PopStyleColor();
        ImGui::PushStyleColor(ImGuiCol_ChildBg, tokens.surface);
        if (fixture) { w::badge(tokens.neutral, tr(Key::CaptureFixture)); }
        if (unreadable_preferences) w::banner(tokens.warning, tokens, tr(Key::PreferencesInvalid));
        if (unsaved_preferences) w::banner(tokens.warning, tokens, tr(Key::PreferencesWriteFailed));
        const auto lab_header = [&](Key title) {
            page_header(title, tr(Key::LabNotice));
            if (worker.busy()) w::caption(tokens, message(view.status));
        };
        switch (page) {
        case Page::connect: connect_page(worker); break;
        case Page::diagnostics: {
            page_header(Key::LogsDiagnostics);
            if (w::begin_card(tokens, "env")) {
                w::heading(tokens, tr(Key::EnvironmentCard));
                if (w::begin_fields("env-fields", 200)) {
                    w::field(tokens, tr(Key::Renderer), message(display.mode));
                    w::field(tokens, tr(Key::ActiveWirelessInterface), active ? active->description : "");
                    w::field(tokens, tr(Key::AssociatedSsid), active ? net::display_ssid(active->ssid) : "");
                    w::field(tokens, tr(Key::InterfaceCount), std::to_string(view.hardware.interfaces.size()));
                    w::field(tokens, tr(Key::DiscoveredBss), std::to_string(view.discovery.bss.size()));
                    w::end_fields();
                }
                if (!display.warning.empty()) w::banner(tokens.warning, tokens, message(display.warning));
                w::caption(tokens, message(view.status));
                ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::RefreshHardware))) worker.inspect(); ImGui::EndDisabled();
                errors(view.hardware.errors);
            }
            w::end_card();
            w::gap(space::md);
            if (w::begin_card(tokens, "diagnosis")) {
                w::heading(tokens, tr(Key::DiagnosisCard));
                if (ImGui::BeginTable("diag", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoSavedSettings)) {
                    ImGui::TableSetupColumn("##step", ImGuiTableColumnFlags_WidthFixed, w::px(150));
                    ImGui::TableSetupColumn("##status", ImGuiTableColumnFlags_WidthFixed, w::px(120));
                    ImGui::TableSetupColumn("##detail", ImGuiTableColumnFlags_WidthStretch);
                    for (const auto& d : net::diagnose(active, !view.discovery.bss.empty(), current ? &current->observation : nullptr)) {
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        text(message(d.step));
                        ImGui::TableNextColumn();
                        const auto tone = d.status == "PASS" ? tokens.success : d.status == "WARN" ? tokens.warning :
                            d.status == "UNAVAILABLE" ? tokens.danger : d.status == "INFERRED" ? tokens.info : tokens.neutral;
                        w::badge(tone, message(d.status));
                        ImGui::TableNextColumn(); paragraph(message(d.detail));
                    }
                    ImGui::EndTable();
                }
            }
            w::end_card();
            w::gap(space::md);
            if (w::begin_card(tokens, "events")) {
                w::heading(tokens, tr(Key::EventsCard));
                ImGui::Checkbox(tr(Key::WarningsAndErrorsOnly), &warnings_only);
                ImGui::SameLine();
                ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::ExportEventsTsv))) worker.export_logs(); ImGui::EndDisabled();
                ImGui::PushFont(fonts.choose(Typeface::mono), ImGui::GetStyle().FontSizeBase * 0.92f);
                for (auto it = view.logs.rbegin(); it != view.logs.rend(); ++it) {
                    if (warnings_only && it->level == "INFO") continue;
                    ImGui::TextColored(it->level == "INFO" ? tokens.muted : tokens.warning.strong, "%lld | %s | %s | %s | %u", it->when,
                        message(it->level).c_str(), message(it->module).c_str(), message(it->event).c_str(), it->code);
                }
                ImGui::PopFont();
            }
            w::end_card();
            break;
        }
        case Page::hardware:
            lab_header(Key::HardwareLab);
            ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::RefreshHardware))) worker.inspect(); ImGui::EndDisabled();
            errors(view.hardware.errors);
            w::gap(space::sm);
            for (const auto& i : view.hardware.interfaces) {
                ImGui::PushID(i.id.c_str());
                if (w::begin_card(tokens, "iface")) {
                    w::heading(tokens, i.name.empty() ? i.description : i.name);
                    if (w::begin_fields("f", 190)) {
                        w::field(tokens, tr(Key::Description), i.description); w::field(tokens, tr(Key::InterfaceId), i.id);
                        w::field(tokens, tr(Key::AdapterClass), message(i.kind)); w::field(tokens, tr(Key::PnpHardwareId), i.hardware_id);
                        w::field(tokens, tr(Key::Manufacturer), i.manufacturer); w::field(tokens, tr(Key::DriverProvider), i.provider);
                        w::field(tokens, tr(Key::DriverVersion), i.driver); w::field(tokens, tr(Key::DriverDate), i.driver_date);
                        w::field(tokens, tr(Key::Ipv4), i.address); w::field(tokens, tr(Key::Gateway), i.gateway); w::field(tokens, tr(Key::DnsConfiguration), i.dns);
                        w::end_fields();
                    }
                    ImGui::TextColored(tokens.muted, tr(Key::OperationalSAssociatedSDhcp), i.up ? tr(Key::Yes117) : tr(Key::No118),
                        i.associated ? tr(Key::Yes117) : tr(Key::Unconfirmed), i.dhcp ? tr(Key::Reported) : tr(Key::NotReported));
                }
                w::end_card(); w::gap(space::sm);
                ImGui::PopID();
            }
            break;
        case Page::radio: {
            lab_header(Key::RadioCapabilities);
            std::set<std::string> physical, associated;
            std::set<unsigned> observed_bands;
            for (const auto& i : view.hardware.interfaces) {
                if (!i.wireless) continue;
                if (i.hardware_interface && !i.device_instance.empty()) {
                    physical.insert(i.device_instance);
                    if (i.associated) associated.insert(i.device_instance);
                }
                ImGui::PushID(i.id.c_str());
                if (w::begin_card(tokens, "radio")) {
                    w::heading(tokens, i.description);
                    field(tr(Key::PhyDriverReported), i.phy);
                    field(tr(Key::ActiveRoleOsReport), radio_roles(i.active_role)); field(tr(Key::DriverReportedRadioState), message(i.radio));
                    field(tr(Key::WlanInterfaceTypeReported), message(i.wlan_interface_type)); field(tr(Key::AssociatedPhyReported), i.current_phy);
                    field(tr(Key::Value80211dSupported), reported(i.dot11d_capable)); field(tr(Key::ManagementFrameProtection), reported(i.mfp_capable));
                    field(tr(Key::LegacyHostedNetworkCapability), reported(i.hosted_network_capable));
                    if (i.desired_bssids) ImGui::Text(tr(Key::DesiredBssidListLimitU), *i.desired_bssids);
                    if (i.channel && *i.channel) ImGui::Text(tr(Key::CurrentChannelDriverReportedU), *i.channel);
                    if (!i.documented_radio.empty()) paragraph(std::string(tr(Key::DocumentedModel)) + message(i.documented_radio));
                    field(tr(Key::ObservedAssociation), i.bssid);
                    if (i.secondary_sta_reported) {
                        ImGui::Text(tr(Key::DualStaCount), i.secondary_sta.size());
                        for (const auto& secondary : i.secondary_sta) {
                            text(secondary.id); ImGui::SameLine(); ImGui::Text(tr(Key::DualStaState), secondary.state);
                        }
                    }
                    else if (i.secondary_sta_query) ImGui::Text(tr(Key::SecondaryStaUnknownNativeStatus), i.secondary_sta_query->code);
                    field(tr(Key::DualStaSync), reported(i.secondary_synchronized));
                    metric(tr(Key::Rssi), i.rssi ? std::optional<double>(*i.rssi) : std::nullopt, tr(Key::Dbm));
                    ImGui::Text(tr(Key::LinkRatesReportedRx1f), i.rx_mbps, i.tx_mbps);
                }
                w::end_card(); w::gap(space::sm);
                ImGui::PopID();
            }
            for (const auto& b : view.discovery.bss) {
                if (b.mhz >= 2400 && b.mhz <= 2500) observed_bands.insert(24);
                if (b.mhz >= 4900 && b.mhz < 5925) observed_bands.insert(5);
                if (b.mhz >= 5925 && b.mhz <= 7125) observed_bands.insert(6);
            }
            if (w::begin_card(tokens, "radio-summary", 0, true)) {
                for (const auto band : observed_bands) ImGui::Text(tr(Key::SGhzObservedInScan), band == 24 ? "2.4" : band == 5 ? "5" : "6");
                ImGui::Text(tr(Key::IdentifiedPhysicalWlanDevicesZu), physical.size(), associated.size());
                w::colored(tokens.warning.strong, tr(Key::PhysicalWirelessCapacityCeilingUnknown));
                if (view.overlapping_validated_windows) ImGui::Text(tr(Key::VerifiedPairedExperimentWindowsU), view.overlapping_validated_windows);
                else text(tr(Key::ConcurrentPhysicalTrialsNotVerified));
                paragraph(tr(Key::AWindowsSnapshotIsNot));
                paragraph(tr(Key::DualStaReadOnly));
            }
            w::end_card();
            break;
        }
        case Page::virtual_lab: {
            lab_header(Key::VirtualClientLab);
            w::colored(tokens.info.strong, tr(Key::SimulatedOfflineNoRadioClients));
            w::gap(space::sm);
            if (w::begin_card(tokens, "lab-config")) {
                ImGui::BeginDisabled(worker.busy());
                ImGui::PushItemWidth(std::min(w::px(260), ImGui::GetContentRegionAvail().x * 0.5f));
                number_input(tr(Key::LogicalClients), config.clients); number_input(tr(Key::LeaseSlots), config.pool); number_input(tr(Key::LeaseDurationSimulatedMs), config.lease_ms); number_input(tr(Key::Seed), config.seed);
                if (ImGui::BeginCombo(tr(Key::Scenario), message(sim::name(config.scenario)).c_str())) {
                    for (unsigned x = 0; x < static_cast<unsigned>(sim::Scenario::count); ++x) {
                        const auto option = static_cast<sim::Scenario>(x);
                        if (ImGui::Selectable(message(sim::name(option)).c_str(), option == config.scenario)) config.scenario = option;
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopItemWidth();
                ImGui::Checkbox(tr(Key::RecordScenarioAndCsvHistory), &export_csv);
                ImGui::BeginDisabled(!config.valid()); if (w::button(tokens, tr(Key::StartOfflineLab), w::Kind::primary)) { logical_selected = 0; logical_page = 1; worker.lab(config, false, export_csv); } ImGui::EndDisabled();
                ImGui::SameLine(); if (w::button(tokens, tr(Key::ReplayLastSavedSeed))) { logical_selected = 0; logical_page = 1; worker.lab(config, true, export_csv); }
                ImGui::SameLine(); if (w::button(tokens, tr(Key::ClearLogicalClients))) { logical_selected = 0; logical_page = 1; worker.clear_lab(); }
                ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::BeginDisabled(!worker.busy() || !view.status.starts_with(english(Key::SimulatedVirtualLab)));
                if (w::button(tokens, worker.paused() ? tr(Key::Resume) : tr(Key::Pause))) worker.pause();
                ImGui::EndDisabled();
            }
            w::end_card();
            w::gap(space::sm);
            if (w::begin_card(tokens, "lab-results")) {
                ImGui::Text(tr(Key::UClientsUTransitionsSimulated), view.client_count, view.events, view.simulated_ms);
                for (unsigned j = 0; j < view.counts.size(); ++j) if (view.counts[j]) { ImGui::TextColored(tokens.muted, "%s: %u", message(sim::name(static_cast<sim::State>(j))).c_str(), view.counts[j]); }
                const auto& clients = view.completed_clients ? *view.completed_clients : view.clients;
                constexpr unsigned page_size = 120;
                const auto pages_total = std::max(1u, (static_cast<unsigned>(clients.size()) + page_size - 1) / page_size);
                if (pages_total > 1) { ImGui::SetNextItemWidth(w::px(140)); number_input(tr(Key::ClientPage), logical_page); ImGui::SameLine(); ImGui::Text("/ %u", pages_total); }
                else if (worker.busy() && !view.clients.empty()) text(tr(Key::LivePreviewAllClientsAre));
                logical_page = std::clamp(logical_page, 1u, pages_total);
                if (logical_selected && logical_selected <= clients.size()) {
                    const auto& cl = clients[logical_selected - 1];
                    w::section(tokens, tr(Key::SelectedLogicalClientHistory));
                    ImGui::Text(tr(Key::ClientUUDiscoverAttempts), cl.id, cl.discovers, cl.leases);
                    for (unsigned i = 0; i < cl.history_size; ++i) ImGui::TextColored(tokens.muted, "%6u ms  %s", cl.history[i].at_ms, message(sim::name(cl.history[i].state)).c_str());
                }
                if (ImGui::BeginTable("clients", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, {0, 295 * dpi})) {
                    for (const auto* h : {tr(Key::LogicalId), tr(Key::State), tr(Key::LeaseCount), tr(Key::LatencySimMs), tr(Key::LossSim), tr(Key::RssiSimDbm)}) ImGui::TableSetupColumn(h);
                    ImGui::TableSetupScrollFreeze(0, 1);
                    ImGui::TableHeadersRow();
                    const auto first = (logical_page - 1) * page_size;
                    for (auto i = first; i < std::min<std::size_t>(first + page_size, clients.size()); ++i) {
                        const auto& cl = clients[i];
                        ImGui::TableNextRow(); ImGui::TableNextColumn();
                        if (ImGui::Selectable(std::to_string(cl.id).c_str(), logical_selected == cl.id)) logical_selected = cl.id;
                        ImGui::TableNextColumn(); text(message(sim::name(cl.state)));
                        ImGui::TableNextColumn(); ImGui::Text("%u", cl.leases); ImGui::TableNextColumn(); ImGui::Text("%.1f", cl.metrics.latency_ms);
                        ImGui::TableNextColumn(); ImGui::Text("%.1f", cl.metrics.loss_percent); ImGui::TableNextColumn();
                        if (cl.metrics.rssi_dbm < 0) ImGui::Text("%.0f", cl.metrics.rssi_dbm); else text(tr(Key::NA));
                    }
                    ImGui::EndTable();
                }
            }
            w::end_card();
            break;
        }
        case Page::discovery:
            lab_header(Key::NetworkDiscovery);
            ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::ScanWindowsWlan), w::Kind::primary)) { selected_bss = -1; consent = false; batch.clear(); worker.scan(); } ImGui::EndDisabled();
#ifdef _WIN32
            ImGui::SameLine(); if (w::button(tokens, tr(Key::WifiSettings))) open_windows_wifi();
            if (wifi_settings_failed) paragraph(tr(Key::WifiSettingsFailed));
#endif
            w::caption(tokens, tr(Key::WifiGuidance));
            if (!worker.busy() && view.discovery.bss.empty()) paragraph(tr(Key::WifiEmpty));
            if (!fresh_scan && !view.discovery.bss.empty()) w::colored(tokens.warning.strong, tr(Key::ScanExpiredRefreshBeforeConnecting));
            errors(view.discovery.errors);
            ImGui::SetNextItemWidth(std::min(w::px(320), ImGui::GetContentRegionAvail().x));
            discovery_filter.Draw(tr(Key::FilterSsidBssidOrProfile));
            if (ImGui::BeginTable("bss", 7, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, {0, std::max(w::px(240), ImGui::GetContentRegionAvail().y - w::px(8))})) {
                for (const auto* h : {tr(Key::Ssid), tr(Key::Bssid), tr(Key::Signal), tr(Key::MhzChannel), tr(Key::SecurityEvidence), tr(Key::WindowsProfile), tr(Key::ActiveWirelessInterface)}) ImGui::TableSetupColumn(h);
                ImGui::TableSetupScrollFreeze(1, 1);
                ImGui::TableHeadersRow();
                std::vector<int> indices;
                for (unsigned j = 0; j < view.discovery.bss.size(); ++j) {
                    const auto& row = view.discovery.bss[j];
                    if (discovery_filter.PassFilter((net::display_ssid(row.ssid) + " " + row.bssid + " " + row.profile).c_str())) indices.push_back(static_cast<int>(j));
                }
                ImGuiListClipper clip; clip.Begin(static_cast<int>(indices.size()));
                while (clip.Step()) for (auto row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto j = indices[row]; const auto& b = view.discovery.bss[j];
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(j);
                    const auto label = b.ssid.empty() ? std::string(tr(Key::Hidden)) : net::display_ssid(b.ssid);
                    if (ImGui::Selectable(label.c_str(), selected_bss == j)) { selected_bss = j; consent = false; page = Page::trials; }
                    ImGui::PopID(); ImGui::TableNextColumn(); text(b.bssid); ImGui::TableNextColumn(); ImGui::Text("%d dBm", b.rssi);
                    ImGui::TableNextColumn(); ImGui::Text("%u / %u", b.mhz, b.channel); ImGui::TableNextColumn(); text(message(b.security)); ImGui::TableNextColumn(); text(b.profile);
                    ImGui::TableNextColumn();
                    const auto adapter = std::find_if(view.hardware.interfaces.begin(), view.hardware.interfaces.end(), [&](const auto& i) { return i.id == b.interface_id; });
                    text(adapter != view.hardware.interfaces.end() && !adapter->name.empty() ? adapter->name : b.interface_id);
                }
                ImGui::EndTable();
            } break;
        case Page::experiments:
            lab_header(Key::ExperimentCenter);
            if (w::begin_card(tokens, "scopes")) {
                paragraph(tr(Key::LoadTestScopeConfWith));
                ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::LoadTestScopeConf), w::Kind::primary)) { consent = false; batch.clear(); worker.load_scope(); } ImGui::EndDisabled();
                if (view.scopes.empty()) w::colored(tokens.warning.strong, tr(Key::NoActiveTestScope));
                for (const auto& scope : view.scopes) {
                    w::section(tokens, scope.bssid); field(tr(Key::WindowsProfile), scope.profile); field(tr(Key::Method), scope.method());
                    field(tr(Key::ControlledDnsName), scope.dns_name);
                    ImGui::Text(tr(Key::USBatchCooldownGateway), scope.cooldown_s, scope.require_gateway_reply ? tr(Key::Required) : tr(Key::Optional));
                }
                w::gap(space::sm);
                if (w::button(tokens, tr(Key::OpenVirtualClientLab), w::Kind::link)) page = Page::virtual_lab;
            }
            w::end_card();
            break;
        case Page::performance:
            lab_header(Key::PerformanceLab);
            if (w::begin_card(tokens, "perf")) {
            if (latest) {
                measurement_offset = std::clamp(measurement_offset, 0, static_cast<int>(view.candidates.size()) - 1);
                ImGui::SliderInt(tr(Key::PriorObservations), &measurement_offset, 0, static_cast<int>(view.candidates.size()) - 1);
                const auto& record = view.candidates[view.candidates.size() - 1 - measurement_offset];
                const auto& o = record.observation;
                field(tr(Key::Scope), record.method); field(tr(Key::Bssid), record.bss.bssid);
                field(tr(Key::HostValidation), record.host_validated ? tr(Key::Pass) : tr(Key::NotVerifiedNotEligibleFor));
                ImGui::TextColored(o.sample_complete ? tokens.success.strong : tokens.warning.strong, tr(Key::SIcmpRepliesUU), o.sample_complete ? tr(Key::CompleteTransfers) : tr(Key::InsufficientSamples), o.replies, o.attempts, o.bytes);
                metric(tr(Key::MeanRtt), o.latency_ms, tr(Key::Ms)); metric(tr(Key::MedianRtt), o.rtt_distribution.median, tr(Key::Ms)); metric(tr(Key::P95Rtt), o.rtt_distribution.p95, tr(Key::Ms));
                metric(tr(Key::MeanAbsJitter), o.jitter_ms, tr(Key::Ms)); metric(tr(Key::IcmpMisses), o.loss_percent, "%"); metric(tr(Key::DnsMedian), o.dns_ms, tr(Key::Ms));
                metric(tr(Key::GoodputRepeatability), o.repeatability, "%");
                if (!o.latency_history.empty()) ImGui::PlotLines(tr(Key::IcmpReplyRttMs), o.latency_history.data(), static_cast<int>(o.latency_history.size()), 0, nullptr, 0, 350, {0, 100});
                if (ImGui::BeginTable("transfers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, {0, 210 * dpi})) {
                    for (const auto* h : {tr(Key::PayloadBytes), tr(Key::CompletedTried), tr(Key::DownMedian), tr(Key::UpMedian), tr(Key::DownMinMax), tr(Key::DownP95MbS)}) ImGui::TableSetupColumn(h);
                    ImGui::TableHeadersRow();
                    for (const auto& series : o.transfers) {
                        const auto down = net::summarize(series.down), up = net::summarize(series.up);
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%u", series.bytes);
                        ImGui::TableNextColumn(); ImGui::Text("%u / %u", down.count, series.attempts);
                        ImGui::TableNextColumn(); if (down.median) ImGui::Text("%.2f", *down.median); else text(tr(Key::NA));
                        ImGui::TableNextColumn(); if (up.median) ImGui::Text("%.2f", *up.median); else text(tr(Key::NA));
                        ImGui::TableNextColumn(); if (down.minimum) ImGui::Text("%.2f / %.2f", *down.minimum, *down.maximum); else text(tr(Key::NA));
                        ImGui::TableNextColumn(); if (down.p95) ImGui::Text("%.2f", *down.p95); else text(tr(Key::NA));
                    }
                    ImGui::EndTable();
                }
                w::caption(tokens, tr(Key::Clp1LabGoodputIncludesProtocol));
                if (o.error) errors({*o.error});
            } else w::caption(tokens, tr(Key::NoAuthorizedEndpointObservationsYet));
            }
            w::end_card();
            break;
        case Page::intelligence: {
            lab_header(Key::IntelligenceEngine);
            const std::array labels{tr(Key::Reachability), tr(Key::Downstream), tr(Key::Rtt), tr(Key::IcmpMisses), tr(Key::Jitter)};
            if (w::begin_card(tokens, "weights")) {
                ImGui::PushItemWidth(std::min(w::px(220), ImGui::GetContentRegionAvail().x * 0.5f));
                for (unsigned x = 0; x < labels.size(); ++x) ImGui::InputDouble(labels[x], &weights[x], 1., 5., "%.0f");
                ImGui::PopItemWidth();
                w::caption(tokens, tr(Key::ScoringV2UsesMedianGoodput));
            }
            w::end_card();
            auto ranked = view.candidates;
            try { for (auto& cnd : ranked) cnd.rating = net::candidate_score(cnd, weights); }
            catch (const std::invalid_argument& e) { w::banner(tokens.warning, tokens, e.what()); break; }
            for (const auto& cnd : net::rank(std::move(ranked), net::Clock::now())) {
                w::gap(space::sm);
                ImGui::PushID(&cnd);
                if (w::begin_card(tokens, "cand")) {
                    w::heading(tokens, cnd.bss.bssid);
                    w::caption(tokens, cnd.method);
                    ImGui::Text(tr(Key::Value1f1000fCoverageU), cnd.rating.points, cnd.rating.coverage * 100., cnd.rating.samples, cnd.rating.eligible ? tr(Key::Eligible) : tr(Key::LowConfidenceStale));
                    for (unsigned x = 0; x < labels.size(); ++x) metric(labels[x], cnd.rating.components[x], "/ 100");
                }
                w::end_card();
                ImGui::PopID();
            }
            break;
        }
        case Page::trials: {
            lab_header(Key::LabTrialsTitle);
            if (w::begin_card(tokens, "trials")) {
                field(tr(Key::WifiNetwork), chosen ? net::display_ssid(chosen->ssid) : "");
                field(tr(Key::CurrentWindowsProfile), active ? active->profile : ""); field(tr(Key::SelectedBssid), chosen ? chosen->bssid : "");
                field(tr(Key::SelectedWindowsProfile), chosen ? chosen->profile : "");
                w::caption(tokens, tr(Key::WindowsObtainsItsOwnIp));
                if (!chosen) {
                    w::gap(space::xs);
                    if (w::button(tokens, tr(Key::NetworkDiscovery), w::Kind::link)) page = Page::discovery;
                }
                w::gap(space::sm);
                consent_box("##consent", tr(Key::IAuthorizeThisScopeAnd), &consent);
                const auto* chosen_scope = chosen ? authorized(*chosen) : nullptr;
                ImGui::BeginDisabled(worker.busy() || !consent || !fresh_scan || !chosen_scope);
                if (w::button(tokens, tr(Key::ConnectAndValidateSelectedProfile), w::Kind::primary)) { request({{*chosen_scope, *chosen}}); }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(worker.busy() || !fresh_scan || !chosen_scope || batch.size() >= 4 || (chosen && std::any_of(batch.begin(), batch.end(), [&](const auto& b) { return b.bssid == chosen->bssid && b.interface_id == chosen->interface_id; })));
                if (w::button(tokens, tr(Key::QueueSelectedCandidate))) { batch.push_back(*chosen); consent = false; }
                ImGui::EndDisabled();
                std::vector<net::Experiment> planned;
                if (batch.empty()) w::caption(tokens, tr(Key::NoQueuedCandidates));
                for (const auto& b : batch) {
                    if (auto* auth = authorized(b); auth && present(b)) planned.push_back({*auth, b});
                    ImGui::BulletText(tr(Key::SOnS), b.bssid.c_str(), b.interface_id.c_str());
                }
                ImGui::BeginDisabled(worker.busy() || batch.empty());
                if (w::button(tokens, tr(Key::ClearQueue))) { batch.clear(); consent = false; }
                ImGui::EndDisabled(); ImGui::SameLine();
                ImGui::BeginDisabled(worker.busy() || !consent || !fresh_scan || planned.empty() || planned.size() != batch.size());
                if (w::button(tokens, tr(Key::RunAuthorizedQueue))) { request(std::move(planned), true); }
                ImGui::EndDisabled();
                std::optional<net::Candidate> recommended;
                if (current && fresh_scan && authorized(current->bss) && authorized(current->bss)->method() == current->method) {
                    auto baseline = *current;
                    auto measured = view.candidates;
                    try {
                        baseline.rating = net::candidate_score(baseline, weights);
                        for (auto& cnd : measured) cnd.rating = net::candidate_score(cnd, weights);
                        for (const auto& cnd : net::rank(std::move(measured), now)) {
                            const auto* scope = authorized(cnd.bss);
                            if (scope && scope->method() == cnd.method && present(cnd.bss) &&
                                net::should_switch(baseline, cnd, now, view.last_handoff)) { recommended = cnd; break; }
                        }
                    } catch (const std::invalid_argument&) { paragraph(tr(Key::WeightsAreInvalidAssistedSelection)); }
                }
                ImGui::BeginDisabled(worker.busy() || !consent || !recommended);
                if (w::button(tokens, tr(Key::ApplyBestMeasuredImprovement))) { request({{*authorized(recommended->bss), recommended->bss}}); }
                ImGui::EndDisabled();
                w::caption(tokens, tr(Key::SingleRadioExperimentsAreSerialized));
                if (!view.wifi_history.empty()) {
                    w::gap(space::sm);
                    ImGui::Text(tr(Key::WifiHistoryCount), view.wifi_history.size());
                    w::caption(tokens, tr(Key::WifiHistoryInfo));
                    ImGui::BeginDisabled(worker.busy());
                    if (w::button(tokens, tr(Key::WifiHistoryClear))) worker.clear_wifi_history();
                    ImGui::EndDisabled();
                }
            }
            w::end_card();
            const std::string modal_title = std::string(tr(Key::ConfirmExperiment)) + "###physical_confirmation";
            ImGui::SetNextWindowSizeConstraints({std::min(450 * dpi, vp->WorkSize.x), 0}, {std::min(640 * dpi, vp->WorkSize.x), vp->WorkSize.y});
            if (ImGui::BeginPopupModal(modal_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                paragraph(tr(Key::DetailPhysicalTrial));
                for (const auto& trial : confirmation) field(tr(Key::SelectedBssid), trial.candidate.bssid);
                ImGui::BeginDisabled(!fresh_scan || worker.busy() || confirmation.empty());
                if (w::button(tokens, tr(Key::Proceed), w::Kind::primary)) {
                    worker.experiments(std::move(confirmation));
                    if (confirmation_queue) batch.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled(); ImGui::SameLine();
                if (w::button(tokens, tr(Key::Dismiss))) { confirmation.clear(); ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            for (const auto& outcome : view.outcomes) {
                w::gap(space::xs);
                field(outcome.bssid.c_str(), message(outcome.status));
                if (!outcome.detail.empty()) paragraph(message(outcome.detail));
            }
            break;
        }
        case Page::settings: {
            page_header(Key::Settings);
            if (w::begin_card(tokens, "appearance")) {
                w::heading(tokens, tr(Key::AppearanceCard));
                if (w::begin_fields("prefs", 190)) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); w::colored(tokens.muted, tr(Key::Language), false);
                    ImGui::TableNextColumn(); ImGui::PushID("settings"); language_control(prefs); ImGui::PopID();
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); w::colored(tokens.muted, tr(Key::Theme), false);
                    ImGui::TableNextColumn(); theme_control(prefs);
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); w::colored(tokens.muted, tr(Key::Typeface), false);
                    ImGui::TableNextColumn();
                    {
                        const std::array faces{"Inter", "JetBrains Mono", tr(Key::FallbackFont)};
                        ImGui::SetNextItemWidth(w::px(220));
                        if (ImGui::BeginCombo("##typeface", faces[static_cast<unsigned>(prefs.font)])) {
                            for (unsigned i = 0; i < faces.size(); ++i)
                                if (ImGui::Selectable(faces[i], i == static_cast<unsigned>(prefs.font))) prefs.font = static_cast<Typeface>(i);
                            ImGui::EndCombo();
                        }
                    }
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); w::colored(tokens.muted, tr(Key::FontSize), false);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(w::px(220)); ImGui::SliderInt("##size", &prefs.size, 13, 24, "%d px", ImGuiSliderFlags_AlwaysClamp);
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); w::colored(tokens.muted, tr(Key::CompactMode), false);
                    ImGui::TableNextColumn(); ImGui::Checkbox("##compact", &prefs.compact);
                    w::end_fields();
                }
                w::gap(space::sm);
                if (w::button(tokens, tr(Key::RestoreDisplay))) {
                    prefs = {};
                    reset_display = true;
                }
                if (!fonts.inter || !fonts.mono) w::banner(tokens.warning, tokens, tr(Key::FontUnavailable));
                w::section(tokens, tr(Key::TypographyPreview));
                ImGui::PushFont(fonts.choose(prefs.font), static_cast<float>(prefs.size)); paragraph(tr(Key::PreviewText)); ImGui::PopFont();
                w::gap(space::sm);
                w::caption(tokens, std::string(tr(Key::UserDataDirectory)) + " : " + worker.folder().string());
            }
            w::end_card();
            w::gap(space::md);
            if (w::begin_card(tokens, "capacity")) {
                w::heading(tokens, tr(Key::SimulationCard));
                w::caption(tokens, tr(Key::LogicalSimulationCapacityNotRadio));
                ImGui::PushItemWidth(std::min(w::px(220), ImGui::GetContentRegionAvail().x * 0.5f));
                number_input(tr(Key::MaximumLogicalClients), budget.max_clients); number_input(tr(Key::RssGrowthBudgetMib), budget.memory_mib);
                number_input(tr(Key::WallTimeBudgetMs), budget.wall_ms); number_input(tr(Key::ProcessCpuBudgetMs), budget.cpu_ms);
                ImGui::PopItemWidth();
                ImGui::BeginDisabled(worker.busy()); if (w::button(tokens, tr(Key::MeasureLocalSimulationCapacity))) worker.profile(budget); ImGui::EndDisabled();
                if (view.capacity) {
                    ImGui::Text(tr(Key::StopSLargestCompletedConfiguration), message(sim::name(view.capacity->end)).c_str(), view.capacity->completed_clients());
                    ImGui::PushFont(fonts.choose(Typeface::mono), ImGui::GetStyle().FontSizeBase * 0.9f);
                    for (const auto& s : view.capacity->samples) ImGui::Text(tr(Key::Value6u7uEvents2fMs),
                        s.clients, s.events, s.elapsed_ms, s.cpu_ms, s.p95_us, s.sampled_peak_rss / 1048576., s.sampled_growth / 1048576., s.within_budget ? tr(Key::WithinBudget) : tr(Key::Stopped));
                    ImGui::PopFont();
                }
            }
            w::end_card();
            break;
        }
        case Page::about: page_header(Key::About); about(); break;
        case Page::paths: {
            lab_header(Key::PathSpace);
            if (w::begin_card(tokens, "path-measure")) {
                w::banner(tokens.info, tokens, tr(Key::PathNotDeployed));
                w::gap(space::xs);
                w::caption(tokens, tr(Key::PathScopeHelp));
                consent_box("##path-consent", tr(Key::PathConsent), &path_consent);
                if (view.path_error) errors({*view.path_error});
                w::section(tokens, tr(Key::PathInventory));
                ImGui::BeginDisabled(worker.busy());
                if (w::button(tokens, tr(Key::RefreshHardware))) worker.inspect();
                ImGui::EndDisabled();
                for (const auto& i : view.hardware.interfaces) {
                    if (!i.hardware_interface && !i.wireless) continue;
                    w::gap(space::xs);
                    w::heading(tokens, i.name.empty() ? i.id : i.name);
                    if (w::begin_fields(("pf" + i.id).c_str(), 170)) {
                        w::field(tokens, tr(Key::InterfaceId), i.id);
                        w::field(tokens, tr(Key::Ipv4), i.address.empty() ? tr(Key::Unknown) : i.address);
                        w::field(tokens, tr(Key::Gateway), i.gateway.empty() ? tr(Key::Unknown) : i.gateway);
                        if (i.wireless) w::field(tokens, tr(Key::AssociatedSsid), i.ssid.empty() ? tr(Key::Unknown) : net::display_ssid(i.ssid));
                        w::end_fields();
                    }
                    const net::Bss active_path{i.id, i.ssid, i.bssid, {}, i.profile};
                    if (const auto* scope = i.associated ? authorized(active_path) : nullptr) {
                        ImGui::PushID(i.id.c_str());
                        field(tr(Key::Method), scope->method());
                        ImGui::BeginDisabled(!path_consent || worker.busy());
                        if (w::button(tokens, tr(Key::PathMeasure), w::Kind::primary)) { path_consent = false; worker.measure_path(i.id, *scope); }
                        ImGui::EndDisabled(); ImGui::PopID();
                    }
                }
            }
            w::end_card();
            if (!view.paths.empty()) {
                w::gap(space::md);
                if (w::begin_card(tokens, "path-results")) {
                    w::heading(tokens, tr(Key::PathMeasurements));
                    for (auto it = view.paths.rbegin(); it != view.paths.rend(); ++it) {
                        const auto& record = *it;
                        w::section(tokens, record.bss.interface_id);
                        w::caption(tokens, record.method);
                        ImGui::Text(tr(Key::PathAge), static_cast<long long>(std::chrono::duration_cast<std::chrono::seconds>(now - record.observed).count()));
                        field(tr(Key::HostValidation), record.host_validated ? tr(Key::Pass) : tr(Key::Unconfirmed));
                        metric(tr(Key::MedianRtt), record.observation.rtt_distribution.median, tr(Key::Ms));
                        metric(tr(Key::P95Rtt), record.observation.rtt_distribution.p95, tr(Key::Ms));
                        metric(tr(Key::MeanAbsJitter), record.observation.jitter_ms, tr(Key::Ms));
                        metric(tr(Key::IcmpMisses), record.observation.loss_percent, "%");
                        metric(tr(Key::DownMedian), record.observation.down_mbps, "Mbit/s");
                    }
                }
                w::end_card();
            }
            w::gap(space::md);
            if (w::begin_card(tokens, "path-sim")) {
                w::heading(tokens, tr(Key::PathSimulated));
                constexpr std::array scenarios{Key::PathScenarioClean, Key::PathScenarioShared, Key::PathScenarioLoss};
                ImGui::BeginDisabled(worker.busy());
                ImGui::SetNextItemWidth(std::min(w::px(360), ImGui::GetContentRegionAvail().x));
                if (ImGui::BeginCombo("##path-scenario", tr(scenarios[static_cast<unsigned>(path_scenario)]))) {
                    for (unsigned n = 0; n < scenarios.size(); ++n)
                        if (ImGui::Selectable(tr(scenarios[n]), path_scenario == static_cast<int>(n))) path_scenario = static_cast<int>(n);
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (w::button(tokens, tr(Key::PathLabRun), w::Kind::primary)) worker.run_path_lab(path_scenario);
                ImGui::EndDisabled();
                if (view.path_lab) {
                    w::caption(tokens, tr(scenarios[static_cast<unsigned>(view.path_lab_scenario)]));
                    constexpr std::array names{Key::PathSingle, Key::PathRoundRobin, Key::PathEarliest};
                    for (std::size_t i = 0; i < names.size(); ++i) {
                        const auto& r = (*view.path_lab)[i];
                        w::section(tokens, tr(names[i]));
                        ImGui::Text(tr(Key::PathSimResult), r.mbps, r.elapsed_ms, r.completed, 128u, r.retries, r.reordered);
                        ImGui::Text(tr(Key::PathSimDistribution), r.delivered[0], r.delivered[1], r.peak_buffer);
                    }
                }
            }
            w::end_card();
            break;
        }
        case Page::count: break;
        }
        ImGui::PopStyleColor();
        ImGui::EndChild(); ImGui::PopID(); ImGui::End();
    }
};

// Deterministic sample data so that smoke captures can show every connection state.
// Used only by --ui-smoke; the pages say so with a visible badge.
View fixture_view(int state) {
    View v;
    const auto now = net::Clock::now();
    net::Interface wifi; wifi.id = "{fixture-wifi}"; wifi.name = "Wi-Fi"; wifi.description = "Intel(R) Wi-Fi 6 AX201 160MHz";
    wifi.wireless = wifi.hardware_interface = wifi.up = true; wifi.device_instance = "PCI\\FIXTURE";
    if (state != 0) {
        wifi.associated = true; wifi.ssid = "Campus"; wifi.profile = "Campus"; wifi.bssid = "02:00:00:00:00:a1";
        wifi.address = "192.0.2.211"; wifi.gateway = "192.0.2.1"; wifi.rssi = -61; wifi.channel = 44;
    } else wifi.wlan_idle = true;
    v.hardware.interfaces = {wifi};
    v.inspected_at = now - 4s;
    v.reach = {{wifi.id, state == 4 ? net::Reachability::portal : net::Reachability::internet}};
    if (state != 0) {
        const auto add = [&](std::string ssid, std::string bssid, int rssi, unsigned mhz, unsigned ch, bool secured, std::string profile) {
            v.discovery.bss.push_back({wifi.id, std::move(ssid), std::move(bssid), secured ? "Privacy bit set (suite unverified)" : "No privacy bit",
                std::move(profile), rssi, mhz, ch});
        };
        add("Campus", "02:00:00:00:00:a1", -61, 5220, 44, true, "Campus");
        add("Campus", "02:00:00:00:00:a2", -67, 5500, 100, true, "Campus");
        add("Campus", "02:00:00:00:00:a3", -74, 2437, 6, true, "Campus");
        add("Biblioth\\xc3\\xa8que", "02:00:00:00:00:b1", -58, 5180, 36, true, "Biblioth\xC3\xA8que");
        add("Caf\\xc3\\xa9 \\xc3\\x89tudiant", "02:00:00:00:00:c1", -70, 2462, 11, false, "");
        add("Lab-Recherche", "02:00:00:00:00:d1", -82, 5745, 149, true, "");
        add("", "02:00:00:00:00:e1", -88, 2412, 1, true, "");
        v.scan_at = now - 25s;
    }
    if (state == 2) v.wifi_progress = net::WifiProgress{net::WifiStage::obtaining_address, 1, 3, "02:00:00:00:00:a1"}, v.wifi_mode = net::WifiMode::assisted;
    if (state >= 3) {
        net::WifiResult r;
        net::WifiAttempt a{v.discovery.bss[0], {}, {}, net::WifiReason::selected};
        a.timing.association = 1840ms; a.timing.usable = 6420ms;
        r.link = wifi; r.elapsed = 9100ms;
        switch (state) {
        case 3: r.status = net::WifiStatus::internet_reported; a.timing.internet_reported = 8900ms; r.evidence = net::Reachability::internet; break;
        case 4: r.status = net::WifiStatus::portal; r.evidence = net::Reachability::portal; break;
        case 5: {
            r.status = net::WifiStatus::failed; r.link.reset(); r.restored = true;
            a.error = net::Error{"WlanConnect", 0x48002, "Windows reported that the connection did not complete", false,
                net::WifiFailure::authentication, 0x48002, "La clé réseau est incorrecte."};
            break;
        }
        default: r.status = net::WifiStatus::ip_pending; a.timing.usable.reset(); break;
        }
        r.attempts.push_back(std::move(a));
        v.wifi = std::move(r);
        v.wifi_mode = state == 5 ? net::WifiMode::normal : net::WifiMode::assisted;
    }
    return v;
}
}
int run(bool smoke, bool graphics) {
    if (SDL_GetVersion() != SDL_VERSION) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL runtime/header mismatch"); return 1; }
    if (smoke && !graphics && !SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE)) return 1;
    struct Quit { ~Quit() { SDL_Quit(); } } quit;
    if (!SDL_Init(SDL_INIT_VIDEO)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init(video): %s", SDL_GetError()); return 1; }
    auto* user_path = smoke ? nullptr : SDL_GetPrefPath("CAMPUSLINK", "CampusLink");
    const std::unique_ptr<char, decltype(&SDL_free)> folder(user_path, SDL_free);
    const auto data_folder = folder ? fs::path(std::u8string(reinterpret_cast<const char8_t*>(folder.get()))) : fs::path{};
    const auto pref_path = data_folder.empty() ? fs::path{} : data_folder / "display-v1.conf";
    const auto loaded = load_preferences(pref_path);
    auto prefs = loaded.value;
    const std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("CAMPUSLINK " CL_TEXT(CAMPUSLINK_VERSION), prefs.width, prefs.height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | (smoke && !graphics ? SDL_WINDOW_HIDDEN : 0)), SDL_DestroyWindow);
    if (!window) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow: %s", SDL_GetError()); return 1; }
    SDL_SetWindowMinimumSize(window.get(), 720, 480);
    ImGui::CreateContext();
    struct Context { ~Context() { ImGui::DestroyContext(); } } context;
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    Fonts fonts;
    fonts.load();
    if (smoke && (!fonts.inter || !fonts.mono)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Bundled fonts not found"); return 1; }
    Display display(window.get());
    if (!display.setup(smoke && !graphics) || (graphics && !display.gpu)) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Renderer: %s", SDL_GetError()); return 1; }
    Worker worker(data_folder);
    Screen screen;
    screen.unreadable_preferences = loaded.invalid;
    if (!smoke) worker.inspect();
    // Smoke: every page, then the connection page in each sample state (empty, list+selection,
    // progress, Internet, portal, failure, address pending), for four language/theme/size/DPI sets.
    constexpr auto panels = page_count;
    // CAMPUSLINK_CAPTURE_LIVE adds one connection page rendered from a real, read-only inventory and
    // scan of this machine (field evidence; never committed because it shows nearby networks).
    const bool live = smoke && !graphics && SDL_getenv("CAMPUSLINK_CAPTURE_LIVE");
    std::optional<View> live_view;
    if (live) {
        View v;
        v.hardware = net::platform().inspect({});
        v.discovery = net::platform().scan({});
        v.reach = reachability(v.hardware);
        v.scan_at = v.inspected_at = net::Clock::now();
        live_view = std::move(v);
    }
    const unsigned fixtures = 7 + (live ? 1u : 0u);
    const auto per_test = panels + fixtures;
    constexpr unsigned repeats = 3; // auto-sized cards settle after the first frame of new content
    const auto total_frames = 4 * per_test * repeats;
    constexpr std::array target_sizes{ImVec2{1360, 840}, ImVec2{1000, 700}, ImVec2{1440, 900}, ImVec2{820, 600}};
    constexpr std::array dpi_tests{1.f, 1.25f, 1.5f, 2.f};
    constexpr std::array capture_codes{"fr-light", "fr-dark", "en-light", "en-dark"};
    unsigned frames{};
    std::vector<double> frame_ms(total_frames);
    bool running = true;
    auto previous = prefs;
    const auto* capture_dir = smoke && !graphics ? SDL_getenv("CAMPUSLINK_CAPTURE_DIR") : nullptr;
    // Idle pacing: render while something changes, otherwise wait for input with a short timeout
    // so worker results and relative times still refresh. Never spin while minimized.
    unsigned settle_frames = 3;
    while (running) {
        const auto frame_start = smoke ? net::Clock::now() : net::Clock::time_point{};
        SDL_Event event{};
        if (!smoke) {
            const auto flags = SDL_GetWindowFlags(window.get());
            const bool hidden = flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED);
            const bool animate = worker.busy() || settle_frames > 0;
            if (hidden || !animate) {
                if (SDL_WaitEventTimeout(&event, hidden ? 500 : 250)) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                    if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get()))) running = false;
                    settle_frames = 3;
                }
                if (hidden && running) continue;
            }
        }
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            settle_frames = 3;
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get()))) running = false;
        }
        if (!running) break;
        const auto test = frames / (per_test * repeats);
        const auto step = frames / repeats;
        const bool last_repeat = frames % repeats == repeats - 1;
        float dpi = std::clamp(SDL_GetWindowDisplayScale(window.get()), 1.f, 3.f);
        int fixture_state = -1;
        if (smoke) {
            if (test >= dpi_tests.size()) return 1;
            if (frames % (per_test * repeats) == 0) {
                const auto size = target_sizes[test];
                if (!SDL_SetWindowSize(window.get(), static_cast<int>(size.x), static_cast<int>(size.y))) return 1;
            }
            prefs.language = test < 2 ? Language::fr : Language::en;
            prefs.theme = test % 2 == 0 ? Theme::light : Theme::dark;
            prefs.font = test == 2 ? Typeface::mono : Typeface::inter;
            prefs.size = test == 0 ? 16 : test == 1 ? 18 : test == 2 ? 15 : 13;
            prefs.compact = test >= 2;
            if (!graphics) dpi = dpi_tests[test]; // injected scale, not a Windows monitor observation
            const auto index = step % per_test;
            if (index < panels) { screen.page = static_cast<Page>(index); screen.fixture = false; screen.view = View{}; screen.seen_revision = 0; }
            else {
                fixture_state = static_cast<int>(index - panels);
                screen.page = Page::connect;
                if (fixture_state == 7 && live_view) {
                    screen.fixture = false; screen.view = *live_view; screen.selected_network.clear();
                    for (const auto& n : group_networks(screen.view)) if (n.saved) { screen.selected_network = n.key; break; }
                } else {
                    screen.fixture = true; screen.view = fixture_view(fixture_state);
                    screen.selected_network = fixture_state == 0 ? std::string{} : "Campus\nPrivacy bit set (suite unverified)";
                }
            }
        } else {
            int width{}, height{};
            if (SDL_GetWindowSize(window.get(), &width, &height)) {
                prefs.width = std::clamp(width, 720, 7680);
                prefs.height = std::clamp(height, 480, 4320);
            }
        }
        language = prefs.language; color_theme = prefs.theme;
        apply_appearance(prefs, dpi);
        io.FontDefault = fonts.choose(prefs.font);
        display.frame();
        ImGui::PushFont(io.FontDefault, static_cast<float>(prefs.size));
        if (smoke && frames == 0) {
            for (const auto face : {fonts.inter, fonts.mono, fonts.fallback}) {
                auto* baked = face->GetFontBaked(16);
                for (const auto code : {0x00e9, 0x00e0, 0x00e7, 0x0153, 0x2019, 0x00b7, 0x2026})
                    if (!baked->FindGlyphNoFallback(static_cast<ImWchar>(code))) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing French glyph U+%04x", static_cast<unsigned>(code)); return 1;
                    }
            }
        }
        screen.draw(worker, display, prefs, fonts);
        ImGui::PopFont();
        std::string screenshot;
        if (capture_dir && last_repeat) screenshot = std::string(capture_dir) + '/' + capture_codes[test] + '-' +
            (fixture_state == 7 ? std::string("connect-live") : fixture_state >= 0 ? "connect-" + std::to_string(fixture_state) : "page-" + std::to_string(static_cast<unsigned>(screen.page))) + ".bmp";
        if (!display.draw(screenshot.empty() ? nullptr : screenshot.c_str())) {
            if (smoke || !display.gpu) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rendering failed: %s", SDL_GetError()); return 1; }
            display.shutdown(); display.warning = english(Key::VulkanRenderingFailedUsingSdl);
            if (!display.setup(true)) return 1;
        }
        if (settle_frames) --settle_frames;
        if (!smoke && (screen.reset_display || prefs != previous)) {
            if (screen.reset_display) {
                SDL_SetWindowSize(window.get(), prefs.width, prefs.height);
                screen.reset_display = false;
            }
            screen.unsaved_preferences = !save_preferences(pref_path, prefs);
            if (!screen.unsaved_preferences) screen.unreadable_preferences = false;
            previous = prefs;
            settle_frames = 3;
        }
        if (smoke) {
            frame_ms[frames++] = std::chrono::duration<double, std::milli>(net::Clock::now() - frame_start).count();
            if (frames == total_frames) { SDL_Event close{}; close.type = SDL_EVENT_QUIT; if (!SDL_PushEvent(&close)) return 1; }
        }
        if (!display.gpu && !smoke && (worker.busy() || settle_frames)) SDL_Delay(16);
    }
    worker.stop();
    if (smoke) {
        if (frames != total_frames) return 1;
        std::sort(frame_ms.begin(), frame_ms.end());
        std::printf("UI_SMOKE_FRAME_MS count=%u p50=%.3f p95=%.3f capture=%s\n", frames,
            frame_ms[(total_frames - 1) / 2], frame_ms[(95 * total_frames + 99) / 100 - 1], capture_dir ? "on" : "off");
    }
    return 0;
}
}
