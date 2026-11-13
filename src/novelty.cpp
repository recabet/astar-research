#include "novelty.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "experiments.hpp"
#include "oracle.hpp"
#include "search.hpp"

namespace astar
{
    namespace
    {
        struct SampleStats {
            std::size_t n = 0;
            double h_corr = 0.0; // Spearman rank correlation
            double h_pearson = 0.0; // raw Pearson correlation
            double h_mae = 0.0;
            double h_rmse = 0.0;
            double h_nmae = 0.0;
            double h_nrmse = 0.0;
            double h_exact_frac = 0.0;
            double h_over_frac = 0.0;
            double mean_h = 0.0;
            double mean_d = 0.0;
        };

        double mean_of(const std::vector<double> &values)
        {
            if (values.empty()) return 0.0;
            return std::accumulate(values.begin(), values.end(), 0.0) /
                   static_cast<double>(values.size());
        }

        double pearson(const std::vector<double> &x, const std::vector<double> &y)
        {
            if (x.size() < 2 || x.size() != y.size()) return 0.0;
            const double mx = mean_of(x);
            const double my = mean_of(y);
            double num = 0.0;
            double dx = 0.0;
            double dy = 0.0;
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                const double a = x[i] - mx;
                const double b = y[i] - my;
                num += a * b;
                dx += a * a;
                dy += b * b;
            }
            if (dx <= 0.0 || dy <= 0.0) return 0.0;
            return num / std::sqrt(dx * dy);
        }

