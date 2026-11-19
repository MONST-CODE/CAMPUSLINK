#include "campuslink/ui.hpp"
#include "campuslink/build_info.hpp"
#include "campuslink/network.hpp"
#include "campuslink/simulation.hpp"
#include "campuslink/ui_text.hpp"
#include "appearance.hpp"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlgpu3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
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
thread_local Language language{Language::fr};
thread_local Theme color_theme{Theme::light};
const char* tr(Key key) { return translate(language, key); }
std::string message(std::string_view value) { return localize(language, value); }
namespace fs = std::filesystem;
struct Log { long long when; std::string level, module, event; unsigned code{}; };
struct View {
    std::string status{english(Key::Ready)};
    net::Snapshot hardware;
    net::Discovery discovery;
    std::vector<net::Scope> scopes;
    struct Outcome { std::string bssid, status, detail; };
    std::vector<Outcome> outcomes;
    std::vector<net::Candidate> candidates;
    std::vector<sim::Client> clients;
    std::shared_ptr<const std::vector<sim::Client>> completed_clients;
    std::optional<sim::Profile> capacity;
    std::array<unsigned, static_cast<unsigned>(sim::State::count)> counts{};
    std::vector<Log> logs;
    unsigned client_count{}, events{}, simulated_ms{}, overlapping_validated_windows{};
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
            const auto summary = data.interfaces.empty() ? english(Key::NoInterfacesReported) : english(Key::InventoryRefreshed);
            for (const auto& e : data.errors) log("WARN", "inventory", e.operation, e.code);
            update([&](View& v) { v.hardware = std::move(data); v.status = summary; });
        });
    }
    void scan() {
        run(english(Key::WindowsWlanDiscovery), [this](auto stop) {
            if (net::Clock::now() - last_scan_ < 10s) throw std::runtime_error(english(Key::DiscoveryCooldown10Seconds));
            last_scan_ = net::Clock::now();
            auto data = net::platform().scan(stop);
            for (const auto& e : data.errors) log("WARN", "discovery", e.operation, e.code);
            update([&](View& v) { v.discovery = std::move(data); v.scan_at = net::Clock::now(); v.status = english(Key::DiscoveryCompletedReviewNativeStatus); });
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
                if (!stop.stop_requested()) v.hardware = std::move(hardware);
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

void text(std::string_view s) { ImGui::TextUnformatted(s.data(), s.data() + s.size()); }
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
void paragraph(const std::string& s) { ImGui::PushTextWrapPos(0); text(s); ImGui::PopTextWrapPos(); }
void errors(const std::vector<net::Error>& list) {
    for (const auto& e : list) {
        ImGui::TextColored(palette(color_theme).caution, "%s [%u]", message(e.operation).c_str(), e.code);
        paragraph(message(e.detail));
    }
}
void field(const char* name, const std::string& v) { ImGui::Text("%s: %s", name, v.empty() ? tr(Key::Unknown) : v.c_str()); }
std::string reported(std::optional<bool> value) { return !value ? "" : *value ? tr(Key::Yes) : tr(Key::No); }
void metric(const char* name, std::optional<double> v, const char* unit) {
    if (v) ImGui::Text("%-18s %8.2f %s", name, *v, unit); else ImGui::Text(tr(Key::Value18sUnverified), name);
}
void number_input(const char* label, unsigned& v) { ImGui::InputScalar(label, ImGuiDataType_U32, &v); }
constexpr std::array page_keys{Key::Overview, Key::HardwareLab, Key::RadioCapabilities, Key::VirtualClientLab, Key::NetworkDiscovery,
    Key::ExperimentCenter, Key::PerformanceLab, Key::IntelligenceEngine, Key::ConnectionManager, Key::LogsDiagnostics, Key::Settings, Key::About};
void language_control(Preferences& prefs) {
    ImGui::SetNextItemWidth(76 * ImGui::GetStyle().FontScaleDpi);
    if (ImGui::BeginCombo("##language", prefs.language == Language::fr ? "FR" : "EN")) {
        if (ImGui::Selectable(tr(Key::UseFrench), prefs.language == Language::fr)) prefs.language = Language::fr;
        if (ImGui::Selectable(tr(Key::UseEnglish), prefs.language == Language::en)) prefs.language = Language::en;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("%s", tr(Key::Language));
}
void theme_control(Preferences& prefs) {
    if (ImGui::Button(tr(prefs.theme == Theme::light ? Key::Light : Key::Dark))) prefs.theme = prefs.theme == Theme::light ? Theme::dark : Theme::light;
    ImGui::SetItemTooltip("%s", tr(Key::Theme));
}
void metric_cards(unsigned a, unsigned b, unsigned c) {
    constexpr std::array labels{Key::InterfaceCount, Key::DiscoveredBss, Key::LogicalEvents};
    const std::array values{a, b, c};
    const auto cols = ImGui::GetContentRegionAvail().x < ImGui::GetFontSize() * 30 ? 1 : 3;
    if (ImGui::BeginTable("overview_metrics", cols, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
        for (std::size_t j = 0; j < values.size(); ++j) {
            ImGui::TableNextColumn(); ImGui::PushID(static_cast<int>(j));
            const auto height = ImGui::GetTextLineHeightWithSpacing() * 2.8f;
            if (ImGui::BeginChild("metric", {0, height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar)) {
                ImGui::TextDisabled("%s", tr(labels[j]));
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.45f);
                ImGui::TextColored(palette(color_theme).key, "%u", values[j]);
                ImGui::PopFont();
            }
            ImGui::EndChild(); ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
struct Screen {
    int page{}, selected_bss{-1};
    bool export_csv{}, consent{}, warnings_only{};
    bool unreadable_preferences{}, unsaved_preferences{};
    std::vector<net::Experiment> confirmation;
    bool confirmation_queue{};
    unsigned logical_selected{}, logical_page{1};
    std::vector<net::Bss> batch;
    int measurement_offset{};
    ImGuiTextFilter discovery_filter;

    sim::Config config;
    sim::Budget budget;
    std::array<double,5> weights{net::base_weights};
    net::Clock::time_point refresh{};
    View view;
    void request(std::vector<net::Experiment> experiments, bool from_queue = false) {
        confirmation_queue = from_queue;
        confirmation = std::move(experiments);
        ImGui::OpenPopup("###physical_confirmation");
        consent = false;
    }
    void about() {
        paragraph(tr(Key::AboutIntro));
        ImGui::SeparatorText(tr(Key::Modules));
        constexpr std::array descriptions{Key::ModuleHardware, Key::ModuleRadio, Key::ModuleVirtual, Key::ModuleDiscovery, Key::ModuleExperiment,
            Key::ModulePerformance, Key::ModuleIntelligence, Key::ModuleConnection, Key::ModuleDiagnostics};
        for (std::size_t i = 0; i < descriptions.size(); ++i) {
            ImGui::TextColored(palette(color_theme).key, "%s", tr(page_keys[i + 1]));
            paragraph(tr(descriptions[i]));
        }
        ImGui::SeparatorText(tr(Key::QuickStart));
        constexpr std::array order{0, 1, 2, 4, 3, 5, 6, 7, 9};
        constexpr std::array descriptions_quick{Key::GuideOverview, Key::GuideHardware, Key::GuideRadio, Key::GuideDiscovery, Key::GuideVirtual,
            Key::GuideExperiment, Key::GuidePerformance, Key::GuideIntelligence, Key::GuideDiagnostics};
        for (std::size_t i = 0; i < order.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            const auto label = std::to_string(i + 1) + ". " + tr(page_keys[order[i]]);
            if (ImGui::SmallButton(label.c_str())) page = order[i];
            paragraph(tr(descriptions_quick[i])); ImGui::PopID();
        }
        paragraph(tr(Key::SimulationVersusPhysical));
        ImGui::SeparatorText(tr(Key::BuildInformation));
        field(tr(Key::Product), "CAMPUSLINK"); field(tr(Key::Version), build::version); field(tr(Key::Build), build::revision);
        field(tr(Key::Architecture), build::architecture); field(tr(Key::Target), message(build::system));
        field(tr(Key::Configuration), message(build::configuration));
        field(tr(Key::Compiler), build::compiler);
        const auto v = SDL_GetVersion();
        field("SDL3", std::to_string(SDL_VERSIONNUM_MAJOR(v)) + '.' + std::to_string(SDL_VERSIONNUM_MINOR(v)) + '.' + std::to_string(SDL_VERSIONNUM_MICRO(v)));
        field("Dear ImGui", ImGui::GetVersion());
        ImGui::SeparatorText(tr(Key::References)); paragraph(tr(Key::Credits));
    }
    void draw(Worker& worker, const Display& display, Preferences& prefs, const Fonts& fonts) {
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
        const bool fresh_scan = view.scan_at != net::Clock::time_point{} && now - view.scan_at < 2min;
        const auto* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("CAMPUSLINK", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        const float dpi = ImGui::GetStyle().FontScaleDpi;
        const auto& tokens = palette(color_theme);
        const bool narrow = ImGui::GetContentRegionAvail().x < 950 * dpi;
        ImGui::BeginChild("Toolbar", {0, ImGui::GetFrameHeightWithSpacing() + 14 * dpi}, ImGuiChildFlags_Borders);
        ImGui::AlignTextToFramePadding(); ImGui::TextColored(tokens.key, "CAMPUSLINK");
        if (!narrow) { ImGui::SameLine(); ImGui::TextDisabled("%s", tr(Key::WorkspaceTagline)); }
        ImGui::SameLine(); language_control(prefs); ImGui::SameLine(); theme_control(prefs);
        ImGui::SameLine(); ImGui::BeginDisabled(!worker.busy());
        if (ImGui::Button(tr(Key::StopTask))) worker.stop();
        ImGui::EndDisabled(); ImGui::EndChild();
        if (!narrow) {
            ImGui::BeginChild("Navigation", {244 * dpi, 0}, ImGuiChildFlags_Borders);
            ImGui::TextDisabled("%s", tr(Key::Workspace));
            ImGui::Spacing();
            for (unsigned i = 0; i < page_keys.size(); ++i) {
                const auto label = std::string(tr(page_keys[i])) + "###nav" + std::to_string(i);
                if (ImGui::Selectable(label.c_str(), page == static_cast<int>(i), 0, {0, ImGui::GetFrameHeight()})) page = static_cast<int>(i);
            }
            ImGui::Spacing(); ImGui::Separator();
            ImGui::TextDisabled("v%s", build::version);
            ImGui::EndChild(); ImGui::SameLine();
        } else {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginCombo("##navigation", tr(page_keys[page]))) {
                for (unsigned i = 0; i < page_keys.size(); ++i)
                    if (ImGui::Selectable(tr(page_keys[i]), page == static_cast<int>(i))) page = static_cast<int>(i);
                ImGui::EndCombo();
            }
        }
        ImGui::PushID(page);
        ImGui::BeginChild("Panel", {0, 0}, ImGuiChildFlags_Borders);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.65f);
        ImGui::PushStyleColor(ImGuiCol_Text, tokens.key);
        paragraph(tr(page_keys[page]));
        ImGui::PopStyleColor(); ImGui::PopFont();
        if (page == 3) ImGui::TextDisabled("%s", tr(Key::SimulatedOfflineNoRadioClients));
        if (worker.busy()) ImGui::TextColored(tokens.caution, "%s", tr(Key::WorkerActive));
        paragraph(message(view.status));
        if (unreadable_preferences) paragraph(tr(Key::PreferencesInvalid));
        if (unsaved_preferences) paragraph(tr(Key::PreferencesWriteFailed));
        ImGui::Separator(); ImGui::Spacing();
        switch (page) {
        case 0:
            metric_cards(static_cast<unsigned>(view.hardware.interfaces.size()), static_cast<unsigned>(view.discovery.bss.size()), view.events);
            if (ImGui::Button(tr(Key::QuickStart))) page = 11;
            ImGui::Separator();
            field(tr(Key::Renderer), message(display.mode)); if (!display.warning.empty()) paragraph(message(display.warning));
            field(tr(Key::ActiveWirelessInterface), active ? active->description : "");
            field(tr(Key::AssociatedSsid), active ? active->ssid : "");
            field(tr(Key::AuthorizedApplicationEndpoint), current && current->host_validated && current->observation.endpoint_verified ? tr(Key::Verified) : "");
            metric(tr(Key::ConnectionScore), current && current->host_validated && current->rating.coverage ? std::optional<double>(current->rating.points) : std::nullopt, "/ 100");

            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::RefreshHardware))) worker.inspect(); ImGui::EndDisabled();
            if (view.hardware.interfaces.empty()) paragraph(tr(Key::WaitingForInventory));
            errors(view.hardware.errors); break;
        case 1:
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::RefreshHardware))) worker.inspect(); ImGui::EndDisabled();
            errors(view.hardware.errors);
            for (const auto& i : view.hardware.interfaces) {
                ImGui::PushID(i.id.c_str());
                if (ImGui::CollapsingHeader(i.name.empty() ? i.description.c_str() : i.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                    field(tr(Key::Description), i.description); field(tr(Key::InterfaceId), i.id); field(tr(Key::AdapterClass), message(i.kind)); field(tr(Key::PnpHardwareId), i.hardware_id); field(tr(Key::Manufacturer), i.manufacturer);
                    field(tr(Key::DriverProvider), i.provider); field(tr(Key::DriverVersion), i.driver); field(tr(Key::DriverDate), i.driver_date);
                    field(tr(Key::Ipv4), i.address); field(tr(Key::Gateway), i.gateway); field(tr(Key::DnsConfiguration), i.dns);
                    ImGui::Text(tr(Key::OperationalSAssociatedSDhcp), i.up ? tr(Key::Yes117) : tr(Key::No118), i.associated ? tr(Key::Yes117) : tr(Key::Unconfirmed), i.dhcp ? tr(Key::Reported) : tr(Key::NotReported));
                }
                ImGui::PopID();
            } break;
        case 2: {
            std::set<std::string> physical, associated;
            std::set<unsigned> observed_bands;
            for (const auto& i : view.hardware.interfaces) {
                if (!i.wireless) continue;
                if (i.hardware_interface && !i.device_instance.empty()) {
                    physical.insert(i.device_instance);
                    if (i.associated) associated.insert(i.device_instance);
                }
                ImGui::SeparatorText(i.description.c_str()); field(tr(Key::PhyDriverReported), i.phy);
                field(tr(Key::ActiveRoleOsReport), radio_roles(i.active_role)); field(tr(Key::DriverReportedRadioState), message(i.radio));
                field(tr(Key::WlanInterfaceTypeReported), message(i.wlan_interface_type)); field(tr(Key::AssociatedPhyReported), i.current_phy);
                field(tr(Key::Value80211dSupported), reported(i.dot11d_capable)); field(tr(Key::ManagementFrameProtection), reported(i.mfp_capable));
                field(tr(Key::LegacyHostedNetworkCapability), reported(i.hosted_network_capable));
                if (i.desired_bssids) ImGui::Text(tr(Key::DesiredBssidListLimitU), *i.desired_bssids);
                if (i.channel && *i.channel) ImGui::Text(tr(Key::CurrentChannelDriverReportedU), *i.channel);
                if (!i.documented_radio.empty()) paragraph(std::string(tr(Key::DocumentedModel)) + message(i.documented_radio));
                field(tr(Key::ObservedAssociation), i.bssid);
                if (i.secondary_sta_reported) ImGui::Text(tr(Key::SecondaryStaQueryReturnedU), i.secondary_sta_bytes);
                else if (i.secondary_sta_query) ImGui::Text(tr(Key::SecondaryStaUnknownNativeStatus), i.secondary_sta_query->code);
                metric(tr(Key::Rssi), i.rssi ? std::optional<double>(*i.rssi) : std::nullopt, tr(Key::Dbm));
                ImGui::Text(tr(Key::LinkRatesReportedRx1f), i.rx_mbps, i.tx_mbps);
            }
            for (const auto& b : view.discovery.bss) {
                if (b.mhz >= 2400 && b.mhz <= 2500) observed_bands.insert(24);
                if (b.mhz >= 4900 && b.mhz < 5925) observed_bands.insert(5);
                if (b.mhz >= 5925 && b.mhz <= 7125) observed_bands.insert(6);
            }
            for (const auto band : observed_bands) ImGui::Text(tr(Key::SGhzObservedInScan), band == 24 ? "2.4" : band == 5 ? "5" : "6");
            ImGui::Text(tr(Key::IdentifiedPhysicalWlanDevicesZu), physical.size(), associated.size());
            ImGui::TextColored(palette(color_theme).caution, "%s", tr(Key::PhysicalWirelessCapacityCeilingUnknown));
            if (view.overlapping_validated_windows) ImGui::Text(tr(Key::VerifiedPairedExperimentWindowsU), view.overlapping_validated_windows);
            else text(tr(Key::ConcurrentPhysicalTrialsNotVerified));
            paragraph(tr(Key::AWindowsSnapshotIsNot));
            break;
        }
        case 3: {
            ImGui::BeginDisabled(worker.busy());
            number_input(tr(Key::LogicalClients), config.clients); number_input(tr(Key::LeaseSlots), config.pool); number_input(tr(Key::LeaseDurationSimulatedMs), config.lease_ms); number_input(tr(Key::Seed), config.seed);
            if (ImGui::BeginCombo(tr(Key::Scenario), message(sim::name(config.scenario)).c_str())) {
                for (unsigned x = 0; x < static_cast<unsigned>(sim::Scenario::count); ++x) {
                    const auto option = static_cast<sim::Scenario>(x);
                    if (ImGui::Selectable(message(sim::name(option)).c_str(), option == config.scenario)) config.scenario = option;
                }
                ImGui::EndCombo();
            }
            ImGui::Checkbox(tr(Key::RecordScenarioAndCsvHistory), &export_csv);
            ImGui::BeginDisabled(!config.valid()); if (ImGui::Button(tr(Key::StartOfflineLab))) { logical_selected = 0; logical_page = 1; worker.lab(config, false, export_csv); } ImGui::EndDisabled();
            ImGui::SameLine(); if (ImGui::Button(tr(Key::ReplayLastSavedSeed))) { logical_selected = 0; logical_page = 1; worker.lab(config, true, export_csv); }
            ImGui::SameLine(); if (ImGui::Button(tr(Key::ClearLogicalClients))) { logical_selected = 0; logical_page = 1; worker.clear_lab(); }
            ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::BeginDisabled(!worker.busy() || !view.status.starts_with(english(Key::SimulatedVirtualLab)));
            if (ImGui::Button(worker.paused() ? tr(Key::Resume) : tr(Key::Pause))) worker.pause();
            ImGui::EndDisabled();
            ImGui::Text(tr(Key::UClientsUTransitionsSimulated), view.client_count, view.events, view.simulated_ms);
            for (unsigned j = 0; j < view.counts.size(); ++j) if (view.counts[j]) { ImGui::Text("%s: %u", message(sim::name(static_cast<sim::State>(j))).c_str(), view.counts[j]); }
            const auto& clients = view.completed_clients ? *view.completed_clients : view.clients;
            constexpr unsigned page_size = 120;
            const auto pages_total = std::max(1u, (static_cast<unsigned>(clients.size()) + page_size - 1) / page_size);
            if (pages_total > 1) { number_input(tr(Key::ClientPage), logical_page); ImGui::SameLine(); ImGui::Text("/ %u", pages_total); }
            else if (worker.busy() && !view.clients.empty()) text(tr(Key::LivePreviewAllClientsAre));
            logical_page = std::clamp(logical_page, 1u, pages_total);
            if (logical_selected && logical_selected <= clients.size()) {
                const auto& c = clients[logical_selected - 1];
                ImGui::SeparatorText(tr(Key::SelectedLogicalClientHistory));
                ImGui::Text(tr(Key::ClientUUDiscoverAttempts), c.id, c.discovers, c.leases);
                for (unsigned i = 0; i < c.history_size; ++i) ImGui::Text("%6u ms  %s", c.history[i].at_ms, message(sim::name(c.history[i].state)).c_str());
            }
            if (ImGui::BeginTable("clients", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, {0, 295 * dpi})) {
                for (const auto* h : {tr(Key::LogicalId), tr(Key::State), tr(Key::LeaseCount), tr(Key::LatencySimMs), tr(Key::LossSim), tr(Key::RssiSimDbm)}) ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                const auto first = (logical_page - 1) * page_size;
                for (auto i = first; i < std::min<std::size_t>(first + page_size, clients.size()); ++i) {
                    const auto& c = clients[i];
                    ImGui::TableNextRow(); ImGui::TableNextColumn();
                    if (ImGui::Selectable(std::to_string(c.id).c_str(), logical_selected == c.id)) logical_selected = c.id;
                    ImGui::TableNextColumn(); text(message(sim::name(c.state)));
                    ImGui::TableNextColumn(); ImGui::Text("%u", c.leases); ImGui::TableNextColumn(); ImGui::Text("%.1f", c.metrics.latency_ms);
                    ImGui::TableNextColumn(); ImGui::Text("%.1f", c.metrics.loss_percent); ImGui::TableNextColumn();
                    if (c.metrics.rssi_dbm < 0) ImGui::Text("%.0f", c.metrics.rssi_dbm); else text(tr(Key::NA));
                }
                ImGui::EndTable();
            }
            break;
        }
        case 4:
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::ScanWindowsWlan))) { selected_bss = -1; consent = false; batch.clear(); worker.scan(); } ImGui::EndDisabled();
            paragraph(tr(Key::ExplicitScanNoConnectionIs));
            if (!fresh_scan && !view.discovery.bss.empty()) ImGui::TextColored(palette(color_theme).caution, "%s", tr(Key::ScanExpiredRefreshBeforeConnecting));
            errors(view.discovery.errors);
            discovery_filter.Draw(tr(Key::FilterSsidBssidOrProfile));
            if (ImGui::BeginTable("bss", 6, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit, {0, 295 * dpi})) {
                for (const auto* h : {tr(Key::Ssid), tr(Key::Bssid), tr(Key::Signal), tr(Key::MhzChannel), tr(Key::SecurityEvidence), tr(Key::WindowsProfile)}) ImGui::TableSetupColumn(h);
                ImGui::TableHeadersRow();
                std::vector<int> indices;
                for (unsigned j = 0; j < view.discovery.bss.size(); ++j) {
                    const auto& row = view.discovery.bss[j];
                    if (discovery_filter.PassFilter((row.ssid + " " + row.bssid + " " + row.profile).c_str())) indices.push_back(static_cast<int>(j));
                }
                ImGuiListClipper clip; clip.Begin(static_cast<int>(indices.size()));
                while (clip.Step()) for (auto row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
                    const auto j = indices[row]; const auto& b = view.discovery.bss[j];
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::PushID(j);
                    if (ImGui::Selectable(b.ssid.empty() ? tr(Key::Hidden) : b.ssid.c_str(), selected_bss == j)) { selected_bss = j; consent = false; page = 8; }
                    ImGui::PopID(); ImGui::TableNextColumn(); text(b.bssid); ImGui::TableNextColumn(); ImGui::Text("%d dBm", b.rssi);
                    ImGui::TableNextColumn(); ImGui::Text("%u / %u", b.mhz, b.channel); ImGui::TableNextColumn(); text(message(b.security)); ImGui::TableNextColumn(); text(b.profile);
                }
                ImGui::EndTable();
            } break;
        case 5:
            paragraph(tr(Key::LoadTestScopeConfWith));
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::LoadTestScopeConf))) { consent = false; batch.clear(); worker.load_scope(); } ImGui::EndDisabled();
            if (view.scopes.empty()) ImGui::TextColored(palette(color_theme).caution, "%s", tr(Key::NoActiveTestScope));
            for (const auto& scope : view.scopes) {
                ImGui::SeparatorText(scope.bssid.c_str()); field(tr(Key::WindowsProfile), scope.profile); field(tr(Key::Method), scope.method());
                field(tr(Key::ControlledDnsName), scope.dns_name);
                ImGui::Text(tr(Key::USBatchCooldownGateway), scope.cooldown_s, scope.require_gateway_reply ? tr(Key::Required) : tr(Key::Optional));
            }
            if (ImGui::Button(tr(Key::OpenVirtualClientLab))) page = 3;
            break;
        case 6:
            if (latest) {
                measurement_offset = std::clamp(measurement_offset, 0, static_cast<int>(view.candidates.size()) - 1);
                ImGui::SliderInt(tr(Key::PriorObservations), &measurement_offset, 0, static_cast<int>(view.candidates.size()) - 1);
                const auto& record = view.candidates[view.candidates.size() - 1 - measurement_offset];
                const auto& o = record.observation;
                field(tr(Key::Scope), record.method); field(tr(Key::Bssid), record.bss.bssid);
                field(tr(Key::HostValidation), record.host_validated ? tr(Key::Pass) : tr(Key::NotVerifiedNotEligibleFor));
                ImGui::TextColored(o.sample_complete ? palette(color_theme).key : palette(color_theme).caution, tr(Key::SIcmpRepliesUU), o.sample_complete ? tr(Key::CompleteTransfers) : tr(Key::InsufficientSamples), o.replies, o.attempts, o.bytes);
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
                paragraph(tr(Key::Clp1LabGoodputIncludesProtocol));
                if (o.error) errors({*o.error});
            } else ImGui::TextColored(palette(color_theme).muted, "%s", tr(Key::NoAuthorizedEndpointObservationsYet));
            break;
        case 7: {
            const std::array labels{tr(Key::Reachability), tr(Key::Downstream), tr(Key::Rtt), tr(Key::IcmpMisses), tr(Key::Jitter)};
            for (unsigned x = 0; x < labels.size(); ++x) ImGui::InputDouble(labels[x], &weights[x], 1., 5., "%.0f");
            auto ranked = view.candidates;
            try { for (auto& c : ranked) c.rating = net::candidate_score(c, weights); }
            catch (const std::invalid_argument& e) { paragraph(e.what()); break; }
            for (const auto& c : net::rank(std::move(ranked), net::Clock::now())) {
                ImGui::SeparatorText(c.bss.bssid.c_str());
                paragraph(c.method);
                ImGui::Text(tr(Key::Value1f1000fCoverageU), c.rating.points, c.rating.coverage * 100., c.rating.samples, c.rating.eligible ? tr(Key::Eligible) : tr(Key::LowConfidenceStale));
                for (unsigned x = 0; x < labels.size(); ++x) metric(labels[x], c.rating.components[x], "/ 100");
            }
            paragraph(tr(Key::ScoringV2UsesMedianGoodput));
            break;
        }
        case 8: {
            field(tr(Key::CurrentWindowsProfile), active ? active->profile : ""); field(tr(Key::SelectedBssid), chosen ? chosen->bssid : "");
            field(tr(Key::SelectedWindowsProfile), chosen ? chosen->profile : "");
            paragraph(tr(Key::WindowsObtainsItsOwnIp));
            ImGui::Checkbox(tr(Key::IAuthorizeThisScopeAnd), &consent);
            const auto* chosen_scope = chosen ? authorized(*chosen) : nullptr;
            ImGui::BeginDisabled(worker.busy() || !consent || !fresh_scan || !chosen_scope);
            if (ImGui::Button(tr(Key::ConnectAndValidateSelectedProfile))) { request({{*chosen_scope, *chosen}}); }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(worker.busy() || !fresh_scan || !chosen_scope || batch.size() >= 4 || (chosen && std::any_of(batch.begin(), batch.end(), [&](const auto& b) { return b.bssid == chosen->bssid && b.interface_id == chosen->interface_id; })));
            if (ImGui::Button(tr(Key::QueueSelectedCandidate))) { batch.push_back(*chosen); consent = false; }
            ImGui::EndDisabled();
            std::vector<net::Experiment> planned;
            if (batch.empty()) ImGui::TextDisabled("%s", tr(Key::NoQueuedCandidates));
            for (const auto& b : batch) {
                if (auto* auth = authorized(b); auth && present(b)) planned.push_back({*auth, b});
                ImGui::BulletText(tr(Key::SOnS), b.bssid.c_str(), b.interface_id.c_str());
            }
            ImGui::BeginDisabled(worker.busy() || batch.empty());
            if (ImGui::Button(tr(Key::ClearQueue))) { batch.clear(); consent = false; }
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(worker.busy() || !consent || !fresh_scan || planned.empty() || planned.size() != batch.size());
            if (ImGui::Button(tr(Key::RunAuthorizedQueue))) { request(std::move(planned), true); }
            ImGui::EndDisabled();
            std::optional<net::Candidate> recommended;
            if (current && fresh_scan && authorized(current->bss) && authorized(current->bss)->method() == current->method) {
                auto baseline = *current;
                auto measured = view.candidates;
                try {
                    baseline.rating = net::candidate_score(baseline, weights);
                    for (auto& c : measured) c.rating = net::candidate_score(c, weights);
                    for (const auto& c : net::rank(std::move(measured), now)) {
                        const auto* scope = authorized(c.bss);
                        if (scope && scope->method() == c.method && present(c.bss) &&
                            net::should_switch(baseline, c, now, view.last_handoff)) { recommended = c; break; }
                    }
                } catch (const std::invalid_argument&) { paragraph(tr(Key::WeightsAreInvalidAssistedSelection)); }
            }
            ImGui::BeginDisabled(worker.busy() || !consent || !recommended);
            if (ImGui::Button(tr(Key::ApplyBestMeasuredImprovement))) { request({{*authorized(recommended->bss), recommended->bss}}); }
            ImGui::EndDisabled();
            paragraph(tr(Key::SingleRadioExperimentsAreSerialized));
            const std::string modal_title = std::string(tr(Key::ConfirmExperiment)) + "###physical_confirmation";
            ImGui::SetNextWindowSizeConstraints({std::min(450 * dpi, vp->WorkSize.x), 0}, {std::min(640 * dpi, vp->WorkSize.x), vp->WorkSize.y});
            if (ImGui::BeginPopupModal(modal_title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                paragraph(tr(Key::DetailPhysicalTrial));
                for (const auto& trial : confirmation) field(tr(Key::SelectedBssid), trial.candidate.bssid);
                ImGui::BeginDisabled(!fresh_scan || worker.busy() || confirmation.empty());
                if (ImGui::Button(tr(Key::Proceed))) {
                    worker.experiments(std::move(confirmation));
                    if (confirmation_queue) batch.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled(); ImGui::SameLine();
                if (ImGui::Button(tr(Key::Dismiss))) { confirmation.clear(); ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            for (const auto& outcome : view.outcomes) {
                field(outcome.bssid.c_str(), message(outcome.status));
                if (!outcome.detail.empty()) paragraph(message(outcome.detail));
            }
            break;
        }
        case 9:
            for (const auto& d : net::diagnose(active, !view.discovery.bss.empty(), current ? &current->observation : nullptr)) {
                ImGui::Text("%-17s %s", message(d.step).c_str(), message(d.status).c_str()); paragraph(message(d.detail));
            }
            ImGui::SeparatorText(tr(Key::RecentWorkerEvents)); ImGui::Checkbox(tr(Key::WarningsAndErrorsOnly), &warnings_only);
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::ExportEventsTsv))) worker.export_logs(); ImGui::EndDisabled();
            ImGui::PushFont(fonts.choose(Typeface::mono), ImGui::GetStyle().FontSizeBase);
            for (auto it = view.logs.rbegin(); it != view.logs.rend(); ++it) {
                if (warnings_only && it->level == "INFO") continue;
                ImGui::Text("%lld | %s | %s | %s | %u", it->when, message(it->level).c_str(), message(it->module).c_str(), message(it->event).c_str(), it->code);
            }
            ImGui::PopFont(); break;
        case 10:
            ImGui::SeparatorText(tr(Key::DisplayPreferences));
            ImGui::TextUnformatted(tr(Key::Language)); ImGui::SameLine(); ImGui::PushID("settings"); language_control(prefs); ImGui::PopID();
            ImGui::TextUnformatted(tr(Key::Theme)); ImGui::SameLine(); theme_control(prefs);
            {
                const std::array faces{"Inter", "JetBrains Mono", tr(Key::FallbackFont)};
                if (ImGui::BeginCombo(tr(Key::Typeface), faces[static_cast<unsigned>(prefs.font)])) {
                    for (unsigned i = 0; i < faces.size(); ++i)
                        if (ImGui::Selectable(faces[i], i == static_cast<unsigned>(prefs.font))) prefs.font = static_cast<Typeface>(i);
                    ImGui::EndCombo();
                }
            }
            ImGui::SliderInt(tr(Key::FontSize), &prefs.size, 13, 24, "%d px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox(tr(Key::CompactMode), &prefs.compact);
            if (ImGui::Button(tr(Key::RestoreDisplay))) {
                const int width = prefs.width, height = prefs.height;
                prefs = {}; prefs.width = width; prefs.height = height; unreadable_preferences = false;
            }
            if (!fonts.inter || !fonts.mono) paragraph(tr(Key::FontUnavailable));
            ImGui::SeparatorText(tr(Key::TypographyPreview));
            ImGui::PushFont(fonts.choose(prefs.font), static_cast<float>(prefs.size)); paragraph(tr(Key::PreviewText)); ImGui::PopFont();
            ImGui::Separator();
            field(tr(Key::UserDataDirectory), worker.folder().string());
            number_input(tr(Key::MaximumLogicalClients), budget.max_clients); number_input(tr(Key::RssGrowthBudgetMib), budget.memory_mib);
            number_input(tr(Key::WallTimeBudgetMs), budget.wall_ms); number_input(tr(Key::ProcessCpuBudgetMs), budget.cpu_ms);
            ImGui::BeginDisabled(worker.busy()); if (ImGui::Button(tr(Key::MeasureLocalSimulationCapacity))) worker.profile(budget); ImGui::EndDisabled();
            ImGui::SeparatorText(tr(Key::LogicalSimulationCapacityNotRadio));
            if (view.capacity) {
                ImGui::Text(tr(Key::StopSLargestCompletedConfiguration), message(sim::name(view.capacity->end)).c_str(), view.capacity->completed_clients());
                for (const auto& s : view.capacity->samples) ImGui::Text(tr(Key::Value6u7uEvents2fMs),
                    s.clients, s.events, s.elapsed_ms, s.cpu_ms, s.p95_us, s.sampled_peak_rss / 1048576., s.sampled_growth / 1048576., s.within_budget ? tr(Key::WithinBudget) : tr(Key::Stopped));
            }
            break;
        case 11: about(); break;
        }
        ImGui::EndChild(); ImGui::PopID(); ImGui::End();
    }
};
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
    constexpr auto panels = static_cast<unsigned>(page_keys.size());
    constexpr auto total_frames = 4 * panels;
    constexpr std::array target_sizes{ImVec2{1360, 840}, ImVec2{1000, 700}, ImVec2{1440, 900}, ImVec2{820, 600}};
    constexpr std::array dpi_tests{1.f, 1.25f, 1.5f, 2.f};
    constexpr std::array capture_codes{"fr-light", "fr-dark", "en-light", "en-dark"};
    unsigned frames{};
    bool running = true;
    auto previous = prefs;
    const auto* capture_dir = smoke && !graphics ? SDL_getenv("CAMPUSLINK_CAPTURE_DIR") : nullptr;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window.get()))) running = false;
        }
        if (!running) break;
        const auto test = frames / panels;
        float dpi = std::clamp(SDL_GetWindowDisplayScale(window.get()), 1.f, 3.f);
        if (smoke) {
            if (test >= dpi_tests.size()) return 1;
            if (frames % panels == 0) {
                const auto size = target_sizes[test];
                if (!SDL_SetWindowSize(window.get(), static_cast<int>(size.x), static_cast<int>(size.y))) return 1;
            }
            prefs.language = test < 2 ? Language::fr : Language::en;
            prefs.theme = test % 2 == 0 ? Theme::light : Theme::dark;
            prefs.font = test == 2 ? Typeface::mono : Typeface::inter;
            prefs.size = test == 0 ? 16 : test == 1 ? 18 : test == 2 ? 15 : 13;
            prefs.compact = test >= 2;
            if (!graphics) dpi = dpi_tests[test]; // injected scale, not a Windows monitor observation
            screen.page = static_cast<int>(frames % panels);
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
                for (const auto code : {0x00e9, 0x00e0, 0x00e7, 0x0153, 0x2019})
                    if (!baked->FindGlyphNoFallback(static_cast<ImWchar>(code))) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Missing French glyph U+%04x", static_cast<unsigned>(code)); return 1;
                    }
            }
        }
        screen.draw(worker, display, prefs, fonts);
        ImGui::PopFont();
        std::string screenshot;
        if (capture_dir && (screen.page == 0 || screen.page == 3 || screen.page == 10 || screen.page == 11))
            screenshot = std::string(capture_dir) + '/' + capture_codes[test] + '-' + std::to_string(screen.page) + ".bmp";
        if (!display.draw(screenshot.empty() ? nullptr : screenshot.c_str())) {
            if (smoke || !display.gpu) { SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Rendering failed: %s", SDL_GetError()); return 1; }
            display.shutdown(); display.warning = english(Key::VulkanRenderingFailedUsingSdl);
            if (!display.setup(true)) return 1;
        }
        if (!smoke && prefs != previous) {
            screen.unsaved_preferences = !save_preferences(pref_path, prefs);
            previous = prefs;
        }
        if (smoke && ++frames == total_frames) { SDL_Event close{}; close.type = SDL_EVENT_QUIT; if (!SDL_PushEvent(&close)) return 1; }
        if (!display.gpu && !smoke) SDL_Delay(16);
    }
    worker.stop();
    if (smoke && frames != total_frames) return 1;
    return 0;
}
}
