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
inline constexpr std::array<std::string_view, 7> kResourceName = {
    "FISH", "FRUIT", "ANIMAL", "WHALES", "ORE", "CROPS", "RUINS"};

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
// Parent per tech (01 tech-tree table); roots have none.
inline constexpr std::array<std::optional<Tech>, kNumTechs> kTechParent = {
    std::nullopt,   std::nullopt,       std::nullopt,  std::nullopt,       std::nullopt,
    Tech::Hunting,  Tech::Organization, Tech::Hunting, Tech::Riding,       Tech::Climbing,
    Tech::Climbing, Tech::Riding,       Tech::Fishing, Tech::Organization, Tech::Fishing,
    Tech::Whaling,  Tech::FreeSpirit,   Tech::Farming, Tech::Forestry,     Tech::Sailing,
    Tech::Mining,   Tech::Archery,      Tech::Roads,   Tech::Meditation};
inline constexpr std::array<std::string_view, kNumTechs> kTechName = {
    "CLIMBING",    "FISHING",    "HUNTING",     "ORGANIZATION", "RIDING",   "ARCHERY",
    "FARMING",     "FORESTRY",   "FREE_SPIRIT", "MEDITATION",   "MINING",   "ROADS",
    "SAILING",     "SHIELDS",    "WHALING",     "AQUATISM",     "CHIVALRY", "CONSTRUCTION",
    "MATHEMATICS", "NAVIGATION", "SMITHERY",    "SPIRITUALISM", "TRADE",    "PHILOSOPHY"};

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
inline constexpr std::array<int, 12> kUnitMove = {1, 2, 1, 1, 1, 1, 3, 1, 2, 3, 3, 1};
inline constexpr std::array<int, 12> kUnitAttack = {2, 2, 1, 3, 2, 4, 4, 0, 1, 2, 4, 5};
inline constexpr std::array<int, 12> kUnitDefence = {2, 1, 3, 3, 1, 0, 1, 1, 1, 2, 3, 4};
inline constexpr std::array<int, 12> kUnitRange = {1, 1, 1, 1, 2, 3, 1, 1, 2, 2, 2, 1};
// Combat constants (01).
inline constexpr double kAttackModifier = 4.5;
inline constexpr double kDefenceBonus = 1.5;
inline constexpr double kDefenceInWalls = 4.0;
inline constexpr int kAttackRepercussion = -5;
inline constexpr int kCaptureRepercussion = -30;
inline constexpr int kMaxTurnsScore = 30;     // 04 §8.1
inline constexpr int kMaxTurnsCapitals = 50;  // 04 §8.2
inline constexpr int kGateOfPowerKills = 10;
// Cost (spawn, upgrade, disband refund base) and required tech (01 unit table).
inline constexpr std::array<int, 12> kUnitCost = {2, 3, 3, 5, 3, 8, 8, 5, 0, 5, 15, 10};
inline constexpr std::array<std::optional<Tech>, 12> kUnitTech = {
    std::nullopt,  Tech::Riding,      Tech::Shields,    Tech::Smithery,
    Tech::Archery, Tech::Mathematics, Tech::Chivalry,   Tech::Philosophy,
    Tech::Sailing, Tech::Sailing,     Tech::Navigation, std::nullopt};

// City level-up rewards (01, 03a §2), in pairs by the city's level before levelling up.
enum class LevelUpBonus : std::uint8_t {
    Workshop,
    Explorer,
    CityWall,
    Resources,
    PopGrowth,
    BorderGrowth,
    Park,
    SuperUnit
};
inline constexpr std::array<std::string_view, 8> kLevelUpBonusName = {
    "WORKSHOP",   "EXPLORER",      "CITY_WALL", "RESOURCES",
    "POP_GROWTH", "BORDER_GROWTH", "PARK",      "SUPERUNIT"};

// Resource gathering (01 map-resources table): cost, population or star bonus.
inline constexpr int kGatherCost = 2;  // Fish, Fruit, Animal; Whales cost 0
inline constexpr int kWhaleStars = 10;
inline constexpr int kRoadCost = 2;
inline constexpr int kVeteranKills = 3;
inline constexpr int kVeteranPlusHp = 5;
inline constexpr int kForestClearStars = 2;
inline constexpr int kForestGrowCost = 5;
inline constexpr int kForestBurnCost = 5;
inline constexpr int kRecoverHp = 2;  // + the same again inside own borders
inline constexpr int kLevelUpResourceStars = 5;
inline constexpr int kLevelUpPopGrowth = 3;  // also the ruin population bonus
inline constexpr int kRuinStars = 10;
inline constexpr int kParkPoints = 250;
inline constexpr int kPopulationPoints = 5;
inline constexpr int kTombStars = 100;    // Emperor's Tomb unlock
inline constexpr int kPacifistTurns = 5;  // Altar of Peace unlock
inline constexpr int kSendStarsBound = 15;
inline constexpr int kSendStarsTurnCap = 30;
inline constexpr int kAllegianceMax = 60;
inline constexpr int kWarAllegiance = -30;  // declare war target value and threshold

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
// Building table (01): cost, bonus, tech gate, in enum order (monuments: cost 0, bonus 3).
inline constexpr std::array<int, 19> kBuildingCost = {10, 5,  5, 5, 5, 5, 2, 5, 20, 20,
                                                      15, 20, 0, 0, 0, 0, 0, 0, 0};
inline constexpr std::array<int, 19> kBuildingBonus = {2, 2, 2, 2, 1, 2, 1, 1, 1, 1,
                                                       1, 1, 3, 3, 3, 3, 3, 3, 3};
inline constexpr std::array<std::optional<Tech>, 19> kBuildingTech = {
    Tech::Sailing,      Tech::Mining,     Tech::Smithery,    Tech::Farming,    Tech::Construction,
    Tech::Trade,        Tech::Forestry,   Tech::Mathematics, Tech::FreeSpirit, Tech::Aquatism,
    Tech::Spiritualism, Tech::Meditation, Tech::Meditation,  Tech::Trade,      Tech::Navigation,
    std::nullopt,       Tech::Roads,      std::nullopt,      Tech::Philosophy};
inline constexpr int kMonumentPoints = 400;
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
