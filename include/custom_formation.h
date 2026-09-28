#pragma once
#include "formation_math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// A formation the player draws in the formation editor: up to PARTY_CAPACITY places around them,
// in game units (lateral: + right, longitudinal: + ahead of the direction of travel), each
// optionally reserved for one companion. Pure data and rules, shared by the controller, the
// editor and the tests.
namespace Wayfarer::Custom
{
    constexpr float reach = 560.0F;    // How far from the player a place may be (8 m).
    constexpr float clearance = 75.0F; // Nobody stands on the player's own feet.
    constexpr float spacing = 55.0F;   // Two companions cannot stand closer than this.
    constexpr float step = 25.0F;      // Snap-to-grid step.
    constexpr float unitsPerMetre = 70.0F;

    // Emblems a player may give their formation: Game-icons.net art that appears nowhere else in
    // the mod, so the Custom order never looks like another. Each ships as
    // Interface/WalkWithMe/crest-<key>.svg; attribution is in ThirdParty/GameIcons-NOTICE.txt.
    struct Emblem
    {
        const char* key;   // Stored in the INI; also the file name.
        const char* label; // Shown in the picker.
    };
    inline constexpr std::array<Emblem, 16> emblems{{{"band", "Band"},     {"banner", "Banner"}, {"crown", "Crown"},
                                                     {"castle", "Castle"}, {"swords", "Swords"}, {"dragon", "Dragon"},
                                                     {"wolf", "Wolf"},     {"raven", "Raven"},   {"stag", "Stag"},
                                                     {"steed", "Steed"},   {"pine", "Pine"},     {"rune", "Rune"},
                                                     {"watch", "Watch"},   {"moon", "Moon"},     {"calm", "Calm"},
                                                     {"tavern", "Tavern"}}};
    inline constexpr int emblemCount = static_cast<int>(emblems.size());
    // Three friends: plainly "my party".
    inline constexpr int defaultEmblem = 0;
    inline int EmblemFromKey(std::string_view key)
    {
        for (int i = 0; i < emblemCount; ++i) {
            if (key == emblems[i].key) {
                return i;
            }
        }
        return defaultEmblem;
    }
    inline constexpr std::size_t maxTitle = 24;
    // A readable name: trimmed, at most 24 characters, never empty.
    inline std::string CleanTitle(std::string title)
    {
        const auto first = title.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return "Custom";
        }
        title = title.substr(first, title.find_last_not_of(" \t\r\n") - first + 1);
        if (title.size() > maxTitle) {
            title.resize(maxTitle);
            while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0) == 0x80) {
                title.pop_back(); // Never cut a multi-byte character in half.
            }
            if (!title.empty() && static_cast<unsigned char>(title.back()) >= 0xC0) {
                title.pop_back();
            }
        }
        return title;
    }

    struct Formation
    {
        int count{};
        std::array<FormationSlot, PARTY_CAPACITY> slots{};
        std::array<std::string, PARTY_CAPACITY> companions{}; // Reference keys; empty = anyone.
        std::array<std::string, PARTY_CAPACITY> names{};      // Display names for the editor.
        std::string title{"Custom"};                          // Its name on the wheel and the HUD.
        int emblem{defaultEmblem};                            // Index into `emblems`.
        [[nodiscard]] bool Defined() const { return count > 0; }
        bool operator==(const Formation&) const = default;
    };

    // Keep a place inside the editor's reach and off the player; optionally on the grid.
    inline FormationSlot Constrain(FormationSlot s, bool snap)
    {
        if (!std::isfinite(s.lateral) || !std::isfinite(s.longitudinal)) {
            return {0, -clearance * 2};
        }
        if (snap) {
            s.lateral = std::round(s.lateral / step) * step;
            s.longitudinal = std::round(s.longitudinal / step) * step;
        }
        float d = std::hypot(s.lateral, s.longitudinal);
        if (d > reach) {
            s.lateral *= reach / d;
            s.longitudinal *= reach / d;
        }
        d = std::hypot(s.lateral, s.longitudinal);
        if (d < clearance) {
            // Push straight out; a place dropped exactly on the player goes behind them.
            const float x = d > .01F ? s.lateral / d : 0, y = d > .01F ? s.longitudinal / d : -1;
            s = {x * clearance, y * clearance};
        }
        return s;
    }

    // Whether a place at `s` would crowd one of the others (ignoring index `except`).
    inline bool Crowded(const Formation& f, FormationSlot s, int except = -1)
    {
        for (int i = 0; i < std::clamp(f.count, 0, PARTY_CAPACITY); ++i) {
            if (i != except && std::hypot(f.slots[i].lateral - s.lateral, f.slots[i].longitudinal - s.longitudinal) <
                                   spacing) {
                return true;
            }
        }
        return false;
    }

    inline bool Add(Formation& f, FormationSlot s, bool snap)
    {
        s = Constrain(s, snap);
        if (f.count >= PARTY_CAPACITY || Crowded(f, s)) {
            return false;
        }
        f.slots[f.count] = s;
        f.companions[f.count].clear();
        f.names[f.count].clear();
        ++f.count;
        return true;
    }

    // Later places move up one number, so the numbering never has gaps.
    inline void Remove(Formation& f, int index)
    {
        if (index < 0 || index >= f.count) {
            return;
        }
        for (int i = index; i + 1 < f.count; ++i) {
            f.slots[i] = f.slots[i + 1];
            f.companions[i] = std::move(f.companions[i + 1]);
            f.names[i] = std::move(f.names[i + 1]);
        }
        --f.count;
        f.slots[f.count] = {};
        f.companions[f.count].clear();
        f.names[f.count].clear();
    }

    // Left becomes right; the line of travel stays where it is.
    inline void Flip(Formation& f)
    {
        for (int i = 0; i < f.count; ++i) {
            f.slots[i].lateral = -f.slots[i].lateral;
        }
    }

    // A copy of place `index` on the other side of the line of travel, if there is room.
    inline bool MirrorOne(Formation& f, int index, bool snap)
    {
        if (index < 0 || index >= f.count || std::abs(f.slots[index].lateral) < spacing * .5F) {
            return false;
        }
        return Add(f, {-f.slots[index].lateral, f.slots[index].longitudinal}, snap);
    }

    // A reserved companion keeps one place only: reserving them again frees their old place.
    inline void Reserve(Formation& f, int index, const std::string& key, const std::string& name)
    {
        if (index < 0 || index >= f.count) {
            return;
        }
        if (!key.empty()) {
            for (int i = 0; i < f.count; ++i) {
                if (f.companions[i] == key) {
                    f.companions[i].clear();
                    f.names[i].clear();
                }
            }
        }
        f.companions[index] = key;
        f.names[index] = key.empty() ? std::string{} : name;
    }

    // Loaded or saved data made safe: count in range, every place finite, in reach and apart.
    inline Formation Sanitize(Formation f)
    {
        Formation clean;
        clean.title = CleanTitle(f.title);
        clean.emblem = f.emblem >= 0 && f.emblem < emblemCount ? f.emblem : defaultEmblem;
        for (int i = 0; i < std::clamp(f.count, 0, PARTY_CAPACITY); ++i) {
            const auto s = Constrain(f.slots[i], false);
            if (!Crowded(clean, s)) {
                clean.slots[clean.count] = s;
                clean.companions[clean.count] = f.companions[i];
                clean.names[clean.count] = f.names[i];
                ++clean.count;
            }
        }
        return clean;
    }

    // `base` with the places (and who stands in them) taken from `places`: the editor saves places,
    // the settings page owns the name and icon, and neither overwrites the other.
    inline Formation WithPlaces(Formation base, const Formation& places)
    {
        base.count = places.count;
        base.slots = places.slots;
        base.companions = places.companions;
        base.names = places.names;
        return base;
    }

    // Where role `role` stands. Past the last drawn place, extra companions fall in behind the
    // rearmost one, alternating sides, so a small formation never leaves anyone without a place.
    inline FormationSlot RolePosition(const Formation& f, int role)
    {
        role = std::max(0, role);
        if (role < f.count) {
            return f.slots[role];
        }
        float rear = -clearance;
        for (int i = 0; i < f.count; ++i) {
            rear = std::min(rear, f.slots[i].longitudinal);
        }
        const int extra = role - f.count;
        const float side = (extra % 2 ? 1.0F : -1.0F) * (extra >= 2 ? 150.0F : 70.0F);
        return {side, rear - 110.0F * (1 + extra / 2)};
    }

    // Which role each companion in `keys` must take: its reserved place, or -1 when it may take
    // any unreserved one. Reserved places that nobody present claims stay free for others.
    inline std::vector<int> ReservedRoles(const Formation& f, std::span<const std::string> keys)
    {
        std::vector<int> roles(keys.size(), -1);
        for (std::size_t k = 0; k < keys.size(); ++k) {
            for (int i = 0; i < f.count && !keys[k].empty(); ++i) {
                if (f.companions[i] == keys[k]) {
                    roles[k] = i;
                }
            }
        }
        return roles;
    }

    // The roles the `need` companions without a reserved place should fill, in the player's
    // numbering: free places first, then places kept for companions who are not here, and only
    // then the fall-in places behind.
    inline std::vector<int> OpenRoles(const Formation& f, std::span<const int> reserved, int need)
    {
        std::vector<int> open;
        auto take = [&](int role) {
            if (static_cast<int>(open.size()) < need && std::find(open.begin(), open.end(), role) == open.end() &&
                std::find(reserved.begin(), reserved.end(), role) == reserved.end()) {
                open.push_back(role);
            }
        };
        for (int role = 0; role < f.count; ++role) {
            if (f.companions[role].empty()) {
                take(role);
            }
        }
        for (int role = 0; role < f.count; ++role) {
            take(role);
        }
        for (int role = f.count; static_cast<int>(open.size()) < need; ++role) {
            take(role);
        }
        return open;
    }
} // namespace Wayfarer::Custom
