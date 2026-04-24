#include "GameState.h"
#include "Scripts.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace scc {
namespace {

std::string normalizeName(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        } else if (ch == '_' || ch == '-' || ch == ' ') {
            out.push_back('_');
        }
    }
    while (out.find("__") != std::string::npos) {
        out.erase(out.find("__"), 1);
    }
    if (!out.empty() && out.front() == '_') out.erase(out.begin());
    if (!out.empty() && out.back() == '_') out.pop_back();
    return out;
}

std::string trim(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    if (first >= last) return {};
    return std::string(first, last);
}

bool tryParsePlayerId(std::string_view rawToken, int& player) {
    std::string token(rawToken);
    std::string normalized = normalizeName(token);
    if (!normalized.empty() && normalized.front() == 'p') {
        normalized.erase(normalized.begin());
    } else if (normalized.rfind("player", 0) == 0) {
        normalized.erase(0, std::string_view("player").size());
        if (!normalized.empty() && normalized.front() == '_') {
            normalized.erase(normalized.begin());
        }
    }
    if (normalized.empty()) return false;

    char* end = nullptr;
    const long numeric = std::strtol(normalized.c_str(), &end, 10);
    if (!end || *end != '\0' || numeric < 0 || numeric >= 12) {
        return false;
    }

    player = static_cast<int>(numeric);
    return true;
}

std::string stripUnitTypesPrefix(std::string_view name) {
    constexpr std::string_view prefix = "UnitTypes::";
    if (name.substr(0, prefix.size()) == prefix) {
        return std::string(name.substr(prefix.size()));
    }
    return std::string(name);
}

