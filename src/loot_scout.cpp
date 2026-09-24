#include "loot_scout.h"
#include "engine_compatibility.h"
#include "formation_controller.h"
#include "travel_space.h"
#include <numbers>

namespace Wayfarer::Loot
{
    namespace
    {
        constexpr RE::FormID PLAYER_FACTION = 0xDB1;
        constexpr RE::FormID GOLD = 0xF;
        constexpr RE::FormID ANIMAL_HIDE = 0x914EA; // VendorItemAnimalHide
        constexpr RE::FormID ANIMAL_PART = 0x914EB; // VendorItemAnimalPart
        // Looping searches from vanilla idle markers; each lasts until the stand-up event. Ralof uses
        // IdleSearchBody at the end of a Travel package in Helgen, the same template WWM travels with.
        constexpr RE::FormID IDLE_SEARCH_BODY = 0xEFC64;     // Kneel beside it ("IdleKneeling").
        constexpr RE::FormID IDLE_SEARCHING_CHEST = 0x6FF14; // Kneel and rummage in a low container.
        constexpr RE::FormID IDLE_SEARCHING_TABLE = 0x6FF0F; // Stand and rummage at a tall one.
        constexpr RE::FormID IDLE_PICKUP_GROUND = 0x75C3E;   // Last resort: a brief reach.

        std::mutex activationMutex;
        std::vector<RE::FormID> activations;
        std::mutex markerMutex;
        std::vector<Marker> markers;
        struct Sight
        {
            bool visible{};
            float delay{};
        };
        std::unordered_map<RE::FormID, Sight> sights;
        struct Rising
        {
            RE::ActorHandle actor;
            float remaining{};
        };
        std::vector<Rising> rising; // Stand-ups to verify; game thread only.

        struct ActivationSink final : RE::BSTEventSink<RE::TESActivateEvent>
        {
            RE::BSEventNotifyControl ProcessEvent(const RE::TESActivateEvent* event,
                                                  RE::BSTEventSource<RE::TESActivateEvent>*) override
            {
                if (event && event->objectActivated && event->actionRef &&
                    event->actionRef.get() == RE::PlayerCharacter::GetSingleton()) {
                    std::lock_guard lock(activationMutex);
                    if (activations.size() < 64) {
                        activations.push_back(event->objectActivated->GetFormID());
                    }
                }
                return RE::BSEventNotifyControl::kContinue;
            }
        } activationSink;

        class SightCollector final : public RE::hkpClosestRayHitCollector
        {
          public:
            RE::FormID closestReference{};
            void AddRayHit(const RE::hkpCdBody& body, const RE::hkpShapeRayCastCollectorOutput& hit) override
            {
                auto* root = &body;
                while (root->parent) {
                    root = root->parent;
                }
                auto* ref =
                    RE::TESHavokUtilities::FindCollidableRef(*static_cast<const RE::hkpCollidable*>(root));
                // People never hide loot; the follower's own body starts the ray.
                if (ref && ref->As<RE::Actor>()) {
                    return;
                }
                if (hit.hitFraction < rayHit.hitFraction) {
                    closestReference = ref ? ref->GetFormID() : 0;
                }
                RE::hkpClosestRayHitCollector::AddRayHit(body, hit);
            }
        };

        bool IsLootForm(const RE::TESBoundObject& object)
        {
            switch (object.GetFormType()) {
            case RE::FormType::Weapon:
            case RE::FormType::Armor:
            case RE::FormType::Ammo:
            case RE::FormType::Misc:
            case RE::FormType::Book:
            case RE::FormType::Ingredient:
            case RE::FormType::AlchemyItem:
            case RE::FormType::SoulGem:
            case RE::FormType::KeyMaster:
            case RE::FormType::Scroll:
                return true;
            default:
                return false;
            }
        }

