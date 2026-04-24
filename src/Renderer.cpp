#include "Renderer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace scc {
namespace {

struct PcxImage {
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<std::uint8_t> data;
};

std::uint16_t readLe16(const std::vector<unsigned char>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data.at(offset) | (data.at(offset + 1) << 8));
}

PcxImage loadPcx(const std::vector<unsigned char>& data) {
    if (data.size() < 128 || data[0] != 0x0a || data[2] != 1 || data[3] != 8) {
        throw std::runtime_error("Unsupported PCX data");
    }

    const std::uint16_t minX = readLe16(data, 4);
    const std::uint16_t minY = readLe16(data, 6);
    const std::uint16_t maxX = readLe16(data, 8);
    const std::uint16_t maxY = readLe16(data, 10);
    const std::uint8_t planes = data.at(65);
    const std::uint16_t bytesPerLine = readLe16(data, 66);
    if (minX != 0 || minY != 0 || planes != 1 || bytesPerLine != maxX + 1) {
        throw std::runtime_error("Unsupported PCX layout");
    }

    PcxImage image;
    image.width = maxX + 1;
    image.height = maxY + 1;
    image.data.resize(image.width * image.height);

    std::size_t src = 128;
    std::size_t dst = 0;
    while (dst < image.data.size() && src < data.size()) {
        std::uint8_t value = data[src++];
        if ((value & 0xc0) == 0xc0) {
            const int count = value & 0x3f;
            const std::uint8_t color = data.at(src++);
            for (int i = 0; i < count && dst < image.data.size(); ++i) {
                image.data[dst++] = color;
            }
        } else {
            image.data[dst++] = value;
        }
    }
    if (dst != image.data.size()) {
        throw std::runtime_error("Failed to decode PCX data");
    }
    return image;
}

const char* tilesetName(std::size_t tilesetIndex) {
    static const std::array<const char*, 8> tilesetNames = {
        "badlands", "platform", "install", "AshWorld", "Jungle", "Desert", "Ice", "Twilight",
    };
    return tilesetNames.at(std::min<std::size_t>(tilesetIndex, tilesetNames.size() - 1));
}

const char* lightTableName(std::size_t index) {
    static const std::array<const char*, 7> names = {
        "ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green",
    };
    return names.at(index);
}

sf::Color playerUiColor(int player) {
    static const std::array<sf::Color, 8> colors = {
        sf::Color(220, 48, 48),
        sf::Color(48, 96, 230),
        sf::Color(38, 190, 92),
        sf::Color(160, 74, 205),
        sf::Color(235, 128, 40),
        sf::Color(96, 210, 210),
        sf::Color(238, 214, 67),
        sf::Color(210, 210, 210),
    };
    if (player < 0) player = 0;
    return colors[static_cast<std::size_t>(player) % colors.size()];
}

bool imageHidden(const bwgame::image_t& image) {
    return (image.flags & bwgame::image_t::flag_hidden) != 0;
}

bool imageFlipped(const bwgame::image_t& image) {
    return (image.flags & bwgame::image_t::flag_horizontally_flipped) != 0;
}

bool imageIdInRange(bwgame::ImageTypes id, bwgame::ImageTypes first, bwgame::ImageTypes last) {
    const int value = static_cast<int>(id);
    return value >= static_cast<int>(first) && value <= static_cast<int>(last);
}

bool flameLikeImage(const bwgame::image_t& image) {
    if (!image.image_type) return false;

    const bwgame::ImageTypes id = image.image_type->id;
    return id == bwgame::ImageTypes::IMAGEID_FlameThrower ||
           id == bwgame::ImageTypes::IMAGEID_Wall_Flame_Trap ||
           id == bwgame::ImageTypes::IMAGEID_Wall_Flame_Trap2 ||
           imageIdInRange(id, bwgame::ImageTypes::IMAGEID_Flames1_Type1_Small, bwgame::ImageTypes::IMAGEID_Flames6_Type3_Small) ||
           imageIdInRange(id, bwgame::ImageTypes::IMAGEID_Flames2_Type1_Small, bwgame::ImageTypes::IMAGEID_Flames8_Type3_Small) ||
           imageIdInRange(id, bwgame::ImageTypes::IMAGEID_Flames1_Type1_Large, bwgame::ImageTypes::IMAGEID_Flames6_Type3_Large) ||
           imageIdInRange(id, bwgame::ImageTypes::IMAGEID_Flames2_Type1_Large, bwgame::ImageTypes::IMAGEID_Flames8_Type3_Large);
}

bool imageSuppressesNormalDrawing(const bwgame::image_t& image) {
    if (!image.image_type) return false;

    return image.image_type->id == bwgame::ImageTypes::IMAGEID_White_Circle;
}

