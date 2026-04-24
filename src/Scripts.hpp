#pragma once

#include "GameState.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

namespace scc::Scripts {

inline std::string normalizeName(std::string_view value) {
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

inline std::string canonicalName(std::string_view scriptName) {
    const std::string normalized = normalizeName(scriptName);
    if (normalized.empty() || normalized == "none" || normalized == "no_script") {
        return "None";
    }
    if (normalized == "attackclosest" || normalized == "attack_closest") {
        return "AttackClosest";
    }
    if (normalized == "kiteclosest" || normalized == "kite_closest") {
        return "KiteClosest";
    }
    throw std::runtime_error("Unknown combat script: " + std::string(scriptName));
}

inline bwgame::unit_t* closestEnemy(GameState& state, bwgame::unit_t* unit) {
    bwgame::unit_t* best = nullptr;
    long long bestDistance = std::numeric_limits<long long>::max();
    for (bwgame::unit_t* other : state.liveUnits()) {
        if (other == unit) continue;
        if (other->owner == unit->owner) continue;
        if (state.rawState().alliances[unit->owner][other->owner]) continue;

        const long long dx = static_cast<long long>(other->sprite->position.x) - unit->sprite->position.x;
        const long long dy = static_cast<long long>(other->sprite->position.y) - unit->sprite->position.y;
        const long long distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = other;
        }
    }
    return best;
}

inline bwgame::unit_t* closestAttackableEnemy(GameState& state, bwgame::unit_t* unit) {
    bwgame::unit_t* best = nullptr;
    long long bestDistance = std::numeric_limits<long long>::max();
    for (bwgame::unit_t* other : state.liveUnits()) {
        if (other == unit) continue;
        if (other->owner == unit->owner) continue;
        if (state.rawState().alliances[unit->owner][other->owner]) continue;
        if (!state.functions().unit_can_attack_target(unit, other)) continue;

        const int distance = state.functions().units_distance(state.functions().unit_main_unit(unit), other);
        const long long score = static_cast<long long>(distance) * distance;
        if (score < bestDistance) {
            bestDistance = score;
            best = other;
        }
    }
    return best;
}

inline bool isMedic(const bwgame::unit_t* unit) {
    return unit && unit->unit_type && unit->unit_type->id == bwgame::UnitTypes::Terran_Medic;
}

inline bwgame::unit_t* closestHealTarget(GameState& state, bwgame::unit_t* medic) {
    if (!state.functions().unit_can_use_tech(medic, state.functions().get_tech_type(bwgame::TechTypes::Healing))) {
        return nullptr;
    }

    bwgame::unit_t* best = nullptr;
    long long bestDistance = std::numeric_limits<long long>::max();
    for (bwgame::unit_t* other : state.liveUnits()) {
        if (other->owner != medic->owner) continue;
        if (!state.functions().medic_can_heal_target(medic, other)) continue;

        const long long dx = static_cast<long long>(other->sprite->position.x) - medic->sprite->position.x;
        const long long dy = static_cast<long long>(other->sprite->position.y) - medic->sprite->position.y;
        const long long distance = dx * dx + dy * dy;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = other;
        }
    }
    return best;
}

inline void healClosest(GameState& state, bwgame::unit_t* medic) {
    bwgame::unit_t* target = closestHealTarget(state, medic);
    if (!target) return;

    const bool hasHealOrder = medic->order_type &&
                              medic->order_type->id == bwgame::Orders::MedicHeal &&
                              medic->order_target.unit == target;
    if (!hasHealOrder) {
        state.functions().set_unit_order(medic, state.functions().get_order_type(bwgame::Orders::MedicHeal), target);
    }
}

inline double unitTopSpeedPerFrame(const bwgame::unit_t* unit) {
    if (!unit) return 0.0;
    return std::max(0.0, static_cast<double>(unit->flingy_top_speed.raw_value) / 256.0);
}

inline int weaponCooldownRemaining(GameState& state, bwgame::unit_t* unit, bwgame::unit_t* target) {
    bwgame::unit_t* attackingUnit = state.functions().unit_attacking_unit(unit);
    if (!attackingUnit) return 0;
    if (state.functions().u_flying(target)) {
        return attackingUnit->air_weapon_cooldown;
    }
    return attackingUnit->ground_weapon_cooldown;
}

inline double framesToWeaponRange(GameState& state, bwgame::unit_t* unit, bwgame::unit_t* target, const bwgame::weapon_type_t* weapon) {
    const int distance = state.functions().units_distance(state.functions().unit_main_unit(unit), target);
    const int range = state.functions().weapon_max_range(unit, weapon);
    const int distanceToRange = std::max(0, distance - range);
    const double ownSpeed = unitTopSpeedPerFrame(unit);
    const double targetSpeed = state.functions().u_can_move(target) ? unitTopSpeedPerFrame(target) : 0.0;
    const double closingSpeed = std::max(0.25, ownSpeed + targetSpeed);
    return static_cast<double>(distanceToRange) / closingSpeed;
}

