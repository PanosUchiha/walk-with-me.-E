#pragma once
#include "rest_activities.h"
#include "rest_pose.h"
#include "social_planner.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

// What companions do while resting depends on where they are: a campfire in the
// wilds, a shrine in a temple, a broom at home, a nervous glance in a crypt.
// Everything here is pure so it can be tested; the controller supplies the
// location keywords, model paths and weather.
namespace Wayfarer
{
    enum class RestPlace : std::uint8_t
    {
        Wild,
        Town,
        Home,
        PlayerHome,
        Temple,
        Shop,
        Hall,
        Tavern,
        Cave,
        Ruin
    };

    // Location keywords collected from the current location and its parents.
    struct PlaceSignals
    {
        bool interior{}, cave{}, ruin{}, inn{}, playerHouse{}, temple{}, store{}, hall{}, town{};
    };

    inline RestPlace ClassifyPlace(const PlaceSignals& s)
    {
        if (!s.interior) {
            return s.town ? RestPlace::Town : RestPlace::Wild;
        }
        if (s.ruin) {
            return RestPlace::Ruin;
        }
        if (s.cave) {
            return RestPlace::Cave;
        }
        if (s.inn) {
            return RestPlace::Tavern;
        }
        if (s.playerHouse) {
            return RestPlace::PlayerHome;
        }
        if (s.temple) {
            return RestPlace::Temple;
        }
        if (s.store) {
            return RestPlace::Shop;
        }
        return s.hall ? RestPlace::Hall : RestPlace::Home;
    }

    inline const char* PlaceName(RestPlace place)
    {
        constexpr std::array<const char*, 10> names{"wilderness", "town",   "home", "player home", "temple",
                                                    "shop",       "hall",   "inn",  "cave",        "ruin"};
        return names[static_cast<std::size_t>(place)];
    }

    // Things worth walking over to. Water and Vista are ground, not objects.
    enum class AreaFeature : std::uint8_t
    {
        None,
        Fire,
        Shrine,
        Stone,
        Water,
        Vista
    };

    inline const char* FeatureName(AreaFeature feature)
    {
        constexpr std::array<const char*, 6> names{"none", "fire", "shrine", "standing stone", "water's edge",
                                                   "lookout"};
        return names[static_cast<std::size_t>(feature)];
    }

    // Classifies a lower-case model path. Vanilla names lit and unlit campfires
    // "...Burning.nif" and "...Off.nif"; hanging braziers and sconces sit overhead.
    inline AreaFeature FeatureFromModel(std::string_view path)
    {
        auto has = [&](std::string_view part) { return path.find(part) != std::string_view::npos; };
        if (has("\\shrines\\") || has("shrineof")) {
            return AreaFeature::Shrine;
        }
        if (has("powershrine")) {
            return AreaFeature::Stone;
        }
        for (auto skip : {"off.nif", "hanging", "sconce", "sign", "firepillar", "dragonfire", "burningvol",
                          "burningbeam", "firevol", "smoke", "firesalt", "magic\\", "spell"}) {
            if (has(skip)) {
                return AreaFeature::None;
            }
        }
        if ((has("campfire") || has("fireplacewood")) && has("burning")) {
            return AreaFeature::Fire;
        }
        if (has("fxfire") || has("firepit") || has("fireplace") || has("brazier")) {
            return AreaFeature::Fire;
        }
        return AreaFeature::None;
    }

    // How many companions a feature takes at once.
    inline int FeatureCapacity(AreaFeature feature) { return feature == AreaFeature::Fire ? 3 : 1; }

    // Lower is better; added to the walking distance when ranking destinations.
    inline float FeatureScore(AreaFeature feature, RestPlace place, bool cold)
    {
        switch (feature) {
        case AreaFeature::Fire:
            return cold ? -900.0F : place == RestPlace::Town ? -200.0F : -450.0F;
        case AreaFeature::Shrine:
            return place == RestPlace::Temple ? -600.0F : -200.0F;
        case AreaFeature::Stone:
            return -300.0F;
        default:
            return 0;
        }
    }

    // Conditions that change what makes sense to do.
    struct PlaceTraits
    {
        bool cold{};  // Snowing outside, or an ice cave.
        bool sword{}; // A one-handed sword in the right hand.
    };

    inline bool OutdoorPlace(RestPlace place) { return place == RestPlace::Wild || place == RestPlace::Town; }

    // Somewhere people live or work, where companions have no business going through things.
    inline bool InhabitedInterior(RestPlace place)
    {
        return !OutdoorPlace(place) && place != RestPlace::Cave && place != RestPlace::Ruin;
    }

