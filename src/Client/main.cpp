#define SDL_MAIN_HANDLED

#include "GameData/Rules.h"
#include "Renderer/D3D11Renderer.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"

#include <SDL3/SDL.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
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
    Rect rect;
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
    bool initialize(std::string& error) {
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
        loadStrings();
        loadRuntimeAssets();
        simulation_ = std::make_unique<simulation::Simulation>(rules_.e2());
        simulation_->spawn(Owner::Red, {22, 28});
        simulation_->spawn(Owner::Red, {25, 31});
        simulation_->spawn(Owner::Blue, {39, 29});
        simulation_->spawn(Owner::Blue, {42, 32});
        return true;
    }

    int run() {
        auto previous = std::chrono::steady_clock::now();
        while (running_) {
            const auto now = std::chrono::steady_clock::now();
            const float seconds = std::min(0.1F, std::chrono::duration<float>(now - previous).count());
            previous = now;
            processEvents();
            if (mode_ == AppMode::EditorSandbox) {
                simulation_->update(seconds);
            }
            render();
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
    void loadStrings() {
        strings_ = {
            {"title", L"RA2YR-BYCPP"}, {"subtitle", L"FIRST PLAYABLE EDITOR SLICE"},
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
            {"runtime", L"SIMULATION ONLINE"}, {"asset_missing", L"REAL CONS.SHP / UNIT PALETTE NOT FOUND"},
        };
        const std::filesystem::path executablePath = std::filesystem::absolute(".");
        const std::filesystem::path candidates[] = {
            executablePath / "assets/ui/strings.ini",
            std::filesystem::current_path() / "assets/ui/strings.ini",
        };
        for (const auto& path : candidates) {
            std::string error;
            westwood::IniDocument document;
            if (!document.load(path, error)) {
                continue;
            }
            for (const auto& [key, fallback] : strings_) {
                strings_[key] = utf8ToWide(document.get("ui", key, ""));
                if (strings_[key].empty()) {
                    strings_[key] = fallback;
                }
            }
            return;
        }
    }

    void loadRuntimeAssets() {
        const char* corpusEnvironment = std::getenv("RA2YR_CORPUS_ROOT");
        if (corpusEnvironment == nullptr || *corpusEnvironment == '\0') {
            assetError_ = T("asset_missing");
            std::cerr << "[Content][Error] RA2YR_CORPUS_ROOT is not set; real CONS.SHP is unavailable.\n";
            return;
        }
        const std::filesystem::path root(corpusEnvironment);
        const std::filesystem::path rulesPath = root / "extracted/ini/yr-1.001-patch/rulesmd.ini";
        const std::filesystem::path alternateRulesPath = root / "rulesmd.ini";
        std::string error;
        if (!rules_.load(std::filesystem::exists(rulesPath) ? rulesPath : alternateRulesPath, error)) {
            assetError_ = utf8ToWide(error);
            std::cerr << "[Content][Error] " << error << '\n';
            return;
        }
        const std::filesystem::path spritePath = root / "extracted/leaf/ra2.mix/conquer.mix/cons.shp";
        const std::filesystem::path palettePath = root / "extracted/leaf/ra2.mix/cache.mix/unittem.pal";
        if (!palette_.load(palettePath, error) || !sprite_.load(spritePath, error)) {
            assetError_ = utf8ToWide(error);
            std::cerr << "[Content][Error] " << error << '\n';
            return;
        }
        assetReady_ = true;
        std::cerr << "[Content] Loaded effective rulesmd.ini, CONS.SHP and unittem.pal\n";
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
        if (mode_ == AppMode::MainMenu) {
            if (key == SDLK_ESCAPE) {
                running_ = false;
            } else if (key == SDLK_e) {
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
        } else if (key == SDLK_m) {
            pendingAction_ = PendingAction::Move;
        } else if (key == SDLK_s) {
            simulation_->issueStop();
        } else if (key == SDLK_h) {
            simulation_->issueHold();
        } else if (key == SDLK_p) {
            pendingAction_ = PendingAction::Patrol;
        } else if (key == SDLK_a) {
            pendingAction_ = PendingAction::AttackMove;
        } else if (key == SDLK_r) {
            placingOwner_ = Owner::Red;
            placing_ = true;
        } else if (key == SDLK_b) {
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
            simulation_->selectBox(screenToGrid(dragStart_), screenToGrid(dragEnd_));
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
            const float cellWidth = 72.0F;
            const float cellHeight = 54.0F;
            const int column = static_cast<int>((mouse_.x - card.x) / 76.0F);
            const int row = static_cast<int>((mouse_.y - card.y) / 58.0F);
            if (column >= 0 && column < 5 && row >= 0 && row < 3) {
                const int slot = row * 5 + column;
                if (slot == 0) {
                    pendingAction_ = PendingAction::Move;
                } else if (slot == 1) {
                    simulation_->issueStop();
                } else if (slot == 2 || slot == 6) {
                    simulation_->issueHold();
                } else if (slot == 3) {
                    pendingAction_ = PendingAction::Attack;
                } else if (slot == 4 || slot == 9) {
                    pendingAction_ = PendingAction::AttackMove;
                } else if (slot == 5) {
                    pendingAction_ = PendingAction::Patrol;
                } else if (slot == 8) {
                    pendingAction_ = PendingAction::Move;
                }
                return true;
            }
            (void)cellWidth;
            (void)cellHeight;
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
        renderer_.present();
    }

    void renderMainMenu() {
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.0F, 0.0F, 0.0F, 1.0F});
        const Rect monitor{420.0F, 118.0F, 910.0F, 720.0F};
        renderer_.drawRect(monitor, {0.18F, 0.20F, 0.22F, 1.0F});
        renderer_.drawBorder(monitor, {0.70F, 0.73F, 0.76F, 1.0F}, 5.0F);
        const Rect screen{470.0F, 160.0F, 810.0F, 630.0F};
        renderer_.drawRect(screen, {0.015F, 0.0F, 0.0F, 1.0F});
        renderer_.drawBorder(screen, {0.28F, 0.02F, 0.02F, 1.0F}, 4.0F);
        const ScreenCoord center{875.0F, 474.0F};
        for (int radius = 90; radius <= 300; radius += 70) {
            for (int segment = 0; segment < 36; ++segment) {
                const float first = static_cast<float>(segment) * 6.2831853F / 36.0F;
                const float second = static_cast<float>(segment + 1) * 6.2831853F / 36.0F;
                renderer_.drawLine({center.x + std::cos(first) * radius, center.y + std::sin(first) * radius * 0.78F},
                    {center.x + std::cos(second) * radius, center.y + std::sin(second) * radius * 0.78F},
                    {0.42F, 0.01F, 0.01F, 0.8F}, 2.0F);
            }
        }
        renderer_.drawLine({center.x - 360.0F, center.y}, {center.x + 360.0F, center.y}, {0.5F, 0.01F, 0.01F, 0.8F}, 2.0F);
        renderer_.drawLine({center.x, center.y - 280.0F}, {center.x, center.y + 280.0F}, {0.5F, 0.01F, 0.01F, 0.8F}, 2.0F);
        renderer_.drawText(T("title"), {520.0F, 392.0F, 710.0F, 65.0F}, 50, {0.95F, 0.72F, 0.22F, 1.0F});
        renderer_.drawText(L"COMMAND & CONQUER", {520.0F, 348.0F, 710.0F, 38.0F}, 23, {0.9F, 0.9F, 0.84F, 1.0F});
        renderer_.drawText(T("subtitle"), {510.0F, 705.0F, 730.0F, 34.0F}, 18, {0.8F, 0.18F, 0.12F, 1.0F});

        const Rect panel{1445.0F, 88.0F, 425.0F, 865.0F};
        renderer_.drawRect(panel, {0.035F, 0.04F, 0.045F, 0.98F});
        renderer_.drawBorder(panel, {0.42F, 0.45F, 0.48F, 1.0F}, 5.0F);
        renderer_.drawBorder({1460.0F, 103.0F, 395.0F, 835.0F}, {0.60F, 0.30F, 0.08F, 1.0F}, 2.0F);
        for (std::size_t i = 0; i < menuButtons_.size(); ++i) {
            const MenuButton& button = menuButtons_[i];
            const bool hovered = button.rect.contains(mouse_.x, mouse_.y);
            const bool pressed = pressedMenuButton_ == static_cast<int>(i);
            renderer_.drawRect(button.rect, pressed ? Color{0.38F, 0.03F, 0.02F, 1.0F} : hovered ?
                Color{0.20F, 0.08F, 0.03F, 1.0F} : Color{0.045F, 0.045F, 0.05F, 1.0F});
            renderer_.drawBorder(button.rect, hovered ? Color{1.0F, 0.45F, 0.08F, 1.0F} : Color{0.58F, 0.36F, 0.16F, 1.0F}, 2.0F);
            renderer_.drawText(T(button.key), {button.rect.x + 5.0F, button.rect.y + 2.0F, button.rect.width - 10.0F, button.rect.height - 4.0F},
                25, {1.0F, 0.84F, 0.18F, 1.0F});
        }
        renderer_.drawText(L"DTA-INSPIRED CLIENT SHELL", {40.0F, 1002.0F, 520.0F, 28.0F}, 16, {0.55F, 0.55F, 0.58F, 1.0F}, false);
        renderer_.drawText(L"C++23 / SDL3 / DIRECT3D 11", {40.0F, 1032.0F, 520.0F, 28.0F}, 16, {0.55F, 0.55F, 0.58F, 1.0F}, false);
    }

    void renderEditor() {
        renderer_.drawRect({0.0F, 0.0F, kLogicalWidth, kLogicalHeight}, {0.008F, 0.012F, 0.018F, 1.0F});
        renderer_.drawRect({100.0F, 50.0F, 1390.0F, 780.0F}, {0.04F, 0.07F, 0.05F, 1.0F});
        for (int x = 0; x < 64; ++x) {
            for (int y = 0; y < 64; ++y) {
                const ScreenCoord center = gridToScreen({static_cast<float>(x), static_cast<float>(y)});
                if (center.x < 80.0F || center.x > 1510.0F || center.y < 30.0F || center.y > 870.0F) {
                    continue;
                }
                const bool alternate = ((x + y) & 1) != 0;
                renderer_.drawDiamond(center, kTileWidth, kTileHeight,
                    alternate ? Color{0.10F, 0.25F, 0.14F, 1.0F} : Color{0.08F, 0.21F, 0.12F, 1.0F},
                    {0.18F, 0.38F, 0.20F, 0.70F});
            }
        }
        renderer_.drawBorder({100.0F, 50.0F, 1390.0F, 780.0F}, {0.65F, 0.48F, 0.18F, 1.0F}, 3.0F);

        for (const auto& entity : simulation_->entities()) {
            if (entity.health <= 0) {
                continue;
            }
            const ScreenCoord position = gridToScreen(entity.position);
            if (assetReady_) {
                renderer_.drawSprite(sprite_.frame(static_cast<std::size_t>(simulation_->animationFrame())), palette_, entity.owner, position, 0.72F);
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
        renderer_.drawBorder({8.0F, 114.0F, 78.0F, 650.0F}, {0.64F, 0.42F, 0.16F, 1.0F}, 2.0F);
        renderer_.drawText(T("strategic"), {12.0F, 128.0F, 70.0F, 40.0F}, 15, {1.0F, 0.82F, 0.20F, 1.0F});
        for (int i = 0; i < 5; ++i) {
            const Rect ability{18.0F, 184.0F + i * 106.0F, 58.0F, 86.0F};
            renderer_.drawRect(ability, {0.04F, 0.05F, 0.06F, 1.0F});
            renderer_.drawBorder(ability, {0.42F, 0.20F, 0.08F, 1.0F}, 2.0F);
            renderer_.drawText(L"◆", {ability.x, ability.y + 12.0F, ability.width, 34.0F}, 24, {0.9F, 0.12F, 0.08F, 1.0F});
            renderer_.drawText(std::to_wstring(i + 1), {ability.x, ability.y + 48.0F, ability.width, 22.0F}, 14, {0.8F, 0.78F, 0.45F, 1.0F});
        }

        const Rect side{1500.0F, 40.0F, 420.0F, 785.0F};
        renderer_.drawRect(side, {0.025F, 0.03F, 0.04F, 0.98F});
        renderer_.drawBorder(side, {0.75F, 0.55F, 0.18F, 1.0F}, 4.0F);
        renderer_.drawText(L"10000", {1630.0F, 45.0F, 155.0F, 36.0F}, 28, {1.0F, 0.84F, 0.20F, 1.0F});
        renderer_.drawText(T("production"), {1520.0F, 120.0F, 370.0F, 30.0F}, 20, {0.95F, 0.78F, 0.22F, 1.0F});
        const std::string tabs[] = {"building", "defense", "infantry", "vehicles"};
        for (int i = 0; i < 4; ++i) {
            const Rect tab{1520.0F + i * 96.0F, 158.0F, 88.0F, 42.0F};
            renderer_.drawRect(tab, i == activeTab_ ? Color{0.30F, 0.11F, 0.04F, 1.0F} : Color{0.05F, 0.06F, 0.07F, 1.0F});
            renderer_.drawBorder(tab, {0.72F, 0.44F, 0.12F, 1.0F}, 2.0F);
            renderer_.drawText(T(tabs[i]), tab, 17, {1.0F, 0.82F, 0.20F, 1.0F});
        }
        for (int i = 0; i < 12; ++i) {
            const int column = i % 3;
            const int row = i / 3;
            const Rect product{1528.0F + column * 122.0F, 220.0F + row * 95.0F, 108.0F, 80.0F};
            renderer_.drawRect(product, {0.035F, 0.04F, 0.05F, 1.0F});
            renderer_.drawBorder(product, {0.22F, 0.24F, 0.27F, 1.0F}, 2.0F);
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
            renderer_.drawText(T("weapon") + L": M1Carbine   " + T("armor") + L": " + utf8ToWide(rules_.e2().armor), {140.0F, 974.0F, 720.0F, 28.0F}, 18, {0.72F, 0.75F, 0.82F, 1.0F}, false);
            if (assetReady_) {
                renderer_.drawSprite(sprite_.frame(static_cast<std::size_t>(simulation_->animationFrame())), palette_, selected->owner, {1145.0F, 965.0F}, 0.95F);
            }
        }
        renderer_.drawText(T("runtime"), {860.0F, 1026.0F, 440.0F, 24.0F}, 15, {0.3F, 0.9F, 0.4F, 1.0F});
        if (!assetReady_) {
            renderer_.drawText(assetError_.empty() ? T("asset_missing") : assetError_, {150.0F, 1005.0F, 760.0F, 26.0F}, 14, {1.0F, 0.25F, 0.16F, 1.0F}, false);
        }
        const Rect card{1518.0F, 850.0F, 382.0F, 188.0F};
        renderer_.drawRect(card, {0.018F, 0.022F, 0.028F, 1.0F});
        renderer_.drawBorder(card, {0.75F, 0.50F, 0.16F, 1.0F}, 3.0F);
        const std::string commandKeys[] = {"move", "stop", "guard", "attack", "attack_move", "patrol", "hold", "repair", "waypoint", "deploy", "settings", "building", "defense", "infantry", "vehicles"};
        for (int slot = 0; slot < 15; ++slot) {
            const int column = slot % 5;
            const int row = slot / 5;
            const Rect button{1523.0F + column * 76.0F, 855.0F + row * 58.0F, 72.0F, 54.0F};
            const bool hot = button.contains(mouse_.x, mouse_.y);
            renderer_.drawRect(button, hot ? Color{0.28F, 0.10F, 0.03F, 1.0F} : Color{0.055F, 0.06F, 0.07F, 1.0F});
            renderer_.drawBorder(button, hot ? Color{1.0F, 0.48F, 0.12F, 1.0F} : Color{0.48F, 0.35F, 0.16F, 1.0F}, 2.0F);
            renderer_.drawText(T(commandKeys[slot]), button, 14, {1.0F, 0.84F, 0.26F, 1.0F});
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
    westwood::Palette palette_;
    westwood::ShpTsDocument sprite_;
    std::unique_ptr<simulation::Simulation> simulation_;
    std::unordered_map<std::string, std::wstring> strings_;
    std::wstring assetError_;
    std::wstring toast_;
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
    std::vector<MenuButton> menuButtons_ = {
        {"campaign", {1490.0F, 175.0F, 335.0F, 62.0F}},
        {"load", {1490.0F, 250.0F, 335.0F, 62.0F}},
        {"skirmish", {1490.0F, 325.0F, 335.0F, 62.0F}},
        {"online", {1490.0F, 400.0F, 335.0F, 62.0F}},
        {"lan", {1490.0F, 475.0F, 335.0F, 62.0F}},
        {"settings", {1490.0F, 550.0F, 335.0F, 62.0F}},
        {"statistics", {1490.0F, 625.0F, 335.0F, 62.0F}},
        {"editor", {1490.0F, 700.0F, 335.0F, 62.0F}},
        {"exit", {1490.0F, 775.0F, 335.0F, 62.0F}},
    };
};

} // namespace
} // namespace ra2yr::client

int main() {
    ra2yr::client::ClientApp app;
    std::string error;
    if (!app.initialize(error)) {
        std::cerr << "[Fatal] " << error << '\n';
        return 1;
    }
    return app.run();
}
