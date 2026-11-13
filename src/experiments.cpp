#include "experiments.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "oracle.hpp"
#include "search.hpp"

namespace astar
{
    namespace
    {
        struct Band {
            const char* name;
            double low;
            double high;
        };

        constexpr Band kBands[3] = {
            {"short", 0.08, 0.25},
            {"medium", 0.40, 0.60},
            {"long", 0.80, 0.97},
        };

        const char* csv_header()
        {
            return "experiment,graph_id,family,width,height,obstacle_density,n,m_arcs,"
                    "graph_seed,query_seed,pair_id,band,source,target,oracle_distance,"
                    "variant,algorithm,tie_break,k,rep,time_ns,status,distance,correct,"
                    "expanded,pushed,popped,stale_popped,relaxed_edges,improved,"
                    "heuristic_calls,heap_peak\n";
        }

        void write_record(std::ofstream &out, const std::string &experiment,
                          const std::string &graph_id, const std::string &family,
                          std::uint32_t width, std::uint32_t height,
                          double obstacle_density, const Graph &graph,
                          std::uint64_t graph_seed, std::uint64_t query_seed,
                          std::uint32_t pair_id, const Query &query,
                          const std::string &variant, const std::string &algorithm,
                          const std::string &tie_break, std::uint32_t k,
                          std::uint32_t rep, std::uint64_t time_ns,
                          const SearchResult &result, bool correct)
        {
            out << experiment << ',' << graph_id << ',' << family << ',' << width << ','
                    << height << ',' << std::fixed << std::setprecision(6) << obstacle_density
                    << ',' << graph.n << ',' << graph.num_arcs() << ',' << graph_seed << ','
                    << query_seed << ',' << pair_id << ',' << query.band << ',' << query.source
                    << ',' << query.target << ',';
            if (query.oracle_distance == kInfiniteIndex)
            {
                out << -1;
            }
            else
            {
                out << query.oracle_distance;
            }
            out << ',' << variant << ',' << algorithm << ',' << tie_break << ',' << k
                    << ',' << rep << ',' << time_ns << ',' << status_name(result.status)
                    << ',';
            if (result.status == SearchStatus::Ok && result.distance < kInfinity)
            {
                out << result.distance;
            }
            else
            {
                out << -1;
            }
            const SearchCounters &c = result.counters;
            out << ',' << (correct ? 1 : 0) << ',' << c.expanded << ',' << c.pushed << ','
                    << c.popped << ',' << c.stale_popped << ',' << c.relaxed_edges << ','
                    << c.improved << ',' << c.heuristic_calls << ',' << c.heap_peak << '\n';
        }

        bool result_matches_oracle(const SearchResult &result, std::uint32_t oracle)
        {
            if (oracle == kInfiniteIndex) return result.status == SearchStatus::NoPath;
            return result.status == SearchStatus::Ok && result.distance == oracle;
        }

        struct TimedResult {
            SearchResult result;
            std::uint64_t time_ns = 0;
        };

        TimedResult run_timed(const Graph &graph, Vertex source, Vertex target,
                              Heuristic* heuristic, TieBreak tie,
                              std::uint64_t time_limit_ns, SearchScratch &scratch)
        {
            SearchOptions options;
            options.tie = tie;
            options.build_path = false;
            options.time_limit_ns = time_limit_ns;
            const auto start = std::chrono::steady_clock::now();
            SearchResult result = shortest_path(graph, source, target, heuristic, options, scratch);
            const auto stop = std::chrono::steady_clock::now();
            TimedResult timed;
            timed.result = std::move(result);
            timed.time_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
            return timed;
        }

        std::string make_graph_id(GridFamily family, std::uint32_t size)
        {
            return "grid_" + grid_family_name(family) + "_" + std::to_string(size);
        }
    } // namespace

