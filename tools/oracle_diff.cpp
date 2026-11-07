// Differential test against oracle logs (oracle/bridge): loads the logged level into our simulator,
// then replays the log event by event (turn start, each action, turn end) and compares our state
// dump with the oracle's after every event, and our legal-action set with each logged one (as
// multisets: the log sorts them). Map-load logs (--init-only) hold only the first state.
//
// A log stops early, without failing, at the first action type the simulator does not implement
// yet; the summary counts those stops by type so the remaining work is visible.
//
// Usage: oracle_diff <log>...   Exit code 0 iff no log mismatched or errored.

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "poly/dump.hpp"
#include "poly/level.hpp"
#include "poly/rules.hpp"

namespace {

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string l;
    while (std::getline(ss, l)) {
        out.push_back(l);
    }
    return out;
}

std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream ss(s);
    std::string t;
    while (ss >> t) {
        out.push_back(t);
    }
    return out;
}

// Reports the first differing line and the first differing space-separated field in it.
void report_diff(std::ostream& os, const std::string& expected, const std::string& actual) {
    const auto e = split_lines(expected);
    const auto a = split_lines(actual);
    for (std::size_t i = 0; i < std::max(e.size(), a.size()); ++i) {
        const std::string el = i < e.size() ? e[i] : "<missing>";
        const std::string al = i < a.size() ? a[i] : "<missing>";
        if (el == al) {
            continue;
        }
        const auto et = tokens(el);
        const auto at = tokens(al);
        for (std::size_t f = 0; f < std::max(et.size(), at.size()); ++f) {
            const std::string ef = f < et.size() ? et[f] : "<end>";
            const std::string af = f < at.size() ? at[f] : "<end>";
            if (ef != af) {
                os << "  line " << i << " field " << f << ": oracle '" << ef.substr(0, 120)
                   << "' ours '" << af.substr(0, 120) << "'\n  oracle: " << el.substr(0, 160)
                   << "\n  ours:   " << al.substr(0, 160) << '\n';
                return;
            }
        }
        return;
    }
}

