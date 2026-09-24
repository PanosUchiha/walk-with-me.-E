#include "formation_controller.h"

// Keep the old script bridge so saved r12 Hold stacks can restore movement.
// The experimental implementation is excluded from the release build.
namespace Wayfarer
{
    void FormationController::InitializeVictoryEvents()
    {
        logger::info("[Features] Post-victory reactions are shelved in this release");
    }
    void FormationController::UpdateVictoryCelebration(RE::PlayerCharacter&, float) {}
    void FormationController::UpdateVictoryCleanup(float) {}
    void FormationController::ResetVictoryCelebration(bool) {}
    void FormationController::ReportAliasSync(bool) {}
    bool FormationController::VictoryHoldState(RE::Actor*, std::int32_t, std::int32_t)
    {
        return false;
    }
}
