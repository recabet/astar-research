#include "grid.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <random>
#include <stdexcept>
#include <utility>

namespace astar
{
    namespace
    {
        std::uint64_t derived_seed(const GridConfig &config)
        {
            return config.seed ^ fnv1a64(grid_family_name(config.family) + "_" +
                                         std::to_string(config.width) + "x" +
                                         std::to_string(config.height));
        }

        void make_random_obstacles(GridConfig config, std::vector<std::uint8_t> &blocked,
                                   std::mt19937_64 &rng)
        {
            const double density = config.obstacle_density > 0.0 ? config.obstacle_density : 0.20;
            std::bernoulli_distribution obstacle(density);
            for (unsigned char & i : blocked)
            {
                i = obstacle(rng) ? 1 : 0;
            }
        }

        void make_clustered_obstacles(GridConfig config, std::vector<std::uint8_t> &blocked,
                                      std::mt19937_64 &rng)
        {
            const double density = config.obstacle_density > 0.0 ? config.obstacle_density : 0.30;
            const auto target = static_cast<std::uint64_t>(
                density * static_cast<double>(blocked.size()));
            const int max_radius = std::max(3, static_cast<int>(
                                                std::min(config.width, config.height) / 20));
            std::uniform_int_distribution<int> x_dist(0, static_cast<int>(config.width) - 1);
            std::uniform_int_distribution<int> y_dist(0, static_cast<int>(config.height) - 1);
            std::uniform_int_distribution<int> radius_dist(3, max_radius);
            std::uniform_real_distribution<double> keep(0.0, 1.0);

            std::uint64_t blocked_count = 0;
            std::uint64_t attempts = 0;
            const std::uint64_t max_attempts = static_cast<std::uint64_t>(blocked.size()) * 8 + 1000;
            while (blocked_count < target && attempts++ < max_attempts)
            {
                const int cx = x_dist(rng);
                const int cy = y_dist(rng);
                const int radius = radius_dist(rng);
                for (int dy = -radius; dy <= radius && blocked_count < target; ++dy)
                {
                    for (int dx = -radius; dx <= radius && blocked_count < target; ++dx)
                    {
                        if (dx * dx + dy * dy > radius * radius) continue;
                        const int x = cx + dx;
                        const int y = cy + dy;
                        if (x < 0 || y < 0 || x >= static_cast<int>(config.width) ||
                            y >= static_cast<int>(config.height))
                        {
                            continue;
                        }
                        const std::size_t idx = static_cast<std::size_t>(y) * config.width + x;
                        if (blocked[idx] == 0 && keep(rng) < 0.85)
                        {
                            blocked[idx] = 1;
                            ++blocked_count;
                        }
                    }
                }
            }
        }

        void make_rooms(GridConfig config, std::vector<std::uint8_t> &blocked,
                        std::mt19937_64 &rng)
        {
            constexpr std::uint32_t room = 16;

            // One door per room boundary segment, not one door per infinite wall.
            // Otherwise a 4x4 arrangement of rooms would have only six doors and most
            // rooms would be isolated.  Doors avoid wall intersections, so every
            // adjacent pair of rooms is connected.
            for (std::uint32_t x = room; x < config.width; x += room)
            {
                for (std::uint32_t y0 = 0; y0 < config.height; y0 += room)
                {
                    const std::uint32_t y1 = std::min(config.height, y0 + room);
                    if (y1 <= y0 + 1) continue;
                    const std::uint32_t first = y0 + 1;
                    const std::uint32_t last = y1 - 1;
                    std::uniform_int_distribution<std::uint32_t> door_dist(first, last);
                    const std::uint32_t door_y = door_dist(rng);
                    for (std::uint32_t y = first; y <= last; ++y)
                    {
                        if (y != door_y)
                        {
                            blocked[static_cast<std::size_t>(y) * config.width + x] = 1;
                        }
                    }
                }
            }
            for (std::uint32_t y = room; y < config.height; y += room)
            {
                for (std::uint32_t x0 = 0; x0 < config.width; x0 += room)
                {
                    const std::uint32_t x1 = std::min(config.width, x0 + room);
                    if (x1 <= x0 + 1) continue;
                    const std::uint32_t first = x0 + 1;
                    const std::uint32_t last = x1 - 1;
                    std::uniform_int_distribution<std::uint32_t> door_dist(first, last);
                    const std::uint32_t door_x = door_dist(rng);
                    for (std::uint32_t x = first; x <= last; ++x)
                    {
                        if (x != door_x)
                        {
                            blocked[static_cast<std::size_t>(y) * config.width + x] = 1;
                        }
                    }
                }
            }
            // Wall intersections are solid; the doors above are all away from them.
            for (std::uint32_t y = room; y < config.height; y += room)
            {
                for (std::uint32_t x = room; x < config.width; x += room)
                {
                    blocked[static_cast<std::size_t>(y) * config.width + x] = 1;
                }
            }
        }

