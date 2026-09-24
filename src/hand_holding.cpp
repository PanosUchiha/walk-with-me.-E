#include "command_gesture.h"
#include "engine_compatibility.h"
#include "formation_math.h"
#include "hand_hold_attachment.h"
#include "hand_hold_ik.h"
#include "hand_hold_overrides.h"
#include "hand_hold_pose.h"
#include "hand_holding_backends.h"

namespace Wayfarer::HandHolding::Classic
{
    namespace
    {
        State state;
        Tuning tuning;
        Approach approach;
        std::recursive_mutex mutex;
        std::atomic<RE::FormID> renderPartner{};
        std::atomic<bool> rendering{};
        std::atomic<RE::FormID> attachedActor{};
        std::atomic<RE::FormID> locomotionActor{};
        Attachment attachment;
        SeekMotion seekMotion;
        SeekGait seekGait;
        RE::NiPoint3 seekLastPosition{};
        bool haveSeekPosition{};
        std::uint32_t seekUpdates{};
        float reachFailureTime{}, traceTime{}, seekTraceTime{};
        RE::TESObjectCELL* lastCell{};
        RE::TESWorldSpace* lastWorld{};
        RE::NiPoint3 lastPlayer{}, grip{}, forward{0, 1, 0}, right{1, 0, 0};
        bool havePosition{}, haveGrip{}, poseFailed{};
        std::uint32_t playerPosePasses{}, partnerPosePasses{}, partnerUpdatePasses{}, channelPasses{};
        float nativeDirection{}, nativeTurnDelta{};
        float writtenPhysicsSpeed{};
        std::uint32_t visualUpdates{};
        struct CollisionLease
        {
            RE::NiPointer<RE::bhkCharacterController> controller;
            CollisionOwnership ownership;
            void Restore()
            {
                if (ownership.Release() && controller) {
                    controller->flags.reset(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
                }
                controller.reset();
            }
            bool Acquire(RE::Actor& actor)
            {
                auto* current = actor.GetCharController();
                if (current != controller.get()) {
                    Restore();
                    if (!current) {
                        return false;
                    }
                    controller.reset(current);
                    ownership.Acquire(current->flags.any(RE::CHARACTER_FLAGS::kNoCharacterCollisions));
                }
                if (!current) {
                    return false;
                }
                current->flags.set(RE::CHARACTER_FLAGS::kNoCharacterCollisions);
                return true;
            }
        } collision;
        struct GraphSpeedLease
        {
            const char* name{"Speed"};
            RE::ActorHandle actor;
            float before{}, applied{};
            bool edited{};
            void Restore()
            {
                if (edited) {
                    if (auto owner = actor.get()) {
                        float current{};
                        if (owner->GetGraphVariableFloat(name, current) && current == applied) {
                            owner->SetGraphVariableFloat(name, before);
                        }
                    }
                }
                edited = false;
                actor = {};
            }
            void Apply(RE::Actor& owner, float speed)
            {
                Restore();
                if (!owner.GetGraphVariableFloat(name, before)) {
                    return;
                }
                applied = speed;
                edited = owner.SetGraphVariableFloat(name, applied);
                if (edited) {
                    actor = owner.GetHandle();
                }
            }
        } graphSpeedLease, graphDirectionLease{"Direction"}, graphTurnLease{"TurnDelta"};

        IK::Point P(RE::NiPoint3 p)
        {
            return {p.x, p.y, p.z};
        }
        RE::NiPoint3 N(IK::Point p)
        {
            return {p.x, p.y, p.z};
        }
        RE::NiPoint3 N(const RE::hkVector4& p)
        {
            alignas(16) float values[4];
            _mm_store_ps(values, p.quad);
            return {values[0], values[1], values[2]};
        }
        RE::NiPoint3 Unit(RE::NiPoint3 p, RE::NiPoint3 fallback = {1, 0, 0})
        {
            return N(IK::Unit(P(p), P(fallback)));
        }

        void UpdateVisualPosition(RE::Actor& actor)
        {
            // Actor::Update3DPosition clears velocity; use the base scene update.
            using Function = void (*)(RE::TESObjectREFR*, bool);
            static const auto update = []() {
                REL::Relocation<std::uintptr_t> table{RE::VTABLE_TESObjectREFR[0]};
                return reinterpret_cast<Function*>(table.address())[0x3F];
            }();
            update(&actor, false);
            ++visualUpdates;
        }

        bool MovePartner(RE::Actor& actor, RE::NiPoint3 destination, std::optional<IK::Point> velocity = {})
        {
            auto* controller = actor.GetCharController();
            const float scale = RE::bhkWorld::GetWorldScale();
            if (!controller || !std::isfinite(scale) || scale <= 0) {
                return false;
            }
            RE::hkVector4 nativeVelocity{};
            controller->GetLinearVelocityImpl(nativeVelocity);
            if (!IK::Finite(P(destination)) || !IK::Finite(P(N(nativeVelocity)))) {
                return false;
            }

            const RE::hkVector4 position{destination.x * scale, destination.y * scale, destination.z * scale,
                                         0};
            controller->SetPositionImpl(position, true, false);
            actor.SetPosition(destination, false);
            if (velocity) {
                const auto current = N(nativeVelocity);
                nativeVelocity = RE::hkVector4{velocity->x * scale, velocity->y * scale, current.z, 0};
            }

            // Position writes clear velocity, so restore it last.
            controller->SetLinearVelocityImpl(nativeVelocity);
            RE::hkVector4 written{};
            controller->GetLinearVelocityImpl(written);
            const auto measured = N(written);
            writtenPhysicsSpeed = std::hypot(measured.x, measured.y) / scale;
            return true;
        }

        bool Attached(RE::NiAVObject* node, RE::NiAVObject* root)
        {
            for (int i = 0; node && i < 64; ++i, node = node->parent) {
                if (node == root) {
                    return true;
                }
            }
            return false;
        }

