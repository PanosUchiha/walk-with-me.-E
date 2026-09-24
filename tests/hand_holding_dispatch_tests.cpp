#include "hand_holding_backends.h"
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

using namespace Wayfarer::HandHolding;
namespace
{
    std::vector<std::string_view> calls;
    bool pending{};
    void Check(bool ok)
    {
        if (!ok) {
            std::cerr << "Hand-holding dispatcher regression\n";
            std::exit(1);
        }
    }
} // namespace
namespace Wayfarer::HandHolding::Classic
{
    void Install() {}
    void Reset()
    {
        calls.push_back("classic reset");
    }
    bool BeginFrame(float, bool permitted, bool paused, const Tuning& tuning)
    {
        Check(!pending);
        calls.push_back("classic frame");
        return permitted && !paused && tuning.enabled;
    }
    void UpdatePartner(RE::PlayerCharacter&, RE::Actor*, float, const TravelNavigation&) {}
    Approach GetApproach()
    {
        return {1};
    }
    bool IsAttached(RE::FormID actor)
    {
        return actor == 1;
    }
    void ReleaseActor(RE::FormID) {}
} // namespace Wayfarer::HandHolding::Classic
namespace Wayfarer::HandHolding::Tether
{
    std::string ReferenceKey(RE::Actor&)
    {
        return "companion";
    }
    void Install() {}
    void Reset()
    {
        calls.push_back("tether reset");
    }
    void OnGameLoaded()
    {
        pending = false;
        calls.push_back("tether loaded");
    }
    bool CleanupPending()
    {
        return pending;
    }
    bool BeginFrame(float, bool permitted, bool paused, const Tuning& tuning)
    {
        calls.push_back(permitted ? "tether frame" : "tether cleanup");
        if (!permitted) {
            Check(!tuning.enabled);
        }
        return permitted && !paused && tuning.enabled && !pending;
    }
    void UpdatePartner(RE::PlayerCharacter&, RE::Actor*, float, const TravelNavigation&) {}
    Approach GetApproach()
    {
        return {2};
    }
    bool IsAttached(RE::FormID actor)
    {
        return actor == 2;
    }
    void ReleaseActor(RE::FormID) {}
    bool OwnsMovement(RE::FormID actor)
    {
        return pending && actor == 2;
    }
    std::int32_t HandoffTicket(RE::FormID)
    {
        return 7;
    }
    bool HandoffCleanupFinished(RE::FormID, std::int32_t)
    {
        return !pending;
    }
    void ConfirmHandoff(RE::FormID, std::int32_t, bool, bool) {}
    std::int32_t TetherStepStatus(std::int32_t ticket, std::int32_t, RE::Actor*, RE::Actor*)
    {
        return ticket;
    }
    float SpeedMultiplier(RE::Actor*)
    {
        return 1;
    }
} // namespace Wayfarer::HandHolding::Tether

int main()
{
    Tuning tuning;
    tuning.enabled = true;
    Check(tuning.method == Method::Classic);
    Check(BeginFrame(.016F, true, false, tuning));
    Check(GetApproach().actor == 1);
    tuning.method = Method::Tether;
    Check(BeginFrame(.016F, true, false, tuning));
    Check(GetApproach().actor == 2);

    // An outstanding or failed cleanup must keep Classic entirely idle, including
    // across multiple frames. Papyrus endpoints retain the TETHER session owner.
    pending = true;
    tuning.method = Method::Classic;
    calls.clear();
    Check(!BeginFrame(.016F, true, false, tuning));
    Check(!BeginFrame(.016F, true, false, tuning));
    Check(calls == std::vector<std::string_view>{"tether cleanup", "tether cleanup"});
    Check(OwnsMovement(2));
    Check(TetherStepStatus(7, 3, nullptr, nullptr) == 7);

    // Returning to TETHER during teardown must not accidentally start Classic.
    tuning.method = Method::Tether;
    Check(!BeginFrame(.016F, true, false, tuning));
    tuning.method = Method::Classic;
    pending = false;
    calls.clear();
    Check(BeginFrame(.016F, true, false, tuning));
    Check(calls == std::vector<std::string_view>{"tether cleanup", "classic frame"});
    Check(GetApproach().actor == 1 && IsAttached(1) && !OwnsMovement(1));
    Check(!BeginFrame(.016F, true, true, tuning));
    tuning.enabled = false;
    Check(!BeginFrame(.016F, true, false, tuning));

    // In the other direction, Classic's pose/collision leases release first.
    tuning.enabled = true;
    tuning.method = Method::Tether;
    calls.clear();
    Check(BeginFrame(.016F, true, false, tuning));
    Check(calls == std::vector<std::string_view>{"classic reset", "tether frame"});
    Check(GetApproach().actor == 2);
    pending = true;
    OnGameLoaded();
    Check(!pending);
    tuning.method = Method::Classic;
    Check(BeginFrame(.016F, true, false, tuning));
    calls.clear();
    Reset();
    Check(calls == std::vector<std::string_view>{"classic reset", "tether reset"});
    std::cout << "Hand-holding method transitions passed\n";
}