const std::vector<std::pair<std::string_view, bwgame::UnitTypes>>& unitNameTable() {
    using bwgame::UnitTypes;
    static const std::vector<std::pair<std::string_view, UnitTypes>> table = {
        {"Terran_Marine", UnitTypes::Terran_Marine},
        {"Terran_Ghost", UnitTypes::Terran_Ghost},
        {"Terran_Vulture", UnitTypes::Terran_Vulture},
        {"Terran_Goliath", UnitTypes::Terran_Goliath},
        {"Terran_Goliath_Turret", UnitTypes::Terran_Goliath_Turret},
        {"Terran_Siege_Tank_Tank_Mode", UnitTypes::Terran_Siege_Tank_Tank_Mode},
        {"Terran_Siege_Tank_Tank_Mode_Turret", UnitTypes::Terran_Siege_Tank_Tank_Mode_Turret},
        {"Terran_SCV", UnitTypes::Terran_SCV},
        {"Terran_Wraith", UnitTypes::Terran_Wraith},
        {"Terran_Science_Vessel", UnitTypes::Terran_Science_Vessel},
        {"Hero_Gui_Montag", UnitTypes::Hero_Gui_Montag},
        {"Terran_Dropship", UnitTypes::Terran_Dropship},
        {"Terran_Battlecruiser", UnitTypes::Terran_Battlecruiser},
        {"Terran_Vulture_Spider_Mine", UnitTypes::Terran_Vulture_Spider_Mine},
        {"Terran_Nuclear_Missile", UnitTypes::Terran_Nuclear_Missile},
        {"Terran_Civilian", UnitTypes::Terran_Civilian},
        {"Hero_Sarah_Kerrigan", UnitTypes::Hero_Sarah_Kerrigan},
        {"Hero_Alan_Schezar", UnitTypes::Hero_Alan_Schezar},
        {"Hero_Alan_Schezar_Turret", UnitTypes::Hero_Alan_Schezar_Turret},
        {"Hero_Jim_Raynor_Vulture", UnitTypes::Hero_Jim_Raynor_Vulture},
        {"Hero_Jim_Raynor_Marine", UnitTypes::Hero_Jim_Raynor_Marine},
        {"Hero_Tom_Kazansky", UnitTypes::Hero_Tom_Kazansky},
        {"Hero_Magellan", UnitTypes::Hero_Magellan},
        {"Hero_Edmund_Duke_Tank_Mode", UnitTypes::Hero_Edmund_Duke_Tank_Mode},
        {"Hero_Edmund_Duke_Tank_Mode_Turret", UnitTypes::Hero_Edmund_Duke_Tank_Mode_Turret},
        {"Hero_Edmund_Duke_Siege_Mode", UnitTypes::Hero_Edmund_Duke_Siege_Mode},
        {"Hero_Edmund_Duke_Siege_Mode_Turret", UnitTypes::Hero_Edmund_Duke_Siege_Mode_Turret},
        {"Hero_Arcturus_Mengsk", UnitTypes::Hero_Arcturus_Mengsk},
        {"Hero_Hyperion", UnitTypes::Hero_Hyperion},
        {"Hero_Norad_II", UnitTypes::Hero_Norad_II},
        {"Terran_Siege_Tank_Siege_Mode", UnitTypes::Terran_Siege_Tank_Siege_Mode},
        {"Terran_Siege_Tank_Siege_Mode_Turret", UnitTypes::Terran_Siege_Tank_Siege_Mode_Turret},
        {"Terran_Firebat", UnitTypes::Terran_Firebat},
        {"Spell_Scanner_Sweep", UnitTypes::Spell_Scanner_Sweep},
        {"Terran_Medic", UnitTypes::Terran_Medic},
        {"Zerg_Larva", UnitTypes::Zerg_Larva},
        {"Zerg_Egg", UnitTypes::Zerg_Egg},
        {"Zerg_Zergling", UnitTypes::Zerg_Zergling},
        {"Zerg_Hydralisk", UnitTypes::Zerg_Hydralisk},
        {"Zerg_Ultralisk", UnitTypes::Zerg_Ultralisk},
        {"Zerg_Broodling", UnitTypes::Zerg_Broodling},
        {"Zerg_Drone", UnitTypes::Zerg_Drone},
        {"Zerg_Overlord", UnitTypes::Zerg_Overlord},
        {"Zerg_Mutalisk", UnitTypes::Zerg_Mutalisk},
        {"Zerg_Guardian", UnitTypes::Zerg_Guardian},
        {"Zerg_Queen", UnitTypes::Zerg_Queen},
        {"Zerg_Defiler", UnitTypes::Zerg_Defiler},
        {"Zerg_Scourge", UnitTypes::Zerg_Scourge},
        {"Hero_Torrasque", UnitTypes::Hero_Torrasque},
        {"Hero_Matriarch", UnitTypes::Hero_Matriarch},
        {"Zerg_Infested_Terran", UnitTypes::Zerg_Infested_Terran},
        {"Hero_Infested_Kerrigan", UnitTypes::Hero_Infested_Kerrigan},
        {"Hero_Unclean_One", UnitTypes::Hero_Unclean_One},
        {"Hero_Hunter_Killer", UnitTypes::Hero_Hunter_Killer},
        {"Hero_Devouring_One", UnitTypes::Hero_Devouring_One},
        {"Hero_Kukulza_Mutalisk", UnitTypes::Hero_Kukulza_Mutalisk},
        {"Hero_Kukulza_Guardian", UnitTypes::Hero_Kukulza_Guardian},
        {"Hero_Yggdrasill", UnitTypes::Hero_Yggdrasill},
        {"Terran_Valkyrie", UnitTypes::Terran_Valkyrie},
        {"Zerg_Cocoon", UnitTypes::Zerg_Cocoon},
        {"Protoss_Corsair", UnitTypes::Protoss_Corsair},
        {"Protoss_Dark_Templar", UnitTypes::Protoss_Dark_Templar},
        {"Zerg_Devourer", UnitTypes::Zerg_Devourer},
        {"Protoss_Dark_Archon", UnitTypes::Protoss_Dark_Archon},
        {"Protoss_Probe", UnitTypes::Protoss_Probe},
        {"Protoss_Zealot", UnitTypes::Protoss_Zealot},
        {"Protoss_Dragoon", UnitTypes::Protoss_Dragoon},
        {"Protoss_High_Templar", UnitTypes::Protoss_High_Templar},
        {"Protoss_Archon", UnitTypes::Protoss_Archon},
        {"Protoss_Shuttle", UnitTypes::Protoss_Shuttle},
        {"Protoss_Scout", UnitTypes::Protoss_Scout},
        {"Protoss_Arbiter", UnitTypes::Protoss_Arbiter},
        {"Protoss_Carrier", UnitTypes::Protoss_Carrier},
        {"Protoss_Interceptor", UnitTypes::Protoss_Interceptor},
        {"Hero_Dark_Templar", UnitTypes::Hero_Dark_Templar},
        {"Hero_Zeratul", UnitTypes::Hero_Zeratul},
        {"Hero_Tassadar_Zeratul_Archon", UnitTypes::Hero_Tassadar_Zeratul_Archon},
        {"Hero_Fenix_Zealot", UnitTypes::Hero_Fenix_Zealot},
        {"Hero_Fenix_Dragoon", UnitTypes::Hero_Fenix_Dragoon},
        {"Hero_Tassadar", UnitTypes::Hero_Tassadar},
        {"Hero_Mojo", UnitTypes::Hero_Mojo},
        {"Hero_Warbringer", UnitTypes::Hero_Warbringer},
        {"Hero_Gantrithor", UnitTypes::Hero_Gantrithor},
        {"Protoss_Reaver", UnitTypes::Protoss_Reaver},
        {"Protoss_Observer", UnitTypes::Protoss_Observer},
        {"Protoss_Scarab", UnitTypes::Protoss_Scarab},
        {"Hero_Danimoth", UnitTypes::Hero_Danimoth},
        {"Hero_Aldaris", UnitTypes::Hero_Aldaris},
        {"Hero_Artanis", UnitTypes::Hero_Artanis},
        {"Critter_Rhynadon", UnitTypes::Critter_Rhynadon},
        {"Critter_Bengalaas", UnitTypes::Critter_Bengalaas},
        {"Special_Cargo_Ship", UnitTypes::Special_Cargo_Ship},
        {"Special_Mercenary_Gunship", UnitTypes::Special_Mercenary_Gunship},
        {"Critter_Scantid", UnitTypes::Critter_Scantid},
        {"Critter_Kakaru", UnitTypes::Critter_Kakaru},
        {"Critter_Ragnasaur", UnitTypes::Critter_Ragnasaur},
        {"Critter_Ursadon", UnitTypes::Critter_Ursadon},
        {"Zerg_Lurker_Egg", UnitTypes::Zerg_Lurker_Egg},
        {"Hero_Raszagal", UnitTypes::Hero_Raszagal},
        {"Hero_Samir_Duran", UnitTypes::Hero_Samir_Duran},
        {"Hero_Alexei_Stukov", UnitTypes::Hero_Alexei_Stukov},
        {"Special_Map_Revealer", UnitTypes::Special_Map_Revealer},
        {"Hero_Gerard_DuGalle", UnitTypes::Hero_Gerard_DuGalle},
        {"Zerg_Lurker", UnitTypes::Zerg_Lurker},
        {"Hero_Infested_Duran", UnitTypes::Hero_Infested_Duran},
        {"Spell_Disruption_Web", UnitTypes::Spell_Disruption_Web},
        {"Terran_Command_Center", UnitTypes::Terran_Command_Center},
        {"Terran_Comsat_Station", UnitTypes::Terran_Comsat_Station},
        {"Terran_Nuclear_Silo", UnitTypes::Terran_Nuclear_Silo},
        {"Terran_Supply_Depot", UnitTypes::Terran_Supply_Depot},
        {"Terran_Refinery", UnitTypes::Terran_Refinery},
        {"Terran_Barracks", UnitTypes::Terran_Barracks},
        {"Terran_Academy", UnitTypes::Terran_Academy},
        {"Terran_Factory", UnitTypes::Terran_Factory},
        {"Terran_Starport", UnitTypes::Terran_Starport},
        {"Terran_Control_Tower", UnitTypes::Terran_Control_Tower},
        {"Terran_Science_Facility", UnitTypes::Terran_Science_Facility},
        {"Terran_Covert_Ops", UnitTypes::Terran_Covert_Ops},
        {"Terran_Physics_Lab", UnitTypes::Terran_Physics_Lab},
        {"Unused_Terran1", UnitTypes::Unused_Terran1},
        {"Terran_Machine_Shop", UnitTypes::Terran_Machine_Shop},
        {"Unused_Terran2", UnitTypes::Unused_Terran2},
        {"Terran_Engineering_Bay", UnitTypes::Terran_Engineering_Bay},
        {"Terran_Armory", UnitTypes::Terran_Armory},
        {"Terran_Missile_Turret", UnitTypes::Terran_Missile_Turret},
        {"Terran_Bunker", UnitTypes::Terran_Bunker},
        {"Special_Crashed_Norad_II", UnitTypes::Special_Crashed_Norad_II},
        {"Special_Ion_Cannon", UnitTypes::Special_Ion_Cannon},
        {"Powerup_Uraj_Crystal", UnitTypes::Powerup_Uraj_Crystal},
        {"Powerup_Khalis_Crystal", UnitTypes::Powerup_Khalis_Crystal},
        {"Zerg_Infested_Command_Center", UnitTypes::Zerg_Infested_Command_Center},
        {"Zerg_Hatchery", UnitTypes::Zerg_Hatchery},
        {"Zerg_Lair", UnitTypes::Zerg_Lair},
        {"Zerg_Hive", UnitTypes::Zerg_Hive},
        {"Zerg_Nydus_Canal", UnitTypes::Zerg_Nydus_Canal},
        {"Zerg_Hydralisk_Den", UnitTypes::Zerg_Hydralisk_Den},
        {"Zerg_Defiler_Mound", UnitTypes::Zerg_Defiler_Mound},
        {"Zerg_Greater_Spire", UnitTypes::Zerg_Greater_Spire},
        {"Zerg_Queens_Nest", UnitTypes::Zerg_Queens_Nest},
        {"Zerg_Evolution_Chamber", UnitTypes::Zerg_Evolution_Chamber},
        {"Zerg_Ultralisk_Cavern", UnitTypes::Zerg_Ultralisk_Cavern},
        {"Zerg_Spire", UnitTypes::Zerg_Spire},
        {"Zerg_Spawning_Pool", UnitTypes::Zerg_Spawning_Pool},
        {"Zerg_Creep_Colony", UnitTypes::Zerg_Creep_Colony},
        {"Zerg_Spore_Colony", UnitTypes::Zerg_Spore_Colony},
        {"Unused_Zerg1", UnitTypes::Unused_Zerg1},
        {"Zerg_Sunken_Colony", UnitTypes::Zerg_Sunken_Colony},
        {"Special_Overmind_With_Shell", UnitTypes::Special_Overmind_With_Shell},
        {"Special_Overmind", UnitTypes::Special_Overmind},
        {"Zerg_Extractor", UnitTypes::Zerg_Extractor},
        {"Special_Mature_Chrysalis", UnitTypes::Special_Mature_Chrysalis},
        {"Special_Cerebrate", UnitTypes::Special_Cerebrate},
        {"Special_Cerebrate_Daggoth", UnitTypes::Special_Cerebrate_Daggoth},
        {"Unused_Zerg2", UnitTypes::Unused_Zerg2},
        {"Protoss_Nexus", UnitTypes::Protoss_Nexus},
        {"Protoss_Robotics_Facility", UnitTypes::Protoss_Robotics_Facility},
        {"Protoss_Pylon", UnitTypes::Protoss_Pylon},
        {"Protoss_Assimilator", UnitTypes::Protoss_Assimilator},
        {"Unused_Protoss1", UnitTypes::Unused_Protoss1},
        {"Protoss_Observatory", UnitTypes::Protoss_Observatory},
        {"Protoss_Gateway", UnitTypes::Protoss_Gateway},
        {"Unused_Protoss2", UnitTypes::Unused_Protoss2},
        {"Protoss_Photon_Cannon", UnitTypes::Protoss_Photon_Cannon},
        {"Protoss_Citadel_of_Adun", UnitTypes::Protoss_Citadel_of_Adun},
        {"Protoss_Cybernetics_Core", UnitTypes::Protoss_Cybernetics_Core},
        {"Protoss_Templar_Archives", UnitTypes::Protoss_Templar_Archives},
        {"Protoss_Forge", UnitTypes::Protoss_Forge},
        {"Protoss_Stargate", UnitTypes::Protoss_Stargate},
        {"Special_Stasis_Cell_Prison", UnitTypes::Special_Stasis_Cell_Prison},
        {"Protoss_Fleet_Beacon", UnitTypes::Protoss_Fleet_Beacon},
        {"Protoss_Arbiter_Tribunal", UnitTypes::Protoss_Arbiter_Tribunal},
        {"Protoss_Robotics_Support_Bay", UnitTypes::Protoss_Robotics_Support_Bay},
        {"Protoss_Shield_Battery", UnitTypes::Protoss_Shield_Battery},
        {"Special_Khaydarin_Crystal_Form", UnitTypes::Special_Khaydarin_Crystal_Form},
        {"Special_Protoss_Temple", UnitTypes::Special_Protoss_Temple},
        {"Special_XelNaga_Temple", UnitTypes::Special_XelNaga_Temple},
        {"Resource_Mineral_Field", UnitTypes::Resource_Mineral_Field},
        {"Resource_Mineral_Field_Type_2", UnitTypes::Resource_Mineral_Field_Type_2},
        {"Resource_Mineral_Field_Type_3", UnitTypes::Resource_Mineral_Field_Type_3},
        {"Unused_Cave", UnitTypes::Unused_Cave},
        {"Unused_Cave_In", UnitTypes::Unused_Cave_In},
        {"Unused_Cantina", UnitTypes::Unused_Cantina},
        {"Unused_Mining_Platform", UnitTypes::Unused_Mining_Platform},
        {"Unused_Independant_Command_Center", UnitTypes::Unused_Independant_Command_Center},
        {"Special_Independant_Starport", UnitTypes::Special_Independant_Starport},
        {"Unused_Independant_Jump_Gate", UnitTypes::Unused_Independant_Jump_Gate},
        {"Unused_Ruins", UnitTypes::Unused_Ruins},
        {"Unused_Khaydarin_Crystal_Formation", UnitTypes::Unused_Khaydarin_Crystal_Formation},
        {"Resource_Vespene_Geyser", UnitTypes::Resource_Vespene_Geyser},
        {"Special_Warp_Gate", UnitTypes::Special_Warp_Gate},
        {"Special_Psi_Disrupter", UnitTypes::Special_Psi_Disrupter},
        {"Unused_Zerg_Marker", UnitTypes::Unused_Zerg_Marker},
        {"Unused_Terran_Marker", UnitTypes::Unused_Terran_Marker},
        {"Unused_Protoss_Marker", UnitTypes::Unused_Protoss_Marker},
        {"Special_Zerg_Beacon", UnitTypes::Special_Zerg_Beacon},
        {"Special_Terran_Beacon", UnitTypes::Special_Terran_Beacon},
        {"Special_Protoss_Beacon", UnitTypes::Special_Protoss_Beacon},
        {"Special_Zerg_Flag_Beacon", UnitTypes::Special_Zerg_Flag_Beacon},
        {"Special_Terran_Flag_Beacon", UnitTypes::Special_Terran_Flag_Beacon},
        {"Special_Protoss_Flag_Beacon", UnitTypes::Special_Protoss_Flag_Beacon},
        {"Special_Power_Generator", UnitTypes::Special_Power_Generator},
        {"Special_Overmind_Cocoon", UnitTypes::Special_Overmind_Cocoon},
        {"Spell_Dark_Swarm", UnitTypes::Spell_Dark_Swarm},
        {"Special_Floor_Missile_Trap", UnitTypes::Special_Floor_Missile_Trap},
        {"Special_Floor_Hatch", UnitTypes::Special_Floor_Hatch},
        {"Special_Upper_Level_Door", UnitTypes::Special_Upper_Level_Door},
        {"Special_Right_Upper_Level_Door", UnitTypes::Special_Right_Upper_Level_Door},
        {"Special_Pit_Door", UnitTypes::Special_Pit_Door},
        {"Special_Right_Pit_Door", UnitTypes::Special_Right_Pit_Door},
        {"Special_Floor_Gun_Trap", UnitTypes::Special_Floor_Gun_Trap},
        {"Special_Wall_Missile_Trap", UnitTypes::Special_Wall_Missile_Trap},
        {"Special_Wall_Flame_Trap", UnitTypes::Special_Wall_Flame_Trap},
        {"Special_Right_Wall_Missile_Trap", UnitTypes::Special_Right_Wall_Missile_Trap},
        {"Special_Right_Wall_Flame_Trap", UnitTypes::Special_Right_Wall_Flame_Trap},
        {"Special_Start_Location", UnitTypes::Special_Start_Location},
        {"Powerup_Flag", UnitTypes::Powerup_Flag},
        {"Powerup_Young_Chrysalis", UnitTypes::Powerup_Young_Chrysalis},
        {"Powerup_Psi_Emitter", UnitTypes::Powerup_Psi_Emitter},
        {"Powerup_Data_Disk", UnitTypes::Powerup_Data_Disk},
        {"Powerup_Khaydarin_Crystal", UnitTypes::Powerup_Khaydarin_Crystal},
        {"Powerup_Mineral_Cluster_Type_1", UnitTypes::Powerup_Mineral_Cluster_Type_1},
        {"Powerup_Mineral_Cluster_Type_2", UnitTypes::Powerup_Mineral_Cluster_Type_2},
        {"Powerup_Protoss_Gas_Orb_Type_1", UnitTypes::Powerup_Protoss_Gas_Orb_Type_1},
        {"Powerup_Protoss_Gas_Orb_Type_2", UnitTypes::Powerup_Protoss_Gas_Orb_Type_2},
        {"Powerup_Zerg_Gas_Sac_Type_1", UnitTypes::Powerup_Zerg_Gas_Sac_Type_1},
        {"Powerup_Zerg_Gas_Sac_Type_2", UnitTypes::Powerup_Zerg_Gas_Sac_Type_2},
        {"Powerup_Terran_Gas_Tank_Type_1", UnitTypes::Powerup_Terran_Gas_Tank_Type_1},
        {"Powerup_Terran_Gas_Tank_Type_2", UnitTypes::Powerup_Terran_Gas_Tank_Type_2},
    };
    return table;
}

