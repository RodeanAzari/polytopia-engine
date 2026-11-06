#pragma once

// Game vocabulary and static tables. Source: rule-facts sheet docs/rules/01 and 05 (facts only,
// ADR-002). Enumerator order is part of the contract: it fixes the tech bit order and the "enum
// order" the rules use for uniform draws (01, 03b), and the names are the ones the oracle log
// prints.

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace poly {

enum class Terrain : std::uint8_t {
    Plain,
    ShallowWater,
    DeepWater,
    Mountain,
    Village,
    City,
    Forest
};
inline constexpr std::array<char, 7> kTerrainChar = {'.', 's', 'd', 'm', 'v', 'c', 'f'};

enum class Resource : std::uint8_t { Fish, Fruit, Animal, Whales, Ore, Crops, Ruins, None };
inline constexpr std::array<char, 7> kResourceChar = {'h', 'f', 'a', 'w', 'o', 'c', 'r'};

enum class Tech : std::uint8_t {
    Climbing,
    Fishing,
    Hunting,
    Organization,
    Riding,
    Archery,
    Farming,
    Forestry,
    FreeSpirit,
    Meditation,
    Mining,
    Roads,
    Sailing,
    Shields,
    Whaling,
    Aquatism,
    Chivalry,
    Construction,
    Mathematics,
    Navigation,
    Smithery,
    Spiritualism,
    Trade,
    Philosophy,
};
inline constexpr int kNumTechs = 24;
// Tier per tech (01 tech-tree table), in enum order.
inline constexpr std::array<int, kNumTechs> kTechTier = {1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2,
                                                         2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};

enum class UnitType : std::uint8_t {
    Warrior,
    Rider,
    Defender,
    Swordsman,
    Archer,
    Catapult,
    Knight,
    MindBender,
    Boat,
    Ship,
    Battleship,
    SuperUnit,
};
inline constexpr std::array<std::string_view, 12> kUnitName = {
    "WARRIOR", "RIDER",       "DEFENDER", "SWORDMAN", "ARCHER",     "CATAPULT",
    "KNIGHT",  "MIND_BENDER", "BOAT",     "SHIP",     "BATTLESHIP", "SUPERUNIT"};
// Max HP and score points (01 unit table). Naval units take the carried unit's HP (0 here).
inline constexpr std::array<int, 12> kUnitMaxHp = {10, 10, 15, 15, 10, 10, 15, 10, 0, 0, 0, 40};
inline constexpr std::array<int, 12> kUnitPoints = {10, 15, 15, 25, 15, 40, 40, 25, 0, 0, 0, 50};

enum class Building : std::uint8_t {
    Port,
    Mine,
    Forge,
    Farm,
    Windmill,
    CustomsHouse,
    LumberHut,
    Sawmill,
    Temple,
    WaterTemple,
    ForestTemple,
    MountainTemple,
    AltarOfPeace,
    EmperorsTomb,
    EyeOfGod,
    GateOfPower,
    GrandBazar,
    ParkOfFortune,
    TowerOfWisdom,
    None,
};
inline constexpr std::array<std::string_view, 19> kBuildingName = {
    "PORT",           "MINE",          "FORGE",
    "FARM",           "WINDMILL",      "CUSTOMS_HOUSE",
    "LUMBER_HUT",     "SAWMILL",       "TEMPLE",
    "WATER_TEMPLE",   "FOREST_TEMPLE", "MOUNTAIN_TEMPLE",
    "ALTAR_OF_PEACE", "EMPERORS_TOMB", "EYE_OF_GOD",
    "GATE_OF_POWER",  "GRAND_BAZAR",   "PARK_OF_FORTUNE",
    "TOWER_OF_WISDOM"};
inline constexpr int kNumMonuments = 7;
inline constexpr int kFirstMonument = static_cast<int>(Building::AltarOfPeace);

enum class MonumentStatus : std::uint8_t { Unavailable, Available, Built };
inline constexpr std::array<std::string_view, 3> kMonumentStatusName = {"UNAVAILABLE", "AVAILABLE",
                                                                        "BUILT"};

enum class TurnStatus : std::uint8_t { Fresh, Moved, Attacked, MovedAndAttacked, Pushed, Finished };
inline constexpr std::array<std::string_view, 6> kTurnStatusName = {
    "FRESH", "MOVED", "ATTACKED", "MOVED_AND_ATTACKED", "PUSHED", "FINISHED"};

enum class Result : std::uint8_t { Win, Loss, Incomplete };
inline constexpr std::array<std::string_view, 3> kResultName = {"WIN", "LOSS", "INCOMPLETE"};

// Tribe type; the enumerator value is the type key used in level files (05 §1.1).
enum class TribeType : std::uint8_t {
    XinXi,
    Imperius,
    Bardur,
    Oumaji,
    Kickoo,
    Hoodrick,
    Luxidoor,
    Vengir,
    Zebasi,
    AiMo,
    Quetzali,
    Yadakk,
};
inline constexpr int kNumTribeTypes = 12;
inline constexpr std::array<std::string_view, kNumTribeTypes> kTribeName = {
    "XIN_XI",   "IMPERIUS", "BARDUR", "OUMAJI", "KICKOO",   "HOODRICK",
    "LUXIDOOR", "VENGIR",   "ZEBASI", "AI_MO",  "QUETZALI", "YADAKK"};
// Starting tech and unit (01 tribes table).
inline constexpr std::array<std::optional<Tech>, kNumTribeTypes> kStartTech = {
    Tech::Climbing, Tech::Organization, Tech::Hunting, Tech::Riding,
    Tech::Fishing,  Tech::Archery,      std::nullopt,  Tech::Smithery,
    Tech::Farming,  Tech::Meditation,   Tech::Shields, Tech::Roads};
inline constexpr std::array<UnitType, kNumTribeTypes> kStartUnit = {
    UnitType::Warrior, UnitType::Warrior, UnitType::Warrior,  UnitType::Rider,
    UnitType::Warrior, UnitType::Archer,  UnitType::Warrior,  UnitType::Swordsman,
    UnitType::Warrior, UnitType::Warrior, UnitType::Defender, UnitType::Warrior};

enum class GameMode : std::uint8_t { Capitals, Score };
enum class ObsMode : std::uint8_t { Full, Partial };  // ADR-004

// Scoring constants (01).
inline constexpr int kCityCentrePoints = 100;
inline constexpr int kBorderTilePoints = 20;
inline constexpr int kRevealTilePoints = 5;
inline constexpr int kStartingStars = 5;

}  // namespace poly
