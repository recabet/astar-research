#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common.hpp"
#include "experiments.hpp"
#include "graph.hpp"
#include "grid.hpp"
#include "heuristic.hpp"
#include "novelty.hpp"
#include "omp.hpp"
#include "oracle.hpp"
#include "preprocess.hpp"

namespace
{
    using namespace astar;

    std::vector<std::string> split(const std::string &text, char separator)
    {
        std::vector<std::string> parts;
        std::string current;
        std::istringstream stream(text);
        while (std::getline(stream, current, separator))
        {
            if (!current.empty()) parts.push_back(current);
        }
        return parts;
    }

    std::uint64_t parse_u64(const std::string &value, const std::string &name)
    {
        std::size_t used = 0;
        const std::uint64_t result = std::stoull(value, &used);
        if (used != value.size()) throw std::runtime_error("bad value for " + name + ": " + value);
        return result;
    }

    std::uint32_t parse_u32(const std::string &value, const std::string &name)
    {
        const std::uint64_t result = parse_u64(value, name);
        if (result > std::numeric_limits<std::uint32_t>::max())
        {
            throw std::runtime_error("value too large for " + name);
        }
        return static_cast<std::uint32_t>(result);
    }

    double parse_double(const std::string &value, const std::string &name)
    {
        std::size_t used = 0;
        const double result = std::stod(value, &used);
        if (used != value.size()) throw std::runtime_error("bad value for " + name + ": " + value);
        return result;
    }