        bool Playable(const RE::TESBoundObject& object)
        {
            constexpr std::uint32_t nonPlayable = 1u << 2;
            switch (object.GetFormType()) {
            case RE::FormType::Weapon:
            case RE::FormType::Armor:
            case RE::FormType::Ammo:
            case RE::FormType::Misc:
            case RE::FormType::KeyMaster:
                if (object.formFlags & nonPlayable) {
                    return false;
                }
                break;
            default:
                break;
            }
            if (const auto* weapon = object.As<RE::TESObjectWEAP>();
                weapon && weapon->weaponData.flags.any(RE::TESObjectWEAP::Data::Flag::kNonPlayable)) {
                return false;
            }
            return true;
        }

        bool HasKeyword(const RE::TESBoundObject& object, RE::FormID id)
        {
            const auto* keywords = object.As<RE::BGSKeywordForm>();
            auto* keyword = RE::TESForm::LookupByID<RE::BGSKeyword>(id);
            return keywords && keyword && keywords->HasKeyword(keyword);
        }

        Item Describe(const RE::TESBoundObject& object, int count, int value)
        {
            Item item;
            item.count = count;
            item.value = value;
            item.playable = Playable(object);
            const char* name = object.GetName();
            item.named = name && *name;
            item.coins = object.GetFormID() == GOLD;
            const auto* potion = object.As<RE::AlchemyItem>();
            item.provisions = object.Is(RE::FormType::Ingredient) || (potion && potion->IsFood()) ||
                              HasKeyword(object, ANIMAL_HIDE) || HasKeyword(object, ANIMAL_PART);
            return item;
        }

        bool Owned(RE::TESObjectREFR& ref)
        {
            if (ref.IsCrimeToActivate()) {
                return true;
            }
            // The player's own storage is not a discovery.
            const auto* owner = ref.GetOwner();
            auto* player = RE::PlayerCharacter::GetSingleton();
            return owner &&
                   ((player && owner == player->GetActorBase()) || owner->GetFormID() == PLAYER_FACTION);
        }

        bool Candidate(RE::TESObjectREFR& ref, RE::PlayerCharacter& player, const Tuning& tuning)
        {
            if (&ref == &player || ref.IsDisabled() || ref.IsDeleted() || !ref.Is3DLoaded()) {
                return false;
            }
            auto* base = ref.GetBaseObject();
            if (!base) {
                return false;
            }
            if (auto* actor = ref.As<RE::Actor>()) {
                return tuning.bodies && actor->IsDead() && !actor->IsPlayerTeammate() &&
                       !actor->IsSummoned() && !actor->IsActivationBlocked();
            }
            if (base->Is(RE::FormType::Container)) {
                return tuning.containers && !ref.IsActivationBlocked() && !Owned(ref);
            }
            if (!tuning.looseItems || !IsLootForm(*base)) {
                return false;
            }
            // Value first: rooms are full of loose plates and cups, and ownership checks cost more.
            const int count = std::max(1, static_cast<int>(ref.extraList.GetCount()));
            const int worth = base->GetFormID() == GOLD ? count : base->GetGoldValue();
            return Valuable(worth, tuning.valuableGold) && !ref.IsActivationBlocked() && !Owned(ref);
        }

        float Radius(RE::TESObjectREFR& ref)
        {
            auto* model = ref.Get3D();
            const float radius = model ? model->worldBound.radius : 0;
            return std::isfinite(radius) && radius > 0 ? std::min(radius, 200.0F) : 40.0F;
        }