        void make_maze(GridConfig config, std::vector<std::uint8_t> &blocked,
                       std::mt19937_64 &rng)
        {
            if (config.width < 3 || config.height < 3)
            {
                throw std::runtime_error("maze needs at least 3x3 cells");
            }
            std::ranges::fill(blocked.begin(), blocked.end(), 1);
            std::vector<std::uint8_t> visited(blocked.size(), 0);

            constexpr int start_x = 1;
            constexpr int start_y = 1;
            const auto start = static_cast<Vertex>(start_y * config.width + start_x);
            std::vector<Vertex> stack;
            stack.push_back(start);
            visited[start] = 1;
            blocked[start] = 0;

            constexpr std::array<std::pair<int, int>, 4> directions = {
                std::make_pair(-2, 0), std::make_pair(2, 0),
                std::make_pair(0, -2), std::make_pair(0, 2)
            };

            while (!stack.empty())
            {
                const Vertex current = stack.back();
                const int cx = static_cast<int>(current % config.width);
                const int cy = static_cast<int>(current / config.width);

                std::array<int, 4> order = {0, 1, 2, 3};
                std::ranges::shuffle(order.begin(), order.end(), rng);

                bool advanced = false;
                for (int direction: order)
                {
                    const int nx = cx + directions[direction].first;
                    const int ny = cy + directions[direction].second;
                    if (nx <= 0 || ny <= 0 || nx >= static_cast<int>(config.width) - 1 ||
                        ny >= static_cast<int>(config.height) - 1)
                    {
                        continue;
                    }
                    const auto next = static_cast<Vertex>(ny * config.width + nx);
                    if (visited[next]) continue;

                    const auto wall = static_cast<Vertex>(
                        (cy + directions[direction].second / 2) * config.width +
                        (cx + directions[direction].first / 2));
                    blocked[wall] = 0;
                    blocked[next] = 0;
                    visited[next] = 1;
                    stack.push_back(next);
                    advanced = true;
                    break;
                }
                if (!advanced) stack.pop_back();
            }
        }
    } // namespace

    std::string grid_family_name(GridFamily family)
    {
        switch (family)
        {
            case GridFamily::Open: return "open";
            case GridFamily::Random: return "random";
            case GridFamily::Clustered: return "clustered";
            case GridFamily::Rooms: return "rooms";
            case GridFamily::Maze: return "maze";
        }
        return "unknown";
    }

    GridFamily parse_grid_family(const std::string &name)
    {
        if (name == "open") return GridFamily::Open;
        if (name == "random" || name == "random20" || name == "random25") return GridFamily::Random;
        if (name == "clustered" || name == "clustered30") return GridFamily::Clustered;
        if (name == "rooms") return GridFamily::Rooms;
        if (name == "maze") return GridFamily::Maze;
        throw std::runtime_error("unknown grid family: " + name);
    }

    Grid make_grid_from_blocked(std::uint32_t width, std::uint32_t height,
                                std::vector<std::uint8_t> blocked)
    {
        if (blocked.size() != static_cast<std::size_t>(width) * height)
        {
            throw std::runtime_error("blocked array has the wrong size");
        }
        if (static_cast<std::uint64_t>(width) * height > std::numeric_limits<Vertex>::max())
        {
            throw std::runtime_error("grid has too many cells for 32-bit vertex ids");
        }

        Grid grid;
        grid.width = width;
        grid.height = height;
        grid.blocked = std::move(blocked);
        grid.free_cells = 0;
        for (std::uint8_t b: grid.blocked)
        {
            if (!b) ++grid.free_cells;
        }

        GraphBuilder builder(width * height, true);
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const auto u = static_cast<Vertex>(static_cast<std::uint64_t>(y) * width + x);
                if (grid.blocked[u]) continue;
                if (x + 1 < width)
                {
                    const Vertex v = u + 1;
                    if (!grid.blocked[v]) builder.add_edge(u, v, 1);
                }
                if (y + 1 < height)
                {
                    const Vertex v = u + width;
                    if (!grid.blocked[v]) builder.add_edge(u, v, 1);
                }
            }
        }
        grid.graph = builder.build(false);
        return grid;
    }

    Grid make_grid(const GridConfig &config)
    {
        if (config.width == 0 || config.height == 0)
        {
            throw std::runtime_error("grid dimensions must be positive");
        }
        std::vector<std::uint8_t> blocked(
            static_cast<std::size_t>(config.width) * config.height, 0);
        std::mt19937_64 rng(derived_seed(config));

        switch (config.family)
        {
            case GridFamily::Open:
                break;
            case GridFamily::Random:
                make_random_obstacles(config, blocked, rng);
                break;
            case GridFamily::Clustered:
                make_clustered_obstacles(config, blocked, rng);
                break;
            case GridFamily::Rooms:
                make_rooms(config, blocked, rng);
                break;
            case GridFamily::Maze:
                make_maze(config, blocked, rng);
                break;
        }
        return make_grid_from_blocked(config.width, config.height, std::move(blocked));
    }
} // namespace astar
