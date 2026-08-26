#include "Westwood/Palette/Palette.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <vector>

namespace ra2yr::westwood {

PaletteColor Palette::remappedColor(std::uint8_t index, Owner owner) const {
    PaletteColor result = colors_[index];
    if (index < 0xc0U || index > 0xcfU) {
        return result;
    }
    const float shade = 0.60F + static_cast<float>(index - 0xc0U) / 32.0F;
    const PaletteColor ownerColor = owner == Owner::Red ? PaletteColor{255, 31, 20} :
        owner == Owner::Blue ? PaletteColor{46, 140, 255} : PaletteColor{217, 217, 217};
    result.r = static_cast<std::uint8_t>(std::clamp(ownerColor.r * shade, 0.0F, 255.0F));
    result.g = static_cast<std::uint8_t>(std::clamp(ownerColor.g * shade, 0.0F, 255.0F));
    result.b = static_cast<std::uint8_t>(std::clamp(ownerColor.b * shade, 0.0F, 255.0F));
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