        // One ray on the line-of-sight layer. People never block it. Only the find itself, or a surface
        // within a hand's width of the aim point, may stop it short; any other hit means something is
        // in the way, however close to the find it is.
        bool Clear(RE::TESObjectCELL* cell, RE::NiPoint3 from, RE::NiPoint3 to, RE::FormID target)
        {
            constexpr float handWidth = 12;
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            const float scale = RE::bhkWorld::GetWorldScale();
            const float length = from.GetDistance(to);
            if (!world || !std::isfinite(scale) || scale <= 0 || !std::isfinite(length)) {
                return false;
            }
            if (length < 1) {
                return true;
            }
            SightCollector collector;
            collector.Reset();
            RE::bhkPickData pick{};
            const auto a = from * scale, b = to * scale;
            pick.rayInput.from = RE::hkVector4{a.x, a.y, a.z, 0};
            pick.rayInput.to = RE::hkVector4{b.x, b.y, b.z, 0};
#ifdef WAYFARER_MODERN_COMMONLIB
            pick.rayInput.filterInfo.filter = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
            pick.closestRayHitCollector = &collector;
#else
            pick.rayInput.filterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
            pick.rayHitCollectorA8 = &collector;
#endif
            {
                RE::BSReadLockGuard guard(world->worldLock);
                world->PickObject(pick);
            }
#ifdef WAYFARER_MODERN_COMMONLIB
            if (pick.pickFailed) {
                return false;
            }
#else
            if (pick.unkC0) {
                return false;
            }
#endif
            if (!collector.HasHit() || collector.closestReference == target) {
                return true;
            }
            const float fraction = collector.rayHit.hitFraction;
            return std::isfinite(fraction) && (fraction >= 1 || fraction * length >= length - handWidth);
        }

        bool PlayIdle(RE::Actor& actor, RE::TESIdleForm* idle)
        {
            auto* process = actor.GetActorRuntimeData().currentProcess;
            if (!process || !idle) {
                return false;
            }
#ifdef WAYFARER_MODERN_COMMONLIB
            return process->PlayIdle(&actor, idle, nullptr);
#else
            using Function = bool (*)(RE::AIProcess*, RE::Actor*, RE::DEFAULT_OBJECT, RE::TESIdleForm*, bool,
                                      bool, RE::TESObjectREFR*);
            static REL::Relocation<Function> play{RELOCATION_ID(38290, 39256)};
            return play(process, &actor, RE::DEFAULT_OBJECT::kActionIdle, idle, true, false, nullptr);
#endif
        }

        RE::NiCamera* Camera(RE::NiAVObject* object)
        {
            if (!object) {
                return nullptr;
            }
            if (auto* camera = netimmerse_cast<RE::NiCamera*>(object)) {
                return camera;
            }
            if (auto* node = object->AsNode()) {
                for (const auto& child : node->GetChildren()) {
                    if (auto* camera = Camera(child.get())) {
                        return camera;
                    }
                }
            }
            return nullptr;
        }

        bool VisibleUI(RE::UI& ui)
        {
            if (ui.GameIsPaused() || !ui.IsShowingMenus() || Menu::HasBlockingWindow()) {
                return false;
            }
            for (const auto* name : {"Dialogue Menu", "Loading Menu", "Main Menu", "Console", "TweenMenu",
                                     "InventoryMenu", "MagicMenu", "ContainerMenu", "BarterMenu", "MapMenu",
                                     "Journal Menu", "Crafting Menu", "StatsMenu", "FavoritesMenu",
                                     "RaceSex Menu", "Book Menu", "Sleep/Wait Menu", "MessageBoxMenu"}) {
                if (ui.IsMenuOpen(name)) {
                    return false;
                }
            }
            return true;
        }

        void Publish(std::vector<Marker> value = {})
        {
            std::lock_guard lock(markerMutex);
            markers = std::move(value);
        }
    } // namespace

    void Install()
    {
        if (auto* events = RE::ScriptEventSourceHolder::GetSingleton()) {
            events->AddEventSink<RE::TESActivateEvent>(&activationSink);
        }
        logger::info("[Loot] Scouting r1: companions point out nearby containers, bodies and valuables");
    }

    std::vector<RE::ObjectRefHandle> Collect(RE::PlayerCharacter& player, float radius, const Tuning& tuning)
    {
        std::vector<RE::ObjectRefHandle> found;
        auto* tes = RE::TES::GetSingleton();
        if (!tes || !(radius > 0)) {
            return found;
        }
        auto visit = [&](RE::TESObjectREFR& ref) {
            if (Candidate(ref, player, tuning)) {
                found.push_back(ref.GetHandle());
            }
            return found.size() < 256 ? RE::BSContainer::ForEachResult::kContinue
                                      : RE::BSContainer::ForEachResult::kStop;
        };
#ifdef WAYFARER_MODERN_COMMONLIB
        tes->ForEachReferenceInRange(&player, radius, [&](RE::TESObjectREFR* ref) {
            return ref ? visit(*ref) : RE::BSContainer::ForEachResult::kContinue;
        });
#else
        tes->ForEachReferenceInRange(&player, radius, visit);
#endif
        return found;
    }