inline bwgame::xy kiteMovePosition(GameState& state, bwgame::unit_t* unit, bwgame::unit_t* target, int cooldownRemaining) {
    const bwgame::xy from = target->sprite->position;
    const bwgame::xy to = unit->sprite->position;
    const double dx = static_cast<double>(to.x - from.x);
    const double dy = static_cast<double>(to.y - from.y);
    const double length = std::max(1.0, std::sqrt(dx * dx + dy * dy));
    const double ownSpeed = unitTopSpeedPerFrame(unit);
    const double fleePixels = std::clamp(ownSpeed * std::max(8, cooldownRemaining + 8), 96.0, 320.0);
    return {
        static_cast<int>(std::lround(to.x + dx / length * fleePixels)),
        static_cast<int>(std::lround(to.y + dy / length * fleePixels)),
    };
}

inline bool hasRecentMoveOrder(const bwgame::unit_t* unit, bwgame::xy position) {
    if (!unit->order_type || unit->order_type->id != bwgame::Orders::Move) return false;
    const int dx = unit->order_target.pos.x - position.x;
    const int dy = unit->order_target.pos.y - position.y;
    return dx * dx + dy * dy <= 24 * 24;
}

inline void kiteClosest(GameState& state, int player) {
    if (player < 0 || player >= 12) {
        throw std::runtime_error("Script player id out of range");
    }

    for (bwgame::unit_t* unit : state.liveUnits()) {
        if (unit->owner != player) continue;
        if (unit->owner >= 8) continue;
        if (isMedic(unit)) {
            healClosest(state, unit);
            continue;
        }

        bwgame::unit_t* target = closestAttackableEnemy(state, unit);
        if (!target) continue;

        bwgame::unit_t* attackingUnit = state.functions().unit_attacking_unit(unit);
        if (!attackingUnit || !attackingUnit->unit_type->attack_unit) continue;

        const bwgame::weapon_type_t* weapon = state.functions().unit_target_weapon(unit, target);
        if (!weapon) continue;

        const int cooldownRemaining = weaponCooldownRemaining(state, unit, target);
        const double returnFrames = framesToWeaponRange(state, unit, target, weapon);
        constexpr double turnAroundBufferFrames = 2.0;
        if (cooldownRemaining > returnFrames + turnAroundBufferFrames && state.functions().u_can_move(unit)) {
            const bwgame::xy movePosition = kiteMovePosition(state, unit, target, cooldownRemaining);
            if (!hasRecentMoveOrder(unit, movePosition)) {
                state.functions().set_unit_order(unit, state.functions().get_order_type(bwgame::Orders::Move), movePosition);
            }
            continue;
        }

        const bool hasAttackOrder = attackingUnit->order_type == attackingUnit->unit_type->attack_unit;
        if (!hasAttackOrder || attackingUnit->order_target.unit != target) {
            state.functions().set_unit_order(unit, unit->unit_type->attack_unit, target);
        }
    }
}

inline void kiteClosest(GameState& state) {
    for (int player = 0; player < 8; ++player) {
        kiteClosest(state, player);
    }
}

inline void attackClosest(GameState& state, int player) {
    if (player < 0 || player >= 12) {
        throw std::runtime_error("Script player id out of range");
    }

    for (bwgame::unit_t* unit : state.liveUnits()) {
        if (unit->owner != player) continue;
        if (unit->owner >= 8) continue;
        if (isMedic(unit)) {
            healClosest(state, unit);
            continue;
        }
        if (!unit->unit_type->attack_unit) continue;

        bwgame::unit_t* target = closestEnemy(state, unit);
        if (!target) continue;

        bwgame::unit_t* attackingUnit = state.functions().unit_attacking_unit(unit);
        if (!attackingUnit || !attackingUnit->unit_type->attack_unit) continue;

        const bool hasAttackOrder = attackingUnit->order_type == attackingUnit->unit_type->attack_unit;
        if (!hasAttackOrder || attackingUnit->order_target.unit != target) {
            state.functions().set_unit_order(unit, unit->unit_type->attack_unit, target);
        }
    }
}

inline void attackClosest(GameState& state) {
    for (int player = 0; player < 8; ++player) {
        attackClosest(state, player);
    }
}

inline void run(std::string_view scriptName, GameState& state, int player) {
    const std::string canonical = canonicalName(scriptName);
    if (canonical == "None") return;
    if (canonical == "AttackClosest") {
        attackClosest(state, player);
        return;
    }
    if (canonical == "KiteClosest") {
        kiteClosest(state, player);
        return;
    }
    throw std::runtime_error("Unknown combat script: " + std::string(scriptName));
}

inline void run(std::string_view scriptName, GameState& state) {
    const std::string canonical = canonicalName(scriptName);
    if (canonical == "None") return;
    if (canonical == "AttackClosest") {
        attackClosest(state);
        return;
    }
    if (canonical == "KiteClosest") {
        kiteClosest(state);
        return;
    }
    throw std::runtime_error("Unknown combat script: " + std::string(scriptName));
}

} // namespace scc::Scripts
