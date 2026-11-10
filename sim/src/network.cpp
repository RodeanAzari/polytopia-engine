#include "poly/network.hpp"

#include <bitset>
#include <utility>

namespace poly {

namespace {

constexpr int kPortTradeDistance = 4;  // 01: steps, destination port included
constexpr int kGrandBazaarCities = 5;

bool is_water(Terrain t) { return t == Terrain::ShallowWater || t == Terrain::DeepWater; }

// Breadth-first search over 8-neighbours. `pass(i)` says whether tile i may be entered; `extra`
// adds jump links from a tile. Fills dist (-1 = unreached) up to max_depth.
template <typename Pass, typename Extra>
void bfs(const GameState& s, int start, int max_depth, const Pass& pass, const Extra& extra,
         std::array<std::int16_t, kMaxTiles>& dist) {
    dist.fill(-1);
    std::array<std::uint16_t, kMaxTiles> queue{};
    int head = 0;
    int tail = 0;
    dist[static_cast<std::size_t>(start)] = 0;
    queue[static_cast<std::size_t>(tail++)] = static_cast<std::uint16_t>(start);
    auto visit = [&](int n, int d) {
        if (dist[static_cast<std::size_t>(n)] < 0 && pass(n)) {
            dist[static_cast<std::size_t>(n)] = static_cast<std::int16_t>(d);
            queue[static_cast<std::size_t>(tail++)] = static_cast<std::uint16_t>(n);
        }
    };
    while (head < tail) {
        const int i = queue[static_cast<std::size_t>(head++)];
        const int d = dist[static_cast<std::size_t>(i)] + 1;
        if (d > max_depth) {
            continue;
        }
        const int x = i / s.side;
        const int y = i % s.side;
        for (int nx = x - 1; nx <= x + 1; ++nx) {
            for (int ny = y - 1; ny <= y + 1; ++ny) {
                if ((nx != x || ny != y) && s.on_board(nx, ny)) {
                    visit(s.index(nx, ny), d);
                }
            }
        }
        extra(i, [&](int n) { visit(n, d); });
    }
}

// Tribe t's network layers (03a §6): tiles that carry the connection, navigable water, own ports.
struct NetTiles {
    std::bitset<kMaxTiles> connected;
    std::bitset<kMaxTiles> navigable;
    InlineVector<std::uint16_t, kMaxTiles> ports;
};

void classify_tiles(const GameState& s, const TribeState& t, NetTiles& out) {
    const int tiles = s.side * s.side;
    for (int i = 0; i < tiles; ++i) {
        const auto ui = static_cast<std::size_t>(i);
        const Slot owner = s.city_owner[ui];
        const bool mine = owner != kNoSlot && t.cities.contains(owner);
        const bool not_enemy = mine || owner == kNoSlot;
        const Terrain ter = s.terrain[ui];
        const bool road = s.network.test(ui) && !is_water(ter) && ter != Terrain::City;
        if (mine && (ter == Terrain::City || s.building[ui] == Building::Port)) {
            out.connected.set(ui, s.network.test(ui));
            if (s.building[ui] == Building::Port) {
                out.ports.push_back(static_cast<std::uint16_t>(i));
            }
        } else if (not_enemy && road) {
            out.connected.set(ui, true);
        }
        out.navigable.set(ui, is_water(ter) && t.visible.test(ui) && not_enemy);
    }
}

constexpr std::size_t kMaxPorts = 64;
using PortLinks = std::array<std::array<bool, kMaxPorts>, kMaxPorts>;

// Port links: shortest navigable water path of at most kPortTradeDistance steps (03a §6).
// [Tribes quirk] Each pair is searched once, from the earlier port (row-major) to the later one, and
// linked both ways if found: the destination must be navigable, the origin need not be, so a pair
// whose later port is not navigable (e.g. not visible) is never linked.
void link_ports(const GameState& s, const NetTiles& net, PortLinks& link,
                std::array<std::int16_t, kMaxTiles>& dist) {
    const auto& ports = net.ports;
    check(ports.size() <= kMaxPorts, "network: more than 64 ports of one tribe");
    for (std::size_t a = 0; a < ports.size(); ++a) {
        bfs(
            s, ports[a], kPortTradeDistance,
            [&](int n) { return net.navigable.test(static_cast<std::size_t>(n)); },
            [](int, const auto&) {}, dist);
        for (std::size_t b = a + 1; b < ports.size(); ++b) {
            if (dist[ports[b]] >= 0) {
                link[a][b] = true;
                link[b][a] = true;
            }
        }
    }
}

// Updates p's connected-city list from the capital's reach `dist` and applies the population
// changes (03a §6).
void apply_connections(GameState& s, int p, const std::array<std::int16_t, kMaxTiles>& dist) {
    TribeState& t = s.tribes[static_cast<std::size_t>(p)];
    const bool active = p == s.active_tribe;
    int added = 0;
    int lost = 0;
    InlineVector<Slot, kMaxCities> added_cities;
    auto drop = [&](Slot c) {
        t.connected.erase_value(c);
        add_population(s, s.cities[c], p, -1);  // credited to p even if p lost the city
        ++lost;
    };
    for (Slot c : t.cities) {
        if (c == t.capital) {
            continue;
        }
        const City& city = s.cities[c];
        const bool now = dist[static_cast<std::size_t>(s.index(city.x, city.y))] > 0;
        if (t.connected.contains(c)) {
            if (!now) {
                drop(c);
            }
        } else if (now) {
            t.connected.push_back(c);
            added_cities.push_back(c);
            ++added;
        }
    }
    const InlineVector<Slot, kMaxCities> before = t.connected;
    for (Slot c : before) {
        if (!t.cities.contains(c)) {
            drop(c);
        }
    }
    add_population(s, s.cities[t.capital], p, added - lost);
    if (static_cast<int>(t.connected.size()) >= kGrandBazaarCities) {
        unlock(t, Building::GrandBazar);
    }
    if (active) {  // Tribes fact: the +1 for newly connected cities only on the tribe's own turn
        for (Slot c : added_cities) {
            add_population(s, s.cities[c], p, 1);
        }
    }
}

}  // namespace

void recompute_network_tribe(GameState& s, int p) {
    if (!s.native && p != s.active_tribe) {
        return;  // non-native copy: only the active tribe's network is updated (03b §10)
    }
    TribeState& t = s.tribes[static_cast<std::size_t>(p)];
    if (t.capital == kNoSlot || !t.cities.contains(t.capital)) {
        t.connected.clear();  // no population changes in this branch (Tribes fact, 03a §6)
        return;
    }
    NetTiles net;
    classify_tiles(s, t, net);
    PortLinks link{};
    std::array<std::int16_t, kMaxTiles> dist{};
    link_ports(s, net, link, dist);
    auto jumps = [&](int i, const auto& visit) {
        for (std::size_t a = 0; a < net.ports.size(); ++a) {
            if (std::cmp_equal(net.ports[a], i)) {
                for (std::size_t b = 0; b < net.ports.size(); ++b) {
                    if (link[a][b]) {
                        visit(net.ports[b]);
                    }
                }
            }
        }
    };
    const City& capital = s.cities[t.capital];
    bfs(
        s, s.index(capital.x, capital.y), s.side * s.side,
        [&](int n) { return net.connected.test(static_cast<std::size_t>(n)); }, jumps, dist);
    apply_connections(s, p, dist);
}

void set_network(GameState& s, int x, int y, bool on) {
    s.network.set(static_cast<std::size_t>(s.index(x, y)), on);
    for (int p = 0; p < s.num_tribes; ++p) {
        recompute_network_tribe(s, p);
    }
}

}  // namespace poly
