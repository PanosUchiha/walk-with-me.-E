#include "party_behavior.h"
#include <algorithm>
#include <cmath>
namespace Wayfarer
{
    void SandboxState::Update(Vec2 player, float speed, float dt, bool explicitOrder, float delay,
                              const SandboxTuning& t)
    {
        if (!initialized) {
            center = stillOrigin = player;
            initialized = true;
        }
        const bool moving = speed >= 24 || Length({player.x - stillOrigin.x, player.y - stillOrigin.y}) > 48;
        if (moving) {
            stationaryTime = 0;
            stillOrigin = player;
        }
        else {
            stationaryTime += std::clamp(dt, 0.0F, 0.5F);
        }
        const bool withinRestArea = Length({player.x - center.x, player.y - center.y}) <= t.resumeDistance;
        const bool wanted =
            explicitOrder || (t.automatic && (active ? withinRestArea : stationaryTime >= delay));
        if (wanted && !active) {
            center = player;
            activeTime = 0;
            expanded = false;
        }
        else if (wanted) {
            activeTime += std::clamp(dt, 0.0F, 0.5F);
        }
        active = wanted;
        if (!active) {
            radius = 0;
            activeTime = 0;
            expanded = false;
            center = player;
            return;
        }

        expanded = expanded || explicitOrder || activeTime >= std::max(0.0F, t.fullSandboxAfter);
        radius = expanded ? std::max(t.startRadius, t.maxRadius) : t.startRadius;
    }
    bool DistantCatchup(bool active, float distance, float enter, float leave)
    {
        return active ? distance > leave : distance >= enter;
    }
    float TravelSpeedTarget(float normalScale, float selectedCap, bool distant, float playerDistance,
                            float goalDistance, float enter, float leave)
    {
        if (!distant) {
            return normalScale;
        }
        auto eased = [](float x) {
            x = std::clamp(x, 0.0F, 1.0F);
            return x * x * (3 - 2 * x);
        };

        const float fullDistance = std::max(enter * 2.0F, leave + 900.0F);
        const float weight = std::min(eased((playerDistance - leave) / (fullDistance - leave)),
                                      eased((goalDistance - 200.0F) / 600.0F));
        return normalScale + (std::max(normalScale, 2.0F * selectedCap) - normalScale) * weight;
    }
} // namespace Wayfarer