        std::vector<double> ranks(const std::vector<double> &values)
        {
            const std::size_t n = values.size();
            std::vector<std::size_t> order(n);
            std::iota(order.begin(), order.end(), 0);
            std::ranges::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
                if (values[a] != values[b]) return values[a] < values[b];
                return a < b;
            });

            std::vector<double> result(n);
            for (std::size_t i = 0; i < n;)
            {
                std::size_t j = i + 1;
                while (j < n && values[order[j]] == values[order[i]]) ++j;
                const double rank = (static_cast<double>(i + 1) + static_cast<double>(j)) / 2.0;
                for (std::size_t k = i; k < j; ++k) result[order[k]] = rank;
                i = j;
            }
            return result;
        }

        double spearman(const std::vector<double> &x, const std::vector<double> &y)
        {
            if (x.size() < 2 || x.size() != y.size()) return 0.0;
            return pearson(ranks(x), ranks(y));
        }

        SampleStats compute_stats(const std::vector<double> &h,
                                  const std::vector<double> &d)
        {
            SampleStats stats;
            stats.n = h.size();
            if (h.empty() || h.size() != d.size()) return stats;

            std::vector<double> errors;
            errors.reserve(h.size());
            double sum_h = 0.0;
            double sum_d = 0.0;
            double sum_sq = 0.0;
            std::uint64_t exact = 0;
            std::uint64_t over = 0;
            for (std::size_t i = 0; i < h.size(); ++i)
            {
                const double error = d[i] - h[i];
                errors.push_back(std::fabs(error));
                sum_h += h[i];
                sum_d += d[i];
                sum_sq += error * error;
                if (h[i] == d[i]) ++exact;
                if (h[i] > d[i]) ++over;
            }

            stats.mean_h = sum_h / static_cast<double>(h.size());
            stats.mean_d = sum_d / static_cast<double>(d.size());
            stats.h_mae = mean_of(errors);
            stats.h_rmse = std::sqrt(sum_sq / static_cast<double>(h.size()));
            stats.h_nmae = stats.mean_d > 0.0 ? stats.h_mae / stats.mean_d : 0.0;
            stats.h_nrmse = stats.mean_d > 0.0 ? stats.h_rmse / stats.mean_d : 0.0;
            stats.h_exact_frac = static_cast<double>(exact) / static_cast<double>(h.size());
            stats.h_over_frac = static_cast<double>(over) / static_cast<double>(h.size());
            stats.h_corr = spearman(h, d);
            stats.h_pearson = pearson(h, d);
            return stats;
        }

        // Solve N = (b^(d+1)-1)/(b-1) for b > 1.  This is the effective branching
        // factor of a uniform tree with the same number of nodes and solution depth.
        double effective_branching(std::uint64_t nodes, std::uint32_t depth)
        {
            if (depth == 0 || nodes <= 1) return 1.0;
            if (nodes <= static_cast<std::uint64_t>(depth) + 1) return 1.0;

            auto tree_nodes = [&](double b) {
                if (std::fabs(b - 1.0) < 1e-12) return static_cast<double>(depth + 1);
                return (std::pow(b, static_cast<double>(depth + 1)) - 1.0) / (b - 1.0);
            };

            double lo = 1.0;
            double hi = 1000.0;
            for (int i = 0; i < 80; ++i)
            {
                const double mid = (lo + hi) / 2.0;
                if (tree_nodes(mid) < static_cast<double>(nodes))
                {
                    lo = mid;
                }
                else
                {
                    hi = mid;
                }
            }
            return (lo + hi) / 2.0;
        }

        std::string grid_id(GridFamily family, std::uint32_t size)
        {
            return "grid_" + grid_family_name(family) + "_" + std::to_string(size);
        }

        void write_grid_header(std::ofstream &out)
        {
            out << "graph_id,family,width,height,obstacle_density,component_size,pair_id,band,"
                    "source,target,optimal_distance,manhattan,detour,sample_size,"
                    "h_corr,h_pearson,h_mae,h_rmse,h_nmae,h_nrmse,h_exact_frac,h_over_frac,"
                    "mean_h,mean_d,expanded_d,expanded_a,exp_ratio,ebf_d,ebf_a\n";
        }

        void write_grid_row(std::ofstream &out, const std::string &id,
                            GridFamily family, std::uint32_t width, std::uint32_t height,
                            double obstacle_density, std::uint64_t component_size,
                            std::uint32_t pair_id, const Query &query,
                            std::uint32_t manhattan, double detour,
                            const SampleStats &stats, std::uint64_t expanded_d,
                            std::uint64_t expanded_a)
        {
            const double exp_ratio = expanded_a == 0
                                         ? 0.0
                                         : static_cast<double>(expanded_d) / static_cast<double>(expanded_a);
            out << id << ',' << grid_family_name(family) << ',' << width << ',' << height
                    << ',' << std::fixed << std::setprecision(6) << obstacle_density << ','
                    << component_size << ',' << pair_id << ',' << query.band << ','
                    << query.source << ',' << query.target << ',' << query.oracle_distance << ','
                    << manhattan << ',' << detour << ',' << stats.n << ','
                    << stats.h_corr << ',' << stats.h_pearson << ',' << stats.h_mae << ','
                    << stats.h_rmse << ',' << stats.h_nmae << ',' << stats.h_nrmse << ','
                    << stats.h_exact_frac << ',' << stats.h_over_frac << ',' << stats.mean_h
                    << ',' << stats.mean_d << ',' << expanded_d << ',' << expanded_a << ','
                    << exp_ratio << ',' << effective_branching(expanded_d, query.oracle_distance)
                    << ',' << effective_branching(expanded_a, query.oracle_distance) << '\n';
        }

        void write_real_header(std::ofstream &out)
        {
            out << "graph_id,family,pair_id,band,source,target,optimal_distance,k,"
                    "sample_size,h_corr,h_pearson,h_mae,h_rmse,h_nmae,h_nrmse,"
                    "h_exact_frac,h_over_frac,mean_h,mean_d,expanded_d,expanded_a,"
                    "exp_ratio,ebf_d,ebf_a\n";
        }

        void write_real_row(std::ofstream &out, std::uint32_t pair_id, const Query &query,
                            std::uint32_t k, const SampleStats &stats,
                            std::uint64_t expanded_d, std::uint64_t expanded_a)
        {
            const double exp_ratio = expanded_a == 0
                                         ? 0.0
                                         : static_cast<double>(expanded_d) / static_cast<double>(expanded_a);
            out << "roadNet-CA,road," << pair_id << ',' << query.band << ','
                    << query.source << ',' << query.target << ',' << query.oracle_distance << ','
                    << k << ',' << stats.n << ',' << std::fixed << std::setprecision(6)
                    << stats.h_corr << ',' << stats.h_pearson << ',' << stats.h_mae << ','
                    << stats.h_rmse << ',' << stats.h_nmae << ',' << stats.h_nrmse << ','
                    << stats.h_exact_frac << ',' << stats.h_over_frac << ',' << stats.mean_h
                    << ',' << stats.mean_d << ',' << expanded_d << ',' << expanded_a << ','
                    << exp_ratio << ',' << effective_branching(expanded_d, query.oracle_distance)
                    << ',' << effective_branching(expanded_a, query.oracle_distance) << '\n';
        }

        struct TimedSearch {
            SearchResult result;
            std::uint64_t time_ns = 0;
        };

        TimedSearch timed_search(const Graph &graph, Vertex source, Vertex target,
                                 Heuristic* heuristic, const SearchOptions &options,
                                 SearchScratch &scratch)
        {
            const auto start = std::chrono::steady_clock::now();
            SearchResult result = shortest_path(graph, source, target, heuristic,
                                                options, scratch);
            const auto stop = std::chrono::steady_clock::now();
            TimedSearch timed;
            timed.result = std::move(result);
            timed.time_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
            return timed;
        }

        void write_tie_plateau_header(std::ofstream &out)
        {
            out << "experiment,graph_id,family,size,pair_id,band,source,target,"
                   "tie_break,time_ns,status,distance,expanded,pushed,popped,"
                   "stale_popped,relaxed_edges,improved,heuristic_calls,heap_peak,"
                   "equal_f_expanded,f_plateau_runs,max_f_plateau\n";
        }

        void write_tie_plateau_row(std::ofstream &out, const std::string &experiment,
                                   const std::string &id, const std::string &family,
                                   std::uint32_t size, std::size_t pair_id,
                                   const Query &query, const char* tie_name,
                                   const TimedSearch &timed)
        {
            const SearchResult &result = timed.result;
            out << experiment << ',' << id << ',' << family << ',' << size << ','
                << static_cast<std::uint32_t>(pair_id) << ',' << query.band << ','
                << query.source << ',' << query.target << ',' << tie_name << ','
                << timed.time_ns << ',' << status_name(result.status) << ',';
            if (result.status == SearchStatus::Ok)
            {
                out << result.distance;
            }
            else
            {
                out << -1;
            }
            out << ',' << result.counters.expanded << ','
                << result.counters.pushed << ','
                << result.counters.popped << ','
                << result.counters.stale_popped << ','
                << result.counters.relaxed_edges << ','
                << result.counters.improved << ','
                << result.counters.heuristic_calls << ','
                << result.counters.heap_peak << ','
                << result.counters.equal_f_expanded << ','
                << result.counters.f_plateau_runs << ','
                << result.counters.max_f_plateau << '\n';
        }
    } // namespace

    int run_grid_diagnostics(const std::string &output,
                             const std::vector<std::uint32_t> &sizes,
                             const std::vector<GridFamily> &families,
                             std::uint32_t per_band, std::uint64_t seed)
    {
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open diagnostics output: " + output);
        write_grid_header(out);

        std::mt19937_64 sample_rng(seed ^ fnv1a64("diagnostics_sample"));

        for (GridFamily family: families)
        {
            for (std::uint32_t size: sizes)
            {
                const std::string id = grid_id(family, size);
                GridConfig config;
                config.width = size;
                config.height = size;
                config.family = family;
                config.obstacle_density =
                        family == GridFamily::Random
                            ? 0.20
                            : family == GridFamily::Clustered
                                  ? 0.30
                                  : 0.0;
                config.seed = seed ^ fnv1a64("graph_" + id);
                const Grid grid = make_grid(config);
                const std::vector<Vertex> component = largest_connected_component(grid.graph);
                if (component.size() < 10) continue;
                const std::uint64_t query_seed = seed ^ fnv1a64("query_" + id);
                const std::vector<Query> queries =
                        select_queries(grid.graph, component, per_band, query_seed);
                if (queries.empty()) continue;

                const double obstacle_density =
                        1.0 - static_cast<double>(grid.free_cells) /
                        (static_cast<double>(size) * size);

                ManhattanHeuristic manhattan(size);
                SearchScratch d_scratch(grid.graph.n, TieBreak::LargerG);
                SearchScratch a_scratch(grid.graph.n, TieBreak::LargerG);

                for (std::size_t qi = 0; qi < queries.size(); ++qi)
                {
                    const Query &query = queries[qi];
                    const std::vector<std::uint32_t> true_dist =
                            bfs_distances(grid.graph, query.target);
                    manhattan.prepare(query.target);

                    const std::size_t sample_size =
                            std::min<std::size_t>(5000, component.size());
                    std::vector<Vertex> sample;
                    sample.reserve(sample_size);
                    std::ranges::sample(component.begin(), component.end(),
                                std::back_inserter(sample), sample_size, sample_rng);

                    std::vector<double> h_values;
                    std::vector<double> d_values;
                    h_values.reserve(sample.size());
                    d_values.reserve(sample.size());
                    for (Vertex v: sample)
                    {
                        if (true_dist[v] == kInfiniteIndex) continue;
                        h_values.push_back(static_cast<double>(manhattan.estimate(v)));
                        d_values.push_back(static_cast<double>(true_dist[v]));
                    }
                    const SampleStats stats = compute_stats(h_values, d_values);

                    SearchOptions options;
                    options.tie = TieBreak::LargerG;
                    options.build_path = false;
                    const SearchResult d =
                            shortest_path(grid.graph, query.source, query.target, nullptr,
                                          options, d_scratch);
                    const SearchResult a =
                            shortest_path(grid.graph, query.source, query.target, &manhattan,
                                          options, a_scratch);
                    if (d.status != SearchStatus::Ok || a.status != SearchStatus::Ok)
                    {
                        throw std::runtime_error("diagnostics search failed");
                    }

                    const std::uint32_t s_row = query.source / size;
                    const std::uint32_t s_col = query.source % size;
                    const std::uint32_t t_row = query.target / size;
                    const std::uint32_t t_col = query.target % size;
                    const std::uint32_t manhattan_distance =
                            (s_row > t_row ? s_row - t_row : t_row - s_row) +
                            (s_col > t_col ? s_col - t_col : t_col - s_col);
                    const double detour = manhattan_distance == 0
                                              ? 0.0
                                              : static_cast<double>(query.oracle_distance) / manhattan_distance;

                    write_grid_row(out, id, family, size, size, obstacle_density,
                                   component.size(), static_cast<std::uint32_t>(qi),
                                   query, manhattan_distance, detour, stats,
                                   d.counters.expanded, a.counters.expanded);
                }
                std::cerr << "diagnostics " << id << ": " << queries.size()
                        << " queries\n";
            }
        }
        return 0;
    }

    int run_real_diagnostics(const std::string &output, const Graph &graph,
                             const LandmarkData &landmarks, std::uint32_t k,
                             std::uint32_t per_band, std::uint64_t seed)
    {
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open diagnostics output: " + output);
        write_real_header(out);

        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;
        const std::uint64_t query_seed = seed ^ fnv1a64("query_roadNet-CA");
        const std::vector<Query> queries =
                select_queries(graph, component, per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("no real diagnostics queries");

        ALTHeuristic alt(landmarks, k);
        SearchScratch d_scratch(graph.n, TieBreak::LargerG);
        SearchScratch a_scratch(graph.n, TieBreak::LargerG);
        std::mt19937_64 sample_rng(seed ^ fnv1a64("diagnostics_real_sample"));

        for (std::size_t qi = 0; qi < queries.size(); ++qi)
        {
            const Query &query = queries[qi];
            const std::vector<std::uint32_t> true_dist =
                    bfs_distances(graph, query.target);
            alt.prepare(query.target);

            const std::size_t sample_size = std::min<std::size_t>(5000, graph.n);
            std::vector<Vertex> sample;
            sample.reserve(sample_size);
            std::ranges::sample(component.begin(), component.end(),
                        std::back_inserter(sample), sample_size, sample_rng);

            std::vector<double> h_values;
            std::vector<double> d_values;
            h_values.reserve(sample.size());
            d_values.reserve(sample.size());
            for (Vertex v: sample)
            {
                if (true_dist[v] == kInfiniteIndex) continue;
                h_values.push_back(static_cast<double>(alt.estimate(v)));
                d_values.push_back(static_cast<double>(true_dist[v]));
            }
            const SampleStats stats = compute_stats(h_values, d_values);

            SearchOptions options;
            options.tie = TieBreak::LargerG;
            options.build_path = false;
            const SearchResult d =
                    shortest_path(graph, query.source, query.target, nullptr, options,
                                  d_scratch);
            const SearchResult a =
                    shortest_path(graph, query.source, query.target, &alt, options,
                                  a_scratch);
            if (d.status != SearchStatus::Ok || a.status != SearchStatus::Ok)
            {
                throw std::runtime_error("real diagnostics search failed");
            }
            write_real_row(out, static_cast<std::uint32_t>(qi), query, k, stats,
                           d.counters.expanded, a.counters.expanded);
            if ((qi + 1) % 5 == 0)
            {
                std::cerr << "real diagnostics: " << (qi + 1) << "/" << queries.size()
                        << "\n";
            }
        }
        return 0;
    }

    // Finer epsilon grid than the original maze-only sweep.  Small values are
    // needed to see the cost-speed knee on graphs that have several paths.
    constexpr double kWeightedEpsilons[] = {
        1.0, 1.1, 1.25, 1.5, 2.0, 3.0, 5.0, 10.0, 20.0, 50.0
    };

    static void write_weighted_header(std::ofstream &out)
    {
        out << "graph_id,family,size,pair_id,band,source,target,rep,epsilon,variant,"
                "time_ns,status,distance,optimal_distance,cost_ratio,expanded,pushed,"
                "popped,stale_popped,relaxed_edges,improved,heuristic_calls,heap_peak\n";
    }

    static void write_weighted_dijkstra(std::ofstream &out, const std::string &id,
                                 const std::string &family, std::uint32_t size,
                                 std::size_t qi, const Query &query,
                                 std::uint32_t rep, const TimedSearch &d)
    {
        out << id << ',' << family << ',' << size << ','
                << static_cast<std::uint32_t>(qi) << ',' << query.band << ','
                << query.source << ',' << query.target << ',' << rep
                << ",0,dijkstra," << d.time_ns << ",ok," << d.result.distance
                << ',' << query.oracle_distance << ",1,"
                << d.result.counters.expanded << ',' << d.result.counters.pushed
                << ',' << d.result.counters.popped << ','
                << d.result.counters.stale_popped << ','
                << d.result.counters.relaxed_edges << ','
                << d.result.counters.improved << ','
                << d.result.counters.heuristic_calls << ','
                << d.result.counters.heap_peak << '\n';
    }

    static void write_weighted_astar(std::ofstream &out, const std::string &id,
                              const std::string &family, std::uint32_t size,
                              std::size_t qi, const Query &query,
                              std::uint32_t rep, double eps,
                              const TimedSearch &a)
    {
        const bool ok = a.result.status == SearchStatus::Ok;
        const double cost_ratio =
                ok
                    ? static_cast<double>(a.result.distance) /
                      static_cast<double>(query.oracle_distance)
                    : 0.0;
        out << id << ',' << family << ',' << size << ','
                << static_cast<std::uint32_t>(qi) << ',' << query.band << ','
                << query.source << ',' << query.target << ',' << rep << ','
                << std::fixed << std::setprecision(2) << eps
                << ",weighted_astar," << a.time_ns << ','
                << status_name(a.result.status) << ',';
        out << (ok ? std::to_string(a.result.distance) : std::string("-1"));
        out << ',' << query.oracle_distance << ',' << cost_ratio << ','
                << a.result.counters.expanded << ','
                << a.result.counters.pushed << ','
                << a.result.counters.popped << ','
                << a.result.counters.stale_popped << ','
                << a.result.counters.relaxed_edges << ','
                << a.result.counters.improved << ','
                << a.result.counters.heuristic_calls << ','
                << a.result.counters.heap_peak << '\n';
    }

    int run_weighted_grid(const std::string &output,
                          const std::vector<std::uint32_t> &sizes,
                          const std::vector<GridFamily> &families,
                          std::uint32_t per_band, std::uint32_t reps,
                          std::uint64_t seed, bool allow_reopen)
    {
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open weighted output: " + output);
        write_weighted_header(out);

        for (GridFamily family: families)
        {
            for (std::uint32_t size: sizes)
            {
                const std::string family_name = grid_family_name(family);
                const std::string id = "grid_" + family_name + "_" +
                                       std::to_string(size);
                GridConfig config;
                config.width = size;
                config.height = size;
                config.family = family;
                config.obstacle_density =
                        family == GridFamily::Random
                            ? 0.20
                            : family == GridFamily::Clustered
                                  ? 0.30
                                  : 0.0;
                config.seed = seed ^ fnv1a64("graph_" + id);
                const Grid grid = make_grid(config);
                const std::vector<Vertex> component =
                        largest_connected_component(grid.graph);
                if (component.size() < 10) continue;
                const std::uint64_t query_seed = seed ^ fnv1a64("query_" + id);
                const std::vector<Query> queries =
                        select_queries(grid.graph, component, per_band, query_seed);
                if (queries.empty()) continue;

                ManhattanHeuristic manhattan(size);
                SearchScratch d_scratch(grid.graph.n, TieBreak::LargerG);
                SearchScratch a_scratch(grid.graph.n, TieBreak::LargerG);

                // Warm-up every treatment on the first query.
                SearchOptions warm;
                warm.tie = TieBreak::LargerG;
                (void) timed_search(grid.graph, queries[0].source, queries[0].target,
                                    nullptr, warm, d_scratch);
                for (double eps: kWeightedEpsilons)
                {
                    SearchOptions options;
                    options.tie = TieBreak::LargerG;
                    options.heuristic_weight = eps;
                    options.allow_reopen = allow_reopen;
                    (void) timed_search(grid.graph, queries[0].source,
                                        queries[0].target, &manhattan, options,
                                        a_scratch);
                }

                for (std::size_t qi = 0; qi < queries.size(); ++qi)
                {
                    const Query &query = queries[qi];
                    for (std::uint32_t rep = 1; rep <= reps; ++rep)
                    {
                        SearchOptions d_options;
                        d_options.tie = TieBreak::LargerG;
                        const TimedSearch d =
                                timed_search(grid.graph, query.source, query.target,
                                             nullptr, d_options, d_scratch);
                        if (d.result.status != SearchStatus::Ok ||
                            d.result.distance != query.oracle_distance)
                        {
                            throw std::runtime_error(
                                "weighted experiment: Dijkstra mismatch");
                        }
                        write_weighted_dijkstra(out, id, family_name, size, qi,
                                                query, rep, d);

                        for (double eps: kWeightedEpsilons)
                        {
                            SearchOptions options;
                            options.tie = TieBreak::LargerG;
                            options.heuristic_weight = eps;
                            options.allow_reopen = allow_reopen;
                            const TimedSearch a =
                                    timed_search(grid.graph, query.source,
                                                 query.target, &manhattan, options,
                                                 a_scratch);
                            if (a.result.status == SearchStatus::Ok &&
                                a.result.distance < query.oracle_distance)
                            {
                                throw std::runtime_error(
                                    "weighted A* beat the optimum");
                            }
                            if (eps == 1.0 &&
                                (a.result.status != SearchStatus::Ok ||
                                 a.result.distance != query.oracle_distance))
                            {
                                throw std::runtime_error(
                                    "epsilon=1 weighted A* is not optimal");
                            }
                            write_weighted_astar(out, id, family_name, size, qi,
                                                 query, rep, eps, a);
                        }
                    }
                }
                std::cerr << "weighted " << id << ": " << queries.size()
                        << " queries\n";
            }
        }
        return 0;
    }

    int run_weighted_maze(const std::string &output,
                          const std::vector<std::uint32_t> &sizes,
                          std::uint32_t per_band, std::uint32_t reps,
                          std::uint64_t seed)
    {
        return run_weighted_grid(output, sizes, {GridFamily::Maze}, per_band, reps,
                                 seed);
    }

    int run_weighted_real(const std::string &output, const Graph &graph,
                          const LandmarkData &landmarks, std::uint32_t k,
                          std::uint32_t per_band, std::uint32_t reps,
                          std::uint64_t seed, bool allow_reopen)
    {
        if (graph.n == 0 || landmarks.n != graph.n)
        {
            throw std::runtime_error("weighted real: graph/landmark size mismatch");
        }
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open weighted output: " + output);
        write_weighted_header(out);

        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;
        // Same seed phrase as the frozen real benchmark, so these are the same
        // 30 queries as results/real.csv.
        const std::uint64_t query_seed =
                seed ^ fnv1a64("query_roadNet-CA");
        const std::vector<Query> queries =
                select_queries(graph, component, per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("weighted real: no queries");

        ALTHeuristic alt(landmarks, k);
        SearchScratch d_scratch(graph.n, TieBreak::LargerG);
        SearchScratch a_scratch(graph.n, TieBreak::LargerG);

        SearchOptions warm;
        warm.tie = TieBreak::LargerG;
        (void) timed_search(graph, queries[0].source, queries[0].target, nullptr,
                            warm, d_scratch);
        for (double eps: kWeightedEpsilons)
        {
            SearchOptions options;
            options.tie = TieBreak::LargerG;
            options.heuristic_weight = eps;
            options.allow_reopen = allow_reopen;
            (void) timed_search(graph, queries[0].source, queries[0].target, &alt,
                                options, a_scratch);
        }

        const std::string id = "roadNet-CA";
        const std::string family = "road";
        for (std::size_t qi = 0; qi < queries.size(); ++qi)
        {
            const Query &query = queries[qi];
            for (std::uint32_t rep = 1; rep <= reps; ++rep)
            {
                SearchOptions d_options;
                d_options.tie = TieBreak::LargerG;
                const TimedSearch d =
                        timed_search(graph, query.source, query.target, nullptr,
                                     d_options, d_scratch);
                if (d.result.status != SearchStatus::Ok ||
                    d.result.distance != query.oracle_distance)
                {
                    throw std::runtime_error("weighted real: Dijkstra mismatch");
                }
                write_weighted_dijkstra(out, id, family, 0, qi, query, rep, d);

                for (double eps: kWeightedEpsilons)
                {
                    SearchOptions options;
                    options.tie = TieBreak::LargerG;
                    options.heuristic_weight = eps;
                    options.allow_reopen = allow_reopen;
                    const TimedSearch a =
                            timed_search(graph, query.source, query.target, &alt,
                                         options, a_scratch);
                    if (a.result.status == SearchStatus::Ok &&
                        a.result.distance < query.oracle_distance)
                    {
                        throw std::runtime_error("weighted real: A* beat the optimum");
                    }
                    if (eps == 1.0 &&
                        (a.result.status != SearchStatus::Ok ||
                         a.result.distance != query.oracle_distance))
                    {
                        throw std::runtime_error(
                            "weighted real: epsilon=1 is not optimal");
                    }
                    write_weighted_astar(out, id, family, 0, qi, query, rep, eps,
                                         a);
                }
            }
            if ((qi + 1) % 5 == 0)
            {
                std::cerr << "weighted real: " << (qi + 1) << "/" << queries.size()
                        << "\n";
            }
        }
        return 0;
    }
    int run_tie_plateau_grid(const std::string &output,
                             const std::vector<std::uint32_t> &sizes,
                             const std::vector<GridFamily> &families,
                             std::uint32_t per_band, std::uint64_t seed)
    {
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open plateau output: " + output);
        write_tie_plateau_header(out);

        for (GridFamily family: families)
        {
            for (std::uint32_t size: sizes)
            {
                const std::string id = grid_id(family, size);
                GridConfig config;
                config.width = size;
                config.height = size;
                config.family = family;
                config.obstacle_density =
                        family == GridFamily::Random ? 0.20
                        : family == GridFamily::Clustered ? 0.30
                                                          : 0.0;
                config.seed = seed ^ fnv1a64("graph_" + id);
                const Grid grid = make_grid(config);
                const std::vector<Vertex> component =
                        largest_connected_component(grid.graph);
                if (component.size() < 10) continue;
                const std::uint64_t query_seed = seed ^ fnv1a64("query_" + id);
                const std::vector<Query> queries =
                        select_queries(grid.graph, component, per_band, query_seed);
                if (queries.empty()) continue;

                ManhattanHeuristic manhattan(size);
                SearchScratch node_scratch(grid.graph.n, TieBreak::NodeId);
                SearchScratch g_scratch(grid.graph.n, TieBreak::LargerG);

                for (std::size_t qi = 0; qi < queries.size(); ++qi)
                {
                    const Query &query = queries[qi];
                    SearchOptions node_options;
                    node_options.tie = TieBreak::NodeId;
                    SearchOptions g_options;
                    g_options.tie = TieBreak::LargerG;

                    const TimedSearch node =
                            timed_search(grid.graph, query.source, query.target,
                                         &manhattan, node_options, node_scratch);
                    const TimedSearch large_g =
                            timed_search(grid.graph, query.source, query.target,
                                         &manhattan, g_options, g_scratch);
                    if (node.result.status != SearchStatus::Ok ||
                        large_g.result.status != SearchStatus::Ok ||
                        node.result.distance != query.oracle_distance ||
                        large_g.result.distance != query.oracle_distance)
                    {
                        throw std::runtime_error("tie plateau: grid result mismatch");
                    }
                    write_tie_plateau_row(out, "grid", id, grid_family_name(family),
                                          size, qi, query, "node_id", node);
                    write_tie_plateau_row(out, "grid", id, grid_family_name(family),
                                          size, qi, query, "larger_g", large_g);
                }
                std::cerr << "tie plateau " << id << ": " << queries.size()
                          << " queries\n";
            }
        }
        return 0;
    }

    int run_tie_plateau_real(const std::string &output, const Graph &graph,
                             const LandmarkData &landmarks, std::uint32_t k,
                             std::uint32_t per_band, std::uint64_t seed)
    {
        if (graph.n == 0 || landmarks.n != graph.n)
        {
            throw std::runtime_error("tie plateau: graph/landmark size mismatch");
        }
        std::ofstream out(output);
        if (!out) throw std::runtime_error("cannot open plateau output: " + output);
        write_tie_plateau_header(out);

        std::vector<Vertex> component(graph.n);
        for (Vertex v = 0; v < graph.n; ++v) component[v] = v;
        const std::uint64_t query_seed = seed ^ fnv1a64("query_roadNet-CA");
        const std::vector<Query> queries =
                select_queries(graph, component, per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("tie plateau: no queries");

        ALTHeuristic alt(landmarks, k);
        SearchScratch node_scratch(graph.n, TieBreak::NodeId);
        SearchScratch g_scratch(graph.n, TieBreak::LargerG);
        const std::string id = "roadNet-CA";

        for (std::size_t qi = 0; qi < queries.size(); ++qi)
        {
            const Query &query = queries[qi];
            SearchOptions node_options;
            node_options.tie = TieBreak::NodeId;
            SearchOptions g_options;
            g_options.tie = TieBreak::LargerG;

            const TimedSearch node =
                    timed_search(graph, query.source, query.target, &alt,
                                 node_options, node_scratch);
            const TimedSearch large_g =
                    timed_search(graph, query.source, query.target, &alt,
                                 g_options, g_scratch);
            if (node.result.status != SearchStatus::Ok ||
                large_g.result.status != SearchStatus::Ok ||
                node.result.distance != query.oracle_distance ||
                large_g.result.distance != query.oracle_distance)
            {
                throw std::runtime_error("tie plateau: road result mismatch");
            }
            write_tie_plateau_row(out, "real", id, "road", 0, qi, query,
                                  "node_id", node);
            write_tie_plateau_row(out, "real", id, "road", 0, qi, query,
                                  "larger_g", large_g);
            if ((qi + 1) % 5 == 0)
            {
                std::cerr << "tie plateau real: " << (qi + 1) << "/"
                          << queries.size() << "\n";
            }
        }
        return 0;
    }

} // namespace astar