    std::vector<Query> select_queries(const Graph &graph,
                                      const std::vector<Vertex> &component,
                                      std::uint32_t per_band, std::uint64_t seed)
    {
        std::vector<Query> queries;
        if (component.size() < 2 || per_band == 0) return queries;

        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<std::size_t> pick_source(0, component.size() - 1);
        std::uint32_t selected[3] = {0, 0, 0};
        std::uint32_t attempts = 0;
        const std::uint32_t max_attempts = per_band * 12 + 20;

        while ((selected[0] < per_band || selected[1] < per_band || selected[2] < per_band) &&
               attempts++ < max_attempts)
        {
            const Vertex source = component[pick_source(rng)];
            const std::vector<std::uint32_t> dist = bfs_distances(graph, source);

            std::uint32_t dmax = 0;
            for (Vertex v: component)
            {
                if (dist[v] != kInfiniteIndex && dist[v] > dmax) dmax = dist[v];
            }
            if (dmax < 2) continue;

            for (int b = 0; b < 3; ++b)
            {
                if (selected[b] >= per_band) continue;
                const std::uint32_t low = std::max<std::uint32_t>(
                    1, static_cast<std::uint32_t>(std::floor(kBands[b].low * dmax)));
                const std::uint32_t high = std::min<std::uint32_t>(
                    dmax, static_cast<std::uint32_t>(std::ceil(kBands[b].high * dmax)));
                const std::uint32_t center = (low + high) / 2;

                Vertex chosen = kInvalidVertex;
                std::uint64_t count = 0;
                for (Vertex v: component)
                {
                    if (v == source || dist[v] == kInfiniteIndex) continue;
                    if (dist[v] < low || dist[v] > high) continue;
                    ++count;
                    if (std::uniform_int_distribution<std::uint64_t>(0, count - 1)(rng) == 0)
                    {
                        chosen = v;
                    }
                }
                if (chosen == kInvalidVertex)
                {
                    // Small or very narrow components: use the reachable vertex
                    // whose distance is closest to the requested band's centre.
                    std::uint32_t best_delta = std::numeric_limits<std::uint32_t>::max();
                    for (Vertex v: component)
                    {
                        if (v == source || dist[v] == kInfiniteIndex) continue;
                        const std::uint32_t delta = dist[v] > center ? dist[v] - center : center - dist[v];
                        if (delta < best_delta)
                        {
                            best_delta = delta;
                            chosen = v;
                        }
                    }
                }
                if (chosen == kInvalidVertex) continue;

                Query query;
                query.source = source;
                query.target = chosen;
                query.band = kBands[b].name;
                query.oracle_distance = dist[chosen];
                queries.push_back(std::move(query));
                ++selected[b];
                if (selected[0] >= per_band && selected[1] >= per_band &&
                    selected[2] >= per_band)
                {
                    break;
                }
            }
        }

        if (queries.size() != static_cast<std::size_t>(per_band) * 3)
        {
            std::cerr << "warning: selected " << queries.size() << " queries, expected "
                    << per_band * 3 << "\n";
        }
        return queries;
    }

    std::vector<Query> select_weighted_queries(const Graph &graph,
                                               const std::vector<Vertex> &component,
                                               std::uint32_t per_band,
                                               std::uint64_t seed)
    {
        std::vector<Query> queries;
        if (component.size() < 2 || per_band == 0) return queries;

        std::mt19937_64 rng(seed);
        std::uniform_int_distribution<std::size_t> pick_source(0, component.size() - 1);
        std::uint32_t selected[3] = {0, 0, 0};
        std::uint32_t attempts = 0;
        const std::uint32_t max_attempts = per_band * 12 + 20;

        while ((selected[0] < per_band || selected[1] < per_band || selected[2] < per_band) &&
               attempts++ < max_attempts)
        {
            const Vertex source = component[pick_source(rng)];
            const std::vector<Distance> dist = dijkstra_distances(graph, source);

            Distance dmax = 0;
            for (Vertex v: component)
            {
                if (dist[v] != kInfinity && dist[v] > dmax) dmax = dist[v];
            }
            if (dmax < 2) continue;

            for (int b = 0; b < 3; ++b)
            {
                if (selected[b] >= per_band) continue;
                const Distance low = std::max<Distance>(
                    1, static_cast<Distance>(std::floor(kBands[b].low * static_cast<double>(dmax))));
                const Distance high = std::min<Distance>(
                    dmax, static_cast<Distance>(std::ceil(kBands[b].high * static_cast<double>(dmax))));
                const Distance center = (low + high) / 2;

                Vertex chosen = kInvalidVertex;
                std::uint64_t count = 0;
                for (Vertex v: component)
                {
                    if (v == source || dist[v] == kInfinity) continue;
                    if (dist[v] < low || dist[v] > high) continue;
                    ++count;
                    if (std::uniform_int_distribution<std::uint64_t>(0, count - 1)(rng) == 0)
                    {
                        chosen = v;
                    }
                }
                if (chosen == kInvalidVertex)
                {
                    Distance best_delta = kInfinity;
                    for (Vertex v: component)
                    {
                        if (v == source || dist[v] == kInfinity) continue;
                        const Distance delta = dist[v] > center ? dist[v] - center : center - dist[v];
                        if (delta < best_delta)
                        {
                            best_delta = delta;
                            chosen = v;
                        }
                    }
                }
                if (chosen == kInvalidVertex) continue;
                if (dist[chosen] > static_cast<Distance>(kInfiniteIndex - 1))
                {
                    throw std::runtime_error("weighted query distance exceeds 32-bit range");
                }

                Query query;
                query.source = source;
                query.target = chosen;
                query.band = kBands[b].name;
                query.oracle_distance = static_cast<std::uint32_t>(dist[chosen]);
                queries.push_back(std::move(query));
                ++selected[b];
                if (selected[0] >= per_band && selected[1] >= per_band &&
                    selected[2] >= per_band)
                {
                    break;
                }
            }
        }

        if (queries.size() != static_cast<std::size_t>(per_band) * 3)
        {
            std::cerr << "warning: selected " << queries.size() << " weighted queries, expected "
                    << per_band * 3 << "\n";
        }
        return queries;
    }

