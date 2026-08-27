#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ra2yr::gamedata {

// The locale files intentionally use a flat JSON object.  This keeps the
// runtime dependency-free while still letting mods replace every UIName and
// visible UI label without changing Rules.ini or C++.
class LocalizationDatabase {
public:
    bool load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] std::string get(std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] bool loaded() const { return loaded_; }

private:
    std::unordered_map<std::string, std::string> entries_;
    bool loaded_ = false;
};

} // namespace ra2yr::gamedata
