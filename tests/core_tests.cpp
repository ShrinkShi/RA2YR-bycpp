#include "GameData/Art.h"
#include "Engine/Core/Utf.h"
#include "GameData/Rules.h"
#include "GameData/Localization.h"
#include "GameData/Terrain.h"
#include "GameData/UI.h"
#include "GameData/Veterancy.h"
#include "Editor/EditorToolController.h"
#include "Client/Hud/UnitStatusViewModel.h"
#include "Westwood/Palette/Palette.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"
#include "Westwood/Shp/Shp.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

int main() {
    using namespace ra2yr;

    westwood::IniDocument ini;
    std::string error;
    assert(ini.loadText(R"(
[E2]
Strength=125
Speed=4
Primary=M1Carbine ; comment
[InlineSection] ; section comment
Value=ok
// comment-only line
[M1Carbine]
Damage=15
ROF=25
Range=4
Projectile=InvisibleLow
Warhead=SA
[SA]
Verses=100%,80%
[InvisibleLow]
Inviso=yes
)", error));
    assert(ini.getInt("E2", "Strength") == 125);
    assert(ini.get("E2", "Primary") == "M1Carbine");
    assert(ini.get("InlineSection", "Value") == "ok");
    assert(ini.getBool("InvisibleLow", "Inviso"));
    assert(ini.hasKey("E2", "Strength"));

    const std::filesystem::path contentRoot = std::filesystem::current_path();
    const std::filesystem::path rulesPath = contentRoot / "INI/Rules.ini";
    const std::filesystem::path artPath = contentRoot / "INI/Art.ini";
    const std::filesystem::path spritePath = contentRoot / "assets/game/ra2/infantry/CONS.SHP";
    const std::filesystem::path palettePath = contentRoot / "assets/game/ra2/palettes/unittem.pal";
    assert(std::filesystem::exists(rulesPath));
    assert(std::filesystem::exists(artPath));
    assert(std::filesystem::exists(spritePath));
    assert(std::filesystem::exists(palettePath));

    gamedata::RulesDatabase rules;
    assert(rules.load(rulesPath, error));
    assert(rules.e2().id == "E2");
    assert(rules.e2().image == "CONS");
    assert(rules.e2().armorValue == 0 && rules.e2().armorType == "Light");
    assert(rules.e2().selectable && rules.e2().autoAcquire && rules.e2().returnFire);
    assert(rules.e2().faction == Faction::Soviet);
    assert(rules.e2().unitTags.size() == 2 && rules.e2().unitTags[0] == "Biological" &&
        rules.e2().unitTags[1] == "Infantry");
    assert(std::abs(rules.e2().selectionRadius - 0.30F) < 0.001F);
    assert(rules.e2().occupancyProfile == "Infantry");
    assert(rules.e2().voiceSelect == "E2Select" && rules.e2().voiceMove == "E2Move" &&
        rules.e2().voiceAttack == "E2Attack");
    assert(rules.e2().primary.projectile == "InvisibleLow");
    assert(rules.e2().name == "Conscript");
    assert(rules.e2().secondaryName.empty());
    assert(rules.e2().weapons.size() == 1 && rules.e2().weapons.front().uiName == "M1Carbine");

    gamedata::LocalizationDatabase chinese;
    gamedata::LocalizationDatabase english;
    assert(chinese.load(contentRoot / "assets/ui/locales/zh_cn.json", error));
    assert(english.load(contentRoot / "assets/ui/locales/en_us.json", error));
    assert(chinese.get("unit.E2.name") == "动员兵");
    assert(english.get("unit.E2.name") == "CONSCRIPT");
    assert(chinese.get("force_attack") == "强制攻击");

    gamedata::TerrainDatabase terrainDatabase;
    assert(terrainDatabase.load(contentRoot / "INI/Terrain.ini", error));
    assert(terrainDatabase.find("GRASS") != nullptr);
    assert(terrainDatabase.find("GRASS")->passable && terrainDatabase.find("GRASS")->buildable);
    gamedata::VeterancyDatabase veterancy;
    assert(veterancy.load(rulesPath, error));
    assert(veterancy.find("Standard") != nullptr);
    assert(veterancy.level("Standard", 0)->id == "Rookie");
    assert(veterancy.level("Standard", 50)->id == "Veteran");
    assert(veterancy.nextLevel("Standard", 50)->id == "Elite");

    gamedata::UiLayoutDatabase ui;
    assert(ui.load(contentRoot / "INI/UI.ini", error));
    assert(ui.theme().skin.name == "ra2_soviet");
    const std::array<const char*, 19> formalPanelImages = {
        "ui.hud.background", "ui.hud.minimap.background", "ui.hud.unitstatus.background",
        "ui.hud.portrait.background",
        "ui.hud.commandcard.background", "ui.hud.production.background", "ui.hud.strategic.background",
        "ui.editor.sandbox.background", "ui.editor.button.normal", "ui.editor.button.hover",
        "ui.editor.button.active", "ui.editor.tab.normal", "ui.editor.tab.active",
        "ui.editor.asset.normal", "ui.editor.asset.hover", "ui.editor.asset.active",
        "ui.editor.dropdown", "ui.editor.separator", "ui.unitstatus.card",
    };
    for (const char* imageId : formalPanelImages) {
        assert(ui.hasImage(imageId));
        assert(std::filesystem::exists(ui.imagePath(imageId, contentRoot)));
    }
    assert(ui.rect("hud.unitstatus").width > 600.0F);
    assert(ui.childRect("hud.unitstatus", "hud.unitstatus.preview").width > 0.0F);
    assert(ui.relativeRect("hud.unitstatus.card.badge").width > 0.0F);
    assert(ui.imagePath("ui.hud.unitstatus.background", contentRoot).filename() == "unitstatus_clean.png");
    const Rect commandPanel = ui.rect("hud.command_card");
    const Rect minimapPanel = ui.rect("hud.minimap");
    const Rect minimapUiField = ui.childRect("hud.minimap", "minimap.field");
    const Rect portraitPanel = ui.rect("hud.portrait");
    const Rect portraitViewport = ui.rect("hud.portrait.viewport");
    assert(commandPanel.height > ui.rect("hud.unitstatus").height);
    assert(commandPanel.height == minimapPanel.height);
    assert(minimapUiField.width == minimapUiField.height);
    assert(minimapPanel.x + minimapPanel.width == ui.rect("hud.unitstatus").x);
    assert(ui.rect("hud.unitstatus").x + ui.rect("hud.unitstatus").width == portraitPanel.x);
    assert(portraitPanel.x + portraitPanel.width == commandPanel.x);
    assert(std::abs(portraitPanel.width / portraitPanel.height - (2.0F / 3.0F)) < 0.001F);
    assert(std::abs(portraitViewport.width / portraitViewport.height - (2.0F / 3.0F)) < 0.001F);
    assert(ui.setting("HUD.UnitStatus", "CardWidth", 0.0F) ==
        ui.setting("HUD.UnitStatus", "CardHeight", 0.0F));
    for (int slot = 0; slot < 15; ++slot) {
        const Rect commandSlot = ui.childRect("hud.command_card",
            "hud.command_card.slot." + std::to_string(slot));
        assert(commandSlot.width == commandSlot.height);
        assert(commandSlot.x >= commandPanel.x && commandSlot.y >= commandPanel.y);
        assert(commandSlot.x + commandSlot.width <= commandPanel.x + commandPanel.width);
        assert(commandSlot.y + commandSlot.height <= commandPanel.y + commandPanel.height);
    }
    for (int index = 0; index < 4; ++index) {
        const Rect tab = ui.rect("hud.production.tab." + std::to_string(index));
        assert(tab.width == tab.height);
    }
    for (int index = 0; index < 3; ++index) {
        const Rect producer = ui.rect("hud.production.producer." + std::to_string(index));
        assert(producer.width == producer.height);
    }
    for (int index = 0; index < 6; ++index) {
        const Rect product = ui.rect("hud.production.product." + std::to_string(index));
        assert(std::abs(product.width / product.height - 0.87F) < 0.01F);
    }
    const Rect assetIcon = ui.relativeRect("sandbox.asset.icon.0");
    const Rect assetLabel = ui.relativeRect("sandbox.asset.label.0");
    assert(assetIcon.width > 0.0F && assetIcon.height > 0.0F);
    assert(assetLabel.width == ui.relativeRect("sandbox.asset.card.0").width);
    assert(std::abs(ui.setting("HUD.UnitStatus", "HealthyThreshold") - 0.60F) < 0.001F);
    assert(std::abs(ui.setting("HUD.UnitStatus", "CriticalThreshold") - 0.30F) < 0.001F);
    assert(std::abs(ui.setting("HUD.UnitStatus", "CardColumns") - 5.0F) < 0.001F);
    assert(utf8ToWide("动员兵") == L"动员兵");
    assert(wideToUtf8(L"轻型装甲") == "轻型装甲");

    westwood::IniDocument voices;
    assert(voices.load(contentRoot / "assets/audio/voices.ini", error));
    assert(voices.get("E2Select", "Files").find("e2_select_1.wav") != std::string::npos);
    assert(voices.get("E2Select", "Files").find("e2_select_3.wav") != std::string::npos);
    assert(voices.getBool("E2Select", "NoImmediateRepeat"));

    const std::filesystem::path movedUiPath =
        std::filesystem::temp_directory_path() / "ra2yr-ui-layout-test.ini";
    {
        std::ofstream movedUi(movedUiPath);
        movedUi << "[Theme]\nName=test\n[Images]\n[Rects]\n"
                   "world.viewport=0,0,100,100\n"
                   "hud.command_card=400,500,382,188\n"
                   "[RelativeRects]\n"
                   "sandbox.title_bar=0,0,20,20\n"
                   "hud.command_card.slot.0=5,28,72,48\n";
    }
    gamedata::UiLayoutDatabase movedUi;
    assert(movedUi.load(movedUiPath, error));
    const Rect movedCard = movedUi.rect("hud.command_card");
    const Rect movedSlot = movedUi.childRect("hud.command_card", "hud.command_card.slot.0");
    assert(movedSlot.x == movedCard.x + 5.0F && movedSlot.y == movedCard.y + 28.0F);
    std::filesystem::remove(movedUiPath);

    gamedata::ArtDatabase art;
    assert(art.load(artPath, error));
    assert(art.find("CONS") != nullptr);
    assert(art.find("CONS")->remapable);
    assert(art.facingCount("CONS") == 8);
    assert(art.facingForDirection("CONS", 0) == 0);
    assert(art.facingForDirection("CONS", 7) == 7);
    assert(art.frameIndexForDirection("CONS", "Walk", 0, 5) == 8 + 5 * 6);
    assert(art.frameIndex("CONS", "Ready") == 0);
    assert(art.frameIndex("CONS", "Ready", 0, 7) == 7);
    assert(art.frameIndex("CONS", "Walk") == 8);
    assert(art.frameIndex("CONS", "Walk", 3, 2) == 23);
    assert(art.frameIndex("CONS", "Fire", 5, 7) == 211);
    assert(art.frameIndex("CONS", "Death", 14, 0) == 148);
    assert(art.frameIndex("CONS", "Death", 14, 7) == 148);
    assert(art.sequenceIsDirectional("CONS", "Ready"));
    assert(art.sequenceIsDirectional("CONS", "Walk"));
    assert(art.sequenceIsDirectional("CONS", "Fire"));
    assert(!art.sequenceIsDirectional("CONS", "Death"));
    assert(art.sequenceFrameDelayMs("CONS", "Walk") == 83);
    assert(art.sequenceLoops("CONS", "Walk"));
    assert(!art.sequenceLoops("CONS", "Death"));
    for (int facing = 0; facing < 8; ++facing) {
        assert(art.frameIndex("CONS", "Ready", 0, facing) == facing);
        assert(art.frameIndex("CONS", "Walk", 0, facing) == 8 + facing * 6);
        assert(art.frameIndex("CONS", "Fire", 0, facing) == 164 + facing * 6);
        assert(art.frameIndex("CONS", "Death", 0, facing) == 134);
    }

    westwood::ShpTsDocument projectShp;
    assert(projectShp.load(spritePath, error));
    assert(projectShp.width() == 76 && projectShp.height() == 96 && projectShp.frameCount() == 602);
    assert(projectShp.frame(0).fullWidth == 76 && projectShp.frame(0).fullHeight == 96);
    westwood::Palette projectPalette;
    assert(projectPalette.load(palettePath, error));
    assert(projectPalette.remappedColor(16U, Owner::Red).r != projectPalette.remappedColor(16U, Owner::Blue).r);
    assert(projectPalette.remappedColor(31U, Owner::Red).g != projectPalette.remappedColor(31U, Owner::Blue).g);
    assert(projectPalette.remappedColor(0U, Owner::Red).r == projectPalette.color(0U).r);

    IsoProjection projection;
    for (const GridCoord coordinate : std::vector<GridCoord>{{0, 0}, {3, 7}, {24, 11}, {63, 63}}) {
        const GridCoord roundTrip = projection.toGrid(projection.toScreen({static_cast<float>(coordinate.x), static_cast<float>(coordinate.y)}));
        assert(roundTrip == coordinate);
    }
    IsometricCamera camera;
    camera.viewportCenter = {795.0F, 440.0F};
    camera.worldCenter = {12.0F, 9.0F};
    const ScreenCoord cameraPoint = camera.toScreen({15.0F, 11.0F});
    const WorldCoord cameraRoundTrip = camera.toWorld(cameraPoint);
    assert(std::abs(cameraRoundTrip.x - 15.0F) < 0.001F && std::abs(cameraRoundTrip.y - 11.0F) < 0.001F);
    const ScreenCoord zoomCursor{920.0F, 520.0F};
    const WorldCoord zoomBefore = camera.toWorld(zoomCursor);
    camera.zoomAt(zoomCursor, 1.5F);
    const WorldCoord zoomAfter = camera.toWorld(zoomCursor);
    assert(std::abs(zoomBefore.x - zoomAfter.x) < 0.001F && std::abs(zoomBefore.y - zoomAfter.y) < 0.001F);
    assert(simulation::Simulation::directionFromDelta(-1.0F, -1.0F) == Direction8::North);
    assert(simulation::Simulation::directionFromDelta(-1.0F, 0.0F) == Direction8::NorthWest);
    assert(simulation::Simulation::directionFromDelta(-1.0F, 1.0F) == Direction8::West);
    assert(simulation::Simulation::directionFromDelta(0.0F, 1.0F) == Direction8::SouthWest);
    assert(simulation::Simulation::directionFromDelta(1.0F, 1.0F) == Direction8::South);
    assert(simulation::Simulation::directionFromDelta(1.0F, 0.0F) == Direction8::SouthEast);
    assert(simulation::Simulation::directionFromDelta(1.0F, -1.0F) == Direction8::East);
    assert(simulation::Simulation::directionFromDelta(0.0F, -1.0F) == Direction8::NorthEast);

    const std::filesystem::path syntheticShp = std::filesystem::temp_directory_path() / "ra2yr-core-test.shp";
    std::array<std::uint8_t, 36> shpBytes{};
    shpBytes[2] = 2;
    shpBytes[4] = 2;
    shpBytes[6] = 1;
    shpBytes[8 + 4] = 2;
    shpBytes[8 + 6] = 2;
    shpBytes[8 + 8] = 1;
    shpBytes[8 + 20] = 32;
    shpBytes[32] = 1;
    shpBytes[33] = 2;
    shpBytes[34] = 3;
    shpBytes[35] = 4;
    {
        std::ofstream stream(syntheticShp, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(shpBytes.data()), static_cast<std::streamsize>(shpBytes.size()));
    }
    westwood::ShpTsDocument shp;
    assert(shp.load(syntheticShp, error));
    assert(shp.width() == 2 && shp.height() == 2 && shp.frameCount() == 1);
    assert(shp.frame(0).pixels.size() == 4 && shp.frame(0).pixels[3] == 4);
    std::filesystem::remove(syntheticShp);

    const gamedata::ArtDefinition& animationDefinition = *art.find("CONS");
    gamedata::UnitDefinition definition;
    definition.id = "E2";
    definition.faction = Faction::Soviet;
    definition.unitTags = {"Biological", "Infantry"};
    definition.strength = 60;
    definition.speed = 4;
    definition.primary.damage = 30;
    definition.primary.range = 4.0F;
    simulation::Simulation simulation(animationDefinition);
    const std::uint32_t red = simulation.spawn(definition, Owner::Red, {0, 0});
    const std::uint32_t blue = simulation.spawn(definition, Owner::Blue, {8, 0});
    assert(simulation.find(red)->definitionId == "E2");
    assert(simulation.find(red)->faction == Faction::Soviet);
    simulation.selectSingle({0, 0});
    assert(simulation.find(red)->animationState == simulation::AnimationState::Idle);
    simulation.issueMove({3, 0});
    simulation.update(1.0F);
    const auto* moved = simulation.find(red);
    assert(moved != nullptr && moved->position.x > 0.0F);
    assert(moved->animationState == simulation::AnimationState::Walk ||
        moved->animationState == simulation::AnimationState::Idle);
    assert(moved->facing >= 0 && moved->facing < 8);
    assert(moved->direction == Direction8::SouthEast);

    simulation::Simulation boxSimulation(animationDefinition);
    const std::uint32_t boxFirst = boxSimulation.spawn(definition, Owner::Red, {4, 4});
    const std::uint32_t boxSecond = boxSimulation.spawn(definition, Owner::Red, {6, 5});
    const std::uint32_t boxOutside = boxSimulation.spawn(definition, Owner::Red, {12, 12});
    boxSimulation.selectBox(std::array<WorldCoord, 4>{
        WorldCoord{3.0F, 3.0F}, WorldCoord{8.0F, 3.0F},
        WorldCoord{8.0F, 7.0F}, WorldCoord{3.0F, 7.0F}});
    assert(boxSimulation.find(boxFirst)->selected);
    assert(boxSimulation.find(boxSecond)->selected);
    assert(!boxSimulation.find(boxOutside)->selected);

    gamedata::UnitDefinition tagOnlyDefinition = definition;
    tagOnlyDefinition.unitTags = {"Infantry"};
    tagOnlyDefinition.occupancyProfile.clear();
    simulation::Simulation explicitOccupancyOnly(animationDefinition);
    const std::uint32_t tagOnly = explicitOccupancyOnly.spawn(tagOnlyDefinition, Owner::Red, {10, 10});
    assert(explicitOccupancyOnly.find(tagOnly)->occupancySubcell == simulation::InfantrySubcell::None);

    gamedata::UnitDefinition infantryDefinition = definition;
    infantryDefinition.occupancyProfile = "Infantry";
    infantryDefinition.selectionRadius = 0.30F;
    simulation::Simulation occupancySimulation(animationDefinition);
    const std::uint32_t infantryOne = occupancySimulation.spawn(infantryDefinition, Owner::Red, {10, 10});
    const std::uint32_t infantryTwo = occupancySimulation.spawn(infantryDefinition, Owner::Red, {10, 10});
    const std::uint32_t infantryThree = occupancySimulation.spawn(infantryDefinition, Owner::Red, {10, 10});
    const std::uint32_t infantryFour = occupancySimulation.spawn(infantryDefinition, Owner::Red, {10, 10});
    const auto* firstInfantry = occupancySimulation.find(infantryOne);
    const auto* secondInfantry = occupancySimulation.find(infantryTwo);
    const auto* thirdInfantry = occupancySimulation.find(infantryThree);
    const auto* fourthInfantry = occupancySimulation.find(infantryFour);
    const GridCoord requestedCell{10, 10};
    assert(firstInfantry->occupancyCell == requestedCell);
    assert(secondInfantry->occupancyCell == requestedCell);
    assert(thirdInfantry->occupancyCell == requestedCell);
    assert(firstInfantry->occupancySubcell != secondInfantry->occupancySubcell);
    assert(firstInfantry->occupancySubcell != thirdInfantry->occupancySubcell);
    assert(secondInfantry->occupancySubcell != thirdInfantry->occupancySubcell);
    assert(fourthInfantry->occupancySubcell != simulation::InfantrySubcell::None);
    assert(distance(firstInfantry->position, fourthInfantry->position) > 0.01F);
    for (auto& entity : occupancySimulation.entities()) {
        entity.selected = true;
    }
    occupancySimulation.issueMove({20, 20});
    for (int step = 0; step < 300; ++step) {
        occupancySimulation.update(1.0F / 60.0F);
    }
    const auto& occupiedEntities = occupancySimulation.entities();
    for (std::size_t first = 0; first < occupiedEntities.size(); ++first) {
        for (std::size_t second = first + 1; second < occupiedEntities.size(); ++second) {
            assert(distance(occupiedEntities[first].position, occupiedEntities[second].position) > 0.01F);
        }
    }

    const auto formationSignature = [&](std::size_t count) {
        simulation::Simulation formation(animationDefinition);
        std::vector<std::uint32_t> ids;
        for (std::size_t index = 0; index < count; ++index) {
            const std::uint32_t id = formation.spawn(infantryDefinition, Owner::Red,
                {static_cast<int>(index) * 2, 2});
            assert(id != 0);
            ids.push_back(id);
            formation.find(id)->selected = true;
        }
        formation.issueMove({8, 8});
        std::vector<std::pair<GridCoord, simulation::InfantrySubcell>> signature;
        for (const std::uint32_t id : ids) {
            const auto* entity = formation.find(id);
            assert(entity != nullptr && entity->reservedSubcell != simulation::InfantrySubcell::None);
            signature.emplace_back(entity->reservedCell, entity->reservedSubcell);
        }
        return std::pair{std::move(formation), std::move(signature)};
    };
    auto [sixFormation, sixSignature] = formationSignature(6);
    auto sixRepeat = formationSignature(6);
    assert(sixSignature == sixRepeat.second);
    for (int step = 0; step < 600; ++step) {
        sixFormation.update(1.0F / 60.0F);
    }
    std::map<std::pair<int, int>, int> sixCells;
    std::map<std::pair<int, int>, std::array<bool, 3>> sixSubcells;
    for (const auto& entity : sixFormation.entities()) {
        sixCells[{entity.occupancyCell.x, entity.occupancyCell.y}]++;
        const auto slot = static_cast<std::size_t>(entity.occupancySubcell);
        assert(slot < 3U);
        sixSubcells[{entity.occupancyCell.x, entity.occupancyCell.y}][slot] = true;
    }
    assert(sixCells.size() == 2);
    for (const auto& [cell, cellCount] : sixCells) {
        assert(cellCount == 3);
        assert(std::all_of(sixSubcells[cell].begin(), sixSubcells[cell].end(),
            [](bool occupied) { return occupied; }));
    }
    const auto sixStableLayout = [&sixFormation]() {
        std::vector<std::tuple<GridCoord, simulation::InfantrySubcell, WorldCoord>> layout;
        for (const auto& entity : sixFormation.entities()) {
            layout.emplace_back(entity.occupancyCell, entity.occupancySubcell, entity.position);
            assert(entity.order.kind == CommandKind::None);
        }
        return layout;
    }();
    for (int step = 0; step < 300; ++step) {
        sixFormation.update(1.0F / 60.0F);
    }
    for (std::size_t index = 0; index < sixFormation.entities().size(); ++index) {
        const auto& entity = sixFormation.entities()[index];
        const auto& stable = sixStableLayout[index];
        assert(entity.occupancyCell == std::get<0>(stable));
        assert(entity.occupancySubcell == std::get<1>(stable));
        assert(std::abs(entity.position.x - std::get<2>(stable).x) < 0.001F);
        assert(std::abs(entity.position.y - std::get<2>(stable).y) < 0.001F);
        assert(entity.order.kind == CommandKind::None);
    }
    // A second command to the same cell must reuse the released slots rather than
    // seeing stale reservations left behind by the first arrival.
    sixFormation.issueMove({8, 8});
    for (std::size_t index = 0; index < sixSignature.size(); ++index) {
        const auto& entity = sixFormation.entities()[index];
        assert(std::make_pair(entity.reservedCell, entity.reservedSubcell) == sixSignature[index]);
    }
    auto [nineFormation, nineSignature] = formationSignature(9);
    static_cast<void>(nineSignature);
    for (int step = 0; step < 600; ++step) {
        nineFormation.update(1.0F / 60.0F);
    }
    std::map<std::pair<int, int>, int> nineCells;
    std::map<std::pair<int, int>, std::array<bool, 3>> nineSubcells;
    for (const auto& entity : nineFormation.entities()) {
        nineCells[{entity.occupancyCell.x, entity.occupancyCell.y}]++;
        const auto slot = static_cast<std::size_t>(entity.occupancySubcell);
        assert(slot < 3U);
        nineSubcells[{entity.occupancyCell.x, entity.occupancyCell.y}][slot] = true;
    }
    assert(nineCells.size() == 3);
    for (const auto& [cell, cellCount] : nineCells) {
        assert(cellCount == 3);
        assert(std::all_of(nineSubcells[cell].begin(), nineSubcells[cell].end(),
            [](bool occupied) { return occupied; }));
    }
    const auto nineStableLayout = [&nineFormation]() {
        std::vector<std::tuple<GridCoord, simulation::InfantrySubcell, WorldCoord>> layout;
        for (const auto& entity : nineFormation.entities()) {
            layout.emplace_back(entity.occupancyCell, entity.occupancySubcell, entity.position);
            assert(entity.order.kind == CommandKind::None);
        }
        return layout;
    }();
    for (int step = 0; step < 300; ++step) {
        nineFormation.update(1.0F / 60.0F);
    }
    for (std::size_t index = 0; index < nineFormation.entities().size(); ++index) {
        const auto& entity = nineFormation.entities()[index];
        const auto& stable = nineStableLayout[index];
        assert(entity.occupancyCell == std::get<0>(stable));
        assert(entity.occupancySubcell == std::get<1>(stable));
        assert(std::abs(entity.position.x - std::get<2>(stable).x) < 0.001F);
        assert(std::abs(entity.position.y - std::get<2>(stable).y) < 0.001F);
        assert(entity.order.kind == CommandKind::None);
    }
    for (std::size_t first = 0; first < nineFormation.entities().size(); ++first) {
        for (std::size_t second = first + 1; second < nineFormation.entities().size(); ++second) {
            assert(distance(nineFormation.entities()[first].position,
                nineFormation.entities()[second].position) > 0.01F);
        }
    }

    simulation::Simulation overrideSimulation(animationDefinition);
    const std::uint32_t overrideRed = overrideSimulation.spawn(definition, Owner::Red, {0, 0});
    const std::uint32_t overrideBlue = overrideSimulation.spawn(definition, Owner::Blue, {3, 0});
    overrideSimulation.find(overrideRed)->recentAttacker = overrideBlue;
    overrideSimulation.selectEntity(overrideRed);
    overrideSimulation.issueAttack(overrideBlue);
    overrideSimulation.issueMove({12, 0});
    assert(overrideSimulation.find(overrideRed)->order.kind == CommandKind::Move);
    assert(overrideSimulation.find(overrideRed)->recentAttacker == 0);
    const float overrideStartX = overrideSimulation.find(overrideRed)->position.x;
    overrideSimulation.update(0.25F);
    assert(overrideSimulation.find(overrideRed)->position.x > overrideStartX);

    const Rect minimapField{14.0F, 38.0F, 292.0F, 168.0F};
    const IsoMapProjection minimapProjection(64.0F, 64.0F, minimapField);
    const ScreenCoord minimapCenter = minimapProjection.project({32.0F, 32.0F});
    const ScreenCoord minimapTop = minimapProjection.project({0.0F, 0.0F});
    const ScreenCoord minimapRight = minimapProjection.project({64.0F, 0.0F});
    const ScreenCoord minimapBottom = minimapProjection.project({64.0F, 64.0F});
    const ScreenCoord minimapLeft = minimapProjection.project({0.0F, 64.0F});
    assert(std::abs(minimapCenter.x - (minimapField.x + minimapField.width * 0.5F)) < 0.001F);
    assert(std::abs(minimapCenter.y - (minimapField.y + minimapField.height * 0.5F)) < 0.001F);
    assert(std::abs(minimapTop.x - minimapCenter.x) < 0.001F);
    assert(std::abs(minimapBottom.x - minimapCenter.x) < 0.001F);
    assert(std::abs(minimapLeft.y - minimapCenter.y) < 0.001F);
    assert(std::abs(minimapRight.y - minimapCenter.y) < 0.001F);
    assert(minimapRight.x > minimapLeft.x && minimapBottom.y > minimapTop.y);
    const WorldCoord minimapRoundTripSource{19.5F, 27.25F};
    const WorldCoord minimapRoundTrip = minimapProjection.unproject(
        minimapProjection.project(minimapRoundTripSource));
    assert(std::abs(minimapRoundTrip.x - minimapRoundTripSource.x) < 0.001F);
    assert(std::abs(minimapRoundTrip.y - minimapRoundTripSource.y) < 0.001F);

    const auto minimapViewport = [&minimapProjection](const IsometricCamera& camera) {
        constexpr Rect viewport{100.0F, 50.0F, 1390.0F, 780.0F};
        const std::array<ScreenCoord, 4> screenCorners = {
            ScreenCoord{viewport.x, viewport.y},
            ScreenCoord{viewport.x + viewport.width, viewport.y},
            ScreenCoord{viewport.x + viewport.width, viewport.y + viewport.height},
            ScreenCoord{viewport.x, viewport.y + viewport.height},
        };
        std::array<ScreenCoord, 4> corners{};
        for (std::size_t index = 0; index < screenCorners.size(); ++index) {
            corners[index] = minimapProjection.project(camera.toWorld(screenCorners[index]));
        }
        return corners;
    };
    const auto polygonArea = [](const std::array<ScreenCoord, 4>& polygon) {
        float area = 0.0F;
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const ScreenCoord first = polygon[index];
            const ScreenCoord second = polygon[(index + 1U) % polygon.size()];
            area += first.x * second.y - second.x * first.y;
        }
        return std::abs(area) * 0.5F;
    };
    IsometricCamera minimapCamera;
    minimapCamera.viewportCenter = {795.0F, 440.0F};
    minimapCamera.worldCenter = {32.0F, 32.0F};
    const auto minimapBeforePan = minimapViewport(minimapCamera);
    const float minimapBeforePanArea = polygonArea(minimapBeforePan);
    minimapCamera.panScreen({160.0F, 90.0F});
    const auto minimapAfterPan = minimapViewport(minimapCamera);
    assert(std::abs(polygonArea(minimapAfterPan) - minimapBeforePanArea) < 0.1F);
    assert(std::abs(minimapAfterPan[0].x - minimapBeforePan[0].x) > 0.001F ||
        std::abs(minimapAfterPan[0].y - minimapBeforePan[0].y) > 0.001F);
    minimapCamera.zoomAt({795.0F, 440.0F}, 1.3F);
    const auto minimapAfterZoom = minimapViewport(minimapCamera);
    assert(polygonArea(minimapAfterZoom) < polygonArea(minimapAfterPan));
    const WorldCoord minimapDown = minimapCamera.toWorld({795.0F, 540.0F});
    assert(minimapProjection.project(minimapDown).y > minimapProjection.project(minimapCamera.worldCenter).y);

    simulation.clearSelection();
    simulation.selectSingle({static_cast<int>(moved->position.x), static_cast<int>(moved->position.y)});
    simulation.issueAttack(blue);
    const int initialHealth = simulation.find(blue)->health;
    simulation.update(1.0F);
    simulation.update(1.0F);
    assert(simulation.find(blue) == nullptr || simulation.find(blue)->health < initialHealth);

    editor::TerrainMap editorMap(8, 8);
    editorMap.fill("GRASS");
    simulation::Simulation editorSimulation(animationDefinition, &veterancy);
    editor::EditorToolController editorController(editorMap, terrainDatabase, rules, editorSimulation);
    assert(editorController.loadBrushPresets(contentRoot / "INI/Editor.ini", error));
    assert(editorController.brushPresets().size() == 7);
    editorController.state().category = editor::EditorAssetCategory::Terrain;
    editorController.state().tool = editor::EditorToolId::Pencil;
    assert(editorController.apply({1, 1}).changed == false);
    editorController.state().terrainAsset = "GRASS";
    editorController.state().tool = editor::EditorToolId::Eraser;
    assert(editorController.apply({1, 1}).changed);
    assert(!editorMap.cell({1, 1}).exists);
    editorController.state().tool = editor::EditorToolId::Pencil;
    editorController.state().terrainAsset = "GRASS";
    editorController.state().tool = editor::EditorToolId::Eraser;
    assert(editorController.apply({2, 2}).changed);
    editorController.state().tool = editor::EditorToolId::Pencil;
    editorController.beginStroke();
    assert(editorController.continueStroke({2, 2}).changed);
    assert(!editorController.continueStroke({2, 2}).changed);
    editorController.endStroke();
    editorController.state().terrainAsset = "DIRT";
    editorController.state().tool = editor::EditorToolId::FillBucket;
    assert(editorController.apply({0, 0}).changed);
    assert(editorMap.cell({0, 0}).terrainTypeId == "DIRT");
    editorController.state().tool = editor::EditorToolId::Brush;
    editorController.state().brushPreset = 1;
    assert(editorController.previewCells({4, 4}).size() == 4);
    editorController.state().category = editor::EditorAssetCategory::Unit;
    editorController.state().unitAsset = "E2";
    editorController.state().tool = editor::EditorToolId::Pencil;
    editorController.state().owner = Owner::Blue;
    const std::uint32_t placedOne = editorController.apply({4, 4}).changed ?
        editorSimulation.entityAtCell({4, 4}) : 0;
    assert(placedOne != 0 && editorSimulation.find(placedOne)->owner == Owner::Blue);
    const std::uint32_t placedTwo = editorSimulation.spawn(rules.e2(), Owner::Blue, {4, 4});
    assert(placedTwo != 0 && editorSimulation.find(placedTwo)->position.x !=
        editorSimulation.find(placedOne)->position.x);
    editorController.state().tool = editor::EditorToolId::Eraser;
    assert(editorController.apply({4, 4}, placedOne).changed);
    assert(editorSimulation.find(placedOne) == nullptr);

    simulation::Entity statusEntity;
    statusEntity.health = 25;
    statusEntity.maxHealth = 125;
    statusEntity.shields = {10, 5};
    statusEntity.energy = 4;
    statusEntity.maxEnergy = 10;
    statusEntity.killCount = 2;
    statusEntity.veterancyProfile = "Standard";
    statusEntity.veterancyLevel = "Rookie";
    gamedata::UnitDefinition statusDefinition = rules.e2();
    statusDefinition.weapons = {statusDefinition.primary, statusDefinition.primary, statusDefinition.primary};
    const client::hud::UnitStatusViewModel status = client::hud::UnitStatusViewModelBuilder::build(
        statusEntity, statusDefinition, rules, veterancy, {});
    assert(status.displayName == "Conscript" && status.healthBand == client::hud::HealthBand::Critical);
    assert(status.shields.size() == 2 && status.energy == 4 && status.kills == 2);
    assert(status.weapons.size() == 3 && status.tags.size() == 3);
    assert(client::hud::UnitStatusViewModelBuilder::tooltip(status.weapons.front()).find(L"伤害：") !=
        std::wstring::npos);

    gamedata::UnitDefinition alliedDefinition = definition;
    alliedDefinition.id = "S1";
    alliedDefinition.faction = Faction::Allied;
    const std::uint32_t redAllied = simulation.spawn(alliedDefinition, Owner::Red, {30, 30});
    assert(simulation.find(redAllied)->owner == Owner::Red);
    assert(simulation.find(redAllied)->faction == Faction::Allied);

    gamedata::UnitDefinition behaviorDefinition = definition;
    behaviorDefinition.strength = 1000;
    behaviorDefinition.primary.damage = 10;
    behaviorDefinition.autoAcquire = true;
    behaviorDefinition.returnFire = true;
    simulation::Simulation idleSimulation(animationDefinition);
    const std::uint32_t idleRed = idleSimulation.spawn(behaviorDefinition, Owner::Red, {0, 0});
    const std::uint32_t idleBlue = idleSimulation.spawn(behaviorDefinition, Owner::Blue, {3, 0});
    const int idleBlueHealth = idleSimulation.find(idleBlue)->health;
    idleSimulation.update(1.0F);
    assert(idleSimulation.find(idleBlue)->health < idleBlueHealth);
    assert(idleSimulation.find(idleRed)->animationState == simulation::AnimationState::Attack);

    simulation::Simulation stopSimulation(animationDefinition);
    const std::uint32_t stopRed = stopSimulation.spawn(behaviorDefinition, Owner::Red, {0, 0});
    const std::uint32_t stopBlue = stopSimulation.spawn(behaviorDefinition, Owner::Blue, {3, 0});
    stopSimulation.selectSingle({0, 0});
    stopSimulation.issueMove({20, 0});
    stopSimulation.update(0.25F);
    stopSimulation.selectSingle({static_cast<int>(stopSimulation.find(stopRed)->position.x), 0});
    stopSimulation.issueStop();
    const float stoppedX = stopSimulation.find(stopRed)->position.x;
    const int stopBlueHealth = stopSimulation.find(stopBlue)->health;
    stopSimulation.update(1.0F);
    assert(stopSimulation.find(stopRed)->position.x == stoppedX);
    assert(stopSimulation.find(stopBlue)->health < stopBlueHealth);

    simulation::Simulation holdSimulation(animationDefinition);
    const std::uint32_t holdRed = holdSimulation.spawn(behaviorDefinition, Owner::Red, {0, 0});
    const std::uint32_t holdBlue = holdSimulation.spawn(behaviorDefinition, Owner::Blue, {3, 0});
    holdSimulation.selectSingle({0, 0});
    holdSimulation.issueHold();
    const int holdBlueHealth = holdSimulation.find(holdBlue)->health;
    holdSimulation.update(1.0F);
    assert(holdSimulation.find(holdRed)->position.x == 0.0F);
    assert(holdSimulation.find(holdBlue)->health < holdBlueHealth);

    gamedata::UnitDefinition experienceDefinition = behaviorDefinition;
    experienceDefinition.strength = 40;
    experienceDefinition.primary.damage = 40;
    experienceDefinition.experienceValue = 25;
    simulation::Simulation experienceSimulation(animationDefinition, &veterancy);
    const std::uint32_t experienceAttacker = experienceSimulation.spawn(
        experienceDefinition, Owner::Red, {0, 0});
    const std::uint32_t experienceTarget = experienceSimulation.spawn(
        experienceDefinition, Owner::Blue, {3, 0});
    experienceSimulation.selectEntity(experienceAttacker);
    experienceSimulation.issueAttack(experienceTarget);
    experienceSimulation.update(1.0F / 30.0F);
    assert(experienceSimulation.find(experienceAttacker)->killCount == 1);
    assert(experienceSimulation.find(experienceAttacker)->experience == 25);
    assert(experienceSimulation.find(experienceTarget) != nullptr);

    gamedata::UnitDefinition returnFireDefinition = behaviorDefinition;
    returnFireDefinition.autoAcquire = false;
    simulation::Simulation returnFireSimulation(animationDefinition);
    const std::uint32_t returnRed = returnFireSimulation.spawn(returnFireDefinition, Owner::Red, {0, 0});
    const std::uint32_t returnBlue = returnFireSimulation.spawn(returnFireDefinition, Owner::Blue, {3, 0});
    returnFireSimulation.selectSingle({3, 0});
    returnFireSimulation.issueAttack(returnRed);
    returnFireSimulation.update(1.0F);
    const int returnBlueHealth = returnFireSimulation.find(returnBlue)->health;
    returnFireSimulation.update(1.0F);
    assert(returnFireSimulation.find(returnRed)->recentAttacker == returnBlue);
    assert(returnFireSimulation.find(returnBlue)->health < returnBlueHealth);

    simulation::Simulation forceAttackSimulation(animationDefinition);
    const std::uint32_t forceAttacker = forceAttackSimulation.spawn(
        behaviorDefinition, Owner::Red, {0, 0});
    const std::uint32_t forceFriendlyTarget = forceAttackSimulation.spawn(
        behaviorDefinition, Owner::Red, {2, 0});
    forceAttackSimulation.selectEntity(forceAttacker);
    const int friendlyHealth = forceAttackSimulation.find(forceFriendlyTarget)->health;
    forceAttackSimulation.issueForceAttack({2, 0}, forceFriendlyTarget);
    forceAttackSimulation.update(1.0F / 30.0F);
    assert(forceAttackSimulation.find(forceFriendlyTarget)->health < friendlyHealth);

    forceAttackSimulation.selectEntity(forceAttacker);
    const std::uint32_t attackEventBefore = forceAttackSimulation.find(forceAttacker)->attackEvent;
    forceAttackSimulation.issueForceAttack({1, 0});
    forceAttackSimulation.update(1.0F);
    assert(forceAttackSimulation.find(forceAttacker)->attackEvent > attackEventBefore);

    std::cout << "ra2yr core tests passed\n";
    return 0;
}