    // Eating and drinking look natural at a table, at the bar or round a campfire. A town street,
    // a temple, a shop counter or a crypt is none of those; companions standing in the middle of
    // town eating bread was the worst of it.
    inline bool MealPlace(RestPlace place, bool atFire)
    {
        switch (place) {
        case RestPlace::Tavern:
        case RestPlace::Home:
        case RestPlace::PlayerHome:
        case RestPlace::Hall:
            return true;
        case RestPlace::Wild:
            return atFire; // A meal at camp.
        default:
            return false;
        }
    }

    // Skyrim's own sandbox meals (the package's Allow Eating). A short stop, which is what
    // temporary rest is, only eats at the inn.
    inline bool NativeMeals(RestPlace place, RestContext context, bool atFire, bool settled,
                            const SandboxTuning& tuning)
    {
        return tuning.meals && context != RestContext::Dungeon && MealPlace(place, atFire) &&
               (settled || place == RestPlace::Tavern);
    }

    // Reading wants somewhere quiet and safe; not a street, a shop floor or a crypt.
    inline bool ReadingPlace(RestPlace place)
    {
        return place != RestPlace::Town && place != RestPlace::Shop && place != RestPlace::Cave &&
               place != RestPlace::Ruin;
    }

    // No limbering up in front of an altar or a shopkeeper.
    inline bool StretchingPlace(RestPlace place) { return place != RestPlace::Temple && place != RestPlace::Shop; }

    // What to do on arriving at a feature. A short stop (temporary rest) does not sit down.
    inline RestActivity FeatureActivity(AreaFeature feature, RestPlace place, std::uint32_t roll,
                                        const SandboxTuning& tuning, bool settled = true)
    {
        switch (feature) {
        case AreaFeature::Fire:
            if (place == RestPlace::Wild) {
                return settled && tuning.groundSitting && roll % 2 ? RestActivity::Ground
                                                                   : RestActivity::WarmHandsCrouched;
            }
            return place == RestPlace::Cave || place == RestPlace::Ruin ? RestActivity::WarmHandsCrouched
                                                                       : RestActivity::WarmHands;
        case AreaFeature::Shrine:
            return RestActivity::Pray;
        case AreaFeature::Stone:
            return roll % 2 ? RestActivity::Study : RestActivity::Examine;
        case AreaFeature::Water:
            return roll % 3 ? RestActivity::Kneel : RestActivity::LookFar;
        case AreaFeature::Vista:
            return RestActivity::LookFar;
        default:
            return RestActivity::Stand;
        }
    }

    // What to do next while still at the feature, once the first pose ends.
    inline RestActivity FeatureFollowUp(AreaFeature feature, RestPlace place, std::uint32_t roll,
                                        const SandboxTuning& tuning, bool settled = true)
    {
        switch (feature) {
        case AreaFeature::Fire:
            // A camp is where the wilderness meal happens; otherwise stay by the fire.
            if (place == RestPlace::Wild && settled && tuning.meals && roll % 4 == 0) {
                return RestActivity::Eat;
            }
            return FeatureActivity(feature, place, roll, tuning, settled);
        case AreaFeature::Shrine:
            return roll % 3 ? RestActivity::Stand : RestActivity::Pray;
        case AreaFeature::Stone:
        case AreaFeature::Vista:
            return roll % 2 ? RestActivity::Stand : RestActivity::LookFar;
        case AreaFeature::Water:
            return roll % 2 ? RestActivity::Kneel : RestActivity::LookFar;
        default:
            return RestActivity::Stand;
        }
    }

    inline const char* RestActivityName(RestActivity activity)
    {
        constexpr std::array<const char*, 20> names{
            "stands",       "sits on the ground", "eats",      "drinks",      "reads",     "stretches",
            "examines",     "looks into the distance", "warms their hands", "warms their hands", "prays",
            "kneels",       "studies",            "sweeps",    "fidgets",     "wipes their brow",
            "cleans their sword", "rubs their arms", "takes a seat at the table", "applauds"};
        const auto index = static_cast<std::size_t>(activity);
        return index < names.size() ? names[index] : "rests";
    }

    // How long a pose is held. One-shot clips get a few seconds; loops longer, and
    // longest by a fire.
    inline float RestPoseSeconds(RestActivity activity, AreaFeature at, std::uint32_t roll)
    {
        const float r = static_cast<float>(roll % 1000) / 1000.0F;
        switch (activity) {
        case RestActivity::Pray:
            return 9 + 3 * r;
        case RestActivity::Stretch:
        case RestActivity::Examine:
        case RestActivity::LookFar:
        case RestActivity::WipeBrow:
        case RestActivity::CleanSword:
        case RestActivity::WarmArms:
            return 7 + 3 * r;
        case RestActivity::WarmHands:
        case RestActivity::WarmHandsCrouched:
            return 40 + 20 * r;
        case RestActivity::Ground:
            return at == AreaFeature::Fire ? 45 + 25 * r : 25 + 20 * r;
        case RestActivity::Kneel:
            return 15 + 10 * r;
        case RestActivity::Nervous:
            return 12 + 6 * r;
        case RestActivity::Study:
            return 20 + 15 * r;
        case RestActivity::Sweep:
            return 30 + 20 * r;
        case RestActivity::TableSeat:
            return 240 + 120 * r;
        case RestActivity::Applaud:
            return 5 + 2 * r;
        default:
            return 25 + 20 * r;
        }
    }

