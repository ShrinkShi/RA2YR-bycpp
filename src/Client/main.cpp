#include "GameData/Rules.h"
#include "GameData/Art.h"
#include "Renderer/D3D11Renderer.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ra2yr::client {
namespace {

constexpr float kLogicalWidth = 1920.0F;
constexpr float kLogicalHeight = 1080.0F;

struct RenderScaleConfig {
    float worldRenderScale;
    float unitVisualScale;
    float hudModelScale;
    float hudPortraitScale;
};

constexpr RenderScaleConfig kRenderScale{1.0F, 1.65F, 1.10F, 1.20F};
constexpr float kTileWidth = 44.0F * kRenderScale.worldRenderScale;
constexpr float kTileHeight = 22.0F * kRenderScale.worldRenderScale;

enum class AppMode {
    MainMenu,
    EditorSandbox,
};

enum class EditorTool {
    Terrain,
    Unit,
};

enum class PendingAction {
    None,
    Move,
    Patrol,
    AttackMove,
    Attack,
};

struct MenuButton {
    std::string key;
    std::string image;
    std::string hoverImage;
    Rect rect;
};

struct PerformanceTracker {
    std::deque<float> recentFrameTimesMs;
    float averageFps = 0.0F;
    float frameTimeMs = 0.0F;
    float p95FrameTimeMs = 0.0F;
    float simulationTimeMs = 0.0F;
    float renderCpuTimeMs = 0.0F;
    bool showOverlay = false;
};

enum class AudioCue {
    MenuHover,
    MenuClick,
    UnitSelect,
    UnitMove,
    UnitAttack,
};

class AudioService {
public:
    ~AudioService() { shutdown(); }

    bool initialize() {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = 48000;
        stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (stream_ == nullptr || !SDL_ResumeAudioStreamDevice(stream_)) {
            std::cerr << "[Audio] Disabled: " << SDL_GetError() << '\n';
            shutdown();
            return false;
        }
        std::cerr << "[Audio] Stream ready; procedural cues remain fallback\n";
        return true;
    }

    bool loadVoiceSet(const std::filesystem::path& configPath, std::string& error) {
        westwood::IniDocument config;
        if (!config.load(configPath, error)) {
            return false;
        }
        const std::pair<AudioCue, std::string_view> voices[] = {
            {AudioCue::UnitSelect, "VoiceSelect"},
            {AudioCue::UnitMove, "VoiceMove"},
            {AudioCue::UnitAttack, "VoiceAttack"},
        };
        for (const auto& [cue, section] : voices) {
            const std::string file = config.get(section, "File");
            if (file.empty()) {
                error = "Missing audio File in [" + std::string(section) + "]";
                return false;
            }
            std::vector<float> samples;
            const std::filesystem::path audioPath = configPath.parent_path() / file;
            if (!decodeWav(audioPath, samples, error)) {
                return false;
            }
            voiceSamples_[cue] = std::move(samples);
            std::cerr << "[Audio] Loaded voice " << section << ": " << audioPath.string() << '\n';
        }
        return true;
    }

    void shutdown() {
        if (stream_ != nullptr) {
            SDL_DestroyAudioStream(stream_);
            stream_ = nullptr;
        }
    }

    void play(AudioCue cue) {
        if (stream_ == nullptr) {
            return;
        }
        const auto voice = voiceSamples_.find(cue);
        if (voice != voiceSamples_.end() && !voice->second.empty()) {
            if (!SDL_PutAudioStreamData(stream_, voice->second.data(),
                static_cast<int>(voice->second.size() * sizeof(float)))) {
                std::cerr << "[Audio] Voice queue failed: " << SDL_GetError() << '\n';
            }
            return;
        }
        const CueSettings settings = settingsFor(cue);
        constexpr int sampleRate = 48000;
        const int sampleCount = std::max(1, static_cast<int>(settings.duration * sampleRate));
        std::vector<float> samples(static_cast<std::size_t>(sampleCount));
        constexpr float pi = 3.14159265358979323846F;
        for (int index = 0; index < sampleCount; ++index) {
            const float t = static_cast<float>(index) / static_cast<float>(sampleRate);
            const float attack = std::min(1.0F, static_cast<float>(index) / 160.0F);
            const float release = std::min(1.0F, static_cast<float>(sampleCount - index) / 900.0F);
            const float envelope = attack * release;
            samples[static_cast<std::size_t>(index)] = settings.volume * envelope *
                (std::sin(2.0F * pi * settings.frequency * t) +
                    0.30F * std::sin(2.0F * pi * settings.secondFrequency * t));
        }
        if (!SDL_PutAudioStreamData(stream_, samples.data(), static_cast<int>(samples.size() * sizeof(float)))) {
            std::cerr << "[Audio] Cue queue failed: " << SDL_GetError() << '\n';
        }
    }

private:
    struct CueSettings {
        float frequency;
        float secondFrequency;
        float duration;
        float volume;
    };

    static CueSettings settingsFor(AudioCue cue) {
        switch (cue) {
        case AudioCue::MenuHover: return {880.0F, 1320.0F, 0.045F, 0.10F};
        case AudioCue::MenuClick: return {220.0F, 440.0F, 0.095F, 0.16F};
        case AudioCue::UnitSelect: return {330.0F, 495.0F, 0.16F, 0.18F};
        case AudioCue::UnitMove: return {260.0F, 390.0F, 0.12F, 0.15F};
        case AudioCue::UnitAttack: return {120.0F, 180.0F, 0.20F, 0.20F};
        }
        return {440.0F, 660.0F, 0.10F, 0.12F};
    }

    static bool decodeWav(const std::filesystem::path& path, std::vector<float>& output, std::string& error) {
        SDL_AudioSpec spec{};
        Uint8* data = nullptr;
        Uint32 length = 0;
        const std::u8string nativePath = path.u8string();
        const std::string utf8Path(reinterpret_cast<const char*>(nativePath.data()), nativePath.size());
        if (!SDL_LoadWAV(utf8Path.c_str(), &spec, &data, &length)) {
            error = "Unable to load voice WAV " + path.string() + ": " + SDL_GetError();
            return false;
        }
        const std::size_t bytesPerSample = SDL_AUDIO_BYTESIZE(spec.format);
        const std::size_t channels = static_cast<std::size_t>(std::max(1, spec.channels));
        const std::size_t frameBytes = bytesPerSample * channels;
        if (spec.freq <= 0 || frameBytes == 0 || length < frameBytes) {
            SDL_free(data);
            error = "Unsupported or empty voice WAV " + path.string();
            return false;
        }

        const std::size_t sourceFrames = length / frameBytes;
        std::vector<float> mono(sourceFrames, 0.0F);
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t frame = 0; frame < sourceFrames; ++frame) {
            float sum = 0.0F;
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const std::size_t offset = frame * frameBytes + channel * bytesPerSample;
                float sample = 0.0F;
                if (spec.format == SDL_AUDIO_S16LE || spec.format == SDL_AUDIO_S16) {
                    const auto value = static_cast<std::int16_t>(
                        static_cast<std::uint16_t>(bytes[offset]) |
                        (static_cast<std::uint16_t>(bytes[offset + 1]) << 8));
                    sample = static_cast<float>(value) / 32768.0F;
                } else if (spec.format == SDL_AUDIO_F32LE || spec.format == SDL_AUDIO_F32) {
                    std::memcpy(&sample, bytes + offset, sizeof(float));
                } else if (spec.format == SDL_AUDIO_U8) {
                    sample = (static_cast<float>(bytes[offset]) - 128.0F) / 128.0F;
                } else {
                    SDL_free(data);
                    error = "Unsupported voice WAV format in " + path.string();
                    return false;
                }
                sum += sample;
            }
            mono[frame] = std::clamp(sum / static_cast<float>(channels), -1.0F, 1.0F);
        }
        SDL_free(data);

