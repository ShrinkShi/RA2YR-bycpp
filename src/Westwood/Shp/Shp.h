#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ra2yr::westwood {

struct ShpFrame {
    std::uint16_t x = 0;
    std::uint16_t y = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t fullWidth = 0;
    std::uint16_t fullHeight = 0;
    std::vector<std::uint8_t> pixels;
};

class ShpTsDocument {
public:
    bool load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] std::uint16_t width() const { return width_; }
    [[nodiscard]] std::uint16_t height() const { return height_; }
    [[nodiscard]] std::size_t frameCount() const { return frames_.size(); }
    [[nodiscard]] const ShpFrame& frame(std::size_t index) const { return frames_.at(index % frames_.size()); }

private:
    std::uint16_t width_ = 0;
    std::uint16_t height_ = 0;
    std::vector<ShpFrame> frames_;
};

} // namespace ra2yr::westwood
