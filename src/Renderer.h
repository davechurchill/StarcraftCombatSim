#pragma once

#include "GameState.h"

#include <SFML/Graphics.hpp>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace scc {

class Renderer {
public:
    explicit Renderer(const GameState& state);

    void reload(const sf::RenderWindow& window, const GameState& state);
    void centerOnCombat(const sf::RenderWindow& window, const GameState& state);
    void fitToUnits(const sf::RenderWindow& window, const GameState& state);
    void handleEvent(sf::RenderWindow& window, const sf::Event& event);
    void draw(sf::RenderWindow& window, const GameState& state, bool showCollisionBoxes, bool drawGameTerrain);

private:
    struct TextureKey {
        const bwgame::grp_t* grp = nullptr;
        std::size_t frame = 0;
        int color = 0;
        int modifier = 0;
        int modifierData1 = 0;
        int colorShift = 0;
        int imageId = 0;
        bool flipped = false;

        bool operator==(const TextureKey& other) const {
            return grp == other.grp &&
                   frame == other.frame &&
                   color == other.color &&
                   modifier == other.modifier &&
                   modifierData1 == other.modifierData1 &&
                   colorShift == other.colorShift &&
                   imageId == other.imageId &&
                   flipped == other.flipped;
        }
    };

    struct TextureKeyHash {
        std::size_t operator()(const TextureKey& key) const;
    };

    struct TerrainMiniTile {
        std::array<std::uint8_t, 64> pixels{};
        std::array<std::uint8_t, 64> flippedPixels{};
    };

    struct TerrainMegaTile {
        std::array<std::uint16_t, 16> miniTileIndices{};
    };

    std::array<sf::Color, 256> palette_{};
    std::array<std::array<std::uint8_t, 8>, 16> playerUnitColors_{};
    std::array<std::vector<std::uint8_t>, 7> lightRemapTables_;
    std::unordered_map<TextureKey, sf::Texture, TextureKeyHash> textureCache_;
    std::vector<TerrainMiniTile> terrainMiniTiles_;
    std::vector<TerrainMegaTile> terrainMegaTiles_;
    std::unordered_map<std::uint16_t, sf::Texture> terrainTextureCache_;
    std::unordered_map<std::size_t, sf::Texture> terrainCreepTextureCache_;
    bwgame::grp_t terrainCreepGrp_;
    std::array<int, 0x100> creepEdgeFrameIndex_{};
    std::vector<std::uint8_t> creepRandomTileIndices_;
    std::size_t terrainTilesetIndex_ = static_cast<std::size_t>(-1);
    sf::View view_;
    float zoomLevel_ = 1.f;
    bool viewInitialized_ = false;
    bool rightMousePanning_ = false;
    sf::Vector2i lastPanPixel_{};

    void loadPalette(const GameState& state);
    void loadPlayerColors(const GameState& state);
    void loadSpecialEffectTables(const GameState& state);
    sf::Vector2f combatCenter(const GameState& state) const;
    void resizeView(sf::Vector2u windowSize);
    sf::Texture& textureFor(const bwgame::image_t& image, int colorIndex);
    sf::BlendMode blendModeFor(const bwgame::image_t& image) const;
    void drawSprite(sf::RenderWindow& window, const bwgame::sprite_t& sprite, int colorIndex);
    void drawAbstractTerrain(sf::RenderWindow& window, const GameState& state, const sf::View& view);
    void drawRealTerrain(sf::RenderWindow& window, const GameState& state, const sf::View& view);
    void drawHealth(sf::RenderWindow& window, const bwgame::unit_t& unit);
    void drawCollisionBounds(sf::RenderWindow& window, const GameState& state, const bwgame::unit_t& unit);
    void loadTerrainTileset(const GameState& state);
    void initializeCreepEdgeFrameIndex();
    void ensureCreepRandomTileIndices(std::size_t tileCount);
    std::uint16_t mapMegatileIndex(const GameState& state, std::size_t tileX, std::size_t tileY);
    sf::Texture& terrainTextureFor(std::uint16_t megatileIndex);
    sf::Texture& terrainCreepTextureFor(std::size_t frameIndex);
};

} // namespace scc