bwgame::xy imageMapPosition(const bwgame::image_t& image) {
    bwgame::xy mapPos = image.sprite->position + image.offset;
    const auto& frame = image.grp->frames.at(image.frame_index);
    if (imageFlipped(image)) {
        mapPos.x += static_cast<int>(image.grp->width / 2 - (frame.offset.x + frame.size.x));
    } else {
        mapPos.x += static_cast<int>(frame.offset.x - image.grp->width / 2);
    }
    if (image.flags & bwgame::image_t::flag_y_frozen) {
        mapPos.y = image.frozen_y_value;
    } else {
        mapPos.y += static_cast<int>(frame.offset.y - image.grp->height / 2);
    }
    return mapPos;
}

} // namespace

std::size_t Renderer::TextureKeyHash::operator()(const TextureKey& key) const {
    std::size_t value = reinterpret_cast<std::uintptr_t>(key.grp);
    value ^= key.frame + 0x9e3779b9 + (value << 6) + (value >> 2);
    value ^= static_cast<std::size_t>(key.color + 31 * key.modifier) + 0x9e3779b9 + (value << 6) + (value >> 2);
    value ^= static_cast<std::size_t>(key.modifierData1 + 17 * key.colorShift) + 0x9e3779b9 + (value << 6) + (value >> 2);
    value ^= static_cast<std::size_t>(key.imageId) + 0x9e3779b9 + (value << 6) + (value >> 2);
    value ^= key.flipped ? 0x517cc1b727220a95ull : 0;
    return value;
}

Renderer::Renderer(const GameState& state) {
    loadPalette(state);
    loadPlayerColors(state);
    loadSpecialEffectTables(state);
}

void Renderer::reload(const sf::RenderWindow& window, const GameState& state) {
    textureCache_.clear();
    terrainTextureCache_.clear();
    terrainCreepTextureCache_.clear();
    terrainMiniTiles_.clear();
    terrainMegaTiles_.clear();
    terrainCreepGrp_ = {};
    terrainTilesetIndex_ = static_cast<std::size_t>(-1);
    loadPalette(state);
    loadPlayerColors(state);
    loadSpecialEffectTables(state);
    centerOnCombat(window, state);
}

sf::Vector2f Renderer::combatCenter(const GameState& state) const {
    const auto live = state.liveUnits();
    float centerX = static_cast<float>(state.gameState().map_width) * 0.5f;
    float centerY = static_cast<float>(state.gameState().map_height) * 0.5f;
    if (!live.empty()) {
        centerX = 0.f;
        centerY = 0.f;
        for (const bwgame::unit_t* unit : live) {
            centerX += static_cast<float>(unit->sprite->position.x);
            centerY += static_cast<float>(unit->sprite->position.y);
        }
        centerX /= static_cast<float>(live.size());
        centerY /= static_cast<float>(live.size());
    }
    return {centerX, centerY};
}

void Renderer::resizeView(sf::Vector2u windowSize) {
    if (windowSize.x == 0 || windowSize.y == 0) return;
    view_.setSize({static_cast<float>(windowSize.x) * zoomLevel_, static_cast<float>(windowSize.y) * zoomLevel_});
}

void Renderer::centerOnCombat(const sf::RenderWindow& window, const GameState& state) {
    zoomLevel_ = 1.f;
    view_ = sf::View(combatCenter(state), {static_cast<float>(window.getSize().x), static_cast<float>(window.getSize().y)});
    resizeView(window.getSize());
    viewInitialized_ = true;
    rightMousePanning_ = false;
}

void Renderer::fitToUnits(const sf::RenderWindow& window, const GameState& state) {
    const sf::Vector2u windowSize = window.getSize();
    if (windowSize.x == 0 || windowSize.y == 0) return;

    const auto live = state.liveUnits();
    if (live.empty()) {
        centerOnCombat(window, state);
        return;
    }

    float left = std::numeric_limits<float>::max();
    float top = std::numeric_limits<float>::max();
    float right = std::numeric_limits<float>::lowest();
    float bottom = std::numeric_limits<float>::lowest();
    for (const bwgame::unit_t* unit : live) {
        const bwgame::rect bounds = state.functions().unit_sprite_bounding_box(unit);
        left = std::min(left, static_cast<float>(bounds.from.x));
        top = std::min(top, static_cast<float>(bounds.from.y));
        right = std::max(right, static_cast<float>(bounds.to.x));
        bottom = std::max(bottom, static_cast<float>(bounds.to.y));
    }

    constexpr float padding = 96.f;
    left -= padding;
    top -= padding;
    right += padding;
    bottom += padding;

    const float centerX = (left + right) * 0.5f;
    const float centerY = (top + bottom) * 0.5f;
    const float requiredWidth = std::max(1.f, right - left);
    const float requiredHeight = std::max(1.f, bottom - top);
    const float windowWidth = static_cast<float>(windowSize.x);
    const float windowHeight = static_cast<float>(windowSize.y);
    const float requiredZoom = std::max(requiredWidth / windowWidth, requiredHeight / windowHeight);

    constexpr float minAutoZoomLevel = 0.25f;
    zoomLevel_ = std::max(requiredZoom, minAutoZoomLevel);
    view_.setCenter({centerX, centerY});
    view_.setSize({windowWidth * zoomLevel_, windowHeight * zoomLevel_});
    viewInitialized_ = true;
    rightMousePanning_ = false;
}

