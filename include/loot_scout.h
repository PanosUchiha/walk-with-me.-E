#pragma once
#include "loot_scout_policy.h"
#include <vector>

namespace Wayfarer
{
    class TravelNavigation;
    struct RestReachability;
} // namespace Wayfarer

namespace Wayfarer::Loot
{
    struct Target
    {
        RE::ObjectRefHandle ref;
        RE::FormID id{};
        Kind kind{Kind::Container};
        bool valuable{};
        RE::NiPoint3 center{};
        RE::NiPoint3 marker{}; // Where the travel package walks; it stops about 65 units short.
        float reach{};         // Arm's length from the center, where the search animation fits.
    };

    struct Marker
    {
        float x{}, y{}; // Normalized screen coordinates; no game pointers on the render thread.
        bool valuable{};
        float alpha{1};
        std::uint32_t actor{};
        bool pathfinder{}; // The pathfinder emblem rather than the loot chest.
    };

    void Install();

    // Cheap filters only (type, ownership, loaded 3D); called once per scan around the player.
    [[nodiscard]] std::vector<RE::ObjectRefHandle> Collect(RE::PlayerCharacter& player, float radius,
                                                           const Tuning& tuning);
    [[nodiscard]] Kind KindOf(RE::TESObjectREFR& ref);
    [[nodiscard]] RE::NiPoint3 Center(RE::TESObjectREFR& ref);
    // Line of sight from the follower's eyes; only the target's own collision may stop the ray.
    [[nodiscard]] bool Sees(RE::Actor& actor, RE::TESObjectREFR& ref, RE::NiPoint3 center);
    // How far along the line-of-sight ray from `from` to `to` the first obstacle lies (1: none).
    [[nodiscard]] float SightFraction(RE::TESObjectCELL* cell, RE::NiPoint3 from, RE::NiPoint3 to);
    // Plays a one-shot idle (a wave, a point) through the actor's own process.
    bool PlayGesture(RE::Actor& actor, RE::FormID idle);
    // Nothing between the follower's hands and the find, checked once they arrive.
    [[nodiscard]] bool Reaches(RE::Actor& actor, RE::TESObjectREFR& ref, RE::NiPoint3 center);
    // Finds a standing spot beside the find on the follower's connected navmesh (`area`, explored
    // from the follower) with nothing between it and the find.
    [[nodiscard]] Route Measure(const TravelNavigation& navigation, const RestReachability& area,
                                RE::Actor& actor, RE::TESObjectREFR& ref, const Target& plan);
    // Reads the inventory, which resolves an untouched container's leveled loot, so call it only for
    // references the follower can actually see.
    [[nodiscard]] Contents Inspect(RE::TESObjectREFR& ref, Kind kind, int threshold);
    [[nodiscard]] Target Plan(RE::TESObjectREFR& ref, Kind kind, bool valuable);
    // Turns toward the point a little each frame; true once facing it.
    bool Face(RE::Actor& actor, RE::NiPoint3 point, float dt);
    // Starts a looping kneel or rummage that lasts until Stand().
    bool Search(RE::Actor& actor, const Target& target);
    // Plays the stand-up; if the pose is still stuck a few seconds later it is cleared.
    void Stand(RE::Actor& actor);
    [[nodiscard]] bool IdlePlaying(RE::Actor& actor);
    [[nodiscard]] std::vector<RE::FormID> ConsumeActivations();

    void Update(float dt);
    void Reset();
    [[nodiscard]] std::vector<Marker> ReadMarkers();
} // namespace Wayfarer::Loot
