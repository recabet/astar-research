#include "omp.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <omp.h>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "experiments.hpp"
#include "grid.hpp"
#include "oracle.hpp"
#include "search.hpp"

namespace astar
{
    namespace
    {
        struct TaskRecord {
            std::uint32_t pair_id = 0;
            std::uint32_t rep = 0;
            std::uint64_t time_d = 0;
            std::uint64_t time_a = 0;
            std::uint64_t expanded_d = 0;
            std::uint64_t expanded_a = 0;
            SearchStatus status_d = SearchStatus::NoPath;
            SearchStatus status_a = SearchStatus::NoPath;
        };

        struct BatchResult {
            std::vector<TaskRecord> tasks;
            std::uint64_t batch_time_ns = 0;
            std::uint64_t total_d_time_ns = 0;
            std::uint64_t total_a_time_ns = 0;
        };

        std::uint64_t elapsed_ns(std::chrono::steady_clock::time_point start,
                                 std::chrono::steady_clock::time_point stop)
        {
            return static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start).count());
        }

        BatchResult run_batch(
            const Graph &graph, const std::vector<Query> &queries, std::uint32_t reps,
            int threads, std::uint64_t time_limit_ns,
            const std::vector<std::unique_ptr<SearchScratch> > &scratches,
            const std::vector<std::unique_ptr<Heuristic> > &heuristics)
        {
            BatchResult batch;
            const std::size_t task_count = queries.size() * static_cast<std::size_t>(reps);
            batch.tasks.resize(task_count);

            const double start = omp_get_wtime();
#pragma omp parallel  num_threads(threads)
            {
                SearchScratch &scratch = *scratches[omp_get_thread_num()];
                Heuristic* heuristic = heuristics[omp_get_thread_num()].get();

#pragma omp for schedule(dynamic, 1)
                for (std::int64_t task = 0; task < static_cast<std::int64_t>(task_count);
                     ++task)
                {
                    const std::size_t query_id =
                            static_cast<std::size_t>(task) / reps;
                    const std::uint32_t rep =
                            static_cast<std::uint32_t>(static_cast<std::size_t>(task) % reps) + 1;
                    const Query &query = queries[query_id];

                    SearchOptions options;
                    options.tie = TieBreak::LargerG;
                    options.build_path = false;
                    options.time_limit_ns = time_limit_ns;

                    const auto d_start = std::chrono::steady_clock::now();
                    const SearchResult d = shortest_path(graph, query.source, query.target,
                                                         nullptr, options, scratch);
                    const auto d_stop = std::chrono::steady_clock::now();

                    const auto a_start = std::chrono::steady_clock::now();
                    const SearchResult a = shortest_path(graph, query.source, query.target,
                                                         heuristic, options, scratch);
                    const auto a_stop = std::chrono::steady_clock::now();

                    TaskRecord &record = batch.tasks[static_cast<std::size_t>(task)];
                    record.pair_id = static_cast<std::uint32_t>(query_id);
                    record.rep = rep;
                    record.time_d = elapsed_ns(d_start, d_stop);
                    record.time_a = elapsed_ns(a_start, a_stop);
                    record.expanded_d = d.counters.expanded;
                    record.expanded_a = a.counters.expanded;
                    record.status_d = d.status;
                    record.status_a = a.status;
                }
            }
            const double stop = omp_get_wtime();
            batch.batch_time_ns = static_cast<std::uint64_t>((stop - start) * 1e9);
            for (const TaskRecord &record: batch.tasks)
            {
                batch.total_d_time_ns += record.time_d;
                batch.total_a_time_ns += record.time_a;
            }
            return batch;
        }

        void write_batch_row(std::ofstream &out, const std::string &graph_id,
                             const std::string &family, const Graph &graph,
                             int threads, std::uint32_t batch_rep,
                             const std::vector<Query> &queries, const BatchResult &batch)
        {
            const double seconds = static_cast<double>(batch.batch_time_ns) / 1e9;
            const double throughput = seconds > 0.0
                                          ? static_cast<double>(batch.tasks.size()) / seconds
                                          : 0.0;
            out << graph_id << ',' << family << ',' << graph.n << ',' << graph.num_arcs()
                    << ',' << threads << ',' << batch_rep << ',' << queries.size() << ','
                    << batch.tasks.size() << ',' << batch.batch_time_ns << ','
                    << batch.total_d_time_ns << ',' << batch.total_a_time_ns << ','
                    << std::fixed << std::setprecision(3) << throughput << '\n';
        }

        void write_task_rows(std::ofstream &out, const std::string &graph_id,
                             const std::string &family, int threads,
                             std::uint32_t batch_rep, const std::vector<Query> &queries,
                             const BatchResult &batch)
        {
            for (const TaskRecord &record: batch.tasks)
            {
                const Query &query = queries[record.pair_id];
                out << graph_id << ',' << family << ',' << threads << ',' << batch_rep
                        << ',' << record.pair_id << ',' << record.rep << ',' << query.band
                        << ',' << query.source << ',' << query.target << ',' << record.time_d
                        << ',' << record.time_a << ',' << record.expanded_d << ','
                        << record.expanded_a << ',' << status_name(record.status_d) << ','
                        << status_name(record.status_a) << '\n';
            }
        }

        void run_graph_batches(const Graph &graph, const std::string &graph_id,
                               const std::string &family, const std::vector<Query> &queries,
                               bool grid_mode, std::uint32_t grid_width,
                               const LandmarkData* landmarks, std::uint32_t landmark_k,
                               const std::vector<int> &thread_counts,
                               std::uint32_t reps, std::uint32_t batch_reps,
                               std::ofstream &batch_out, std::ofstream &task_out,
                               std::uint64_t time_limit_ns)
        {
            const int max_threads = omp_get_max_threads();
            for (int threads: thread_counts)
            {
                if (threads > max_threads)
                {
                    std::cerr << "warning: requested " << threads << " threads, runtime max is "
                            << max_threads << "; skipping\n";
                    continue;
                }

                std::vector<std::unique_ptr<SearchScratch> > scratches;
                std::vector<std::unique_ptr<Heuristic> > heuristics;
                scratches.reserve(static_cast<std::size_t>(threads));
                heuristics.reserve(static_cast<std::size_t>(threads));
                for (int i = 0; i < threads; ++i)
                {
                    scratches.push_back(std::make_unique<SearchScratch>(
                        graph.n, TieBreak::LargerG));
                    if (grid_mode)
                    {
                        heuristics.push_back(
                            std::make_unique<ManhattanHeuristic>(grid_width));
                    }
                    else
                    {
                        heuristics.push_back(
                            std::make_unique<ALTHeuristic>(*landmarks, landmark_k));
                    }
                }

                // One untimed warm-up per thread count.
                (void) run_batch(graph, queries, reps, threads, time_limit_ns, scratches,
                                 heuristics);

                for (std::uint32_t batch_rep = 1; batch_rep <= batch_reps; ++batch_rep)
                {
                    const BatchResult batch = run_batch(
                        graph, queries, reps, threads, time_limit_ns, scratches,
                        heuristics);
                    write_batch_row(batch_out, graph_id, family, graph, threads, batch_rep,
                                    queries, batch);
                    write_task_rows(task_out, graph_id, family, threads, batch_rep,
                                    queries, batch);
                }
                std::cerr << "  " << graph_id << " threads=" << threads << " done\n";
            }
        }
    } // namespace

    int run_omp_scaling(const std::string &batch_output,
                        const std::string &task_output,
                        const Graph &real_graph,
                        const LandmarkData &landmarks,
                        const std::vector<std::uint32_t> &grid_sizes,
                        const std::vector<int> &thread_counts,
                        std::uint32_t per_band, std::uint32_t reps,
                        std::uint32_t batch_reps, std::uint64_t seed)
    {
        if (real_graph.n == 0 || landmarks.n != real_graph.n)
        {
            throw std::runtime_error("omp scaling: graph/landmark mismatch");
        }
        omp_set_dynamic(0);

        std::ofstream batch_out(batch_output);
        std::ofstream task_out(task_output);
        if (!batch_out || !task_out)
        {
            throw std::runtime_error("cannot open OpenMP output files");
        }
        batch_out << "graph_id,family,n,m_arcs,threads,batch_rep,queries,tasks,"
                "batch_time_ns,total_d_time_ns,total_a_time_ns,"
                "throughput_queries_per_second\n";
        task_out << "graph_id,family,threads,batch_rep,pair_id,rep,band,source,target,"
                "time_d,time_a,expanded_d,expanded_a,status_d,status_a\n";

        const std::uint64_t time_limit_ns = 30ull * 1000ull * 1000ull * 1000ull;

        for (std::uint32_t size: grid_sizes)
        {
            for (GridFamily family: {
                     GridFamily::Open, GridFamily::Random,
                     GridFamily::Clustered, GridFamily::Rooms,
                     GridFamily::Maze
                 })
            {
                const std::string graph_id =
                        "grid_" + grid_family_name(family) + "_" + std::to_string(size);
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
                config.seed = seed ^ fnv1a64("graph_" + graph_id);
                const Grid grid = make_grid(config);
                const std::vector<Vertex> component =
                        largest_connected_component(grid.graph);
                if (component.size() < 10) continue;
                const std::uint64_t query_seed = seed ^ fnv1a64("query_" + graph_id);
                const std::vector<Query> queries =
                        select_queries(grid.graph, component, per_band, query_seed);
                if (queries.empty()) continue;

                std::cerr << "OpenMP batch " << graph_id << " (" << queries.size()
                        << " queries, " << reps << " reps)\n";
                run_graph_batches(grid.graph, graph_id, grid_family_name(family),
                                  queries, true, size, nullptr, 0, thread_counts, reps,
                                  batch_reps, batch_out, task_out, time_limit_ns);
            }
        }

        std::vector<Vertex> component(real_graph.n);
        for (Vertex v = 0; v < real_graph.n; ++v) component[v] = v;
        const std::uint64_t query_seed = seed ^ fnv1a64("query_roadNet-CA");
        const std::vector<Query> queries =
                select_queries(real_graph, component, per_band, query_seed);
        if (queries.empty()) throw std::runtime_error("no OpenMP real queries");
        std::cerr << "OpenMP batch roadNet-CA (" << queries.size() << " queries, "
                << reps << " reps)\n";
        run_graph_batches(real_graph, "roadNet-CA", "road", queries, false, 0,
                          &landmarks, 8, thread_counts, reps, batch_reps, batch_out,
                          task_out, time_limit_ns);

        return 0;
    }
} // namespace astar
