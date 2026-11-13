#include "selfplay/record.hpp"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <stdexcept>

namespace poly::selfplay {

static_assert(std::endian::native == std::endian::little, "records are little-endian");

namespace {

constexpr std::array<char, 8> kMagic = {'P', 'L', 'Y', 'R', 'E', 'C', '1', '\0'};

class Out {
   public:
    template <typename T>
    void put(const T& v) {
        std::array<char, sizeof(T)> b{};
        std::memcpy(b.data(), &v, sizeof(T));
        buf_.insert(buf_.end(), b.begin(), b.end());
    }
    template <typename T>
    void put_all(const std::vector<T>& v) {
        for (const T& x : v) {
            put(x);
        }
    }
    void put_bytes(const std::string& s) { buf_.insert(buf_.end(), s.begin(), s.end()); }
    [[nodiscard]] const std::vector<char>& bytes() const { return buf_; }

   private:
    std::vector<char> buf_;
};

class In {
   public:
    explicit In(std::vector<char> b) : buf_(std::move(b)) {}
    template <typename T>
    T get() {
        need(sizeof(T));
        T v{};
        std::memcpy(&v, &buf_[pos_], sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    template <typename T>
    std::vector<T> get_all(std::size_t n) {
        std::vector<T> v(n);
        for (T& x : v) {
            x = get<T>();
        }
        return v;
    }
    std::string get_bytes(std::size_t n) {
        need(n);
        std::string s(&buf_[pos_], n);
        pos_ += n;
        return s;
    }
    [[nodiscard]] bool done() const { return pos_ == buf_.size(); }

   private:
    void need(std::size_t n) const {
        if (pos_ + n > buf_.size()) {
            throw std::runtime_error("record: truncated");
        }
    }
    std::vector<char> buf_;
    std::size_t pos_ = 0;
};

}  // namespace

Decision make_decision(const Features& f, int to_move,
                       std::vector<std::pair<std::uint32_t, std::uint32_t>> edges) {
    const auto tiles = static_cast<std::size_t>(f.side) * static_cast<std::size_t>(f.side);
    const auto fields = std::span(f.fields).first(tiles * field::kCount);
    const auto flags = std::span(f.flags).first(tiles);
    const auto scalars = std::span(f.scalars).first(tiles * scalar::kCount);
    return {.to_move = static_cast<std::uint8_t>(to_move),
            .fields = std::vector<std::uint8_t>(fields.begin(), fields.end()),
            .flags = std::vector<std::uint8_t>(flags.begin(), flags.end()),
            .scalars = std::vector<float>(scalars.begin(), scalars.end()),
            .global = std::vector<float>(f.global.begin(), f.global.end()),
            .edges = std::move(edges)};
}

void write_record(const std::filesystem::path& path, const GameRecord& g) {
    const std::size_t tiles = static_cast<std::size_t>(g.side) * g.side;
    Out o;
    for (const char c : kMagic) {
        o.put(c);
    }
    o.put(kRecordVersion);
    o.put(g.features_version);
    o.put(g.side);
    o.put(g.tribes);
    o.put(g.level_seed);
    o.put(g.game_seed);
    o.put(g.obs);
    o.put(g.mode);
    o.put(g.winner);
    o.put(g.draw);
    o.put(g.ticks);
    o.put(static_cast<std::uint32_t>(g.decisions.size()));
    o.put(static_cast<std::uint32_t>(g.agent.size()));
    o.put_bytes(g.agent);
    for (const Decision& d : g.decisions) {
        if (d.fields.size() != tiles * field::kCount || d.flags.size() != tiles ||
            d.scalars.size() != tiles * scalar::kCount ||
            d.global.size() != static_cast<std::size_t>(gfeat::kSize) || d.edges.size() > 0xFFFF) {
            throw std::runtime_error("record: decision does not match the header's side");
        }
        o.put(d.to_move);
        o.put(std::uint8_t{1});
        o.put(static_cast<std::uint16_t>(d.edges.size()));
        o.put_all(d.fields);
        o.put_all(d.flags);
        o.put_all(d.scalars);
        o.put_all(d.global);
        for (const auto& [index, visits] : d.edges) {
            o.put(index);
            o.put(visits);
        }
    }
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        f.write(o.bytes().data(), static_cast<std::streamsize>(o.bytes().size()));
        if (!f) {
            throw std::runtime_error("record: cannot write " + tmp.string());
        }
    }
    std::filesystem::rename(tmp, path);
}

GameRecord read_record(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error("record: cannot open " + path.string());
    }
    In in(std::vector<char>(std::istreambuf_iterator<char>(f), {}));
    for (const char c : kMagic) {
        if (in.get<char>() != c) {
            throw std::runtime_error("record: bad magic in " + path.string());
        }
    }
    if (in.get<std::uint32_t>() != kRecordVersion) {
        throw std::runtime_error("record: unknown format version in " + path.string());
    }
    GameRecord g;
    g.features_version = in.get<std::uint32_t>();
    g.side = in.get<std::uint32_t>();
    g.tribes = in.get<std::uint32_t>();
    g.level_seed = in.get<std::uint64_t>();
    g.game_seed = in.get<std::uint64_t>();
    g.obs = in.get<std::uint8_t>();
    g.mode = in.get<std::uint8_t>();
    g.winner = in.get<std::int8_t>();
    g.draw = in.get<std::uint8_t>();
    g.ticks = in.get<std::uint32_t>();
    const auto n = in.get<std::uint32_t>();
    g.agent = in.get_bytes(in.get<std::uint32_t>());
    const std::size_t tiles = static_cast<std::size_t>(g.side) * g.side;
    g.decisions.reserve(n);
    for (std::uint32_t i = 0; i < n; ++i) {
        Decision d;
        d.to_move = in.get<std::uint8_t>();
        (void)in.get<std::uint8_t>();  // searched
        const auto edges = in.get<std::uint16_t>();
        d.fields = in.get_all<std::uint8_t>(tiles * field::kCount);
        d.flags = in.get_all<std::uint8_t>(tiles);
        d.scalars = in.get_all<float>(tiles * scalar::kCount);
        d.global = in.get_all<float>(gfeat::kSize);
        for (std::uint16_t e = 0; e < edges; ++e) {
            const auto index = in.get<std::uint32_t>();
            d.edges.emplace_back(index, in.get<std::uint32_t>());
        }
        g.decisions.push_back(std::move(d));
    }
    if (!in.done()) {
        throw std::runtime_error("record: trailing bytes in " + path.string());
    }
    return g;
}

}  // namespace poly::selfplay