void Renderer::handleEvent(sf::RenderWindow& window, const sf::Event& event) {
    if (!viewInitialized_) {
        view_ = window.getDefaultView();
        zoomLevel_ = 1.f;
        viewInitialized_ = true;
    }

    if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        resizeView(resized->size);
        return;
    }

    if (const auto* wheel = event.getIf<sf::Event::MouseWheelScrolled>()) {
        if (wheel->wheel != sf::Mouse::Wheel::Vertical) return;

        const sf::Vector2f beforeZoom = window.mapPixelToCoords(wheel->position, view_);
        constexpr float minZoomLevel = 0.25f;
        constexpr float maxZoomLevel = 16.f;
        const float requestedFactor = std::pow(0.9f, wheel->delta);
        const float newZoomLevel = std::clamp(zoomLevel_ * requestedFactor, minZoomLevel, maxZoomLevel);
        const float appliedFactor = newZoomLevel / zoomLevel_;
        if (appliedFactor == 1.f) return;

        view_.zoom(appliedFactor);
        zoomLevel_ = newZoomLevel;
        const sf::Vector2f afterZoom = window.mapPixelToCoords(wheel->position, view_);
        view_.move(beforeZoom - afterZoom);
        return;
    }

    if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        if (pressed->button == sf::Mouse::Button::Right) {
            rightMousePanning_ = true;
            lastPanPixel_ = pressed->position;
        }
        return;
    }

    if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Right) {
            rightMousePanning_ = false;
        }
        return;
    }

    if (const auto* moved = event.getIf<sf::Event::MouseMoved>()) {
        if (!rightMousePanning_) return;

        const sf::Vector2f previousWorld = window.mapPixelToCoords(lastPanPixel_, view_);
        const sf::Vector2f currentWorld = window.mapPixelToCoords(moved->position, view_);
        view_.move(previousWorld - currentWorld);
        lastPanPixel_ = moved->position;
    }
}

void Renderer::loadPalette(const GameState& state) {
    auto data = state.loadDataFile(std::string("Tileset/") + tilesetName(state.gameState().tileset_index) + ".wpe");
    if (data.size() != 256 * 4) {
        throw std::runtime_error("Tileset palette has an unexpected size");
    }
    for (std::size_t i = 0; i < palette_.size(); ++i) {
        palette_[i] = sf::Color(data[i * 4 + 0], data[i * 4 + 1], data[i * 4 + 2], 255);
    }
}

void Renderer::initializeCreepEdgeFrameIndex() {
    std::array<int, 0x100> neighborIndex{};
    std::array<int, 128> compactNeighborIndex{};

    for (std::size_t i = 0; i != neighborIndex.size(); ++i) {
        int value = 0;
        if (i & 2) value |= 0x10;
        if (i & 8) value |= 0x24;
        if (i & 0x10) value |= 9;
        if (i & 0x40) value |= 2;
        if ((i & 0xc0) == 0xc0) value |= 1;
        if ((i & 0x60) == 0x60) value |= 4;
        if ((i & 3) == 3) value |= 0x20;
        if ((i & 6) == 6) value |= 8;
        if ((value & 0x21) == 0x21) value |= 0x40;
        if ((value & 0xc) == 0xc) value |= 0x40;
        neighborIndex[i] = value;
    }

    int compactIndex = 0;
    for (int i = 0; i != static_cast<int>(compactNeighborIndex.size()); ++i) {
        if (std::find(neighborIndex.begin(), neighborIndex.end(), i) == neighborIndex.end()) continue;
        compactNeighborIndex[static_cast<std::size_t>(i)] = compactIndex++;
    }

    for (std::size_t i = 0; i != creepEdgeFrameIndex_.size(); ++i) {
        creepEdgeFrameIndex_[i] = compactNeighborIndex[static_cast<std::size_t>(neighborIndex[i])];
    }
}

