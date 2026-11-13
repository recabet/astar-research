#include "heuristic.hpp"

#include <algorithm>
#include <cassert>
#include <fstream>
#include <stdexcept>

#include "oracle.hpp"

namespace astar
{
    void ManhattanHeuristic::prepare(Vertex target)
    {
        target_ = target;
        target_row_ = static_cast<std::int64_t>(target / width_);
        target_col_ = static_cast<std::int64_t>(target % width_);
    }

    Distance ManhattanHeuristic::estimate(Vertex vertex) const
    {
        const auto row = static_cast<std::int64_t>(vertex / width_);
        const auto col = static_cast<std::int64_t>(vertex % width_);
        return std::llabs(row - target_row_) + std::llabs(col - target_col_);
    }

    LandmarkData select_landmarks(const Graph &graph, std::uint32_t k,
                                  std::uint64_t seed)
    {
        if (graph.n == 0) throw std::runtime_error("cannot select landmarks of an empty graph");
        if (k == 0) throw std::runtime_error("number of landmarks must be positive");
        k = std::min<std::uint32_t>(k, graph.n);

        LandmarkData data;
        data.n = graph.n;
        data.k = k;
        data.ids.resize(k);
        data.distances.resize(static_cast<std::size_t>(k) * graph.n);

        const auto first = static_cast<Vertex>(seed % graph.n);
        std::vector<std::uint32_t> min_distance(graph.n, kInfiniteIndex);
        Vertex current = first;

        for (std::uint32_t l = 0; l < k; ++l)
        {
            const std::vector<std::uint32_t> d = bfs_distances(graph, current);
            data.ids[l] = current;
            std::copy(d.begin(), d.end(),
                      data.distances.begin() + static_cast<std::size_t>(l) * graph.n);

            if (l == 0)
            {
                min_distance = d;
            }
            else
            {
                for (std::size_t v = 0; v < graph.n; ++v)
                {
                    if (d[v] < min_distance[v]) min_distance[v] = d[v];
                }
            }

            if (l + 1 == k) break;

            // Farthest-point sampling: the next landmark maximises the distance to
            // the nearest already selected landmark.
            Vertex next = kInvalidVertex;
            std::uint32_t best_distance = 0;
            for (Vertex v = 0; v < graph.n; ++v)
            {
                if (min_distance[v] != kInfiniteIndex && min_distance[v] > best_distance)
                {
                    best_distance = min_distance[v];
                    next = v;
                }
            }
            if (next == kInvalidVertex)
            {
                // Disconnected graph (not expected for the benchmark graph): repeat
                // the last landmark so that the table remains rectangular.
                next = data.ids[l];
            }
            current = next;
        }

        return data;
    }

    LandmarkData select_landmarks_weighted(const Graph &graph, std::uint32_t k,
                                           std::uint64_t seed)
    {
        if (graph.n == 0) throw std::runtime_error("cannot select landmarks of an empty graph");
        if (k == 0) throw std::runtime_error("number of landmarks must be positive");
        k = std::min<std::uint32_t>(k, graph.n);

        LandmarkData data;
        data.n = graph.n;
        data.k = k;
        data.ids.resize(k);
        data.distances.resize(static_cast<std::size_t>(k) * graph.n);

        const auto first = static_cast<Vertex>(seed % graph.n);
        std::vector<Distance> min_distance(graph.n, kInfinity);
        Vertex current = first;

        for (std::uint32_t l = 0; l < k; ++l)
        {
            const std::vector<Distance> d = dijkstra_distances(graph, current);
            data.ids[l] = current;
            for (std::size_t v = 0; v < graph.n; ++v)
            {
                if (d[v] == kInfinity)
                {
                    data.distances[static_cast<std::size_t>(l) * graph.n + v] =
                            kInfiniteIndex;
                }
                else if (d[v] > static_cast<Distance>(kInfiniteIndex - 1))
                {
                    throw std::runtime_error("weighted landmark distance exceeds 32-bit range");
                }
                else
                {
                    data.distances[static_cast<std::size_t>(l) * graph.n + v] =
                            static_cast<std::uint32_t>(d[v]);
                }
            }

            if (l == 0)
            {
                min_distance = d;
            }
            else
            {
                for (std::size_t v = 0; v < graph.n; ++v)
                {
                    if (d[v] < min_distance[v]) min_distance[v] = d[v];
                }
            }

            if (l + 1 == k) break;

            Vertex next = kInvalidVertex;
            Distance best_distance = 0;
            for (Vertex v = 0; v < graph.n; ++v)
            {
                if (min_distance[v] != kInfinity && min_distance[v] > best_distance)
                {
                    best_distance = min_distance[v];
                    next = v;
                }
            }
            if (next == kInvalidVertex) next = data.ids[l];
            current = next;
        }

        return data;
    }

