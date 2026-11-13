#include "oracle.hpp"

#include <algorithm>
#include <cassert>
#include <functional>
#include <numeric>
#include <queue>
#include <stdexcept>

namespace astar
{
    std::vector<std::uint32_t> bfs_distances(const Graph &graph, Vertex source)
    {
        std::vector<std::uint32_t> dist(graph.n, kInfiniteIndex);
        if (source >= graph.n) return dist;

        std::vector<Vertex> queue;
        queue.reserve(graph.n);
        dist[source] = 0;
        queue.push_back(source);

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            const Vertex u = queue[head];
            const std::uint32_t next_distance = dist[u] + 1;
            for (std::uint64_t i = graph.offsets[u]; i < graph.offsets[u + 1]; ++i)
            {
                const Vertex v = graph.targets[i];
                if (dist[v] == kInfiniteIndex)
                {
                    dist[v] = next_distance;
                    queue.push_back(v);
                }
            }
        }
        return dist;
    }

    std::vector<Distance> dijkstra_distances(const Graph &graph, Vertex source)
    {
        std::vector<Distance> dist(graph.n, kInfinity);
        if (source >= graph.n) return dist;

        using Item = std::pair<Distance, Vertex>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item> > queue;
        dist[source] = 0;
        queue.emplace(0, source);

        while (!queue.empty())
        {
            const auto [du, u] = queue.top();
            queue.pop();
            if (du != dist[u]) continue;
            for (std::uint64_t e = graph.offsets[u]; e < graph.offsets[u + 1]; ++e)
            {
                const Vertex v = graph.targets[e];
                const Weight w = graph.weights[e];
                if (w == kInfiniteWeight || du > kInfinity - static_cast<Distance>(w)) continue;
                const Distance candidate = du + static_cast<Distance>(w);
                if (candidate < dist[v])
                {
                    dist[v] = candidate;
                    queue.emplace(candidate, v);
                }
            }
        }
        return dist;
    }

    std::uint32_t bfs_distance(const Graph &graph, Vertex source, Vertex target)
    {
        if (source >= graph.n || target >= graph.n) return kInfiniteIndex;
        if (source == target) return 0;

        std::vector<std::uint32_t> dist(graph.n, kInfiniteIndex);
        std::vector<Vertex> queue;
        queue.reserve(1024);
        dist[source] = 0;
        queue.push_back(source);

        for (std::size_t head = 0; head < queue.size(); ++head)
        {
            const Vertex u = queue[head];
            if (u == target) return dist[u];
            const std::uint32_t next_distance = dist[u] + 1;
            for (std::uint64_t i = graph.offsets[u]; i < graph.offsets[u + 1]; ++i)
            {
                const Vertex v = graph.targets[i];
                if (dist[v] == kInfiniteIndex)
                {
                    dist[v] = next_distance;
                    queue.push_back(v);
                }
            }
        }
        return kInfiniteIndex;
    }

    std::vector<std::vector<Distance> > floyd_warshall(const Graph &graph)
    {
        const std::size_t n = graph.n;
        std::vector<std::vector<Distance> > dist(n, std::vector<Distance>(n, kInfinity));
        for (std::size_t i = 0; i < n; ++i) dist[i][i] = 0;
        for (Vertex u = 0; u < graph.n; ++u)
        {
            for (std::uint64_t i = graph.offsets[u]; i < graph.offsets[u + 1]; ++i)
            {
                const Vertex v = graph.targets[i];
                dist[u][v] = std::min<Distance>(dist[u][v], graph.weights[i]);
            }
        }
        for (std::size_t k = 0; k < n; ++k)
        {
            for (std::size_t i = 0; i < n; ++i)
            {
                if (dist[i][k] == kInfinity) continue;
                for (std::size_t j = 0; j < n; ++j)
                {
                    if (dist[k][j] == kInfinity) continue;
                    const Distance through = dist[i][k] + dist[k][j];
                    if (through < dist[i][j]) dist[i][j] = through;
                }
            }
        }
        return dist;
    }

    bool validate_path(const Graph &graph, Vertex source, Vertex target,
                       const std::vector<Vertex> &path, Distance expected_distance,
                       std::string* error)
    {
        auto fail = [&](const std::string &message) {
            if (error != nullptr) *error = message;
            return false;
        };

        if (path.empty()) return fail("empty path");
        if (path.front() != source) return fail("path does not start at source");
        if (path.back() != target) return fail("path does not end at target");

        Distance total = 0;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            const Vertex u = path[i];
            const Vertex v = path[i + 1];
            Weight best = kInfiniteWeight;
            for (std::uint64_t e = graph.offsets[u]; e < graph.offsets[u + 1]; ++e)
            {
                if (graph.targets[e] == v) best = std::min(best, graph.weights[e]);
            }
            if (best == kInfiniteWeight) return fail("consecutive path vertices are not adjacent");
            total += best;
        }

        if (total != expected_distance)
        {
            return fail("path length " + std::to_string(total) + " != expected " +
                        std::to_string(expected_distance));
        }
        return true;
    }

    std::vector<Vertex> largest_connected_component(const Graph &graph)
    {
        const std::size_t n = graph.n;
        if (n == 0) return {};

        std::vector<Vertex> parent(n);
        std::iota(parent.begin(), parent.end(), 0);
        std::vector<std::uint32_t> rank(n, 0);

        auto find = [&](Vertex x) {
            Vertex root = x;
            while (parent[root] != root) root = parent[root];
            while (parent[x] != x)
            {
                const Vertex next = parent[x];
                parent[x] = root;
                x = next;
            }
            return root;
        };
        auto unite = [&](Vertex a, Vertex b) {
            a = find(a);
            b = find(b);
            if (a == b) return;
            if (rank[a] < rank[b]) std::swap(a, b);
            parent[b] = a;
            if (rank[a] == rank[b]) ++rank[a];
        };

        for (Vertex u = 0; u < graph.n; ++u)
        {
            for (std::uint64_t i = graph.offsets[u]; i < graph.offsets[u + 1]; ++i)
            {
                unite(u, graph.targets[i]);
            }
        }

        std::vector<std::uint32_t> sizes(n, 0);
        for (Vertex v = 0; v < graph.n; ++v) ++sizes[find(v)];
        const Vertex best = static_cast<Vertex>(
            std::ranges::max_element(sizes.begin(), sizes.end()) - sizes.begin());

        std::vector<Vertex> component;
        component.reserve(sizes[best]);
        for (Vertex v = 0; v < graph.n; ++v)
        {
            if (find(v) == best) component.push_back(v);
        }
        return component;
    }
} // namespace astar