void Renderer::loadTerrainTileset(const GameState& state) {
    const std::size_t tileset = std::min<std::size_t>(state.gameState().tileset_index, 7);
    if (terrainTilesetIndex_ == tileset && !terrainMiniTiles_.empty() && !terrainMegaTiles_.empty()) return;

    terrainTextureCache_.clear();
    terrainCreepTextureCache_.clear();
    terrainMiniTiles_.clear();
    terrainMegaTiles_.clear();
    terrainCreepGrp_ = {};
    terrainTilesetIndex_ = tileset;

    const std::string base = std::string("Tileset/") + tilesetName(tileset);
    const auto vr4Data = state.loadDataFile(base + ".vr4");
    const auto vx4Data = state.loadDataFile(base + ".vx4");
    const auto grpData = state.loadDataFile(base + ".grp");

    if (vr4Data.size() % 64 != 0) {
        throw std::runtime_error("Terrain VR4 data has an unexpected size");
    }
    terrainMiniTiles_.resize(vr4Data.size() / 64);
    for (std::size_t i = 0; i != terrainMiniTiles_.size(); ++i) {
        const std::size_t baseOffset = i * 64;
        for (std::size_t y = 0; y != 8; ++y) {
            for (std::size_t x = 0; x != 8; ++x) {
                terrainMiniTiles_[i].pixels[y * 8 + x] = vr4Data[baseOffset + y * 8 + x];
                terrainMiniTiles_[i].flippedPixels[y * 8 + x] = vr4Data[baseOffset + y * 8 + (7 - x)];
            }
        }
    }

    if (vx4Data.size() % 32 != 0) {
        throw std::runtime_error("Terrain VX4 data has an unexpected size");
    }
    terrainMegaTiles_.resize(vx4Data.size() / 32);
    for (std::size_t i = 0; i != terrainMegaTiles_.size(); ++i) {
        const std::size_t baseOffset = i * 32;
        for (std::size_t image = 0; image != terrainMegaTiles_[i].miniTileIndices.size(); ++image) {
            terrainMegaTiles_[i].miniTileIndices[image] = readLe16(vx4Data, baseOffset + image * 2);
        }
    }

    terrainCreepGrp_ = bwgame::read_grp(bwgame::data_loading::data_reader_le(grpData.data(), grpData.data() + grpData.size()));
    initializeCreepEdgeFrameIndex();
}

void Renderer::ensureCreepRandomTileIndices(std::size_t tileCount) {
    if (creepRandomTileIndices_.size() == tileCount) return;

    creepRandomTileIndices_.resize(tileCount);
    std::uint32_t randomState = 0x13579bdfu;
    auto nextRandom = [&]() {
        randomState = randomState * 22695477u + 1u;
        return (randomState >> 16u) & 0x7fffu;
    };

    for (std::uint8_t& value : creepRandomTileIndices_) {
        if (nextRandom() % 100 < 4) {
            value = static_cast<std::uint8_t>(6 + nextRandom() % 7);
        } else {
            value = static_cast<std::uint8_t>(nextRandom() % 6);
        }
    }
}

std::uint16_t Renderer::mapMegatileIndex(const GameState& state, std::size_t tileX, std::size_t tileY) {
    const auto& game = state.gameState();
    const auto& raw = state.rawState();
    const std::size_t tileIndex = tileY * game.map_tile_width + tileX;
    const bwgame::tile_t& tile = raw.tiles.at(tileIndex);

    if ((tile.flags & bwgame::tile_t::flag_has_creep) != 0 && game.cv5.size() > 1) {
        ensureCreepRandomTileIndices(raw.tiles.size());
        return game.cv5.at(1).mega_tile_index[creepRandomTileIndices_.at(tileIndex) % 16];
    }

    return static_cast<std::uint16_t>(raw.tiles_mega_tile_index.at(tileIndex) & 0x7fff);
}

sf::Texture& Renderer::terrainTextureFor(std::uint16_t megatileIndex) {
    auto found = terrainTextureCache_.find(megatileIndex);
    if (found != terrainTextureCache_.end()) return found->second;

    if (terrainMegaTiles_.empty() || terrainMiniTiles_.empty()) {
        throw std::runtime_error("Terrain tileset is not loaded");
    }

    const TerrainMegaTile& megaTile = terrainMegaTiles_.at(megatileIndex % terrainMegaTiles_.size());
    std::vector<std::uint8_t> pixels(32 * 32 * 4, 255);

    for (std::size_t miniY = 0; miniY != 4; ++miniY) {
        for (std::size_t miniX = 0; miniX != 4; ++miniX) {
            const std::uint16_t imageIndex = megaTile.miniTileIndices[miniY * 4 + miniX];
            const bool flipped = (imageIndex & 1) != 0;
            const TerrainMiniTile& miniTile = terrainMiniTiles_.at((imageIndex / 2) % terrainMiniTiles_.size());
            const auto& indexedPixels = flipped ? miniTile.flippedPixels : miniTile.pixels;

            for (std::size_t y = 0; y != 8; ++y) {
                for (std::size_t x = 0; x != 8; ++x) {
                    sf::Color color = palette_[indexedPixels[y * 8 + x]];
                    color.a = 255;
                    const std::size_t outX = miniX * 8 + x;
                    const std::size_t outY = miniY * 8 + y;
                    const std::size_t offset = (outY * 32 + outX) * 4;
                    pixels[offset + 0] = color.r;
                    pixels[offset + 1] = color.g;
                    pixels[offset + 2] = color.b;
                    pixels[offset + 3] = color.a;
                }
            }
        }
    }

    sf::Image image({32u, 32u}, pixels.data());
    auto [it, inserted] = terrainTextureCache_.try_emplace(megatileIndex);
    if (!it->second.loadFromImage(image)) {
        throw std::runtime_error("Failed to create SFML texture from terrain tile");
    }
    return it->second;
}