        void World(RE::NiAVObject* node, int depth = 0)
        {
            if (!node || depth > 48) {
                return;
            }
            RE::NiUpdateData data{};
            data.flags.set(RE::NiUpdateData::Flag::kDirty, RE::NiUpdateData::Flag::kDisableCollision);
            node->UpdateWorldData(&data);
            if (auto* branch = node->AsNode()) {
#ifdef WAYFARER_MODERN_COMMONLIB
                for (auto& child : branch->children) {
                    World(child.get(), depth + 1);
                }
#else
                for (auto& child : branch->GetChildren()) {
                    World(child.get(), depth + 1);
                }
#endif
            }
        }
        struct Bone
        {
            RE::NiPointer<RE::NiAVObject> node;
            RE::NiMatrix3 before, applied;
            bool edited{};
            void Capture()
            {
                before = node->local.rotate;
                edited = true;
            }
            void Finish() { applied = node->local.rotate; }
        };
        struct Arm
        {
            RE::NiPointer<RE::NiAVObject> root;
            RE::NiPointer<RE::NiNode> boneTree;
            Bone upper, lower, hand;
            std::array<Bone, 12> fingers;
            bool left{};
            RE::ActorHandle actor;
            void Restore(RE::Actor* owner)
            {
                if (!root || !owner || owner->Get3D(false) != root.get()) {
                    *this = {};
                    return;
                }
                bool changed = false;
                auto restore = [&](Bone& bone) {
                    if (bone.edited && bone.node && Attached(bone.node.get(), root.get()) &&
                        bone.node->local.rotate == bone.applied) {
                        bone.node->local.rotate = bone.before;
                        changed = true;
                    }
                    bone.edited = false;
                };
                restore(upper);
                restore(lower);
                restore(hand);
                for (auto& bone : fingers) {
                    restore(bone);
                }
                if (changed && upper.node && Attached(upper.node.get(), root.get())) {
                    World(upper.node.get());
                }
            }
            bool Bind(RE::Actor& owner, bool useLeft)
            {
                auto* current = owner.Get3D(false);
                if (current == root.get() && left == useLeft && upper.node && lower.node && hand.node &&
                    Attached(upper.node.get(), current) && Attached(lower.node.get(), upper.node.get()) &&
                    Attached(hand.node.get(), lower.node.get())) {
                    return true;
                }
                Restore(&owner);
                *this = {};
                if (!current) {
                    return false;
                }
                root.reset(current);
                left = useLeft;
                actor = owner.GetHandle();
                upper.node.reset(
                    current->GetObjectByName(useLeft ? "NPC L UpperArm [LUar]" : "NPC R UpperArm [RUar]"));
                lower.node.reset(
                    current->GetObjectByName(useLeft ? "NPC L Forearm [LLar]" : "NPC R Forearm [RLar]"));
                hand.node.reset(
                    current->GetObjectByName(useLeft ? "NPC L Hand [LHnd]" : "NPC R Hand [RHnd]"));
                if (!upper.node || !lower.node || !hand.node ||
                    !Attached(lower.node.get(), upper.node.get()) ||
                    !Attached(hand.node.get(), lower.node.get())) {
                    *this = {};
                    return false;
                }
                for (int finger = 1; finger <= 4; ++finger) {
                    for (int joint = 0; joint < 3; ++joint) {
                        const auto name = fmt::format("NPC {} Finger{}{} [{}F{}{}]", useLeft ? "L" : "R",
                                                      finger, joint, useLeft ? "L" : "R", finger, joint);
                        fingers[(finger - 1) * 3 + joint].node.reset(current->GetObjectByName(name.c_str()));
                    }
                }
                return true;
            }
            float Upper() const
            {
                return (lower.node->world.translate - upper.node->world.translate).Length();
            }
            float Lower() const
            {
                return (hand.node->world.translate - lower.node->world.translate).Length();
            }
        };
        Arm playerArm, followerArm;
        void SyncBoneCache(Arm& arm);

