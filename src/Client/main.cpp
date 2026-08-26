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
#include <limits>
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
constexpr Rect kWorldViewport{100.0F, 50.0F, 1390.0F, 780.0F};
constexpr float kCameraEdgeThreshold = 22.0F;
constexpr float kCameraPanPixelsPerSecond = 420.0F;

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
    UIHover,
    UIClick,
    VoiceSelect,
    VoiceMoveAcknowledgement,
    VoiceAttackAcknowledgement,
    WeaponFire,
};

class AudioService {
public:
    ~AudioService() { shutdown(); }

    bool initialize() {
        SDL_AudioSpec spec{};
        spec.format = SDL_AUDIO_F32;
        spec.channels = 1;
        spec.freq = 48000;
        voiceStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        sfxStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
        if (voiceStream_ == nullptr || sfxStream_ == nullptr ||
            !SDL_ResumeAudioStreamDevice(voiceStream_) || !SDL_ResumeAudioStreamDevice(sfxStream_)) {
            std::cerr << "[Audio] Disabled: " << SDL_GetError() << '\n';
            shutdown();
            return false;
        }
        std::cerr << "[Audio] Voice/SFX streams ready; procedural SFX remain fallback\n";
        return true;
    }

    bool loadVoiceSet(const std::filesystem::path& configPath, std::string& error) {
        westwood::IniDocument config;
        if (!config.load(configPath, error)) {
            return false;
        }
        const std::pair<AudioCue, std::string_view> voices[] = {
            {AudioCue::VoiceSelect, "VoiceSelect"},
            {AudioCue::VoiceMoveAcknowledgement, "VoiceMoveAcknowledgement"},
            {AudioCue::VoiceAttackAcknowledgement, "VoiceAttackAcknowledgement"},
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
        if (voiceStream_ != nullptr) {
            SDL_DestroyAudioStream(voiceStream_);
            voiceStream_ = nullptr;
        }
        if (sfxStream_ != nullptr) {
            SDL_DestroyAudioStream(sfxStream_);
            sfxStream_ = nullptr;
        }
    }

    void play(AudioCue cue) {
        if (isVoiceCue(cue)) {
            playVoice(cue);
            return;
        }
        if (cue == AudioCue::WeaponFire) {
            // WeaponFire has a separate event path; it remains silent until a
            // real weapon sample is added and must never borrow unit voice.
            return;
        }
        playProcedural(sfxStream_, cue);
    }

    [[nodiscard]] std::uint32_t voiceAckCount() const { return voiceAckCount_; }
    [[nodiscard]] const std::string& lastVoiceEvent() const { return lastVoiceEvent_; }

private:
    static bool isVoiceCue(AudioCue cue) {
        return cue == AudioCue::VoiceSelect || cue == AudioCue::VoiceMoveAcknowledgement ||
            cue == AudioCue::VoiceAttackAcknowledgement;
    }

    void playVoice(AudioCue cue) {
        if (cue == AudioCue::VoiceMoveAcknowledgement || cue == AudioCue::VoiceAttackAcknowledgement) {
            ++voiceAckCount_;
        }
        lastVoiceEvent_ = cueName(cue);
        if (voiceStream_ == nullptr) {
            return;
        }
        // A command acknowledgement is a latest-intent channel.  Clearing
        // the previous sample prevents stale voice commands from piling up.
        SDL_ClearAudioStream(voiceStream_);
        const auto voice = voiceSamples_.find(cue);
        if (voice != voiceSamples_.end() && !voice->second.empty()) {
            if (!SDL_PutAudioStreamData(voiceStream_, voice->second.data(),
                static_cast<int>(voice->second.size() * sizeof(float)))) {
                std::cerr << "[Audio] Voice queue failed: " << SDL_GetError() << '\n';
            }
            return;
        }
        playProcedural(voiceStream_, cue);
    }

    static std::string cueName(AudioCue cue) {
        switch (cue) {
        case AudioCue::VoiceSelect: return "VoiceSelect";
        case AudioCue::VoiceMoveAcknowledgement: return "VoiceMoveAcknowledgement";
        case AudioCue::VoiceAttackAcknowledgement: return "VoiceAttackAcknowledgement";
        default: return {};
        }
    }

    static void playProcedural(SDL_AudioStream* stream, AudioCue cue) {
        if (stream == nullptr) {
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
        if (!SDL_PutAudioStreamData(stream, samples.data(), static_cast<int>(samples.size() * sizeof(float)))) {
            std::cerr << "[Audio] Cue queue failed: " << SDL_GetError() << '\n';
        }
    }

    struct CueSettings {
        float frequency;
        float secondFrequency;
        float duration;
        float volume;
    };

    static CueSettings settingsFor(AudioCue cue) {
        switch (cue) {
        case AudioCue::UIHover: return {880.0F, 1320.0F, 0.045F, 0.10F};
        case AudioCue::UIClick: return {220.0F, 440.0F, 0.095F, 0.16F};
        case AudioCue::VoiceSelect: return {330.0F, 495.0F, 0.16F, 0.18F};
        case AudioCue::VoiceMoveAcknowledgement: return {260.0F, 390.0F, 0.12F, 0.15F};
        case AudioCue::VoiceAttackAcknowledgement: return {120.0F, 180.0F, 0.20F, 0.20F};
        case AudioCue::WeaponFire: return {0.0F, 0.0F, 0.0F, 0.0F};
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

    SDL_AudioStream* voiceStream_ = nullptr;
    SDL_AudioStream* sfxStream_ = nullptr;
    std::unordered_map<AudioCue, std::vector<float>> voiceSamples_;
    std::uint32_t voiceAckCount_ = 0;
    std::string lastVoiceEvent_;
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
        camera_.projection.tileWidth = kTileWidth;
        camera_.projection.tileHeight = kTileHeight;
        camera_.viewportCenter = {kWorldViewport.x + kWorldViewport.width * 0.5F,
            kWorldViewport.y + kWorldViewport.height * 0.5F};
        camera_.worldCenter = {0.0F, 0.0F};
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
                updateCamera(static_cast<float>(seconds));
                updateDirectionTest(static_cast<float>(seconds));
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
             {"sandbox_palette", L"沙盒工具"}, {"category", L"类别"}, {"current_object", L"当前对象"},
             {"faction_owner", L"所属"}, {"disabled", L"不可用"}, {"palette_hint", L"F2 显示/隐藏工具窗"},
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
        tiles.reserve(64U * 64U);
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                const bool alternate = ((x + y) & 1) != 0;
                tiles.push_back({{static_cast<float>(x), static_cast<float>(y)}, kTileWidth, kTileHeight,
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
        return camera_.toScreen(coord);
    }

    GridCoord screenToGrid(ScreenCoord screen) const {
        return camera_.toGrid(screen);
    }

    WorldCoord screenToWorld(ScreenCoord screen) const {
        return camera_.toWorld(screen);
    }

    bool inWorld(ScreenCoord position) const {
        return kWorldViewport.contains(position.x, position.y);
    }

    void updateCamera(float seconds) {
        if (!inWorld(mouse_) || sandboxPaletteDragging_ ||
            (sandboxPaletteVisible_ && sandboxPaletteRect().contains(mouse_.x, mouse_.y))) {
            return;
        }
        ScreenCoord delta{};
        if (mouse_.x <= kWorldViewport.x + kCameraEdgeThreshold) {
            delta.x -= kCameraPanPixelsPerSecond * seconds;
        } else if (mouse_.x >= kWorldViewport.x + kWorldViewport.width - kCameraEdgeThreshold) {
            delta.x += kCameraPanPixelsPerSecond * seconds;
        }
        if (mouse_.y <= kWorldViewport.y + kCameraEdgeThreshold) {
            delta.y -= kCameraPanPixelsPerSecond * seconds;
        } else if (mouse_.y >= kWorldViewport.y + kWorldViewport.height - kCameraEdgeThreshold) {
            delta.y += kCameraPanPixelsPerSecond * seconds;
        }
        if (std::abs(delta.x) > 0.0F || std::abs(delta.y) > 0.0F) {
            camera_.panScreen(delta);
        }
    }

    void updateDirectionTest(float seconds) {
        if (!directionTestMode_) {
            return;
        }
        directionTestTime_ -= seconds;
        if (directionTestTime_ > 0.0F) {
            return;
        }
        const simulation::Entity* testEntity = nullptr;
        for (const auto& entity : simulation_->entities()) {
            if (entity.health > 0) {
                testEntity = &entity;
                break;
            }
        }
        if (testEntity == nullptr) {
            return;
        }
        simulation_->selectEntity(testEntity->id);
        static constexpr GridCoord worldDeltas[] = {
            {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}};
        const GridCoord delta = worldDeltas[directionTestIndex_];
        const GridCoord origin{static_cast<int>(std::lround(testEntity->position.x)),
            static_cast<int>(std::lround(testEntity->position.y))};
        simulation_->issueMove({origin.x + delta.x * 4, origin.y + delta.y * 4});
        directionTestIndex_ = (directionTestIndex_ + 1) % 8;
        directionTestTime_ = 1.25F;
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
                if (sandboxPaletteDragging_) {
                    sandboxPalettePosition_ = {mouse_.x - sandboxPaletteDragOffset_.x,
                        mouse_.y - sandboxPaletteDragOffset_.y};
                }
                if (dragging_) {
                    dragEnd_ = mouse_;
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (mode_ == AppMode::EditorSandbox && inWorld(mouse_) &&
                    !(sandboxPaletteVisible_ && sandboxPaletteRect().contains(mouse_.x, mouse_.y))) {
                    camera_.zoomAt(mouse_, event.wheel.y > 0.0F ? 1.10F : 1.0F / 1.10F);
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
        if (key == SDLK_F2 && mode_ == AppMode::EditorSandbox) {
            sandboxPaletteVisible_ = !sandboxPaletteVisible_;
            return;
        }
        if (key == SDLK_F6 && mode_ == AppMode::EditorSandbox) {
            directionTestMode_ = !directionTestMode_;
            directionTestIndex_ = 0;
            directionTestTime_ = 0.0F;
            toast_ = directionTestMode_ ? L"8-DIRECTION TEST ON" : L"8-DIRECTION TEST OFF";
            toastTime_ = 1.5F;
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
            if (pendingAction_ != PendingAction::None) {
                pendingAction_ = PendingAction::None;
            } else if (placing_) {
                placing_ = false;
            } else {
                mode_ = AppMode::MainMenu;
            }
        } else if (key == SDLK_M) {
            pendingAction_ = PendingAction::Move;
        } else if (key == SDLK_S) {
            simulation_->issueStop();
            if (hasSelectedUnits()) {
                audio_.play(AudioCue::VoiceMoveAcknowledgement);
            }
        } else if (key == SDLK_H) {
            simulation_->issueHold();
            if (hasSelectedUnits()) {
                audio_.play(AudioCue::VoiceMoveAcknowledgement);
            }
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
                audio_.play(AudioCue::UIHover);
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
                    audio_.play(AudioCue::UIClick);
                    return;
                }
            }
            return;
        }
        if (!leftButton) {
            if (pendingAction_ != PendingAction::None) {
                pendingAction_ = PendingAction::None;
                toast_ = L"COMMAND CANCELLED";
                toastTime_ = 1.5F;
            } else {
                issueWorldAction(mouse_, PendingAction::None);
            }
            return;
        }
        if (handleEditorUiClick()) {
            return;
        }
        if (pendingAction_ != PendingAction::None) {
            issueWorldAction(mouse_, pendingAction_);
            return;
        }
        if (placing_ && inWorld(mouse_)) {
            if (editorTool_ == EditorTool::Unit) {
                simulation_->spawn(rules_.e2(), placingOwner_, screenToGrid(mouse_));
            }
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
        if (sandboxPaletteDragging_) {
            sandboxPaletteDragging_ = false;
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
            const std::uint32_t unit = unitAt(mouse_);
            if (unit != 0) {
                simulation_->selectEntity(unit);
            } else {
                simulation_->clearSelection();
            }
            if (selectedEntity() != nullptr) {
                audio_.play(AudioCue::VoiceSelect);
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

    Rect sandboxPaletteRect() const {
        return {sandboxPalettePosition_.x, sandboxPalettePosition_.y, 300.0F,
            sandboxPaletteCollapsed_ ? 42.0F : 328.0F};
    }

    bool handleSandboxPaletteClick() {
        if (!sandboxPaletteVisible_ || !sandboxPaletteRect().contains(mouse_.x, mouse_.y)) {
            return false;
        }
        const Rect palette = sandboxPaletteRect();
        const Rect header{palette.x, palette.y, palette.width, 42.0F};
        if (header.contains(mouse_.x, mouse_.y)) {
            const Rect collapse{palette.x + palette.width - 76.0F, palette.y + 7.0F, 30.0F, 28.0F};
            const Rect close{palette.x + palette.width - 38.0F, palette.y + 7.0F, 30.0F, 28.0F};
            if (close.contains(mouse_.x, mouse_.y)) {
                sandboxPaletteVisible_ = false;
            } else if (collapse.contains(mouse_.x, mouse_.y)) {
                sandboxPaletteCollapsed_ = !sandboxPaletteCollapsed_;
            } else {
                sandboxPaletteDragging_ = true;
                sandboxPaletteDragOffset_ = {mouse_.x - palette.x, mouse_.y - palette.y};
            }
            audio_.play(AudioCue::UIClick);
            return true;
        }
        if (sandboxPaletteCollapsed_) {
            return true;
        }
        const Rect terrain{palette.x + 16.0F, palette.y + 74.0F, 126.0F, 38.0F};
        const Rect unit{palette.x + 158.0F, palette.y + 74.0F, 126.0F, 38.0F};
        if (terrain.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Terrain;
            placing_ = false;
        } else if (unit.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Unit;
        } else if (Rect{palette.x + 16.0F, palette.y + 122.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y) ||
            Rect{palette.x + 158.0F, palette.y + 122.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            // Building and resource tools are intentionally disabled in this
            // slice; the window reserves their future editor slots.
        } else if (Rect{palette.x + 16.0F, palette.y + 180.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Red;
        } else if (Rect{palette.x + 158.0F, palette.y + 180.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Blue;
        } else if (Rect{palette.x + 16.0F, palette.y + 238.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            placing_ = false;
        } else if (Rect{palette.x + 158.0F, palette.y + 238.0F, 126.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Unit;
            placing_ = true;
        } else {
            return true;
        }
        audio_.play(AudioCue::UIClick);
        return true;
    }

    bool handleEditorUiClick() {
        if (handleSandboxPaletteClick()) {
            return true;
        }
        if (Rect{17.0F, 121.0F, 60.0F, 38.0F}.contains(mouse_.x, mouse_.y)) {
            strategicCollapsed_ = !strategicCollapsed_;
            audio_.play(AudioCue::UIClick);
            return true;
        }
        if (mouse_.y >= 108.0F && mouse_.y <= 150.0F && mouse_.x >= 1520.0F && mouse_.x < 1900.0F) {
            activeTab_ = std::clamp(static_cast<int>((mouse_.x - 1520.0F) / 96.0F), 0, 3);
            audio_.play(AudioCue::UIClick);
            return true;
        }
        if (mouse_.y >= 180.0F && mouse_.y <= 222.0F && mouse_.x >= 1520.0F && mouse_.x < 1894.0F) {
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
                    if (hasSelectedUnits()) {
                        audio_.play(AudioCue::VoiceMoveAcknowledgement);
                    }
                } else if (slot == 2) {
                    simulation_->issueHold();
                    if (hasSelectedUnits()) {
                        audio_.play(AudioCue::VoiceMoveAcknowledgement);
                    }
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
        const bool hasSelection = hasSelectedUnits();
        if (action == PendingAction::Move) {
            if (hasSelection) {
                simulation_->issueMove(destination);
                audio_.play(AudioCue::VoiceMoveAcknowledgement);
            }
        } else if (action == PendingAction::Patrol) {
            if (hasSelection) {
                simulation_->issuePatrol(destination);
                audio_.play(AudioCue::VoiceMoveAcknowledgement);
            }
        } else if (action == PendingAction::AttackMove) {
            const std::uint32_t target = unitAt(position);
            if (hasSelection && isEnemyTarget(target)) {
                simulation_->issueAttack(target);
                audio_.play(AudioCue::VoiceAttackAcknowledgement);
            } else if (hasSelection) {
                simulation_->issueAttackMove(destination);
                audio_.play(AudioCue::VoiceAttackAcknowledgement);
            }
        } else if (action == PendingAction::Attack) {
            const std::uint32_t target = unitAt(position);
            if (hasSelection && isEnemyTarget(target)) {
                simulation_->issueAttack(target);
                audio_.play(AudioCue::VoiceAttackAcknowledgement);
            }
        } else {
            const std::uint32_t target = unitAt(position);
            if (hasSelection && isEnemyTarget(target)) {
                simulation_->issueAttack(target);
                audio_.play(AudioCue::VoiceAttackAcknowledgement);
            } else if (hasSelection) {
                simulation_->issueMove(destination);
                audio_.play(AudioCue::VoiceMoveAcknowledgement);
            }
        }
        if (action != PendingAction::None && hasSelection) {
            pendingAction_ = PendingAction::None;
        }
    }

    void playSimulationAudio() {
        for (const auto& entity : simulation_->entities()) {
            auto [it, inserted] = seenAttackEvents_.try_emplace(entity.id, entity.attackEvent);
            if (inserted) {
                continue;
            }
            if (entity.attackEvent > it->second) {
                audio_.play(AudioCue::WeaponFire);
                it->second = entity.attackEvent;
            }
        }
    }

    bool hasSelectedUnits() const {
        for (const auto& entity : simulation_->entities()) {
            if (entity.selected && entity.health > 0) {
                return true;
            }
        }
        return false;
    }

    bool isEnemyTarget(std::uint32_t target) const {
        const auto* candidate = simulation_->find(target);
        if (candidate == nullptr || candidate->health <= 0) {
            return false;
        }
        for (const auto& entity : simulation_->entities()) {
            if (entity.selected && entity.health > 0 && entity.owner != candidate->owner) {
                return true;
            }
        }
        return false;
    }

    std::uint32_t unitAt(ScreenCoord position) const {
        std::uint32_t result = 0;
        float closest = std::numeric_limits<float>::max();
        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord unitPosition = gridToScreen(entity.position);
            const std::size_t frameIndex = static_cast<std::size_t>(art_.frameIndex(
                rules_.e2().image, animationSequence(entity.animationState), entity.animationFrame, entity.facing));
            const Rect bounds = renderer_.spriteBounds(rules_.e2().image, frameIndex, unitPosition,
                kRenderScale.unitVisualScale * camera_.zoom);
            const float expanded = 6.0F * camera_.zoom;
            const Rect hitBounds{bounds.x - expanded, bounds.y - expanded,
                bounds.width + expanded * 2.0F, bounds.height + expanded * 2.0F};
            if (hitBounds.contains(position.x, position.y)) {
                const float candidate = std::abs(unitPosition.x - position.x) +
                    std::abs(unitPosition.y - position.y);
                if (candidate < closest) {
                    closest = candidate;
                    result = entity.id;
                }
            }
        }
        return result;
    }

    static std::string directionName(Direction8 direction) {
        static constexpr const char* names[] = {"N", "NW", "W", "SW", "S", "SE", "E", "NE"};
        return names[static_cast<std::size_t>(direction)];
    }

    static std::wstring directionLabel(Direction8 direction) {
        return utf8ToWide(directionName(direction));
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

    std::wstring pendingActionLabel() const {
        switch (pendingAction_) {
        case PendingAction::Move: return T("move");
        case PendingAction::Patrol: return T("patrol");
        case PendingAction::AttackMove: return T("attack_move");
        case PendingAction::Attack: return T("attack");
        case PendingAction::None: return {};
        }
        return {};
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
        renderer_.drawRect({24.0F, 62.0F, 700.0F, 340.0F}, {0.01F, 0.02F, 0.025F, 0.94F});
        renderer_.drawBorder({24.0F, 62.0F, 700.0F, 340.0F}, {0.35F, 0.80F, 0.55F, 0.95F}, 2.0F);
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
            const int debugFrameIndex = art_.frameIndex(rules_.e2().image,
                animationSequence(debugEntity->animationState), debugEntity->animationFrame, debugEntity->facing);
            renderer_.drawText(L"ASSET E2 / CONS.SHP   FACINGS 8   SEQUENCE " +
                utf8ToWide(animationSequence(debugEntity->animationState)) +
                L"   ANIMATION FRAME " + std::to_wstring(debugEntity->animationFrame) +
                L"   SHP FRAME INDEX " + std::to_wstring(debugFrameIndex), {42.0F, 180.0F, 660.0F, 24.0F}, 14,
                {1.0F, 0.82F, 0.28F, 1.0F}, false);
        }
        renderer_.drawText(L"WORLD " + directionLabel(debugEntity != nullptr ? debugEntity->direction : Direction8::North) +
            L"   ART FACING " + std::to_wstring(debugEntity != nullptr ? debugEntity->facing : 0) +
            L"   FRAME " + std::to_wstring(debugEntity != nullptr ? debugEntity->animationFrame : 0),
            {42.0F, 206.0F, 660.0F, 24.0F}, 14, {1.0F, 0.82F, 0.28F, 1.0F}, false);
        renderer_.drawText(L"GROUND " + (debugEntity != nullptr ?
            (std::to_wstring(static_cast<int>(debugEntity->position.x)) + L"," +
                std::to_wstring(static_cast<int>(debugEntity->position.y))) : L"--") +
            L"   CAMERA " + std::to_wstring(static_cast<int>(camera_.worldCenter.x)) + L"," +
            std::to_wstring(static_cast<int>(camera_.worldCenter.y)) + L"   ZOOM " +
            formatNumber(camera_.zoom, 2), {42.0F, 232.0F, 660.0F, 24.0F}, 14,
            {0.72F, 0.90F, 0.80F, 1.0F}, false);
        renderer_.drawText(L"VOICE ACK " + std::to_wstring(audio_.voiceAckCount()) + L"   LAST " +
            utf8ToWide(audio_.lastVoiceEvent()), {42.0F, 258.0F, 660.0F, 24.0F}, 14,
            {0.90F, 0.72F, 0.38F, 1.0F}, false);
        renderer_.drawText(L"MOUSE " + std::to_wstring(static_cast<int>(mouse_.x)) + L"," +
            std::to_wstring(static_cast<int>(mouse_.y)) + L"   SELECTED " +
            std::to_wstring(selectedEntity() != nullptr ? 1 : 0), {42.0F, 284.0F, 660.0F, 24.0F}, 14,
            {0.72F, 0.90F, 0.80F, 1.0F}, false);
        renderer_.drawText(L"8-DIRECTION TEST [F6] " + std::wstring(directionTestMode_ ? L"ON" : L"OFF") +
            L"   NEXT " + directionLabel(static_cast<Direction8>(directionTestIndex_)),
            {42.0F, 310.0F, 660.0F, 24.0F}, 14, {0.62F, 0.78F, 1.0F, 1.0F}, false);
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
        const WorldCoord viewCorners[] = {
            camera_.toWorld({kWorldViewport.x, kWorldViewport.y}),
            camera_.toWorld({kWorldViewport.x + kWorldViewport.width, kWorldViewport.y}),
            camera_.toWorld({kWorldViewport.x, kWorldViewport.y + kWorldViewport.height}),
            camera_.toWorld({kWorldViewport.x + kWorldViewport.width, kWorldViewport.y + kWorldViewport.height}),
        };
        float minX = viewCorners[0].x;
        float maxX = viewCorners[0].x;
        float minY = viewCorners[0].y;
        float maxY = viewCorners[0].y;
        for (const WorldCoord corner : viewCorners) {
            minX = std::min(minX, corner.x);
            maxX = std::max(maxX, corner.x);
            minY = std::min(minY, corner.y);
            maxY = std::max(maxY, corner.y);
        }
        const Rect cameraView{field.x + std::clamp(minX / 64.0F, 0.0F, 1.0F) * field.width,
            field.y + std::clamp(minY / 64.0F, 0.0F, 1.0F) * field.height,
            std::max(2.0F, (std::clamp(maxX / 64.0F, 0.0F, 1.0F) -
                std::clamp(minX / 64.0F, 0.0F, 1.0F)) * field.width),
            std::max(2.0F, (std::clamp(maxY / 64.0F, 0.0F, 1.0F) -
                std::clamp(minY / 64.0F, 0.0F, 1.0F)) * field.height)};
        renderer_.drawBorder(cameraView, {1.0F, 0.84F, 0.25F, 0.85F}, 2.0F);
    }

    void renderSandboxPalette() {
        if (!sandboxPaletteVisible_) {
            return;
        }
        const Rect palette = sandboxPaletteRect();
        renderer_.drawRect(palette, {0.015F, 0.035F, 0.060F, 0.97F});
        renderer_.drawBorder(palette, {0.24F, 0.62F, 0.86F, 1.0F}, 3.0F);
        renderer_.drawText(T("sandbox_palette"), {palette.x + 12.0F, palette.y + 5.0F, 172.0F, 30.0F}, 18,
            {0.65F, 0.88F, 1.0F, 1.0F}, false);
        renderer_.drawText(sandboxPaletteCollapsed_ ? L"+" : L"_",
            {palette.x + palette.width - 76.0F, palette.y + 7.0F, 30.0F, 28.0F}, 18,
            {0.80F, 0.90F, 1.0F, 1.0F});
        renderer_.drawText(L"x", {palette.x + palette.width - 38.0F, palette.y + 7.0F, 30.0F, 28.0F}, 18,
            {1.0F, 0.45F, 0.35F, 1.0F});
        if (sandboxPaletteCollapsed_) {
            return;
        }
        const auto drawToolButton = [this](Rect rect, const std::wstring& label, bool active, bool enabled = true) {
            renderer_.drawRect(rect, enabled ? (active ? Color{0.18F, 0.25F, 0.12F, 1.0F} :
                Color{0.04F, 0.07F, 0.10F, 1.0F}) : Color{0.025F, 0.03F, 0.04F, 1.0F});
            renderer_.drawBorder(rect, enabled && active ? Color{0.95F, 0.76F, 0.20F, 1.0F} :
                Color{0.24F, 0.40F, 0.52F, 1.0F}, 2.0F);
            renderer_.drawText(label, rect, 14, enabled ? Color{0.88F, 0.90F, 0.86F, 1.0F} :
                Color{0.38F, 0.42F, 0.46F, 1.0F});
        };
        renderer_.drawText(T("category"), {palette.x + 16.0F, palette.y + 48.0F, 100.0F, 22.0F}, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        drawToolButton({palette.x + 16.0F, palette.y + 74.0F, 126.0F, 38.0F}, T("terrain"),
            editorTool_ == EditorTool::Terrain);
        drawToolButton({palette.x + 158.0F, palette.y + 74.0F, 126.0F, 38.0F}, T("unit"),
            editorTool_ == EditorTool::Unit);
        drawToolButton({palette.x + 16.0F, palette.y + 122.0F, 126.0F, 38.0F}, T("building"), false, false);
        drawToolButton({palette.x + 158.0F, palette.y + 122.0F, 126.0F, 38.0F}, L"资源", false, false);
        renderer_.drawText(T("current_object") + L": E2 " + T("unit_name"),
            {palette.x + 16.0F, palette.y + 166.0F, 268.0F, 26.0F}, 14,
            {0.92F, 0.84F, 0.34F, 1.0F}, false);
        renderer_.drawText(T("faction_owner") + L":", {palette.x + 16.0F, palette.y + 202.0F, 80.0F, 22.0F}, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        drawToolButton({palette.x + 16.0F, palette.y + 222.0F, 126.0F, 38.0F}, T("red"),
            placingOwner_ == Owner::Red);
        drawToolButton({palette.x + 158.0F, palette.y + 222.0F, 126.0F, 38.0F}, T("blue"),
            placingOwner_ == Owner::Blue);
        renderer_.drawText(T("mode") + L":", {palette.x + 16.0F, palette.y + 270.0F, 80.0F, 22.0F}, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        drawToolButton({palette.x + 16.0F, palette.y + 286.0F, 126.0F, 34.0F}, T("select"), !placing_);
        drawToolButton({palette.x + 158.0F, palette.y + 286.0F, 126.0F, 34.0F}, T("place"), placing_);
    }

    void renderEditor() {
        renderer_.setWorldStats(terrainTileCount_, simulation_->entities().size());
        renderer_.setWorldCamera(camera_.worldCenter, camera_.zoom, camera_.viewportCenter,
            kTileWidth, kTileHeight);
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.008F, 0.012F, 0.018F, 1.0F});
        const Rect world = kWorldViewport;
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
                    entity.facing)), entity.owner, position, kRenderScale.unitVisualScale * camera_.zoom);
            }
            if (entity.selected) {
                renderer_.drawDiamond(position, 38.0F * camera_.zoom, 14.0F * camera_.zoom,
                    {0.12F, 0.08F, 0.01F, 0.10F}, {0.95F, 0.85F, 0.25F, 1.0F});
            }
            const float healthRatio = std::clamp(static_cast<float>(entity.health) /
                static_cast<float>(entity.maxHealth), 0.0F, 1.0F);
            const float healthBarWidth = 44.0F * camera_.zoom;
            const Color healthColor = healthRatio >= 0.66F ? Color{0.20F, 0.90F, 0.22F, 1.0F} :
                healthRatio >= 0.33F ? Color{0.95F, 0.78F, 0.10F, 1.0F} :
                Color{0.92F, 0.12F, 0.08F, 1.0F};
            renderer_.drawRect({position.x - healthBarWidth * 0.5F, position.y - 10.0F * camera_.zoom,
                healthBarWidth, 4.0F * camera_.zoom},
                {0.16F, 0.02F, 0.02F, 1.0F});
            renderer_.drawRect({position.x - healthBarWidth * 0.5F, position.y - 10.0F * camera_.zoom,
                healthBarWidth * healthRatio, 4.0F * camera_.zoom}, healthColor);
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
        renderer_.drawText(T("production"), {1520.0F, 70.0F, 370.0F, 30.0F}, 20,
            {0.95F, 0.78F, 0.22F, 1.0F});
        const std::string tabs[] = {"building", "defense", "infantry", "vehicles"};
        for (int index = 0; index < 4; ++index) {
            const Rect tab{1520.0F + index * 96.0F, 108.0F, 88.0F, 42.0F};
            renderer_.drawImage(index == activeTab_ ? "ui.hud.tab_hover" : "ui.hud.tab", tab);
            renderer_.drawText(T(tabs[index]), tab, 16, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        renderer_.drawText(T("producer"), {1520.0F, 155.0F, 370.0F, 26.0F}, 16,
            {0.86F, 0.68F, 0.22F, 1.0F});
        for (int index = 0; index < 3; ++index) {
            const Rect producer{1520.0F + index * 126.0F, 180.0F, 118.0F, 42.0F};
            renderer_.drawImage("ui.hud.tab", producer, {0.55F, 0.55F, 0.58F, 1.0F});
            renderer_.drawText(L"--", producer, 18, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        for (int index = 0; index < 12; ++index) {
            const int column = index % 3;
            const int row = index / 3;
            const Rect product{1528.0F + column * 122.0F, 239.0F + row * 95.0F, 108.0F, 80.0F};
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
        if (pendingAction_ != PendingAction::None) {
            renderer_.drawText(L"TARGET: " + pendingActionLabel(), {1700.0F, 855.0F, 188.0F, 20.0F}, 12,
                {1.0F, 0.34F, 0.18F, 1.0F}, false);
        }
        const std::string commandKeys[] = {"move", "stop", "hold", "patrol", "attack_move", "", "", "", "", "", "", "", "", "", ""};
        for (int slot = 0; slot < 15; ++slot) {
            const int column = slot % 5;
            const int row = slot / 5;
            const Rect button{1523.0F + column * 76.0F, 878.0F + row * 52.0F, 72.0F, 48.0F};
            const bool hot = button.contains(mouse_.x, mouse_.y);
            const bool active = (slot == 0 && pendingAction_ == PendingAction::Move) ||
                (slot == 3 && pendingAction_ == PendingAction::Patrol) ||
                (slot == 4 && pendingAction_ == PendingAction::AttackMove);
            renderer_.drawImage(hot || active ? "ui.hud.button_hover" : "ui.hud.button", button,
                active ? Color{1.0F, 0.78F, 0.38F, 1.0F} : Color{1.0F, 1.0F, 1.0F, 1.0F});
            if (!commandKeys[slot].empty()) {
                const int fontSize = slot == 4 ? 9 : 13;
                renderer_.drawText(T(commandKeys[slot]), button, fontSize, {1.0F, 0.84F, 0.26F, 1.0F});
            }
        }

        renderer_.drawText(T("editor_title"), {116.0F, 10.0F, 280.0F, 30.0F}, 21,
            {1.0F, 0.82F, 0.20F, 1.0F}, false);
        renderer_.drawText(T("palette_hint"), {410.0F, 10.0F, 1350.0F, 30.0F}, 15,
            {0.80F, 0.82F, 0.82F, 1.0F}, false);
        renderer_.drawText(T("editor_hint"), {410.0F, 34.0F, 1350.0F, 24.0F}, 13,
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
        renderSandboxPalette();
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
    bool sandboxPaletteVisible_ = true;
    bool sandboxPaletteCollapsed_ = false;
    bool sandboxPaletteDragging_ = false;
    bool directionTestMode_ = false;
    int activeTab_ = 0;
    int directionTestIndex_ = 0;
    int pressedMenuButton_ = -1;
    int hoveredMenuButton_ = -1;
    float toastTime_ = 0.0F;
    float directionTestTime_ = 0.0F;
    ScreenCoord mouse_{};
    ScreenCoord dragStart_{};
    ScreenCoord dragEnd_{};
    ScreenCoord sandboxPalettePosition_{118.0F, 92.0F};
    ScreenCoord sandboxPaletteDragOffset_{};
    IsometricCamera camera_{};
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
