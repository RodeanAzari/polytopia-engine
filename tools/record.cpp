// Records one game as a replay for the viewer: a uniform-random game (tools/random_game.hpp), or
// with --agents a game between two agents in the tournament loop (search/match.hpp), seeded as the
// tournament seeds a game (tools/agents.hpp): pass a CSV row's game seed and its two agents in seat
// order to re-record that game. Replays of
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
// A state key equal to the previous frame's value is omitted; the viewer carries it forward (d, hl
// and vc are per frame, never carried).
// Agent games add "agents" and "game_seed" at the top, and on the frame of each action an agent
// chose by PUCT search, vc: the root actions with the most visits (summed over its trees), at most
// 8, most visited first, each [x0, y0, x1, y1, visits, description] (tiles as hl; -1 when none),
// and vt: the root's total visits.
//
// Usage: record <level-file> <out.json> [--partial] [--agents=A,B [--seed=<game seed>]]
//   --partial: fog of war (ADR-004); --agents: tournament agent names (default: a random game);
//   --seed: the game seed (a tournament CSV's game_seed column; default: the level seed).

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "agents.hpp"
#include "random_game.hpp"
#include "search/match.hpp"

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

    // `extra`: further JSON members for this frame only (",\"vc\":..."), or empty.
    void frame(const poly::GameState& s, const std::string& event, const Described& d,
               const std::string& extra = {}) {
        *out_ << (frames_ == 0 ? "\n" : ",\n") << "{\"t\":" << s.tick << ",\"p\":" << s.active_tribe
              << ",\"e\":" << quoted(event) << ",\"d\":" << quoted(d.text);
        if (d.x0 >= 0 && d.x1 >= 0) {
            *out_ << ",\"hl\":[" << d.x0 << ',' << d.y0 << ',' << d.x1 << ',' << d.y1 << ']';
        } else if (d.x0 >= 0 || d.x1 >= 0) {  // one tile only
            const int x = d.x0 >= 0 ? d.x0 : d.x1;
            const int y = d.x0 >= 0 ? d.y0 : d.y1;
            *out_ << ",\"hl\":[" << x << ',' << y << ',' << x << ',' << y << ']';
        }
        *out_ << extra;
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

// Plays as `inner` and keeps, after each PUCT search, the root's most visited actions as a frame's
// vc/vt members (see the file comment); other agents and unsearched decisions leave none.
class Watched final : public poly::search::Agent {
   public:
    explicit Watched(std::unique_ptr<poly::search::Agent> inner)
        : inner_(std::move(inner)),
          puct_(dynamic_cast<const poly::search::PuctAgent*>(inner_.get())) {}

    poly::Action act(const poly::Observation& obs, std::span<const poly::Action> legal) override {
        const poly::Action a = inner_->act(obs, legal);
        pending_.clear();
        if (puct_ != nullptr && puct_->last_fm_calls() > 0) {
            pending_ = visits_json(obs.view());
        }
        return a;
    }

    // The last decision's vc/vt members, once (empty if none).
    std::string take() { return std::exchange(pending_, {}); }

   private:
    [[nodiscard]] std::string visits_json(const poly::GameState& view) const {
        const auto actions = puct_->root_actions();
        const auto edges = puct_->summed();
        std::vector<std::size_t> order(actions.size());
        long long total = 0;
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
            total += edges[i].visits;
        }
        std::ranges::stable_sort(
            order, [&](std::size_t a, std::size_t b) { return edges[a].visits > edges[b].visits; });
        std::ostringstream o;
        o << ",\"vt\":" << total << ",\"vc\":[";
        for (std::size_t k = 0; k < std::min<std::size_t>(8, order.size()); ++k) {
            const std::size_t i = order[k];
            const Described d = describe(view, actions[i]);
            o << (k > 0 ? "," : "") << '[' << d.x0 << ',' << d.y0 << ',' << d.x1 << ',' << d.y1
              << ',' << edges[i].visits << ',' << quoted(d.text) << ']';
        }
        o << ']';
        return o.str();
    }

    std::unique_ptr<poly::search::Agent> inner_;
    const poly::search::PuctAgent* puct_;
    std::string pending_;
};

}  // namespace

struct Options {
    std::string level;
    std::string path;
    bool partial = false;
    std::vector<std::string> agents;  // empty: a random game
    std::optional<std::uint64_t> seed;
};

std::optional<Options> parse(const std::vector<std::string>& args) {
    Options o;
    std::vector<std::string> positional;
    for (const std::string& a : args) {
        if (a == "--partial") {
            o.partial = true;
        } else if (a.starts_with("--agents=")) {
            const std::string list = a.substr(9);
            const std::size_t comma = list.find(',');
            if (comma == std::string::npos) {
                return std::nullopt;
            }
            o.agents = {list.substr(0, comma), list.substr(comma + 1)};
        } else if (a.starts_with("--seed=")) {
            o.seed = std::stoull(a.substr(7));
        } else if (a.starts_with("--")) {
            return std::nullopt;
        } else {
            positional.push_back(a);
        }
    }
    if (positional.size() != 2 || (o.seed && o.agents.empty())) {
        return std::nullopt;
    }
    o.level = positional[0];
    o.path = positional[1];
    return o;
}

