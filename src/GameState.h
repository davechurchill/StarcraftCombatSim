#pragma once

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "OpenBWCombat.h"

namespace scc {

struct UnitSnapshot {
    int id = 0;
    int player = 0;
    std::string typeName;
    bwgame::UnitTypes type = bwgame::UnitTypes::None;
    int x = 0;
    int y = 0;
    int hitPoints = 0;
    int maxHitPoints = 0;
    int shields = 0;
    int maxShields = 0;
    bool alive = false;
};

class GameState {
public:
    explicit GameState(std::filesystem::path dataPath, std::filesystem::path mapPath = {});
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = default;
    GameState& operator=(GameState&&) = default;

    void reset();
    void loadScenario(const std::filesystem::path& scenarioPath);
    bwgame::unit_t* addUnit(bwgame::UnitTypes type, int player, bwgame::xy position);
    void setAllPlayerScripts(std::string_view scriptName);
    void setPlayerScript(int player, std::string_view scriptName);

    void update();
    void update(int frames);
    void issueAttackClosest();
    const std::string& scriptName() const;

    bool isTerminal() const;
    int frame() const;
    std::vector<UnitSnapshot> units() const;
    std::vector<bwgame::unit_t*> liveUnits() const;

    const bwgame::global_state& globalState() const;
    const bwgame::game_state& gameState() const;
    const bwgame::state& rawState() const;
    bwgame::state& rawState();
    bwgame::state_functions& functions();
    const bwgame::state_functions& functions() const;
    const std::filesystem::path& mapPath() const;

    std::vector<unsigned char> loadDataFile(std::string filename) const;

    static bwgame::UnitTypes parseUnitType(std::string_view name);
    static std::string unitTypeName(bwgame::UnitTypes type);

private:
    std::filesystem::path dataPath_;
    std::filesystem::path mapPath_;
    std::array<std::string, 12> playerScripts_;
    std::string scriptSummary_ = "AttackClosest";
    mutable bwgame::data_loading::data_files_loader<> dataLoader_;
    std::unique_ptr<bwgame::global_state> global_;
    std::unique_ptr<bwgame::game_state> game_;
    std::unique_ptr<bwgame::state> state_;
    std::unique_ptr<bwgame::game_load_functions> loadFunctions_;
    std::unique_ptr<bwgame::state_functions> frameFunctions_;

    void initializeOpenBW();
    void initializeDataLoader();
    void loadMap();
    void configurePlayers();
    void fillCarrierInterceptors(bwgame::unit_t* carrier);
    void refreshScriptSummary();
    void runScripts();
    bool isAlive(const bwgame::unit_t* unit) const;
};

} // namespace scc
