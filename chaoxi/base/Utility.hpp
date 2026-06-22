#pragma once

#include <cstddef>
#include <string_view>

namespace chaoxi::base
{
struct StringHash
{
    using is_transparent = void;

    std::size_t operator()(std::string_view sv) const
    {
        return std::hash<std::string_view>{}(sv);
    }
};

};  // namespace chaoxi::base