#pragma once

#include "Westwood/Ini/Ini.h"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace ra2yr::gamedata {

struct AnimationSequence {
    int firstFrame = 0;
    int frameCount = 1;
    int facingCount = 1;
    std::string facing;
};

struct ArtDefinition {
    std::string image;
    std::string sequence;
    bool remapable = false;
    std::unordered_map<std::string, AnimationSequence> sequences;
};

class ArtDatabase {
public:
    bool load(const std::filesystem::path& artPath, std::string& error);

    [[nodiscard]] const ArtDefinition* find(std::string_view image) const;
    [[nodiscard]] int frameIndex(std::string_view image, std::string_view sequence,
        int animationIndex = 0) const;
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    std::unordered_map<std::string, ArtDefinition> definitions_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