    Kind KindOf(RE::TESObjectREFR& ref)
    {
        if (ref.As<RE::Actor>()) {
            return Kind::Body;
        }
        auto* base = ref.GetBaseObject();
        return base && base->Is(RE::FormType::Container) ? Kind::Container : Kind::Item;
    }

    RE::NiPoint3 Center(RE::TESObjectREFR& ref)
    {
        if (auto* model = ref.Get3D()) {
            const auto center = model->worldBound.center;
            if (model->worldBound.radius > 0 && std::isfinite(center.x) && std::isfinite(center.y) &&
                std::isfinite(center.z)) {
                return center;
            }
        }
        auto position = ref.GetPosition();
        position.z += 20;
        return position;
    }

    bool Sees(RE::Actor& actor, RE::TESObjectREFR& ref, RE::NiPoint3 center)
    {
        auto eye = actor.GetPosition();
        eye.z += 110;
        if (auto* model = actor.Get3D()) {
            if (auto* head = model->GetObjectByName("NPC Head [Head]")) {
                eye = head->world.translate;
            }
        }
        // The middle, or the top over a low obstacle. A wall in between hides both.
        const RE::NiPoint3 top{center.x, center.y, center.z + std::min(Radius(ref) * .5F, 40.0F)};
        return Clear(actor.GetParentCell(), eye, center, ref.GetFormID()) ||
               Clear(actor.GetParentCell(), eye, top, ref.GetFormID());
    }

    bool Reaches(RE::Actor& actor, RE::TESObjectREFR& ref, RE::NiPoint3 center)
    {
        auto hand = actor.GetPosition();
        hand.z += 60;
        return Clear(actor.GetParentCell(), hand, center, ref.GetFormID());
    }

    Route Measure(const TravelNavigation& navigation, const RestReachability& area, RE::Actor& actor,
                  RE::TESObjectREFR& ref, const Target& plan)
    {
        Route route;
        if (area.distance.empty()) {
            return route; // No navmesh under the follower to judge by; the stall check still covers it.
        }
        const auto from = actor.GetPosition();
        const auto c = plan.center;
        const float ring = plan.reach + 15;
        const float toward = std::atan2(from.y - c.y, from.x - c.x);
        constexpr float eighth = std::numbers::pi_v<float> / 4;
        // Raised finds (on a table or shelf) are reached from the floor below them.
        for (const float drop : {0.0F, 70.0F}) {
            // The side facing the follower first, then around; the first spot that works is enough.
            for (int i = 0; i < 8; ++i) {
                const float angle =
                    toward + static_cast<float>((i + 1) / 2) * eighth * (i % 2 ? 1.0F : -1.0F);
                const RE::NiPoint3 spot{c.x + std::cos(angle) * ring, c.y + std::sin(angle) * ring,
                                        plan.marker.z - drop};
                const auto destination = navigation.FindRestDestination(area, spot);
                if (!destination) {
                    continue;
                }
                // A spot on the far side of the wall the find is pressed against does not count.
                const RE::NiPoint3 hand{destination->point.x, destination->point.y,
                                        destination->point.z + 60};
                if (!Clear(actor.GetParentCell(), hand, c, ref.GetFormID())) {
                    continue;
                }
                route.known = true;
                route.length = destination->routeLength;
                return route;
            }
        }
        // A complete search that found no spot means there is no way there. A search cut short by its
        // size limit proves nothing, so leave that to the stall check.
        route.known = !area.limited;
        return route;
    }