    void save_coordinates_binary(const Coordinates &data, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot open coordinate output: " + path);
        constexpr char magic[8] = {'A', 'S', 'T', 'A', 'R', 'X', 'Y', '0'};
        out.write(magic, sizeof(magic));
        out.write(reinterpret_cast<const char *>(&data.n), sizeof(data.n));
        out.write(reinterpret_cast<const char *>(data.x.data()),
                  static_cast<std::streamsize>(data.x.size() * sizeof(double)));
        out.write(reinterpret_cast<const char *>(data.y.data()),
                  static_cast<std::streamsize>(data.y.size() * sizeof(double)));
        if (!out) throw std::runtime_error("failed while writing coordinates: " + path);
    }

    Coordinates load_coordinates_binary(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open coordinate input: " + path);
        char magic[8];
        constexpr char expected[8] = {'A', 'S', 'T', 'A', 'R', 'X', 'Y', '0'};
        in.read(magic, sizeof(magic));
        if (!in || !std::equal(magic, magic + 8, expected))
        {
            throw std::runtime_error("bad coordinate file magic: " + path);
        }
        Coordinates data;
        in.read(reinterpret_cast<char *>(&data.n), sizeof(data.n));
        if (!in || data.n == 0) throw std::runtime_error("bad coordinate header: " + path);
        data.x.resize(data.n);
        data.y.resize(data.n);
        in.read(reinterpret_cast<char *>(data.x.data()),
                static_cast<std::streamsize>(data.x.size() * sizeof(double)));
        in.read(reinterpret_cast<char *>(data.y.data()),
                static_cast<std::streamsize>(data.y.size() * sizeof(double)));
        if (!in) throw std::runtime_error("truncated coordinate file: " + path);
        return data;
    }

    void save_landmarks_binary(const LandmarkData &data, const std::string &path)
    {
        std::ofstream out(path, std::ios::binary);
        if (!out) throw std::runtime_error("cannot open landmark output: " + path);

        constexpr char magic[8] = {'A', 'S', 'T', 'A', 'R', 'A', 'L', 'T'};
        out.write(magic, sizeof(magic));
        out.write(reinterpret_cast<const char *>(&data.n), sizeof(data.n));
        out.write(reinterpret_cast<const char *>(&data.k), sizeof(data.k));
        out.write(reinterpret_cast<const char *>(data.ids.data()),
                  static_cast<std::streamsize>(data.ids.size() * sizeof(Vertex)));
        out.write(reinterpret_cast<const char *>(data.distances.data()),
                  static_cast<std::streamsize>(data.distances.size() * sizeof(std::uint32_t)));
        if (!out) throw std::runtime_error("failed while writing landmarks: " + path);
    }

    LandmarkData load_landmarks_binary(const std::string &path)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("cannot open landmark input: " + path);

        char magic[8];
        constexpr char expected[8] = {'A', 'S', 'T', 'A', 'R', 'A', 'L', 'T'};
        in.read(magic, sizeof(magic));
        if (!in || !std::equal(magic, magic + 8, expected))
        {
            throw std::runtime_error("bad landmark file magic: " + path);
        }

        LandmarkData data;
        in.read(reinterpret_cast<char *>(&data.n), sizeof(data.n));
        in.read(reinterpret_cast<char *>(&data.k), sizeof(data.k));
        if (!in || data.k == 0 || data.k > data.n)
        {
            throw std::runtime_error("bad landmark header: " + path);
        }
        data.ids.resize(data.k);
        data.distances.resize(static_cast<std::size_t>(data.k) * data.n);
        in.read(reinterpret_cast<char *>(data.ids.data()),
                static_cast<std::streamsize>(data.ids.size() * sizeof(Vertex)));
        in.read(reinterpret_cast<char *>(data.distances.data()),
                static_cast<std::streamsize>(data.distances.size() * sizeof(std::uint32_t)));
        if (!in) throw std::runtime_error("truncated landmark file: " + path);
        return data;
    }

    void ALTHeuristic::prepare(Vertex target)
    {
        target_ = target;
        for (std::uint32_t l = 0; l < k_; ++l)
        {
            target_dist_[l] = data_.get(l, target);
        }
    }

    Distance ALTHeuristic::estimate(Vertex vertex) const
    {
        Distance best = 0;
        for (std::uint32_t l = 0; l < k_; ++l)
        {
            const std::uint32_t dv = data_.get(l, vertex);
            const std::uint32_t dt = target_dist_[l];
            if (dv == kInfiniteIndex || dt == kInfiniteIndex) continue;
            const std::uint32_t diff = dv > dt ? dv - dt : dt - dv;
            if (static_cast<Distance>(diff) > best) best = static_cast<Distance>(diff);
        }
        return best;
    }
} // namespace astar
