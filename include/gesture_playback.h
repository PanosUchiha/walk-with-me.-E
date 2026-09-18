#pragma once
#include <cmath>

namespace Wayfarer::CommandGesture
{

    struct Playback
    {
        enum class Action
        {
            None,
            Stop,
            Release,
            Abandon,
            RetryStop,
            Stalled
        };
        bool owned{}, stopping{}, retried{}, warned{};
        int armType{1};
        float elapsed{}, duration{1.5F};

        void Start(int type = 1, float seconds = 1.5F)
        {
            *this = {};
            owned = true;
            armType = type;
            duration = seconds;
        }
        void Stop()
        {
            stopping = true;
            elapsed = 0;
        }
        Action Update(float dt, bool channelOurs, bool active, bool cancel)
        {
            if (!owned) {
                return Action::None;
            }
            if (!channelOurs) {
                *this = {};
                return Action::Abandon;
            }
            if (!std::isfinite(dt) || dt <= 0 || dt > 1) {
                return Action::None;
            }
            elapsed += dt;
            if (!stopping) {
                if (cancel || elapsed >= duration) {
                    Stop();
                    return Action::Stop;
                }
            }
            else {
                if (elapsed >= .35F && !active) {
                    owned = false;
                    return Action::Release;
                }
                if (elapsed >= 1.0F && !retried) {
                    retried = true;
                    return Action::RetryStop;
                }
                if (elapsed >= 2.0F && !warned) {
                    warned = true;
                    return Action::Stalled;
                }
            }
            return Action::None;
        }
    };
} // namespace Wayfarer::CommandGesture