sf::Texture& Renderer::terrainCreepTextureFor(std::size_t frameIndex) {
    auto found = terrainCreepTextureCache_.find(frameIndex);
    if (found != terrainCreepTextureCache_.end()) return found->second;

    const bwgame::grp_t::frame_t& frame = terrainCreepGrp_.frames.at(frameIndex);
    const std::size_t width = frame.size.x;
    const std::size_t height = frame.size.y;
    std::vector<std::uint8_t> pixels(width * height * 4, 0);

    auto writePixel = [&](std::size_t x, std::size_t y, std::uint8_t paletteIndex) {
        const sf::Color color = palette_[paletteIndex];
        const std::size_t offset = (y * width + x) * 4;
        pixels[offset + 0] = color.r;
        pixels[offset + 1] = color.g;
        pixels[offset + 2] = color.b;
        pixels[offset + 3] = color.a;
    };

    for (std::size_t y = 0; y != height; ++y) {
        const std::uint8_t* encoded = frame.data_container.data() + frame.line_data_offset.at(y);
        for (std::size_t x = 0; x != width;) {
            int run = *encoded++;
            if (run & 0x80) {
                x += static_cast<std::size_t>(run & 0x7f);
            } else if (run & 0x40) {
                run &= 0x3f;
                const std::uint8_t color = *encoded++;
                for (int i = 0; i < run && x < width; ++i, ++x) writePixel(x, y, color);
            } else {
                for (int i = 0; i < run && x < width; ++i, ++x) writePixel(x, y, *encoded++);
            }
        }
    }

    sf::Image image({static_cast<unsigned int>(width), static_cast<unsigned int>(height)}, pixels.data());
    auto [it, inserted] = terrainCreepTextureCache_.try_emplace(frameIndex);
    if (!it->second.loadFromImage(image)) {
        throw std::runtime_error("Failed to create SFML texture from terrain creep edge");
    }
    return it->second;
}

void Renderer::loadPlayerColors(const GameState& state) {
    for (std::size_t player = 0; player < playerUnitColors_.size(); ++player) {
        for (std::size_t i = 0; i < playerUnitColors_[player].size(); ++i) {
            playerUnitColors_[player][i] = static_cast<std::uint8_t>(8 + i);
        }
    }

    try {
        const auto pcx = loadPcx(state.loadDataFile("game/tunit.pcx"));
        if (pcx.width == 128 && pcx.height == 1) {
            for (std::size_t player = 0; player < playerUnitColors_.size(); ++player) {
                for (std::size_t i = 0; i < playerUnitColors_[player].size(); ++i) {
                    playerUnitColors_[player][i] = pcx.data[player * 8 + i];
                }
            }
        }
    } catch (...) {
        // The ring and health bar still distinguish players if the remap table is absent.
    }
}

void Renderer::loadSpecialEffectTables(const GameState& state) {
    lightRemapTables_ = {};

    const std::string base = std::string("Tileset/") + tilesetName(state.gameState().tileset_index) + "/";
    for (std::size_t tableIndex = 0; tableIndex != lightRemapTables_.size(); ++tableIndex) {
        try {
            const PcxImage pcx = loadPcx(state.loadDataFile(base + lightTableName(tableIndex) + ".pcx"));
            if (pcx.width != 256 || pcx.data.empty() || pcx.data.size() % 256 != 0) continue;

            lightRemapTables_[tableIndex] = pcx.data;
        } catch (...) {
            lightRemapTables_[tableIndex].clear();
        }
    }
}

