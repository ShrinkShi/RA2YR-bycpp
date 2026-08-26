#include "GameData/Rules.h"
#include "GameData/Art.h"
#include "Renderer/D3D11Renderer.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"

#include <SDL3/SDL.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
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
constexpr float kTileWidth = 44.0F;
constexpr float kTileHeight = 22.0F;

enum class AppMode {
    MainMenu,
    EditorSandbox,
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
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
            error = SDL_GetError();
            return false;
        }
        window_ = SDL_CreateWindow("RA2YR-bycpp - Editor Sandbox", 1280, 720,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr) {
            error = SDL_GetError();
            return false;
        }
        if (!renderer_.initialize(window_, error)) {
            return false;
        }
        contentRoot_ = executableDirectory();
        loadStrings();
        if (!loadRuntimeAssets(error)) {
            return false;
        }
        if (!buildTerrain(error)) {
            return false;
        }
        simulation_ = std::make_unique<simulation::Simulation>(rules_.e2());
        // Keep the sample forces inside the initial camera framing so the
        // first editor run visibly demonstrates both owner colors.
        simulation_->spawn(Owner::Red, {8, 12});
        simulation_->spawn(Owner::Red, {12, 10});
        simulation_->spawn(Owner::Blue, {20, 8});
        simulation_->spawn(Owner::Blue, {16, 12});
        if (stressEntities) {
            for (int index = 0; index < 100; ++index) {
                const Owner owner = index < 50 ? Owner::Red : Owner::Blue;
                const int localIndex = index % 50;
                simulation_->spawn(owner, {16 + (localIndex % 10) * 2, 18 + (localIndex / 10) * 2});
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
            {"editor_hint", L"LMB SELECT / DRAG SELECT   RMB MOVE OR ATTACK   M MOVE  S STOP  H HOLD  P PATROL  A ATTACK MOVE"},
            {"red", L"RED"}, {"blue", L"BLUE"}, {"place_red", L"PLACE RED E2"}, {"place_blue", L"PLACE BLUE E2"},
            {"cancel_place", L"CANCEL PLACEMENT"}, {"strategic", L"STRATEGIC"}, {"production", L"PRODUCTION"},
            {"building", L"BUILDING"}, {"defense", L"DEFENSE"}, {"infantry", L"INFANTRY"}, {"vehicles", L"VEHICLES"},
            {"unit_name", L"CONSCRIPT E2 / CONS.SHP"}, {"owner", L"OWNER"}, {"health", L"HEALTH"},
            {"weapon", L"WEAPON"}, {"armor", L"ARMOR"}, {"move", L"MOVE"}, {"stop", L"STOP"},
            {"guard", L"GUARD"}, {"attack", L"ATTACK"}, {"deploy", L"DEPLOY"}, {"hold", L"HOLD"},
            {"patrol", L"PATROL"}, {"repair", L"REPAIR"}, {"waypoint", L"WAYPOINT"}, {"attack_move", L"ATTACK MOVE"},
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
            {"ui.hud.ability", contentRoot_ / "assets/ui/dta/hud/slocindicator.png"},
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
            placing_ = true;
        } else if (key == SDLK_B) {
            placingOwner_ = Owner::Blue;
            placing_ = true;
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
            simulation_->spawn(placingOwner_, screenToGrid(mouse_));
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
        if (mouse_.y >= 158.0F && mouse_.y <= 200.0F && mouse_.x >= 1520.0F && mouse_.x < 1900.0F) {
            activeTab_ = std::clamp(static_cast<int>((mouse_.x - 1520.0F) / 96.0F), 0, 3);
            return true;
        }
        if (Rect{1510.0F, 70.0F, 190.0F, 46.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Red;
            placing_ = true;
            return true;
        }
        if (Rect{1710.0F, 70.0F, 190.0F, 46.0F}.contains(mouse_.x, mouse_.y)) {
            placingOwner_ = Owner::Blue;
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
            const int row = static_cast<int>((mouse_.y - card.y) / 58.0F);
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
                } else if (slot == 4 || slot == 9) {
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
        } else if (action == PendingAction::Patrol) {
            simulation_->issuePatrol(destination);
        } else if (action == PendingAction::AttackMove) {
            simulation_->issueAttackMove(destination);
        } else if (action == PendingAction::Attack) {
            const std::uint32_t target = unitAt(position);
            if (target != 0) {
                simulation_->issueAttack(target);
            }
        } else {
            const std::uint32_t target = unitAt(position);
            if (target != 0) {
                simulation_->issueAttack(target);
            } else {
                simulation_->issueMove(destination);
            }
        }
        pendingAction_ = PendingAction::None;
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
        renderer_.drawRect({24.0F, 62.0F, 460.0F, 142.0F}, {0.01F, 0.02F, 0.025F, 0.92F});
        renderer_.drawBorder({24.0F, 62.0F, 460.0F, 142.0F}, {0.35F, 0.80F, 0.55F, 0.95F}, 2.0F);
        renderer_.drawText(L"PERFORMANCE [F3] / VSYNC [F4]", {42.0F, 72.0F, 420.0F, 24.0F}, 17,
            {0.45F, 1.0F, 0.60F, 1.0F}, false);
        renderer_.drawText(L"FPS " + formatNumber(performance_.averageFps) + L"   FRAME " +
            formatNumber(performance_.frameTimeMs) + L" ms   P95 " + formatNumber(performance_.p95FrameTimeMs) + L" ms",
            {42.0F, 102.0F, 420.0F, 24.0F}, 15, {0.85F, 0.90F, 0.88F, 1.0F}, false);
        renderer_.drawText(L"SIM " + formatNumber(performance_.simulationTimeMs) + L" ms   RENDER " +
            formatNumber(performance_.renderCpuTimeMs) + L" ms   DRAWS " + std::to_wstring(renderStats.drawCalls),
            {42.0F, 128.0F, 420.0F, 24.0F}, 15, {0.85F, 0.90F, 0.88F, 1.0F}, false);
        renderer_.drawText(L"TILES " + std::to_wstring(renderStats.visibleTiles) + L"   ENTITIES " +
            std::to_wstring(renderStats.visibleEntities) + L"   LAYOUTS " +
            std::to_wstring(renderStats.textLayoutUpdates) + L"   UPLOADS " +
            std::to_wstring(renderStats.textureUploadCount), {42.0F, 154.0F, 420.0F, 24.0F}, 15,
            {0.85F, 0.90F, 0.88F, 1.0F}, false);
    }

    void renderMainMenu() {
        renderer_.drawImage("ui.main.background", {0.0F, 0.0F, kLogicalWidth, kLogicalHeight});
        renderer_.drawText(T("title"), {275.0F, 430.0F, 1040.0F, 72.0F}, 48, {1.0F, 0.84F, 0.34F, 1.0F});
        renderer_.drawText(T("subtitle"), {300.0F, 505.0F, 990.0F, 92.0F}, 68, {1.0F, 0.28F, 0.10F, 1.0F});
        renderer_.drawText(L"RA2YR-BYCPP  //  MAIN CONTROL", {420.0F, 625.0F, 750.0F, 34.0F}, 20, {0.86F, 0.62F, 0.28F, 1.0F});
        for (std::size_t i = 0; i < menuButtons_.size(); ++i) {
            const MenuButton& button = menuButtons_[i];
            const bool hovered = button.rect.contains(mouse_.x, mouse_.y);
            const bool pressed = pressedMenuButton_ == static_cast<int>(i);
            renderer_.drawImage(pressed || hovered ? button.hoverImage : button.image, button.rect,
                pressed ? Color{0.80F, 0.80F, 0.80F, 1.0F} : Color{1.0F, 1.0F, 1.0F, 1.0F});
            renderer_.drawText(T(button.key), button.rect, 24, {1.0F, 0.86F, 0.20F, 1.0F});
        }
        renderer_.drawText(L"RED COMMAND CONSOLE  //  C++23 / SDL3 / D3D11", {94.0F, 1008.0F, 980.0F, 28.0F}, 16, {0.72F, 0.34F, 0.18F, 1.0F}, false);
    }

    void renderEditor() {
        renderer_.setWorldStats(terrainTileCount_, simulation_->entities().size());
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.008F, 0.012F, 0.018F, 1.0F});
        renderer_.drawRect({100.0F, 50.0F, 1390.0F, 780.0F}, {0.04F, 0.07F, 0.05F, 1.0F});
        renderer_.drawStaticTerrain();
        renderer_.drawBorder({100.0F, 50.0F, 1390.0F, 780.0F}, {0.65F, 0.48F, 0.18F, 1.0F}, 3.0F);

        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord position = gridToScreen(entity.position);
            if (assetReady_) {
                renderer_.drawSprite(rules_.e2().image, "unittem", static_cast<std::size_t>(art_.frameIndex(
                    rules_.e2().image, "Walk", simulation_->animationFrame())), entity.owner, position, 0.72F);
            }
            if (entity.selected) {
                renderer_.drawBorder({position.x - 22.0F, position.y - 8.0F, 44.0F, 16.0F}, {0.95F, 0.85F, 0.25F, 1.0F}, 2.0F);
            }
            const float healthRatio = std::clamp(static_cast<float>(entity.health) / static_cast<float>(entity.maxHealth), 0.0F, 1.0F);
            renderer_.drawRect({position.x - 22.0F, position.y - 30.0F, 44.0F, 4.0F}, {0.16F, 0.02F, 0.02F, 1.0F});
            renderer_.drawRect({position.x - 22.0F, position.y - 30.0F, 44.0F * healthRatio, 4.0F},
                entity.owner == Owner::Red ? Color{0.95F, 0.10F, 0.06F, 1.0F} : Color{0.18F, 0.48F, 1.0F, 1.0F});
        }

        renderer_.drawRect({0.0F, 105.0F, 94.0F, 670.0F}, {0.025F, 0.035F, 0.045F, 0.98F});
        renderer_.drawImage("ui.hud.leftbar", {0.0F, 105.0F, 24.0F, 670.0F});
        renderer_.drawImage("ui.hud.leftbar", {70.0F, 105.0F, 24.0F, 670.0F});
        renderer_.drawBorder({8.0F, 114.0F, 78.0F, 650.0F}, {0.64F, 0.42F, 0.16F, 1.0F}, 2.0F);
        renderer_.drawText(T("strategic"), {12.0F, 128.0F, 70.0F, 40.0F}, 15, {1.0F, 0.82F, 0.20F, 1.0F});
        for (int i = 0; i < 5; ++i) {
            const Rect ability{18.0F, 184.0F + i * 106.0F, 58.0F, 86.0F};
            renderer_.drawImage("ui.hud.button", ability);
            renderer_.drawImage("ui.hud.ability", {ability.x + 4.0F, ability.y + 4.0F, 50.0F, 50.0F});
            renderer_.drawText(std::to_wstring(i + 1), {ability.x, ability.y + 48.0F, ability.width, 22.0F}, 14, {0.8F, 0.78F, 0.45F, 1.0F});
        }

        const Rect side{1500.0F, 40.0F, 420.0F, 785.0F};
        renderer_.drawRect(side, {0.025F, 0.03F, 0.04F, 0.98F});
        renderer_.drawImage("ui.hud.leftbar", {1500.0F, 40.0F, 24.0F, 785.0F});
        renderer_.drawImage("ui.hud.rightbar", {1896.0F, 40.0F, 24.0F, 785.0F});
        renderer_.drawBorder(side, {0.75F, 0.55F, 0.18F, 1.0F}, 4.0F);
        renderer_.drawText(L"10000", {1630.0F, 45.0F, 155.0F, 36.0F}, 28, {1.0F, 0.84F, 0.20F, 1.0F});
        renderer_.drawText(T("production"), {1520.0F, 120.0F, 370.0F, 30.0F}, 20, {0.95F, 0.78F, 0.22F, 1.0F});
        const std::string tabs[] = {"building", "defense", "infantry", "vehicles"};
        for (int i = 0; i < 4; ++i) {
            const Rect tab{1520.0F + i * 96.0F, 158.0F, 88.0F, 42.0F};
            renderer_.drawImage(i == activeTab_ ? "ui.hud.tab_hover" : "ui.hud.tab", tab);
            renderer_.drawText(T(tabs[i]), tab, 17, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        for (int i = 0; i < 12; ++i) {
            const int column = i % 3;
            const int row = i / 3;
            const Rect product{1528.0F + column * 122.0F, 220.0F + row * 95.0F, 108.0F, 80.0F};
            renderer_.drawImage("ui.hud.button", product);
            renderer_.drawText(L"--", {product.x, product.y + 12.0F, product.width, 28.0F}, 24, {0.42F, 0.44F, 0.48F, 1.0F});
        }
        renderer_.drawText(T("red"), {1510.0F, 70.0F, 190.0F, 46.0F}, 19, {1.0F, 0.30F, 0.18F, 1.0F});
        renderer_.drawText(T("blue"), {1710.0F, 70.0F, 190.0F, 46.0F}, 19, {0.32F, 0.58F, 1.0F, 1.0F});

        const Rect hud{100.0F, 842.0F, 1390.0F, 220.0F};
        renderer_.drawRect(hud, {0.025F, 0.028F, 0.032F, 0.99F});
        renderer_.drawBorder(hud, {0.65F, 0.46F, 0.16F, 1.0F}, 4.0F);
        const auto selected = selectedEntity();
        renderer_.drawText(selected == nullptr ? L"NO UNIT SELECTED" : T("unit_name"), {140.0F, 860.0F, 680.0F, 34.0F}, 25, {1.0F, 0.82F, 0.20F, 1.0F}, false);
        renderer_.drawText(selected == nullptr ? L"OWNER --" : T("owner") + L": " + ownerText(selected->owner), {140.0F, 906.0F, 520.0F, 28.0F}, 19, {0.8F, 0.8F, 0.74F, 1.0F}, false);
        if (selected != nullptr) {
            renderer_.drawText(T("health") + L": " + std::to_wstring(selected->health) + L" / " + std::to_wstring(selected->maxHealth), {140.0F, 940.0F, 520.0F, 28.0F}, 19, {0.36F, 1.0F, 0.36F, 1.0F}, false);
            renderer_.drawText(T("weapon") + L": M1Carbine   " + T("armor") + L": " + utf8ToWide(rules_.e2().armorType), {140.0F, 974.0F, 720.0F, 28.0F}, 18, {0.72F, 0.75F, 0.82F, 1.0F}, false);
            if (assetReady_) {
                renderer_.drawSprite(rules_.e2().image, "unittem", static_cast<std::size_t>(art_.frameIndex(
                    rules_.e2().image, "Ready", 0)), selected->owner, {1145.0F, 965.0F}, 0.95F);
            }
        }
        renderer_.drawText(T("runtime"), {860.0F, 1026.0F, 440.0F, 24.0F}, 15, {0.3F, 0.9F, 0.4F, 1.0F});
        if (!assetReady_) {
            renderer_.drawText(assetError_.empty() ? T("asset_missing") : assetError_, {150.0F, 1005.0F, 760.0F, 26.0F}, 14, {1.0F, 0.25F, 0.16F, 1.0F}, false);
        }
        const Rect card{1518.0F, 850.0F, 382.0F, 188.0F};
        renderer_.drawRect(card, {0.018F, 0.022F, 0.028F, 1.0F});
        renderer_.drawBorder(card, {0.75F, 0.50F, 0.16F, 1.0F}, 3.0F);
        const std::string commandKeys[] = {"move", "stop", "hold", "patrol", "attack_move", "", "", "", "", "", "", "", "", "", ""};
        for (int slot = 0; slot < 15; ++slot) {
            const int column = slot % 5;
            const int row = slot / 5;
            const Rect button{1523.0F + column * 76.0F, 855.0F + row * 58.0F, 72.0F, 54.0F};
            const bool hot = button.contains(mouse_.x, mouse_.y);
            renderer_.drawImage(hot ? "ui.hud.button_hover" : "ui.hud.button", button);
            if (!commandKeys[slot].empty()) {
                renderer_.drawText(T(commandKeys[slot]), button, 14, {1.0F, 0.84F, 0.26F, 1.0F});
            }
        }
        renderer_.drawText(T("editor_title"), {116.0F, 10.0F, 320.0F, 30.0F}, 21, {1.0F, 0.82F, 0.20F, 1.0F}, false);
        renderer_.drawText(T("editor_hint"), {410.0F, 10.0F, 1260.0F, 30.0F}, 15, {0.65F, 0.68F, 0.70F, 1.0F}, false);
        renderer_.drawText(L"MENU", {1820.0F, 8.0F, 90.0F, 40.0F}, 17, {1.0F, 0.84F, 0.24F, 1.0F});
        if (placing_) {
            renderer_.drawBorder({100.0F, 50.0F, 1390.0F, 780.0F}, placingOwner_ == Owner::Red ? Color{1.0F, 0.12F, 0.08F, 1.0F} : Color{0.18F, 0.45F, 1.0F, 1.0F}, 5.0F);
            renderer_.drawText(placingOwner_ == Owner::Red ? T("place_red") : T("place_blue"), {580.0F, 72.0F, 420.0F, 34.0F}, 20, {1.0F, 0.82F, 0.20F, 1.0F});
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
    std::unordered_map<std::string, std::wstring> strings_;
    std::wstring assetError_;
    std::wstring toast_;
    std::filesystem::path contentRoot_;
    PerformanceTracker performance_;
    AppMode mode_ = AppMode::MainMenu;
    PendingAction pendingAction_ = PendingAction::None;
    Owner placingOwner_ = Owner::Red;
    bool running_ = true;
    bool assetReady_ = false;
    bool placing_ = false;
    bool dragging_ = false;
    int activeTab_ = 0;
    int pressedMenuButton_ = -1;
    float toastTime_ = 0.0F;
    ScreenCoord mouse_{};
    ScreenCoord dragStart_{};
    ScreenCoord dragEnd_{};
    IsoProjection projection_{kTileWidth, kTileHeight, {790.0F, 440.0F}};
    std::size_t terrainTileCount_ = 0;
    std::vector<MenuButton> menuButtons_ = {
        {"campaign", "ui.main.button", "ui.main.button_hover", {1500.0F, 166.0F, 335.0F, 76.0F}},
        {"load", "ui.main.button", "ui.main.button_hover", {1500.0F, 250.0F, 335.0F, 76.0F}},
        {"skirmish", "ui.main.button", "ui.main.button_hover", {1500.0F, 334.0F, 335.0F, 76.0F}},
        {"online", "ui.main.button", "ui.main.button_hover", {1500.0F, 418.0F, 335.0F, 76.0F}},
        {"lan", "ui.main.button", "ui.main.button_hover", {1500.0F, 502.0F, 335.0F, 76.0F}},
        {"settings", "ui.main.button", "ui.main.button_hover", {1500.0F, 586.0F, 335.0F, 76.0F}},
        {"statistics", "ui.main.button", "ui.main.button_hover", {1500.0F, 670.0F, 335.0F, 76.0F}},
        {"editor", "ui.main.button", "ui.main.button_hover", {1500.0F, 754.0F, 335.0F, 76.0F}},
        {"exit", "ui.main.button", "ui.main.button_hover", {1500.0F, 838.0F, 335.0F, 76.0F}},
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