void addMpqIfPresent(bwgame::data_loading::data_files_loader<>& loader, const std::filesystem::path& dir, std::string_view filename) {
    const auto path = dir / std::string(filename);
    if (std::filesystem::exists(path)) {
        loader.add_mpq_file(path.generic_string());
    }
}

std::filesystem::path mpqDirectory(const std::filesystem::path& dataPath) {
    return dataPath / "mpq";
}

std::filesystem::path mapsDirectory(const std::filesystem::path& dataPath) {
    return dataPath / "maps";
}

bwgame::xy formationOffset(int index, int spacing) {
    const int columns = 2;
    const int row = index / columns;
    const int col = index % columns;
    return {col * spacing, row * spacing};
}

std::filesystem::path defaultMapPath(const std::filesystem::path& dataPath) {
    const auto mapsPath = mapsDirectory(dataPath) / "(4)Python.scx";
    if (std::filesystem::exists(mapsPath)) return mapsPath;
    return dataPath / "(4)Python.scx";
}

std::filesystem::path resolveMapPath(
    const std::filesystem::path& scenarioPath,
    const std::filesystem::path& dataPath,
    const std::string& value) {
    std::filesystem::path candidate(value);
    if (candidate.is_absolute()) return candidate;

    const auto scenarioRelative = scenarioPath.parent_path() / candidate;
    if (std::filesystem::exists(scenarioRelative)) return scenarioRelative;

    const auto dataRelative = dataPath / candidate;
    if (std::filesystem::exists(dataRelative)) return dataRelative;

    const auto mapsRelative = mapsDirectory(dataPath) / candidate;
    if (std::filesystem::exists(mapsRelative)) return mapsRelative;

    return mapsRelative;
}

