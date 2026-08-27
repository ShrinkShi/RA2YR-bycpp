#include "Engine/Core/Utf.h"

#include <windows.h>

#include <limits>

namespace ra2yr {
namespace {

int checkedLength(std::size_t size) {
    return size > static_cast<std::size_t>(std::numeric_limits<int>::max()) ? 0 :
        static_cast<int>(size);
}

} // namespace

std::wstring utf8ToWide(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int inputLength = checkedLength(value.size());
    if (inputLength == 0) {
        return {};
    }
    const int outputLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value.data(), inputLength, nullptr, 0);
    if (outputLength <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(outputLength), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength,
        result.data(), outputLength) <= 0) {
        return {};
    }
    return result;
}

std::string wideToUtf8(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int inputLength = checkedLength(value.size());
    if (inputLength == 0) {
        return {};
    }
    const int outputLength = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.data(), inputLength, nullptr, 0, nullptr, nullptr);
    if (outputLength <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(outputLength), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), inputLength,
        result.data(), outputLength, nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

} // namespace ra2yr
