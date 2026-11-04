#include "graph.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace astar
{
    bool Graph::validate(std::string* error) const
    {
        auto fail = [&](const std::string &message) {
            if (error != nullptr) *error = message;
            return false;
        };

        if (offsets.size() != static_cast<std::size_t>(n) + 1)
        {
            return fail("offsets.size() != n + 1");
        }
        if (offsets.front() != 0) return fail("offsets[0] != 0");
        if (offsets.back() != targets.size())
        {
            return fail("offsets[n] != number of arcs");
        }
        if (targets.size() != weights.size())
        {
            return fail("targets and weights have different sizes");
        }
        for (std::size_t i = 1; i < offsets.size(); ++i)
        {
            if (offsets[i] < offsets[i - 1]) return fail("offsets not monotone");
        }
        for (std::size_t i = 0; i < targets.size(); ++i)
        {
            if (targets[i] >= n) return fail("target vertex out of range");
            if (weights[i] == 0) return fail("non-positive edge weight");
        }
        return true;
    }

    void GraphBuilder::add_edge(Vertex u, Vertex v, Weight w)
    {
        assert(u < n_ && v < n_);
        assert(w > 0);
        if (u == v) return; // positive self-loops can never improve a shortest path
        edges_.push_back({u, v, w});
        if (undirected_) edges_.push_back({v, u, w});
    }

    Graph GraphBuilder::build(bool deduplicate)
    {
        std::ranges::sort(edges_.begin(), edges_.end(), [](const Record &a, const Record &b) {
            if (a.u != b.u) return a.u < b.u;
            if (a.v != b.v) return a.v < b.v;
            return a.w < b.w;
        });

        if (deduplicate)
        {
            std::size_t out = 0;
            for (std::size_t i = 0; i < edges_.size(); ++i)
            {
                if (i != 0 && edges_[i].u == edges_[i - 1].u &&
                    edges_[i].v == edges_[i - 1].v)
                {
                    continue; // the first record for a pair has the minimum weight
                }
                if (out != i) edges_[out] = edges_[i];
                ++out;
            }
            edges_.resize(out);
        }

        std::vector<std::uint64_t> offsets(static_cast<std::size_t>(n_) + 1, 0);
        for (const Record &e: edges_) ++offsets[static_cast<std::size_t>(e.u) + 1];
        for (std::size_t i = 1; i < offsets.size(); ++i) offsets[i] += offsets[i - 1];

        std::vector<Vertex> targets(edges_.size());
        std::vector<Weight> weights(edges_.size());
        std::vector<std::uint64_t> cursor = offsets;
        for (const Record &e: edges_)
        {
            const std::uint64_t pos = cursor[e.u]++;
            targets[pos] = e.v;
            weights[pos] = e.w;
        }

        return {n_, std::move(offsets), std::move(targets), std::move(weights)};
    }

    void save_graph_binary(const Graph &graph, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot open graph output: " + path);

        constexpr char magic[8] = {'A', 'S', 'T', 'A', 'R', 'G', 'R', '1'};
        out.write(magic, sizeof(magic));
        const std::uint32_t n = graph.n;
        const std::uint64_t m = graph.targets.size();
        out.write(reinterpret_cast<const char *>(&n), sizeof(n));
        out.write(reinterpret_cast<const char *>(&m), sizeof(m));
        out.write(reinterpret_cast<const char *>(graph.offsets.data()),
                  static_cast<std::streamsize>(graph.offsets.size() * sizeof(std::uint64_t)));
        out.write(reinterpret_cast<const char *>(graph.targets.data()),
                  static_cast<std::streamsize>(graph.targets.size() * sizeof(Vertex)));
        out.write(reinterpret_cast<const char *>(graph.weights.data()),
                  static_cast<std::streamsize>(graph.weights.size() * sizeof(Weight)));
        if (!out) throw std::runtime_error("failed while writing graph: " + path);
    }

    Graph load_graph_binary(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open graph input: " + path);

        char magic[8];
        in.read(magic, sizeof(magic));
        constexpr char expected[8] = {'A', 'S', 'T', 'A', 'R', 'G', 'R', '1'};
        if (!in || !std::equal(magic, magic + 8, expected))
        {
            throw std::runtime_error("bad graph file magic: " + path);
        }

        std::uint32_t n = 0;
        std::uint64_t m = 0;
        in.read(reinterpret_cast<char *>(&n), sizeof(n));
        in.read(reinterpret_cast<char *>(&m), sizeof(m));
        if (!in) throw std::runtime_error("truncated graph header: " + path);

        Graph graph;
        graph.n = n;
        graph.offsets.resize(static_cast<std::size_t>(n) + 1);
        graph.targets.resize(static_cast<std::size_t>(m));
        graph.weights.resize(static_cast<std::size_t>(m));
        in.read(reinterpret_cast<char *>(graph.offsets.data()),
                static_cast<std::streamsize>(graph.offsets.size() * sizeof(std::uint64_t)));
        in.read(reinterpret_cast<char *>(graph.targets.data()),
                static_cast<std::streamsize>(graph.targets.size() * sizeof(Vertex)));
        in.read(reinterpret_cast<char *>(graph.weights.data()),
                static_cast<std::streamsize>(graph.weights.size() * sizeof(Weight)));
        if (!in) throw std::runtime_error("truncated graph body: " + path);

        std::string error;
        if (!graph.validate(&error))
        {
            throw std::runtime_error("invalid graph file " + path + ": " + error);
        }
        return graph;
    }
} // namespace astar
