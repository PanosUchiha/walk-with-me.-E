#include "engine_compatibility.h"
#include "follower_diagnostic_log.h"
#include "formation_controller.h"
#include "hand_holding.h"

namespace Wayfarer
{
    void FormationController::TraceFollowerEvent(ManagedFollower& f, const char* reason, bool incident,
                                                 bool finish)
    {
        if (!Diagnostics::log) {
            return;
        }
        const auto actor = f.handle.get();
        const auto id = actor ? actor->GetFormID() : 0;
        const double now = GetTickCount64() / 1000.0;
        auto emit = [&](unsigned sequence, std::string_view kind, double at, std::string_view text) {
            Diagnostics::log->info("[FollowerIncident] actor={:08X} name=\"{}\" generation={} incident={} "
                                   "kind={} mono={:.3f} retries={} {}",
                                   id, actor ? actor->GetName() : "unloaded", generation, sequence, kind, at,
                                   f.trace.retries, text);
        };
        if (incident) {
            f.trace.Begin(now, reason, emit);
            ++f.trace.retries;
        }
        f.trace.Event(now, reason, emit);
        if (finish) {
            f.trace.End(now, reason, emit);
        }
        if (incident || finish) {
            Diagnostics::log->flush();
        }
    }

    void FormationController::TraceFollower(ManagedFollower& f, RE::Actor& actor, RE::PlayerCharacter& player,
                                            float dt)
    {
        if (!Diagnostics::log) {
            return;
        }
        const double now = GetTickCount64() / 1000.0;
        const auto id = actor.GetFormID();
        const auto pos = actor.GetPosition(), playerPos = player.GetPosition();
        RE::NiPoint3 velocity{};
        actor.GetLinearVelocity(velocity);
        auto* current = actor.GetCurrentPackage();
        auto* data = RE::TESDataHandler::GetSingleton();
        auto* expected = data ? data->LookupForm<RE::TESPackage>(PackageLocal(f.slot),
                                                                 Settings::GetSingleton().Get().pluginName)
                              : nullptr;
        auto* alias = GetRefAlias(questCache, FollowerAlias(f.slot));
        auto* ref = alias ? alias->GetReference() : nullptr;
        auto* markerAlias = GetRefAlias(questCache, MarkerAlias(f.slot));
        auto* marker = markerAlias ? markerAlias->GetReference() : nullptr;
        const auto markerPosition = marker ? marker->GetPosition() : RE::NiPoint3{};
        auto* cell = actor.GetParentCell();
        auto* playerCell = player.GetParentCell();
        auto* world = actor.GetWorldspace();
        auto* scene = actor.GetCurrentScene();
        const auto type = current ? static_cast<int>(current->packData.packType.get()) : -1;
        const auto procedure = current ? static_cast<int>(current->procedureType.get()) : -1;
        const bool blocked =
            type == 36 || procedure == 44 ||
            actor.GetActorRuntimeData().boolFlags.any(RE::Actor::BOOL_FLAGS::kMovementBlocked);
        const bool loaded = actor.Is3DLoaded();
        const bool travel = f.travelActive && !f.sandboxActive;
        const float dx = pos.x - f.tracePosition.x, dy = pos.y - f.tracePosition.y;
        const float step = f.traceHavePosition ? std::hypot(dx, dy) : 0;
        const float gx = f.traceGoal.x - f.tracePosition.x, gy = f.traceGoal.y - f.tracePosition.y;
        const float priorDistance = std::hypot(gx, gy);
        const float forward =
            f.traceHavePosition && priorDistance > 1 ? (dx * gx + dy * gy) / priorDistance : 0;
        const float distance = std::hypot(f.goal.goal.x - pos.x, f.goal.goal.y - pos.y);
        auto* process = actor.GetActorRuntimeData().currentProcess;
        auto* high = process ? process->high : nullptr;
        RE::NiPoint3 desired{}, engineVelocity{};
        if (high) {
            desired = high->pathingDesiredPosition;
            engineVelocity = high->pathingDesiredMovementSpeed;
        }
        auto emit = [&](unsigned sequence, std::string_view kind, double at, std::string_view text) {
            Diagnostics::log->info("[FollowerIncident] actor={:08X} name=\"{}\" generation={} incident={} "
                                   "kind={} mono={:.3f} retries={} {}",
                                   id, actor.GetName(), generation, sequence, kind, at, f.trace.retries,
                                   text);
        };
        const auto signature =
            fmt::format("package={:08X} type={} procedure={} source={} alias={:08X} cell={:08X} scene={:08X} "
                        "loaded={} travel={} sandbox={}",
                        current ? current->GetFormID() : 0, type, procedure,
                        current && current->GetFile(0) ? current->GetFile(0)->GetFilename() : "runtime",
                        ref ? ref->GetFormID() : 0, cell ? cell->GetFormID() : 0,
                        scene ? scene->GetFormID() : 0, loaded, travel, f.sandboxActive);
        if (signature != f.traceSignature) {
            f.trace.Event(now, "transition " + signature, emit);
            f.traceSignature = signature;
        }
        auto text = fmt::format(
            "dt={:.3f} pos=({:.1f},{:.1f},{:.1f}) player=({:.1f},{:.1f},{:.1f}) playerCell={:08X} "
            "world={:08X} velocity=({:.1f},{:.1f},{:.1f}) step={:.2f} forwardStep={:.2f} "
            "goal=({:.1f},{:.1f}) submitted=({:.1f},{:.1f}) distance={:.1f} goalActive={} hasRoute={} "
            "expected={:08X} owns={} blocked={} stalled={:.2f} recovery={:.2f} releasePending={} "
            "routeAge={:.2f} targetAge={:.2f} slot={} role={} pace={} scale={:.2f} high={} "
            "engineDesired=({:.1f},{:.1f},{:.1f}) engineVelocity=({:.1f},{:.1f},{:.1f}) bump={} "
            "avoidWait={:.2f} scene={:08X} furniture={} combat={} dead={} handAttached={} {}",
            dt, pos.x, pos.y, pos.z, playerPos.x, playerPos.y, playerPos.z,
            playerCell ? playerCell->GetFormID() : 0, world ? world->GetFormID() : 0, velocity.x, velocity.y,
            velocity.z, step, forward, f.goal.goal.x, f.goal.goal.y, f.goal.submittedGoal.x,
            f.goal.submittedGoal.y, distance, f.goal.active, f.goal.hasRoute,
            expected ? expected->GetFormID() : 0, current && current == expected, blocked, f.goal.stalledTime,
            f.goal.recoveryTime, f.recoveryReleasePending, f.goal.sinceRepath, f.goal.sinceCommit, f.slot,
            f.role, static_cast<int>(f.goal.pace), f.speedScale, high != nullptr, desired.x, desired.y,
            desired.z, engineVelocity.x, engineVelocity.y, engineVelocity.z,
            high ? static_cast<int>(high->bumpedState) : -1,
#ifdef WAYFARER_MODERN_COMMONLIB
            high ? high->avoidWaitTimer : 0,
#else
            -1.0F, // The legacy headers do not expose this diagnostic field.
#endif
            scene ? scene->GetFormID() : 0, bool(actor.GetOccupiedFurniture().get()), actor.IsInCombat(),
            actor.IsDead(), HandHolding::IsAttached(id), signature);
        bool animationDriven = false;
        float graphSpeed = 0;
        const bool graphDrivenKnown =
            loaded && actor.GetGraphVariableBool("bAnimationDriven", animationDriven);
        const bool graphSpeedKnown = loaded && actor.GetGraphVariableFloat("Speed", graphSpeed);
        text += fmt::format(" aiEnabled={} animationDriven={}/{} graphSpeed={:.1f}/{} aliasSyncBusy={} "
                            "mode={} playerSpeed={:.1f} actorFlags={:08X}",
                            actor.IsAIEnabled(), animationDriven, graphDrivenKnown, graphSpeed,
                            graphSpeedKnown, aliasSyncBusy, static_cast<int>(mode), intent.Speed(),
                            actor.GetActorRuntimeData().boolFlags.underlying());
        text += fmt::format(" movementAllowed={} ownershipYield={} displacedBy={:08X}",
                            movementGates[f.slot] && movementGates[f.slot]->value != 0, f.handoff.yielded,
                            f.handoff.displacedBy);
        auto* ui = RE::UI::GetSingleton();
        const bool contextValid = enabled && mode != FormationMode::kVanilla && loaded && !actor.IsDead() &&
                                  !actor.IsInCombat() &&
                                  (!ui || !ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME));
        const bool wasActive = f.trace.active;
        f.trace.Sample({now, dt, step, forward, travel, blocked,
                        f.goal.active && distance > Settings::GetSingleton().Get().travel.arrivalRadius + 70,
                        f.goal.active && distance <= Settings::GetSingleton().Get().travel.arrivalRadius,
                        contextValid},
                       text, emit);
        if (now - f.traceHeartbeatAt >= 30.0) {
            f.traceHeartbeatAt = now;
            emit(f.trace.sequence, "heartbeat", now, text);
        }
        // Nearby actors are candidates, not proven blockers. Include non-followers.
        if (f.trace.active && now - f.traceNeighborsAt >= 1.0) {
            f.traceNeighborsAt = now;
            unsigned neighbors = 0;
            if (auto* lists = RE::ProcessLists::GetSingleton()) {
                Engine::ForEachHighActor(*lists, [&](RE::Actor& neighbor) {
                    if (neighbors >= 8 || neighbor.GetFormID() == id || !neighbor.Is3DLoaded()) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    if (neighbor.GetParentCell() != cell && (!world || neighbor.GetWorldspace() != world)) {
                        return RE::BSContainer::ForEachResult::kContinue;
                    }
                    const auto p = neighbor.GetPosition();
                    const float gap = std::hypot(p.x - pos.x, p.y - pos.y);
                    if (gap < 350 && std::abs(p.z - pos.z) < 150) {
                        ++neighbors;
                        emit(f.trace.sequence, "nearby", now,
                             fmt::format("actor={:08X} name=\"{}\" managed={} gap={:.1f} dz={:.1f} "
                                         "pos=({:.1f},{:.1f},{:.1f})",
                                         neighbor.GetFormID(), neighbor.GetName(),
                                         managed.contains(neighbor.GetFormID()), gap, p.z - pos.z, p.x, p.y,
                                         p.z));
                    }
                    return RE::BSContainer::ForEachResult::kContinue;
                });
            }
            emit(f.trace.sequence, "marker", now,
                 fmt::format("ref={:08X} cell={:08X} pos=({:.1f},{:.1f},{:.1f}) playerGap={:.1f} "
                             "nearbyCount={} nearbyCap=8",
                             marker ? marker->GetFormID() : 0,
                             marker && marker->GetParentCell() ? marker->GetParentCell()->GetFormID() : 0,
                             markerPosition.x, markerPosition.y, markerPosition.z,
                             std::hypot(playerPos.x - pos.x, playerPos.y - pos.y), neighbors));
            Diagnostics::log->flush();
        }
        if (f.trace.active && contextValid && now - f.traceProbeAt >= 5.0 && f.goal.active && distance > 1) {
            f.traceProbeAt = now;
            // Use a separate avoidance state: these probes cannot alter the live route.
            ObstacleAvoidance observationOnly;
            const float reach = std::min(160.0F, distance);
            const RE::NiPoint3 probeGoal{pos.x + (f.goal.goal.x - pos.x) * reach / distance,
                                         pos.y + (f.goal.goal.y - pos.y) * reach / distance, pos.z};
            const auto probe = navigation.AvoidObstacles(actor, probeGoal, 100, .15F, 40, observationOnly);
            auto* blocker = probe.blocker ? RE::TESForm::LookupByID(probe.blocker) : nullptr;
            emit(f.trace.sequence, "probe", now,
                 fmt::format("observationOnly=true span={:.1f} route={} blocker={:08X} blockerSource={} "
                             "rays={} navRejects={} queryFailed={} (static-probe-excludes-actors)",
                             reach, static_cast<int>(probe.route), probe.blocker,
                             blocker && blocker->GetFile(0) ? blocker->GetFile(0)->GetFilename()
                                                            : "none/runtime",
                             probe.rays, probe.navRejects, probe.queryFailed));
        }
        if (wasActive != f.trace.active) {
            Diagnostics::log->flush();
        }
        f.tracePosition = pos;
        f.traceGoal = f.goal.goal;
        f.traceHavePosition = true;
    }
} // namespace Wayfarer
