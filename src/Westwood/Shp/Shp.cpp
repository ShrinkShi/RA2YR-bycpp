#include "Westwood/Shp/Shp.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

namespace ra2yr::westwood {
namespace {

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(bytes.at(offset) | (static_cast<std::uint16_t>(bytes.at(offset + 1)) << 8U));
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes.at(offset)) |
        (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 8U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 16U) |
        (static_cast<std::uint32_t>(bytes.at(offset + 3)) << 24U);
}

void decodeRleZeros(const std::vector<std::uint8_t>& source, std::size_t begin, std::size_t end,
    std::vector<std::uint8_t>& destination, std::size_t destinationOffset) {
    std::size_t sourceOffset = begin;
    std::size_t destinationIndex = destinationOffset;
    while (sourceOffset < end && destinationIndex < destination.size()) {
        const std::uint8_t command = source[sourceOffset++];
        if (command == 0) {
            if (sourceOffset >= end) {
                return;
            }
            const std::size_t count = source[sourceOffset++];
            destinationIndex = std::min(destinationIndex + count, destination.size());
        } else {
            destination[destinationIndex++] = command;
        }
    }
}

} // namespace

bool ShpTsDocument::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open SHP: " + path.string();
        return false;
    }
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 8 || readU16(bytes, 0) != 0) {
        error = "Not a TS/RA2 SHP file: " + path.string();
        return false;
    }
    width_ = readU16(bytes, 2);
    height_ = readU16(bytes, 4);
    const std::uint16_t frameCount = readU16(bytes, 6);
    if (width_ == 0 || height_ == 0 || frameCount == 0 || 8ULL + 24ULL * frameCount > bytes.size()) {
        error = "Invalid SHP header: " + path.string();
        return false;
    }

    frames_.clear();
    frames_.reserve(frameCount);
    for (std::size_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
        const std::size_t header = 8 + frameIndex * 24;
        ShpFrame frame;
        frame.x = readU16(bytes, header);
        frame.y = readU16(bytes, header + 2);
        frame.width = readU16(bytes, header + 4);
        frame.height = readU16(bytes, header + 6);
        const std::uint8_t format = bytes.at(header + 8);
        const std::uint32_t fileOffset = readU32(bytes, header + 20);
        const std::size_t dataWidth = frame.width + (frame.width % 2U);
        const std::size_t dataHeight = frame.height + (frame.height % 2U);
        frame.pixels.assign(dataWidth * dataHeight, 0);
        if (fileOffset != 0 && frame.width != 0 && frame.height != 0 && fileOffset < bytes.size()) {
            std::size_t source = fileOffset;
            for (std::size_t row = 0; row < frame.height && source < bytes.size(); ++row) {
                if (format == 3) {
                    if (source + 2 > bytes.size()) {
                        error = "Truncated SHP scanline header: " + path.string();
                        return false;
                    }
                    const std::size_t rowEnd = source + readU16(bytes, source);
                    source += 2;
                    if (rowEnd > bytes.size() || rowEnd < source) {
                        error = "Invalid SHP scanline range: " + path.string();
                        return false;
                    }
                    decodeRleZeros(bytes, source, rowEnd, frame.pixels, row * dataWidth);
                    source = rowEnd;
                } else {
                    const std::size_t rowLength = format == 2 ? readU16(bytes, source) - 2U : frame.width;
                    if (format == 2) {
                        source += 2;
                    }
                    if (source + rowLength > bytes.size() || rowLength > dataWidth) {
                        error = "Invalid SHP scanline length: " + path.string();
                        return false;
                    }
                    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(source), rowLength,
                        frame.pixels.begin() + static_cast<std::ptrdiff_t>(row * dataWidth));
                    source += rowLength;
                }
            }
        }
        frames_.push_back(std::move(frame));
    }
    return true;
}

} // namespace ra2yr::westwood
