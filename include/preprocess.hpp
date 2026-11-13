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

    struct DimacsPreprocessOptions {
        std::string input_gr;
        std::string input_co;
        std::string output_graph;
        std::string output_landmarks;
        std::string output_coords;
        std::uint32_t landmarks = 16;
        std::uint64_t seed = 20261030;
    };

    int preprocess_real_graph(const PreprocessOptions &options);

    // Reads a DIMACS .gr/.co pair, keeps edge weights, symmetrises the graph,
    // compacts to its largest connected component, and writes the graph,
    // weighted ALT landmarks, and projected coordinates.
    int preprocess_dimacs_graph(const DimacsPreprocessOptions &options);
} // namespace astar
