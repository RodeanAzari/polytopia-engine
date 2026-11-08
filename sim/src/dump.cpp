#include "poly/dump.hpp"

#include <algorithm>
#include <vector>

namespace poly {

namespace {

// Java's List.toString / Arrays.toString: "[a, b, c]".
template <typename Range, typename F>
void list(std::string& o, const Range& r, const F& f) {
    o += '[';
    bool first = true;
    for (const auto& v : r) {
        if (!first) {
            o += ", ";
        }
        first = false;
        o += f(v);
    }
    o += ']';
}

std::string str(long long v) { return std::to_string(v); }
char bit(bool b) { return b ? '1' : '0'; }
std::string name(std::string_view v) { return std::string(v); }

class Dumper {
   public:
    explicit Dumper(const GameState& s, int observer = -1)
        : s_(&s),
          observer_(observer),
          tiles_(static_cast<std::size_t>(s.side) * static_cast<std::size_t>(s.side)) {
        o_.reserve((tiles_ * 16) + 4096);
    }

    std::string run() {
        header();
        board();
        for (int t = 0; t < s_->num_tribes; ++t) {
            tribe(t);
        }
        actors();
        o_ += "END";
        return std::move(o_);
    }

   private:
    [[nodiscard]] std::int32_t unit_id(Slot u) const { return u == kNoSlot ? 0 : s_->units[u].id; }
    [[nodiscard]] std::int32_t city_id(Slot c) const {
        return c == kNoSlot ? -1 : s_->cities[c].id;
    }

    void header() {
        o_ += (observer_ >= 0 ? "OBS" : "STATE") + std::string(" tick=") + str(s_->tick) +
              " active=" + str(s_->active_tribe) + " over=" + bit(s_->game_over) +
              " mustEnd=" + bit(s_->turn_must_end) + " levelingUp=" + bit(s_->leveling_up) +
              " nextActorId=" + str(s_->next_actor_id) + " canEnd=";
        for (int t = 0; t < s_->num_tribes; ++t) {
            o_ += bit(((s_->can_end_turn >> t) & 1U) != 0);
        }
        o_ += '\n';
    }

    template <typename F>
    void tile_row(const char* label, const F& cell) {
        o_ += label;
        for (std::size_t i = 0; i < tiles_; ++i) {
            o_ += ' ';
            cell(i);
        }
        o_ += '\n';
    }

    void board() {
        tile_row("terrain", [&](std::size_t i) {
            o_ += kTerrainChar[static_cast<std::size_t>(s_->terrain[i])];
        });
        tile_row("resource", [&](std::size_t i) {
            const Resource r = s_->resource[i];
            o_ += r == Resource::None ? '-' : kResourceChar[static_cast<std::size_t>(r)];
        });
        tile_row("building", [&](std::size_t i) {
            const Building b = s_->building[i];
            o_ += b == Building::None ? std::string("-")
                                      : name(kBuildingName[static_cast<std::size_t>(b)]);
        });
        tile_row("unit", [&](std::size_t i) { o_ += str(unit_id(s_->unit_at[i])); });
        tile_row("cityTile", [&](std::size_t i) {
            o_ += s_->terrain[i] == Terrain::Fog ? "0" : str(city_id(s_->city_owner[i]));
        });
        tile_row("network", [&](std::size_t i) { o_ += bit(s_->network.test(i)); });
    }

    void tribe(int id) {
        const auto ti = static_cast<std::size_t>(id);
        const TribeState& t = s_->tribes[ti];
        const bool hidden = observer_ >= 0 && id != observer_;
        o_ += "TRIBE " + str(id) + ' ' + name(kTribeName[static_cast<std::size_t>(t.type)]) +
              " stars=" + str(t.stars) + " score=" + str(t.score) +
              " result=" + name(kResultName[static_cast<std::size_t>(t.result)]) +
              " capital=" + str(city_id(t.capital)) + " cities=";
        list(o_, ids(t.cities, hidden, [&](Slot c) { return city_id(c); }),
             [](std::int32_t v) { return str(v); });
        o_ += " extraUnits=";
        list(o_, ids(t.tribe_units, hidden, [&](Slot u) { return unit_id(u); }),
             [](std::int32_t v) { return str(v); });
        o_ += " met=";
        list(o_, t.met, [](std::int8_t m) { return str(m); });
        o_ += " connected=";
        list(o_, t.connected, [&](Slot c) { return str(city_id(c)); });
        o_ += " tech=";
        for (int k = 0; k < kNumTechs; ++k) {
            o_ += bit(t.has_tech(static_cast<Tech>(k)));
        }
        o_ +=
            std::string(" allTech=") + bit(t.all_researched) + " kills=" + str(t.kills) +
            " pacifist=" + str(t.pacifist_count) + " starsSentTurn=" + str(t.stars_sent_this_turn) +
            " declaredWarTurn=" + bit(t.declared_war_this_turn) + " wars=" + str(t.wars_declared) +
            " starsSentTotal=" + str(t.stars_sent_total) + " monuments={";
        // Monument names in enum order are also alphabetical, matching the oracle's sorted map.
        const std::size_t shown = hidden ? 0 : kNumMonuments;  // the copy's map is empty
        for (std::size_t m = 0; m < shown; ++m) {
            o_ += (m > 0 ? ", " : "") + name(kBuildingName[kFirstMonument + m]) + '=' +
                  name(kMonumentStatusName[static_cast<std::size_t>(t.monuments[m])]);
        }
        o_ += "} allegiance=";
        std::vector<int> row;
        row.reserve(static_cast<std::size_t>(s_->num_tribes));
        for (int k = 0; k < s_->num_tribes; ++k) {
            row.push_back(s_->allegiance[ti][static_cast<std::size_t>(k)]);
        }
        list(o_, row, [](int v) { return str(v); });
        o_ += " vis=";
        for (std::size_t i = 0; i < tiles_; ++i) {
            o_ += bit(t.visible.test(i));
        }
        o_ += '\n';
    }