void write_header(std::ostream& out, const polytools::LevelFile& f, const Options& o,
                  std::uint64_t game_seed) {
    const poly::GameState& s0 = f.state;
    out << R"({"format":"poly-replay-1","seed":)" << f.seed << ",\"mode\":" << quoted(f.mode)
        << ",\"obs\":" << quoted(o.partial ? "partial" : "full") << ",\"side\":" << s0.side;
    if (!o.agents.empty()) {
        out << ",\"agents\":[" << quoted(o.agents[0]) << ',' << quoted(o.agents[1])
            << "],\"game_seed\":" << game_seed;
    }
    out << ",\"tribes\":[";
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
}

std::string turn_start_text(const poly::GameState& s) {
    return "Tribe " + std::to_string(s.active_tribe) + " starts turn " + std::to_string(s.tick);
}

// A uniform-random game; returns the final state.
poly::GameState record_random(polytools::LevelFile& f, Recorder& rec) {
    poly::GameState before = f.state;  // the state before each event, for action descriptions
    polytools::play_random(
        f.state, f.seed, [&](const poly::GameState& s, polytools::Event e, const poly::Action& a) {
            if (e == polytools::Event::TurnStart) {
                rec.frame(s, "start", {.text = turn_start_text(s)});
            } else if (e == polytools::Event::Action) {
                rec.frame(s, "action", describe(before, a));
            } else {
                rec.frame(s, "end", {.text = "End turn"});
            }
            before = s;
        });
    return f.state;
}

// A game between o.agents in the tournament loop, seeded as tools/tournament.cpp seeds game
// `game_seed`; returns the final state and prints the result as a CSV row would show it.
poly::GameState record_match(const polytools::LevelFile& f, const Options& o,
                             std::uint64_t game_seed, Recorder& rec) {
    Watched a0(
        polytools::make_agent(o.agents[0], polytools::agent_seed(game_seed, 0), f.state.side));
    Watched a1(
        polytools::make_agent(o.agents[1], polytools::agent_seed(game_seed, 1), f.state.side));
    std::array<Watched*, 2> watched = {&a0, &a1};
    const std::array<poly::search::Agent*, 2> seats = {&a0, &a1};
    poly::GameState before = f.state;
    poly::GameState last = f.state;
    const auto r = poly::search::play_match(
        f.state, seats, game_seed,
        [&](const poly::GameState& s, poly::search::MatchEvent e, const poly::Action& a) {
            Watched& w = *watched[static_cast<std::size_t>(s.active_tribe)];
            if (e == poly::search::MatchEvent::TurnStart) {
                rec.frame(s, "start", {.text = turn_start_text(s)});
            } else if (e == poly::search::MatchEvent::Action) {
                rec.frame(s, "action", describe(before, a), w.take());
            } else {
                rec.frame(s, "end", {.text = "End turn"}, w.take());
            }
            before = s;
            last = s;
        });
    // play_match decides the game after the last event: take the outcome from its result.
    last.tick = r.ticks;
    for (int p = 0; p < last.num_tribes; ++p) {
        last.tribes[static_cast<std::size_t>(p)].result =
            p == r.winner ? poly::Result::Win : poly::Result::Loss;
    }
    const char* result0 = "LOSS";
    if (r.draw) {
        result0 = "DRAW";
    } else if (r.winner == 0) {
        result0 = "WIN";
    }
    std::cout << "result0 " << result0 << ", scores " << r.score[0] << ' ' << r.score[1]
              << ", ticks " << r.ticks << ", decisions " << r.decisions << '\n';
    return last;
}

int main(int argc, char** argv) {
    const std::vector<std::string> args(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    std::optional<Options> o;
    try {
        o = parse(args);
    } catch (const std::exception&) {
        o.reset();  // a malformed --seed
    }
    if (!o) {
        std::cerr << "usage: record <level-file> <out.json> [--partial] [--agents=A,B "
                     "[--seed=<game seed>]]\n";
        return 2;
    }
    try {
        polytools::LevelFile f = polytools::load_level_file(
            o->level, o->partial ? poly::ObsMode::Partial : poly::ObsMode::Full);
        const std::uint64_t game_seed = o->seed.value_or(f.seed);
        std::ofstream out(o->path);
        if (!out) {
            throw std::runtime_error("cannot write " + o->path);
        }
        write_header(out, f, *o, game_seed);
        Recorder rec(out);
        rec.frame(f.state, "load", {.text = "Initial state"});
        const poly::GameState end =
            o->agents.empty() ? record_random(f, rec) : record_match(f, *o, game_seed, rec);
        out << "\n],\"result\":[";
        for (int p = 0; p < end.num_tribes; ++p) {
            const poly::TribeState& t = end.tribes[static_cast<std::size_t>(p)];
            out << (p > 0 ? "," : "")
                << quoted(std::string(poly::kResultName[static_cast<std::size_t>(t.result)]));
        }
        out << "]}\n";
        std::cout << o->path << ": " << rec.frames() << " frames, game over at tick " << end.tick
                  << '\n';
    } catch (const std::exception& e) {
        std::cerr << "record: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