sf::Texture& Renderer::textureFor(const bwgame::image_t& image, int colorIndex) {
    const bool flipped = imageFlipped(image);
    const int colorShift = image.image_type ? image.image_type->color_shift : 0;
    const int imageId = image.image_type ? static_cast<int>(image.image_type->id) : 0;
    const TextureKey key{image.grp, image.frame_index, colorIndex, image.modifier, image.modifier_data1, colorShift, imageId, flipped};
    auto found = textureCache_.find(key);
    if (found != textureCache_.end()) return found->second;

    const auto& frame = image.grp->frames.at(image.frame_index);
    const std::size_t width = frame.size.x;
    const std::size_t height = frame.size.y;
    std::vector<std::uint8_t> pixels(width * height * 4, 0);

    auto lightTableColor = [&](std::size_t tableIndex, std::uint8_t paletteIndex, bool forceTint) {
        if (tableIndex >= lightRemapTables_.size()) return sf::Color(0, 0, 0, 0);

        if (tableIndex == 4 && !forceTint) {
            sf::Color color = palette_[paletteIndex];
            color.a = 96;
            return color;
        }

        if (paletteIndex == 0) return sf::Color(0, 0, 0, 0);
        const std::size_t row = static_cast<std::size_t>(paletteIndex - 1);
        const std::size_t rows = lightRemapTables_[tableIndex].size() / 256;
        if (rows == 0 || row >= rows) return sf::Color(0, 0, 0, 0);

        struct Tint {
            std::uint8_t r;
            std::uint8_t g;
            std::uint8_t b;
            std::uint8_t maxAlpha;
        };
        static const std::array<Tint, 7> tints = {{
            {255, 92, 20, 118},
            {92, 255, 72, 108},
            {76, 178, 255, 118},
            {255, 198, 92, 122},
            {255, 180, 82, 96},
            {255, 66, 58, 112},
            {70, 255, 76, 108},
        }};

        const float intensity = std::sqrt(static_cast<float>(row + 1) / static_cast<float>(rows));
        const Tint tint = tints[tableIndex];
        return sf::Color(
            tint.r,
            tint.g,
            tint.b,
            static_cast<std::uint8_t>(std::clamp(static_cast<int>(12 + tint.maxAlpha * intensity), 0, 170)));
    };

    const bool shieldOverlay = image.image_type && image.image_type->id == bwgame::ImageTypes::IMAGEID_Shield_Overlay;
    const bool flameOverlay = flameLikeImage(image);
    auto shieldOverlayColor = [&](std::uint8_t paletteIndex) {
        if (paletteIndex == 0) return sf::Color(0, 0, 0, 0);

        std::size_t tableIndex = colorShift > 0 ? static_cast<std::size_t>(colorShift - 1) : 2;
        if (tableIndex >= lightRemapTables_.size()) tableIndex = 2;

        float intensity = static_cast<float>(paletteIndex) / 255.f;
        const std::size_t rowCount = lightRemapTables_[tableIndex].size() / 256;
        if (rowCount > 1) {
            intensity = static_cast<float>(std::min<std::size_t>(paletteIndex - 1, rowCount - 1)) / static_cast<float>(rowCount - 1);
        }
        intensity = std::sqrt(std::clamp(intensity, 0.f, 1.f));

        const auto lerp = [intensity](int low, int high) {
            return static_cast<std::uint8_t>(std::clamp(static_cast<int>(low + (high - low) * intensity), 0, 255));
        };

        return sf::Color(
            lerp(70, 190),
            lerp(170, 238),
            255,
            lerp(18, 128));
    };

    auto writePixel = [&](std::size_t x, std::size_t y, std::uint8_t paletteIndex) {
        sf::Color color;
        if (paletteIndex >= 8 && paletteIndex < 16 && (image.modifier == 0 || image.modifier == 1 || image.modifier == 14)) {
            paletteIndex = playerUnitColors_[static_cast<std::size_t>(colorIndex) % playerUnitColors_.size()][paletteIndex - 8];
        }

        if (shieldOverlay) {
            color = shieldOverlayColor(paletteIndex);
        } else if (image.modifier == 9 && colorShift > 0) {
            color = lightTableColor(static_cast<std::size_t>(colorShift - 1), paletteIndex, flameOverlay);
        } else if (image.modifier == 17) {
            const int row = std::max(0, image.modifier_data1);
            color = lightTableColor(0, static_cast<std::uint8_t>(std::min(row, 255)), true);
        } else if (image.modifier == 10) {
            color = sf::Color(0, 0, 0, 95);
        } else {
            color = palette_[paletteIndex];
        }

        const std::size_t outX = flipped ? (width - 1 - x) : x;
        const std::size_t offset = (y * width + outX) * 4;
        pixels[offset + 0] = color.r;
        pixels[offset + 1] = color.g;
        pixels[offset + 2] = color.b;
        pixels[offset + 3] = color.a;
    };

    for (std::size_t y = 0; y < height; ++y) {
        const std::uint8_t* encoded = frame.data_container.data() + frame.line_data_offset.at(y);
        for (std::size_t x = 0; x < width;) {
            int run = *encoded++;
            if (run & 0x80) {
                x += static_cast<std::size_t>(run & 0x7f);
            } else if (run & 0x40) {
                run &= 0x3f;
                const std::uint8_t color = *encoded++;
                for (int i = 0; i < run && x < width; ++i, ++x) writePixel(x, y, color);
            } else {
                for (int i = 0; i < run && x < width; ++i, ++x) writePixel(x, y, *encoded++);
            }
        }
    }

    sf::Image sfImage({static_cast<unsigned int>(width), static_cast<unsigned int>(height)}, pixels.data());
    auto [it, inserted] = textureCache_.try_emplace(key);
    if (!it->second.loadFromImage(sfImage)) {
        throw std::runtime_error("Failed to create SFML texture from GRP frame");
    }
    return it->second;
}

