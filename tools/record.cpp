// Records one uniform-random game (tools/random_game.hpp) as a replay for the viewer
// (tools/viewer/index.html, `make replay`). JSON, our own schema "poly-replay-1":
//
//   {"format":"poly-replay-1", "seed", "mode", "obs", "side", "tribes":[type name per tribe id],
//    "units":[unit type names], "buildings":[building names], "frames":[frame...]}
//
// One frame after the initial load and after every turn start, executed action and turn end:
//   t tick, p active tribe, e "load"|"start"|"action"|"end", d description, hl [x0,y0,x1,y1]
//   (actions: actor tile -> target tile, or omitted), and the state: ter  terrain chars per tile
//   (index x * side + y; 01 map chars), res resource chars ('-' none), bld  building per tile ('-'
//   none, else 'A' + Building enum), road '0'/'1' per tile (Tribes' network tiles), own owning city
//   id per tile (0 none), vis[tribe] visible tiles as '0'/'1', cities [[id, tribe, x, y, level,
//   pop, need, capital, walls]], units [[id, type, tribe, x, y, hp, maxHp, veteran, carried type or
//   -1]], tr [[stars, score, result 0 win/1 loss/2 playing, techs]].
// A state key equal to the previous frame's value is omitted; the viewer carries it forward (d and
// hl are per frame, never carried).
//
// Usage: record <level-file> <out.json> [--partial]   (--partial: fog of war, ADR-004)

#include <bit>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "random_game.hpp"

