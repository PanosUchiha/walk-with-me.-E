#include "area_activities.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace Wayfarer;
int checks = 0;
void Check(bool ok, const char* why)
{
    ++checks;
    if (!ok) {
        std::cerr << why << '\n';
        std::exit(1);
    }
}
std::set<RestActivity> Palette(RestPlace place, RestContext context, const SandboxTuning& tuning,
                               PlaceTraits traits = {})
{
    std::set<RestActivity> seen;
    for (std::uint32_t roll = 0; roll < 400; ++roll) {
        seen.insert(ChoosePlaceActivity(place, context, roll, tuning, traits));
    }
    return seen;
}
int main()
{
    // Places from location keywords.
    Check(ClassifyPlace({}) == RestPlace::Wild, "an exterior without town keywords is the wilderness");
    Check(ClassifyPlace({.town = true}) == RestPlace::Town, "exterior town keywords make a town");
    Check(ClassifyPlace({.interior = true}) == RestPlace::Home, "a plain interior is somebody's home");
    Check(ClassifyPlace({.interior = true, .inn = true}) == RestPlace::Tavern, "inns");
    Check(ClassifyPlace({.interior = true, .playerHouse = true}) == RestPlace::PlayerHome, "the player's house");
    Check(ClassifyPlace({.interior = true, .temple = true}) == RestPlace::Temple, "temples");
    Check(ClassifyPlace({.interior = true, .store = true}) == RestPlace::Shop, "shops");
    Check(ClassifyPlace({.interior = true, .hall = true}) == RestPlace::Hall, "castles, guilds, barracks");
    Check(ClassifyPlace({.interior = true, .cave = true}) == RestPlace::Cave, "caves and mines");
    Check(ClassifyPlace({.interior = true, .ruin = true}) == RestPlace::Ruin, "ruins, forts and crypts");
    Check(ClassifyPlace({.interior = true, .ruin = true, .inn = true}) == RestPlace::Ruin,
          "a dungeon keyword outranks everything indoors");
    Check(ClassifyPlace({.interior = false, .ruin = true}) == RestPlace::Wild,
          "outside a ruin is still the wilderness");

    for (auto place : {RestPlace::Home, RestPlace::PlayerHome, RestPlace::Temple, RestPlace::Shop, RestPlace::Hall,
                       RestPlace::Tavern}) {
        Check(InhabitedInterior(place), "no loot scouting where people live or work");
    }
    for (auto place : {RestPlace::Wild, RestPlace::Town, RestPlace::Cave, RestPlace::Ruin}) {
        Check(!InhabitedInterior(place), "caves, ruins and the outdoors stay open to scouting");
    }

    // Features from vanilla model paths (lower case, as the controller passes them).
    Check(FeatureFromModel("clutter\\woodfires\\campfire01landburning.nif") == AreaFeature::Fire, "lit campfire");
    Check(FeatureFromModel("clutter\\woodfires\\campfire01landoff.nif") == AreaFeature::None,
          "a cold campfire is not a fire");
    Check(FeatureFromModel("clutter\\giant\\giantcampfire01burning.nif") == AreaFeature::Fire, "giant camp fire");
    Check(FeatureFromModel("effects\\fxfirewithembers01.nif") == AreaFeature::Fire, "fire effect");
    Check(FeatureFromModel("architecture\\windhelm\\whfirepit01.nif") == AreaFeature::Fire, "fire pit");
    Check(FeatureFromModel("architecture\\riften\\clutter\\rtfireplace01.nif") == AreaFeature::Fire, "fireplace");
    Check(FeatureFromModel("architecture\\whiterun\\wrclutter\\wrbrazier01.nif") == AreaFeature::Fire, "brazier");
    Check(FeatureFromModel("architecture\\markarth\\mrkbrazierhanging01.nif") == AreaFeature::None,
          "hanging braziers are overhead");
    Check(FeatureFromModel("clutter\\signage\\windhelm\\signwhcandlehearthinn.nif") == AreaFeature::None,
          "an inn sign is not a hearth");
    Check(FeatureFromModel("magic\\fxfirepillar01.nif") == AreaFeature::None, "spell effects are not campfires");
    Check(FeatureFromModel("effects\\bfxfirevol01.nif") == AreaFeature::None, "burning buildings are not campfires");
    Check(FeatureFromModel("clutter\\shrines\\shrinetalos.nif") == AreaFeature::Shrine, "shrines of the Divines");
    Check(FeatureFromModel("clutter\\statues\\shrineofazura01.nif") == AreaFeature::Shrine, "named shrines");
    Check(FeatureFromModel("clutter\\powershrines\\powershrine01\\warriorstone.nif") == AreaFeature::Stone,
          "standing stones");
    Check(FeatureFromModel("furniture\\common\\chair01.nif") == AreaFeature::None, "ordinary furniture");
    Check(FeatureCapacity(AreaFeature::Fire) == 3 && FeatureCapacity(AreaFeature::Shrine) == 1,
          "a fire takes a small circle; a shrine one at a time");
    Check(FeatureScore(AreaFeature::Fire, RestPlace::Wild, true) < FeatureScore(AreaFeature::Fire, RestPlace::Wild, false),
          "cold weather draws companions to fires");
    Check(FeatureScore(AreaFeature::Shrine, RestPlace::Temple, false) <
              FeatureScore(AreaFeature::Shrine, RestPlace::Town, false),
          "shrines matter most in a temple");

    // What they do at a feature.
    SandboxTuning tuning;
    std::set<RestActivity> atFire;
    for (std::uint32_t roll = 0; roll < 20; ++roll) {
        atFire.insert(FeatureActivity(AreaFeature::Fire, RestPlace::Wild, roll, tuning));
    }
    Check(atFire == std::set{RestActivity::Ground, RestActivity::WarmHandsCrouched},
          "at a wilderness fire companions sit or crouch to warm their hands");
    tuning.groundSitting = false;
    Check(FeatureActivity(AreaFeature::Fire, RestPlace::Wild, 1, tuning) == RestActivity::WarmHandsCrouched,
          "ground sitting off: crouch instead");
    tuning.groundSitting = true;
    Check(FeatureActivity(AreaFeature::Fire, RestPlace::Tavern, 1, tuning) == RestActivity::WarmHands,
          "indoors they stand at the hearth");
    Check(FeatureActivity(AreaFeature::Fire, RestPlace::Cave, 1, tuning) == RestActivity::WarmHandsCrouched,
          "a cave fire is crouched at");
    Check(FeatureActivity(AreaFeature::Shrine, RestPlace::Temple, 7, tuning) == RestActivity::Pray, "shrines: pray");
    Check(FeatureActivity(AreaFeature::Vista, RestPlace::Wild, 7, tuning) == RestActivity::LookFar,
          "high ground: look out");
    std::set<RestActivity> atWater;
    for (std::uint32_t roll = 0; roll < 20; ++roll) {
        atWater.insert(FeatureActivity(AreaFeature::Water, RestPlace::Wild, roll, tuning));
    }
    Check(atWater == std::set{RestActivity::Kneel, RestActivity::LookFar}, "the water's edge: kneel or look out");
    Check(FacesFeature(AreaFeature::Fire) && FacesFeature(AreaFeature::Shrine) && !FacesFeature(AreaFeature::Water),
          "fires and shrines are faced; shore and lookout face outward");

    // Place palettes.
    const auto wild = Palette(RestPlace::Wild, RestContext::Wilderness, tuning);
    Check(wild.contains(RestActivity::LookFar) && !wild.contains(RestActivity::Sweep) &&
              !wild.contains(RestActivity::Pray),
          "wilderness rests look out over the land, never sweep or pray");
    Check(!wild.contains(RestActivity::WarmArms) &&
              Palette(RestPlace::Wild, RestContext::Wilderness, tuning, {.cold = true}).contains(RestActivity::WarmArms),
          "rubbing arms only when it is cold");
    Check(!wild.contains(RestActivity::CleanSword) &&
              Palette(RestPlace::Wild, RestContext::Wilderness, tuning, {.sword = true})
                  .contains(RestActivity::CleanSword),
          "cleaning a sword needs a sword");
    Check(Palette(RestPlace::PlayerHome, RestContext::Interior, tuning).contains(RestActivity::Sweep) &&
              !Palette(RestPlace::Home, RestContext::Interior, tuning).contains(RestActivity::Sweep),
          "chores only in the player's own home");
    Check(Palette(RestPlace::Temple, RestContext::Interior, tuning).contains(RestActivity::Pray), "temples: prayer");
    const auto cave = Palette(RestPlace::Cave, RestContext::Dungeon, tuning);
    Check(cave.contains(RestActivity::Nervous) && cave.contains(RestActivity::Examine) &&
              cave.contains(RestActivity::LookFar) && !cave.contains(RestActivity::Eat),
          "caves are watchful: nervous glances, examining, no meals");
    const auto inn = Palette(RestPlace::Tavern, RestContext::Tavern, tuning);
    Check(inn.contains(RestActivity::Drink) && inn.contains(RestActivity::Eat), "inns: drinks and food");
    SandboxTuning plain = tuning;
    plain.areaActivities = false;
    Check(Palette(RestPlace::Wild, RestContext::Wilderness, plain) == std::set{RestActivity::Stand},
          "with area activities and extra poses off, nothing scripted in the wilds");
    Check(!AnyRestPoses(RestContext::Wilderness, plain) && AnyRestPoses(RestContext::Wilderness, tuning),
          "area activities alone enable rest poses");
    Check(AnyRestPoses(RestContext::Tavern, plain), "inn refreshments stay available on their own");
    // Meals only where a meal fits.
    for (auto place : {RestPlace::Tavern, RestPlace::Home, RestPlace::PlayerHome, RestPlace::Hall}) {
        Check(MealPlace(place, false), "inns, homes and halls have somewhere to eat");
    }
    for (auto place : {RestPlace::Town, RestPlace::Temple, RestPlace::Shop, RestPlace::Cave, RestPlace::Ruin}) {
        Check(!MealPlace(place, false) && !MealPlace(place, true),
              "no meals in the street, before an altar, at a counter or in a crypt");
    }
    Check(!MealPlace(RestPlace::Wild, false) && MealPlace(RestPlace::Wild, true),
          "in the wilds a meal belongs at the campfire");
    SandboxTuning meals;
    Check(!NativeMeals(RestPlace::Town, RestContext::Settlement, false, true, meals),
          "Skyrim's own sandbox never eats bread in the middle of town");
    Check(NativeMeals(RestPlace::Home, RestContext::Interior, false, true, meals) &&
              !NativeMeals(RestPlace::Home, RestContext::Interior, false, false, meals),
          "a home meal waits for a real rest");
    Check(NativeMeals(RestPlace::Tavern, RestContext::Tavern, false, false, meals),
          "a short stop at the inn may still eat and drink");
    Check(!NativeMeals(RestPlace::Wild, RestContext::Dungeon, true, true, meals),
          "no camp meals at a bandit camp's fire");
    meals.meals = false;
    Check(!NativeMeals(RestPlace::Tavern, RestContext::Tavern, false, true, meals), "the meal switch wins");

    SandboxTuning extras = tuning;
    extras.extraPoses = true;
    const auto town = Palette(RestPlace::Town, RestContext::Settlement, extras);
    Check(!town.contains(RestActivity::Eat) && !town.contains(RestActivity::Drink) &&
              !town.contains(RestActivity::Read) && !town.contains(RestActivity::Ground),
          "in town they stand about: no food, drink, books or sitting in the road");
    Check(town.contains(RestActivity::Stand) && town.contains(RestActivity::LookFar), "town rests look around");
    SandboxTuning noArea = extras;
    noArea.areaActivities = false;
    const auto plainTown = Palette(RestPlace::Town, RestContext::Settlement, noArea);
    Check(!plainTown.contains(RestActivity::Eat) && !plainTown.contains(RestActivity::Drink),
          "extra poses alone never bring meals to the street");
    SandboxTuning refreshOnly = tuning;
    refreshOnly.areaActivities = false;
    Check(Palette(RestPlace::Town, RestContext::Settlement, refreshOnly) == std::set{RestActivity::Stand},
          "without extra poses a town rest has no scripted refreshments either");
    const auto temple = Palette(RestPlace::Temple, RestContext::Interior, extras);
    Check(!temple.contains(RestActivity::Stretch) && !temple.contains(RestActivity::Eat) &&
              !temple.contains(RestActivity::Examine),
          "temples are for prayer, not stretching or snacks");
    const auto shop = Palette(RestPlace::Shop, RestContext::Interior, extras);
    Check(!shop.contains(RestActivity::Read) && !shop.contains(RestActivity::Eat) &&
              !shop.contains(RestActivity::Stretch),
          "a shop floor is for looking at the wares");
    const auto home = Palette(RestPlace::Home, RestContext::Interior, extras);
    Check(!home.contains(RestActivity::Eat) && !home.contains(RestActivity::Drink),
          "in someone else's home they eat at the table, not on their feet");
    const auto own = Palette(RestPlace::PlayerHome, RestContext::Interior, extras);
    Check(own.contains(RestActivity::Eat) && own.contains(RestActivity::Read), "at home they help themselves");

    // Temporary rest is a short stop: nothing that settles in.
    auto brief = [&](RestPlace place, RestContext context) {
        std::set<RestActivity> seen;
        for (std::uint32_t roll = 0; roll < 400; ++roll) {
            seen.insert(ChoosePlaceActivity(place, context, roll, extras, {}, false));
        }
        return seen;
    };
    for (auto [place, context] : {std::pair{RestPlace::Wild, RestContext::Wilderness},
                                  std::pair{RestPlace::Home, RestContext::Interior},
                                  std::pair{RestPlace::PlayerHome, RestContext::Interior},
                                  std::pair{RestPlace::Hall, RestContext::Interior}}) {
        const auto seen = brief(place, context);
        for (auto settling : {RestActivity::Ground, RestActivity::Eat, RestActivity::Read, RestActivity::Sweep,
                              RestActivity::Study}) {
            Check(!seen.contains(settling), "a short stop never sits down, eats, reads, studies or sweeps");
        }
        Check(!seen.empty(), "a short stop still has something to do");
    }
    Check(brief(RestPlace::Tavern, RestContext::Tavern).contains(RestActivity::Drink),
          "waiting at the inn, a drink in hand");
    for (std::uint32_t roll = 0; roll < 40; ++roll) {
        Check(FeatureActivity(AreaFeature::Fire, RestPlace::Wild, roll, tuning, false) != RestActivity::Ground,
              "a short stop at a fire crouches rather than sitting down");
        Check(FeatureFollowUp(AreaFeature::Fire, RestPlace::Wild, roll, tuning, false) != RestActivity::Eat,
              "no camp meal on a short stop");
    }
    std::set<RestActivity> camp;
    for (std::uint32_t roll = 0; roll < 40; ++roll) {
        camp.insert(FeatureFollowUp(AreaFeature::Fire, RestPlace::Wild, roll, tuning));
    }
    Check(camp.contains(RestActivity::Eat) && camp.contains(RestActivity::Ground),
          "a Relax camp eats and sits by the fire");
    SandboxTuning noMeals = tuning;
    noMeals.meals = false;
    for (std::uint32_t roll = 0; roll < 40; ++roll) {
        Check(FeatureFollowUp(AreaFeature::Fire, RestPlace::Wild, roll, noMeals) != RestActivity::Eat,
              "the meal switch covers camp meals");
        Check(FeatureFollowUp(AreaFeature::Fire, RestPlace::Tavern, roll, tuning) != RestActivity::Eat,
              "the hearth at the inn is for warming hands");
    }

    for (auto activity : {RestActivity::LookFar, RestActivity::Pray, RestActivity::WipeBrow, RestActivity::Applaud}) {
        Check(FiniteRestActivity(activity) && RestPoseSeconds(activity, AreaFeature::None, 999) <= 12,
              "one-shot clips are held only briefly");
    }
    for (auto activity : {RestActivity::WarmHands, RestActivity::Kneel, RestActivity::Sweep, RestActivity::Study}) {
        Check(!FiniteRestActivity(activity) && RestPoseSeconds(activity, AreaFeature::None, 0) >= 15,
              "loops are held for a while");
    }
    Check(RestPoseSeconds(RestActivity::Ground, AreaFeature::Fire, 0) > RestPoseSeconds(RestActivity::Ground, AreaFeature::None, 999),
          "a seat by the fire is kept longer");
    Check(RestPoseSeconds(RestActivity::TableSeat, AreaFeature::None, 0) >= 240, "a seat at the inn table is kept");
    Check(std::string_view(RestActivityName(RestActivity::Applaud)) == "applauds" &&
              std::string_view(RestActivityName(RestActivity::WarmArms)) == "rubs their arms",
          "every activity has a name for the log");

    // Conversations by place.
    Check(StyleFor(RestPlace::Tavern, true).shareDrink, "inn conversations are over a drink");
    Check(StyleFor(RestPlace::Cave, true).minDuration < StyleFor(RestPlace::Wild, true).minDuration &&
              StyleFor(RestPlace::Cave, true).gesturePercent < StyleFor(RestPlace::Town, true).gesturePercent,
          "cave talks are shorter and hushed");
    Check(!StyleFor(RestPlace::Tavern, false).shareDrink && StyleFor(RestPlace::Tavern, false).minDuration == 120,
          "area activities off: the standard conversation");
    std::set<std::uint32_t> townIdles, caveIdles;
    for (std::uint32_t roll = 0; roll < 50; ++roll) {
        townIdles.insert(ConversationIdle(RestPlace::Town, roll, true));
        caveIdles.insert(ConversationIdle(RestPlace::Cave, roll, true));
    }
    Check(townIdles.contains(0x75C5F) && !caveIdles.contains(0x75C5F), "laughter in town, not in a crypt");
    Check(caveIdles.contains(0x75C62), "glances into the dark in caves");
    SocialPlanner social;
    social.style = StyleFor(RestPlace::Tavern, true);
    social.cooldown = 0;
    social.Update(.25F, {{1, {0, 0}}, {2, {100, 0}}});
    Check(social.count == 2 && social.duration >= 150 && social.pending[0] && social.pending[1],
          "at the inn both partners pick up a drink at once");

    // The inn table.
    TableBeats beats;
    Check(beats.Update(60, 0) == TableBeats::Beat::None, "no beats at an empty table");
    beats.Reset();
    int rounds = 0, drinks = 0, talks = 0;
    for (int i = 0; i < 4 * 600; ++i) {
        switch (beats.Update(.25F, 3)) {
        case TableBeats::Beat::Round:
            ++rounds;
            break;
        case TableBeats::Beat::Drink:
            ++drinks;
            break;
        case TableBeats::Beat::Talk:
            ++talks;
            break;
        default:
            break;
        }
    }
    Check(rounds >= 3 && rounds <= 5, "a round is called soon after sitting down, then every few minutes");
    Check(drinks > 0 && talks > drinks, "between rounds they mostly talk, and drink now and then");
    TableBeats alone;
    bool roundAlone = false;
    for (int i = 0; i < 4 * 300; ++i) {
        roundAlone |= alone.Update(.25F, 1) == TableBeats::Beat::Round;
    }
    Check(!roundAlone, "nobody calls a server for a table of one");
    TableBeats first;
    float firstRound = 0;
    for (int i = 0; i < 4 * 60; ++i) {
        if (first.Update(.25F, 2) == TableBeats::Beat::Round) {
            firstRound = static_cast<float>(i) * .25F;
            break;
        }
    }
    Check(firstRound > 10 && firstRound < 20, "the first round comes about fifteen seconds after sitting");

    std::cout << checks << " area activity checks passed\n";
    return 0;
}
