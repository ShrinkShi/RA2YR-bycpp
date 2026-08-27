#include "GameData/Rules.h"
#include "GameData/Art.h"
#include "GameData/Terrain.h"
#include "GameData/Veterancy.h"
#include "Editor/EditorToolController.h"
#include "Client/Hud/UnitStatusViewModel.h"
#include "GameData/UI.h"
#include "Renderer/D3D11Renderer.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_audio.h>
#include <windows.h>

#include <algorithm>
#include <array>
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
#include <random>
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

// Keep the world grid readable enough for the three infantry subcells. UI
// coordinates remain on the independent 1920x1080 logical canvas.
constexpr RenderScaleConfig kRenderScale{1.35F, 1.65F, 1.10F, 1.20F};
constexpr float kTileWidth = 44.0F * kRenderScale.worldRenderScale;
constexpr float kTileHeight = 22.0F * kRenderScale.worldRenderScale;
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

struct SandboxPaletteLayout {
    Rect window;
    Rect titleBar;
    Rect collapseButton;
    Rect closeButton;
    Rect categoryLabel;
    Rect terrainButton;
    Rect unitButton;
    Rect buildingButton;
    Rect resourceButton;
    Rect objectLabel;
    Rect ownerLabel;
    Rect redButton;
    Rect blueButton;
    Rect modeLabel;
    Rect selectButton;
    Rect placeButton;
    std::array<Rect, 6> toolButtons{};
    std::array<Rect, 4> brushButtons{};
    Rect assetLabel;
    Rect veterancyButton;
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
    struct VoiceSet {
        std::vector<std::string> names;
        std::vector<std::vector<float>> samples;
        bool noImmediateRepeat = true;
        int lastSampleIndex = -1;
    };

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