    std::map<std::string, std::string> parse_options(int argc, char** argv, int start)
    {
        std::map<std::string, std::string> options;
        for (int i = start; i < argc; ++i)
        {
            std::string key = argv[i];
            if (key.rfind("--", 0) != 0)
            {
                throw std::runtime_error("expected --option, got: " + key);
            }
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + key);
            options[key] = argv[++i];
        }
        return options;
    }

    std::string get_option(const std::map<std::string, std::string> &options,
                           const std::string &key, const std::string &fallback)
    {
        const auto it = options.find(key);
        return it == options.end() ? fallback : it->second;
    }

    void ensure_parent_directory(const std::string &path)
    {
        const std::filesystem::path p(path);
        if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    }

    void print_usage()
    {
        std::cout <<
                "astar_research commands:\n"
                "  preprocess-real --input FILE --out-graph FILE [--out-landmarks FILE]\n"
                "                  [--landmarks K] [--seed N]\n"
                "  preprocess-dimacs --gr FILE --co FILE --out-graph FILE\n"
                "                  --out-landmarks FILE --out-coords FILE [--landmarks K]\n"
                "  graph-info --graph FILE\n"
                "  benchmark-grid --out FILE [--sizes 128,256,512,1024]\n"
                "                  [--families open,random,clustered,rooms,maze]\n"
                "                  [--reps 3] [--per-band 5] [--seed N]\n"
                "                  [--random-density 0.20] [--clustered-density 0.30]\n"
                "                  [--time-limit-s 30]\n"
                "  benchmark-real --graph FILE --landmarks FILE --out FILE\n"
                "                  [--reps 5] [--per-band 10] [--seed N]\n"
                "                  [--main-k 8] [--time-limit-s 30]\n"
                "  benchmark-weighted-road --graph FILE --landmarks FILE --coords FILE\n"
                "                  --out FILE [--reps 5] [--per-band 10] [--main-k 8]\n"
                "  weighted-road-eps --graph FILE --landmarks FILE --coords FILE\n"
                "                  --out FILE [--reps 5] [--per-band 10] [--main-k 8]\n"
                "  benchmark-unit-road --graph WEIGHTED_GRAPH --queries FILE --out FILE\n"
                "                  [--reps 5] [--main-k 8]\n"
                "  validate-real --graph FILE --landmarks FILE [--samples 30]\n"
                "                  [--main-k 8] [--seed N]\n"
                "  diagnose-grid --out FILE [--sizes ...] [--families ...]\n"
                "  diagnose-real --graph FILE --landmarks FILE --out FILE [--main-k 8]\n"
                "  weighted-maze --out FILE [--sizes ...] [--reps 3]\n"
                "  weighted-grid --out FILE [--sizes ...] [--families ...] [--reps 3]\n"
                "                [--reopen 1]\n"
                "  weighted-real --graph G --landmarks L --out FILE [--main-k 8]\n"
                "                 [--reopen 1] [--per-band 10] [--reps 5]\n"
                "  tie-plateau-grid --out FILE [--sizes ...] [--families ...]\n"
                "  tie-plateau-real --graph G --landmarks L --out FILE [--main-k 8]\n"
                "  omp-scaling --graph FILE --landmarks FILE [--out-batch FILE]\n"
                "                  [--out-tasks FILE] [--sizes 512] [--threads ...]\n";
    }
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 2)
        {
            print_usage();
            return 1;
        }
        const std::string command = argv[1];

        if (command == "help" || command == "--help" || command == "-h")
        {
            print_usage();
            return 0;
        }

        if (command == "preprocess-real")
        {
            const auto options = parse_options(argc, argv, 2);
            PreprocessOptions config;
            config.input = get_option(options, "--input", "");
            config.output_graph = get_option(options, "--out-graph", "");
            config.output_landmarks = get_option(options, "--out-landmarks", "");
            config.landmarks = static_cast<std::uint32_t>(
                parse_u64(get_option(options, "--landmarks", "16"), "--landmarks"));
            config.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            return preprocess_real_graph(config);
        }

        if (command == "preprocess-dimacs")
        {
            const auto options = parse_options(argc, argv, 2);
            DimacsPreprocessOptions config;
            config.input_gr = get_option(options, "--gr", "");
            config.input_co = get_option(options, "--co", "");
            config.output_graph = get_option(options, "--out-graph", "");
            config.output_landmarks = get_option(options, "--out-landmarks", "");
            config.output_coords = get_option(options, "--out-coords", "");
            config.landmarks = static_cast<std::uint32_t>(
                parse_u64(get_option(options, "--landmarks", "16"), "--landmarks"));
            config.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            return preprocess_dimacs_graph(config);
        }

        if (command == "diagnose-grid")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string out = get_option(options, "--out", "results/grid_diagnostics.csv");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "128,256,512,1024"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            std::vector<GridFamily> families;
            for (const std::string &token: split(
                     get_option(options, "--families", "open,random,clustered,rooms,maze"),
                     ','))
            {
                families.push_back(parse_grid_family(token));
            }
            ensure_parent_directory(out);
            return run_grid_diagnostics(out, sizes, families, per_band, seed);
        }

        if (command == "diagnose-real")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error("diagnose-real needs --graph and --landmarks");
            }
            const std::string out =
                    get_option(options, "--out", "results/real_diagnostics.csv");
            const std::uint32_t k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error("landmark count/table does not match graph");
            }
            ensure_parent_directory(out);
            return run_real_diagnostics(out, graph, landmarks, k, per_band, seed);
        }

        if (command == "weighted-maze")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string out =
                    get_option(options, "--out", "results/weighted_maze.csv");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            const std::uint32_t reps =
                    parse_u32(get_option(options, "--reps", "3"), "--reps");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "128,256,512,1024"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            ensure_parent_directory(out);
            return run_weighted_maze(out, sizes, per_band, reps, seed);
        }

        if (command == "weighted-grid")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string out =
                    get_option(options, "--out", "results/weighted_families.csv");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            const std::uint32_t reps =
                    parse_u32(get_option(options, "--reps", "3"), "--reps");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const bool allow_reopen =
                    get_option(options, "--reopen", "1") != "0";
            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "128,256,512,1024"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            std::vector<GridFamily> families;
            for (const std::string &token: split(
                     get_option(options, "--families",
                                "open,random,clustered,rooms,maze"),
                     ','))
            {
                families.push_back(parse_grid_family(token));
            }
            ensure_parent_directory(out);
            return run_weighted_grid(out, sizes, families, per_band, reps,
                                     seed, allow_reopen);
        }

        if (command == "weighted-real")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path =
                    get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error(
                    "weighted-real needs --graph and --landmarks");
            }
            const std::string out =
                    get_option(options, "--out", "results/weighted_real.csv");
            const std::uint32_t k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            const std::uint32_t reps =
                    parse_u32(get_option(options, "--reps", "5"), "--reps");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const bool allow_reopen =
                    get_option(options, "--reopen", "1") != "0";
            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error(
                    "landmark count/table does not match graph");
            }
            ensure_parent_directory(out);
            return run_weighted_real(out, graph, landmarks, k, per_band, reps,
                                     seed, allow_reopen);
        }

        if (command == "tie-plateau-grid")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string out =
                    get_option(options, "--out", "results/tie_plateau_grid.csv");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "128,256,512,1024"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            std::vector<GridFamily> families;
            for (const std::string &token: split(
                     get_option(options, "--families",
                                "open,random,clustered,rooms,maze"),
                     ','))
            {
                families.push_back(parse_grid_family(token));
            }
            ensure_parent_directory(out);
            return run_tie_plateau_grid(out, sizes, families, per_band, seed);
        }

        if (command == "tie-plateau-real")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path =
                    get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error(
                    "tie-plateau-real needs --graph and --landmarks");
            }
            const std::string out =
                    get_option(options, "--out", "results/tie_plateau_real.csv");
            const std::uint32_t k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error(
                    "landmark count/table does not match graph");
            }
            ensure_parent_directory(out);
            return run_tie_plateau_real(out, graph, landmarks, k, per_band, seed);
        }

        if (command == "omp-scaling")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error("omp-scaling needs --graph and --landmarks");
            }
            const std::string batch_out =
                    get_option(options, "--out-batch", "results/omp_scaling.csv");
            const std::string task_out =
                    get_option(options, "--out-tasks", "results/omp_tasks.csv");
            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "512"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            std::vector<int> threads;
            for (const std::string &token: split(
                     get_option(options, "--threads", "1,2,4,8,16"), ','))
            {
                threads.push_back(static_cast<int>(parse_u32(token, "--threads")));
            }
            const std::uint32_t per_band =
                    parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            const std::uint32_t reps =
                    parse_u32(get_option(options, "--reps", "3"), "--reps");
            const std::uint32_t batch_reps =
                    parse_u32(get_option(options, "--batch-reps", "3"), "--batch-reps");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error("landmark count/table does not match graph");
            }
            ensure_parent_directory(batch_out);
            ensure_parent_directory(task_out);
            return run_omp_scaling(batch_out, task_out, graph, landmarks, sizes,
                                   threads, per_band, reps, batch_reps, seed);
        }

        if (command == "graph-info")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string path = get_option(options, "--graph", "");
            if (path.empty()) throw std::runtime_error("missing --graph");
            const Graph graph = load_graph_binary(path);
            std::uint64_t min_degree = std::numeric_limits<std::uint64_t>::max();
            std::uint64_t max_degree = 0;
            for (Vertex v = 0; v < graph.n; ++v)
            {
                const std::uint64_t d = graph.degree(v);
                min_degree = std::min(min_degree, d);
                max_degree = std::max(max_degree, d);
            }
            const std::vector<Vertex> component = largest_connected_component(graph);
            std::cout << "file: " << path << "\n"
                    << "vertices: " << graph.n << "\n"
                    << "arcs: " << graph.num_arcs() << "\n"
                    << "average_degree: "
                    << (graph.n == 0 ? 0.0 : static_cast<double>(graph.num_arcs()) / graph.n)
                    << "\n"
                    << "min_degree: " << (graph.n == 0 ? 0 : min_degree) << "\n"
                    << "max_degree: " << max_degree << "\n"
                    << "largest_component: " << component.size() << "\n";
            return 0;
        }

        if (command == "benchmark-grid")
        {
            const auto options = parse_options(argc, argv, 2);
            BenchmarkOptions bench;
            bench.output = get_option(options, "--out", "results/grid.csv");
            bench.reps = parse_u32(get_option(options, "--reps", "3"), "--reps");
            bench.per_band = parse_u32(get_option(options, "--per-band", "5"), "--per-band");
            bench.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            bench.time_limit_ns = static_cast<std::uint64_t>(
                parse_double(get_option(options, "--time-limit-s", "30"), "--time-limit-s") *
                1e9);

            std::vector<std::uint32_t> sizes;
            for (const std::string &token: split(
                     get_option(options, "--sizes", "128,256,512,1024"), ','))
            {
                sizes.push_back(parse_u32(token, "--sizes"));
            }
            std::vector<GridFamily> families;
            for (const std::string &token: split(
                     get_option(options, "--families", "open,random,clustered,rooms,maze"),
                     ','))
            {
                families.push_back(parse_grid_family(token));
            }
            const double random_density =
                    parse_double(get_option(options, "--random-density", "0.20"),
                                 "--random-density");
            const double clustered_density =
                    parse_double(get_option(options, "--clustered-density", "0.30"),
                                 "--clustered-density");

            ensure_parent_directory(bench.output);
            return run_grid_benchmark(bench, sizes, families, random_density,
                                      clustered_density);
        }

        if (command == "benchmark-real")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error("benchmark-real needs --graph and --landmarks");
            }
            BenchmarkOptions bench;
            bench.output = get_option(options, "--out", "results/real.csv");
            bench.reps = parse_u32(get_option(options, "--reps", "5"), "--reps");
            bench.per_band = parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            bench.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            bench.time_limit_ns = static_cast<std::uint64_t>(
                parse_double(get_option(options, "--time-limit-s", "30"), "--time-limit-s") *
                1e9);
            const std::uint32_t main_k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");

            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error("landmark count/table does not match graph");
            }
            if (landmarks.k < 16)
            {
                throw std::runtime_error("benchmark-real expects at least 16 landmarks");
            }
            ensure_parent_directory(bench.output);
            return run_real_benchmark(bench, graph, landmarks, main_k);
        }

        if (command == "benchmark-weighted-road")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            const std::string coord_path = get_option(options, "--coords", "");
            if (graph_path.empty() || landmark_path.empty() || coord_path.empty())
            {
                throw std::runtime_error(
                    "benchmark-weighted-road needs --graph, --landmarks and --coords");
            }
            BenchmarkOptions bench;
            bench.output = get_option(options, "--out", "results/weighted_road.csv");
            bench.reps = parse_u32(get_option(options, "--reps", "5"), "--reps");
            bench.per_band = parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            bench.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            bench.time_limit_ns = static_cast<std::uint64_t>(
                parse_double(get_option(options, "--time-limit-s", "30"), "--time-limit-s") *
                1e9);
            const std::uint32_t main_k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");

            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            const Coordinates coordinates = load_coordinates_binary(coord_path);
            if (landmarks.n != graph.n || coordinates.n != graph.n)
            {
                throw std::runtime_error(
                    "graph, landmark and coordinate sizes do not match");
            }
            if (landmarks.k < 16)
            {
                throw std::runtime_error(
                    "benchmark-weighted-road expects at least 16 landmarks");
            }
            ensure_parent_directory(bench.output);
            return run_weighted_road_benchmark(bench, graph, landmarks, coordinates,
                                               main_k);
        }

        if (command == "weighted-road-eps")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            const std::string coord_path = get_option(options, "--coords", "");
            if (graph_path.empty() || landmark_path.empty() || coord_path.empty())
            {
                throw std::runtime_error(
                    "weighted-road-eps needs --graph, --landmarks and --coords");
            }
            BenchmarkOptions bench;
            bench.output = get_option(options, "--out", "results/weighted_road_eps.csv");
            bench.reps = parse_u32(get_option(options, "--reps", "5"), "--reps");
            bench.per_band = parse_u32(get_option(options, "--per-band", "10"), "--per-band");
            bench.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            bench.time_limit_ns = static_cast<std::uint64_t>(
                parse_double(get_option(options, "--time-limit-s", "30"), "--time-limit-s") *
                1e9);
            const std::uint32_t main_k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");

            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            const Coordinates coordinates = load_coordinates_binary(coord_path);
            if (landmarks.n != graph.n || coordinates.n != graph.n)
            {
                throw std::runtime_error(
                    "graph, landmark and coordinate sizes do not match");
            }
            ensure_parent_directory(bench.output);
            return run_weighted_road_epsilon(bench, graph, landmarks, coordinates,
                                             main_k);
        }

        if (command == "benchmark-unit-road")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string query_file =
                    get_option(options, "--queries", "results/weighted_road.csv");
            if (graph_path.empty())
            {
                throw std::runtime_error("benchmark-unit-road needs --graph");
            }
            BenchmarkOptions bench;
            bench.output = get_option(options, "--out", "results/unit_road.csv");
            bench.reps = parse_u32(get_option(options, "--reps", "5"), "--reps");
            bench.seed = parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            bench.time_limit_ns = static_cast<std::uint64_t>(
                parse_double(get_option(options, "--time-limit-s", "30"), "--time-limit-s") *
                1e9);
            const std::uint32_t main_k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");
            const Graph graph = load_graph_binary(graph_path);
            ensure_parent_directory(bench.output);
            return run_unit_road_benchmark(bench, graph, query_file, main_k);
        }

        if (command == "validate-real")
        {
            const auto options = parse_options(argc, argv, 2);
            const std::string graph_path = get_option(options, "--graph", "");
            const std::string landmark_path = get_option(options, "--landmarks", "");
            if (graph_path.empty() || landmark_path.empty())
            {
                throw std::runtime_error("validate-real needs --graph and --landmarks");
            }
            const std::uint32_t samples =
                    parse_u32(get_option(options, "--samples", "30"), "--samples");
            const std::uint32_t main_k =
                    parse_u32(get_option(options, "--main-k", "8"), "--main-k");
            const std::uint64_t seed =
                    parse_u64(get_option(options, "--seed", "20261030"), "--seed");
            const Graph graph = load_graph_binary(graph_path);
            const LandmarkData landmarks = load_landmarks_binary(landmark_path);
            if (landmarks.n != graph.n)
            {
                throw std::runtime_error("landmark count/table does not match graph");
            }
            return run_validation(graph, &landmarks, main_k, samples, seed);
        }

        std::cerr << "unknown command: " << command << "\n";
        print_usage();
        return 1;
    } catch (const std::exception &error)
    {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
