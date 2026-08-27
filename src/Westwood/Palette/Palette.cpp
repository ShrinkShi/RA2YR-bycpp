#include "Westwood/Palette/Palette.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace ra2yr::westwood {

ColorSchemeId colorSchemeForOwner(Owner owner) {
    switch (owner) {
    case Owner::Red: return ColorSchemeId::Red;
    case Owner::Blue: return ColorSchemeId::Blue;
    case Owner::Neutral: return ColorSchemeId::Neutral;
    }
    return ColorSchemeId::Neutral;
}

PaletteColor Palette::remappedColor(std::uint8_t index, Owner owner) const {
    return remappedColor(index, colorSchemeForOwner(owner));
}

PaletteColor Palette::remappedColor(std::uint8_t index, ColorSchemeId scheme) const {
    PaletteColor result = colors_[index];
    if (index < 16U || index > 31U) {
        return result;
    }
    return houseColorRemap(scheme)[static_cast<std::size_t>(index - 16U)];
}

std::array<PaletteColor, 16> Palette::houseColorRemap(ColorSchemeId scheme) const {
    const PaletteColor dark = scheme == ColorSchemeId::Red ? PaletteColor{48, 4, 3} :
        scheme == ColorSchemeId::Blue ? PaletteColor{4, 20, 58} : PaletteColor{32, 32, 32};
    const PaletteColor bright = scheme == ColorSchemeId::Red ? PaletteColor{255, 64, 30} :
        scheme == ColorSchemeId::Blue ? PaletteColor{70, 170, 255} : PaletteColor{210, 210, 210};
    std::array<PaletteColor, 16> result{};
    for (std::size_t index = 0; index < result.size(); ++index) {
        const float ratio = static_cast<float>(index) / 15.0F;
        result[index] = {
            static_cast<std::uint8_t>(std::clamp(dark.r + (bright.r - dark.r) * ratio, 0.0F, 255.0F)),
            static_cast<std::uint8_t>(std::clamp(dark.g + (bright.g - dark.g) * ratio, 0.0F, 255.0F)),
            static_cast<std::uint8_t>(std::clamp(dark.b + (bright.b - dark.b) * ratio, 0.0F, 255.0F)),
        };
    }
    return result;
}

bool Palette::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open PAL: " + path.string();
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() != 768) {
        error = "PAL must contain 768 bytes: " + path.string();
        return false;
    }
    for (std::size_t i = 0; i < colors_.size(); ++i) {
        // Westwood palettes store RGB channels in the 0..63 VGA range.
        colors_[i] = {
            static_cast<std::uint8_t>((std::min<std::uint8_t>(bytes[i * 3], 63U) * 255U) / 63U),
            static_cast<std::uint8_t>((std::min<std::uint8_t>(bytes[i * 3 + 1], 63U) * 255U) / 63U),
            static_cast<std::uint8_t>((std::min<std::uint8_t>(bytes[i * 3 + 2], 63U) * 255U) / 63U),
        };
    }
    return true;
}

} // namespace ra2yr::westwood