    bool loadVoiceSet(const std::filesystem::path& configPath,
        const std::array<std::pair<AudioCue, std::string>, 3>& bindings, std::string& error) {
        westwood::IniDocument config;
        if (!config.load(configPath, error)) {
            return false;
        }
        for (const auto& [cue, section] : bindings) {
            std::vector<std::string> files;
            std::stringstream fileList(config.get(section, "Files"));
            std::string file;
            while (std::getline(fileList, file, ',')) {
                const auto first = file.find_first_not_of(" \t\r\n");
                const auto last = file.find_last_not_of(" \t\r\n");
                if (first != std::string::npos) {
                    files.push_back(file.substr(first, last - first + 1));
                }
            }
            if (files.empty()) {
                const std::string legacyFile = config.get(section, "File");
                if (!legacyFile.empty()) {
                    files.push_back(legacyFile);
                }
            }
            if (files.empty()) {
                error = "Missing audio Files in [" + section + "]";
                return false;
            }
            VoiceSet set;
            set.noImmediateRepeat = config.getBool(section, "NoImmediateRepeat", true);
            for (const std::string& fileName : files) {
                std::vector<float> samples;
                const std::filesystem::path audioPath = configPath.parent_path() / fileName;
                if (!decodeWav(audioPath, samples, error)) {
                    return false;
                }
                set.names.push_back(fileName);
                set.samples.push_back(std::move(samples));
                std::cerr << "[Audio] Loaded voice sample " << section << ": " << audioPath.string() << '\n';
            }
            voiceSamples_[cue] = std::move(set);
            std::cerr << "[Audio] VoiceSet " << section << " loaded with " << files.size() << " samples\n";
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
        if (voice != voiceSamples_.end() && !voice->second.samples.empty()) {
            VoiceSet& set = voice->second;
            std::uniform_int_distribution<std::size_t> distribution(0, set.samples.size() - 1U);
            std::size_t sampleIndex = distribution(random_);
            if (set.noImmediateRepeat && set.samples.size() > 1U &&
                static_cast<int>(sampleIndex) == set.lastSampleIndex) {
                sampleIndex = (sampleIndex + 1U) % set.samples.size();
            }
            set.lastSampleIndex = static_cast<int>(sampleIndex);
            lastVoiceSample_ = set.names[sampleIndex];
            std::cerr << "[Audio] Playing " << cueName(cue) << " sample " << lastVoiceSample_ << '\n';
            if (!SDL_PutAudioStreamData(voiceStream_, set.samples[sampleIndex].data(),
                static_cast<int>(set.samples[sampleIndex].size() * sizeof(float)))) {
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
    std::unordered_map<AudioCue, VoiceSet> voiceSamples_;
    std::mt19937 random_{std::random_device{}()};
    std::uint32_t voiceAckCount_ = 0;
    std::string lastVoiceEvent_;
    std::string lastVoiceSample_;
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
        if (!ui_.load(contentRoot_ / "INI/UI.ini", error)) {
            return false;
        }
        initializeUiLayout();
        camera_.projection.tileWidth = kTileWidth;
        camera_.projection.tileHeight = kTileHeight;
        const Rect viewport = worldViewport();
        camera_.viewportCenter = {viewport.x + viewport.width * 0.5F,
            viewport.y + viewport.height * 0.5F};
        camera_.worldCenter = {0.0F, 0.0F};
        loadStrings();
        if (!loadRuntimeAssets(error)) {
            return false;
        }
        if (!terrainDatabase_.load(contentRoot_ / "INI/Terrain.ini", error) ||
            !veterancy_.load(contentRoot_ / "INI/Rules.ini", error)) {
            assetError_ = utf8ToWide(error);
            return false;
        }
        terrainMap_.fill("GRASS");
        std::string audioError;
        const std::array<std::pair<AudioCue, std::string>, 3> voiceBindings = {
            std::pair{AudioCue::VoiceSelect, rules_.e2().voiceSelect},
            std::pair{AudioCue::VoiceMoveAcknowledgement, rules_.e2().voiceMove},
            std::pair{AudioCue::VoiceAttackAcknowledgement, rules_.e2().voiceAttack},
        };
        if (!audio_.loadVoiceSet(contentRoot_ / "assets/audio/voices.ini", voiceBindings, audioError)) {
            std::cerr << "[Audio] Voice set unavailable; using procedural fallback: " << audioError << '\n';
        }
        if (!buildTerrain(error)) {
            return false;
        }
        const gamedata::ArtDefinition* animationDefinition = art_.find(rules_.e2().image);
        if (animationDefinition == nullptr) {
            error = "Art.ini has no definition for the E2 image " + rules_.e2().image;
            return false;
        }
        simulation_ = std::make_unique<simulation::Simulation>(*animationDefinition, &veterancy_);
        editorTools_ = std::make_unique<editor::EditorToolController>(terrainMap_, terrainDatabase_, rules_, *simulation_);
        if (!editorTools_->loadBrushPresets(contentRoot_ / "INI/Editor.ini", error)) {
            return false;
        }
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
            rebuildTerrainIfDirty();
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

    Rect worldViewport() const {
        return ui_.rect("world.viewport");
    }

    void initializeUiLayout() {
        const std::pair<std::string, std::string> menuDefinitions[] = {
            {"campaign", "main.menu.campaign"}, {"load", "main.menu.load"},
            {"skirmish", "main.menu.skirmish"}, {"online", "main.menu.online"},
            {"lan", "main.menu.lan"}, {"settings", "main.menu.settings"},
            {"statistics", "main.menu.statistics"}, {"editor", "main.menu.editor"},
            {"exit", "main.menu.exit"},
        };
        menuButtons_.clear();
        for (const auto& [key, rectKey] : menuDefinitions) {
            menuButtons_.push_back({key, "ui.main.button", "ui.main.button_hover", ui_.rect(rectKey)});
        }
        const Rect sandbox = ui_.rect("sandbox.window");
        sandboxPalettePosition_ = {sandbox.x, sandbox.y};
    }

    void loadStrings() {
        strings_ = {
            {"title", L"COMMAND & CONQUER"}, {"subtitle", L"YURI'S REVENGE"},
            {"campaign", L"CAMPAIGN"}, {"load", L"LOAD GAME"}, {"skirmish", L"SKIRMISH"},
            {"online", L"ONLINE"}, {"lan", L"LAN"}, {"settings", L"SETTINGS"},
            {"statistics", L"STATISTICS"}, {"editor", L"MAP EDITOR"}, {"exit", L"EXIT GAME"},
            {"unimplemented", L"NOT IMPLEMENTED"}, {"editor_title", L"EDITOR SANDBOX"},
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
             {"tool_pointer", L"指针"}, {"tool_pencil", L"铅笔"}, {"tool_eraser", L"橡皮"},
             {"tool_brush", L"刷子"}, {"tool_fill", L"油漆桶"}, {"tool_eyedropper", L"取色器"},
             {"faction_owner", L"所属"}, {"disabled", L"不可用"}, {"command_card", L"命令面板"},
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
        const std::string imageIds[] = {
            "ui.main.background", "ui.main.button", "ui.main.button_hover", "ui.hud.leftbar",
            "ui.hud.rightbar", "ui.hud.button", "ui.hud.button_hover", "ui.hud.tab", "ui.hud.tab_hover",
            "ui.hud.background", "ui.hud.minimap.background", "ui.hud.model.background",
            "ui.hud.unitinfo.background", "ui.hud.portrait.background", "ui.hud.commandcard.background",
            "ui.hud.production.background", "ui.hud.strategic.background", "ui.editor.sandbox.background",
            "ui.editor.tool.pointer", "ui.editor.tool.pencil", "ui.editor.tool.eraser", "ui.editor.tool.brush",
            "ui.editor.tool.fill", "ui.editor.tool.eyedropper", "ui.unitstatus.armor",
        };
        for (const std::string& id : imageIds) {
            const std::filesystem::path path = ui_.imagePath(id, contentRoot_);
            if (path.empty()) {
                error = "UI.ini has no image path for " + id;
                assetError_ = utf8ToWide(error);
                return false;
            }
            if (!renderer_.loadTexture(id, path, error)) {
                assetError_ = utf8ToWide(error);
                std::cerr << "[Content][Error] " << error << '\n';
                return false;
            }
        }
        unitStatusWeaponImageIds_.clear();
        for (const gamedata::UnitDefinition& unit : rules_.infantry()) {
            for (const gamedata::WeaponDefinition& weapon : unit.weapons) {
                const std::string imageId = "ui.unitstatus.weapon." + weapon.id;
                if (unitStatusWeaponImageIds_.contains(weapon.id)) {
                    continue;
                }
                std::filesystem::path path = ui_.imagePath(imageId, contentRoot_);
                if (path.empty() && !weapon.icon.empty()) {
                    path = contentRoot_ / weapon.icon;
                }
                if (path.empty()) {
                    continue;
                }
                if (!renderer_.loadTexture(imageId, path, error)) {
                    assetError_ = utf8ToWide(error);
                    std::cerr << "[Content][Error] " << error << '\n';
                    return false;
                }
                unitStatusWeaponImageIds_.emplace(weapon.id, imageId);
            }
        }
        assetReady_ = true;
        std::cerr << "[Content] Loaded project Rules.ini, Art.ini, CONS.SHP and unittem.pal\n";
        std::cerr << "[Content] Runtime root: " << contentRoot_.string() << '\n';
        return true;
    }

    bool buildTerrain(std::string& error) {
        std::vector<renderer::TerrainTileVisual> tiles;
        tiles.reserve(static_cast<std::size_t>(terrainMap_.width() * terrainMap_.height()));
        for (int x = 0; x < terrainMap_.width(); ++x) {
            for (int y = 0; y < terrainMap_.height(); ++y) {
                const editor::TerrainCell& cell = terrainMap_.cell({x, y});
                if (!cell.exists) {
                    continue;
                }
                const bool alternate = ((x + y) & 1) != 0;
                tiles.push_back({{static_cast<float>(x), static_cast<float>(y)}, kTileWidth, kTileHeight,
                    alternate ? Color{0.10F, 0.25F, 0.14F, 1.0F} : Color{0.08F, 0.21F, 0.12F, 1.0F},
                    {0.18F, 0.38F, 0.20F, 0.70F}});
            }
        }
        terrainTileCount_ = tiles.size();
        renderer_.buildStaticTerrain(tiles, error);
        static_cast<void>(terrainMap_.consumeDirty());
        return error.empty();
    }

    void rebuildTerrainIfDirty() {
        if (!terrainMap_.consumeDirty()) {
            return;
        }
        std::string error;
        if (!buildTerrain(error)) {
            assetError_ = utf8ToWide(error);
            std::cerr << "[Terrain][Error] " << error << '\n';
        }
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
        return worldViewport().contains(position.x, position.y);
    }

    Rect selectionRect() const {
        return {std::min(dragStart_.x, dragEnd_.x), std::min(dragStart_.y, dragEnd_.y),
            std::abs(dragEnd_.x - dragStart_.x), std::abs(dragEnd_.y - dragStart_.y)};
    }

    void selectScreenBox() {
        const Rect selection = selectionRect();
        const std::array<WorldCoord, 4> corners = {
            screenToWorld({selection.x, selection.y}),
            screenToWorld({selection.x + selection.width, selection.y}),
            screenToWorld({selection.x + selection.width, selection.y + selection.height}),
            screenToWorld({selection.x, selection.y + selection.height}),
        };
        simulation_->selectBox(corners);
    }

    void updateCamera(float seconds) {
        const Rect viewport = worldViewport();
        if (!inWorld(mouse_) || dragging_ || sandboxPaletteDragging_ ||
            (sandboxPaletteVisible_ && sandboxPaletteRect().contains(mouse_.x, mouse_.y))) {
            return;
        }
        ScreenCoord delta{};
        if (mouse_.x <= viewport.x + kCameraEdgeThreshold) {
            delta.x -= kCameraPanPixelsPerSecond * seconds;
        } else if (mouse_.x >= viewport.x + viewport.width - kCameraEdgeThreshold) {
            delta.x += kCameraPanPixelsPerSecond * seconds;
        }
        if (mouse_.y <= viewport.y + kCameraEdgeThreshold) {
            delta.y -= kCameraPanPixelsPerSecond * seconds;
        } else if (mouse_.y >= viewport.y + viewport.height - kCameraEdgeThreshold) {
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
                if (editorStroke_ && leftMouseDown_ && inWorld(mouse_)) {
                    static_cast<void>(editorTools_->continueStroke(screenToGrid(mouse_), unitAt(mouse_)));
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
            } else if (placing_ || editorTools_->state().editsWorld()) {
                placing_ = false;
                editorTools_->state().tool = editor::EditorToolId::Pointer;
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
            editorTools_->state().category = editor::EditorAssetCategory::Unit;
            editorTools_->state().owner = Owner::Red;
            editorTools_->state().tool = editor::EditorToolId::Pencil;
        } else if (key == SDLK_B) {
            placingOwner_ = Owner::Blue;
            editorTool_ = EditorTool::Unit;
            placing_ = true;
            editorTools_->state().category = editor::EditorAssetCategory::Unit;
            editorTools_->state().owner = Owner::Blue;
            editorTools_->state().tool = editor::EditorToolId::Pencil;
        } else if (key == SDLK_T) {
            editorTool_ = editorTool_ == EditorTool::Terrain ? EditorTool::Unit : EditorTool::Terrain;
            placing_ = false;
            editorTools_->state().category = editorTool_ == EditorTool::Terrain ?
                editor::EditorAssetCategory::Terrain : editor::EditorAssetCategory::Unit;
            editorTools_->state().tool = editor::EditorToolId::Pointer;
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
        lastMouseEvent_ = leftButton ? "LDown" : "RDown";
        if (leftButton) {
            leftMouseDown_ = true;
        }
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
        if (editorTools_->state().editsWorld() && inWorld(mouse_)) {
            editorStroke_ = editorTools_->state().tool == editor::EditorToolId::Pencil ||
                editorTools_->state().tool == editor::EditorToolId::Brush;
            editorTools_->beginStroke();
            static_cast<void>(editorTools_->continueStroke(screenToGrid(mouse_), unitAt(mouse_)));
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
        lastMouseEvent_ = "LUp";
        leftMouseDown_ = false;
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
        if (editorStroke_) {
            editorTools_->endStroke();
            editorStroke_ = false;
            return;
        }
        if (!dragging_) {
            return;
        }
        // Button-up coordinates are authoritative when the final motion event
        // was coalesced by SDL or the window manager.
        dragEnd_ = mouse_;
        dragging_ = false;
        const float dragDistance = std::hypot(dragEnd_.x - dragStart_.x, dragEnd_.y - dragStart_.y);
        if (dragDistance > 8.0F) {
            selectScreenBox();
            if (selectedEntity() != nullptr) {
                audio_.play(AudioCue::VoiceSelect);
            }
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
        const Rect configured = ui_.rect("sandbox.window");
        const Rect titleBar = ui_.relativeRect("sandbox.title_bar");
        return {sandboxPalettePosition_.x, sandboxPalettePosition_.y, configured.width,
            sandboxPaletteCollapsed_ ? titleBar.height : configured.height};
    }

    SandboxPaletteLayout sandboxPaletteLayout() const {
        const Rect palette = sandboxPaletteRect();
        SandboxPaletteLayout layout;
        layout.window = palette;
        const auto child = [this, palette](std::string_view key) {
            const Rect relative = ui_.relativeRect(key);
            return Rect{palette.x + relative.x, palette.y + relative.y, relative.width, relative.height};
        };
        layout.titleBar = child("sandbox.title_bar");
        layout.collapseButton = child("sandbox.collapse");
        layout.closeButton = child("sandbox.close");
        layout.categoryLabel = child("sandbox.category");
        layout.terrainButton = child("sandbox.terrain");
        layout.unitButton = child("sandbox.unit");
        layout.buildingButton = child("sandbox.building");
        layout.resourceButton = child("sandbox.resource");
        layout.objectLabel = child("sandbox.object");
        layout.ownerLabel = child("sandbox.owner");
        layout.redButton = child("sandbox.red");
        layout.blueButton = child("sandbox.blue");
        layout.modeLabel = child("sandbox.mode");
        layout.selectButton = child("sandbox.select");
        layout.placeButton = child("sandbox.place");
        for (int index = 0; index < 6; ++index) {
            layout.toolButtons[static_cast<std::size_t>(index)] = child("sandbox.tool." + std::to_string(index));
        }
        for (int index = 0; index < 4; ++index) {
            layout.brushButtons[static_cast<std::size_t>(index)] = child("sandbox.brush." + std::to_string(index));
        }
        layout.assetLabel = child("sandbox.asset");
        layout.veterancyButton = child("sandbox.veterancy");
        return layout;
    }

    bool handleSandboxPaletteClick() {
        const SandboxPaletteLayout layout = sandboxPaletteLayout();
        if (!sandboxPaletteVisible_ || !layout.window.contains(mouse_.x, mouse_.y)) {
            return false;
        }
        if (layout.titleBar.contains(mouse_.x, mouse_.y)) {
            if (layout.closeButton.contains(mouse_.x, mouse_.y)) {
                sandboxPaletteVisible_ = false;
            } else if (layout.collapseButton.contains(mouse_.x, mouse_.y)) {
                sandboxPaletteCollapsed_ = !sandboxPaletteCollapsed_;
            } else {
                sandboxPaletteDragging_ = true;
                sandboxPaletteDragOffset_ = {mouse_.x - layout.window.x, mouse_.y - layout.window.y};
            }
            audio_.play(AudioCue::UIClick);
            return true;
        }
        if (sandboxPaletteCollapsed_) {
            return true;
        }
        const editor::EditorToolId toolIds[] = {editor::EditorToolId::Pointer,
            editor::EditorToolId::Pencil, editor::EditorToolId::Eraser, editor::EditorToolId::Brush,
            editor::EditorToolId::FillBucket, editor::EditorToolId::Eyedropper};
        for (std::size_t index = 0; index < layout.toolButtons.size(); ++index) {
            if (layout.toolButtons[index].contains(mouse_.x, mouse_.y)) {
                editorTools_->state().tool = toolIds[index];
                editorTools_->state().placing = toolIds[index] != editor::EditorToolId::Pointer;
                placing_ = editorTools_->state().placing;
                audio_.play(AudioCue::UIClick);
                return true;
            }
        }
        if (layout.terrainButton.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Terrain;
            editorTools_->state().category = editor::EditorAssetCategory::Terrain;
        } else if (layout.unitButton.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Unit;
            editorTools_->state().category = editor::EditorAssetCategory::Unit;
        } else if (layout.buildingButton.contains(mouse_.x, mouse_.y) ||
            layout.resourceButton.contains(mouse_.x, mouse_.y)) {
            return true;
        } else if (layout.redButton.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Red;
            editorTools_->state().owner = Owner::Red;
        } else if (layout.blueButton.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Blue;
            editorTools_->state().owner = Owner::Blue;
        } else if (layout.selectButton.contains(mouse_.x, mouse_.y)) {
            placing_ = false;
            editorTools_->state().tool = editor::EditorToolId::Pointer;
            editorTools_->state().placing = false;
        } else if (layout.placeButton.contains(mouse_.x, mouse_.y)) {
            editorTool_ = EditorTool::Unit;
            placing_ = true;
            editorTools_->state().category = editor::EditorAssetCategory::Unit;
            editorTools_->state().tool = editor::EditorToolId::Pencil;
            editorTools_->state().placing = true;
        } else {
            for (std::size_t index = 0; index < layout.brushButtons.size(); ++index) {
                if (layout.brushButtons[index].contains(mouse_.x, mouse_.y)) {
                    editorTools_->state().brushPreset = index;
                    audio_.play(AudioCue::UIClick);
                    return true;
                }
            }
            return true;
        }
        audio_.play(AudioCue::UIClick);
        return true;
    }

    bool handleEditorUiClick() {
        if (handleSandboxPaletteClick()) {
            return true;
        }
        if (ui_.rect("hud.strategic.collapse").contains(mouse_.x, mouse_.y)) {
            strategicCollapsed_ = !strategicCollapsed_;
            audio_.play(AudioCue::UIClick);
            return true;
        }
        for (int index = 0; index < 4; ++index) {
            if (ui_.rect("hud.production.tab." + std::to_string(index)).contains(mouse_.x, mouse_.y)) {
                activeTab_ = index;
                audio_.play(AudioCue::UIClick);
                return true;
            }
        }
        for (int index = 0; index < 3; ++index) {
            if (ui_.rect("hud.production.producer." + std::to_string(index)).contains(mouse_.x, mouse_.y)) {
                return true; // Producer slots are intentionally disabled until Simulation has entities for them.
            }
        }
        if (ui_.rect("editor.menu").contains(mouse_.x, mouse_.y)) {
            mode_ = AppMode::MainMenu;
            return true;
        }
        const Rect card = ui_.rect("hud.command_card");
        if (card.contains(mouse_.x, mouse_.y)) {
            for (int slot = 0; slot < 15; ++slot) {
                if (!ui_.childRect("hud.command_card", "hud.command_card.slot." + std::to_string(slot))
                        .contains(mouse_.x, mouse_.y)) {
                    continue;
                }
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

    std::size_t selectedCount() const {
        std::size_t count = 0;
        for (const auto& entity : simulation_->entities()) {
            if (entity.selected && entity.health > 0) {
                ++count;
            }
        }
        return count;
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
            // Use the visible SHP footprint, not the ground anchor, for hit
            // testing. Infantry pivots can be offset substantially from the
            // ground point; centering the hit box on the anchor made clicks
            // on the rendered body miss while still affecting nearby ground.
            const float hitWidth = std::clamp(bounds.width * 0.85F,
                18.0F * camera_.zoom, 42.0F * camera_.zoom);
            const float hitHeight = std::clamp(bounds.height * 0.85F,
                24.0F * camera_.zoom, 72.0F * camera_.zoom);
            const Rect hitBounds{bounds.x + (bounds.width - hitWidth) * 0.5F,
                bounds.y + (bounds.height - hitHeight) * 0.5F, hitWidth, hitHeight};
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
            std::to_wstring(selectedCount()), {42.0F, 284.0F, 660.0F, 24.0F}, 14,
            {0.72F, 0.90F, 0.80F, 1.0F}, false);
        renderer_.drawText(L"MOUSE EVENT " + utf8ToWide(lastMouseEvent_) + L"   DRAG " +
            std::wstring(dragging_ ? L"ON" : L"OFF"), {42.0F, 336.0F, 660.0F, 24.0F}, 14,
            {0.72F, 0.90F, 0.80F, 1.0F}, false);
        renderer_.drawText(L"8-DIRECTION TEST [F6] " + std::wstring(directionTestMode_ ? L"ON" : L"OFF") +
            L"   NEXT " + directionLabel(static_cast<Direction8>(directionTestIndex_)),
            {42.0F, 310.0F, 660.0F, 24.0F}, 14, {0.62F, 0.78F, 1.0F, 1.0F}, false);
    }

    void renderMainMenu() {
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.0F, 0.0F, 0.0F, 1.0F});
        renderer_.drawImage("ui.main.background", ui_.rect("main.background"));
        renderer_.drawText(T("title"), ui_.rect("main.title"), 36, {1.0F, 0.84F, 0.34F, 1.0F});
        renderer_.drawText(T("subtitle"), ui_.rect("main.subtitle"), 50, {1.0F, 0.28F, 0.10F, 1.0F});
        renderer_.drawText(L"RA2YR-BYCPP  //  MAIN CONTROL", ui_.rect("main.footer"), 16, {0.86F, 0.62F, 0.28F, 1.0F});
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

    void drawHudPanel(Rect panel, const std::string& backgroundAsset, const std::wstring& title) {
        renderer_.drawImage(backgroundAsset, panel);
        renderer_.drawText(title, {panel.x + 12.0F, panel.y + 7.0F, panel.width - 24.0F, 26.0F}, 16,
            {1.0F, 0.82F, 0.20F, 1.0F}, false);
    }

    void renderMiniMap(Rect panel) {
        drawHudPanel(panel, "ui.hud.minimap.background", T("minimap"));
        const Rect field = ui_.childRect("hud.minimap", "minimap.field");
        renderer_.drawRect(field, {0.025F, 0.10F, 0.065F, 1.0F});
        const float cellWidth = field.width / static_cast<float>(terrainMap_.width());
        const float cellHeight = field.height / static_cast<float>(terrainMap_.height());
        for (int y = 0; y < terrainMap_.height(); ++y) {
            for (int x = 0; x < terrainMap_.width(); ++x) {
                if (terrainMap_.cell({x, y}).exists) {
                    const Color terrainColor = ((x + y) & 1) != 0 ?
                        Color{0.12F, 0.28F, 0.16F, 1.0F} : Color{0.08F, 0.22F, 0.12F, 1.0F};
                    renderer_.drawRect({field.x + static_cast<float>(x) * cellWidth,
                        field.y + static_cast<float>(y) * cellHeight, cellWidth + 1.0F, cellHeight + 1.0F},
                        terrainColor);
                }
            }
        }
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
        const Rect viewport = worldViewport();
        const ScreenCoord viewportCenter{
            viewport.x + viewport.width * 0.5F,
            viewport.y + viewport.height * 0.5F};
        const WorldCoord cameraCenter = camera_.toWorld(viewportCenter);
        const WorldCoord viewCorners[] = {
            camera_.toWorld({viewport.x, viewport.y}),
            camera_.toWorld({viewport.x + viewport.width, viewport.y}),
            camera_.toWorld({viewport.x, viewport.y + viewport.height}),
            camera_.toWorld({viewport.x + viewport.width, viewport.y + viewport.height}),
        };
        float minRelativeX = viewCorners[0].x - cameraCenter.x;
        float maxRelativeX = minRelativeX;
        float minRelativeY = viewCorners[0].y - cameraCenter.y;
        float maxRelativeY = minRelativeY;
        for (const WorldCoord corner : viewCorners) {
            minRelativeX = std::min(minRelativeX, corner.x - cameraCenter.x);
            maxRelativeX = std::max(maxRelativeX, corner.x - cameraCenter.x);
            minRelativeY = std::min(minRelativeY, corner.y - cameraCenter.y);
            maxRelativeY = std::max(maxRelativeY, corner.y - cameraCenter.y);
        }
        const float viewWidth = std::clamp(maxRelativeX - minRelativeX, 0.0F, 64.0F);
        const float viewHeight = std::clamp(maxRelativeY - minRelativeY, 0.0F, 64.0F);
        const float mapOriginX = std::clamp(cameraCenter.x + minRelativeX, 0.0F, 64.0F - viewWidth);
        const float mapOriginY = std::clamp(cameraCenter.y + minRelativeY, 0.0F, 64.0F - viewHeight);
        const Rect cameraView{field.x + mapOriginX / 64.0F * field.width,
            field.y + mapOriginY / 64.0F * field.height,
            std::max(2.0F, viewWidth / 64.0F * field.width),
            std::max(2.0F, viewHeight / 64.0F * field.height)};
        renderer_.drawBorder(cameraView, {1.0F, 0.84F, 0.25F, 0.85F}, 2.0F);
    }

    void renderSandboxPalette() {
        if (!sandboxPaletteVisible_) {
            return;
        }
        const SandboxPaletteLayout layout = sandboxPaletteLayout();
        const Rect palette = layout.window;
        renderer_.drawImage("ui.editor.sandbox.background", palette);
        renderer_.drawText(T("sandbox_palette"), {palette.x + 12.0F, palette.y + 5.0F, 250.0F, 30.0F}, 18,
            {0.65F, 0.88F, 1.0F, 1.0F}, false);
        renderer_.drawText(sandboxPaletteCollapsed_ ? L"+" : L"_", layout.collapseButton, 18,
            {0.80F, 0.90F, 1.0F, 1.0F});
        renderer_.drawText(L"x", layout.closeButton, 18,
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
        renderer_.drawText(T("category"), layout.categoryLabel, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        const editor::EditorToolId toolIds[] = {editor::EditorToolId::Pointer,
            editor::EditorToolId::Pencil, editor::EditorToolId::Eraser, editor::EditorToolId::Brush,
            editor::EditorToolId::FillBucket, editor::EditorToolId::Eyedropper};
        const std::string toolImages[] = {"ui.editor.tool.pointer", "ui.editor.tool.pencil",
            "ui.editor.tool.eraser", "ui.editor.tool.brush", "ui.editor.tool.fill",
            "ui.editor.tool.eyedropper"};
        const std::wstring toolLabels[] = {L"指针", L"铅笔", L"橡皮", L"刷子", L"油漆桶", L"取色器"};
        for (std::size_t index = 0; index < layout.toolButtons.size(); ++index) {
            const bool active = editorTools_->state().tool == toolIds[index];
            renderer_.drawImage(toolImages[index], layout.toolButtons[index],
                active ? Color{1.0F, 0.82F, 0.42F, 1.0F} : Color{1.0F, 1.0F, 1.0F, 1.0F});
            renderer_.drawText(toolLabels[index], layout.toolButtons[index], 11,
                active ? Color{1.0F, 0.88F, 0.24F, 1.0F} : Color{0.88F, 0.90F, 0.86F, 1.0F});
        }
        drawToolButton(layout.terrainButton, T("terrain"),
            editorTools_->state().category == editor::EditorAssetCategory::Terrain);
        drawToolButton(layout.unitButton, T("unit"),
            editorTools_->state().category == editor::EditorAssetCategory::Unit);
        drawToolButton(layout.buildingButton, T("building"), false, false);
        drawToolButton(layout.resourceButton, L"资源", false, false);
        const std::string& currentAsset = editorTools_->state().currentAsset();
        std::string assetName = currentAsset;
        if (editorTools_->state().category == editor::EditorAssetCategory::Terrain) {
            if (const gamedata::TerrainDefinition* definition = terrainDatabase_.find(currentAsset)) {
                assetName = definition->uiName;
            }
        } else if (editorTools_->state().category == editor::EditorAssetCategory::Unit) {
            if (const gamedata::UnitDefinition* definition = rules_.findUnit(currentAsset)) {
                assetName = definition->name;
            }
        }
        const std::wstring assetText = utf8ToWide(currentAsset + " / " + assetName);
        renderer_.drawText(T("current_object") + L": " + assetText, layout.assetLabel, 14,
            {0.92F, 0.84F, 0.34F, 1.0F}, false);
        renderer_.drawText(T("faction_owner") + L":", layout.ownerLabel, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        drawToolButton(layout.redButton, T("red"),
            editorTools_->state().owner == Owner::Red);
        drawToolButton(layout.blueButton, T("blue"),
            editorTools_->state().owner == Owner::Blue);
        renderer_.drawText(T("mode") + L":", layout.modeLabel, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        const auto& presets = editorTools_->brushPresets();
        renderer_.drawText(L"笔刷", {palette.x + 16.0F, palette.y + 294.0F, 80.0F, 22.0F}, 13,
            {0.60F, 0.76F, 0.84F, 1.0F}, false);
        for (std::size_t index = 0; index < layout.brushButtons.size() && index < presets.size(); ++index) {
            drawToolButton(layout.brushButtons[index], utf8ToWide(presets[index].id),
                editorTools_->state().brushPreset == index);
        }
        drawToolButton(layout.selectButton, T("select"),
            editorTools_->state().tool == editor::EditorToolId::Pointer);
        drawToolButton(layout.placeButton, T("place"),
            editorTools_->state().tool != editor::EditorToolId::Pointer);
        drawToolButton(layout.veterancyButton, L"军阶", false);
    }

    void drawGroundSelectionEllipse(WorldCoord center, float radius, Color color, bool dashed) {
        constexpr int kSegments = 32;
        constexpr float kPi = 3.14159265358979323846F;
        for (int index = 0; index < kSegments; ++index) {
            if (dashed && ((index / 2) % 2 == 1)) {
                continue;
            }
            const float firstAngle = static_cast<float>(index) * 2.0F * kPi /
                static_cast<float>(kSegments);
            const float secondAngle = static_cast<float>(index + 1) * 2.0F * kPi /
                static_cast<float>(kSegments);
            const WorldCoord first{center.x + std::cos(firstAngle) * radius,
                center.y + std::sin(firstAngle) * radius};
            const WorldCoord second{center.x + std::cos(secondAngle) * radius,
                center.y + std::sin(secondAngle) * radius};
            renderer_.drawLine(gridToScreen(first), gridToScreen(second), color,
                std::max(1.5F, 2.0F * camera_.zoom));
        }
    }

    void renderEditor() {
        renderer_.setWorldStats(terrainTileCount_, simulation_->entities().size());
        renderer_.setWorldCamera(camera_.worldCenter, camera_.zoom, camera_.viewportCenter,
            kTileWidth, kTileHeight);
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.008F, 0.012F, 0.018F, 1.0F});
        const Rect world = worldViewport();
        renderer_.drawRect(world, {0.04F, 0.07F, 0.05F, 1.0F});
        renderer_.drawStaticTerrain();
        renderer_.drawBorder(world, {0.65F, 0.48F, 0.18F, 1.0F}, 3.0F);

        const std::uint32_t hoveredUnit = !dragging_ && inWorld(mouse_) ? unitAt(mouse_) : 0;
        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord position = gridToScreen(entity.position);
            const std::size_t frameIndex = static_cast<std::size_t>(art_.frameIndex(
                rules_.e2().image, animationSequence(entity.animationState), entity.animationFrame,
                entity.facing));
            const bool previewed = !entity.selected &&
                ((hoveredUnit == entity.id) || (dragging_ && selectionRect().contains(position.x, position.y)));
            if (assetReady_) {
                renderer_.drawSprite(rules_.e2().image, "unittem", frameIndex, entity.owner, position,
                    kRenderScale.unitVisualScale * camera_.zoom);
            }
            const Rect spriteBounds = renderer_.spriteBounds(rules_.e2().image, frameIndex, position,
                kRenderScale.unitVisualScale * camera_.zoom);
            if (entity.selected || previewed) {
                // The selection marker is a world-ground circle. Projection
                // turns it into the correct isometric ellipse and keeps it
                // locked to the sprite's ground anchor at every zoom.
                drawGroundSelectionEllipse(entity.position, entity.selectionRadius,
                    entity.selected ? Color{1.0F, 0.84F, 0.20F, 1.0F} : Color{0.35F, 0.78F, 1.0F, 0.95F},
                    previewed);
            }
            const float healthRatio = std::clamp(static_cast<float>(entity.health) /
                static_cast<float>(entity.maxHealth), 0.0F, 1.0F);
            const float healthBarWidth = 44.0F * camera_.zoom;
            const Color healthColor = healthRatio >= 0.66F ? Color{0.20F, 0.90F, 0.22F, 1.0F} :
                healthRatio >= 0.33F ? Color{0.95F, 0.78F, 0.10F, 1.0F} :
                Color{0.92F, 0.12F, 0.08F, 1.0F};
            const float healthBarY = spriteBounds.height > 0.0F ? spriteBounds.y - 8.0F * camera_.zoom :
                position.y - 8.0F * camera_.zoom;
            const float healthBarCenterX = spriteBounds.width > 0.0F ?
                spriteBounds.x + spriteBounds.width * 0.5F : position.x;
            renderer_.drawRect({healthBarCenterX - healthBarWidth * 0.5F, healthBarY,
                healthBarWidth, 4.0F * camera_.zoom},
                {0.16F, 0.02F, 0.02F, 1.0F});
            renderer_.drawRect({healthBarCenterX - healthBarWidth * 0.5F, healthBarY,
                healthBarWidth * healthRatio, 4.0F * camera_.zoom}, healthColor);
        }

        const Rect strategicRail = ui_.rect("hud.strategic.rail");
        renderer_.drawImage("ui.hud.strategic.background", strategicRail);
        renderer_.drawImage("ui.hud.leftbar", ui_.rect("hud.strategic.leftbar"));
        renderer_.drawImage("ui.hud.leftbar", ui_.rect("hud.strategic.rightbar"));
        const Rect collapseButton = ui_.rect("hud.strategic.collapse");
        renderer_.drawImage("ui.hud.button", collapseButton);
        renderer_.drawText(strategicCollapsed_ ? T("expand") : T("collapse"), collapseButton, 12,
            {1.0F, 0.82F, 0.20F, 1.0F});
        if (!strategicCollapsed_) {
            renderer_.drawText(T("strategic"), ui_.rect("hud.strategic.title"), 14,
                {1.0F, 0.82F, 0.20F, 1.0F});
            for (int index = 0; index < 5; ++index) {
                const Rect ability = ui_.rect("hud.strategic.slot." + std::to_string(index));
                renderer_.drawImage("ui.hud.button", ability);
                renderer_.drawText(L"--", {ability.x + 2.0F, ability.y + 23.0F,
                    ability.width - 4.0F, 34.0F}, 22, {0.42F, 0.44F, 0.48F, 1.0F});
            }
        }

        const Rect side = ui_.rect("hud.production.sidebar");
        renderer_.drawImage("ui.hud.production.background", side);
        renderer_.drawImage("ui.hud.leftbar", ui_.rect("hud.production.leftbar"));
        renderer_.drawImage("ui.hud.rightbar", ui_.rect("hud.production.rightbar"));
        renderer_.drawText(L"10000", ui_.rect("hud.production.balance"), 28, {1.0F, 0.84F, 0.20F, 1.0F});
        renderer_.drawText(T("production"), ui_.rect("hud.production.title"), 20,
            {0.95F, 0.78F, 0.22F, 1.0F});
        const std::string tabs[] = {"building", "defense", "infantry", "vehicles"};
        for (int index = 0; index < 4; ++index) {
            const Rect tab = ui_.rect("hud.production.tab." + std::to_string(index));
            renderer_.drawImage(index == activeTab_ ? "ui.hud.tab_hover" : "ui.hud.tab", tab);
            renderer_.drawText(T(tabs[index]), tab, 16, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        renderer_.drawText(T("producer"), ui_.rect("hud.production.producer.title"), 16,
            {0.86F, 0.68F, 0.22F, 1.0F});
        for (int index = 0; index < 3; ++index) {
            const Rect producer = ui_.rect("hud.production.producer." + std::to_string(index));
            renderer_.drawImage("ui.hud.tab", producer, {0.55F, 0.55F, 0.58F, 1.0F});
            renderer_.drawText(L"--", producer, 18, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        for (int index = 0; index < 12; ++index) {
            const Rect product = ui_.rect("hud.production.product." + std::to_string(index));
            renderer_.drawImage("ui.hud.button", product);
            renderer_.drawText(L"--", {product.x, product.y + 12.0F, product.width, 28.0F}, 24,
                {0.42F, 0.44F, 0.48F, 1.0F});
        }

        const Rect hudBackground = ui_.rect("hud.background");
        renderer_.drawImage("ui.hud.background", hudBackground);
        const Rect miniMap = ui_.rect("hud.minimap");
        const Rect model = ui_.rect("hud.model");
        const Rect info = ui_.rect("hud.info");
        const Rect portrait = ui_.rect("hud.portrait");
        renderMiniMap(miniMap);
        drawHudPanel(model, "ui.hud.model.background", T("unit_model"));
        drawHudPanel(info, "ui.hud.unitinfo.background", T("unit_info"));
        drawHudPanel(portrait, "ui.hud.portrait.background", T("portrait"));
        const auto selected = selectedEntity();
        const auto preview = previewEntity();
        const Rect modelViewport = ui_.rect("hud.model.viewport");
        const Rect portraitViewport = ui_.rect("hud.portrait.viewport");
        renderer_.drawRect(modelViewport, {0.005F, 0.008F, 0.012F, 1.0F});
        renderer_.drawRect(portraitViewport, {0.005F, 0.008F, 0.012F, 1.0F});
        if (preview != nullptr && assetReady_) {
            const std::size_t frame = static_cast<std::size_t>(art_.frameIndex(rules_.e2().image,
                animationSequence(preview->animationState), preview->animationFrame, preview->facing));
            renderer_.drawSprite(rules_.e2().image, "unittem", frame, preview->owner, {436.0F, 1005.0F},
                kRenderScale.hudModelScale);
            renderer_.drawSprite(rules_.e2().image, "unittem", frame, preview->owner, {1275.0F, 1005.0F},
                kRenderScale.hudPortraitScale);
        } else {
            renderer_.drawText(L"--", modelViewport, 28, {0.42F, 0.44F, 0.48F, 1.0F});
            renderer_.drawText(L"--", portraitViewport, 28, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        if (selected == nullptr) {
            renderer_.drawText(L"未选择单位", ui_.childRect("hud.info", "hud.status.name"), 22,
                {1.0F, 0.82F, 0.20F, 1.0F}, false);
        } else {
            const gamedata::UnitDefinition* definition = rules_.findUnit(selected->definitionId);
            if (definition != nullptr) {
                const hud::UnitStatusViewModel status = hud::UnitStatusViewModelBuilder::build(
                    *selected, *definition, rules_, veterancy_, playerUpgrades_,
                    ui_.setting("HUD.UnitStatus", "HealthyThreshold", 0.60F),
                    ui_.setting("HUD.UnitStatus", "CriticalThreshold", 0.30F));
                const Color healthColor = status.healthBand == hud::HealthBand::Healthy ?
                    Color{0.30F, 1.0F, 0.34F, 1.0F} : status.healthBand == hud::HealthBand::Warning ?
                    Color{1.0F, 0.84F, 0.18F, 1.0F} : Color{1.0F, 0.20F, 0.12F, 1.0F};
                renderer_.drawText(utf8ToWide(status.displayName),
                    ui_.childRect("hud.info", "hud.status.name"), 24,
                    {1.0F, 0.82F, 0.20F, 1.0F}, false);
                renderer_.drawText(utf8ToWide(status.secondaryName),
                    ui_.childRect("hud.info", "hud.status.secondary"), 16,
                    {0.70F, 0.76F, 0.78F, 1.0F}, false);
                renderer_.drawText(T("health") + L": " + std::to_wstring(status.health) + L" / " +
                    std::to_wstring(status.maxHealth), ui_.childRect("hud.info", "hud.status.health"), 19,
                    healthColor, false);
                renderer_.drawText(L"击杀 " + std::to_wstring(status.kills) + L"   " +
                    utf8ToWide(status.veterancyName), ui_.childRect("hud.info", "hud.status.kills"), 15,
                    {0.92F, 0.86F, 0.36F, 1.0F}, false);
                std::wstring tags;
                for (std::size_t index = 0; index < status.tags.size(); ++index) {
                    if (index != 0) {
                        tags += L" / ";
                    }
                    tags += utf8ToWide(status.tags[index]);
                }
                renderer_.drawText(tags, ui_.childRect("hud.info", "hud.status.tags"), 14,
                    {0.72F, 0.84F, 0.86F, 1.0F}, false);
                const Rect armorRect = ui_.childRect("hud.info", "hud.status.armor");
                renderer_.drawImage("ui.unitstatus.armor", armorRect);
                renderer_.drawText(utf8ToWide(status.armor.uiName), armorRect, 12,
                    {1.0F, 0.84F, 0.24F, 1.0F});
                if (armorRect.contains(mouse_.x, mouse_.y)) {
                    renderer_.drawText(hud::UnitStatusViewModelBuilder::tooltip(status.armor),
                        {armorRect.x - 10.0F, armorRect.y - 30.0F, 310.0F, 24.0F}, 12,
                        {1.0F, 0.92F, 0.62F, 1.0F}, false);
                }
                for (std::size_t index = 0; index < status.weapons.size() && index < 3U; ++index) {
                    const Rect weaponRect = ui_.childRect("hud.info", "hud.status.weapon." +
                        std::to_string(index));
                    const auto weaponImage = unitStatusWeaponImageIds_.find(status.weapons[index].id);
                    if (weaponImage != unitStatusWeaponImageIds_.end()) {
                        renderer_.drawImage(weaponImage->second, weaponRect);
                    } else {
                        renderer_.drawImage("ui.hud.button", weaponRect,
                            {0.55F, 0.55F, 0.58F, 1.0F});
                    }
                    renderer_.drawText(utf8ToWide(status.weapons[index].uiName), weaponRect, 12,
                        {1.0F, 0.84F, 0.24F, 1.0F});
                    if (weaponRect.contains(mouse_.x, mouse_.y)) {
                        renderer_.drawText(hud::UnitStatusViewModelBuilder::tooltip(status.weapons[index]),
                            {weaponRect.x - 80.0F, weaponRect.y - 30.0F, 460.0F, 24.0F}, 12,
                            {1.0F, 0.92F, 0.62F, 1.0F}, false);
                    }
                }
            }
        }

        const Rect card = ui_.rect("hud.command_card");
        drawHudPanel(card, "ui.hud.commandcard.background", T("command_card"));
        if (pendingAction_ != PendingAction::None) {
            renderer_.drawText(L"TARGET: " + pendingActionLabel(), ui_.childRect("hud.command_card", "hud.command_card.target"), 12,
                {1.0F, 0.34F, 0.18F, 1.0F}, false);
        }
        const std::string commandKeys[] = {"move", "stop", "hold", "patrol", "attack_move", "", "", "", "", "", "", "", "", "", ""};
        for (int slot = 0; slot < 15; ++slot) {
            const Rect button = ui_.childRect("hud.command_card",
                "hud.command_card.slot." + std::to_string(slot));
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

        renderer_.drawText(T("editor_title"), ui_.rect("editor.title"), 21,
            {1.0F, 0.82F, 0.20F, 1.0F}, false);
        renderer_.drawText(L"MENU", ui_.rect("editor.menu"), 17,
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
    gamedata::TerrainDatabase terrainDatabase_;
    gamedata::VeterancyDatabase veterancy_;
    gamedata::UiLayoutDatabase ui_;
    westwood::Palette palette_;
    westwood::ShpTsDocument sprite_;
    std::unique_ptr<simulation::Simulation> simulation_;
    editor::TerrainMap terrainMap_{64, 64};
    std::unique_ptr<editor::EditorToolController> editorTools_;
    hud::PlayerUpgradeState playerUpgrades_;
    AudioService audio_;
    std::unordered_map<std::uint32_t, std::uint32_t> seenAttackEvents_;
    std::unordered_map<std::string, std::string> unitStatusWeaponImageIds_;
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
    bool editorStroke_ = false;
    bool leftMouseDown_ = false;
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
    ScreenCoord sandboxPalettePosition_{};
    ScreenCoord sandboxPaletteDragOffset_{};
    std::string lastMouseEvent_ = "NONE";
    IsometricCamera camera_{};
    std::size_t terrainTileCount_ = 0;
    std::vector<MenuButton> menuButtons_;
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