sf::BlendMode Renderer::blendModeFor(const bwgame::image_t& image) const {
    const int colorShift = image.image_type ? image.image_type->color_shift : 0;
    if (image.image_type && image.image_type->id == bwgame::ImageTypes::IMAGEID_Shield_Overlay) {
        return sf::BlendAdd;
    }
    if (image.modifier == 9 && colorShift > 0 && colorShift != 5) {
        return sf::BlendAdd;
    }
    if (image.modifier == 17) {
        return sf::BlendAdd;
    }
    return sf::BlendAlpha;
}

void Renderer::drawSprite(sf::RenderWindow& window, const bwgame::sprite_t& sprite, int colorIndex) {
    for (const bwgame::image_t* image : bwgame::ptr(sprite.images)) {
        if (!image || !image->grp || imageHidden(*image)) continue;
        if (imageSuppressesNormalDrawing(*image)) continue;
        if (image->frame_index >= image->grp->frames.size()) continue;

        sf::Texture& texture = textureFor(*image, colorIndex);
        sf::Sprite sfSprite(texture);
        const bwgame::xy pos = imageMapPosition(*image);
        sfSprite.setPosition({static_cast<float>(pos.x), static_cast<float>(pos.y)});
        sf::RenderStates states;
        states.blendMode = blendModeFor(*image);
        window.draw(sfSprite, states);
    }
}

void Renderer::drawAbstractTerrain(sf::RenderWindow& window, const GameState& state, const sf::View& view) {
    const auto& game = state.gameState();
    const auto& raw = state.rawState();
    if (game.map_tile_width == 0 || game.map_tile_height == 0 || raw.tiles.empty()) return;

    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();
    const int x0 = std::max(0, static_cast<int>(std::floor((center.x - size.x * 0.5f) / 32.f)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor((center.y - size.y * 0.5f) / 32.f)) - 1);
    const int x1 = std::min(static_cast<int>(game.map_tile_width), static_cast<int>(std::ceil((center.x + size.x * 0.5f) / 32.f)) + 1);
    const int y1 = std::min(static_cast<int>(game.map_tile_height), static_cast<int>(std::ceil((center.y + size.y * 0.5f) / 32.f)) + 1);

    sf::RectangleShape tileShape({32.f, 32.f});
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const auto& tile = raw.tiles.at(static_cast<std::size_t>(y) * game.map_tile_width + x);
            sf::Color color(58, 93, 63);
            if (~tile.flags & bwgame::tile_t::flag_walkable) color = sf::Color(61, 64, 68);
            else if (tile.flags & bwgame::tile_t::flag_has_creep) color = sf::Color(92, 65, 94);
            else if (tile.flags & bwgame::tile_t::flag_very_high) color = sf::Color(91, 120, 79);
            else if (tile.flags & bwgame::tile_t::flag_high) color = sf::Color(75, 108, 71);
            else if (tile.flags & bwgame::tile_t::flag_middle) color = sf::Color(65, 100, 67);
            if (((x ^ y) & 1) != 0) {
                color.r = static_cast<std::uint8_t>(std::max(0, static_cast<int>(color.r) - 5));
                color.g = static_cast<std::uint8_t>(std::max(0, static_cast<int>(color.g) - 5));
                color.b = static_cast<std::uint8_t>(std::max(0, static_cast<int>(color.b) - 5));
            }
            tileShape.setFillColor(color);
            tileShape.setPosition({static_cast<float>(x * 32), static_cast<float>(y * 32)});
            window.draw(tileShape);
        }
    }
}

