#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "binary_heap.hpp"
#include "common.hpp"
#include "graph.hpp"
#include "heuristic.hpp"

namespace astar
{
    enum class SearchStatus : std::uint8_t { Ok = 0, NoPath = 1, Timeout = 2 };

    const char* status_name(SearchStatus status);

    struct SearchCounters {
        // A vertex is expanded when it is popped, is not stale, is not already
        // closed, and is therefore settled.  The target counts when it is popped.
        std::uint64_t expanded = 0;
        // Heap insertions, including the source and every successful decrease.
        std::uint64_t pushed = 0;
        // All heap removals, including removals of stale entries.
        std::uint64_t popped = 0;
        // Popped entries whose g is not the current tentative distance or whose
        // vertex is already closed.
        std::uint64_t stale_popped = 0;
        // Outgoing arcs inspected from expanded vertices (including arcs to closed
        // vertices, which are inspected but not relaxed further).
        std::uint64_t relaxed_edges = 0;
        // Successful strict distance decreases after the source initialisation.
        std::uint64_t improved = 0;
        // Calls to Heuristic::estimate (zero for Dijkstra).
        std::uint64_t heuristic_calls = 0;
        // Maximum number of entries held by the heap at any time.
        std::uint64_t heap_peak = 0;
    };

    struct SearchOptions {
        TieBreak tie = TieBreak::LargerG;
        bool build_path = false;
        // 0 disables the cap.  The check is every 4096 popped entries.
        std::uint64_t time_limit_ns = 0;
        // Weighted A*: f = g + heuristic_weight * h.  The default is ordinary A*.
        double heuristic_weight = 1.0;
        // Allow a closed vertex to be reopened if a strictly better g is found.
        // Needed when the heuristic is weighted (and therefore not consistent).
        bool allow_reopen = false;
    };

    struct SearchResult {
        SearchStatus status = SearchStatus::Ok;
        Distance distance = kInfinity;
        std::vector<Vertex> path;
        SearchCounters counters;
    };

    // Reusable storage.  reset() is O(1): it advances a stamp instead of clearing
    // O(n) arrays.  Create one scratch object per algorithm/tie-break so that heap
    // capacity cannot leak from one treatment into another.
    class SearchScratch {
    public:
        SearchScratch() = default;

        explicit SearchScratch(std::size_t n, TieBreak tie = TieBreak::LargerG);

        void resize(std::size_t n);

        void reset();

        [[nodiscard]] std::size_t size() const noexcept { return n_; }

        [[nodiscard]] TieBreak tie_break() const noexcept { return heap_.tie_break(); }

    private:
        friend SearchResult shortest_path(const Graph &, Vertex, Vertex, Heuristic*,
                                          const SearchOptions &, SearchScratch &);

        std::size_t n_ = 0;
        std::uint32_t stamp_ = 0;
        std::vector<Distance> dist_;
        std::vector<std::uint32_t> seen_;
        std::vector<std::uint32_t> closed_;
        std::vector<Distance> closed_distance_;
        std::vector<Vertex> parent_;
        BinaryHeap heap_;
    };

    // Shared implementation of Dijkstra and A*.  A null heuristic means Dijkstra.
    // Dijkstra is therefore exactly the same loop and heap as A*, with h(v) = 0.
    SearchResult shortest_path(const Graph &graph, Vertex source, Vertex target,
                               Heuristic* heuristic, const SearchOptions &options,
                               SearchScratch &scratch);

    SearchResult dijkstra(const Graph &graph, Vertex source, Vertex target,
                          const SearchOptions &options, SearchScratch &scratch);

    SearchResult astar_search(const Graph &graph, Vertex source, Vertex target,
                              Heuristic &heuristic, const SearchOptions &options,
                              SearchScratch &scratch);
} // namespace astar