    Contents Inspect(RE::TESObjectREFR& ref, Kind kind, int threshold)
    {
        Contents contents;
        if (kind == Kind::Item) {
            if (auto* base = ref.GetBaseObject(); base && IsLootForm(*base)) {
                contents.Add(Describe(*base, std::max(1, static_cast<int>(ref.extraList.GetCount())),
                                      base->GetGoldValue()),
                             threshold);
            }
            return contents;
        }
        auto inventory = ref.GetInventory([](RE::TESBoundObject& object) { return IsLootForm(object); });
        for (const auto& [object, data] : inventory) {
            const auto& [count, entry] = data;
            if (!object || count <= 0) {
                continue;
            }
            const int value = entry ? entry->GetValue() : object->GetGoldValue();
            contents.Add(Describe(*object, count, value), threshold);
        }
        return contents;
    }

    Target Plan(RE::TESObjectREFR& ref, Kind kind, bool valuable)
    {
        Target target;
        target.ref = ref.GetHandle();
        target.id = ref.GetFormID();
        target.kind = kind;
        target.valuable = valuable;
        target.center = Center(ref);
        // The travel package stops within 65 units of its marker, so aim at the find itself: the
        // follower then ends up at its edge on whichever side they walked up to, close enough to reach in.
        target.marker = {target.center.x, target.center.y, ref.GetPosition().z};
        target.reach = kind == Kind::Body ? 55.0F : std::clamp(Radius(ref) * .5F + 20, 45.0F, 90.0F);
        return target;
    }

    bool Face(RE::Actor& actor, RE::NiPoint3 point, float dt)
    {
        constexpr float tolerance = .2F; // About 11 degrees.
        constexpr float turnRate = 4.0F; // Radians per second.
        const auto pos = actor.GetPosition();
        const float dx = point.x - pos.x, dy = point.y - pos.y;
        if (dx * dx + dy * dy < 1) {
            return true;
        }
        const float yaw = actor.GetAngleZ();
        const float difference = std::remainder(std::atan2(dx, dy) - yaw, 2 * std::numbers::pi_v<float>);
        if (std::abs(difference) <= tolerance) {
            return true;
        }
        const float step = std::clamp(dt, 0.0F, .1F) * turnRate;
        Engine::SetHeading(actor, yaw + std::clamp(difference, -step, step));
        return std::abs(difference) <= step + tolerance;
    }

    bool Search(RE::Actor& actor, const Target& target)
    {
        // Kneel at bodies and floor finds, rummage in low containers, stand at tall ones.
        const bool low = target.center.z - actor.GetPosition().z < 45;
        const RE::FormID loop = target.kind == Kind::Body        ? IDLE_SEARCH_BODY
                                : !low                           ? IDLE_SEARCHING_TABLE
                                : target.kind == Kind::Container ? IDLE_SEARCHING_CHEST
                                                                 : IDLE_SEARCH_BODY;
        if (PlayIdle(actor, RE::TESForm::LookupByID<RE::TESIdleForm>(loop))) {
            logger::info("[Loot] {} searches {:08X} with idle {:08X}", actor.GetName(), target.id, loop);
            return true;
        }
        // The idle records carry no conditions, but send the graph event directly if the AI refuses.
        const char* event = loop == IDLE_SEARCH_BODY       ? "IdleKneeling"
                            : loop == IDLE_SEARCHING_CHEST ? "IdleSearchingChest"
                                                           : "IdleSearchingTable";
        if (actor.NotifyAnimationGraph(event)) {
            logger::info("[Loot] {} searches {:08X} with graph event {}", actor.GetName(), target.id, event);
            return true;
        }
        const bool reached = PlayIdle(actor, RE::TESForm::LookupByID<RE::TESIdleForm>(IDLE_PICKUP_GROUND));
        logger::info("[Loot] {} could not start a search loop at {:08X}; {}", actor.GetName(), target.id,
                     reached ? "brief reach instead" : "waiting beside it");
        return false;
    }

