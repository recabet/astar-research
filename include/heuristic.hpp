#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "graph.hpp"

namespace astar
{
    // A target-dependent admissible heuristic.  prepare() is called once per query;
    // estimate() is then called for every vertex that enters the open list.  The
    // interface makes the cost of the heuristic explicit in the search counters.
    class Heuristic {
    public:
        virtual ~Heuristic() = default;

        virtual void prepare(Vertex target) = 0;

        [[nodiscard]] virtual Distance estimate(Vertex vertex) const = 0;

        [[nodiscard]] virtual std::string name() const = 0;
    };

    class ZeroHeuristic final : public Heuristic {
    public:
        void prepare(Vertex) override
        {
        }

        [[nodiscard]] Distance estimate(Vertex) const override { return 0; }

        [[nodiscard]] std::string name() const override { return "zero"; }
    };

    // Manhattan distance on a 4-neighbour unit grid.  Vertex ids are row-major
    // cell ids; blocked cells are isolated and never expanded.
    class ManhattanHeuristic final : public Heuristic {
    public:
        explicit ManhattanHeuristic(std::uint32_t width) : width_(width)
        {
        }

        void prepare(Vertex target) override;

        [[nodiscard]] Distance estimate(Vertex vertex) const override;

        [[nodiscard]] std::string name() const override { return "manhattan"; }

    private:
        std::uint32_t width_;
        Vertex target_ = kInvalidVertex;
        std::int64_t target_row_ = 0;
        std::int64_t target_col_ = 0;
    };

    // ALT (A*, landmarks, triangle inequality) heuristic.  For an undirected graph
    // with unit or positive weights and exact distances d(L, v) from each landmark:
    //
    //   h(v, t) = max_L |d(L, v) - d(L, t)|.
    //
    // For every L, the triangle inequality gives
    //   |d(L,v) - d(L,t)| <= d(v,t),
    // so h is admissible.  For an edge (u,v) it also gives
    //   |h(u)-h(v)| <= w(u,v),
    // hence h is consistent.  Distances are precomputed before any timed search.
    struct LandmarkData {
        std::uint32_t n = 0;
        std::uint32_t k = 0;
        std::vector<Vertex> ids;
        // Row-major k x n table; row l holds d(landmark_l, v) for every v.
        std::vector<std::uint32_t> distances;

        [[nodiscard]] std::uint32_t get(std::uint32_t landmark, Vertex vertex) const
        {
            return distances[static_cast<std::size_t>(landmark) * n + vertex];
        }
    };

    LandmarkData select_landmarks(const Graph &graph, std::uint32_t k,
                                  std::uint64_t seed);

    void save_landmarks_binary(const LandmarkData &data, const std::string &path);

    LandmarkData load_landmarks_binary(const std::string &path);

    class ALTHeuristic final : public Heuristic {
    public:
        ALTHeuristic(const LandmarkData &data, std::uint32_t k)
            : data_(data), k_(k)
        {
            if (k_ > data_.k) k_ = data_.k;
            target_dist_.resize(k_, 0);
        }

        void prepare(Vertex target) override;

        [[nodiscard]] Distance estimate(Vertex vertex) const override;

        [[nodiscard]] std::string name() const override { return "alt_k" + std::to_string(k_); }

        [[nodiscard]] std::uint32_t landmarks() const { return k_; }

    private:
        const LandmarkData &data_;
        std::uint32_t k_;
        Vertex target_ = kInvalidVertex;
        std::vector<std::uint32_t> target_dist_;
    };
} // namespace astar