struct ScenarioUnit {
    bwgame::UnitTypes type = bwgame::UnitTypes::None;
    std::string typeName;
    int player = 0;
    int x = 0;
    int y = 0;
    int count = 1;
    int spacing = 16;
};

} // namespace

GameState::GameState(std::filesystem::path dataPath, std::filesystem::path mapPath)
    : dataPath_(std::move(dataPath))
    , mapPath_(std::move(mapPath)) {
    setAllPlayerScripts("AttackClosest");
    if (mapPath_.empty()) {
        mapPath_ = defaultMapPath(dataPath_);
    }
    initializeOpenBW();
}

void GameState::initializeDataLoader() {
    dataLoader_.mpqs.clear();
    const std::array<std::filesystem::path, 2> mpqDirectories = {
        mpqDirectory(dataPath_),
        dataPath_,
    };
    for (const auto& mpqDirectory : mpqDirectories) {
        addMpqIfPresent(dataLoader_, mpqDirectory, "Patch_rt.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "patch_rt.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "PATCH_RT.MPQ");
        addMpqIfPresent(dataLoader_, mpqDirectory, "BROODAT.MPQ");
        addMpqIfPresent(dataLoader_, mpqDirectory, "BrooDat.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "broodat.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "STARDAT.MPQ");
        addMpqIfPresent(dataLoader_, mpqDirectory, "StarDat.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "stardat.mpq");
        addMpqIfPresent(dataLoader_, mpqDirectory, "StarCraft.mpq");
    }
}

void GameState::initializeOpenBW() {
    initializeDataLoader();

    global_ = std::make_unique<bwgame::global_state>();
    game_ = std::make_unique<bwgame::game_state>();
    state_ = std::make_unique<bwgame::state>();
    state_->global = global_.get();
    state_->game = game_.get();

    auto loadFile = [this](bwgame::a_vector<unsigned char>& dst, bwgame::a_string filename) {
        dataLoader_(dst, std::move(filename));
    };
    try {
        bwgame::global_init(*global_, loadFile);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("OpenBW global_init failed: ") + e.what());
    }

    loadFunctions_ = std::make_unique<bwgame::game_load_functions>(*state_);
    frameFunctions_ = std::make_unique<bwgame::state_functions>(*state_);

    try {
        reset();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("initial GameState reset failed: ") + e.what());
    }
}