namespace {

using poly::ActionType;

const poly::Unit* find_unit(const poly::GameState& s, std::int32_t id) {
    for (const poly::Unit& u : s.units) {
        if (u.id != 0 && u.id == id) {
            return &u;
        }
    }
    return nullptr;
}

const poly::City* find_city(const poly::GameState& s, std::int32_t id) {
    for (const poly::City& c : s.cities) {
        if (c.id != 0 && c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

std::string unit_label(const poly::GameState& s, std::int32_t id) {
    const poly::Unit* u = find_unit(s, id);
    return u == nullptr ? "unit " + std::to_string(id)
                        : std::string(poly::kUnitName[static_cast<std::size_t>(u->type)]) + " " +
                              std::to_string(id) + " at " + std::to_string(u->x) + "," +
                              std::to_string(u->y);
}

struct Described {
    std::string text;
    int x0 = -1, y0 = -1, x1 = -1, y1 = -1;
};

// `s` is the state before the action.
Described describe(const poly::GameState& s, const poly::Action& a) {
    Described d;
    const std::string at = std::to_string(a.x) + "," + std::to_string(a.y);
    const poly::Unit* u = find_unit(s, a.actor);
    const poly::City* c = find_city(s, a.actor);
    if (u != nullptr) {
        d.x0 = u->x;
        d.y0 = u->y;
    } else if (c != nullptr) {
        d.x0 = c->x;
        d.y0 = c->y;
    }
    const auto target_unit = [&] {
        if (const poly::Unit* t = find_unit(s, a.target)) {
            d.x1 = t->x;
            d.y1 = t->y;
        }
        return unit_label(s, a.target);
    };
    const auto tile = [&] {
        d.x1 = a.x;
        d.y1 = a.y;
    };
    const std::string city = c == nullptr ? "" : "city " + std::to_string(a.actor);
    switch (a.type) {
        case ActionType::EndTurn:
            d.text = "End turn";
            break;
        case ActionType::ResearchTech:
            d.text = "Research " + std::string(poly::kTechName[a.param]);
            break;
        case ActionType::DeclareWar:
            d.text = "Declare war on tribe " + std::to_string(a.target);
            break;
        case ActionType::SendStars:
            d.text =
                "Send " + std::to_string(a.amount) + " stars to tribe " + std::to_string(a.target);
            break;
        case ActionType::BuildRoad:
            tile();
            d.text = "Build road at " + at;
            break;
        case ActionType::GatherResource:
            tile();
            d.text = city + ": gather " + std::string(poly::kResourceName[a.param]) + " at " + at;
            break;
        case ActionType::LevelUp:
            d.text = city + ": level up, " + std::string(poly::kLevelUpBonusName[a.param]);
            break;
        case ActionType::Spawn:
            d.text = city + ": spawn " + std::string(poly::kUnitName[a.param]);
            break;
        case ActionType::ClearForest:
            tile();
            d.text = city + ": clear forest at " + at;
            break;
        case ActionType::GrowForest:
            tile();
            d.text = city + ": grow forest at " + at;
            break;
        case ActionType::BurnForest:
            tile();
            d.text = city + ": burn forest at " + at;
            break;
        case ActionType::Build:
            tile();
            d.text = city + ": build " + std::string(poly::kBuildingName[a.param]) + " at " + at;
            break;
        case ActionType::Destroy:
            tile();
            d.text = city + ": destroy building at " + at;
            break;
        case ActionType::Recover:
            d.text = unit_label(s, a.actor) + ": recover";
            break;
        case ActionType::Move:
            tile();
            d.text = unit_label(s, a.actor) + ": move to " + at;
            break;
        case ActionType::Examine:
            d.text = unit_label(s, a.actor) + ": examine ruins";
            break;
        case ActionType::Capture:
            d.text = unit_label(s, a.actor) + ": capture " +
                     (a.target == 0 ? std::string("village") : "city " + std::to_string(a.target));
            break;
        case ActionType::Attack:
            d.text = unit_label(s, a.actor) + ": attack ";
            d.text += target_unit();
            break;
        case ActionType::Disband:
            d.text = unit_label(s, a.actor) + ": disband";
            break;
        case ActionType::Upgrade:
            d.text = unit_label(s, a.actor) + ": upgrade";
            break;
        case ActionType::MakeVeteran:
            d.text = unit_label(s, a.actor) + ": become veteran";
            break;
        case ActionType::HealOthers:
            d.text = unit_label(s, a.actor) + ": heal others";
            break;
        case ActionType::Convert:
            d.text = unit_label(s, a.actor) + ": convert ";
            d.text += target_unit();
            break;
    }
    return d;
}

std::string quoted(const std::string& v) {
    std::string out = "\"";
    for (const char ch : v) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
        }
        out += ch;
    }
    return out + '"';
}

using Fields = std::map<std::string, std::string>;  // frame key -> JSON value

// Per-tile strings and the owner list (index x * side + y).
void board_fields(const poly::GameState& s, Fields& out) {
    const int n = s.side * s.side;
    std::string ter;
    std::string res;
    std::string bld;
    std::string road;
    std::ostringstream own;
    own << '[';
    for (int i = 0; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        ter += poly::kTerrainChar[static_cast<std::size_t>(s.terrain[k])];
        res += s.resource[k] == poly::Resource::None
                   ? '-'
                   : poly::kResourceChar[static_cast<std::size_t>(s.resource[k])];
        bld += s.building[k] == poly::Building::None
                   ? '-'
                   : static_cast<char>('A' + static_cast<int>(s.building[k]));
        road += s.network[k] ? '1' : '0';
        own << (i > 0 ? "," : "")
            << (s.city_owner[k] == poly::kNoSlot ? 0 : s.cities[s.city_owner[k]].id);
    }
    own << ']';
    out["ter"] = quoted(ter);
    out["res"] = quoted(res);
    out["bld"] = quoted(bld);
    out["road"] = quoted(road);
    out["own"] = own.str();
}

// Visibility and stats per tribe.
void tribe_fields(const poly::GameState& s, Fields& out) {
    const int n = s.side * s.side;
    std::ostringstream vis;
    std::ostringstream tr;
    vis << '[';
    tr << '[';
    for (int p = 0; p < s.num_tribes; ++p) {
        const poly::TribeState& t = s.tribes[static_cast<std::size_t>(p)];
        std::string bits;
        for (int i = 0; i < n; ++i) {
            bits += t.visible[static_cast<std::size_t>(i)] ? '1' : '0';
        }
        vis << (p > 0 ? "," : "") << quoted(bits);
        tr << (p > 0 ? "," : "") << '[' << t.stars << ',' << t.score << ','
           << static_cast<int>(t.result) << ',' << std::popcount(t.techs) << ']';
    }
    vis << ']';
    tr << ']';
    out["vis"] = vis.str();
    out["tr"] = tr.str();
}

// Live cities and units, in slot order.
void actor_fields(const poly::GameState& s, Fields& out) {
    std::ostringstream cities;
    const char* sep = "";
    cities << '[';
    for (const poly::City& c : s.cities) {
        if (c.id != 0) {
            cities << sep << '[' << c.id << ',' << c.tribe << ',' << int{c.x} << ',' << int{c.y}
                   << ',' << c.level << ',' << c.population << ',' << c.population_need << ','
                   << int{c.capital} << ',' << int{c.walls} << ']';
            sep = ",";
        }
    }
    cities << ']';
    std::ostringstream units;
    sep = "";
    units << '[';
    for (const poly::Unit& u : s.units) {
        if (u.id != 0) {
            const bool naval = u.type == poly::UnitType::Boat || u.type == poly::UnitType::Ship ||
                               u.type == poly::UnitType::Battleship;
            units << sep << '[' << u.id << ',' << static_cast<int>(u.type) << ',' << u.tribe << ','
                  << int{u.x} << ',' << int{u.y} << ',' << u.hp << ',' << u.max_hp << ','
                  << int{u.veteran} << ',' << (naval ? static_cast<int>(u.carried) : -1) << ']';
            sep = ",";
        }
    }
    units << ']';
    out["cities"] = cities.str();
    out["units"] = units.str();
}

Fields fields(const poly::GameState& s) {
    Fields out;
    board_fields(s, out);
    tribe_fields(s, out);
    actor_fields(s, out);
    return out;
}

class Recorder {
   public:
    explicit Recorder(std::ostream& out) : out_(&out) {}

    void frame(const poly::GameState& s, const std::string& event, const Described& d) {
        *out_ << (frames_ == 0 ? "\n" : ",\n") << "{\"t\":" << s.tick << ",\"p\":" << s.active_tribe
              << ",\"e\":" << quoted(event) << ",\"d\":" << quoted(d.text);
        if (d.x0 >= 0 && d.x1 >= 0) {
            *out_ << ",\"hl\":[" << d.x0 << ',' << d.y0 << ',' << d.x1 << ',' << d.y1 << ']';
        } else if (d.x0 >= 0 || d.x1 >= 0) {  // one tile only
            const int x = d.x0 >= 0 ? d.x0 : d.x1;
            const int y = d.x0 >= 0 ? d.y0 : d.y1;
            *out_ << ",\"hl\":[" << x << ',' << y << ',' << x << ',' << y << ']';
        }
        for (auto& [key, value] : fields(s)) {
            if (prev_[key] != value) {
                *out_ << ",\"" << key << "\":" << value;
                prev_[key] = std::move(value);
            }
        }
        *out_ << '}';
        ++frames_;
    }

    [[nodiscard]] int frames() const { return frames_; }

   private:
    std::ostream* out_;
    std::map<std::string, std::string> prev_;
    int frames_ = 0;
};

}  // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    const bool partial = args.size() == 3 && args[2] == "--partial";
    if (args.size() != 2 && !partial) {
        std::cerr << "usage: record <level-file> <out.json> [--partial]\n";
        return 2;
    }
    try {
        const std::string& level = args[0];
        const std::string& path = args[1];
        polytools::LevelFile f = polytools::load_level_file(
            level, partial ? poly::ObsMode::Partial : poly::ObsMode::Full);
        std::ofstream out(path);
        if (!out) {
            throw std::runtime_error("cannot write " + path);
        }
        const poly::GameState& s0 = f.state;
        out << R"({"format":"poly-replay-1","seed":)" << f.seed << ",\"mode\":" << quoted(f.mode)
            << ",\"obs\":" << quoted(partial ? "partial" : "full") << ",\"side\":" << s0.side
            << ",\"tribes\":[";
        for (int p = 0; p < s0.num_tribes; ++p) {
            out << (p > 0 ? "," : "")
                << quoted(std::string(poly::kTribeName[static_cast<std::size_t>(
                       s0.tribes[static_cast<std::size_t>(p)].type)]));
        }
        out << "],\"units\":[";
        for (std::size_t k = 0; k < poly::kUnitName.size(); ++k) {
            out << (k > 0 ? "," : "") << quoted(std::string(poly::kUnitName[k]));
        }
        out << "],\"buildings\":[";
        for (std::size_t k = 0; k < poly::kBuildingName.size(); ++k) {
            out << (k > 0 ? "," : "") << quoted(std::string(poly::kBuildingName[k]));
        }
        out << "],\"frames\":[";

        Recorder rec(out);
        rec.frame(s0, "load", {.text = "Initial state"});
        poly::GameState before = s0;  // the state before each event, for action descriptions
        polytools::play_random(
            f.state, f.seed,
            [&](const poly::GameState& s, polytools::Event e, const poly::Action& a) {
                switch (e) {
                    case polytools::Event::TurnStart:
                        rec.frame(s, "start",
                                  {.text = "Tribe " + std::to_string(s.active_tribe) +
                                           " starts turn " + std::to_string(s.tick)});
                        break;
                    case polytools::Event::Action:
                        rec.frame(s, "action", describe(before, a));
                        break;
                    case polytools::Event::TurnEnd:
                        rec.frame(s, "end", {.text = "End turn"});
                        break;
                }
                before = s;
            });
        out << "\n],\"result\":[";
        for (int p = 0; p < f.state.num_tribes; ++p) {
            const poly::TribeState& t = f.state.tribes[static_cast<std::size_t>(p)];
            out << (p > 0 ? "," : "")
                << quoted(std::string(poly::kResultName[static_cast<std::size_t>(t.result)]));
        }
        out << "]}\n";
        std::cout << path << ": " << rec.frames() << " frames, game over at tick " << f.state.tick
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << "record: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