        const std::size_t targetFrames = std::max<std::size_t>(1, static_cast<std::size_t>(
            std::llround(static_cast<double>(sourceFrames) * 48000.0 / static_cast<double>(spec.freq))));
        output.resize(targetFrames);
        for (std::size_t frame = 0; frame < targetFrames; ++frame) {
            if (sourceFrames == 1 || targetFrames == 1) {
                output[frame] = mono.front();
                continue;
            }
            const double sourcePosition = static_cast<double>(frame) * static_cast<double>(sourceFrames - 1) /
                static_cast<double>(targetFrames - 1);
            const auto lower = static_cast<std::size_t>(sourcePosition);
            const auto upper = std::min(sourceFrames - 1, lower + 1);
            const float blend = static_cast<float>(sourcePosition - static_cast<double>(lower));
            output[frame] = mono[lower] * (1.0F - blend) + mono[upper] * blend;
        }
        return true;
    }

    SDL_AudioStream* stream_ = nullptr;
    std::unordered_map<AudioCue, std::vector<float>> voiceSamples_;
};

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) {
        return std::wstring(value.begin(), value.end());
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

class ClientApp {
public:
    bool initialize(std::string& error, bool stressEntities) {
        SDL_SetAppMetadata("RA2YR-bycpp", "0.1.0", "com.shrinkshi.ra2yr-bycpp");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO)) {
            error = SDL_GetError();
            return false;
        }
        window_ = SDL_CreateWindow("RA2YR-bycpp - Editor Sandbox", 1280, 720,
            SDL_WINDOW_RESIZABLE);
        if (window_ == nullptr) {
            error = SDL_GetError();
            return false;
        }
        audio_.initialize();
        if (!renderer_.initialize(window_, error)) {
            return false;
        }
        contentRoot_ = executableDirectory();
        std::string audioError;
        if (!audio_.loadVoiceSet(contentRoot_ / "assets/audio/voices.ini", audioError)) {
            std::cerr << "[Audio] Voice set unavailable; using procedural fallback: " << audioError << '\n';
        }
        loadStrings();
        if (!loadRuntimeAssets(error)) {
            return false;
        }
        if (!buildTerrain(error)) {
            return false;
        }
        const gamedata::ArtDefinition* animationDefinition = art_.find(rules_.e2().image);
        if (animationDefinition == nullptr) {
            error = "Art.ini has no definition for the E2 image " + rules_.e2().image;
            return false;
        }
        simulation_ = std::make_unique<simulation::Simulation>(*animationDefinition);
        // Keep the sample forces inside the initial camera framing so the
        // first editor run visibly demonstrates both owner colors.
        simulation_->spawn(rules_.e2(), Owner::Red, {8, 12});
        simulation_->spawn(rules_.e2(), Owner::Red, {12, 10});
        simulation_->spawn(rules_.e2(), Owner::Blue, {20, 8});
        simulation_->spawn(rules_.e2(), Owner::Blue, {16, 12});
        if (stressEntities) {
            for (int index = 0; index < 100; ++index) {
                const Owner owner = index < 50 ? Owner::Red : Owner::Blue;
                const int localIndex = index % 50;
                simulation_->spawn(rules_.e2(), owner,
                    {16 + (localIndex % 10) * 2, 18 + (localIndex / 10) * 2});
            }
        }
        return true;
    }

    int run() {
        auto previous = std::chrono::steady_clock::now();
        double simulationAccumulator = 0.0;
        constexpr double kSimulationStep = 1.0 / 60.0;
        while (running_) {
            const auto frameStart = std::chrono::steady_clock::now();
            const auto now = std::chrono::steady_clock::now();
            const double seconds = std::min(0.25, std::chrono::duration<double>(now - previous).count());
            previous = now;
            simulationAccumulator += seconds;
            processEvents();
            const auto simulationStart = std::chrono::steady_clock::now();
            if (mode_ == AppMode::EditorSandbox) {
                while (simulationAccumulator >= kSimulationStep) {
                    simulation_->update(static_cast<float>(kSimulationStep));
                    playSimulationAudio();
                    simulationAccumulator -= kSimulationStep;
                }
            }
            const float simulationMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - simulationStart).count();
            const auto renderStart = std::chrono::steady_clock::now();
            render();
            const float renderMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - renderStart).count();
            recordPerformance(std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - frameStart).count(), simulationMs, renderMs);
        }
        return 0;
    }

    ~ClientApp() {
        simulation_.reset();
        audio_.shutdown();
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
        }
        SDL_Quit();
    }