        void ClearPose()
        {
            collision.Restore();
            graphSpeedLease.Restore();
            graphDirectionLease.Restore();
            graphTurnLease.Restore();
            locomotionActor.store(0, std::memory_order_release);
            seekMotion.Reset();
            seekGait.Reset();
            haveSeekPosition = false;
            seekUpdates = 0;
            approach.closing = false;
            approach.velocity = {};
            attachedActor.store(0, std::memory_order_release);
            attachment.Reset();
            reachFailureTime = traceTime = 0;
            rendering.store(false, std::memory_order_release);
            renderPartner.store(0, std::memory_order_release);
            auto p = playerArm.actor.get();
            playerArm.Restore(p.get());
            SyncBoneCache(playerArm);
            auto f = followerArm.actor.get();
            followerArm.Restore(f.get());
            SyncBoneCache(followerArm);
            playerArm = {};
            followerArm = {};
            haveGrip = false;
            poseFailed = false;
            channelPasses = 0;
            nativeDirection = nativeTurnDelta = 0;
        }
        const char* Rejection(RE::Actor& actor)
        {
            const auto* s = actor.AsActorState();
            if (s && s->IsSprinting()) {
                return "sprinting";
            }
            if (!s || !actor.Is3DLoaded() || actor.IsDisabled() || actor.IsDead() || actor.IsInCombat() ||
                actor.IsInKillMove() || actor.IsInRagdollState() || actor.IsInMidair() ||
                actor.IsSneaking() || actor.IsOnMount() || actor.GetCurrentScene() || s->IsSwimming() ||
                s->IsFlying() || s->IsSprinting() || s->IsWeaponDrawn() ||
                s->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) {
                return "actor unavailable or busy";
            }
            bool driven = false, offset = false;
            actor.GetGraphVariableBool("bAnimationDriven", driven);
            actor.GetGraphVariableBool("bOffsetGPMA", offset);
            return driven || offset ? "another arm animation" : nullptr;
        }
        bool Busy(RE::Actor& actor)
        {
            return Rejection(actor) != nullptr;
        }
        void Drop(const char* reason, bool immediate = false)
        {
            if (state.GetPhase() == Phase::Holding) {
                logger::info("[HandHolding] Released {:08X}: {}", state.Partner(), reason);
            }
            collision.Restore();
            graphSpeedLease.Restore();
            graphDirectionLease.Restore();
            graphTurnLease.Restore();
            locomotionActor.store(0, std::memory_order_release);
            seekMotion.Reset();
            seekGait.Reset();
            haveSeekPosition = false;
            seekUpdates = 0;
            attachedActor.store(0, std::memory_order_release);
            attachment.Reset();
            approach.actor = 0;
            if (immediate) {
                ClearPose();
                state.Reset();
            }
            else {
                state.Release();
            }
        }
        class PairRayCollector final : public RE::hkpClosestRayHitCollector
        {
          public:
            RE::TESObjectREFR* player{};
            RE::TESObjectREFR* follower{};
            void AddRayHit(const RE::hkpCdBody& body, const RE::hkpShapeRayCastCollectorOutput& hit) override
            {
                const auto* root = &body;
                while (root->parent) {
                    root = root->parent;
                }
                auto* ref =
                    RE::TESHavokUtilities::FindCollidableRef(*static_cast<const RE::hkpCollidable*>(root));

                if (ref && (ref == player || ref == follower)) {
                    return;
                }
                RE::hkpClosestRayHitCollector::AddRayHit(body, hit);
            }
        };
        const char* StepRejection(RE::Actor& actor, RE::NiPoint3 from, RE::NiPoint3 to)
        {
            if ((to - from).Length() < .05F) {
                return nullptr;
            }
            auto* cell = actor.GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            const float scale = RE::bhkWorld::GetWorldScale();
            if (!world || !std::isfinite(scale) || scale <= 0) {
                return "collision world unavailable";
            }

            for (float height : {35.0F, 85.0F}) {
                for (float side : {-18.0F, 0.0F, 18.0F}) {
                    const auto offset = right * side + RE::NiPoint3{0, 0, height};
                    const auto a = (from + offset) * scale, b = (to + offset) * scale;
                    RE::bhkPickData pick{};
                    PairRayCollector collector;
                    collector.Reset();
                    collector.player = RE::PlayerCharacter::GetSingleton();
                    collector.follower = &actor;
                    pick.rayInput.from = RE::hkVector4{a.x, a.y, a.z, 0};
                    pick.rayInput.to = RE::hkVector4{b.x, b.y, b.z, 0};
#ifdef WAYFARER_MODERN_COMMONLIB
                    pick.rayInput.filterInfo.filter =
                        static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) | (1u << 16);
                    pick.closestRayHitCollector = &collector;
#else
                    pick.rayInput.filterInfo = static_cast<std::uint32_t>(RE::COL_LAYER::kLOS) | (1u << 16);
                    pick.rayHitCollectorA8 = &collector;
#endif
                    world->PickObject(pick);
#ifdef WAYFARER_MODERN_COMMONLIB
                    if (pick.pickFailed) {
                        return "collision query failed";
                    }
#else
                    if (pick.unkC0) {
                        return "collision query failed";
                    }
#endif
                    if (!std::isfinite(collector.rayHit.hitFraction)) {
                        return "invalid collision result";
                    }
                    if (collector.HasHit() && collector.rayHit.hitFraction < .99F) {
                        return "movement obstacle";
                    }
                }
            }
            return nullptr;
        }
        RE::NiMatrix3 Native(const IK::Matrix& matrix)
        {
            RE::NiMatrix3 result;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    result.entry[i][j] = matrix.m[i][j];
                }
            }
            return result;
        }
        IK::Matrix Portable(const RE::NiMatrix3& matrix)
        {
            IK::Matrix result;
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    result.m[i][j] = matrix.entry[i][j];
                }
            }
            return result;
        }
        RE::NiMatrix3 Between(RE::NiPoint3 from, RE::NiPoint3 to)
        {
            from = Unit(from);
            to = Unit(to);
            const float dot = std::clamp(from.Dot(to), -1.0F, 1.0F);
            RE::NiMatrix3 rotation;
            if (dot > .99999F) {
                return rotation;
            }
            auto axis = from.Cross(to);
            if (axis.Length() < 1e-5F) {
                axis = from.Cross(std::abs(from.z) < .9F ? RE::NiPoint3{0, 0, 1} : RE::NiPoint3{0, 1, 0});
            }
            return Native(IK::Rotation(std::acos(dot), P(axis)));
        }
        RE::NiMatrix3 Blend(const RE::NiMatrix3& a, const RE::NiMatrix3& b, float t)
        {
            return Native(IK::BlendRotation(Portable(a), Portable(b), t));
        }
        void RotateWorld(Bone& bone, const RE::NiMatrix3& desired)
        {
            if (!bone.node->parent) {
                return;
            }
            bone.Capture();
            bone.node->local.rotate = bone.node->parent->world.rotate.Transpose() * desired;
            bone.Finish();
            World(bone.node.get());
        }
        RE::NiPoint3 WristTarget(bool player)
        {

            const float side = static_cast<float>(approach.side) * (player ? -1.0F : 1.0F);
            return grip + right * (side * 2.0F);
        }
        bool Reachable(const Arm& arm, RE::NiPoint3 target)
        {
            const auto shoulder = arm.upper.node->world.translate;
            const float upper = arm.Upper(), lower = arm.Lower();
            const float gap = (target - shoulder).Length();
            return std::isfinite(gap) && gap < (upper + lower) * .97F &&
                   IK::Solve(P(shoulder), upper, lower, P(target), P(shoulder - forward * 30)).has_value();
        }
        void Apply(Arm& arm, bool player)
        {
            const float weight = state.Weight();
            if (weight <= 0 || !arm.upper.node || !arm.lower.node || !arm.hand.node) {
                return;
            }
            const auto shoulder = arm.upper.node->world.translate;
            const auto elbow = arm.lower.node->world.translate;
            const auto hand = arm.hand.node->world.translate;
            auto target = hand + (WristTarget(player) - hand) * weight;
            const float upper = arm.Upper(), lower = arm.Lower(), limit = (upper + lower) * .98F;
            const auto reach = target - shoulder;

            if (reach.Length() > limit) {
                target = shoulder + Unit(reach) * limit;
            }
            const auto pole = elbow + ((shoulder - forward * 30) - elbow) * weight;
            const auto solved = IK::Solve(P(shoulder), upper, lower, P(target), P(pole));
            if (!solved) {
                poseFailed = true;
                return;
            }
            RotateWorld(arm.upper, Between(elbow - shoulder, N(solved->elbow) - shoulder) *
                                       arm.upper.node->world.rotate);
            const auto newElbow = arm.lower.node->world.translate;
            RotateWorld(arm.lower, Between(arm.hand.node->world.translate - newElbow, target - newElbow) *
                                       arm.lower.node->world.rotate);

            auto* index = arm.fingers[0].node.get();
            auto* pinky = arm.fingers[9].node.get();
            if (index && pinky && Attached(index, arm.hand.node.get()) &&
                Attached(pinky, arm.hand.node.get())) {
                const auto along =
                    (index->world.translate + pinky->world.translate) * .5F - arm.hand.node->world.translate;
                const auto across = index->world.translate - pinky->world.translate;
                const auto desiredNormal =
                    right * (static_cast<float>(approach.side) * (player ? 1.0F : -1.0F));
                const auto alignment = IK::WristAlignment(P(along), P(across), arm.left, P(desiredNormal));
                if (!alignment) {
                    return;
                }
                const auto desired = Native(*alignment) * arm.hand.node->world.rotate;
                RotateWorld(arm.hand, Blend(arm.hand.node->world.rotate, desired, weight));

                for (auto& bone : arm.fingers) {
                    if (!bone.node || !Attached(bone.node.get(), arm.hand.node.get())) {
                        continue;
                    }
                    const auto curlAxis = Unit(RE::NiPoint3{0, 0, -1}.Cross(desiredNormal), forward);
                    const auto curl = Native(IK::Rotation(.38F * weight, P(curlAxis)));
                    RotateWorld(bone, curl * bone.node->world.rotate);
                }
            }
        }

        void SyncBoneCache(Arm& arm)
        {
            if (!arm.boneTree || !arm.upper.node || !Attached(arm.upper.node.get(), arm.boneTree.get()) ||
                !Attached(arm.boneTree.get(), arm.root.get())) {
                return;
            }
#ifdef WAYFARER_MODERN_COMMONLIB
            auto& data = static_cast<RE::BSFlattenedBoneTree*>(arm.boneTree.get())->GetRuntimeData();
#else

            struct Entry
            {
                RE::NiTransform local, world;
                std::int16_t parentIndex, unknown;
                std::uint16_t childCount;
                std::int16_t nextSiblingIndex;
                RE::NiAVObject* node;
                RE::BSFixedString nodeName;
            };
            struct Data
            {
                std::uint32_t numBones, numPopulatedBones;
                Entry* boneEntries;
            };
            static_assert(sizeof(Entry) == 0x80);
            auto& data = REL::RelocateMember<Data>(arm.boneTree.get(), 0x128, 0x150);
#endif
            SynchronizePoseCache(data.boneEntries, data.numBones, [&](auto& bone) {
                if (bone.node && Attached(bone.node, arm.upper.node.get())) {
                    bone.local = bone.node->local;
                    bone.world = bone.node->world;
                    return true;
                }
                return false;
            });
        }
        bool RenderActor(RE::Actor* actor)
        {
            return actor && rendering.load(std::memory_order_acquire) &&
                   (actor->IsPlayerRef() ||
                    actor->GetFormID() == renderPartner.load(std::memory_order_acquire));
        }
        bool LocomotionActor(RE::Actor* actor)
        {
            return actor && actor->GetFormID() == locomotionActor.load(std::memory_order_acquire);
        }
        void DriveLocomotion(RE::Actor& actor)
        {
            if (!LocomotionActor(&actor) || Busy(actor)) {
                return;
            }
            const bool holding = state.GetPhase() == Phase::Holding;
            if (!holding && (!state.Searching() || !seekMotion.Active())) {
                return;
            }
            const auto velocity = holding ? attachment.Velocity() : seekMotion.Velocity();
            graphSpeedLease.Apply(actor, PoseSpeed(std::hypot(velocity.x, velocity.y)));

            if (holding) {
                graphDirectionLease.Apply(actor, 0);
                graphTurnLease.Apply(actor, 0);
            }
            else {
                graphDirectionLease.Restore();
                graphTurnLease.Restore();
            }
        }
        void AlignAttached(RE::Actor& actor)
        {
            if (state.GetPhase() != Phase::Holding || !LocomotionActor(&actor) || Busy(actor)) {
                return;
            }
            Engine::SetHeading(actor, attachment.Yaw());
            UpdateVisualPosition(actor);
        }
        struct PostChannelHook
        {
            static inline REL::Relocation<void (*)(RE::IPostAnimationChannelUpdateFunctor*)> original;
            static void Thunk(RE::IPostAnimationChannelUpdateFunctor* self)
            {
                original(self);
                if (!rendering.load(std::memory_order_acquire)) {
                    return;
                }

                std::unique_lock lock(mutex, std::try_to_lock);
                if (!lock.owns_lock() || state.GetPhase() != Phase::Holding) {
                    return;
                }
                auto actor = followerArm.actor.get();
                if (!actor || actor->AsIPostAnimationChannelUpdateFunctor() != self ||
                    !LocomotionActor(actor.get()) || Busy(*actor)) {
                    return;
                }
                actor->GetGraphVariableFloat("Direction", nativeDirection);
                actor->GetGraphVariableFloat("TurnDelta", nativeTurnDelta);
                DriveLocomotion(*actor);
                ++channelPasses;
            }
            static void Install()
            {

                REL::Relocation<std::uintptr_t> table{RE::VTABLE_Character[9]};
                original = table.write_vfunc(0x1, Thunk);
            }
        };
        void RefreshPairPose()
        {
            auto player = playerArm.actor.get(), follower = followerArm.actor.get();
            if (!haveGrip || !player || !follower || !RenderActor(player.get()) || Busy(*player) ||
                Busy(*follower)) {
                return;
            }
            if (playerArm.root.get() != player->Get3D(false) ||
                followerArm.root.get() != follower->Get3D(false) || !playerArm.upper.node ||
                !followerArm.upper.node) {
                return;
            }
            playerArm.Restore(player.get());
            followerArm.Restore(follower.get());
            World(playerArm.upper.node.get());
            World(followerArm.upper.node.get());
            if (state.GetPhase() == Phase::Holding) {
                auto target =
                    (playerArm.upper.node->world.translate + followerArm.upper.node->world.translate) * .5F;
                const float a = playerArm.Upper() + playerArm.Lower(),
                            b = followerArm.Upper() + followerArm.Lower();
                target.z -= std::min(a, b) * .76F;
                target += forward * 4;
                const auto offset = right * (static_cast<float>(approach.side) * 2);
                if (auto shared = IK::SharedTarget(
                        P(playerArm.upper.node->world.translate + offset), a * .94F,
                        P(followerArm.upper.node->world.translate - offset), b * .94F, P(target))) {
                    grip = N(*shared);
                }
            }
            Apply(playerArm, true);
            Apply(followerArm, false);
            SyncBoneCache(playerArm);
            SyncBoneCache(followerArm);
        }
        template <std::size_t Slot, bool Flattened = true> struct BonePassHook
        {
            static inline REL::Relocation<void (*)(RE::NiNode*, RE::NiUpdateData&, std::uint32_t)> original;
            static void Thunk(RE::NiNode* tree, RE::NiUpdateData& data, std::uint32_t flags)
            {
                original(tree, data, flags);
                if (!rendering.load(std::memory_order_acquire)) {
                    return;
                }
                std::lock_guard lock(mutex);
                if (!rendering.load(std::memory_order_acquire)) {
                    return;
                }
                bool relevant = false;
                for (auto* arm : {&playerArm, &followerArm}) {
                    if (arm->upper.node && Attached(arm->upper.node.get(), tree) &&
                        Attached(tree, arm->root.get())) {
                        if constexpr (Flattened) {
                            arm->boneTree.reset(tree);
                        }
                        relevant = true;
                        if (arm == &playerArm) {
                            ++playerPosePasses;
                        }
                        else {
                            ++partnerPosePasses;
                        }
                    }
                }
                if (relevant) {
                    RefreshPairPose();
                }
            }
            static void Install()
            {
                REL::Relocation<std::uintptr_t> table{Flattened ? RE::VTABLE_BSFlattenedBoneTree[0]
                                                                : RE::VTABLE_BSFadeNode[0]};
                original = table.write_vfunc(Slot, Thunk);
            }
        };
        struct PartnerUpdateHook
        {
            static inline REL::Relocation<void (*)(RE::Actor*, float)> original;
            static void Thunk(RE::Actor* actor, float dt)
            {
                if (LocomotionActor(actor)) {
                    std::lock_guard lock(mutex);
                    DriveLocomotion(*actor);
                }
                original(actor, dt);
                if (!LocomotionActor(actor)) {
                    return;
                }
                std::lock_guard lock(mutex);
                if (!LocomotionActor(actor) || Busy(*actor)) {
                    return;
                }
                if (state.Searching() && seekMotion.Active()) {
                    if (graphSpeedLease.edited && dt > 0) {
                        ++seekUpdates;
                    }
                    Engine::SetHeading(*actor, seekMotion.Heading());
                    UpdateVisualPosition(*actor);
                    DriveLocomotion(*actor);
                    return;
                }
                if (state.GetPhase() != Phase::Holding) {
                    return;
                }
                ++partnerUpdatePasses;

                Engine::SetHeading(*actor, attachment.Yaw());
                UpdateVisualPosition(*actor);
                DriveLocomotion(*actor);
                RefreshPairPose();
            }
            static void Install()
            {
                REL::Relocation<std::uintptr_t> table{RE::VTABLE_Character[0]};
                original = table.write_vfunc(0xAD, Thunk);
            }
        };

        template <bool Player> struct AnimationHook
        {
            static inline REL::Relocation<void (*)(RE::Actor*, float)> original;
            static void Thunk(RE::Actor* actor, float dt)
            {
                if constexpr (!Player) {
                    if (LocomotionActor(actor)) {
                        std::lock_guard lock(mutex);
                        AlignAttached(*actor);
                        DriveLocomotion(*actor);
                    }
                }
                if (RenderActor(actor)) {
                    std::lock_guard lock(mutex);
                    (Player ? playerArm : followerArm).Restore(actor);
                }
                original(actor, dt);
                if (!RenderActor(actor)) {
                    return;
                }
                std::lock_guard lock(mutex);
                if (!RenderActor(actor)) {
                    return;
                }
                if (Busy(*actor)) {
                    poseFailed = true;
                    return;
                }
                auto& arm = Player ? playerArm : followerArm;
                if (!arm.root) {
                    poseFailed = true;
                    return;
                }
                const bool left = Player ? approach.side < 0 : approach.side > 0;
                if (!arm.Bind(*actor, left)) {
                    poseFailed = true;
                    return;
                }
                if constexpr (!Player) {
                    AlignAttached(*actor);
                }
                World(arm.upper.node.get());
                RefreshPairPose();
            }
            static void Install()
            {
                REL::Relocation<std::uintptr_t> table{Player ? RE::VTABLE_PlayerCharacter[0]
                                                             : RE::VTABLE_Character[0]};
                original = table.write_vfunc(0x7D, Thunk);
            }
        };
    } // namespace

    std::string ReferenceKey(RE::Actor& actor)
    {

        if ((actor.GetFormID() >> 24) == 0xFF) {
            return {};
        }
        auto* file = actor.GetFile(0);
        if (!file) {
            return {};
        }
        auto key = fmt::format("{}|{:06X}", file->GetFilename(),
                               actor.GetFormID() & (file->IsLight() ? 0xFFF : 0xFFFFFF));
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }
    void Install()
    {
        AnimationHook<true>::Install();
        AnimationHook<false>::Install();
        PartnerUpdateHook::Install();
        PostChannelHook::Install();
        BonePassHook<0x2C>::Install();
        BonePassHook<0x2D>::Install();
        BonePassHook<0x2E>::Install();
        BonePassHook<0x2C, false>::Install();
        BonePassHook<0x2D, false>::Install();
        BonePassHook<0x2E, false>::Install();
        logger::info("[HandHolding] Revision 11.2: revision 11 pose and attachment; scene-only refresh "
                     "preserves controller velocity");
    }
    void Reset()
    {
        std::lock_guard lock(mutex);
        if (state.GetPhase() == Phase::Holding) {
            logger::info("[HandHolding] Released {:08X}: travel controller reset", state.Partner());
        }
        ClearPose();
        state.Reset();
        approach = {};
        seekTraceTime = 0;
        havePosition = false;
        lastCell = nullptr;
        lastWorld = nullptr;
    }
    void ReleaseActor(RE::FormID actor)
    {
        std::lock_guard lock(mutex);
        if (actor && (approach.actor == actor || state.Partner() == actor)) {
            Drop("follower travel released", true);
        }
    }
    bool BeginFrame(float dt, bool permitted, bool paused, const Tuning& value)
    {
        std::lock_guard lock(mutex);
        const bool changed =
            value.partner != tuning.partner || value.enabled != tuning.enabled || value.delay != tuning.delay;
        tuning = value;
        if (changed || !permitted || !value.enabled || value.partner.empty()) {
            ClearPose();
            approach = {};
        }
        state.Tick(dt, permitted, paused, tuning);
        if (state.GetPhase() == Phase::Waiting) {
            ClearPose();
            approach = {};
        }
        if (paused) {
            return false;
        }
        return permitted && (state.Searching() || state.GetPhase() == Phase::Holding ||
                             state.GetPhase() == Phase::Releasing);
    }
    void UpdatePartner(RE::PlayerCharacter& player, RE::Actor* partner, float dt,
                       const TravelNavigation& navigation)
    {
        std::lock_guard lock(mutex);
        const auto pos = player.GetPosition();
        auto* cell = player.GetParentCell();
        RE::NiPoint3 nativePlayerVelocity{};
        player.GetLinearVelocity(nativePlayerVelocity);
        auto playerVelocity = havePosition && std::isfinite(dt) && dt > 0 && dt <= .25F
                                  ? P(pos - lastPlayer) * (1 / dt)
                                  : P(nativePlayerVelocity);
        playerVelocity.z = 0;
        const bool transition =
            havePosition && (lastWorld != player.GetWorldspace() ||
                             (lastCell != cell && ((cell && cell->IsInteriorCell()) ||
                                                   (lastCell && lastCell->IsInteriorCell()))) ||
                             (pos - lastPlayer).Length() > 300);
        lastWorld = player.GetWorldspace();
        lastCell = cell;
        lastPlayer = pos;
        havePosition = true;
        auto* camera = RE::PlayerCamera::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        auto* controls = RE::ControlMap::GetSingleton();
        const bool firstPerson = !camera || camera->IsInFirstPerson();
        const auto* rejection = Rejection(player);
        if (transition || firstPerson || rejection || !controls || !controls->IsMovementControlsEnabled() ||
            (ui && ui->IsMenuOpen(RE::DialogueMenu::MENU_NAME)) || CommandGesture::SavedOwnership()) {
            Drop(rejection     ? rejection
                 : transition  ? "load or teleport"
                 : firstPerson ? "first person"
                               : "controls, dialogue or gesture",
                 true);
            return;
        }
        if (state.GetPhase() == Phase::Releasing) {

            if (poseFailed || !partner || Busy(*partner)) {
                Drop("release interrupted", true);
            }
            return;
        }
        if (!partner || Busy(*partner)) {
            if (state.GetPhase() == Phase::Holding) {
                Drop(partner ? Rejection(*partner) : "selected companion unavailable", true);
            }
            else {
                ClearPose();
                approach = {};
            }
            return;
        }
        const bool holding = state.GetPhase() == Phase::Holding;
        if (holding && (playerArm.root.get() != player.Get3D(false) ||
                        followerArm.root.get() != partner->Get3D(false))) {
            Drop("skeleton changed", true);
            return;
        }
        float correction{};
        if (holding) {
            if (!collision.Acquire(*partner)) {
                Drop("character controller unavailable", true);
                return;
            }
            if ((partner->GetPosition() - pos).Length() > tuning.releaseDistance) {
                Drop("separated beyond release distance");
                return;
            }
            const auto desired =
                attachment.Advance(dt, P(pos), player.GetAngleZ(), player.AsActorState()->IsSprinting());
            if (!desired) {
                Drop("attachment timing or movement discontinuity", true);
                return;
            }
            approach.velocity = attachment.Velocity();
            approach.heading = attachment.Yaw();
            forward = N(attachment.Forward());
            right = N(attachment.Right());
            const auto from = partner->GetPosition();
            const auto grounded = navigation.AttachmentPoint(from, N(*desired));
            if (!grounded) {
                logger::info("[HandHolding] Ground rejected: from=({:.2f},{:.2f},{:.2f}) "
                             "desired=({:.2f},{:.2f},{:.2f}) step={:.3f}",
                             from.x, from.y, from.z, desired->x, desired->y, desired->z,
                             IK::Length(*desired - P(from)));
                Drop("attachment left reachable ground");
                return;
            }
            correction = (*grounded - from).Length();
            if (correction > 96) {
                Drop("attachment correction too large");
                return;
            }
            if (const auto* blocked = StepRejection(*partner, from, *grounded)) {
                Drop(blocked);
                return;
            }

            if (!MovePartner(*partner, *grounded, attachment.Velocity())) {
                Drop("character controller unavailable", true);
                return;
            }
            Engine::SetHeading(*partner, attachment.Yaw());
            UpdateVisualPosition(*partner);
        }
        else {
            const auto f = ForwardFromYaw(player.GetAngleZ());
            forward = {f.x, f.y, 0};
            right = {f.y, -f.x, 0};
        }
        if (state.Searching() && approach.actor != partner->GetFormID()) {
            seekMotion.Reset();
            seekGait.Reset();
            haveSeekPosition = false;
            seekUpdates = 0;
            approach = {partner->GetFormID(), (partner->GetPosition() - pos).Dot(right) < 0 ? -1 : 1,
                        SeekSpacing(tuning.connectDistance)};
            seekTraceTime = 2;
            logger::info("[HandHolding] Seeking {} ({:08X}); side={} handSpacing={:.1f}; independent of "
                         "formation spacing",
                         partner->GetName(), partner->GetFormID(), approach.side, approach.spacing);
        }
        bool idleContact = false;
        if (state.Searching()) {
            approach.spacing = SeekSpacing(tuning.connectDistance);
            const auto target = SeekPoint(P(pos), player.GetAngleZ(), approach.side, approach.spacing);
            const auto from = partner->GetPosition();
            RE::NiPoint3 nativeVelocity{};
            partner->GetLinearVelocity(nativeVelocity);
            float nativeGraphSpeed{};
            const bool haveGraphSpeed = partner->GetGraphVariableFloat("Speed", nativeGraphSpeed);
            idleContact =
                haveGraphSpeed &&
                IdleContactReady(std::hypot(playerVelocity.x, playerVelocity.y),
                                 std::hypot(nativeVelocity.x, nativeVelocity.y), nativeGraphSpeed,
                                 std::remainder(partner->GetAngleZ() - player.GetAngleZ(), 6.283185307F));
            const auto nativeStep = haveSeekPosition ? from - seekLastPosition : RE::NiPoint3{};
            const bool gaitReady =
                seekGait.Observe(dt, graphSpeedLease.edited, nativeGraphSpeed, graphSpeedLease.applied,
                                 std::hypot(nativeStep.x, nativeStep.y));
            if (nativeVelocity.Length() < 1 && !seekMotion.Active()) {
                const auto facing = ForwardFromYaw(partner->GetAngleZ());
                nativeVelocity = {facing.x * PoseSpeed(nativeGraphSpeed),
                                  facing.y * PoseSpeed(nativeGraphSpeed), 0};
            }
            const auto step = seekMotion.Advance(P(from), P(pos), target, playerVelocity, P(nativeVelocity),
                                                 partner->GetAngleZ(), player.GetAngleZ(), dt, gaitReady);
            bool unknown = false, corrected = false;
            bool collisionNeeded = false;
            const char* seekStatus = step ? "line of sight blocked" : "native pathfinding";
            if (step && player.HasLineOfSight(partner, unknown)) {
                seekStatus = "ground step unavailable";
                if (auto grounded = navigation.AttachmentPoint(from, N(*step))) {
                    seekStatus = StepRejection(*partner, from, *grounded);
                    if (!seekStatus) {
                        collisionNeeded = collision.Acquire(*partner);
                        if (collisionNeeded) {
                            locomotionActor.store(partner->GetFormID(), std::memory_order_release);
                            DriveLocomotion(*partner);
                        }
                        corrected =
                            collisionNeeded && (!seekMotion.Integrating() ||
                                                MovePartner(*partner, *grounded, seekMotion.Velocity()));
                        if (corrected) {
                            Engine::SetHeading(*partner, seekMotion.Heading());
                            UpdateVisualPosition(*partner);
                        }
                        seekStatus = corrected ? (seekMotion.Integrating() ? "animated approach"
                                                                           : "starting approach gait")
                                               : "character controller unavailable";
                    }
                }
            }
            if (!corrected) {
                collision.Restore();
                graphSpeedLease.Restore();
                graphDirectionLease.Restore();
                graphTurnLease.Restore();
                locomotionActor.store(0, std::memory_order_release);
                seekMotion.Reset();
                seekGait.Reset();
                haveSeekPosition = false;
                seekUpdates = 0;
            }
            if (corrected) {
                seekLastPosition = partner->GetPosition();
                haveSeekPosition = true;
            }
            approach.closing = corrected;
            approach.velocity = seekMotion.Velocity();
            seekTraceTime += dt;
            if (seekTraceTime >= 2) {
                seekTraceTime = 0;
                logger::info("[HandHolding] Approach {:08X}: gap={:.1f} targetError={:.1f} corrected={} "
                             "status={} desiredSpeed={:.1f} graphSpeed={:.1f} graphDriven={} npcUpdates={} "
                             "gaitReady={} settled={} step={:.2f}",
                             partner->GetFormID(), (from - pos).Length(), IK::Length(target - P(from)),
                             corrected, seekStatus, IK::Length(seekMotion.Velocity()), nativeGraphSpeed,
                             graphSpeedLease.edited, seekUpdates, seekGait.Ready(),
                             seekMotion.Settled(playerVelocity, player.GetAngleZ()),
                             (partner->GetPosition() - from).Length());
            }
        }
        const auto delta = partner->GetPosition() - pos;
        const auto nf = ForwardFromYaw(partner->GetAngleZ());
        const float distance = delta.Length();
        const bool geometry =
            ReadyAtHand(delta.Dot(right), delta.Dot(forward), approach.side, approach.spacing) &&
            std::abs(delta.z) < 60 && nf.x * forward.x + nf.y * forward.y > .35F;
        bool losUnknown = false;
        const bool close = distance <= (state.Searching() ? tuning.connectDistance : tuning.releaseDistance);

        if (!holding && (!geometry || !close ||
                         (!idleContact && !seekMotion.Settled(playerVelocity, player.GetAngleZ())) ||
                         !player.HasLineOfSight(partner, losUnknown))) {
            return;
        }
        if (!playerArm.Bind(player, approach.side < 0) || !followerArm.Bind(*partner, approach.side > 0)) {
            Drop("arm skeleton unavailable", true);
            return;
        }

        playerArm.Restore(&player);
        followerArm.Restore(partner);
        World(playerArm.upper.node.get());
        World(followerArm.upper.node.get());
        auto target = (playerArm.upper.node->world.translate + followerArm.upper.node->world.translate) * .5F;
        const float reach =
            std::min(playerArm.Upper() + playerArm.Lower(), followerArm.Upper() + followerArm.Lower());
        target.z -= reach * .76F;
        target += forward * 4;
        const auto wristOffset = right * (static_cast<float>(approach.side) * 2);
        const auto shared = IK::SharedTarget(P(playerArm.upper.node->world.translate + wristOffset),
                                             (playerArm.Upper() + playerArm.Lower()) * .94F,
                                             P(followerArm.upper.node->world.translate - wristOffset),
                                             (followerArm.Upper() + followerArm.Lower()) * .94F, P(target));
        if (shared) {
            target = N(*shared);
        }
        if (holding || !haveGrip) {
            grip = target;
            haveGrip = true;
        }
        else {
            grip += (target - grip) * (1 - std::exp(-18 * std::clamp(dt, 0.0F, .1F)));
        }
        const bool reachable =
            Reachable(playerArm, WristTarget(true)) && Reachable(followerArm, WristTarget(false));
        reachFailureTime = reachable ? 0 : reachFailureTime + dt;
        if (state.Searching()) {
            if (reachable &&
                attachment.Capture(P(pos), P(partner->GetPosition()), player.GetAngleZ(), playerVelocity) &&
                state.Connect(partner->GetFormID(), distance, true, tuning)) {
                attachedActor.store(partner->GetFormID(), std::memory_order_release);
                locomotionActor.store(partner->GetFormID(), std::memory_order_release);
                approach.closing = false;
                approach.velocity = playerVelocity;
                approach.heading = attachment.Yaw();
                renderPartner.store(partner->GetFormID(), std::memory_order_release);
                rendering.store(true, std::memory_order_release);
                logger::info("[HandHolding] Attached to {} ({:08X}); side={} gap={:.1f}; walk/run enabled, "
                             "sprint releases",
                             partner->GetName(), partner->GetFormID(), approach.side, distance);
            }
        }
        else {
            if (reachFailureTime > .25F || poseFailed) {
                logger::info("[HandHolding] Reach: player={:.1f}/{:.1f} partner={:.1f}/{:.1f} shared={}",
                             (WristTarget(true) - playerArm.upper.node->world.translate).Length(),
                             playerArm.Upper() + playerArm.Lower(),
                             (WristTarget(false) - followerArm.upper.node->world.translate).Length(),
                             followerArm.Upper() + followerArm.Lower(), shared.has_value());
                Drop(poseFailed ? "arm pose interrupted" : "arms out of reach");
                return;
            }
            if (!state.Maintain(partner->GetFormID(), distance, true, tuning)) {
                Drop("separation");
                return;
            }
        }
        if (state.GetPhase() == Phase::Holding) {

            DriveLocomotion(*partner);
            Apply(playerArm, true);
            Apply(followerArm, false);
            SyncBoneCache(playerArm);
            SyncBoneCache(followerArm);
            traceTime += dt;
            if (traceTime >= 1) {
                traceTime = 0;
                RE::NiPoint3 velocity{}, followerVelocity{};
                player.GetLinearVelocity(velocity);
                partner->GetLinearVelocity(followerVelocity);
                RE::hkVector4 physics{};
                if (auto* controller = partner->GetCharController()) {
                    controller->GetLinearVelocityImpl(physics);
                }
                const auto raw = N(physics);
                const auto desiredVelocity = attachment.Velocity();
                float graphSpeed{};
                partner->GetGraphVariableFloat("Speed", graphSpeed);
                float graphDirection{};
                partner->GetGraphVariableFloat("Direction", graphDirection);
                float graphTurn{};
                partner->GetGraphVariableFloat("TurnDelta", graphTurn);
                logger::info(
                    "[HandHolding] Sticky {:08X}: playerSpeed={:.1f} gap={:.1f} correction={:.2f} "
                    "wristGap={:.2f} weight={:.2f} followerSpeed={:.1f} physicsSpeed={:.1f} "
                    "desiredSpeed={:.1f} graphSpeed={:.1f} graphDriven={} characterCollisionOff={} "
                    "posePasses={}/{} npcUpdates={} headingError={:.3f} graphDirection={:.2f} "
                    "graphTurn={:.2f} postChannelPasses={} nativeDirection={:.3f} nativeTurn={:.2f} "
                    "writtenPhysicsSpeed={:.1f} visualUpdates={}",
                    partner->GetFormID(), std::hypot(velocity.x, velocity.y), distance, correction,
                    (playerArm.hand.node->world.translate - followerArm.hand.node->world.translate).Length(),
                    state.Weight(), std::hypot(followerVelocity.x, followerVelocity.y),
                    std::hypot(raw.x, raw.y) / RE::bhkWorld::GetWorldScale(),
                    std::hypot(desiredVelocity.x, desiredVelocity.y), graphSpeed, graphSpeedLease.edited,
                    collision.controller &&
                        collision.controller->flags.any(RE::CHARACTER_FLAGS::kNoCharacterCollisions),
                    playerPosePasses, partnerPosePasses, partnerUpdatePasses,
                    std::remainder(partner->GetAngleZ() - attachment.Yaw(), 6.283185307F), graphDirection,
                    graphTurn, channelPasses, nativeDirection, nativeTurnDelta, writtenPhysicsSpeed,
                    visualUpdates);
                playerPosePasses = partnerPosePasses = partnerUpdatePasses = channelPasses = visualUpdates =
                    0;
            }
        }
    }
    Approach GetApproach()
    {
        std::lock_guard lock(mutex);
        return approach;
    }
    bool IsAttached(RE::FormID actor)
    {
        return actor && attachedActor.load(std::memory_order_acquire) == actor;
    }
} // namespace Wayfarer::HandHolding::Classic
