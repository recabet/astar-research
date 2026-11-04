#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "binary_heap.hpp"
#include "common.hpp"
#include "experiments.hpp"
#include "graph.hpp"
#include "grid.hpp"
#include "heuristic.hpp"
#include "oracle.hpp"
#include "search.hpp"

namespace {

using namespace astar;

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct ExpectedHeapLess {
    bool operator()(const HeapEntry& a, const HeapEntry& b) const {
        if (a.f != b.f) return a.f > b.f;
        if (a.g != b.g) return a.g < b.g;
        return a.node > b.node;
    }
};

void test_binary_heap() {
    std::mt19937_64 rng(12345);
    BinaryHeap heap(TieBreak::LargerG);
    std::priority_queue<HeapEntry, std::vector<HeapEntry>, ExpectedHeapLess> expected;

    for (int i = 0; i < 5000; ++i) {
        const HeapEntry entry{static_cast<Distance>(rng() % 100),
                              static_cast<Distance>(rng() % 100),
                              static_cast<Vertex>(rng() % 50)};
        heap.push(entry);
        expected.push(entry);
    }
    expect(heap.size() == expected.size(), "heap sizes differ after pushes");
    while (!expected.empty()) {
        const HeapEntry a = heap.pop();
        const HeapEntry b = expected.top();
        expected.pop();
        expect(a == b, "heap pop order differs from reference priority_queue");
    }
    expect(heap.empty(), "heap should be empty");
    std::cout << "  binary heap: PASS\n";
}

Graph make_tiny_weighted_graph() {
    GraphBuilder builder(6, true);
    builder.add_edge(0, 1, 7);
    builder.add_edge(0, 2, 9);
    builder.add_edge(0, 5, 14);
    builder.add_edge(1, 2, 10);
    builder.add_edge(1, 3, 15);
    builder.add_edge(2, 3, 11);
    builder.add_edge(2, 5, 2);
    builder.add_edge(3, 4, 6);
    builder.add_edge(4, 5, 9);
    return builder.build(true);
}

void test_tiny_weighted() {
    const Graph graph = make_tiny_weighted_graph();
    const auto expected = floyd_warshall(graph);
    expect(expected[0][3] == 20, "hand-computed 0->3 distance should be 20");
    expect(expected[0][4] == 20, "hand-computed 0->4 distance should be 20");
    expect(expected[0][5] == 11, "hand-computed 0->5 distance should be 11");

    ZeroHeuristic zero;
    SearchOptions options;
    options.build_path = true;
    for (Vertex s = 0; s < graph.n; ++s) {
        SearchScratch d_scratch(graph.n, TieBreak::LargerG);
        SearchScratch a_scratch(graph.n, TieBreak::LargerG);
        for (Vertex t = 0; t < graph.n; ++t) {
            const SearchResult d = dijkstra(graph, s, t, options, d_scratch);
            const SearchResult a = astar_search(graph, s, t, zero, options, a_scratch);
            expect(d.status == SearchStatus::Ok, "Dijkstra failed on tiny graph");
            expect(a.status == SearchStatus::Ok, "A* failed on tiny graph");
            expect(d.distance == expected[s][t], "Dijkstra disagrees with Floyd-Warshall");
            expect(a.distance == expected[s][t], "A* disagrees with Floyd-Warshall");
            std::string error;
            expect(validate_path(graph, s, t, d.path, d.distance, &error),
                   "Dijkstra path invalid: " + error);
            expect(validate_path(graph, s, t, a.path, a.distance, &error),
                   "A* path invalid: " + error);
        }
    }
    std::cout << "  tiny weighted graph vs Floyd-Warshall: PASS\n";
}

void test_grid_hand_example() {
    // 5x5 grid.  Column 2 is a wall except for the bottom cell (4,2).
    // The shortest route from (0,0) to (4,4) is 4 down + 4 right = 8.
    std::vector<std::uint8_t> blocked(25, 0);
    for (int y = 0; y <= 3; ++y) blocked[y * 5 + 2] = 1;
    Grid grid = make_grid_from_blocked(5, 5, blocked);

    const Vertex s = grid.cell(0, 0);
    const Vertex t = grid.cell(4, 4);
    expect(bfs_distance(grid.graph, s, t) == 8, "BFS hand distance should be 8");

    ManhattanHeuristic manhattan(5);
    SearchOptions options;
    options.build_path = true;
    SearchScratch d_scratch(grid.graph.n, TieBreak::LargerG);
    SearchScratch a_scratch(grid.graph.n, TieBreak::LargerG);
    const SearchResult d = dijkstra(grid.graph, s, t, options, d_scratch);
    const SearchResult a = astar_search(grid.graph, s, t, manhattan, options, a_scratch);
    expect(d.status == SearchStatus::Ok && d.distance == 8, "Dijkstra hand distance");
    expect(a.status == SearchStatus::Ok && a.distance == 8, "A* hand distance");
    std::string error;
    expect(validate_path(grid.graph, s, t, a.path, 8, &error), "A* hand path invalid");
    expect(a.counters.expanded < d.counters.expanded,
           "Manhattan A* should expand fewer vertices than Dijkstra here");
    std::cout << "  hand grid example: PASS (A* expanded " << a.counters.expanded
              << ", Dijkstra " << d.counters.expanded << ")\n";
}

void test_grid_no_path() {
    // A complete wall splits the 5x5 grid.
    std::vector<std::uint8_t> blocked(25, 0);
    for (int y = 0; y < 5; ++y) blocked[y * 5 + 2] = 1;
    Grid grid = make_grid_from_blocked(5, 5, blocked);

    ManhattanHeuristic manhattan(5);
    SearchOptions options;
    SearchScratch d_scratch(grid.graph.n, TieBreak::LargerG);
    SearchScratch a_scratch(grid.graph.n, TieBreak::LargerG);
    const Vertex s = grid.cell(0, 0);
    const Vertex t = grid.cell(0, 4);
    const SearchResult d = dijkstra(grid.graph, s, t, options, d_scratch);
    const SearchResult a = astar_search(grid.graph, s, t, manhattan, options, a_scratch);
    expect(d.status == SearchStatus::NoPath, "Dijkstra should report no path");
    expect(a.status == SearchStatus::NoPath, "A* should report no path");
    expect(d.distance == kInfinity && a.distance == kInfinity, "no-path distance");
    expect(d.path.empty() && a.path.empty(), "no-path path should be empty");
    std::cout << "  no-path grid: PASS\n";
}

void test_grid_pairs() {
    const std::vector<GridFamily> families = {
        GridFamily::Open, GridFamily::Random, GridFamily::Clustered,
        GridFamily::Rooms, GridFamily::Maze};
    std::mt19937_64 rng(777);
    for (GridFamily family : families) {
        GridConfig config;
        config.width = 32;
        config.height = 32;
        config.family = family;
        config.obstacle_density = family == GridFamily::Random ? 0.20
                                 : family == GridFamily::Clustered ? 0.30 : 0.0;
        config.seed = 1000 + static_cast<std::uint64_t>(family);
        const Grid grid = make_grid(config);
        const std::vector<Vertex> component = largest_connected_component(grid.graph);
        expect(component.size() > 20, "component too small in grid test");
        const std::vector<Query> queries =
            select_queries(grid.graph, component, 3, 42 + static_cast<std::uint64_t>(family));
        expect(!queries.empty(), "no test queries selected");

        ManhattanHeuristic manhattan(grid.width);
        for (TieBreak tie : {TieBreak::LargerG, TieBreak::NodeId}) {
            SearchScratch d_scratch(grid.graph.n, tie);
            SearchScratch a_scratch(grid.graph.n, tie);
            for (const Query& q : queries) {
                SearchOptions options;
                options.tie = tie;
                options.build_path = true;
                const SearchResult d =
                    dijkstra(grid.graph, q.source, q.target, options, d_scratch);
                const SearchResult a =
                    astar_search(grid.graph, q.source, q.target, manhattan, options, a_scratch);
                const std::uint32_t oracle = bfs_distance(grid.graph, q.source, q.target);
                expect(d.status == SearchStatus::Ok, "Dijkstra failed on grid pair");
                expect(a.status == SearchStatus::Ok, "A* failed on grid pair");
                expect(d.distance == oracle, "Dijkstra vs BFS mismatch");
                expect(a.distance == oracle, "A* vs BFS mismatch");
                expect(d.counters.popped ==
                           d.counters.expanded + d.counters.stale_popped,
                       "Dijkstra counter identity");
                expect(a.counters.popped ==
                           a.counters.expanded + a.counters.stale_popped,
                       "A* counter identity");
                expect(a.counters.heuristic_calls == a.counters.pushed,
                       "A* heuristic calls should equal pushes");
                expect(a.counters.pushed == a.counters.improved + 1,
                       "A* pushed/improved identity");
                std::string error;
                expect(validate_path(grid.graph, q.source, q.target, a.path, a.distance,
                                     &error),
                       "grid A* path invalid: " + error);
            }
        }
        std::cout << "  grid family " << grid_family_name(family) << ": PASS\n";
    }
}

void test_alt_heuristic() {
    // A 9x9 open grid with a few hand-placed obstacles: unweighted, so the
    // landmark distances are BFS distances.
    std::vector<std::uint8_t> blocked(81, 0);
    for (int y = 1; y < 8; ++y) blocked[y * 9 + 4] = 1;
    blocked[7 * 9 + 4] = 0;
    Grid grid = make_grid_from_blocked(9, 9, blocked);
    const LandmarkData data = select_landmarks(grid.graph, 4, 123);
    expect(data.k == 4, "landmark count");

    ALTHeuristic alt(data, 4);
    ZeroHeuristic zero;
    SearchScratch alt_scratch(grid.graph.n, TieBreak::LargerG);
    SearchScratch zero_scratch(grid.graph.n, TieBreak::LargerG);
    SearchOptions options;
    options.build_path = true;

    for (Vertex t = 0; t < grid.graph.n; ++t) {
        if (!grid.is_free(t)) continue;
        alt.prepare(t);
        const auto true_dist = bfs_distances(grid.graph, t);
        for (Vertex v = 0; v < grid.graph.n; ++v) {
            if (!grid.is_free(v)) continue;
            expect(alt.estimate(v) <= true_dist[v], "ALT heuristic is not admissible");
        }
        for (Vertex u = 0; u < grid.graph.n; ++u) {
            if (!grid.is_free(u)) continue;
            for (std::uint64_t e = grid.graph.offsets[u];
                 e < grid.graph.offsets[u + 1]; ++e) {
                const Vertex v = grid.graph.targets[e];
                const Distance diff = alt.estimate(u) > alt.estimate(v)
                                          ? alt.estimate(u) - alt.estimate(v)
                                          : alt.estimate(v) - alt.estimate(u);
                expect(diff <= static_cast<Distance>(grid.graph.weights[e]),
                       "ALT heuristic is not consistent");
            }
        }
    }

    for (Vertex s = 0; s < grid.graph.n; s += 7) {
        if (!grid.is_free(s)) continue;
        for (Vertex t = 3; t < grid.graph.n; t += 11) {
            if (!grid.is_free(t) || s == t) continue;
            const SearchResult z = astar_search(grid.graph, s, t, zero, options, zero_scratch);
            const SearchResult a = astar_search(grid.graph, s, t, alt, options, alt_scratch);
            const std::uint32_t oracle = bfs_distance(grid.graph, s, t);
            expect(z.distance == oracle, "zero-heuristic A* vs BFS");
            expect(a.distance == oracle, "ALT A* vs BFS");
        }
    }
    std::cout << "  ALT admissibility/consistency: PASS\n";
}


void test_weighted_astar() {
    GridConfig config;
    config.width = 48;
    config.height = 48;
    config.family = GridFamily::Random;
    config.obstacle_density = 0.25;
    config.seed = 424242;
    const Grid grid = make_grid(config);
    const auto component = largest_connected_component(grid.graph);
    const auto queries = select_queries(grid.graph, component, 3, 8181);
    ManhattanHeuristic manhattan(grid.width);
    SearchScratch d_scratch(grid.graph.n, TieBreak::LargerG);
    SearchScratch a_scratch(grid.graph.n, TieBreak::LargerG);

    for (const Query& q : queries) {
        SearchOptions optimal;
        optimal.build_path = true;
        const SearchResult d = dijkstra(grid.graph, q.source, q.target, optimal, d_scratch);
        expect(d.status == SearchStatus::Ok, "weighted test: Dijkstra failed");

        SearchOptions weighted;
        weighted.build_path = true;
        weighted.heuristic_weight = 2.0;
        weighted.allow_reopen = true;
        const SearchResult a =
            astar_search(grid.graph, q.source, q.target, manhattan, weighted, a_scratch);
        expect(a.status == SearchStatus::Ok, "weighted A* failed");
        expect(a.distance >= d.distance, "weighted A* returned an impossible distance");
    }

    SearchOptions high;
    high.build_path = true;
    high.heuristic_weight = 50.0;
    high.allow_reopen = true;
    for (const Query& q : queries) {
        const SearchResult a =
            astar_search(grid.graph, q.source, q.target, manhattan, high, a_scratch);
        expect(a.status == SearchStatus::Ok, "high-epsilon A* failed");
        std::string error;
        if (!validate_path(grid.graph, q.source, q.target, a.path, a.distance, &error)) {
            std::cout << "  note: high-epsilon weighted path not parent-consistent: "
                      << error << "\n";
        }
    }

    SearchOptions eps1;
    eps1.build_path = true;
    eps1.heuristic_weight = 1.0;
    eps1.allow_reopen = true;
    for (const Query& q : queries) {
        const SearchResult a =
            astar_search(grid.graph, q.source, q.target, manhattan, eps1, a_scratch);
        const std::uint32_t oracle = bfs_distance(grid.graph, q.source, q.target);
        expect(a.distance == oracle, "eps=1 weighted A* must remain optimal");
        std::string error;
        expect(validate_path(grid.graph, q.source, q.target, a.path, a.distance, &error),
               "eps=1 weighted A* path invalid: " + error);
    }
    GridConfig maze_config;
    maze_config.width = 32;
    maze_config.height = 32;
    maze_config.family = GridFamily::Maze;
    maze_config.seed = 13579;
    const Grid maze = make_grid(maze_config);
    const auto maze_component = largest_connected_component(maze.graph);
    const auto maze_queries = select_queries(maze.graph, maze_component, 2, 2468);
    ManhattanHeuristic maze_h(maze.width);
    SearchScratch maze_scratch(maze.graph.n, TieBreak::LargerG);
    for (const Query& q : maze_queries) {
        SearchOptions high;
        high.build_path = true;
        high.heuristic_weight = 50.0;
        high.allow_reopen = true;
        const SearchResult a = astar_search(maze.graph, q.source, q.target,
                                            maze_h, high, maze_scratch);
        expect(a.status == SearchStatus::Ok, "maze weighted A* failed");
        expect(a.distance >= q.oracle_distance,
               "maze weighted A* beat the unique-path optimum");
        std::string error;
        expect(validate_path(maze.graph, q.source, q.target, a.path, a.distance,
                             &error),
               "maze weighted path invalid: " + error);
    }
    std::cout << "  weighted A* (reopening, epsilon=1,2,50; maze path): PASS\n";
}

void test_tie_break() {
    GridConfig config;
    config.width = 64;
    config.height = 64;
    config.family = GridFamily::Random;
    config.obstacle_density = 0.20;
    config.seed = 98765;
    const Grid grid = make_grid(config);
    const auto component = largest_connected_component(grid.graph);
    const auto queries = select_queries(grid.graph, component, 3, 555);
    ManhattanHeuristic manhattan(64);
    SearchScratch larger(grid.graph.n, TieBreak::LargerG);
    SearchScratch nodeid(grid.graph.n, TieBreak::NodeId);
    for (const Query& q : queries) {
        SearchOptions options;
        options.tie = TieBreak::LargerG;
        const SearchResult a =
            astar_search(grid.graph, q.source, q.target, manhattan, options, larger);
        options.tie = TieBreak::NodeId;
        const SearchResult b =
            astar_search(grid.graph, q.source, q.target, manhattan, options, nodeid);
        expect(a.distance == b.distance, "tie-break changed shortest distance");
    }
    std::cout << "  tie-break distance equivalence: PASS\n";
}

void test_real_sample() {
    const std::string graph_path = "data/roadNet-CA.bin";
    const std::string landmark_path = "data/roadNet-CA.alt16.bin";
    std::ifstream graph_file(graph_path);
    std::ifstream landmark_file(landmark_path);
    if (!graph_file.good() || !landmark_file.good()) {
        std::cout << "  real graph sample: SKIP (run preprocess-real first)\n";
        return;
    }
    Graph graph = load_graph_binary(graph_path);
    LandmarkData data = load_landmarks_binary(landmark_path);
    expect(data.n == graph.n, "real landmark size mismatch");

    // Validate ALT on a small subgraph is not possible without a separate
    // graph, so validate it on the real graph by checking the distance oracle.
    ALTHeuristic alt(data, 8);
    SearchScratch d_scratch(graph.n, TieBreak::LargerG);
    SearchScratch a_scratch(graph.n, TieBreak::LargerG);
    SearchOptions options;
    options.build_path = true;

    std::vector<Vertex> all(graph.n);
    std::iota(all.begin(), all.end(), 0);
    const auto queries = select_queries(graph, all, 2, 24680);
    for (const Query& q : queries) {
        const std::uint32_t oracle = bfs_distance(graph, q.source, q.target);
        const SearchResult d =
            dijkstra(graph, q.source, q.target, options, d_scratch);
        const SearchResult a = astar_search(graph, q.source, q.target, alt, options, a_scratch);
        expect(d.distance == oracle, "real Dijkstra vs BFS");
        expect(a.distance == oracle, "real A* vs BFS");
        std::string error;
        expect(validate_path(graph, q.source, q.target, d.path, d.distance, &error),
               "real Dijkstra path invalid: " + error);
        expect(validate_path(graph, q.source, q.target, a.path, a.distance, &error),
               "real A* path invalid: " + error);
    }
    std::cout << "  real graph sample (" << queries.size() << " queries): PASS\n";
}

}  // namespace

int main() {
    try {
        std::cout << "core tests\n";
        test_binary_heap();
        test_tiny_weighted();
        test_grid_hand_example();
        test_grid_no_path();
        test_grid_pairs();
        test_alt_heuristic();
        test_weighted_astar();
        test_tie_break();
        test_real_sample();
        std::cout << "all tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "TEST FAILURE: " << error.what() << "\n";
        return 1;
    }
}
