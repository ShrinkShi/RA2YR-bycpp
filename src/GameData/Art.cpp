#include "GameData/Art.h"

#include <charconv>
#include <sstream>
#include <utility>

namespace ra2yr::gamedata {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

bool parseInt(std::string value, int& result) {
    value = trim(std::move(value));
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    return ec == std::errc{} && ptr == value.data() + value.size();
}

bool parseSequence(std::string_view value, AnimationSequence& sequence) {
    std::stringstream stream{std::string(value)};
    std::string token;
    std::string fields[4];
    int fieldCount = 0;
    while (std::getline(stream, token, ',') && fieldCount < 4) {
        fields[fieldCount++] = trim(token);
    }
    if (fieldCount < 3 || !parseInt(fields[0], sequence.firstFrame) ||
        !parseInt(fields[1], sequence.frameCount) || !parseInt(fields[2], sequence.facingCount) ||
        sequence.firstFrame < 0 || sequence.frameCount <= 0 || sequence.facingCount < 0) {
        return false;
    }
    if (fieldCount == 4) {
        sequence.facing = fields[3];
    }
    return true;
}

} // namespace

bool ArtDatabase::load(const std::filesystem::path& artPath, std::string& error) {
    westwood::IniDocument art;
    if (!art.load(artPath, error)) {
        return false;
    }
    if (!art.hasSection("CONS") || !art.hasSection("ConSequence")) {
        error = "Enhanced Art.ini must define CONS and ConSequence";
        return false;
    }

    ArtDefinition cons;
    cons.image = art.get("CONS", "Image", "CONS");
    cons.sequence = art.get("CONS", "Sequence", "ConSequence");
    cons.remapable = art.getBool("CONS", "Remapable", false);
    const std::string sequenceKeys[] = {
        "Ready", "Guard", "Walk", "Fire", "FireUp", "Death", "Die1", "Die2", "Prone", "Down", "Up"};
    for (const std::string& key : sequenceKeys) {
        const std::string value = art.get(cons.sequence, key);
        if (value.empty()) {
            continue;
        }
        AnimationSequence sequence;
        if (!parseSequence(value, sequence)) {
            error = "Invalid animation sequence " + cons.sequence + "." + key;
            return false;
        }
        cons.sequences.emplace(key, sequence);
    }
    for (const std::string& required : {"Ready", "Walk", "Fire", "Death"}) {
        if (!cons.sequences.contains(required)) {
            error = "Enhanced Art.ini is missing CONS sequence " + required;
            return false;
        }
    }
    definitions_.clear();
    definitions_.emplace(cons.image, std::move(cons));
    loaded_ = true;
    return true;
}

const ArtDefinition* ArtDatabase::find(std::string_view image) const {
    const auto it = definitions_.find(std::string(image));
    return it == definitions_.end() ? nullptr : &it->second;
}

int ArtDatabase::frameIndex(std::string_view image, std::string_view sequence, int animationIndex) const {
    const ArtDefinition* definition = find(image);
    if (definition == nullptr) {
        return 0;
    }
    const auto it = definition->sequences.find(std::string(sequence));
    if (it == definition->sequences.end()) {
        return 0;
    }
    const AnimationSequence& value = it->second;
    const int offset = animationIndex < 0 ? 0 : animationIndex % value.frameCount;
    return value.firstFrame + offset;
}

} // namespace ra2yr::gamedata