void GameState::reset() {
    try {
        loadMap();
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("OpenBW map reset failed: ") + e.what());
    }
}

void GameState::loadMap() {
    if (mapPath_.empty()) {
        mapPath_ = defaultMapPath(dataPath_);
    }
    if (!std::filesystem::exists(mapPath_)) {
        throw std::runtime_error("Map file not found: " + mapPath_.string());
    }

    try {
        loadFunctions_->load_map_file(mapPath_.generic_string(), [this]() {
            configurePlayers();
            loadFunctions_->setup_info = {};
            loadFunctions_->setup_info.create_no_units = true;
            loadFunctions_->setup_info.starting_units = 1;
            loadFunctions_->setup_info.resource_type = 0;
            loadFunctions_->setup_info.starting_minerals = 0;
        }, false);
    } catch (const std::exception& e) {
        throw std::runtime_error("failed to load map " + mapPath_.string() + ": " + e.what());
    }
}

void GameState::configurePlayers() {
    for (std::size_t i = 0; i < state_->players.size(); ++i) {
        state_->players[i].controller = bwgame::player_t::controller_inactive;
        state_->players[i].race = bwgame::race_t::terran;
        state_->players[i].color = static_cast<int>(i);
        state_->players[i].initially_active = false;
        state_->players[i].victory_state = 0;
        state_->shared_vision[i] = 1u << i;
    }

    state_->players[0].controller = bwgame::player_t::controller_occupied;
    state_->players[1].controller = bwgame::player_t::controller_occupied;
    state_->players[0].initially_active = true;
    state_->players[1].initially_active = true;
    state_->players[11].controller = bwgame::player_t::controller_neutral;

    state_->alliances = {};
    for (std::size_t i = 0; i < state_->alliances.size(); ++i) {
        state_->alliances[i][i] = 1;
        state_->alliances[i][11] = 1;
        state_->alliances[11][i] = 1;
    }
}

