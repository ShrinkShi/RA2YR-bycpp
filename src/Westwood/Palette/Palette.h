#pragma once

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

class Palette {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    [[nodiscard]] PaletteColor color(std::uint8_t index) const { return colors_[index]; }

private:
    std::array<PaletteColor, 256> colors_{};
};

} // namespace ra2yr::westwood