    // Features that must be faced; ground at water's edge or a lookout faces outward.
    inline bool FacesFeature(AreaFeature feature)
    {
        return feature == AreaFeature::Fire || feature == AreaFeature::Shrine || feature == AreaFeature::Stone;
    }

    // Poses chosen while resting with nothing in particular to walk to. The generic table needs
    // Extra rest poses; the place table needs Area activities. Every entry has to suit the place:
    // no meals in the street, no stretching before an altar, no reading in a crypt.
    //
    // `settled` is a Relax order. Temporary rest is a short stop while the player looks around, so
    // it keeps to brief standing poses: eating, reading, sitting on the ground, studying and chores
    // wait for a real rest. The inn is the exception; holding a drink there is what waiting looks like.
    inline RestActivity ChoosePlaceActivity(RestPlace place, RestContext context, std::uint32_t roll,
                                            const SandboxTuning& tuning, const PlaceTraits& traits,
                                            bool settled = true)
    {
        std::array<RestActivity, 40> choices{};
        int count = 0;
        auto add = [&](RestActivity activity, int weight) {
            while (weight-- > 0 && count < static_cast<int>(choices.size())) {
                choices[count++] = activity;
            }
        };
        const bool tavern = place == RestPlace::Tavern;
        const bool dungeon =
            context == RestContext::Dungeon || place == RestPlace::Cave || place == RestPlace::Ruin;
        // Scripted food and drink are held standing, so only where people eat on their feet: the
        // inn and the player's own house. Other homes and halls eat at their tables through
        // Skyrim's own sandbox meals.
        const bool refreshments = tuning.meals && !dungeon && (tavern || (settled && place == RestPlace::PlayerHome));
        if (tuning.extraPoses) {
            if (settled && place == RestPlace::Wild && tuning.groundSitting) {
                add(RestActivity::Ground, 3);
            }
            if (refreshments) {
                add(RestActivity::Eat, tavern ? 2 : 1);
                add(RestActivity::Drink, tavern ? 4 : 1);
            }
            if (tuning.reading && settled && ReadingPlace(place) && !dungeon) {
                add(RestActivity::Read, place == RestPlace::Wild ? 1 : 2);
            }
            if (tuning.stretching && StretchingPlace(place)) {
                add(RestActivity::Stretch, 1);
            }
            add(RestActivity::Examine, dungeon ? 5 : place == RestPlace::Temple ? 0 : 1);
        }
        else if (refreshments) {
            add(RestActivity::Drink, tavern ? 4 : 1);
            add(RestActivity::Eat, 1);
        }
        const int longer = settled ? 1 : 0; // Loops that belong to a real rest.
        if (tuning.areaActivities) {
            switch (place) {
            case RestPlace::Wild:
                add(RestActivity::LookFar, 2);
                add(RestActivity::WarmArms, traits.cold ? 3 : 0);
                add(RestActivity::CleanSword, traits.sword ? 1 : 0);
                break;
            case RestPlace::Town:
                add(RestActivity::Stand, 2);
                add(RestActivity::LookFar, 1);
                add(RestActivity::WarmArms, traits.cold ? 2 : 0);
                break;
            case RestPlace::Home:
                add(RestActivity::Study, 2 * longer);
                add(RestActivity::Stand, 1);
                break;
            case RestPlace::PlayerHome:
                add(RestActivity::Sweep, 2 * longer);
                add(RestActivity::Study, longer);
                add(RestActivity::Stand, 1 - longer);
                break;
            case RestPlace::Temple:
                add(RestActivity::Pray, 3);
                add(RestActivity::Study, longer);
                add(RestActivity::Stand, 2);
                break;
            case RestPlace::Shop:
                add(RestActivity::Examine, 2);
                add(RestActivity::Study, 2 * longer);
                add(RestActivity::Stand, 1);
                break;
            case RestPlace::Hall:
                add(RestActivity::Stand, 3);
                add(RestActivity::Study, longer);
                break;
            case RestPlace::Tavern:
                add(RestActivity::Stand, 1);
                break;
            case RestPlace::Cave:
                add(RestActivity::Examine, 2);
                add(RestActivity::Nervous, 2);
                add(RestActivity::WipeBrow, 1);
                add(RestActivity::LookFar, 1);
                add(RestActivity::CleanSword, traits.sword ? 2 : 0);
                add(RestActivity::WarmArms, traits.cold ? 2 : 0);
                break;
            case RestPlace::Ruin:
                add(RestActivity::Examine, 3);
                add(RestActivity::Study, longer);
                add(RestActivity::Nervous, 1);
                add(RestActivity::LookFar, 1);
                add(RestActivity::CleanSword, traits.sword ? 1 : 0);
                add(RestActivity::WarmArms, traits.cold ? 1 : 0);
                break;
            }
        }
        return count ? choices[roll % static_cast<std::uint32_t>(count)] : RestActivity::Stand;
    }