    void city(const City& c) {
        o_ += "CITY " + str(c.id) + " tribe=" + str(c.tribe) + " pos=" + str(c.x) + ',' + str(c.y) +
              " level=" + str(c.level) + " pop=" + str(c.population) +
              " need=" + str(c.population_need) + " prodBonus=" + str(c.production_bonus) +
              " capital=" + bit(c.capital) + " walls=" + bit(c.walls) +
              " bound=" + str(c.border_radius) + " worth=" + str(c.points_worth) + " units=";
        list(o_, c.units, [&](Slot u) { return str(unit_id(u)); });
        o_ += " buildings=[";
        bool first = true;
        for (const BuildingEntry& b : c.buildings) {
            o_ += (first ? "" : " ") + str(b.x) + ',' + str(b.y) + ':' +
                  name(kBuildingName[static_cast<std::size_t>(b.type)]);
            first = false;
            const bool temple = b.type == Building::Temple || b.type == Building::WaterTemple ||
                                b.type == Building::ForestTemple ||
                                b.type == Building::MountainTemple;
            if (temple) {
                o_ += ':' + str(b.temple_level) + ':' + str(b.turns_to_score);
            }
        }
        o_ += "]\n";
    }

    void unit(const Unit& u) {
        o_ += "UNIT " + str(u.id) + ' ' + name(kUnitName[static_cast<std::size_t>(u.type)]) +
              " tribe=" + str(u.tribe) + " city=" + str(city_id(u.city)) + " pos=" + str(u.x) +
              ',' + str(u.y) + " hp=" + str(u.hp) + '/' + str(u.max_hp) + " kills=" + str(u.kills) +
              " vet=" + bit(u.veteran) +
              " status=" + name(kTurnStatusName[static_cast<std::size_t>(u.status)]);
        if (u.type == UnitType::Boat || u.type == UnitType::Ship ||
            u.type == UnitType::Battleship) {
            o_ += " carries=" + name(kUnitName[static_cast<std::size_t>(u.carried)]);
        }
        o_ += '\n';
    }

    // Actors in id order, as the oracle iterates them.
    void actors() {
        struct Entry {
            std::int32_t id;
            bool is_city;
            std::size_t slot;
        };
        std::vector<Entry> all;
        for (std::size_t i = 0; i < s_->cities.size(); ++i) {
            if (s_->cities[i].id != 0 && s_->cities[i].tribe >= 0) {  // tribe -1: ghost
                all.push_back({.id = s_->cities[i].id, .is_city = true, .slot = i});
            }
        }
        for (std::size_t i = 0; i < s_->units.size(); ++i) {
            if (s_->units[i].id != 0) {
                all.push_back({.id = s_->units[i].id, .is_city = false, .slot = i});
            }
        }
        std::ranges::sort(all, {}, &Entry::id);
        for (const Entry& e : all) {
            if (e.is_city) {
                city(s_->cities[e.slot]);
            } else {
                unit(s_->units[e.slot]);
            }
        }
    }

    // Actor ids of a list, sorted if `sort`.
    template <typename List, typename F>
    static std::vector<std::int32_t> ids(const List& l, bool sort, const F& id) {
        std::vector<std::int32_t> v;
        v.reserve(l.size());
        for (const Slot x : l) {
            v.push_back(id(x));
        }
        if (sort) {
            std::ranges::sort(v);
        }
        return v;
    }

    const GameState* s_;
    int observer_;
    std::size_t tiles_;
    std::string o_;
};

}  // namespace

std::string dump_state(const GameState& s) { return Dumper(s).run(); }

std::string dump_observation(const Observation& obs) {
    return Dumper(obs.view(), obs.observer()).run();
}

}  // namespace poly