void GameState::loadScenario(const std::filesystem::path& scenarioPath) {
    std::ifstream input(scenarioPath);
    if (!input) {
        throw std::runtime_error("Unable to open scenario file: " + scenarioPath.string());
    }

    std::vector<ScenarioUnit> scenarioUnits;
    std::filesystem::path scenarioMap;
    std::array<std::string, 12> scenarioScripts;
    scenarioScripts.fill("AttackClosest");
    bool hasScenarioMap = false;

    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const auto comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        std::istringstream stream(line);
        std::string keyword;
        stream >> keyword;
        if (keyword.empty()) continue;

        const std::string normalizedKeyword = normalizeName(keyword);
        if (normalizedKeyword == "map") {
            std::string mapValue;
            std::getline(stream, mapValue);
            mapValue = trim(std::move(mapValue));
            if (mapValue.empty()) {
                throw std::runtime_error("Scenario line " + std::to_string(lineNumber) + ": expected map <file>");
            }
            scenarioMap = resolveMapPath(scenarioPath, dataPath_, mapValue);
            hasScenarioMap = true;
            continue;
        }
        if (normalizedKeyword == "script") {
            std::string scriptValue;
            std::getline(stream, scriptValue);
            scriptValue = trim(std::move(scriptValue));
            if (scriptValue.empty()) {
                throw std::runtime_error("Scenario line " + std::to_string(lineNumber) + ": expected script <name> or script <player> <name>");
            }

            std::istringstream scriptStream(scriptValue);
            std::vector<std::string> tokens;
            std::string token;
            while (scriptStream >> token) {
                tokens.push_back(std::move(token));
            }

            if (tokens.size() == 1) {
                scenarioScripts.fill(Scripts::canonicalName(tokens[0]));
                continue;
            }

            int player = -1;
            std::string scriptToken;
            if (tokens.size() == 2 && tryParsePlayerId(tokens[0], player)) {
                scriptToken = tokens[1];
            } else if (tokens.size() == 3 && normalizeName(tokens[0]) == "player" && tryParsePlayerId(tokens[1], player)) {
                scriptToken = tokens[2];
            } else {
                throw std::runtime_error("Scenario line " + std::to_string(lineNumber) + ": expected script <name> or script <player> <name>");
            }

            scenarioScripts[static_cast<std::size_t>(player)] = Scripts::canonicalName(scriptToken);
            continue;
        }
        if (normalizedKeyword != "unit") {
            throw std::runtime_error("Scenario line " + std::to_string(lineNumber) + ": expected 'map', 'script', or 'unit'");
        }

        ScenarioUnit unit;
        if (!(stream >> unit.typeName >> unit.player >> unit.x >> unit.y)) {
            throw std::runtime_error("Scenario line " + std::to_string(lineNumber) + ": expected unit <type> <player> <x> <y>");
        }
        stream >> unit.count;
        stream >> unit.spacing;
        if (unit.count < 1) unit.count = 1;
        if (unit.spacing < 1) unit.spacing = 1;
        unit.type = parseUnitType(unit.typeName);
        scenarioUnits.push_back(std::move(unit));
    }

    const std::filesystem::path nextMapPath = hasScenarioMap ? scenarioMap : (mapPath_.empty() ? defaultMapPath(dataPath_) : mapPath_);
    GameState nextState(dataPath_, nextMapPath);
    nextState.playerScripts_ = scenarioScripts;
    nextState.refreshScriptSummary();

    for (const ScenarioUnit& unit : scenarioUnits) {
        for (int i = 0; i < unit.count; ++i) {
            try {
                nextState.addUnit(unit.type, unit.player, bwgame::xy{unit.x, unit.y} + formationOffset(i, unit.spacing));
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "Failed to add " + unit.typeName + " at " + std::to_string(unit.x) + "," + std::to_string(unit.y) + ": " + e.what());
            }
        }
    }

    try {
        nextState.runScripts();
    } catch (const std::exception& e) {
        throw std::runtime_error("initial script " + nextState.scriptSummary_ + " failed: " + e.what());
    }

    *this = std::move(nextState);
}

