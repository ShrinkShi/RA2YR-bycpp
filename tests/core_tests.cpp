#include "GameData/Art.h"
#include "GameData/Rules.h"
#include "GameData/UI.h"
#include "Westwood/Palette/Palette.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"
#include "Westwood/Shp/Shp.h"

#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
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

    gamedata::UiLayoutDatabase ui;
    assert(ui.load(contentRoot / "INI/UI.ini", error));
    assert(ui.theme().skin.name == "ra2_soviet");
    const std::array<const char*, 9> formalPanelImages = {
        "ui.hud.background", "ui.hud.minimap.background", "ui.hud.model.background",
        "ui.hud.unitinfo.background", "ui.hud.portrait.background", "ui.hud.commandcard.background",
        "ui.hud.production.background", "ui.hud.strategic.background", "ui.editor.sandbox.background",
    };
    for (const char* imageId : formalPanelImages) {
        assert(ui.hasImage(imageId));
        assert(std::filesystem::exists(ui.imagePath(imageId, contentRoot)));
    }
    const Rect commandSlot = ui.childRect("hud.command_card", "hud.command_card.slot.0");
    assert(commandSlot.x == ui.rect("hud.command_card").x + 5.0F);
    assert(commandSlot.y == ui.rect("hud.command_card").y + 28.0F);

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

    const auto minimapWorldExtents = [](const IsometricCamera& camera) {
        constexpr Rect viewport{100.0F, 50.0F, 1390.0F, 780.0F};
        const ScreenCoord viewportCenter{
            viewport.x + viewport.width * 0.5F, viewport.y + viewport.height * 0.5F};
        const WorldCoord center = camera.toWorld(viewportCenter);
        const WorldCoord corners[] = {
            camera.toWorld({viewport.x, viewport.y}),
            camera.toWorld({viewport.x + viewport.width, viewport.y}),
            camera.toWorld({viewport.x, viewport.y + viewport.height}),
            camera.toWorld({viewport.x + viewport.width, viewport.y + viewport.height}),
        };
        float minX = corners[0].x - center.x;
        float maxX = minX;
        float minY = corners[0].y - center.y;
        float maxY = minY;
        for (const WorldCoord corner : corners) {
            minX = std::min(minX, corner.x - center.x);
            maxX = std::max(maxX, corner.x - center.x);
            minY = std::min(minY, corner.y - center.y);
            maxY = std::max(maxY, corner.y - center.y);
        }
        return Rect{center.x + minX, center.y + minY, maxX - minX, maxY - minY};
    };
    IsometricCamera minimapCamera;
    minimapCamera.viewportCenter = {795.0F, 440.0F};
    minimapCamera.worldCenter = {32.0F, 32.0F};
    const Rect minimapBeforePan = minimapWorldExtents(minimapCamera);
    minimapCamera.panScreen({160.0F, 90.0F});
    const Rect minimapAfterPan = minimapWorldExtents(minimapCamera);
    assert(std::abs(minimapAfterPan.width - minimapBeforePan.width) < 0.001F);
    assert(std::abs(minimapAfterPan.height - minimapBeforePan.height) < 0.001F);
    assert(std::abs(minimapAfterPan.x - minimapBeforePan.x) > 0.001F ||
        std::abs(minimapAfterPan.y - minimapBeforePan.y) > 0.001F);
    minimapCamera.zoomAt({795.0F, 440.0F}, 1.3F);
    const Rect minimapAfterZoom = minimapWorldExtents(minimapCamera);
    assert(minimapAfterZoom.width < minimapAfterPan.width);
    assert(minimapAfterZoom.height < minimapAfterPan.height);

    simulation.clearSelection();
    simulation.selectSingle({static_cast<int>(moved->position.x), static_cast<int>(moved->position.y)});
    simulation.issueAttack(blue);
    const int initialHealth = simulation.find(blue)->health;
    simulation.update(1.0F);
    simulation.update(1.0F);
    assert(simulation.find(blue) == nullptr || simulation.find(blue)->health < initialHealth);

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

    std::cout << "ra2yr core tests passed\n";
    return 0;
}
