#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.hpp"
#include "graph.hpp"

namespace astar
{
    enum class GridFamily : std::uint8_t {
        Open = 0,
        Random = 1,
        Clustered = 2,
        Rooms = 3,
        Maze = 4,
    };

    struct GridConfig {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        GridFamily family = GridFamily::Open;
        double obstacle_density = 0.0;
        std::uint64_t seed = 0;
    };

    // Row-major cell ids.  A blocked cell is an isolated vertex of the CSR graph
    // (degree zero); a free cell has arcs to its free 4-neighbours.  Thus graph
    // vertex ids are exactly cell ids and the Manhattan heuristic needs only the
    // grid width.
    struct Grid {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> blocked;
        Graph graph;
        std::uint64_t free_cells = 0;

        [[nodiscard]] bool is_free(Vertex v) const
        {
            return v < blocked.size() && blocked[v] == 0;
        }

        [[nodiscard]] Vertex cell(const std::uint32_t row,const  std::uint32_t col) const
        {
            return static_cast<Vertex>(static_cast<std::uint64_t>(row) * width + col);
        }
    };

    Grid make_grid(const GridConfig &config);

    Grid make_grid_from_blocked(std::uint32_t width, std::uint32_t height,
                                std::vector<std::uint8_t> blocked);

    std::string grid_family_name(GridFamily family);

    GridFamily parse_grid_family(const std::string &name);
} // namespace astar
