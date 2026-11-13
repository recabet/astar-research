#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "graph.hpp"
#include "grid.hpp"
#include "heuristic.hpp"

namespace astar
{
    struct Query {
        Vertex source = 0;
        Vertex target = 0;
        std::string band; // short, medium, long
        std::uint32_t oracle_distance = kInfiniteIndex;
    };

    struct BenchmarkOptions {
        std::string output;
        std::uint32_t reps = 3;
        std::uint32_t per_band = 5;
        std::uint64_t seed = 20261030;
        std::uint64_t time_limit_ns = 30ull * 1000ull * 1000ull * 1000ull;
    };

    // Selects connected source-target pairs from the supplied component.  For each
    // source, one target is chosen in each of three BFS-distance bands:
    // short [0.08,0.25], medium [0.40,0.60], long [0.80,0.97] of the eccentricity.
    // Selection is independent of Dijkstra/A* and uses only BFS.
    std::vector<Query> select_queries(const Graph &graph,
                                      const std::vector<Vertex> &component,
                                      std::uint32_t per_band, std::uint64_t seed);

    // Same band design, but bands are fractions of the weighted eccentricity and
    // oracle_distance is the weighted shortest-path distance.
    std::vector<Query> select_weighted_queries(const Graph &graph,
                                               const std::vector<Vertex> &component,
                                               std::uint32_t per_band,
                                               std::uint64_t seed);

    int run_grid_benchmark(const BenchmarkOptions &options,
                           const std::vector<std::uint32_t> &sizes,
                           const std::vector<GridFamily> &families,
                           double random_density = 0.20,
                           double clustered_density = 0.30);

    int run_real_benchmark(const BenchmarkOptions &options, const Graph &graph,
                           const LandmarkData &landmarks,
                           std::uint32_t main_k = 8);

    // Weighted road benchmark: Dijkstra, Euclidean A*, and weighted ALT.
    int run_weighted_road_benchmark(const BenchmarkOptions &options,
                                    const Graph &graph,
                                    const LandmarkData &landmarks,
                                    const Coordinates &coordinates,
                                    std::uint32_t main_k = 8);

    int run_validation(const Graph &graph, const LandmarkData* landmarks,
                       std::uint32_t main_k, std::uint32_t sample_queries,
                       std::uint64_t seed);
} // namespace astar
