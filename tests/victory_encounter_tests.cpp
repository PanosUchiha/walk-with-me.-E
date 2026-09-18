#include "victory_encounter.h"
#include "victory_reaction.h"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace Wayfarer::Victory;

int checks{};
void Check(bool value, const char* message)
{
    ++checks;
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    {
        constexpr std::uint64_t queuedAt = 7;
        Check(!HandoffComplete(queuedAt, 7), "cleared alias alone cannot bypass in-flight cleanup");
        Check(HandoffComplete(queuedAt, 8), "first completed cleanup releases the reaction");
        for (std::uint64_t completed : {8U, 8U, 9U, 9U, 10U}) {
            Check(HandoffComplete(queuedAt, completed),
                  "subsequent routine scans do not close the completed handoff");
        }
        Check(!HandoffComplete(10, 10), "next encounter requires a new completed cleanup");
        Check(HandoffComplete(10, 11), "next encounter releases after its own cleanup");
        Check(startWindowSeconds > 8,
              "companions recovering beyond the old timeout retain a playback opportunity");
    }
    {
        Encounter encounter;
        Check(!encounter.active && encounter.quiet == 0, "new encounter starts idle");
        encounter.ObserveEnemy(0);
        encounter.ObserveParticipant(0);
        encounter.MarkDefeated(0);
        Check(encounter.enemies.empty() && encounter.defeated.empty() && encounter.participants.empty(),
              "zero IDs are ignored");
        Check(!encounter.active, "zero enemy does not start an encounter");
        Check(encounter.Tick(10, false) == Result::None && encounter.quiet == 0,
              "idle time does not advance");
        encounter.ObserveParticipant(100);
        encounter.ObserveParticipant(100);
        Check(encounter.participants.size() == 1 && encounter.participants.contains(100),
              "participants are unique");
        Check(!encounter.active && encounter.enemies.empty(),
              "participants alone do not establish enemies or combat");
        Check(encounter.Tick(.5F, true) == Result::None && encounter.active && encounter.quiet == 0,
              "party combat starts an encounter without a known enemy");
        Check(encounter.Tick(10, true) == Result::None && encounter.quiet == 0,
              "ongoing combat never settles");
        Check(encounter.Tick(1.25F, false) == Result::Finished,
              "combat without known enemies is not a victory");
    }

    for (std::uint32_t count : {1U, 2U, 3U, 4U, 5U}) {
        Encounter encounter;
        encounter.ObserveParticipant(100);
        for (std::uint32_t id = 1; id <= count; ++id) {
            encounter.ObserveEnemy(id);
            encounter.MarkDefeated(id);
        }
        Check(encounter.active, "observing enemies starts an encounter");
        Check(encounter.Tick(.5F, false) == Result::None, "confirmed deaths do not fire immediately");
        Check(encounter.Tick(.5F, false) == Result::None, "one second of quiet is insufficient");
        Check(encounter.Tick(.125F, false) == Result::None, "less than 1.25 seconds never fires");
        const Result expected = count >= 3 ? Result::Victory : Result::Finished;
        Check(encounter.Tick(.125F, false) == expected,
              "three defeated enemies are the victory boundary at 1.25 seconds");
        Check(!encounter.active, "result deactivates the encounter before returning");
        Check(encounter.enemies.size() == count && encounter.defeated.size() == count &&
                  encounter.participants.contains(100),
              "result retains encounter data for the caller");
        for (int repeat = 0; repeat < 3; ++repeat) {
            Check(encounter.Tick(2, false) == Result::None && encounter.quiet == 1.25F,
                  "each result fires only once");
        }
        encounter.Reset();
        Check(encounter.enemies.empty() && encounter.defeated.empty() && encounter.participants.empty(),
              "caller reset after a result clears every set");
        Check(!encounter.active && encounter.quiet == 0, "caller reset clears activity and quiet time");
    }

    {
        Encounter encounter;
        for (int transition = 0; transition < 10; ++transition) {
            for (std::uint32_t id = 1; id <= 2; ++id) {
                encounter.ObserveEnemy(id);
                encounter.MarkDefeated(id);
                encounter.MarkDefeated(id);
            }
            Check(encounter.Tick(.25F, true) == Result::None,
                  "repeated combat transitions resume the same encounter");
            Check(encounter.Tick(.5F, false) == Result::None, "short combat gaps do not finish an encounter");
        }
        Check(encounter.enemies.size() == 2 && encounter.defeated.size() == 2,
              "repeated observations and deaths count each enemy once");
        Check(encounter.Tick(.75F, false) == Result::Finished,
              "repeated transitions cannot turn two enemies into a victory");
    }

    {
        Encounter encounter;
        for (std::uint32_t first : {1U, 3U}) {
            for (std::uint32_t id = first; id < first + 2; ++id) {
                encounter.ObserveEnemy(id);
                encounter.MarkDefeated(id);
            }
            Check(encounter.enemies.size() == 2 && encounter.defeated.size() == 2,
                  "separate fights contain only their own enemies");
            Check(encounter.Tick(1.25F, false) == Result::Finished,
                  "two two-enemy fights never combine into a victory");
            encounter.Reset();
        }
    }

    {
        Encounter encounter;
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.ObserveEnemy(id);
            encounter.MarkDefeated(id);
        }
        Check(encounter.Tick(1, false) == Result::None, "quiet countdown starts");
        Check(encounter.Tick(.25F, true) == Result::None && encounter.quiet == 0,
              "resumed combat resets quiet time");
        Check(encounter.enemies.size() == 5 && encounter.defeated.size() == 5,
              "resuming combat preserves the encounter");
        Check(encounter.Tick(1, false) == Result::None, "old quiet time does not cause a premature victory");
        Check(encounter.Tick(.25F, false) == Result::Victory,
              "victory requires a full quiet interval after combat resumes");
    }

    {
        Encounter encounter;
        encounter.MarkDefeated(5);
        encounter.MarkDefeated(99);
        Check(encounter.defeated.empty() && !encounter.active,
              "unknown deaths neither count nor start an encounter");
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.ObserveEnemy(id);
        }
        for (std::uint32_t id = 1; id <= 4; ++id) {
            encounter.MarkDefeated(id);
        }
        encounter.MarkDefeated(99);
        Check(encounter.defeated.size() == 4 && !encounter.defeated.contains(5),
              "a death before observation is not credited later");
        Check(encounter.Tick(1.25F, false) == Result::Finished,
              "an unknown death cannot replace a missing enemy death");
    }

    for (std::uint32_t confirmedDeaths : {0U, 4U}) {
        Encounter encounter;
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.ObserveEnemy(id);
        }
        for (std::uint32_t id = 1; id <= confirmedDeaths; ++id) {
            encounter.MarkDefeated(id);
        }
        Check(encounter.Tick(2, false) == Result::Finished,
              "fleeing or unloaded enemies prevent victory after quiet settles");
        Check(encounter.enemies.size() == 5, "ending combat does not discard unconfirmed enemies");
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.MarkDefeated(id);
        }
        Check(encounter.Tick(2, false) == Result::None && !encounter.active,
              "late deaths cannot fire a second result");
    }

    {
        Encounter encounter;
        encounter.enemies = {1, 2, 3, 4, 5};
        encounter.defeated = {1, 2, 3, 4, 99};
        encounter.active = true;
        Check(encounter.Tick(1.25F, false) == Result::Finished,
              "equal set sizes are insufficient when one enemy is undefeated");
    }

    {
        Encounter encounter;
        encounter.ObserveEnemy(1);
        encounter.MarkDefeated(1);
        encounter.ObserveParticipant(100);
        Check(encounter.Tick(.5F, false) == Result::None, "active encounter can be reset during quiet time");
        encounter.Reset();
        encounter.Reset();
        Check(encounter.enemies.empty() && encounter.defeated.empty() && encounter.participants.empty(),
              "reset is idempotent");
        Check(!encounter.active && encounter.quiet == 0 && encounter.Tick(2, false) == Result::None,
              "reset cancels a pending result");
        encounter.ObserveEnemy(1);
        Check(encounter.defeated.empty(), "reusing an enemy ID after reset requires a new confirmed death");
        Check(encounter.Tick(1, false) == Result::None, "a new encounter receives its full quiet interval");
    }

    {
        const float invalidTimes[] = {0, -.25F, std::numeric_limits<float>::quiet_NaN(),
                                      std::numeric_limits<float>::infinity(),
                                      -std::numeric_limits<float>::infinity()};
        Encounter encounter;
        for (float dt : invalidTimes) {
            for (bool fighting : {false, true}) {
                Check(encounter.Tick(dt, fighting) == Result::None, "invalid time produces no idle result");
                Check(!encounter.active && encounter.quiet == 0,
                      "invalid time cannot start combat or advance an idle timer");
            }
        }
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.ObserveEnemy(id);
            encounter.MarkDefeated(id);
        }
        encounter.ObserveParticipant(100);
        Check(encounter.Tick(1, false) == Result::None, "valid time advances the quiet timer");
        for (float dt : invalidTimes) {
            for (bool fighting : {false, true}) {
                Check(encounter.Tick(dt, fighting) == Result::None,
                      "invalid time cannot finish an active encounter");
                Check(encounter.active && encounter.quiet == 1,
                      "invalid time neither advances nor resets quiet time");
                Check(encounter.enemies.size() == 5 && encounter.defeated.size() == 5 &&
                          encounter.participants.contains(100),
                      "invalid time preserves encounter sets");
            }
        }
        Check(encounter.Tick(.25F, false) == Result::Victory, "valid time resumes after invalid updates");
    }

    {
        Encounter encounter;
        for (std::uint32_t id = 1; id <= 5; ++id) {
            encounter.ObserveEnemy(id);
            encounter.MarkDefeated(id);
        }
        Check(encounter.Tick(1, false) == Result::None, "quiet begins after the first enemies die");

        encounter.ObserveEnemy(6);
        encounter.MarkDefeated(6);
        Check(encounter.quiet == 0, "a newly observed opponent restarts settling even between updates");
        Check(encounter.Tick(.25F, false) == Result::None,
              "a short intervening skirmish prevents early celebration");
        Check(encounter.Tick(1, false) == Result::Victory, "all six enemies require a fresh quiet interval");
    }

    {
        for (std::uint32_t variant = 0; variant < 12; ++variant) {
            const auto empty = ChooseIdle(Equipment::EmptyHands, variant);
            Check(empty.form && !empty.armed, "empty hands always get an unarmed applause idle");
            for (const auto gear : {Equipment::Sword, Equipment::Greatsword}) {
                const auto armed = ChooseIdle(gear, variant);
                Check(armed.form == 0xF7C8C && armed.armed,
                      "drawn swords always cheer, never clap with a blade");
            }
            Check(!ChooseIdle(Equipment::Unsupported, variant).form,
                  "unsupported drawn equipment waits rather than using applause");
            Check(!ChooseIdle(Equipment::Transition, variant).form,
                  "draw/sheath transitions wait for stable equipment");
        }
        Check(ChooseIdle(Equipment::EmptyHands, 0).form == 0xD8731,
              "the existing tavern applause event is in the pool");
        Check(ChooseIdle(Equipment::EmptyHands, 1).form != ChooseIdle(Equipment::EmptyHands, 0).form,
              "unarmed reactions vary");
        Check(startWindowSeconds > reactionSeconds && retrySeconds > 0,
              "reaction retries are bounded by a finite start window");
    }
    {
        unsigned shoulders = 0;
        for (std::uint32_t variant = 0; variant < 100; ++variant) {
            const auto sheathed = ChooseIdle(Equipment::Sheathed, variant, true);
            shoulders += sheathed.shoulder ? 1U : 0U;
            Check(sheathed.form && !sheathed.armed, "fully sheathed companions select an unarmed reaction");
            Check(sheathed.seconds == (sheathed.shoulder ? 6.5F : 4.9F),
                  "shoulder recovery gets its full duration without extending applause");
            const auto fallback = ChooseIdle(Equipment::Sheathed, variant, false);
            Check(fallback.form && !fallback.shoulder,
                  "missing OAR or authored assets preserves ordinary applause");
            for (const auto gear : {Equipment::EmptyHands, Equipment::Sword, Equipment::Greatsword,
                                    Equipment::Unsupported, Equipment::Transition}) {
                Check(
                    !ChooseIdle(gear, variant, true).shoulder,
                    "shoulder recovery never starts while drawn, drawing or sheathing, including bare fists");
            }
        }
        Check(shoulders == 25, "shoulder recovery is one of four equal sheathed choices");
        for (const auto gear : {Equipment::EmptyHands, Equipment::Sword, Equipment::Greatsword,
                                Equipment::Unsupported, Equipment::Transition}) {
            Check(!ChooseIdle(gear, 3, true).form, "a selected shoulder reaction waits instead of being "
                                                   "consumed by an armed cheer before sheathing");
        }
        Check(ChooseIdle(Equipment::Sheathed, 3, true).shoulder,
              "the same pending selection plays after sheathing completes");
        ShoulderSelections selections;
        selections.Set(0, true);
        Check(!selections.Contains(0), "invalid actors cannot own a shoulder replacement");
        selections.Set(100, true);
        selections.Set(200, true);
        Check(selections.Contains(100) && selections.Contains(200) && !selections.Contains(300),
              "OAR selection is scoped to individual reacting companions");
        selections.Set(100, false);
        Check(!selections.Contains(100) && selections.Contains(200),
              "ending one reaction leaves another companion's reaction intact");
        selections.Set(200, false);
        selections.Set(200, false);
        Check(!selections.Contains(200), "failed starts and repeated cleanup release selection safely");
        selections.Set(100, true);
        selections.Set(200, true);
        selections.Clear();
        Check(!selections.Contains(100) && !selections.Contains(200),
              "combat cancellation and game load clear all transient selections");
    }
    std::cout << checks << " victory encounter and reaction checks passed\n";
}