void Renderer::drawRealTerrain(sf::RenderWindow& window, const GameState& state, const sf::View& view) {
    const auto& game = state.gameState();
    const auto& raw = state.rawState();
    if (game.map_tile_width == 0 || game.map_tile_height == 0 || raw.tiles.empty()) return;

    loadTerrainTileset(state);

    const sf::Vector2f center = view.getCenter();
    const sf::Vector2f size = view.getSize();
    const int x0 = std::max(0, static_cast<int>(std::floor((center.x - size.x * 0.5f) / 32.f)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor((center.y - size.y * 0.5f) / 32.f)) - 1);
    const int x1 = std::min(static_cast<int>(game.map_tile_width), static_cast<int>(std::ceil((center.x + size.x * 0.5f) / 32.f)) + 1);
    const int y1 = std::min(static_cast<int>(game.map_tile_height), static_cast<int>(std::ceil((center.y + size.y * 0.5f) / 32.f)) + 1);

    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            sf::Sprite tile(terrainTextureFor(mapMegatileIndex(state, static_cast<std::size_t>(x), static_cast<std::size_t>(y))));
            tile.setPosition({static_cast<float>(x * 32), static_cast<float>(y * 32)});
            window.draw(tile);
        }
    }

    static const std::array<bwgame::xy, 9> dirs = {{{1, 1}, {0, 1}, {-1, 1}, {1, 0}, {-1, 0}, {1, -1}, {0, -1}, {-1, -1}, {0, 0}}};
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t tileIndex = static_cast<std::size_t>(y) * game.map_tile_width + static_cast<std::size_t>(x);
            if ((raw.tiles.at(tileIndex).flags & bwgame::tile_t::flag_has_creep) != 0) continue;

            std::size_t creepIndex = 0;
            for (std::size_t i = 0; i != dirs.size(); ++i) {
                const int neighborX = x + dirs[i].x;
                const int neighborY = y + dirs[i].y;
                if (neighborX < 0 || neighborY < 0) continue;
                if (neighborX >= static_cast<int>(game.map_tile_width) || neighborY >= static_cast<int>(game.map_tile_height)) continue;

                const std::size_t neighborIndex = static_cast<std::size_t>(neighborY) * game.map_tile_width + static_cast<std::size_t>(neighborX);
                if ((raw.tiles.at(neighborIndex).flags & bwgame::tile_t::flag_has_creep) != 0) creepIndex |= 1u << i;
            }

            const int creepFrame = creepEdgeFrameIndex_.at(creepIndex);
            if (creepFrame <= 0 || terrainCreepGrp_.frames.empty()) continue;

            const std::size_t frameIndex = static_cast<std::size_t>(creepFrame - 1);
            const bwgame::grp_t::frame_t& frame = terrainCreepGrp_.frames.at(frameIndex);
            sf::Sprite edge(terrainCreepTextureFor(frameIndex));
            edge.setPosition({static_cast<float>(x * 32 + static_cast<int>(frame.offset.x)),
                              static_cast<float>(y * 32 + static_cast<int>(frame.offset.y))});
            window.draw(edge);
        }
    }
}

void Renderer::drawCollisionBounds(sf::RenderWindow& window, const GameState& state, const bwgame::unit_t& unit) {
    const bwgame::rect bounds = state.functions().unit_sprite_bounding_box(&unit);
    const float width = static_cast<float>(bounds.to.x - bounds.from.x);
    const float height = static_cast<float>(bounds.to.y - bounds.from.y);
    if (width <= 0.f || height <= 0.f) return;

    sf::RectangleShape box({width, height});
    box.setPosition({static_cast<float>(bounds.from.x), static_cast<float>(bounds.from.y)});
    box.setFillColor(sf::Color(0, 0, 0, 0));
    box.setOutlineThickness(2.f);
    box.setOutlineColor(playerUiColor(unit.owner));
    window.draw(box);
}

void Renderer::drawHealth(sf::RenderWindow& window, const bwgame::unit_t& unit) {
    const float maxHp = std::max(1, unit.unit_type->hitpoints.integer_part());
    const float hp = std::clamp(static_cast<float>(unit.hp.integer_part()), 0.f, maxHp);
    const float width = 34.f;
    const float height = 4.f;
    const float x = static_cast<float>(unit.sprite->position.x) - width / 2.f;
    const float y = static_cast<float>(unit.sprite->position.y - unit.unit_type->dimensions.from.y - 12);

    sf::RectangleShape back({width, height});
    back.setPosition({x, y});
    back.setFillColor(sf::Color(22, 22, 22, 220));
    window.draw(back);

    sf::RectangleShape fill({width * (hp / maxHp), height});
    fill.setPosition({x, y});
    fill.setFillColor(hp / maxHp > 0.5f ? sf::Color(56, 210, 92) : sf::Color(235, 185, 58));
    window.draw(fill);
}

void Renderer::draw(sf::RenderWindow& window, const GameState& state, bool showCollisionBoxes, bool drawGameTerrain) {
    if (!viewInitialized_) {
        centerOnCombat(window, state);
    }
    window.setView(view_);
    window.clear(sf::Color(28, 42, 35));
    if (drawGameTerrain) {
        drawRealTerrain(window, state, view_);
    } else {
        drawAbstractTerrain(window, state, view_);
    }

    std::vector<const bwgame::unit_t*> units;
    for (const bwgame::unit_t* unit : bwgame::ptr(state.rawState().visible_units)) {
        if (unit && unit->sprite) units.push_back(unit);
    }
    std::sort(units.begin(), units.end(), [](const bwgame::unit_t* a, const bwgame::unit_t* b) {
        return a->sprite->position.y < b->sprite->position.y;
    });

    for (const bwgame::unit_t* unit : units) drawSprite(window, *unit->sprite, state.rawState().players[unit->owner].color);
    if (showCollisionBoxes) {
        for (const bwgame::unit_t* unit : units) drawCollisionBounds(window, state, *unit);
    }
    for (const bwgame::unit_t* unit : units) drawHealth(window, *unit);

    for (const bwgame::bullet_t* bullet : bwgame::ptr(state.rawState().active_bullets)) {
        if (bullet && bullet->sprite) drawSprite(window, *bullet->sprite, bullet->owner);
    }
}

} // namespace scc
