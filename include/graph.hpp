#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"

namespace astar
{
    // Compressed sparse row (CSR) representation.  For a graph with n vertices the
    // outgoing arcs of vertex u are targets[offsets[u] .. offsets[u+1]), with the
    // corresponding weights in the parallel array.  Memory is O(n + m).
    struct Graph {
        std::uint32_t n = 0;
        std::vector<std::uint64_t> offsets;
        std::vector<Vertex> targets;
        std::vector<Weight> weights;

        Graph() = default;

        Graph(std::uint32_t n_, std::vector<std::uint64_t> offsets_,
              std::vector<Vertex> targets_, std::vector<Weight> weights_)
            : n(n_), offsets(std::move(offsets_)), targets(std::move(targets_)),
              weights(std::move(weights_))
        {
        }

        [[nodiscard]] std::size_t num_vertices() const noexcept { return n; }

        [[nodiscard]] std::size_t num_arcs() const noexcept { return targets.size(); }

        [[nodiscard]] std::uint64_t degree(Vertex u) const noexcept
        {
            return offsets[u + 1] - offsets[u];
        }

        [[nodiscard]] bool empty() const noexcept { return n == 0; }

        bool validate(std::string* error = nullptr) const;
    };

    // Builds a CSR graph from an edge list.  If undirected is true, each add_edge
    // inserts both directions.  build(true) removes duplicate (u,v) pairs and keeps
    // the minimum weight among them; build(false) preserves parallel arcs.
    class GraphBuilder {
    public:
        explicit GraphBuilder(std::uint32_t n, bool undirected = false)
            : n_(n), undirected_(undirected)
        {
        }

        void add_edge(Vertex u, Vertex v, Weight w = 1);

        Graph build(bool deduplicate = true);

        [[nodiscard]] std::uint32_t num_vertices() const noexcept { return n_; }

        [[nodiscard]] std::size_t num_input_arcs() const noexcept { return edges_.size(); }

    private:
        struct Record {
            Vertex u;
            Vertex v;
            Weight w;
        };

        std::uint32_t n_;
        bool undirected_;
        std::vector<Record> edges_;
    };

    void save_graph_binary(const Graph &graph, const std::string &path);

    Graph load_graph_binary(const std::string &path);
} // namespace astar
