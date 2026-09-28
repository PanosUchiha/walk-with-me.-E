#include "custom_formation.h"
#include "order_wheel.h"
#include "party_orders.h"
#include "travel_planner.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>
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
float Radius(FormationSlot s) { return std::hypot(s.lateral, s.longitudinal); }

int main()
{
    // Placing.
    Check(Radius(Custom::Constrain({2000, 0}, false)) <= Custom::reach + .01F, "places stay within reach");
    Check(Radius(Custom::Constrain({10, 5}, false)) >= Custom::clearance - .01F, "nobody stands on the player");
    const auto dropped = Custom::Constrain({0, 0}, false);
    Check(dropped.longitudinal < 0 && std::abs(dropped.lateral) < .01F, "a place dropped on the player goes behind");
    const auto snapped = Custom::Constrain({112, -163}, true);
    Check(snapped.lateral == 100 && snapped.longitudinal == -175, "snapping uses the 25-unit grid");
    Check(std::isfinite(Custom::Constrain({std::numeric_limits<float>::quiet_NaN(), 3}, false).lateral),
          "bad input becomes a real place");

    Custom::Formation f;
    Check(!f.Defined(), "an empty formation is not defined");
    Check(Custom::Add(f, {-120, 40}, false) && Custom::Add(f, {120, 40}, false) && Custom::Add(f, {0, -160}, false),
          "places can be added");
    Check(f.Defined() && f.count == 3, "three places");
    Check(!Custom::Add(f, {-110, 50}, false), "two companions cannot share a spot");
    Custom::Reserve(f, 1, "katana.esp|005900", "Katana");
    Custom::Reserve(f, 2, "katana.esp|005900", "Katana");
    Check(f.companions[1].empty() && f.companions[2] == "katana.esp|005900" && f.names[2] == "Katana",
          "reserving a companion again moves them");
    Custom::Remove(f, 0);
    Check(f.count == 2 && f.slots[0].lateral == 120 && f.companions[1] == "katana.esp|005900",
          "removing renumbers later places and keeps their companions");
    Custom::Flip(f);
    Check(f.slots[0].lateral == -120, "flip swaps left and right");
    Check(!Custom::MirrorOne(f, 1, false), "a place on the line of travel has no mirror");
    Check(Custom::MirrorOne(f, 0, false) && f.count == 3 && f.slots[2].lateral == 120 && f.companions[2].empty(),
          "mirroring adds an unreserved copy on the other side");
    for (int i = 0; i < PARTY_CAPACITY; ++i) {
        Custom::Add(f, {-500.0F + 110 * i, 300}, false);
    }
    Check(f.count == PARTY_CAPACITY && !Custom::Add(f, {0, 400}, false), "at most ten places");

    // Name and emblem.
    Check(Custom::CleanTitle("   ") == "Custom" && Custom::CleanTitle("  Shield Wall ") == "Shield Wall",
          "names are trimmed and never empty");
    Check(Custom::CleanTitle(std::string(40, 'x')).size() == Custom::maxTitle, "names are kept short");
    const auto cut = Custom::CleanTitle(std::string(23, 'a') + "\xC3\xA9\xC3\xA9");
    Check(cut.size() <= Custom::maxTitle && (static_cast<unsigned char>(cut.back()) & 0xC0) != 0xC0,
          "a long name never ends in half a character");
    Check(Custom::EmblemFromKey("wolf") == 6 && Custom::EmblemFromKey("nonsense") == Custom::defaultEmblem,
          "emblems load by key, unknown ones fall back");
    Custom::Formation named;
    named.title = "  ";
    named.emblem = 99;
    const auto tidy = Custom::Sanitize(named);
    Check(tidy.title == "Custom" && tidy.emblem == Custom::defaultEmblem, "loading repairs the name and emblem");
    Custom::Formation places;
    Custom::Add(places, {0, -200}, false);
    Custom::Formation settings;
    settings.title = "Shield Wall";
    settings.emblem = 4;
    const auto merged = Custom::WithPlaces(settings, places);
    Check(merged.count == 1 && merged.title == "Shield Wall" && merged.emblem == 4,
          "saving from the editor keeps the name and emblem chosen in settings");

    // Loading.
    Custom::Formation messy;
    messy.count = 99;
    messy.slots[0] = {0, 5000};
    messy.slots[1] = {2, 5000}; // Lands on top of the first after clamping.
    messy.slots[2] = {std::numeric_limits<float>::infinity(), 0};
    const auto clean = Custom::Sanitize(messy);
    Check(clean.count <= PARTY_CAPACITY && clean.count >= 2, "loading clamps the count");
    for (int i = 0; i < clean.count; ++i) {
        Check(Radius(clean.slots[i]) <= Custom::reach + .01F && std::isfinite(clean.slots[i].lateral),
              "loaded places are real and in reach");
        Check(!Custom::Crowded(clean, clean.slots[i], i), "loaded places are apart");
    }

    // Roles.
    Custom::Formation g;
    Custom::Add(g, {-120, 60}, false);
    Custom::Add(g, {120, 60}, false);
    Custom::Add(g, {0, -150}, false);
    Custom::Reserve(g, 0, "a", "A");
    Custom::Reserve(g, 2, "absent", "Away");
    for (int extra = 0; extra < 4; ++extra) {
        const auto p = Custom::RolePosition(g, g.count + extra);
        Check(p.longitudinal < -150, "companions beyond the drawn places fall in behind them");
        for (int other = 0; other < extra; ++other) {
            const auto q = Custom::RolePosition(g, g.count + other);
            Check(std::hypot(p.lateral - q.lateral, p.longitudinal - q.longitudinal) > Custom::spacing,
                  "fall-in places are apart");
        }
    }
    const std::vector<std::string> keys{"b", "a", "c", "d"};
    const auto reserved = Custom::ReservedRoles(g, keys);
    Check(reserved == std::vector<int>{-1, 0, -1, -1}, "a present companion takes their reserved place");
    const auto open = Custom::OpenRoles(g, reserved, 3);
    Check(open == std::vector<int>{1, 2, 3}, "free places first, then an absent companion's, then behind");

    // The planner.
    SetCustomFormation(g);
    for (int role = 0; role < 5; ++role) {
        const auto p = FormationPosition(FormationMode::kCustom, role);
        const auto q = Custom::RolePosition(CustomFormation(), role);
        Check(p == q, "custom travel uses the drawn places");
        Check(PartyRole(FormationMode::kCustom, role, 4, 7, 250, 0x1234).offset == q,
              "custom places are exact: no personal jitter");
    }
    Check(!(PartyRole(FormationMode::kLead, 0, 4, 7, 250, 0x1234).offset == FormationPosition(FormationMode::kLead, 0)),
          "the built-in styles keep their personal variation");

    // The wheel with seven orders.
    Check(orderCount == Wheel::count && OrderMode(6) == FormationMode::kCustom, "Custom is the seventh order");
    for (int mode = 0; mode < Wheel::count; ++mode) {
        Check(Wheel::clockwise[Wheel::slots[mode]] == mode, "wheel positions and orders map both ways");
        const float a = Wheel::Angle(mode);
        Check(Wheel::Direction(std::cos(a), std::sin(a)) == mode, "aiming at an order selects it");
        Check(Wheel::Mouse(std::cos(a) * 80, std::sin(a) * 80, 100) == mode, "the mouse finds each wedge");
    }
    Check(Wheel::Angle(1) < -1.5F, "Lead stays at the top");
    std::cout << "Custom formation checks passed: " << checks << '\n';
    return 0;
}
