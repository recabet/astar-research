#include "preprocess.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "graph.hpp"
#include "heuristic.hpp"
#include "oracle.hpp"

namespace astar
{
    int preprocess_real_graph(const PreprocessOptions &options)
    {
        if (options.input.empty()) throw std::runtime_error("missing --input");
        if (options.output_graph.empty()) throw std::runtime_error("missing --out-graph");

        std::ifstream in(options.input);
        if (!in) throw std::runtime_error("cannot open edge list: " + options.input);

        std::vector<std::pair<std::uint32_t, std::uint32_t> > edges;
        edges.reserve(6000000);
        std::uint32_t max_id = 0;
        std::string line;
        std::uint64_t line_number = 0;
        while (std::getline(in, line))
        {
            ++line_number;
            if (line.empty() || line[0] == '#') continue;
            unsigned u = 0;
            unsigned v = 0;
            if (std::sscanf(line.c_str(), "%u %u", &u, &v) != 2)
            {
                throw std::runtime_error("bad edge-list line " + std::to_string(line_number));
            }
            edges.emplace_back(static_cast<std::uint32_t>(u), static_cast<std::uint32_t>(v));
            max_id = std::max(max_id, std::max<std::uint32_t>(u, v));
        }
        in.close();

        std::cerr << "read " << edges.size() << " input arcs, max id " << max_id << "\n";

        GraphBuilder builder(max_id + 1, true);
        for (const auto &[u, v]: edges) builder.add_edge(u, v, 1);
        std::cerr << "building symmetrised, deduplicated graph...\n";
        Graph base = builder.build(true);
        std::cerr << "base graph: n=" << base.n << " arcs=" << base.num_arcs()
                << " (undirected edges=" << base.num_arcs() / 2 << ")\n";

        const std::vector<Vertex> component = largest_connected_component(base);
        std::cerr << "largest connected component: " << component.size() << " vertices\n";
        if (component.size() < 2) throw std::runtime_error("component too small");

        std::vector<std::int32_t> new_id(base.n, -1);
        for (std::size_t i = 0; i < component.size(); ++i)
        {
            new_id[component[i]] = static_cast<std::int32_t>(i);
        }

        GraphBuilder compact(static_cast<std::uint32_t>(component.size()), true);
        std::uint64_t kept_undirected_edges = 0;
        for (Vertex u: component)
        {
            for (std::uint64_t e = base.offsets[u]; e < base.offsets[u + 1]; ++e)
            {
                const Vertex v = base.targets[e];
                if (new_id[v] < 0 || u >= v) continue;
                compact.add_edge(static_cast<Vertex>(new_id[u]),
                                 static_cast<Vertex>(new_id[v]), 1);
                ++kept_undirected_edges;
            }
        }
        Graph graph = compact.build(true);
        std::cerr << "compact graph: n=" << graph.n << " arcs=" << graph.num_arcs()
                << " undirected_edges=" << kept_undirected_edges << "\n";

        save_graph_binary(graph, options.output_graph);

        if (!options.output_landmarks.empty())
        {
            std::cerr << "selecting " << options.landmarks << " ALT landmarks...\n";
            const LandmarkData data =
                    select_landmarks(graph, options.landmarks, options.seed);
            save_landmarks_binary(data, options.output_landmarks);
            std::cerr << "landmarks:";
            for (Vertex id: data.ids) std::cerr << ' ' << id;
            std::cerr << "\n";
        }

        return 0;
    }
} // namespace astar
