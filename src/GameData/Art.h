#pragma once

#include "Westwood/Ini/Ini.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ra2yr::gamedata {

struct AnimationSequence {
    int firstFrame = 0;
    int frameCount = 1;
    // The third value in a Westwood infantry sequence is the per-facing frame stride.
    int facingStride = 0;
    int frameDelayMs = 80;
    bool loop = true;
};

struct ArtDefinition {
    std::string image;
    std::string sequence;
    int facingCount = 8;
    bool remapable = false;
    std::unordered_map<std::string, AnimationSequence> sequences;
};

class ArtDatabase {
public:
    bool load(const std::filesystem::path& artPath, std::string& error);

    [[nodiscard]] const ArtDefinition* find(std::string_view image) const;
    [[nodiscard]] int frameIndex(std::string_view image, std::string_view sequence,
        int animationIndex = 0, int facingIndex = 0) const;
    [[nodiscard]] int sequenceFrameCount(std::string_view image, std::string_view sequence) const;
    [[nodiscard]] int sequenceFrameDelayMs(std::string_view image, std::string_view sequence) const;
    [[nodiscard]] bool sequenceLoops(std::string_view image, std::string_view sequence) const;
    [[nodiscard]] bool sequenceIsDirectional(std::string_view image, std::string_view sequence) const;
    [[nodiscard]] int facingCount(std::string_view image) const;
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    std::unordered_map<std::string, ArtDefinition> definitions_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
