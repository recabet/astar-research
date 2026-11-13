#include "preprocess.hpp"

#include <algorithm>
#include <cmath>
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

    int preprocess_dimacs_graph(const DimacsPreprocessOptions &options)
    {
        if (options.input_gr.empty() || options.input_co.empty())
        {
            throw std::runtime_error("DIMACS preprocessing needs --gr and --co");
        }
        if (options.output_graph.empty() || options.output_landmarks.empty() ||
            options.output_coords.empty())
        {
            throw std::runtime_error(
                "DIMACS preprocessing needs graph, landmark, and coordinate outputs");
        }

        struct Edge {
            std::uint32_t u;
            std::uint32_t v;
            Weight w;
        };

        std::ifstream co_in(options.input_co);
        if (!co_in) throw std::runtime_error("cannot open coordinates: " + options.input_co);
        std::vector<double> lon;
        std::vector<double> lat;
        std::string line;
        std::uint32_t coord_n = 0;
        while (std::getline(co_in, line))
        {
            if (line.empty() || line[0] == 'c') continue;
            if (line[0] == 'p')
            {
                unsigned count = 0;
                if (std::sscanf(line.c_str(), "p aux sp co %u", &count) == 1)
                {
                    coord_n = count;
                    lon.assign(coord_n, 0.0);
                    lat.assign(coord_n, 0.0);
                }
                continue;
            }
            if (line[0] == 'v')
            {
                unsigned id = 0;
                int x = 0;
                int y = 0;
                if (std::sscanf(line.c_str(), "v %u %d %d", &id, &x, &y) != 3)
                {
                    throw std::runtime_error("bad .co vertex line");
                }
                if (id == 0 || id > coord_n)
                {
                    throw std::runtime_error("coordinate id outside header range");
                }
                lon[id - 1] = static_cast<double>(x) * 1e-6;
                lat[id - 1] = static_cast<double>(y) * 1e-6;
            }
        }
        if (coord_n == 0) throw std::runtime_error("missing .co header");

        std::ifstream gr_in(options.input_gr);
        if (!gr_in) throw std::runtime_error("cannot open distance graph: " + options.input_gr);
        std::vector<Edge> edges;
        edges.reserve(800000);
        std::uint32_t graph_n = 0;
        std::uint32_t max_id = 0;
        std::uint64_t arc_count = 0;
        while (std::getline(gr_in, line))
        {
            if (line.empty() || line[0] == 'c') continue;
            if (line[0] == 'p')
            {
                unsigned n = 0;
                unsigned m = 0;
                if (std::sscanf(line.c_str(), "p sp %u %u", &n, &m) == 2)
                {
                    graph_n = n;
                    arc_count = m;
                }
                continue;
            }
            if (line[0] == 'a')
            {
                unsigned u = 0;
                unsigned v = 0;
                unsigned w = 0;
                if (std::sscanf(line.c_str(), "a %u %u %u", &u, &v, &w) != 3)
                {
                    throw std::runtime_error("bad .gr arc line");
                }
                if (u == 0 || v == 0) throw std::runtime_error("zero-based node id");
                edges.push_back({u - 1, v - 1, static_cast<Weight>(w)});
                max_id = std::max(max_id, std::max<std::uint32_t>(u, v));
            }
        }
        if (graph_n == 0 || graph_n != coord_n || max_id > graph_n)
        {
            throw std::runtime_error("DIMACS header/coordinate size mismatch");
        }

        std::cerr << "read " << edges.size() << " arcs (header says " << arc_count
                  << "), n=" << graph_n << "\n";
        GraphBuilder builder(graph_n, true);
        for (const Edge &edge: edges) builder.add_edge(edge.u, edge.v, edge.w);
        const Graph base = builder.build(true);
        std::cerr << "weighted symmetrised graph: n=" << base.n
                  << " arcs=" << base.num_arcs() << "\n";

        const std::vector<Vertex> component = largest_connected_component(base);
        if (component.size() < 2) throw std::runtime_error("component too small");
        std::vector<std::int32_t> new_id(base.n, -1);
        for (std::size_t i = 0; i < component.size(); ++i)
        {
            new_id[component[i]] = static_cast<std::int32_t>(i);
        }

        GraphBuilder compact(static_cast<std::uint32_t>(component.size()), true);
        for (Vertex u: component)
        {
            for (std::uint64_t e = base.offsets[u]; e < base.offsets[u + 1]; ++e)
            {
                const Vertex v = base.targets[e];
                if (new_id[v] < 0 || u >= v) continue;
                compact.add_edge(static_cast<Vertex>(new_id[u]),
                                 static_cast<Vertex>(new_id[v]), base.weights[e]);
            }
        }
        const Graph graph = compact.build(true);
        std::cerr << "compact weighted graph: n=" << graph.n
                  << " arcs=" << graph.num_arcs() << "\n";

        double max_abs_lat = 0.0;
        for (Vertex u: component) max_abs_lat = std::max(max_abs_lat, std::fabs(lat[u]));
        const double pi = std::acos(-1.0);
        const double lon_scale = 111000.0 * std::cos(max_abs_lat * pi / 180.0);
        const double lat_scale = 110000.0;
        // DIMACS .gr distance weights are roughly ten units per metre in this
        // file.  Coordinates are quantised, and some short edges have weight 1,
        // so a slightly conservative scale keeps the Euclidean bound admissible
        // on every arc (the measured maximum h/w is below 1.0 at 7.0).
        const double weight_units_per_metre = 7.0;
        const double lon0 = lon[component[0]];
        const double lat0 = lat[component[0]];

        Coordinates coordinates;
        coordinates.n = graph.n;
        coordinates.x.resize(graph.n);
        coordinates.y.resize(graph.n);
        for (std::size_t i = 0; i < component.size(); ++i)
        {
            const Vertex old = component[i];
            coordinates.x[i] =
                    (lon[old] - lon0) * lon_scale * weight_units_per_metre;
            coordinates.y[i] =
                    (lat[old] - lat0) * lat_scale * weight_units_per_metre;
        }

        save_graph_binary(graph, options.output_graph);
        save_coordinates_binary(coordinates, options.output_coords);

        std::cerr << "selecting " << options.landmarks
                  << " weighted ALT landmarks...\n";
        const LandmarkData data =
                select_landmarks_weighted(graph, options.landmarks, options.seed);
        save_landmarks_binary(data, options.output_landmarks);
        std::cerr << "landmarks:";
        for (Vertex id: data.ids) std::cerr << ' ' << id;
        std::cerr << "\n";
        return 0;
    }
} // namespace astar
