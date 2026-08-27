#pragma once

#include "Engine/Core/Types.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ra2yr::westwood {

struct PaletteColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

enum class ColorSchemeId : std::uint8_t {
    Neutral,
    Red,
    Blue,
};

[[nodiscard]] ColorSchemeId colorSchemeForOwner(Owner owner);

class Palette {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] PaletteColor color(std::uint8_t index) const { return colors_[index]; }
    [[nodiscard]] PaletteColor remappedColor(std::uint8_t index, Owner owner) const;
    [[nodiscard]] PaletteColor remappedColor(std::uint8_t index, ColorSchemeId scheme) const;
    [[nodiscard]] std::array<PaletteColor, 16> houseColorRemap(ColorSchemeId scheme) const;

private:
    std::array<PaletteColor, 256> colors_{};
};

} // namespace ra2yr::westwood
