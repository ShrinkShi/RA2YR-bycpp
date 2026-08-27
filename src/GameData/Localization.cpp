#include "GameData/Localization.h"

#include <cctype>
#include <fstream>
#include <iterator>

namespace ra2yr::gamedata {
namespace {

class JsonCursor {
public:
    explicit JsonCursor(std::string_view source) : source_(source) {}

    void skipWhitespace() {
        while (position_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[position_]))) {
            ++position_;
        }
    }

    bool consume(char expected) {
        skipWhitespace();
        if (position_ >= source_.size() || source_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    bool string(std::string& output) {
        skipWhitespace();
        if (position_ >= source_.size() || source_[position_] != '"') {
            return false;
        }
        ++position_;
        output.clear();
        while (position_ < source_.size()) {
            const char current = source_[position_++];
            if (current == '"') {
                return true;
            }
            if (current != '\\' || position_ >= source_.size()) {
                output.push_back(current);
                continue;
            }
            const char escaped = source_[position_++];
            switch (escaped) {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            default:
                // Locale files are UTF-8 and keep non-ASCII text literal.
                // Reject unsupported escapes instead of silently corrupting a
                // translation.
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] bool atEnd() const {
        return position_ >= source_.size();
    }

private:
    std::string_view source_;
    std::size_t position_ = 0;
};

} // namespace

bool LocalizationDatabase::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "Unable to open locale JSON: " + path.string();
        return false;
    }
    const std::string source((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    JsonCursor cursor(source);
    if (!cursor.consume('{')) {
        error = "Locale JSON must start with an object: " + path.string();
        return false;
    }

    std::unordered_map<std::string, std::string> parsed;
    cursor.skipWhitespace();
    if (!cursor.consume('}')) {
        while (true) {
            std::string key;
            std::string value;
            if (!cursor.string(key) || !cursor.consume(':') || !cursor.string(value)) {
                error = "Invalid locale JSON key/value in: " + path.string();
                return false;
            }
            parsed[std::move(key)] = std::move(value);
            cursor.skipWhitespace();
            if (cursor.consume('}')) {
                break;
            }
            if (!cursor.consume(',')) {
                error = "Invalid locale JSON separator in: " + path.string();
                return false;
            }
        }
    }
    cursor.skipWhitespace();
    if (!cursor.atEnd()) {
        error = "Trailing data in locale JSON: " + path.string();
        return false;
    }
    entries_ = std::move(parsed);
    loaded_ = true;
    return true;
}

std::string LocalizationDatabase::get(std::string_view key, std::string_view fallback) const {
    const auto it = entries_.find(std::string(key));
    return it == entries_.end() ? std::string(fallback) : it->second;
}

bool LocalizationDatabase::has(std::string_view key) const {
    return entries_.contains(std::string(key));
}

} // namespace ra2yr::gamedata
