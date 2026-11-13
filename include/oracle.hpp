#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "graph.hpp"

namespace astar
{
    // Independent distance oracles used only for correctness checks.  None of these
    // functions is used inside the timed searches.
    //
    // bfs_distances: O(n + m).  Unweighted graphs only.
    // bfs_distance:  O(n + m), stops when t is reached.
    // floyd_warshall: O(n^3), used for hand-sized weighted graphs.
    // validate_path: O(path_length * max_degree).
    std::vector<std::uint32_t> bfs_distances(const Graph &graph, Vertex source);

    // Exact weighted distances from one source.  Positive integer weights.
    // Used for weighted ALTs, weighted query selection, and correctness checks.
    std::vector<Distance> dijkstra_distances(const Graph &graph, Vertex source);

    std::uint32_t bfs_distance(const Graph &graph, Vertex source, Vertex target);

    std::vector<std::vector<Distance> > floyd_warshall(const Graph &graph);

    bool validate_path(const Graph &graph, Vertex source, Vertex target,
                       const std::vector<Vertex> &path, Distance expected_distance,
                       std::string* error = nullptr);

    // Returns the vertices of the largest connected component of an undirected
    // graph.  For a graph that is not symmetric only the underlying undirected
    // graph is considered.  O(n + m).
    std::vector<Vertex> largest_connected_component(const Graph &graph);
} // namespace astar
