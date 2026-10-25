#pragma once
#include "config.h"

namespace Wayfarer
{
    enum class PartyOrder
    {
        kTravel,
        kScout,
        kRear,
        kRoam
    };
    struct CompanionView
    {
        RE::FormID id{};
        std::string name;
        std::string state;
        std::string personalityKey, personality;
        std::string handHoldKey;
        int personalityOverride{-1};
        float distance{};
        bool managed{}, excluded{}, nff{}, manuallyAdded{}, apiRegistered{}, eligible{}, packageControlled{};
        bool teammate{}; // Following the player, whether or not a roster Walk With Me reads says so.
    };
    struct PackageControlView
    {
        RE::FormID id{};
        std::string name, state;
        bool controlled{}, canTake{}, manualReleaseRequired{};
        bool canPush{};
        std::string pushReason;
    };
    struct PartyView
    {
        SettingsData settings;
        std::vector<CompanionView> party;
        std::vector<PackageControlView> packageTargets;
        RE::FormID crosshairActor{};
        std::string state{"Load a save to see your party"};
        PartyOrder order{PartyOrder::kTravel};
        FormationMode mode{FormationMode::kDynamic};
        std::uint64_t generation{};
        bool inGame{}, gameplay{}, enabled{true}, nffInstalled{};
        int activeCount{};
        int recallCount{};
        std::string recallReason;
        bool recallPending{};
        bool sandboxActive{};
        float sandboxRadius{};
    };
    namespace Menu
    {
        void Register();
        void Publish();
        void Reset();
        bool HandleInput(RE::InputEvent* a_event);
        bool IsInstalled();
        bool HasBlockingWindow();
        RE::FormID CompanionTarget();
    } // namespace Menu
} // namespace Wayfarer
