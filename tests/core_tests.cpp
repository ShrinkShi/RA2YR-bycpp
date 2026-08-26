#include "GameData/Art.h"
#include "GameData/Rules.h"
#include "Westwood/Palette/Palette.h"
#include "Simulation/Simulation.h"
#include "Westwood/Ini/Ini.h"
#include "Westwood/Shp/Shp.h"

#include <array>
#include <cassert>
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
    assert(rules.e2().primary.projectile == "InvisibleLow");

    gamedata::ArtDatabase art;
    assert(art.load(artPath, error));
    assert(art.find("CONS") != nullptr);
    assert(art.find("CONS")->remapable);
    assert(art.frameIndex("CONS", "Ready") == 0);
    assert(art.frameIndex("CONS", "Walk") == 8);
    assert(art.frameIndex("CONS", "Fire") == 164);
    assert(art.frameIndex("CONS", "Death") == 134);

    westwood::ShpTsDocument projectShp;
    assert(projectShp.load(spritePath, error));
    assert(projectShp.width() == 76 && projectShp.height() == 96 && projectShp.frameCount() == 602);
    westwood::Palette projectPalette;
    assert(projectPalette.load(palettePath, error));
    assert(projectPalette.remappedColor(0xc0U, Owner::Red).r != projectPalette.remappedColor(0xc0U, Owner::Blue).r);

    IsoProjection projection;
    for (const GridCoord coordinate : std::vector<GridCoord>{{0, 0}, {3, 7}, {24, 11}, {63, 63}}) {
        const GridCoord roundTrip = projection.toGrid(projection.toScreen({static_cast<float>(coordinate.x), static_cast<float>(coordinate.y)}));
        assert(roundTrip == coordinate);
    }

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

    gamedata::UnitDefinition definition;
    definition.strength = 60;
    definition.speed = 4;
    definition.primary.damage = 30;
    definition.primary.range = 4.0F;
    simulation::Simulation simulation(definition);
    const std::uint32_t red = simulation.spawn(Owner::Red, {0, 0});
    const std::uint32_t blue = simulation.spawn(Owner::Blue, {8, 0});
    simulation.selectSingle({0, 0});
    simulation.issueMove({3, 0});
    simulation.update(1.0F);
    const auto* moved = simulation.find(red);
    assert(moved != nullptr && moved->position.x > 0.0F);

    simulation.clearSelection();
    simulation.selectSingle({static_cast<int>(moved->position.x), static_cast<int>(moved->position.y)});
    simulation.issueAttack(blue);
    const int initialHealth = simulation.find(blue)->health;
    simulation.update(1.0F);
    simulation.update(1.0F);
    assert(simulation.find(blue) == nullptr || simulation.find(blue)->health < initialHealth);

    std::cout << "ra2yr core tests passed\n";
    return 0;
}