bwgame::unit_t* GameState::addUnit(bwgame::UnitTypes type, int player, bwgame::xy position) {
    if (player < 0 || player >= 12) {
        throw std::runtime_error("Player id out of range");
    }
    const int maxX = std::max(64, static_cast<int>(game_->map_width) - 64);
    const int maxY = std::max(64, static_cast<int>(game_->map_height) - 96);
    position.x = std::clamp(position.x, 64, maxX);
    position.y = std::clamp(position.y, 64, maxY);

    const bwgame::unit_type_t* unitType = loadFunctions_->get_unit_type(type);
    bwgame::unit_t* unit = nullptr;
    try {
        unit = loadFunctions_->create_initial_unit(unitType, position, player);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("OpenBW create_initial_unit failed: ") + e.what());
    }
    if (!unit) {
        throw std::runtime_error("OpenBW failed to create unit " + unitTypeName(type));
    }
    fillCarrierInterceptors(unit);
    return unit;
}

void GameState::fillCarrierInterceptors(bwgame::unit_t* carrier) {
    if (!carrier || !frameFunctions_->unit_is_carrier(carrier)) return;
    if (carrier->owner < 0 || carrier->owner >= 12) return;

    state_->upgrade_levels[carrier->owner][bwgame::UpgradeTypes::Carrier_Capacity] = 1;

    const std::size_t targetCount = frameFunctions_->unit_max_interceptor_count(carrier);
    const bwgame::unit_type_t* interceptorType = frameFunctions_->get_unit_type(bwgame::UnitTypes::Protoss_Interceptor);
    while (frameFunctions_->unit_interceptor_count(carrier) < targetCount) {
        bwgame::unit_t* interceptor = frameFunctions_->create_unit(interceptorType, carrier->sprite->position, carrier->owner);
        if (!interceptor) {
            throw std::runtime_error("OpenBW failed to create default carrier interceptor");
        }

        interceptor->fighter.parent = carrier;
        interceptor->fighter.is_outside = false;
        frameFunctions_->finish_building_unit(interceptor);
        frameFunctions_->complete_unit(interceptor);
        frameFunctions_->hide_unit(interceptor, false);
        carrier->carrier.inside_units.push_front(*interceptor);
        ++carrier->carrier.inside_count;
    }
}

void GameState::setAllPlayerScripts(std::string_view scriptName) {
    playerScripts_.fill(Scripts::canonicalName(scriptName));
    refreshScriptSummary();
}

void GameState::setPlayerScript(int player, std::string_view scriptName) {
    if (player < 0 || player >= static_cast<int>(playerScripts_.size())) {
        throw std::runtime_error("Script player id out of range");
    }
    playerScripts_[static_cast<std::size_t>(player)] = Scripts::canonicalName(scriptName);
    refreshScriptSummary();
}