    void Stand(RE::Actor& actor)
    {
        // The furniture-style exit plays the stand-up clip; IdleStop covers loops that do not take it.
        const bool exited =
            actor.NotifyAnimationGraph("IdleChairExitStart") || actor.NotifyAnimationGraph("IdleStop");
        logger::info("[Loot] {} stands up ({})", actor.GetName(),
                     exited ? "exit animation" : "no exit transition was active");
        std::erase_if(rising, [&](const auto& entry) { return entry.actor == actor.GetHandle(); });
        rising.push_back({actor.GetHandle(), riseSeconds + 1.0F});
    }

    bool IdlePlaying(RE::Actor& actor)
    {
        bool playing = false;
        return actor.GetGraphVariableBool("bIdlePlaying", playing) && playing;
    }

    std::vector<RE::FormID> ConsumeActivations()
    {
        std::lock_guard lock(activationMutex);
        return std::exchange(activations, {});
    }

    void Reset()
    {
        sights.clear();
        Publish();
    }

    void Update(float dt)
    {
        // A follower must never walk off frozen in a kneel: clear any pose the stand-up did not end.
        std::erase_if(rising, [&](auto& entry) {
            entry.remaining -= dt;
            if (entry.remaining > 0) {
                return false;
            }
            if (auto actor = entry.actor.get(); actor && actor->Is3DLoaded() && !actor->IsDead() &&
                                                !actor->IsInCombat() && IdlePlaying(*actor)) {
                actor->NotifyAnimationGraph("IdleForceDefaultState");
                logger::info("[Loot] {} was still posed after standing; cleared it", actor->GetName());
            }
            return true;
        });
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        const auto& tuning = Settings::GetSingleton().Get().loot;
        if (!Menu::IsInstalled() || !tuning.enabled || !tuning.icons || !player || !ui || !VisibleUI(*ui) ||
            player->IsDead() || !player->Is3DLoaded()) {
            Reset();
            return;
        }
        const auto scouts = FormationController::GetSingleton().LootScouts();
        if (scouts.empty()) {
            Reset();
            return;
        }
        std::erase_if(sights, [&](const auto& entry) {
            return std::none_of(scouts.begin(), scouts.end(), [&](const auto& scout) {
                auto actor = scout.actor.get();
                return actor && actor->GetFormID() == entry.first;
            });
        });
        auto* cameraOwner = RE::PlayerCamera::GetSingleton();
        auto* camera = Camera(cameraOwner ? cameraOwner->cameraRoot.get() : nullptr);
        std::vector<Marker> frame;
        for (const auto& scout : scouts) {
            auto actor = scout.actor.get();
            if (!camera || !actor || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() ||
                !InSameTravelSpace(*actor, *player)) {
                continue;
            }
            auto& sight = sights[actor->GetFormID()];
            sight.delay -= dt;
            if (sight.delay <= 0) {
                bool unused{};
                sight.visible = player->HasLineOfSight(actor.get(), unused);
                sight.delay = .25F;
            }
            if (!sight.visible) {
                continue;
            }
            auto point = actor->GetLookingAtLocation();
            if (auto* model = actor->Get3D()) {
                if (auto* head = model->GetObjectByName("NPC Head [Head]")) {
                    point = head->world.translate;
                }
            }
            point.z += 24.0F;
            float x{}, y{}, z{};
            if (RE::NiCamera::WorldPtToScreenPt3(camera->GetRuntimeData().worldToCam,
                                                 camera->GetRuntimeData2().port, point, x, y, z, 1e-5F) &&
                std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && z > 0 && x > .02F && x < .98F &&
                y > .03F && y < .97F) {
                frame.push_back({x, 1.0F - y, scout.valuable, IconAlpha(scout.elapsed), actor->GetFormID()});
            }
        }
        Publish(std::move(frame));
    }

    std::vector<Marker> ReadMarkers()
    {
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !VisibleUI(*ui)) {
            return {};
        }
        std::lock_guard lock(markerMutex);
        return markers;
    }
} // namespace Wayfarer::Loot
