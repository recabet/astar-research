#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "graph.hpp"
#include "heuristic.hpp"

namespace astar
{
    // Batch-throughput study: independent queries run concurrently with OpenMP.
    // Each task still runs Dijkstra and A* sequentially on the same query, so the
    // paired comparison is preserved.  The main frozen experiment remains serial.
    int run_omp_scaling(const std::string &batch_output,
                        const std::string &task_output,
                        const Graph &real_graph,
                        const LandmarkData &landmarks,
                        const std::vector<std::uint32_t> &grid_sizes,
                        const std::vector<int> &thread_counts,
                        std::uint32_t per_band = 5,
                        std::uint32_t reps = 3,
                        std::uint32_t batch_reps = 3,
                        std::uint64_t seed = 20261030);
} // namespace astar