template <std::size_t N>
std::optional<std::uint8_t> lookup(const std::array<std::string_view, N>& names,
                                   const std::string& v) {
    for (std::size_t i = 0; i < N; ++i) {
        if (names[i] == v) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return std::nullopt;
}

std::uint8_t named(std::optional<std::uint8_t> v, const std::string& what) {
    if (!v) {
        throw std::runtime_error("unknown " + what);
    }
    return *v;
}

using Tokens = std::vector<std::string>;

int num(const Tokens& t, std::size_t i) { return std::stoi(t.at(i)); }
std::uint8_t u8(const Tokens& t, std::size_t i) { return static_cast<std::uint8_t>(num(t, i)); }

// Tribe actions: "<KIND> by tribe P ...".
std::optional<poly::Action> parse_tribe_action(const Tokens& t) {
    using poly::ActionType;
    const std::string& kind = t[0];
    if (kind == "END_TURN") {
        return poly::Action{.type = ActionType::EndTurn, .actor = num(t, 3)};
    }
    if (kind == "RESEARCH_TECH") {  // RESEARCH_TECH by tribe P : TECH
        return poly::Action{.type = ActionType::ResearchTech,
                            .actor = num(t, 3),
                            .param = named(lookup(poly::kTechName, t.at(5)), "tech")};
    }
    if (kind == "DECLARE_WAR") {  // DECLARE_WAR by tribe P on tribe Q
        return poly::Action{
            .type = ActionType::DeclareWar, .actor = num(t, 3), .target = num(t, 6)};
    }
    if (kind == "SEND_STARS") {  // SEND_STARS by tribe P to: Q : N stars
        return poly::Action{.type = ActionType::SendStars,
                            .actor = num(t, 3),
                            .target = num(t, 5),
                            .amount = static_cast<std::int16_t>(num(t, 7))};
    }
    if (kind == "BUILD_ROAD") {  // BUILD_ROAD by tribe P at location X : Y
        return poly::Action{
            .type = ActionType::BuildRoad, .actor = num(t, 3), .x = u8(t, 6), .y = u8(t, 8)};
    }
    return std::nullopt;
}

// City actions: "<KIND> by city C ...".
std::optional<poly::Action> parse_city_action(const Tokens& t) {
    using poly::ActionType;
    static const std::map<std::string, ActionType, std::less<>> kAtTile = {
        {"CLEAR_FOREST", ActionType::ClearForest},  // <KIND> by city C at X : Y
        {"GROW_FOREST", ActionType::GrowForest},
        {"BURN_FOREST", ActionType::BurnForest},
        {"DESTROY", ActionType::Destroy}};
    const std::string& kind = t[0];
    if (const auto it = kAtTile.find(kind); it != kAtTile.end()) {
        return poly::Action{.type = it->second, .actor = num(t, 3), .x = u8(t, 5), .y = u8(t, 7)};
    }
    if (kind == "RESOURCE_GATHERED") {  // RESOURCE_GATHERED by city C : RES at X : Y
        return poly::Action{.type = ActionType::GatherResource,
                            .actor = num(t, 3),
                            .x = u8(t, 7),
                            .y = u8(t, 9),
                            .param = named(lookup(poly::kResourceName, t.at(5)), "resource")};
    }
    if (kind == "LEVEL_UP") {  // LEVEL_UP by city C with bonus B
        return poly::Action{
            .type = ActionType::LevelUp,
            .actor = num(t, 3),
            .param = named(lookup(poly::kLevelUpBonusName, t.at(6)), "level-up bonus")};
    }
    if (kind == "SPAWN") {  // SPAWN by city C : UNIT
        return poly::Action{.type = ActionType::Spawn,
                            .actor = num(t, 3),
                            .param = named(lookup(poly::kUnitName, t.at(5)), "unit type")};
    }
    if (kind == "BUILD") {  // BUILD by city C at X : Y : TYPE
        return poly::Action{.type = ActionType::Build,
                            .actor = num(t, 3),
                            .x = u8(t, 5),
                            .y = u8(t, 7),
                            .param = named(lookup(poly::kBuildingName, t.at(9)), "building")};
    }
    return std::nullopt;
}

// Unit actions: "<KIND> by unit U ..." (DISBAND: "of unit U").
std::optional<poly::Action> parse_unit_action(const Tokens& t) {
    using poly::ActionType;
    static const std::map<std::string, ActionType, std::less<>> kUnitOnly = {
        {"EXAMINE", ActionType::Examine},  // EXAMINE by unit U of ruins.
        {"RECOVER", ActionType::Recover},
        {"DISBAND", ActionType::Disband},
        {"UPGRADE", ActionType::Upgrade},
        {"MAKE_VETERAN", ActionType::MakeVeteran}};
    const std::string& kind = t[0];
    if (const auto it = kUnitOnly.find(kind); it != kUnitOnly.end()) {
        return poly::Action{.type = it->second, .actor = num(t, 3)};
    }
    if (kind == "CAPTURE") {  // CAPTURE by unit U of target VILLAGE: -1 | CITY: <id>
        if (t.at(6) != "VILLAGE:" && t.at(6) != "CITY:") {
            throw std::runtime_error("malformed capture: " + t.at(6));
        }
        return poly::Action{.type = ActionType::Capture,
                            .actor = num(t, 3),
                            .target = t.at(6) == "CITY:" ? num(t, 7) : 0};
    }
    if (kind == "ATTACK") {  // ATTACK by unit U to unit T
        return poly::Action{.type = ActionType::Attack, .actor = num(t, 3), .target = num(t, 6)};
    }
    if (kind == "MOVE") {  // MOVE by unit U to X : Y
        return poly::Action{
            .type = ActionType::Move, .actor = num(t, 3), .x = u8(t, 5), .y = u8(t, 7)};
    }
    return std::nullopt;
}

// Parses a logged action (Tribes' toString; the bridge appends the tile to RESOURCE_GATHERED).
// Returns nullopt for action types the simulator does not implement yet.
std::optional<poly::Action> parse_action(const std::string& text) {
    const Tokens t = tokens(text);
    if (t.size() < 4) {
        throw std::runtime_error("malformed action: " + text);
    }
    if (t[2] == "tribe") {
        return parse_tribe_action(t);
    }
    if (t[2] == "city") {
        return parse_city_action(t);
    }
    return t[2] == "unit" ? parse_unit_action(t) : std::nullopt;
}

// One logged draw on Tribes' game RNG: "RAND <caller> <method> <bound> <value>".
struct Draw {
    std::string caller;
    int bound = 0;
    int value = 0;
};

// Replays Tribes' logged draws as semantic outcomes (ADR-005 §3). Every request must match the
// next draw's caller and domain (bound); a mismatch means the rules diverged in when or over what
// randomness is drawn, and fails the log at that point.
class ReplayChance final : public poly::Chance {
   public:
    explicit ReplayChance(std::vector<Draw> draws) : draws_(std::move(draws)) {}

    poly::RuinBonus ruin_bonus(bool exclude_research) override {
        int v = next("Types$EXAMINE_BONUS.random", poly::kNumRuinBonuses);
        while (exclude_research && v == static_cast<int>(poly::RuinBonus::Research)) {
            v = next("Types$EXAMINE_BONUS.random", poly::kNumRuinBonuses);
        }
        return static_cast<poly::RuinBonus>(v);
    }
    int random_tech(int count) override { return next("TechnologyTree.researchAtRandom", count); }
    int explorer_neighbour(int count) override { return next("Board.launchExplorer", count); }
    // java.util.Collections.shuffle: for i = n .. 2, swap(i - 1, nextInt(i)).
    void shuffle_cities(std::span<std::uint16_t> cities) override {
        for (std::size_t i = cities.size(); i > 1; --i) {
            const int j = next("Collections.shuffle", static_cast<int>(i));
            std::swap(cities[i - 1], cities[static_cast<std::size_t>(j)]);
        }
    }

    [[nodiscard]] std::size_t unused() const { return draws_.size() - pos_; }

   private:
    int next(const std::string& caller_prefix, int bound) {
        if (pos_ >= draws_.size()) {
            throw std::runtime_error("chance: we drew " + caller_prefix + " (bound " +
                                     std::to_string(bound) + "), the oracle drew nothing more");
        }
        const Draw& d = draws_[pos_++];
        if (!d.caller.starts_with(caller_prefix) || d.bound != bound) {
            throw std::runtime_error("chance: we drew " + caller_prefix + " (bound " +
                                     std::to_string(bound) + "), the oracle drew " + d.caller +
                                     " (bound " + std::to_string(d.bound) + ")");
        }
        return d.value;
    }

    std::vector<Draw> draws_;
    std::size_t pos_ = 0;
};

// Readable form of an action for mismatch reports (field dump, not Tribes' toString).
std::string show(const poly::Action& a) {
    static constexpr std::array<std::string_view, 21> kName = {
        "END_TURN",          "RESEARCH_TECH", "DECLARE_WAR", "SEND_STARS",   "BUILD_ROAD",
        "RESOURCE_GATHERED", "LEVEL_UP",      "SPAWN",       "CLEAR_FOREST", "GROW_FOREST",
        "BURN_FOREST",       "BUILD",         "DESTROY",     "RECOVER",      "MOVE",
        "EXAMINE",           "CAPTURE",       "ATTACK",      "DISBAND",      "UPGRADE",
        "MAKE_VETERAN"};
    std::ostringstream os;
    os << kName.at(static_cast<std::size_t>(a.type)) << " actor " << a.actor << " target "
       << a.target << " at " << int{a.x} << ':' << int{a.y} << " param " << int{a.param}
       << " amount " << a.amount;
    return os.str();
}

bool action_less(const poly::Action& a, const poly::Action& b) {
    return std::tie(a.type, a.actor, a.target, a.x, a.y, a.param, a.amount) <
           std::tie(b.type, b.actor, b.target, b.x, b.y, b.param, b.amount);
}

struct Outcome {
    bool failed = false;
    int states = 0;          // states compared and equal
    int action_sets = 0;     // legal-action sets compared and equal
    std::string stopped_at;  // first unimplemented action type, if any
    std::string detail;      // where and how the first mismatch happened
};

class Replay {
   public:
    explicit Replay(std::istream& in) : in_(&in) {}

    Outcome run() {
        std::string line;
        while (std::getline(*in_, line)) {
            if (!handle(line)) {
                return out_;
            }
        }
        if (!loaded_) {
            throw std::runtime_error("log is missing GAME, LEVEL or STATE");
        }
        if (turns_ && results_ != s_.num_tribes) {  // a game log (not --init-only) was cut short
            throw std::runtime_error("log ends without GAMEEND and a RESULT per tribe");
        }
        return out_;
    }

   private:
    // One log line; false stops the replay (mismatch, or an unimplemented action).
    bool handle(std::string line) {
        if (line.starts_with("GAME ")) {
            game(line);
        } else if (line.starts_with("LEVEL ")) {
            const int n = std::stoi(line.substr(6));
            for (int i = 0; i < n && std::getline(*in_, line); ++i) {
                level_.push_back(line);
            }
        } else if (line.starts_with("RAND ")) {
            draw(line);
        } else if (line.starts_with("TURN ")) {
            turn(line);
        } else if (line.starts_with("ACT ")) {
            return act(line.substr(4));
        } else if (line.starts_with("STATE ")) {
            out_.failed = !state(line);
        } else if (line.starts_with("ACTIONS ")) {
            return actions(std::stoi(line.substr(8)));
        } else if (line.starts_with("GAMEEND ")) {
            game_end(std::stoi(line.substr(8)));
        } else if (line.starts_with("RESULT ")) {
            out_.failed = !result(line);
        }
        return !out_.failed;
    }

    void game(const std::string& line) {
        std::istringstream ss(line.substr(5));
        std::string level_seed;
        std::string game_seed;
        std::string mode;
        int full = 1;
        ss >> level_seed >> game_seed >> mode >> full;
        mode_ = mode == "SCORE" ? poly::GameMode::Score : poly::GameMode::Capitals;
        obs_ = full != 0 ? poly::ObsMode::Full : poly::ObsMode::Partial;
    }

    void draw(const std::string& line) {
        const auto t = tokens(line);  // RAND <caller> <method> <bound> <value>
        if (t.size() != 5 || t[2] != "nextInt") {
            throw std::runtime_error("unsupported draw: " + line);
        }
        draws_.push_back({.caller = t[1], .bound = std::stoi(t[3]), .value = std::stoi(t[4])});
    }

    void turn(const std::string& line) {
        std::istringstream ss(line.substr(5));
        int tick = 0;
        int tribe = 0;
        ss >> tick >> tribe;
        const int before = s_.tick;
        const int p = poly::next_turn_tribe(s_);
        if (s_.tick != before &&
            poly::update_game_over(s_)) {  // the bridge checks before each tick
            throw std::runtime_error("game over at tick " + std::to_string(s_.tick) +
                                     ", the oracle played on");
        }
        if (p != tribe || s_.tick != tick) {
            throw std::runtime_error("turn order: oracle (" + std::to_string(tick) + ", " +
                                     std::to_string(tribe) + ") ours (" + std::to_string(s_.tick) +
                                     ", " + std::to_string(p) + ")");
        }
        pending_ = Pending::TurnStart;
        event_ = "turn start " + line.substr(5);
        turns_ = true;
    }

    // The action runs when its STATE arrives, so the draws logged in between are available.
    bool act(const std::string& text) {
        const auto a = parse_action(text);
        if (!a) {
            out_.stopped_at = tokens(text).at(0);
            return false;
        }
        action_ = *a;
        pending_ = a->type == poly::ActionType::EndTurn ? Pending::TurnEnd : Pending::Action;
        event_ = "after " + text;
        return true;
    }

    // ACTIONS <n> then n "A <action>" lines: the oracle's legal set in the state just dumped.
    // Compared as sorted multisets; a logged type we cannot parse stops the replay like ACT does.
    bool actions(int n) {
        std::vector<poly::Action> theirs;
        std::string line;
        for (int k = 0; k < n; ++k) {
            if (!std::getline(*in_, line) || !line.starts_with("A ")) {
                throw std::runtime_error("ACTIONS block cut short");
            }
            const auto a = parse_action(line.substr(2));
            if (!a) {
                out_.stopped_at = tokens(line.substr(2)).at(0);
                return false;
            }
            theirs.push_back(*a);
        }
        poly::legal_actions(s_, legal_);
        std::vector<poly::Action> ours(legal_.begin(), legal_.end());
        std::ranges::sort(theirs, action_less);
        std::ranges::sort(ours, action_less);
        if (ours == theirs) {
            ++out_.action_sets;
            return true;
        }
        std::vector<poly::Action> missing;
        std::vector<poly::Action> extra;
        std::ranges::set_difference(theirs, ours, std::back_inserter(missing), action_less);
        std::ranges::set_difference(ours, theirs, std::back_inserter(extra), action_less);
        std::ostringstream os;
        os << "  legal actions " << event_ << " (state " << out_.states << "): oracle "
           << theirs.size() << ", ours " << ours.size() << '\n';
        for (std::size_t k = 0; k < std::min<std::size_t>(missing.size(), 5); ++k) {
            os << "  missing: " << show(missing[k]) << '\n';
        }
        for (std::size_t k = 0; k < std::min<std::size_t>(extra.size(), 5); ++k) {
            os << "  extra:   " << show(extra[k]) << '\n';
        }
        out_.detail = os.str();
        out_.failed = true;
        return false;
    }

    // GAMEEND <tick>: the game ended by a capture or elimination (already over), or by the turn
    // limit, checked at the loop head after the tick advanced (04 §8).
    void game_end(int tick) {
        if (!s_.game_over) {
            ++s_.tick;
            poly::update_game_over(s_);
        }
        if (!s_.game_over || s_.tick != tick) {
            throw std::runtime_error(
                "game end: oracle at tick " + std::to_string(tick) + ", ours " +
                (s_.game_over ? "at tick " + std::to_string(s_.tick) : "not over"));
        }
        ended_ = true;
    }

    // RESULT <tribeId> <result> <score>
    bool result(const std::string& line) {
        const auto t = tokens(line);
        const auto& tr = s_.tribes.at(static_cast<std::size_t>(std::stoi(t.at(1))));
        const std::string ours =
            "RESULT " + t.at(1) + " " +
            std::string(poly::kResultName[static_cast<std::size_t>(tr.result)]) + " " +
            std::to_string(tr.score);
        if (!ended_ || ours != line) {
            out_.detail = "  at game end\n  oracle: " + line + "\n  ours:   " + ours + '\n';
            return false;
        }
        ++results_;
        return true;
    }

    void run_pending() {
        ReplayChance chance(std::move(draws_));
        draws_.clear();
        const Pending done = pending_;
        switch (pending_) {
            case Pending::Load:
                s_ = poly::load_level(level_, mode_, obs_);
                event_ = "initial state";
                break;
            case Pending::TurnStart:
                poly::start_turn(s_, s_.active_tribe);
                break;
            case Pending::Action:
                poly::apply_action(s_, action_, chance);
                break;
            case Pending::TurnEnd:
                poly::end_turn(s_);
                break;
            case Pending::None:
                // Two states in a row: the game ended mid-turn and the turn end followed (04 §2).
                poly::end_turn(s_);
                event_ = "turn end after game over";
                break;
        }
        pending_ = Pending::None;
        check_over_ = done == Pending::Action || done == Pending::TurnEnd || done == Pending::None;
        if (chance.unused() > 0) {
            throw std::runtime_error("chance: the oracle drew " + std::to_string(chance.unused()) +
                                     " more value(s) than we did, " + event_);
        }
    }

    bool state(std::string block) {
        std::string line;
        while (block.substr(block.rfind('\n') + 1) != "END" && std::getline(*in_, line)) {
            block += '\n' + line;
        }
        run_pending();
        loaded_ = true;
        const std::string ours = poly::dump_state(s_);
        if (ours != block) {
            std::ostringstream os;
            os << "  at " << event_ << " (state " << out_.states << ")\n";
            report_diff(os, block, ours);
            out_.detail = os.str();
            return false;
        }
        ++out_.states;
        if (check_over_) {  // the bridge checks after each action and turn end, after the dump
            poly::update_game_over(s_);
        }
        return true;
    }

    std::istream* in_;
    poly::GameMode mode_ = poly::GameMode::Capitals;
    poly::ObsMode obs_ = poly::ObsMode::Full;
    std::vector<std::string> level_;
    poly::GameState s_{};
    poly::ActionList legal_;
    enum class Pending : std::uint8_t { Load, TurnStart, Action, TurnEnd, None };
    Pending pending_ = Pending::Load;
    poly::Action action_;
    std::vector<Draw> draws_;
    bool loaded_ = false;
    bool check_over_ = false;
    bool ended_ = false;
    bool turns_ = false;
    int results_ = 0;
    std::string event_;
    Outcome out_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: oracle_diff <log>...\n";
        return 2;
    }
    const std::vector<std::string> paths(
        argv + 1, argv + argc);  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    int failures = 0;
    int complete = 0;
    long long states = 0;
    long long action_sets = 0;
    std::map<std::string, int> stops;
    for (const std::string& path : paths) {
        try {
            std::ifstream in(path);
            if (!in) {
                throw std::runtime_error("cannot open");
            }
            const Outcome r = Replay(in).run();
            states += r.states;
            action_sets += r.action_sets;
            if (r.failed) {
                ++failures;
                std::cerr << "MISMATCH " << path << '\n' << r.detail;
            } else if (!r.stopped_at.empty()) {
                ++stops[r.stopped_at];
            } else {
                ++complete;
            }
        } catch (const std::exception& e) {
            ++failures;
            std::cerr << "ERROR " << path << ": " << e.what() << '\n';
        }
    }
    std::cout << paths.size() << " logs: " << complete << " replayed to the end, " << failures
              << " failures, " << states << " states and " << action_sets
              << " legal-action sets matched\n";
    for (const auto& [type, n] : stops) {
        std::cout << "  stopped at unimplemented " << type << ": " << n << '\n';
    }
    return failures == 0 ? 0 : 1;
}