void GameState::refreshScriptSummary() {
    const bool allSame = std::all_of(playerScripts_.begin(), playerScripts_.end(), [this](const std::string& scriptName) {
        return scriptName == playerScripts_.front();
    });
    if (allSame) {
        scriptSummary_ = playerScripts_.front();
        return;
    }

    std::ostringstream output;
    output << "P0=" << playerScripts_[0] << ", P1=" << playerScripts_[1];
    for (std::size_t player = 2; player < playerScripts_.size(); ++player) {
        if (playerScripts_[player] == playerScripts_[0] || playerScripts_[player] == playerScripts_[1]) continue;
        output << ", P" << player << '=' << playerScripts_[player];
    }
    scriptSummary_ = output.str();
}

void GameState::runScripts() {
    for (std::size_t player = 0; player < playerScripts_.size(); ++player) {
        Scripts::run(playerScripts_[player], *this, static_cast<int>(player));
    }
}

void GameState::update() {
    runScripts();
    frameFunctions_->next_frame();
}

void GameState::update(int frames) {
    for (int i = 0; i < frames; ++i) {
        update();
    }
}

void GameState::issueAttackClosest() {
    Scripts::attackClosest(*this);
}

const std::string& GameState::scriptName() const {
    return scriptSummary_;
}

bool GameState::isTerminal() const {
    bool player0Alive = false;
    bool player1Alive = false;
    for (const bwgame::unit_t* unit : bwgame::ptr(state_->visible_units)) {
        if (!isAlive(unit)) continue;
        if (unit->owner == 0) player0Alive = true;
        if (unit->owner == 1) player1Alive = true;
    }
    return !player0Alive || !player1Alive;
}

int GameState::frame() const {
    return state_->current_frame;
}

std::vector<UnitSnapshot> GameState::units() const {
    std::vector<UnitSnapshot> result;
    for (const bwgame::unit_t* unit : bwgame::ptr(state_->visible_units)) {
        UnitSnapshot snapshot;
        snapshot.id = static_cast<int>(unit->index);
        snapshot.player = unit->owner;
        snapshot.type = unit->unit_type->id;
        snapshot.typeName = unitTypeName(unit->unit_type->id);
        snapshot.x = unit->sprite->position.x;
        snapshot.y = unit->sprite->position.y;
        snapshot.hitPoints = std::max(0, unit->hp.integer_part());
        snapshot.maxHitPoints = unit->unit_type->hitpoints.integer_part();
        snapshot.shields = std::max(0, unit->shield_points.integer_part());
        snapshot.maxShields = unit->unit_type->shield_points;
        snapshot.alive = isAlive(unit);
        result.push_back(std::move(snapshot));
    }
    return result;
}

std::vector<bwgame::unit_t*> GameState::liveUnits() const {
    std::vector<bwgame::unit_t*> result;
    for (bwgame::unit_t* unit : bwgame::ptr(state_->visible_units)) {
        if (!isAlive(unit)) continue;
        result.push_back(unit);
    }
    return result;
}

bool GameState::isAlive(const bwgame::unit_t* unit) const {
    return unit && unit->sprite && !frameFunctions_->unit_dead(unit) && unit->hp.raw_value > 0;
}

const bwgame::global_state& GameState::globalState() const {
    return *global_;
}

const bwgame::game_state& GameState::gameState() const {
    return *game_;
}

const bwgame::state& GameState::rawState() const {
    return *state_;
}

bwgame::state& GameState::rawState() {
    return *state_;
}

bwgame::state_functions& GameState::functions() {
    return *frameFunctions_;
}

const bwgame::state_functions& GameState::functions() const {
    return *frameFunctions_;
}

const std::filesystem::path& GameState::mapPath() const {
    return mapPath_;
}

std::vector<unsigned char> GameState::loadDataFile(std::string filename) const {
    bwgame::a_vector<unsigned char> data;
    dataLoader_(data, std::move(filename));
    return {data.begin(), data.end()};
}

bwgame::UnitTypes GameState::parseUnitType(std::string_view rawName) {
    const std::string name = stripUnitTypesPrefix(rawName);
    const std::string normalized = normalizeName(name);
    for (const auto& [entryName, type] : unitNameTable()) {
        if (normalizeName(entryName) == normalized) {
            return type;
        }
    }

    std::string numericName(rawName);
    char* end = nullptr;
    const long numeric = std::strtol(numericName.c_str(), &end, 10);
    if (end && *end == '\0' && numeric >= 0 && numeric <= static_cast<int>(bwgame::UnitTypes::None)) {
        return static_cast<bwgame::UnitTypes>(numeric);
    }

    throw std::runtime_error("Unknown OpenBW unit type: " + std::string(rawName));
}

std::string GameState::unitTypeName(bwgame::UnitTypes type) {
    for (const auto& [name, entryType] : unitNameTable()) {
        if (entryType == type) return std::string(name);
    }
    if (type == bwgame::UnitTypes::None) return "None";
    return "UnitType_" + std::to_string(static_cast<int>(type));
}

} // namespace scc
