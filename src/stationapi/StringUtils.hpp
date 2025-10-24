#pragma once

#include <string>
#include <boost/optional.hpp>

std::string FromWideString(const std::u16string& str);
std::u16string ToWideString(const std::string& str);
boost::optional<std::u16string> NullableUtf8ToWide(const unsigned char* buffer);

