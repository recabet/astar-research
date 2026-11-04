#pragma once

#include <cstdint>
#include <string>

namespace astar
{
    struct PreprocessOptions {
        std::string input;
        std::string output_graph;
        std::string output_landmarks;
        std::uint32_t landmarks = 16;
        std::uint64_t seed = 20261030;
    };

    int preprocess_real_graph(const PreprocessOptions &options);
} // namespace astar
