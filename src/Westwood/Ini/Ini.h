#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ra2yr::westwood {

class IniDocument {
public:
    bool load(const std::filesystem::path& path, std::string& error);
    bool loadText(std::string_view text, std::string& error);

    [[nodiscard]] std::string get(std::string_view section, std::string_view key, std::string_view fallback = {}) const;
    [[nodiscard]] int getInt(std::string_view section, std::string_view key, int fallback = 0) const;
    [[nodiscard]] bool getBool(std::string_view section, std::string_view key, bool fallback = false) const;
    [[nodiscard]] bool hasSection(std::string_view section) const;
    [[nodiscard]] bool hasKey(std::string_view section, std::string_view key) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> entries(std::string_view section) const;

private:
    using Section = std::unordered_map<std::string, std::string>;
    std::unordered_map<std::string, Section> sections_;
};

} // namespace ra2yr::westwood
