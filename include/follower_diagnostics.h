#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace Wayfarer::Diagnostics
{
    // Observation only: this state never changes AI, routes, aliases or animation.
    struct Observation
    {
        double time{};
        float dt{}, step{}, forwardStep{};
        bool travel{}, blocked{}, distant{}, arrived{}, contextValid{true};
    };
    class Recorder
    {
      public:
        struct Entry
        {
            double time;
            std::string text;
        };
        std::deque<Entry> history;
        std::uint32_t sequence{}, retries{};
        bool active{};
        double started{}, lastOutput{}, lastSample{}, lastEventOutput{};
        float noProgress{}, blockedTime{}, progressTime{}, arrivedTime{}, inactiveTime{};

        template <class Emit> void Event(double time, std::string text, Emit&& emit)
        {
            Remember(time, text);
            if (active &&
                (time - started <= 10.0 || time - lastEventOutput >= 1.0 || text.starts_with("recovery") ||
                 text.starts_with("evaluate-package") || text.starts_with("transition"))) {
                emit(sequence, "event", time, text);
                lastEventOutput = time;
            }
        }
        template <class Emit> void Begin(double time, std::string_view reason, Emit&& emit)
        {
            if (active) {
                return;
            }
            active = true;
            ++sequence;
            retries = 0;
            started = lastOutput = time;
            progressTime = arrivedTime = inactiveTime = 0;
            emit(sequence, "begin", time, reason);
            for (const auto& entry : history) {
                emit(sequence, "history", entry.time, entry.text);
            }
        }
        template <class Emit> void End(double time, std::string_view outcome, Emit&& emit)
        {
            if (!active) {
                return;
            }
            emit(sequence, "end", time, outcome);
            active = false;
            noProgress = blockedTime = progressTime = arrivedTime = inactiveTime = 0;
        }
        template <class Emit> void Sample(const Observation& o, std::string text, Emit&& emit)
        {
            const float dt = std::clamp(o.dt, 0.0F, 0.5F);
            if (lastSample > 0 && o.time - lastSample > 2.0) {
                Event(o.time, "observation-gap; continuity reset (pause/dialogue/loading possible)", emit);
                progressTime = arrivedTime = noProgress = blockedTime = 0;
            }
            lastSample = o.time;
            Remember(o.time, text);
            noProgress = o.contextValid && o.travel && o.distant && o.step < 8.0F * dt ? noProgress + dt : 0;
            blockedTime = o.contextValid && o.blocked ? blockedTime + dt : 0;
            const bool wasActive = active;
            if (o.contextValid && ((o.travel && o.blocked) || blockedTime >= 4.5F || noProgress >= 3.0F)) {
                Begin(o.time, o.blocked ? "movement-blocked" : "no-progress", emit);
            }
            if (!active) {
                return;
            }
            // Motion must advance toward the *previous* target, independently of
            // moving markers. Ownership alone and sideways jitter aren't recovery.
            progressTime = o.contextValid && !o.blocked && o.forwardStep > 8.0F * dt ? progressTime + dt : 0;
            arrivedTime = o.contextValid && o.travel && !o.blocked && o.arrived ? arrivedTime + dt : 0;
            inactiveTime = !o.contextValid || (!o.travel && !o.blocked) ? inactiveTime + dt : 0;
            if (wasActive && (o.time - started <= 10.0 || o.time - lastOutput >= 1.0)) {
                emit(sequence, "sample", o.time, text);
                lastOutput = o.time;
            }
            if (progressTime >= 2.0F) {
                End(o.time, "sustained-forward-motion", emit);
            }
            else if (arrivedTime >= 2.0F) {
                End(o.time, "stable-arrival", emit);
            }
            else if (inactiveTime >= 10.0F) {
                End(o.time, "context-ended-unresolved", emit);
            }
        }

      private:
        void Remember(double time, const std::string& text)
        {
            history.push_back({time, text});
            while (!history.empty() && (history.size() > 256 || time - history.front().time > 10.0)) {
                history.pop_front();
            }
        }
    };
} // namespace Wayfarer::Diagnostics
