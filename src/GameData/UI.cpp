#include "GameData/UI.h"

#include "Westwood/Ini/Ini.h"

#include <array>
#include <cstdlib>
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

bool parseFloat(std::string value, float& result) {
    value = trim(std::move(value));
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || *end != '\0') {
        return false;
    }
    result = parsed;
    return true;
}

bool parseRect(const std::string& value, Rect& result) {
    std::stringstream stream(value);
    std::string token;
    std::array<float, 4> values{};
    int index = 0;
    while (std::getline(stream, token, ',') && index < static_cast<int>(values.size())) {
        if (!parseFloat(token, values[static_cast<std::size_t>(index)])) {
            return false;
        }
        ++index;
    }
    if (index != static_cast<int>(values.size())) {
        return false;
    }
    result = {values[0], values[1], values[2], values[3]};
    return result.width >= 0.0F && result.height >= 0.0F;
}

} // namespace

bool UiLayoutDatabase::load(const std::filesystem::path& path, std::string& error) {
    westwood::IniDocument document;
    if (!document.load(path, error)) {
        return false;
    }
    if (!document.hasSection("Theme") || !document.hasSection("Images") ||
        !document.hasSection("Rects") || !document.hasSection("RelativeRects")) {
        error = "UI.ini must define Theme, Images, Rects and RelativeRects sections";
        return false;
    }

    UiThemeDefinition parsed;
    parsed.skin.name = document.get("Theme", "Name");
    if (parsed.skin.name.empty()) {
        error = "UI.ini Theme.Name is required";
        return false;
    }
    for (const auto& [key, value] : document.entries("Images")) {
        if (value.empty()) {
            error = "UI.ini image path is empty for " + key;
            return false;
        }
        parsed.skin.imagePaths.emplace(key, value);
    }

    std::unordered_map<std::string, Rect> parsedRects;
    for (const auto& [key, value] : document.entries("Rects")) {
        Rect rectValue;
        if (!parseRect(value, rectValue)) {
            error = "Invalid UI.ini rect in [Rects] " + key;
            return false;
        }
        parsedRects.emplace(key, rectValue);
    }
    std::unordered_map<std::string, Rect> parsedRelativeRects;
    for (const auto& [key, value] : document.entries("RelativeRects")) {
        Rect rectValue;
        if (!parseRect(value, rectValue)) {
            error = "Invalid UI.ini rect in [RelativeRects] " + key;
            return false;
        }
        parsedRelativeRects.emplace(key, rectValue);
    }
    if (!parsedRects.contains("world.viewport") || !parsedRects.contains("hud.command_card") ||
        !parsedRelativeRects.contains("hud.command_card.slot.0") ||
        !parsedRelativeRects.contains("sandbox.title_bar")) {
        error = "UI.ini is missing required world, command card or sandbox layout entries";
        return false;
    }

    theme_ = std::move(parsed);
    rects_ = std::move(parsedRects);
    relativeRects_ = std::move(parsedRelativeRects);
    loaded_ = true;
    return true;
}

bool UiLayoutDatabase::hasRect(std::string_view key) const {
    return rects_.contains(std::string(key));
}

Rect UiLayoutDatabase::rect(std::string_view key) const {
    const auto it = rects_.find(std::string(key));
    return it == rects_.end() ? Rect{} : it->second;
}

Rect UiLayoutDatabase::relativeRect(std::string_view key) const {
    const auto it = relativeRects_.find(std::string(key));
    return it == relativeRects_.end() ? Rect{} : it->second;
}

Rect UiLayoutDatabase::childRect(std::string_view parent, std::string_view child) const {
    const Rect parentRect = rect(parent);
    const Rect childRectValue = relativeRect(child);
    return {parentRect.x + childRectValue.x, parentRect.y + childRectValue.y,
        childRectValue.width, childRectValue.height};
}

bool UiLayoutDatabase::hasImage(std::string_view key) const {
    return theme_.skin.imagePaths.contains(std::string(key));
}

std::filesystem::path UiLayoutDatabase::imagePath(std::string_view key,
    const std::filesystem::path& contentRoot) const {
    const auto it = theme_.skin.imagePaths.find(std::string(key));
    if (it == theme_.skin.imagePaths.end()) {
        return {};
    }
    const std::filesystem::path path = it->second;
    return path.is_absolute() ? path : contentRoot / path;
}

} // namespace ra2yr::gamedata