private:
    static std::filesystem::path executableDirectory() {
        const char* basePath = SDL_GetBasePath();
        if (basePath == nullptr) {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(utf8ToWide(basePath));
    }

    void loadStrings() {
        strings_ = {
            {"title", L"COMMAND & CONQUER"}, {"subtitle", L"YURI'S REVENGE"},
            {"campaign", L"CAMPAIGN"}, {"load", L"LOAD GAME"}, {"skirmish", L"SKIRMISH"},
            {"online", L"ONLINE"}, {"lan", L"LAN"}, {"settings", L"SETTINGS"},
            {"statistics", L"STATISTICS"}, {"editor", L"MAP EDITOR"}, {"exit", L"EXIT GAME"},
            {"unimplemented", L"NOT IMPLEMENTED"}, {"editor_title", L"EDITOR SANDBOX"},
            {"editor_hint", L"左键选择 / 拖框选择   右键移动或攻击   M 移动  S 停止  H 原地不动  P 巡逻  A 攻击移动"},
            {"red", L"RED"}, {"blue", L"BLUE"}, {"place_red", L"PLACE RED E2"}, {"place_blue", L"PLACE BLUE E2"},
            {"cancel_place", L"CANCEL PLACEMENT"}, {"strategic", L"战略能力"}, {"production", L"生产栏"},
            {"building", L"BUILDING"}, {"defense", L"DEFENSE"}, {"infantry", L"INFANTRY"}, {"vehicles", L"VEHICLES"},
            {"unit_name", L"动员兵"}, {"owner", L"所有者"}, {"health", L"生命值"},
            {"weapon", L"武器"}, {"weapon_name", L"M1卡宾枪"}, {"armor", L"护甲"}, {"armor_name", L"轻型装甲"},
             {"move", L"移动"}, {"stop", L"停止"}, {"guard", L"警戒"}, {"attack", L"攻击"},
             {"deploy", L"部署"}, {"hold", L"原地不动"}, {"patrol", L"巡逻"}, {"repair", L"维修"},
             {"waypoint", L"路径点"}, {"attack_move", L"攻击/攻击移动"},
             {"minimap", L"小地图"}, {"unit_model", L"单位模型"}, {"unit_info", L"单位信息"},
             {"portrait", L"头像 / 动画预览"}, {"producer", L"产能建筑"}, {"tool", L"工具"},
             {"terrain", L"Terrain"}, {"unit", L"Unit"}, {"object", L"对象"}, {"mode", L"模式"},
             {"select", L"选择"}, {"place", L"放置"}, {"collapse", L"收起"}, {"expand", L"展开"},
             {"animation_idle", L"待机"}, {"animation_walk", L"行走"}, {"animation_attack", L"攻击"},
            {"animation_death", L"死亡"},
            {"runtime", L"SIMULATION ONLINE"}, {"asset_missing", L"RUNTIME ASSETS NOT FOUND"},
        };
        westwood::IniDocument document;
        std::string error;
        if (document.load(contentRoot_ / "assets/ui/strings.ini", error)) {
            for (auto& [key, value] : strings_) {
                const std::wstring localized = utf8ToWide(document.get("ui", key, ""));
                if (!localized.empty()) {
                    value = localized;
                }
            }
        }
    }

    bool loadRuntimeAssets(std::string& error) {
        const std::filesystem::path rulesPath = contentRoot_ / "INI/Rules.ini";
        const std::filesystem::path artPath = contentRoot_ / "INI/Art.ini";
        const std::filesystem::path spritePath = contentRoot_ / "assets/game/ra2/infantry/CONS.SHP";
        const std::filesystem::path palettePath = contentRoot_ / "assets/game/ra2/palettes/unittem.pal";
        if (!rules_.load(rulesPath, error) || !art_.load(artPath, error) ||
            !palette_.load(palettePath, error) || !sprite_.load(spritePath, error) ||
            !renderer_.loadPalette("unittem", palette_, error) ||
            !renderer_.loadSpriteAsset("CONS", sprite_, error)) {
            assetError_ = utf8ToWide(error);
            std::cerr << "[Content][Error] " << error << '\n';
            return false;
        }
        const std::pair<std::string, std::filesystem::path> images[] = {
            {"ui.main.background", contentRoot_ / "assets/ui/ra2/mainmenu/crt_console.png"},
            {"ui.main.button", contentRoot_ / "assets/ui/ra2/mainmenu/button.png"},
            {"ui.main.button_hover", contentRoot_ / "assets/ui/ra2/mainmenu/button_hover.png"},
            {"ui.hud.leftbar", contentRoot_ / "assets/ui/dta/hud/leftbar.png"},
            {"ui.hud.rightbar", contentRoot_ / "assets/ui/dta/hud/rightbar.png"},
            {"ui.hud.button", contentRoot_ / "assets/ui/dta/hud/160pxbtn.png"},
            {"ui.hud.button_hover", contentRoot_ / "assets/ui/dta/hud/160pxbtn_c.png"},
            {"ui.hud.tab", contentRoot_ / "assets/ui/dta/hud/133pxtab.png"},
            {"ui.hud.tab_hover", contentRoot_ / "assets/ui/dta/hud/133pxtab_c.png"},
        };
        for (const auto& [id, path] : images) {
            if (!renderer_.loadTexture(id, path, error)) {
                assetError_ = utf8ToWide(error);
                std::cerr << "[Content][Error] " << error << '\n';
                return false;
            }
        }
        assetReady_ = true;
        std::cerr << "[Content] Loaded project Rules.ini, Art.ini, CONS.SHP and unittem.pal\n";
        std::cerr << "[Content] Runtime root: " << contentRoot_.string() << '\n';
        return true;
    }

    bool buildTerrain(std::string& error) {
        std::vector<renderer::TerrainTileVisual> tiles;
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                const ScreenCoord center = gridToScreen({static_cast<float>(x), static_cast<float>(y)});
                if (center.x < 80.0F || center.x > 1510.0F || center.y < 30.0F || center.y > 870.0F) {
                    continue;
                }
                const bool alternate = ((x + y) & 1) != 0;
                tiles.push_back({center, kTileWidth, kTileHeight,
                    alternate ? Color{0.10F, 0.25F, 0.14F, 1.0F} : Color{0.08F, 0.21F, 0.12F, 1.0F},
                    {0.18F, 0.38F, 0.20F, 0.70F}});
            }
        }
        terrainTileCount_ = tiles.size();
        renderer_.buildStaticTerrain(tiles, error);
        return error.empty();
    }

    std::wstring T(const std::string& key) const {
        const auto it = strings_.find(key);
        return it == strings_.end() ? utf8ToWide(key) : it->second;
    }

    ScreenCoord logicalMouse(float x, float y) const {
        int width = 1;
        int height = 1;
        SDL_GetWindowSize(window_, &width, &height);
        return {x * kLogicalWidth / static_cast<float>(std::max(width, 1)),
            y * kLogicalHeight / static_cast<float>(std::max(height, 1))};
    }

    ScreenCoord gridToScreen(WorldCoord coord) const {
        return projection_.toScreen(coord);
    }

    GridCoord screenToGrid(ScreenCoord screen) const {
        return projection_.toGrid(screen);
    }

    WorldCoord screenToWorld(ScreenCoord screen) const {
        const GridCoord grid = screenToGrid(screen);
        return {static_cast<float>(grid.x), static_cast<float>(grid.y)};
    }

    bool inWorld(ScreenCoord position) const {
        return Rect{100.0F, 50.0F, 1390.0F, 780.0F}.contains(position.x, position.y);
    }

    void processEvents() {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running_ = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                renderer_.resize();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!event.key.repeat) {
                    processKey(event.key.key);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_ = logicalMouse(event.motion.x, event.motion.y);
                if (mode_ == AppMode::MainMenu) {
                    updateMenuHover();
                }
                if (dragging_) {
                    dragEnd_ = mouse_;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                mouse_ = logicalMouse(event.button.x, event.button.y);
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseDown(true);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    mouseDown(false);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                mouse_ = logicalMouse(event.button.x, event.button.y);
                if (event.button.button == SDL_BUTTON_LEFT) {
                    mouseUp(true);
                }
                break;
            default:
                break;
            }
        }
    }

    void processKey(SDL_Keycode key) {
        if (key == SDLK_F3) {
            performance_.showOverlay = !performance_.showOverlay;
            return;
        }
        if (key == SDLK_F4) {
            renderer_.toggleVSync();
            toast_ = renderer_.vsyncEnabled() ? L"VSYNC ON" : L"VSYNC OFF";
            toastTime_ = 2.0F;
            return;
        }
        if (mode_ == AppMode::MainMenu) {
            if (key == SDLK_ESCAPE) {
                running_ = false;
            } else if (key == SDLK_E) {
                mode_ = AppMode::EditorSandbox;
            }
            return;
        }
        if (key == SDLK_ESCAPE) {
            if (placing_) {
                placing_ = false;
            } else {
                mode_ = AppMode::MainMenu;
            }
        } else if (key == SDLK_M) {
            pendingAction_ = PendingAction::Move;
        } else if (key == SDLK_S) {
            simulation_->issueStop();
        } else if (key == SDLK_H) {
            simulation_->issueHold();
        } else if (key == SDLK_P) {
            pendingAction_ = PendingAction::Patrol;
        } else if (key == SDLK_A) {
            pendingAction_ = PendingAction::AttackMove;
        } else if (key == SDLK_R) {
            placingOwner_ = Owner::Red;
            editorTool_ = EditorTool::Unit;
            placing_ = true;
        } else if (key == SDLK_B) {
            placingOwner_ = Owner::Blue;
            editorTool_ = EditorTool::Unit;
            placing_ = true;
        } else if (key == SDLK_T) {
            editorTool_ = editorTool_ == EditorTool::Terrain ? EditorTool::Unit : EditorTool::Terrain;
            placing_ = false;
        }
    }

    void updateMenuHover() {
        int hovered = -1;
        for (std::size_t index = 0; index < menuButtons_.size(); ++index) {
            if (menuButtons_[index].rect.contains(mouse_.x, mouse_.y)) {
                hovered = static_cast<int>(index);
                break;
            }
        }
        if (hovered != hoveredMenuButton_) {
            if (hovered >= 0) {
                audio_.play(AudioCue::MenuHover);
            }
            hoveredMenuButton_ = hovered;
        }
    }

    void mouseDown(bool leftButton) {
        if (mode_ == AppMode::MainMenu) {
            if (!leftButton) {
                return;
            }
            for (std::size_t i = 0; i < menuButtons_.size(); ++i) {
                if (menuButtons_[i].rect.contains(mouse_.x, mouse_.y)) {
                    pressedMenuButton_ = static_cast<int>(i);
                    audio_.play(AudioCue::MenuClick);
                    return;
                }
            }
            return;
        }
        if (!leftButton) {
            issueWorldAction(mouse_, PendingAction::None);
            return;
        }
        if (handleEditorUiClick()) {
            return;
        }
        if (placing_ && inWorld(mouse_)) {
            simulation_->spawn(rules_.e2(), placingOwner_, screenToGrid(mouse_));
            placing_ = false;
            return;
        }
        if (inWorld(mouse_)) {
            dragging_ = true;
            dragStart_ = mouse_;
            dragEnd_ = mouse_;
        }
    }

    void mouseUp(bool leftButton) {
        if (!leftButton) {
            return;
        }
        if (mode_ == AppMode::MainMenu) {
            if (pressedMenuButton_ >= 0 && pressedMenuButton_ < static_cast<int>(menuButtons_.size()) &&
                menuButtons_[pressedMenuButton_].rect.contains(mouse_.x, mouse_.y)) {
                activateMenuButton(static_cast<std::size_t>(pressedMenuButton_));
            }
            pressedMenuButton_ = -1;
            return;
        }
        if (!dragging_) {
            return;
        }
        dragging_ = false;
        const float dragDistance = std::abs(dragEnd_.x - dragStart_.x) + std::abs(dragEnd_.y - dragStart_.y);
        if (dragDistance > 8.0F) {
            simulation_->selectBox(screenToWorld(dragStart_), screenToWorld(dragEnd_));
        } else if (inWorld(mouse_)) {
            simulation_->selectSingle(screenToGrid(mouse_));
            pendingAction_ = PendingAction::None;
            if (selectedEntity() != nullptr) {
                audio_.play(AudioCue::UnitSelect);
            }
        }
    }

    void activateMenuButton(std::size_t index) {
        if (index == 7) {
            mode_ = AppMode::EditorSandbox;
        } else if (index == 8) {
            running_ = false;
        } else {
            toast_ = T("unimplemented");
            toastTime_ = 2.5F;
        }
    }

    bool handleEditorUiClick() {
        if (Rect{17.0F, 121.0F, 60.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            strategicCollapsed_ = !strategicCollapsed_;
            audio_.play(AudioCue::MenuClick);
            return true;
        }
        if (mouse_.y >= 144.0F && mouse_.y <= 186.0F && mouse_.x >= 1520.0F && mouse_.x < 1900.0F) {
            activeTab_ = std::clamp(static_cast<int>((mouse_.x - 1520.0F) / 96.0F), 0, 3);
            audio_.play(AudioCue::MenuClick);
            return true;
        }
        if (mouse_.y >= 216.0F && mouse_.y <= 258.0F && mouse_.x >= 1520.0F && mouse_.x < 1894.0F) {
            return true;
        }
        if (Rect{1510.0F, 70.0F, 190.0F, 46.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Red;
            editorTool_ = EditorTool::Unit;
            placing_ = true;
            return true;
        }
        if (Rect{1710.0F, 70.0F, 190.0F, 46.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Blue;
            editorTool_ = EditorTool::Unit;
            placing_ = true;
            return true;
        }
        if (Rect{1820.0F, 8.0F, 90.0F, 40.0F}.contains(mouse_.x, mouse_.y)) {
            mode_ = AppMode::MainMenu;
            return true;
        }
        const Rect card{1518.0F, 850.0F, 382.0F, 188.0F};
        if (card.contains(mouse_.x, mouse_.y)) {
            const int column = static_cast<int>((mouse_.x - card.x) / 76.0F);
            const int row = static_cast<int>((mouse_.y - 878.0F) / 52.0F);
            if (column >= 0 && column < 5 && row >= 0 && row < 3) {
                const int slot = row * 5 + column;
                if (slot == 0) {
                    pendingAction_ = PendingAction::Move;
                } else if (slot == 1) {
                    simulation_->issueStop();
                } else if (slot == 2) {
                    simulation_->issueHold();
                } else if (slot == 3) {
                    pendingAction_ = PendingAction::Patrol;
                } else if (slot == 4) {
                    pendingAction_ = PendingAction::AttackMove;
                }
                return true;
            }
        }
        return false;
    }

    void issueWorldAction(ScreenCoord position, PendingAction actionFromMouse) {
        if (!inWorld(position)) {
            return;
        }
        const PendingAction action = actionFromMouse == PendingAction::None ? pendingAction_ : actionFromMouse;
        const GridCoord destination = screenToGrid(position);
        if (action == PendingAction::Move) {
            simulation_->issueMove(destination);
            audio_.play(AudioCue::UnitMove);
        } else if (action == PendingAction::Patrol) {
            simulation_->issuePatrol(destination);
            audio_.play(AudioCue::UnitMove);
        } else if (action == PendingAction::AttackMove) {
            simulation_->issueAttackMove(destination);
            audio_.play(AudioCue::UnitMove);
        } else if (action == PendingAction::Attack) {
            const std::uint32_t target = unitAt(position);
            if (target != 0) {
                simulation_->issueAttack(target);
                audio_.play(AudioCue::UnitAttack);
            }
        } else {
            const std::uint32_t target = unitAt(position);
            if (target != 0) {
                simulation_->issueAttack(target);
                audio_.play(AudioCue::UnitAttack);
            } else {
                simulation_->issueMove(destination);
                audio_.play(AudioCue::UnitMove);
            }
        }
        pendingAction_ = PendingAction::None;
    }

    void playSimulationAudio() {
        for (const auto& entity : simulation_->entities()) {
            auto [it, inserted] = seenAttackEvents_.try_emplace(entity.id, entity.attackEvent);
            if (inserted) {
                continue;
            }
            if (entity.attackEvent > it->second) {
                audio_.play(AudioCue::UnitAttack);
                it->second = entity.attackEvent;
            }
        }
    }

    std::uint32_t unitAt(ScreenCoord position) const {
        std::uint32_t result = 0;
        float closest = 24.0F;
        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord unitPosition = gridToScreen(entity.position);
            const float dx = unitPosition.x - position.x;
            const float dy = unitPosition.y - position.y;
            const float candidate = std::sqrt(dx * dx + dy * dy);
            if (candidate < closest) {
                closest = candidate;
                result = entity.id;
            }
        }
        return result;
    }

    static std::string animationSequence(simulation::AnimationState state) {
        switch (state) {
        case simulation::AnimationState::Idle: return "Ready";
        case simulation::AnimationState::Walk: return "Walk";
        case simulation::AnimationState::Attack: return "Fire";
        case simulation::AnimationState::Death: return "Death";
        }
        return "Ready";
    }

    std::wstring animationLabel(simulation::AnimationState state) const {
        switch (state) {
        case simulation::AnimationState::Idle: return T("animation_idle");
        case simulation::AnimationState::Walk: return T("animation_walk");
        case simulation::AnimationState::Attack: return T("animation_attack");
        case simulation::AnimationState::Death: return T("animation_death");
        }
        return T("animation_idle");
    }

    const simulation::Entity* previewEntity() const {
        if (const simulation::Entity* selected = selectedEntity(); selected != nullptr) {
            return selected;
        }
        for (const auto& entity : simulation_->entities()) {
            if (entity.health > 0) {
                return &entity;
            }
        }
        return nullptr;
    }

    void render() {
        renderer_.beginFrame();
        if (mode_ == AppMode::MainMenu) {
            renderMainMenu();
        } else {
            renderEditor();
        }
        if (toastTime_ > 0.0F) {
            toastTime_ -= 1.0F / 60.0F;
            renderer_.drawRect({690.0F, 968.0F, 540.0F, 54.0F}, {0.02F, 0.02F, 0.03F, 0.96F});
            renderer_.drawBorder({690.0F, 968.0F, 540.0F, 54.0F}, {0.85F, 0.18F, 0.04F, 1.0F});
            renderer_.drawText(toast_, {700.0F, 974.0F, 520.0F, 42.0F}, 22, {1.0F, 0.82F, 0.18F, 1.0F});
        }
        if (performance_.showOverlay) {
            renderPerformanceOverlay();
        }
        renderer_.present();
    }

    static std::wstring formatNumber(float value, int precision = 1) {
        std::wostringstream stream;
        stream << std::fixed << std::setprecision(precision) << value;
        return stream.str();
    }

    void recordPerformance(float frameMs, float simulationMs, float renderMs) {
        performance_.frameTimeMs = frameMs;
        performance_.simulationTimeMs = simulationMs;
        performance_.renderCpuTimeMs = renderMs;
        performance_.recentFrameTimesMs.push_back(frameMs);
        while (performance_.recentFrameTimesMs.size() > 180) {
            performance_.recentFrameTimesMs.pop_front();
        }
        if (performance_.recentFrameTimesMs.empty()) {
            return;
        }
        float totalMs = 0.0F;
        for (const float sample : performance_.recentFrameTimesMs) {
            totalMs += sample;
        }
        const float averageFrameMs = totalMs / static_cast<float>(performance_.recentFrameTimesMs.size());
        performance_.averageFps = averageFrameMs > 0.0F ? 1000.0F / averageFrameMs : 0.0F;
        std::vector<float> sorted(performance_.recentFrameTimesMs.begin(), performance_.recentFrameTimesMs.end());
        std::sort(sorted.begin(), sorted.end());
        const std::size_t p95Index = std::min(sorted.size() - 1,
            static_cast<std::size_t>(std::ceil(static_cast<float>(sorted.size()) * 0.95F)) - 1U);
        performance_.p95FrameTimeMs = sorted[p95Index];
    }

    void renderPerformanceOverlay() {
        const auto& renderStats = renderer_.statistics();
        renderer_.drawRect({24.0F, 62.0F, 620.0F, 228.0F}, {0.01F, 0.02F, 0.025F, 0.94F});
        renderer_.drawBorder({24.0F, 62.0F, 620.0F, 228.0F}, {0.35F, 0.80F, 0.55F, 0.95F}, 2.0F);
        renderer_.drawText(L"PERFORMANCE [F3] / VSYNC [F4]", {42.0F, 72.0F, 420.0F, 24.0F}, 17,
            {0.45F, 1.0F, 0.60F, 1.0F}, false);
        renderer_.drawText(L"FPS " + formatNumber(performance_.averageFps) + L"   FRAME " +
            formatNumber(performance_.frameTimeMs) + L" ms   P95 " + formatNumber(performance_.p95FrameTimeMs) + L" ms",
            {42.0F, 102.0F, 580.0F, 24.0F}, 15, {0.85F, 0.90F, 0.88F, 1.0F}, false);
        renderer_.drawText(L"SIM " + formatNumber(performance_.simulationTimeMs) + L" ms   RENDER " +
            formatNumber(performance_.renderCpuTimeMs) + L" ms   DRAWS " + std::to_wstring(renderStats.drawCalls),
            {42.0F, 128.0F, 580.0F, 24.0F}, 15, {0.85F, 0.90F, 0.88F, 1.0F}, false);
        renderer_.drawText(L"TILES " + std::to_wstring(renderStats.visibleTiles) + L"   ENTITIES " +
            std::to_wstring(renderStats.visibleEntities) + L"   LAYOUTS " +
            std::to_wstring(renderStats.textLayoutUpdates) + L"   UPLOADS " +
            std::to_wstring(renderStats.textureUploadCount), {42.0F, 154.0F, 580.0F, 24.0F}, 15,
            {0.85F, 0.90F, 0.88F, 1.0F}, false);
        const auto* debugEntity = previewEntity();
        if (debugEntity != nullptr) {
            renderer_.drawText(L"ASSET E2 / CONS.SHP   FACINGS 8   SEQUENCE " +
                utf8ToWide(animationSequence(debugEntity->animationState)) +
                L"   DIR " + std::to_wstring(debugEntity->facing) + L"   FRAME " +
                std::to_wstring(debugEntity->animationFrame), {42.0F, 180.0F, 580.0F, 24.0F}, 14,
                {1.0F, 0.82F, 0.28F, 1.0F}, false);
        }
        renderer_.drawText(L"MOUSE " + std::to_wstring(static_cast<int>(mouse_.x)) + L"," +
            std::to_wstring(static_cast<int>(mouse_.y)) + L"   SELECTED " +
            std::to_wstring(selectedEntity() != nullptr ? 1 : 0), {42.0F, 206.0F, 580.0F, 24.0F}, 14,
            {0.72F, 0.90F, 0.80F, 1.0F}, false);
    }

    void renderMainMenu() {
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.0F, 0.0F, 0.0F, 1.0F});
        renderer_.drawImage("ui.main.background", {320.0F, 120.0F, 1280.0F, 824.0F});
        renderer_.drawText(T("title"), {510.0F, 425.0F, 640.0F, 58.0F}, 36, {1.0F, 0.84F, 0.34F, 1.0F});
        renderer_.drawText(T("subtitle"), {500.0F, 482.0F, 660.0F, 72.0F}, 50, {1.0F, 0.28F, 0.10F, 1.0F});
        renderer_.drawText(L"RA2YR-BYCPP  //  MAIN CONTROL", {520.0F, 560.0F, 620.0F, 30.0F}, 16, {0.86F, 0.62F, 0.28F, 1.0F});
        for (std::size_t i = 0; i < menuButtons_.size(); ++i) {
            const MenuButton& button = menuButtons_[i];
            const bool hovered = button.rect.contains(mouse_.x, mouse_.y);
            const bool pressed = pressedMenuButton_ == static_cast<int>(i);
            renderer_.drawImage(pressed || hovered ? button.hoverImage : button.image, button.rect,
                pressed ? Color{0.80F, 0.80F, 0.80F, 1.0F} : Color{1.0F, 1.0F, 1.0F, 1.0F});
            renderer_.drawText(T(button.key), button.rect, 18, {1.0F, 0.86F, 0.20F, 1.0F});
        }
        renderer_.drawText(L"RED COMMAND CONSOLE  //  C++23 / SDL3 / D3D11", {350.0F, 1008.0F, 1220.0F, 28.0F}, 14, {0.72F, 0.34F, 0.18F, 1.0F}, false);
    }

    void drawHudPanel(Rect panel, const std::wstring& title) {
        renderer_.drawRect(panel, {0.018F, 0.023F, 0.030F, 0.99F});
        renderer_.drawBorder(panel, {0.67F, 0.47F, 0.17F, 1.0F}, 3.0F);
        renderer_.drawText(title, {panel.x + 12.0F, panel.y + 7.0F, panel.width - 24.0F, 26.0F}, 16,
            {1.0F, 0.82F, 0.20F, 1.0F}, false);
    }

    void renderMiniMap(Rect panel) {
        drawHudPanel(panel, T("minimap"));
        const Rect field{panel.x + 14.0F, panel.y + 38.0F, panel.width - 28.0F, panel.height - 52.0F};
        renderer_.drawRect(field, {0.025F, 0.10F, 0.065F, 1.0F});
        for (int index = 1; index < 8; ++index) {
            const float x = field.x + field.width * static_cast<float>(index) / 8.0F;
            const float y = field.y + field.height * static_cast<float>(index) / 8.0F;
            renderer_.drawLine({x, field.y}, {x, field.y + field.height}, {0.08F, 0.20F, 0.22F, 0.7F}, 1.0F);
            renderer_.drawLine({field.x, y}, {field.x + field.width, y}, {0.08F, 0.20F, 0.22F, 0.7F}, 1.0F);
        }
        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const float normalizedX = std::clamp(entity.position.x / 64.0F, 0.0F, 1.0F);
            const float normalizedY = std::clamp(entity.position.y / 64.0F, 0.0F, 1.0F);
            const Color color = entity.owner == Owner::Red ? Color{1.0F, 0.12F, 0.08F, 1.0F} :
                Color{0.20F, 0.55F, 1.0F, 1.0F};
            renderer_.drawRect({field.x + normalizedX * field.width - 4.0F,
                field.y + normalizedY * field.height - 4.0F, 8.0F, 8.0F}, color);
        }
    }

    void renderEditor() {
        renderer_.setWorldStats(terrainTileCount_, simulation_->entities().size());
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.008F, 0.012F, 0.018F, 1.0F});
        const Rect world{100.0F, 50.0F, 1390.0F, 780.0F};
        renderer_.drawRect(world, {0.04F, 0.07F, 0.05F, 1.0F});
        renderer_.drawStaticTerrain();
        renderer_.drawBorder(world, {0.65F, 0.48F, 0.18F, 1.0F}, 3.0F);

        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord position = gridToScreen(entity.position);
            if (assetReady_) {
                renderer_.drawSprite(rules_.e2().image, "unittem", static_cast<std::size_t>(art_.frameIndex(
                    rules_.e2().image, animationSequence(entity.animationState), entity.animationFrame,
                    entity.facing)), entity.owner, position, kRenderScale.unitVisualScale);
            }
            if (entity.selected) {
                renderer_.drawBorder({position.x - 22.0F, position.y - 8.0F, 44.0F, 16.0F},
                    {0.95F, 0.85F, 0.25F, 1.0F}, 2.0F);
            }
            const float healthRatio = std::clamp(static_cast<float>(entity.health) /
                static_cast<float>(entity.maxHealth), 0.0F, 1.0F);
            renderer_.drawRect({position.x - 22.0F, position.y - 30.0F, 44.0F, 4.0F},
                {0.16F, 0.02F, 0.02F, 1.0F});
            renderer_.drawRect({position.x - 22.0F, position.y - 30.0F, 44.0F * healthRatio, 4.0F},
                entity.owner == Owner::Red ? Color{0.95F, 0.10F, 0.06F, 1.0F} :
                Color{0.18F, 0.48F, 1.0F, 1.0F});
        }

        const Rect strategicRail{0.0F, 105.0F, 94.0F, 670.0F};
        renderer_.drawRect(strategicRail, {0.025F, 0.035F, 0.045F, 0.98F});
        renderer_.drawImage("ui.hud.leftbar", {0.0F, 105.0F, 24.0F, 670.0F});
        renderer_.drawImage("ui.hud.leftbar", {70.0F, 105.0F, 24.0F, 670.0F});
        renderer_.drawBorder({8.0F, 114.0F, 78.0F, 650.0F}, {0.64F, 0.42F, 0.16F, 1.0F}, 2.0F);
        const Rect collapseButton{17.0F, 121.0F, 60.0F, 38.0F};
        renderer_.drawImage("ui.hud.button", collapseButton);
        renderer_.drawText(strategicCollapsed_ ? T("expand") : T("collapse"), collapseButton, 12,
            {1.0F, 0.82F, 0.20F, 1.0F});
        if (!strategicCollapsed_) {
            renderer_.drawText(T("strategic"), {12.0F, 164.0F, 70.0F, 34.0F}, 14,
                {1.0F, 0.82F, 0.20F, 1.0F});
            for (int index = 0; index < 5; ++index) {
                const Rect ability{18.0F, 202.0F + index * 108.0F, 58.0F, 92.0F};
                renderer_.drawImage("ui.hud.button", ability);
                renderer_.drawText(L"--", {ability.x + 2.0F, ability.y + 23.0F,
                    ability.width - 4.0F, 34.0F}, 22, {0.42F, 0.44F, 0.48F, 1.0F});
            }
        }

        const Rect side{1500.0F, 40.0F, 420.0F, 785.0F};
        renderer_.drawRect(side, {0.025F, 0.03F, 0.04F, 0.98F});
        renderer_.drawImage("ui.hud.leftbar", {1500.0F, 40.0F, 24.0F, 785.0F});
        renderer_.drawImage("ui.hud.rightbar", {1896.0F, 40.0F, 24.0F, 785.0F});
        renderer_.drawBorder(side, {0.75F, 0.55F, 0.18F, 1.0F}, 4.0F);
        renderer_.drawText(L"10000", {1630.0F, 45.0F, 155.0F, 36.0F}, 28, {1.0F, 0.84F, 0.20F, 1.0F});
        renderer_.drawText(T("red"), {1510.0F, 70.0F, 190.0F, 34.0F}, 19, {1.0F, 0.30F, 0.18F, 1.0F});
        renderer_.drawText(T("blue"), {1710.0F, 70.0F, 190.0F, 34.0F}, 19, {0.32F, 0.58F, 1.0F, 1.0F});
        renderer_.drawText(T("production"), {1520.0F, 108.0F, 370.0F, 30.0F}, 20,
            {0.95F, 0.78F, 0.22F, 1.0F});
        const std::string tabs[] = {"building", "defense", "infantry", "vehicles"};
        for (int index = 0; index < 4; ++index) {
            const Rect tab{1520.0F + index * 96.0F, 144.0F, 88.0F, 42.0F};
            renderer_.drawImage(index == activeTab_ ? "ui.hud.tab_hover" : "ui.hud.tab", tab);
            renderer_.drawText(T(tabs[index]), tab, 16, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        renderer_.drawText(T("producer"), {1520.0F, 191.0F, 370.0F, 26.0F}, 16,
            {0.86F, 0.68F, 0.22F, 1.0F});
        for (int index = 0; index < 3; ++index) {
            const Rect producer{1520.0F + index * 126.0F, 216.0F, 118.0F, 42.0F};
            renderer_.drawImage("ui.hud.tab", producer, {0.55F, 0.55F, 0.58F, 1.0F});
            renderer_.drawText(L"--", producer, 18, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        for (int index = 0; index < 12; ++index) {
            const int column = index % 3;
            const int row = index / 3;
            const Rect product{1528.0F + column * 122.0F, 275.0F + row * 95.0F, 108.0F, 80.0F};
            renderer_.drawImage("ui.hud.button", product);
            renderer_.drawText(L"--", {product.x, product.y + 12.0F, product.width, 28.0F}, 24,
                {0.42F, 0.44F, 0.48F, 1.0F});
        }

        const Rect hudBackground{0.0F, 842.0F, 1920.0F, 238.0F};
        renderer_.drawRect(hudBackground, {0.012F, 0.016F, 0.022F, 1.0F});
        const Rect miniMap{8.0F, 850.0F, 320.0F, 220.0F};
        const Rect model{336.0F, 850.0F, 200.0F, 220.0F};
        const Rect info{544.0F, 850.0F, 490.0F, 220.0F};
        const Rect portrait{1042.0F, 850.0F, 466.0F, 220.0F};
        renderMiniMap(miniMap);
        drawHudPanel(model, T("unit_model"));
        drawHudPanel(info, T("unit_info"));
        drawHudPanel(portrait, T("portrait"));
        const auto selected = selectedEntity();
        const auto preview = previewEntity();
        const Rect modelViewport{348.0F, 888.0F, 176.0F, 168.0F};
        const Rect portraitViewport{1056.0F, 888.0F, 438.0F, 168.0F};
        renderer_.drawRect(modelViewport, {0.005F, 0.008F, 0.012F, 1.0F});
        renderer_.drawRect(portraitViewport, {0.005F, 0.008F, 0.012F, 1.0F});
        if (preview != nullptr && assetReady_) {
            const std::size_t frame = static_cast<std::size_t>(art_.frameIndex(rules_.e2().image,
                animationSequence(preview->animationState), preview->animationFrame, preview->facing));
            renderer_.drawSprite(rules_.e2().image, "unittem", frame, preview->owner, {436.0F, 1005.0F},
                kRenderScale.hudModelScale);
            renderer_.drawSprite(rules_.e2().image, "unittem", frame, preview->owner, {1275.0F, 1005.0F},
                kRenderScale.hudPortraitScale);
            renderer_.drawText(animationLabel(preview->animationState) + L"  /  DIR " +
                std::to_wstring(preview->facing), {1300.0F, 918.0F, 170.0F, 30.0F}, 15,
                {1.0F, 0.82F, 0.20F, 1.0F}, false);
        } else {
            renderer_.drawText(L"--", modelViewport, 28, {0.42F, 0.44F, 0.48F, 1.0F});
            renderer_.drawText(L"--", portraitViewport, 28, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        if (selected == nullptr) {
            renderer_.drawText(L"未选择单位", {560.0F, 896.0F, 420.0F, 30.0F}, 22,
                {1.0F, 0.82F, 0.20F, 1.0F}, false);
        } else {
            renderer_.drawText(T("unit_name"), {560.0F, 890.0F, 430.0F, 32.0F}, 24,
                {1.0F, 0.82F, 0.20F, 1.0F}, false);
            renderer_.drawText(T("owner") + L": " + ownerText(selected->owner), {560.0F, 930.0F, 430.0F, 28.0F}, 18,
                {0.82F, 0.82F, 0.78F, 1.0F}, false);
            renderer_.drawText(T("weapon") + L": " + T("weapon_name"), {560.0F, 966.0F, 430.0F, 28.0F}, 18,
                {0.82F, 0.82F, 0.78F, 1.0F}, false);
            renderer_.drawText(T("armor") + L": " + T("armor_name"), {560.0F, 1002.0F, 430.0F, 28.0F}, 18,
                {0.82F, 0.82F, 0.78F, 1.0F}, false);
            renderer_.drawText(T("health") + L": " + std::to_wstring(selected->health) + L" / " +
                std::to_wstring(selected->maxHealth), {560.0F, 1038.0F, 430.0F, 28.0F}, 18,
                {0.36F, 1.0F, 0.36F, 1.0F}, false);
        }

        const Rect card{1518.0F, 850.0F, 382.0F, 188.0F};
        drawHudPanel(card, L"COMMAND CARD  3 x 5");
        const std::string commandKeys[] = {"move", "stop", "hold", "patrol", "attack_move", "", "", "", "", "", "", "", "", "", ""};
        for (int slot = 0; slot < 15; ++slot) {
            const int column = slot % 5;
            const int row = slot / 5;
            const Rect button{1523.0F + column * 76.0F, 878.0F + row * 52.0F, 72.0F, 48.0F};
            const bool hot = button.contains(mouse_.x, mouse_.y);
            renderer_.drawImage(hot ? "ui.hud.button_hover" : "ui.hud.button", button);
            if (!commandKeys[slot].empty()) {
                const int fontSize = slot == 4 ? 9 : 13;
                renderer_.drawText(T(commandKeys[slot]), button, fontSize, {1.0F, 0.84F, 0.26F, 1.0F});
            }
        }

        renderer_.drawText(T("editor_title"), {116.0F, 10.0F, 280.0F, 30.0F}, 21,
            {1.0F, 0.82F, 0.20F, 1.0F}, false);
        const std::wstring toolName = editorTool_ == EditorTool::Terrain ? T("terrain") : T("unit");
        const std::wstring ownerName = editorTool_ == EditorTool::Unit ? ownerText(placingOwner_) : L"--";
        const std::wstring modeName = placing_ ? T("place") : T("select");
        renderer_.drawText(T("tool") + L": " + toolName + L"  |  " + T("object") + L": E2  |  " +
            T("owner") + L": " + ownerName + L"  |  " + T("mode") + L": " + modeName,
            {410.0F, 10.0F, 1350.0F, 30.0F}, 15, {0.80F, 0.82F, 0.82F, 1.0F}, false);
        renderer_.drawText(T("editor_hint") + L"   T 工具", {410.0F, 34.0F, 1350.0F, 24.0F}, 13,
            {0.58F, 0.62F, 0.66F, 1.0F}, false);
        renderer_.drawText(L"MENU", {1820.0F, 8.0F, 90.0F, 40.0F}, 17,
            {1.0F, 0.84F, 0.24F, 1.0F});
        if (placing_) {
            renderer_.drawBorder(world, placingOwner_ == Owner::Red ? Color{1.0F, 0.12F, 0.08F, 1.0F} :
                Color{0.18F, 0.45F, 1.0F, 1.0F}, 5.0F);
            renderer_.drawText(placingOwner_ == Owner::Red ? T("place_red") : T("place_blue"),
                {580.0F, 72.0F, 420.0F, 34.0F}, 20, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        if (dragging_) {
            const Rect selection{std::min(dragStart_.x, dragEnd_.x), std::min(dragStart_.y, dragEnd_.y),
                std::abs(dragEnd_.x - dragStart_.x), std::abs(dragEnd_.y - dragStart_.y)};
            renderer_.drawRect(selection, {0.3F, 0.6F, 1.0F, 0.12F});
            renderer_.drawBorder(selection, {0.45F, 0.80F, 1.0F, 0.9F}, 2.0F);
        }
    }

    const simulation::Entity* selectedEntity() const {
        for (const auto& entity : simulation_->entities()) {
            if (entity.selected) {
                return &entity;
            }
        }
        return nullptr;
    }

    std::wstring ownerText(Owner owner) const {
        return owner == Owner::Red ? T("red") : owner == Owner::Blue ? T("blue") : L"NEUTRAL";
    }

    SDL_Window* window_ = nullptr;
    renderer::D3D11Renderer renderer_;
    gamedata::RulesDatabase rules_;
    gamedata::ArtDatabase art_;
    westwood::Palette palette_;
    westwood::ShpTsDocument sprite_;
    std::unique_ptr<simulation::Simulation> simulation_;
    AudioService audio_;
    std::unordered_map<std::uint32_t, std::uint32_t> seenAttackEvents_;
    std::unordered_map<std::string, std::wstring> strings_;
    std::wstring assetError_;
    std::wstring toast_;
    std::filesystem::path contentRoot_;
    PerformanceTracker performance_;
    AppMode mode_ = AppMode::MainMenu;
    EditorTool editorTool_ = EditorTool::Unit;
    PendingAction pendingAction_ = PendingAction::None;
    Owner placingOwner_ = Owner::Red;
    bool running_ = true;
    bool assetReady_ = false;
    bool placing_ = false;
    bool dragging_ = false;
    bool strategicCollapsed_ = false;
    int activeTab_ = 0;
    int pressedMenuButton_ = -1;
    int hoveredMenuButton_ = -1;
    float toastTime_ = 0.0F;
    ScreenCoord mouse_{};
    ScreenCoord dragStart_{};
    ScreenCoord dragEnd_{};
    IsoProjection projection_{kTileWidth, kTileHeight, {790.0F, 440.0F}};
    std::size_t terrainTileCount_ = 0;
    std::vector<MenuButton> menuButtons_ = {
        {"campaign", "ui.main.button", "ui.main.button_hover", {1322.0F, 315.0F, 176.0F, 49.0F}},
        {"load", "ui.main.button", "ui.main.button_hover", {1322.0F, 376.0F, 176.0F, 49.0F}},
        {"skirmish", "ui.main.button", "ui.main.button_hover", {1322.0F, 437.0F, 176.0F, 49.0F}},
        {"online", "ui.main.button", "ui.main.button_hover", {1322.0F, 498.0F, 176.0F, 49.0F}},
        {"lan", "ui.main.button", "ui.main.button_hover", {1322.0F, 559.0F, 176.0F, 49.0F}},
        {"settings", "ui.main.button", "ui.main.button_hover", {1322.0F, 620.0F, 176.0F, 49.0F}},
        {"statistics", "ui.main.button", "ui.main.button_hover", {1322.0F, 681.0F, 176.0F, 49.0F}},
        {"editor", "ui.main.button", "ui.main.button_hover", {1322.0F, 742.0F, 176.0F, 49.0F}},
        {"exit", "ui.main.button", "ui.main.button_hover", {1322.0F, 803.0F, 176.0F, 49.0F}},
    };
};

} // namespace
} // namespace ra2yr::client

int main(int argc, char** argv) {
    ra2yr::client::ClientApp app;
    std::string error;
    bool stressEntities = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--stress-entities") {
            stressEntities = true;
        }
    }
    if (!app.initialize(error, stressEntities)) {
        std::cerr << "[Fatal] " << error << '\n';
        return 1;
    }
    return app.run();
}