    int run_grid_benchmark(const BenchmarkOptions &options,
                           const std::vector<std::uint32_t> &sizes,
                           const std::vector<GridFamily> &families,
                           double random_density, double clustered_density)
    {
        std::ofstream out(options.output);
        if (!out) throw std::runtime_error("cannot open output: " + options.output);
        out << csv_header();

        for (GridFamily family: families)
        {
            for (std::uint32_t size: sizes)
            {
                const std::string graph_id = make_graph_id(family, size);
                GridConfig config;
                config.width = size;
                config.height = size;
                config.family = family;
                config.obstacle_density =
                        family == GridFamily::Random
                            ? random_density
                            : family == GridFamily::Clustered
                                  ? clustered_density
                                  : 0.0;
                config.seed = options.seed ^ fnv1a64("graph_" + graph_id);

                std::cerr << "building " << graph_id << "...\n";
                Grid grid = make_grid(config);
                const std::vector<Vertex> component = largest_connected_component(grid.graph);
                if (component.size() < 10)
                {
                    std::cerr << "  skipping: largest component has " << component.size()
                            << " vertices\n";
                    continue;
                }
                const std::uint64_t query_seed = options.seed ^ fnv1a64("query_" + graph_id);
                const std::vector<Query> queries =
                        select_queries(grid.graph, component, options.per_band, query_seed);
                if (queries.empty())
                {
                    std::cerr << "  skipping: no queries\n";
                    continue;
                }

                ManhattanHeuristic manhattan(size);
                SearchScratch dijkstra_scratch(size, TieBreak::LargerG);
                SearchScratch astar_scratch(size, TieBreak::LargerG);
                SearchScratch astar_nodeid_scratch(size, TieBreak::NodeId);

                // Untimed warm-up of every treatment on the first query.
                (void) run_timed(grid.graph, queries[0].source, queries[0].target, nullptr,
                                 TieBreak::LargerG, options.time_limit_ns, dijkstra_scratch);
                (void) run_timed(grid.graph, queries[0].source, queries[0].target, &manhattan,
                                 TieBreak::LargerG, options.time_limit_ns, astar_scratch);
                (void) run_timed(grid.graph, queries[0].source, queries[0].target, &manhattan,
                                 TieBreak::NodeId, options.time_limit_ns, astar_nodeid_scratch);

                const double obstacle_density =
                        1.0 - static_cast<double>(grid.free_cells) /
                        (static_cast<double>(size) * size);

                for (std::size_t qi = 0; qi < queries.size(); ++qi)
                {
                    const Query &query = queries[qi];
                    for (std::uint32_t rep = 1; rep <= options.reps; ++rep)
                    {
                        auto emit = [&](const std::string &variant,
                                        const std::string &algorithm,
                                        const std::string &tie_name, std::uint32_t k,
                                        Heuristic* heuristic, TieBreak tie,
                                        SearchScratch &scratch) {
                            const TimedResult timed =
                                    run_timed(grid.graph, query.source, query.target, heuristic,
                                              tie, options.time_limit_ns, scratch);
                            const bool correct = result_matches_oracle(timed.result,
                                                                       query.oracle_distance);
                            write_record(out, "grid", graph_id, grid_family_name(family),
                                         size, size, obstacle_density, grid.graph,
                                         config.seed, query_seed,
                                         static_cast<std::uint32_t>(qi), query, variant,
                                         algorithm, tie_name, k, rep, timed.time_ns,
                                         timed.result, correct);
                            if (timed.result.status == SearchStatus::Timeout)
                            {
                                std::cerr << "  timeout in " << graph_id << " pair " << qi
                                        << " variant " << variant << "\n";
                            }
                            else if (!correct)
                            {
                                throw std::runtime_error("distance mismatch in " + graph_id +
                                                         " pair " + std::to_string(qi));
                            }
                        };

                        if (rep % 2 == 0)
                        {
                            emit("astar_larger_g", "astar", "larger_g", 0, &manhattan,
                                 TieBreak::LargerG, astar_scratch);
                            emit("dijkstra", "dijkstra", "larger_g", 0, nullptr,
                                 TieBreak::LargerG, dijkstra_scratch);
                        }
                        else
                        {
                            emit("dijkstra", "dijkstra", "larger_g", 0, nullptr,
                                 TieBreak::LargerG, dijkstra_scratch);
                            emit("astar_larger_g", "astar", "larger_g", 0, &manhattan,
                                 TieBreak::LargerG, astar_scratch);
                        }
                        emit("astar_node_id", "astar", "node_id", 0, &manhattan,
                             TieBreak::NodeId, astar_nodeid_scratch);
                    }
                }
                std::cerr << "  n=" << grid.graph.n << " m=" << grid.graph.num_arcs()
                        << " free=" << grid.free_cells << " component=" << component.size()
                        << " queries=" << queries.size() << "\n";
            }
        }
        return 0;
    }

