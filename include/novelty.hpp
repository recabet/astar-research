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
    // Heuristic-informativeness diagnostics.  For each query we compute the true
    // distance from every sampled vertex to the target with BFS, compare it with
    // the heuristic, and record rank correlation, error and effective branching.
    int run_grid_diagnostics(const std::string &output,
                             const std::vector<std::uint32_t> &sizes,
                             const std::vector<GridFamily> &families,
                             std::uint32_t per_band = 5,
                             std::uint64_t seed = 20261030);

    int run_real_diagnostics(const std::string &output, const Graph &graph,
                             const LandmarkData &landmarks, std::uint32_t k = 8,
                             std::uint32_t per_band = 10,
                             std::uint64_t seed = 20261030);

    // Weighted A* on perfect mazes: f = g + epsilon * h.  A value epsilon > 1
    // trades optimality for speed; the result is checked against the exact
    // Dijkstra/BFS distance and reported as a cost ratio.
    int run_weighted_maze(const std::string &output,
                          const std::vector<std::uint32_t> &sizes,
                          std::uint32_t per_band = 5,
                          std::uint32_t reps = 3,
                          std::uint64_t seed = 20261030);

    // Weighted A* on every grid family.  Same output schema as run_weighted_maze;
    // on non-maze families the optimal path is not unique, so the cost ratio is a
    // genuine measure of the optimality price.
    int run_weighted_grid(const std::string &output,
                          const std::vector<std::uint32_t> &sizes,
                          const std::vector<GridFamily> &families,
                          std::uint32_t per_band = 5,
                          std::uint32_t reps = 3,
                          std::uint64_t seed = 20261030,
                          bool allow_reopen = true);

    // Weighted A* with ALT on the real road graph.  The Dijkstra distance is the
    // optimal cost; epsilon > 1 may return a longer path.
    int run_weighted_real(const std::string &output, const Graph &graph,
                          const LandmarkData &landmarks, std::uint32_t k = 8,
                          std::uint32_t per_band = 10,
                          std::uint32_t reps = 5,
                          std::uint64_t seed = 20261030,
                          bool allow_reopen = true);

    // Tie-break mechanism study: run A* with node-id and larger-g tie-breaks
    // and record the equal-f plateau structure of the settled vertices.
    int run_tie_plateau_grid(const std::string &output,
                             const std::vector<std::uint32_t> &sizes,
                             const std::vector<GridFamily> &families,
                             std::uint32_t per_band = 5,
                             std::uint64_t seed = 20261030);

    int run_tie_plateau_real(const std::string &output, const Graph &graph,
                             const LandmarkData &landmarks, std::uint32_t k = 8,
                             std::uint32_t per_band = 10,
                             std::uint64_t seed = 20261030);
} // namespace astar
