#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace astar
{
    using Vertex = std::uint32_t;
    using Weight = std::uint32_t;
    using Distance = std::int64_t;

    inline constexpr Vertex kInvalidVertex = std::numeric_limits<Vertex>::max();
    inline constexpr Weight kInfiniteWeight = std::numeric_limits<Weight>::max();
    inline constexpr Distance kInfinity = std::numeric_limits<Distance>::max() / 4;
    inline constexpr std::uint32_t kInfiniteIndex = std::numeric_limits<std::uint32_t>::max();

    // A stable, portable 64-bit hash used to derive independent seeds for graph
    // generation and for query selection.  It is deliberately not std::hash,
    // whose implementation may vary between standard libraries.
    inline std::uint64_t fnv1a64(const std::string &text)
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const unsigned char c: text)
        {
            h ^= c;
            h *= 1099511628211ull;
        }
        return h;
    }
} // namespace astar