    inline bool AnyRestPoses(RestContext context, const SandboxTuning& tuning)
    {
        return tuning.extraPoses || tuning.areaActivities || SettledRefreshments(context, tuning);
    }

    // Conversations take on the place: hushed in crypts, over a drink at the inn.
    inline SocialStyle StyleFor(RestPlace place, bool areaActivities)
    {
        if (!areaActivities) {
            return {};
        }
        switch (place) {
        case RestPlace::Tavern:
            return {150, 31, 40, true};
        case RestPlace::Cave:
        case RestPlace::Ruin:
            return {90, 31, 34, false};
        case RestPlace::Temple:
            return {90, 31, 25, false};
        case RestPlace::Shop:
        case RestPlace::Hall:
            return {100, 41, 50, false};
        default:
            return {};
        }
    }

    // One gesture for a conversation turn in this place.
    inline std::uint32_t ConversationIdle(RestPlace place, std::uint32_t roll, bool areaActivities)
    {
        constexpr std::uint32_t positive = 0x58B45, negative = 0x58B46, laugh = 0x75C5F, lookFar = 0x75C62,
                                wipeBrow = 0x977EC, pointFar1 = 0xB8F1E, pointFar2 = 0xB8F1F;
        if (!areaActivities) {
            return roll % 3 == 0 ? negative : positive;
        }
        switch (place) {
        case RestPlace::Cave:
        case RestPlace::Ruin: {
            constexpr std::array<std::uint32_t, 5> hushed{negative, negative, positive, lookFar, wipeBrow};
            return hushed[roll % hushed.size()];
        }
        case RestPlace::Temple:
            return roll % 2 ? positive : negative;
        case RestPlace::Wild: {
            constexpr std::array<std::uint32_t, 6> open{positive, positive, positive, negative, laugh, pointFar1};
            return open[roll % open.size()];
        }
        case RestPlace::Town: {
            constexpr std::array<std::uint32_t, 7> lively{positive, positive, positive, negative,
                                                          laugh,    laugh,    pointFar2};
            return lively[roll % lively.size()];
        }
        default: {
            constexpr std::array<std::uint32_t, 6> indoor{positive, positive, positive, negative, negative, laugh};
            return indoor[roll % indoor.size()];
        }
        }
    }

    // The drink both partners hold for a conversation at the inn.
    inline constexpr std::uint32_t SHARED_DRINK_IDLE = 0x640FC;
    // Seated loops at an inn table; IdleStop puts the mug or bowl down without standing.
    inline constexpr std::uint32_t CHAIR_DRINK_IDLE = 0x65D07, CHAIR_EAT_IDLE = 0x65D06;
    // The server's nod on reaching the table.
    inline constexpr std::uint32_t SERVE_IDLE = 0x58B45;

    // What happens at a shared inn table, and when. A round (calling a server over
    // for drinks) comes first and then every few minutes; between rounds someone
    // drinks or two of them turn to talk.
    struct TableBeats
    {
        enum class Beat
        {
            None,
            Drink,
            Talk,
            Round
        };
        float timer{10}, roundCooldown{15};
        std::uint32_t sequence{};
        void Reset() { *this = {}; }
        Beat Update(float dt, int seated)
        {
            if (seated < 1) {
                return Beat::None;
            }
            dt = std::clamp(dt, 0.0F, 1.0F);
            timer -= dt;
            roundCooldown -= dt;
            if (seated >= 2 && roundCooldown <= 0) {
                roundCooldown = 150.0F + static_cast<float>(RestPose::Mix(++sequence * 31) % 91);
                timer = std::max(timer, 20.0F); // Let the round be drunk first.
                return Beat::Round;
            }
            if (timer > 0) {
                return Beat::None;
            }
            ++sequence;
            timer = 14.0F + static_cast<float>(RestPose::Mix(sequence * 7919) % 17);
            return seated >= 2 && RestPose::Mix(sequence) % 5 < 3 ? Beat::Talk : Beat::Drink;
        }
    };
} // namespace Wayfarer