    int run_real_benchmark(const BenchmarkOptions &options, const Graph &graph,
                           const LandmarkData &landmarks, std::uint32_t main_k)
    {
        if (graph.n == 0 || landmarks.n != graph.n)
        {
            throw std::runtime_error("real benchmark: graph/landmark size mismatch");
        }
        std::ofstream out(options.output);
        if (!out) throw std::runtime_error("cannot open output: " + options.output);
        out << csv_header();

        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;

        const std::uint64_t query_seed = options.seed ^ fnv1a64("query_roadNet-CA");
        std::cerr << "selecting real-graph queries...\n";
        const std::vector<Query> queries =
                select_queries(graph, component, options.per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("no real-graph queries selected");

        ALTHeuristic alt1(landmarks, 1);
        ALTHeuristic alt4(landmarks, 4);
        ALTHeuristic alt8(landmarks, main_k);
        ALTHeuristic alt16(landmarks, 16);

        SearchScratch dijkstra_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt1_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt4_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt8_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt16_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt8_nodeid_scratch(graph.n, TieBreak::NodeId);

        struct Variant {
            std::string name;
            std::string algorithm;
            std::string tie;
            std::uint32_t k;
            Heuristic* heuristic;
            TieBreak tie_break;
            SearchScratch* scratch;
        };
        std::vector<Variant> variants = {
            {
                "dijkstra", "dijkstra", "larger_g", 0, nullptr, TieBreak::LargerG,
                &dijkstra_scratch
            },
            {
                "astar_alt1", "astar", "larger_g", 1, &alt1, TieBreak::LargerG,
                &alt1_scratch
            },
            {
                "astar_alt4", "astar", "larger_g", 4, &alt4, TieBreak::LargerG,
                &alt4_scratch
            },
            {
                "astar_alt8", "astar", "larger_g", main_k, &alt8, TieBreak::LargerG,
                &alt8_scratch
            },
            {
                "astar_alt16", "astar", "larger_g", 16, &alt16, TieBreak::LargerG,
                &alt16_scratch
            },
            {
                "astar_alt8_node_id", "astar", "node_id", main_k, &alt8, TieBreak::NodeId,
                &alt8_nodeid_scratch
            },
        };

        // Untimed warm-up for every treatment.
        for (const Variant &variant: variants)
        {
            (void) run_timed(graph, queries[0].source, queries[0].target,
                             variant.heuristic, variant.tie_break, options.time_limit_ns,
                             *variant.scratch);
        }

        for (std::size_t qi = 0; qi < queries.size(); ++qi)
        {
            const Query &query = queries[qi];
            for (std::uint32_t rep = 1; rep <= options.reps; ++rep)
            {
                const std::size_t rotation = (rep - 1) % variants.size();
                for (std::size_t v = 0; v < variants.size(); ++v)
                {
                    const Variant &variant = variants[(v + rotation) % variants.size()];
                    const TimedResult timed =
                            run_timed(graph, query.source, query.target, variant.heuristic,
                                      variant.tie_break, options.time_limit_ns, *variant.scratch);
                    const bool correct =
                            result_matches_oracle(timed.result, query.oracle_distance);
                    write_record(out, "real", "roadNet-CA", "road", 0, 0, 0.0, graph,
                                 options.seed, query_seed, static_cast<std::uint32_t>(qi),
                                 query, variant.name, variant.algorithm, variant.tie,
                                 variant.k, rep, timed.time_ns, timed.result, correct);
                    if (timed.result.status == SearchStatus::Timeout)
                    {
                        std::cerr << "  timeout on real pair " << qi << " variant "
                                << variant.name << "\n";
                    }
                    else if (!correct)
                    {
                        throw std::runtime_error("distance mismatch on real pair " +
                                                 std::to_string(qi));
                    }
                }
            }
            if ((qi + 1) % 5 == 0)
            {
                std::cerr << "  real queries: " << (qi + 1) << "/" << queries.size()
                        << "\n";
            }
        }
        return 0;
    }

    int run_weighted_road_benchmark(const BenchmarkOptions &options,
                                    const Graph &graph,
                                    const LandmarkData &landmarks,
                                    const Coordinates &coordinates,
                                    std::uint32_t main_k)
    {
        if (graph.n == 0 || landmarks.n != graph.n || coordinates.n != graph.n)
        {
            throw std::runtime_error("weighted road: graph/landmark/coordinate size mismatch");
        }
        std::ofstream out(options.output);
        if (!out) throw std::runtime_error("cannot open weighted-road output: " + options.output);
        out << csv_header();

        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;
        const std::uint64_t query_seed =
                options.seed ^ fnv1a64("query_roadNet-NY-weighted");
        std::cerr << "selecting weighted road queries...\n";
        const std::vector<Query> queries =
                select_weighted_queries(graph, component, options.per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("no weighted road queries selected");

        EuclideanHeuristic euclidean(coordinates);
        ALTHeuristic alt1(landmarks, 1);
        ALTHeuristic alt4(landmarks, 4);
        ALTHeuristic alt8(landmarks, main_k);
        ALTHeuristic alt16(landmarks, 16);

        SearchScratch dijkstra_scratch(graph.n, TieBreak::LargerG);
        SearchScratch euclidean_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt1_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt4_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt8_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt16_scratch(graph.n, TieBreak::LargerG);
        SearchScratch alt8_nodeid_scratch(graph.n, TieBreak::NodeId);

        struct Variant {
            std::string name;
            std::string algorithm;
            std::string tie;
            std::uint32_t k;
            Heuristic* heuristic;
            TieBreak tie_break;
            SearchScratch* scratch;
        };
        std::vector<Variant> variants = {
            {"dijkstra", "dijkstra", "larger_g", 0, nullptr, TieBreak::LargerG,
             &dijkstra_scratch},
            {"astar_euclidean", "astar", "larger_g", 0, &euclidean, TieBreak::LargerG,
             &euclidean_scratch},
            {"astar_alt1", "astar", "larger_g", 1, &alt1, TieBreak::LargerG,
             &alt1_scratch},
            {"astar_alt4", "astar", "larger_g", 4, &alt4, TieBreak::LargerG,
             &alt4_scratch},
            {"astar_alt8", "astar", "larger_g", main_k, &alt8, TieBreak::LargerG,
             &alt8_scratch},
            {"astar_alt16", "astar", "larger_g", 16, &alt16, TieBreak::LargerG,
             &alt16_scratch},
            {"astar_alt8_node_id", "astar", "node_id", main_k, &alt8, TieBreak::NodeId,
             &alt8_nodeid_scratch},
        };

        for (const Variant &variant: variants)
        {
            (void) run_timed(graph, queries[0].source, queries[0].target,
                             variant.heuristic, variant.tie_break, options.time_limit_ns,
                             *variant.scratch);
        }

        const std::string experiment = "weighted_road";
        const std::string graph_id = "roadNet-NY";
        const std::string family = "road_weighted";
        for (std::size_t qi = 0; qi < queries.size(); ++qi)
        {
            const Query &query = queries[qi];
            for (std::uint32_t rep = 1; rep <= options.reps; ++rep)
            {
                const std::size_t rotation = (rep - 1) % variants.size();
                for (std::size_t v = 0; v < variants.size(); ++v)
                {
                    const Variant &variant = variants[(v + rotation) % variants.size()];
                    const TimedResult timed =
                            run_timed(graph, query.source, query.target,
                                      variant.heuristic, variant.tie_break,
                                      options.time_limit_ns, *variant.scratch);
                    const bool correct =
                            result_matches_oracle(timed.result, query.oracle_distance);
                    write_record(out, experiment, graph_id, family, 0, 0, 0.0, graph,
                                 options.seed, query_seed, static_cast<std::uint32_t>(qi),
                                 query, variant.name, variant.algorithm, variant.tie,
                                 variant.k, rep, timed.time_ns, timed.result, correct);
                    if (timed.result.status == SearchStatus::Timeout)
                    {
                        std::cerr << "  timeout on weighted pair " << qi << " variant "
                                  << variant.name << "\n";
                    }
                    else if (!correct)
                    {
                        throw std::runtime_error("distance mismatch on weighted pair " +
                                                 std::to_string(qi));
                    }
                }
            }
            if ((qi + 1) % 5 == 0)
            {
                std::cerr << "  weighted road queries: " << (qi + 1) << "/"
                          << queries.size() << "\n";
            }
        }
        return 0;
    }

    int run_validation(const Graph &graph, const LandmarkData* landmarks,
                       std::uint32_t main_k, std::uint32_t sample_queries,
                       std::uint64_t seed)
    {
        if (landmarks == nullptr || landmarks->n != graph.n)
        {
            throw std::runtime_error("validation needs matching landmarks");
        }
        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;
        const std::uint32_t per_band = std::max<std::uint32_t>(1, sample_queries / 3);
        const std::vector<Query> queries =
                select_queries(graph, component, per_band, seed ^ fnv1a64("validation"));
        if (queries.empty()) throw std::runtime_error("validation selected no queries");

        ALTHeuristic alt(*landmarks, main_k);
        SearchScratch dijkstra_scratch(graph.n, TieBreak::LargerG);
        SearchScratch astar_scratch(graph.n, TieBreak::LargerG);

        std::uint32_t checked = 0;
        for (const Query &query: queries)
        {
            if (query.oracle_distance == kInfiniteIndex) continue;

            SearchOptions options;
            options.build_path = true;
            options.tie = TieBreak::LargerG;
            const SearchResult d =
                    shortest_path(graph, query.source, query.target, nullptr, options,
                                  dijkstra_scratch);
            const SearchResult a =
                    shortest_path(graph, query.source, query.target, &alt, options,
                                  astar_scratch);

            std::string error;
            if (d.status != SearchStatus::Ok || d.distance != query.oracle_distance ||
                !validate_path(graph, query.source, query.target, d.path, d.distance, &error))
            {
                std::cerr << "Dijkstra validation failed: " << error << "\n";
                return 1;
            }
            if (a.status != SearchStatus::Ok || a.distance != query.oracle_distance ||
                !validate_path(graph, query.source, query.target, a.path, a.distance, &error))
            {
                std::cerr << "A* validation failed: " << error << "\n";
                return 1;
            }
            ++checked;
        }

        std::cout << "validation: " << checked << " queries; BFS oracle, Dijkstra and A*"
                << " distances agree and both paths are valid\n";
        return checked == queries.size() ? 0 : 1;
    }
} // namespace astar
