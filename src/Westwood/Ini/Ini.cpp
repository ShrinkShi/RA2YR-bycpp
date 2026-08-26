#include "Westwood/Ini/Ini.h"

#include <charconv>
#include <fstream>
#include <sstream>

namespace ra2yr::westwood {
namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string stripComment(std::string value) {
    const auto comment = value.find(';');
    if (comment != std::string::npos) {
        value.resize(comment);
    }
    return trim(value);
}

} // namespace

bool IniDocument::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open INI: " + path.string();
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return loadText(contents.str(), error);
}

bool IniDocument::loadText(std::string_view text, std::string& error) {
    sections_.clear();
    std::string currentSection;
    std::istringstream stream{std::string(text)};
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line.front() == ';' || line.front() == '#' || line.rfind("//", 0) == 0) {
            continue;
        }
        if (line.front() == '[') {
            const auto closingBracket = line.find(']');
            if (closingBracket != std::string::npos) {
                const std::string suffix = trim(line.substr(closingBracket + 1));
                if (suffix.empty() || suffix.front() == ';' || suffix.front() == '#' || suffix.rfind("//", 0) == 0) {
                    currentSection = trim(line.substr(1, closingBracket - 1));
                    sections_[currentSection];
                    continue;
                }
            }
        }
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            // Westwood list sections also contain bare entries such as 842-GAWETH_ED.
            continue;
        }
        if (currentSection.empty()) {
            error = "INI key before section at line " + std::to_string(lineNumber);
            return false;
        }
        const std::string key = trim(line.substr(0, separator));
        const std::string value = stripComment(line.substr(separator + 1));
        if (!key.empty()) {
            sections_[currentSection][key] = value;
        }
    }
    return true;
}

std::string IniDocument::get(std::string_view section, std::string_view key, std::string_view fallback) const {
    const auto sectionIt = sections_.find(std::string(section));
    if (sectionIt == sections_.end()) {
        return std::string(fallback);
    }
    const auto valueIt = sectionIt->second.find(std::string(key));
    return valueIt == sectionIt->second.end() ? std::string(fallback) : valueIt->second;
}

int IniDocument::getInt(std::string_view section, std::string_view key, int fallback) const {
    const std::string value = get(section, key);
    int result = fallback;
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    return ec == std::errc{} && ptr == value.data() + value.size() ? result : fallback;
}

bool IniDocument::getBool(std::string_view section, std::string_view key, bool fallback) const {
    const std::string value = get(section, key);
    if (value == "yes" || value == "true" || value == "1") {
        return true;
    }
    if (value == "no" || value == "false" || value == "0") {
        return false;
    }
    return fallback;
}

bool IniDocument::hasSection(std::string_view section) const {
    return sections_.contains(std::string(section));
}

} // namespace ra2yr::westwood
