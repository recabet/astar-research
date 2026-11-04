#include "search.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>

namespace astar
{
    const char* status_name(SearchStatus status)
    {
        switch (status)
        {
            case SearchStatus::Ok: return "ok";
            case SearchStatus::NoPath: return "no_path";
            case SearchStatus::Timeout: return "timeout";
        }
        return "unknown";
    }

    SearchScratch::SearchScratch(std::size_t n, TieBreak tie)
        : n_(n), dist_(n), seen_(n, 0), closed_(n, 0), closed_distance_(n, 0),
          parent_(n, kInvalidVertex), heap_(tie, n)
    {
        reset();
    }

    void SearchScratch::resize(std::size_t n)
    {
        if (n == n_)
        {
            reset();
            return;
        }
        n_ = n;
        dist_.assign(n, 0);
        seen_.assign(n, 0);
        closed_.assign(n, 0);
        closed_distance_.assign(n, 0);
        parent_.assign(n, kInvalidVertex);
        heap_.clear();
        heap_.reserve(n);
        stamp_ = 0;
        reset();
    }

    void SearchScratch::reset()
    {
        ++stamp_;
        if (stamp_ == 0)
        {
            std::ranges::fill(seen_.begin(), seen_.end(), 0);
            std::ranges::fill(closed_.begin(), closed_.end(), 0);
            stamp_ = 1;
        }
        heap_.clear();
    }

    SearchResult shortest_path(const Graph &graph, Vertex source, Vertex target,
                               Heuristic* heuristic, const SearchOptions &options,
                               SearchScratch &scratch)
    {
        SearchResult result;
        if (source >= graph.n || target >= graph.n || graph.n == 0)
        {
            result.status = SearchStatus::NoPath;
            return result;
        }

        if (scratch.size() != graph.n) scratch.resize(graph.n);
        scratch.reset();

        const std::uint32_t stamp = scratch.stamp_;
        auto &dist = scratch.dist_;
        auto &seen = scratch.seen_;
        auto &closed = scratch.closed_;
        auto &closed_distance = scratch.closed_distance_;
        auto &parent = scratch.parent_;
        auto &heap = scratch.heap_;

        auto current_distance = [&](Vertex v) -> Distance {
            return seen[v] == stamp ? dist[v] : kInfinity;
        };
        auto set_distance = [&](Vertex v, Distance d) {
            seen[v] = stamp;
            dist[v] = d;
        };

        const auto start_time = std::chrono::steady_clock::now();
        if (heuristic != nullptr) heuristic->prepare(target);

        auto scaled_heuristic = [&](Vertex v) -> Distance {
            const Distance h = heuristic->estimate(v);
            ++result.counters.heuristic_calls;
            if (options.heuristic_weight == 1.0) return h;
            return static_cast<Distance>(std::llround(
                options.heuristic_weight * static_cast<double>(h)));
        };

        Distance h_source = 0;
        if (heuristic != nullptr) h_source = scaled_heuristic(source);
        set_distance(source, 0);
        parent[source] = kInvalidVertex;
        heap.push({h_source, 0, source});
        ++result.counters.pushed;
        result.counters.heap_peak = heap.size();

        bool found = false;
        while (!heap.empty())
        {
            const HeapEntry entry = heap.pop();
            ++result.counters.popped;
            const Vertex u = entry.node;

            if (entry.g != (seen[u] == stamp ? dist[u] : kInfinity))
            {
                ++result.counters.stale_popped;
                continue;
            }
            if (closed[u] == stamp)
            {
                if (!options.allow_reopen || entry.g >= closed_distance[u])
                {
                    ++result.counters.stale_popped;
                    continue;
                }
            }

            closed[u] = stamp;
            closed_distance[u] = entry.g;
            ++result.counters.expanded;

            if (u == target)
            {
                found = true;
                break;
            }

            const Distance gu = entry.g;
            const std::uint64_t begin = graph.offsets[u];
            const std::uint64_t end = graph.offsets[u + 1];
            for (std::uint64_t i = begin; i < end; ++i)
            {
                ++result.counters.relaxed_edges;
                const Vertex v = graph.targets[i];
                if (!options.allow_reopen && closed[v] == stamp) continue;

                const Distance candidate = gu + static_cast<Distance>(graph.weights[i]);
                if (candidate >= current_distance(v)) continue;

                set_distance(v, candidate);
                if (options.build_path) parent[v] = u;
                ++result.counters.improved;

                Distance f = candidate;
                if (heuristic != nullptr)
                {
                    const Distance h = scaled_heuristic(v);
                    f = (h >= kInfinity - candidate) ? kInfinity : candidate + h;
                }
                heap.push({f, candidate, v});
                ++result.counters.pushed;
                if (heap.size() > result.counters.heap_peak)
                {
                    result.counters.heap_peak = heap.size();
                }
            }

            if (options.time_limit_ns != 0 && (result.counters.popped & 4095u) == 0)
            {
                const auto now = std::chrono::steady_clock::now();
                const std::uint64_t elapsed = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - start_time).count());
                if (elapsed > options.time_limit_ns)
                {
                    result.status = SearchStatus::Timeout;
                    result.distance = kInfinity;
                    return result;
                }
            }
        }

        if (!found)
        {
            result.status = SearchStatus::NoPath;
            result.distance = kInfinity;
            return result;
        }

        result.status = SearchStatus::Ok;
        result.distance = dist[target];

        if (options.build_path)
        {
            std::vector<Vertex> reverse_path;
            Vertex current = target;
            std::size_t guard = 0;
            while (current != kInvalidVertex && guard++ <= graph.n)
            {
                reverse_path.push_back(current);
                if (current == source) break;
                current = parent[current];
            }
            if (!reverse_path.empty() && reverse_path.back() == source)
            {
                std::ranges::reverse(reverse_path.begin(), reverse_path.end());
                result.path = std::move(reverse_path);
            }
            else
            {
                result.path.clear();
            }
        }

        return result;
    }

    SearchResult dijkstra(const Graph &graph, Vertex source, Vertex target,
                          const SearchOptions &options, SearchScratch &scratch)
    {
        return shortest_path(graph, source, target, nullptr, options, scratch);
    }

    SearchResult astar_search(const Graph &graph, Vertex source, Vertex target,
                              Heuristic &heuristic, const SearchOptions &options,
                              SearchScratch &scratch)
    {
        return shortest_path(graph, source, target, &heuristic, options, scratch);
    }
} // namespace astar
