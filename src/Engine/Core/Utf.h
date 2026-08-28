#pragma once

#include <string>
#include <string_view>

namespace ra2yr {

[[nodiscard]] std::wstring utf8ToWide(std::string_view value);
[[nodiscard]] std::string wideToUtf8(std::wstring_view value);

} // namespace ra2yr
