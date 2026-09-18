#include "travel_navigation.h"

namespace Wayfarer
{
    namespace
    {
        class TravelObstacleCollector final : public RE::hkpClosestRayHitCollector
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

                if (ref && ref->As<RE::Actor>()) {
                    return;
                }
                if (hit.hitFraction < rayHit.hitFraction) {
                    closestReference = ref ? ref->GetFormID() : 0;
                }
                RE::hkpClosestRayHitCollector::AddRayHit(body, hit);
            }
        };
    } // namespace

    ObstaclePlan TravelNavigation::AvoidObstacles(RE::Actor& actor, RE::NiPoint3 goal, float speed, float dt,
                                                  float arrivalRadius, ObstacleAvoidance& state) const
    {
        auto* cell = actor.GetParentCell();
        auto* world = cell ? cell->GetbhkWorld() : nullptr;
        const float scale = RE::bhkWorld::GetWorldScale();
        if (!world || !std::isfinite(scale) || scale <= 0) {
            state.Reset();
            ObstaclePlan failed;
            failed.queryFailed = true;
            return failed;
        }
        const auto pos = actor.GetPosition();
        unsigned rays = 0, navRejects = 0;
        RE::FormID blocker = 0;
        bool queryFailed = false;
        const auto probe = [&](GroundPoint origin, Vec2 destination) -> std::optional<GroundPoint> {
            const RE::NiPoint3 startPoint{origin.x, origin.y, origin.z};
            auto ground = Trace(startPoint, destination);
            if (!ground || std::hypot(ground->x - destination.x, ground->y - destination.y) > 2) {
                ++navRejects;
                return {};
            }
            const auto forward = Normalize({destination.x - origin.x, destination.y - origin.y});
            const auto right = RightFromForward(forward);

            for (float side : {-28.0F, 28.0F}) {
                const Vec2 edge{destination.x + right.x * side, destination.y + right.y * side};
                auto support = Trace(startPoint, edge);
                if (!support || std::hypot(support->x - edge.x, support->y - edge.y) > 2 ||
                    std::abs(support->z - ground->z) > 32) {
                    ++navRejects;
                    return {};
                }
            }
            const float distance = std::hypot(destination.x - origin.x, destination.y - origin.y);
            const int segments = std::clamp(static_cast<int>(std::ceil(distance / 150.0F)), 1, 4);
            auto from = startPoint;
            for (int segment = 1; segment <= segments; ++segment) {
                const float fraction = static_cast<float>(segment) / segments;
                const Vec2 xy{origin.x + (destination.x - origin.x) * fraction,
                              origin.y + (destination.y - origin.y) * fraction};
                auto to = segment == segments ? ground : Trace(startPoint, xy);
                if (!to || std::hypot(to->x - xy.x, to->y - xy.y) > 2) {
                    ++navRejects;
                    return {};
                }

                {
                    RE::BSReadLockGuard guard(world->worldLock);
                    for (float height : {40.0F, 92.0F}) {
                        for (float side : {-28.0F, 0.0F, 28.0F}) {
                            const RE::NiPoint3 offset{right.x * side, right.y * side, height};
                            const auto a = (from + offset) * scale, b = (*to + offset) * scale;
                            RE::bhkPickData pick{};
                            TravelObstacleCollector collector;
                            collector.Reset();
                            pick.rayInput.from = RE::hkVector4{a.x, a.y, a.z, 0};
                            pick.rayInput.to = RE::hkVector4{b.x, b.y, b.z, 0};
#ifdef WAYFARER_MODERN_COMMONLIB
                            pick.rayInput.filterInfo.filter =
                                static_cast<std::uint32_t>(RE::COL_LAYER::kCharController) | (1u << 16);
                            pick.closestRayHitCollector = &collector;
#else
                            pick.rayInput.filterInfo =
                                static_cast<std::uint32_t>(RE::COL_LAYER::kCharController) | (1u << 16);
                            pick.rayHitCollectorA8 = &collector;
#endif
                            world->PickObject(pick);
                            ++rays;
#ifdef WAYFARER_MODERN_COMMONLIB
                            if (pick.pickFailed) {
                                queryFailed = true;
                                return {};
                            }
#else
                            if (pick.unkC0) {
                                queryFailed = true;
                                return {};
                            }
#endif
                            if (!std::isfinite(collector.rayHit.hitFraction)) {
                                queryFailed = true;
                                return {};
                            }
                            if (collector.HasHit() && collector.rayHit.hitFraction < 1.0F) {
                                if (!blocker)
                                    blocker = collector.closestReference;
                                return {};
                            }
                        }
                    }
                }
                from = *to;
            }
            return GroundPoint{ground->x, ground->y, ground->z};
        };
        auto result =
            state.Plan({pos.x, pos.y, pos.z}, {goal.x, goal.y, goal.z}, speed, dt, arrivalRadius, probe);
        result.blocker = blocker;
        result.rays = rays;
        result.navRejects = navRejects;
        result.queryFailed = queryFailed;
        return result;
    }

    void TravelNavigation::Collect(RE::PlayerCharacter& a_player)
    {
        meshes.clear();
        std::unordered_set<RE::TESObjectCELL*> seen;
        auto add = [&](RE::TESObjectCELL* cell) {
            if (!cell || !cell->IsAttached() || !seen.insert(cell).second ||
                !cell->GetRuntimeData().navMeshes) {
                return;
            }
            for (const auto& mesh : cell->GetRuntimeData().navMeshes->navMeshes) {
                if (mesh) {
                    meshes.emplace_back(static_cast<RE::BSNavmesh*>(mesh.get()));
                }
            }
        };
        add(a_player.GetParentCell());
        if (a_player.GetParentCell() && !a_player.GetParentCell()->IsInteriorCell()) {
            if (auto* tes = RE::TES::GetSingleton()) {
                const auto p = a_player.GetPosition();
                for (float x : {-1200.0F, 0.0F, 1200.0F}) {
                    for (float y : {-1200.0F, 0.0F, 1200.0F}) {
                        add(tes->GetCell({p.x + x, p.y + y, p.z}));
                    }
                }
            }
        }
        start = Find(a_player.GetPosition());
    }

    std::optional<std::array<GroundPoint, 3>> TravelNavigation::Vertices(Location l) const
    {
        if (!l.mesh || l.triangle >= l.mesh->triangles.size()) {
            return std::nullopt;
        }
        const auto& tri = l.mesh->triangles[l.triangle];
        if (tri.triangleFlags.any(RE::BSNavmeshTriangle::TriangleFlag::kDeleted)) {
            return std::nullopt;
        }
        std::array<GroundPoint, 3> result;
        for (int i = 0; i < 3; ++i) {
            if (tri.vertices[i] >= l.mesh->vertices.size()) {
                return std::nullopt;
            }
            const auto& v = l.mesh->vertices[tri.vertices[i]].location;
            result[i] = {v.x, v.y, v.z};
        }
        return result;
    }

    std::optional<TravelNavigation::Location> TravelNavigation::Find(RE::NiPoint3 p, float maxHeight) const
    {
        std::optional<Location> best;
        float bestHeight = maxHeight;
        for (const auto& mesh : meshes) {
            for (std::uint32_t index = 0; index < mesh->triangles.size() && index < 65535; ++index) {
                Location location{static_cast<RE::NavMesh*>(mesh.get()), static_cast<std::uint16_t>(index)};
                const auto vertices = Vertices(location);
                if (!vertices) {
                    continue;
                }
                const auto weights = GroundWeights({p.x, p.y, p.z}, *vertices);
                if (!weights || (*weights)[0] < -0.001F || (*weights)[1] < -0.001F ||
                    (*weights)[2] < -0.001F) {
                    continue;
                }
                const float height = (*weights)[0] * (*vertices)[0].z + (*weights)[1] * (*vertices)[1].z +
                                     (*weights)[2] * (*vertices)[2].z;
                if (const float difference = std::abs(height - p.z); difference < bestHeight) {
                    bestHeight = difference;
                    best = location;
                }
            }
        }
        return best;
    }

    std::optional<TravelNavigation::Location> TravelNavigation::Neighbor(Location l, int edge) const
    {
        const auto& tri = l.mesh->triangles[l.triangle];
        const auto index = tri.triangles[edge];
        const auto linkFlag = static_cast<RE::BSNavmeshTriangle::TriangleFlag>(1 << edge);
        Location neighbor{l.mesh, index};
        if (tri.triangleFlags.any(linkFlag)) {
            if (index >= l.mesh->extraEdgeInfo.size()) {
                return std::nullopt;
            }
            const auto& link = l.mesh->extraEdgeInfo[index];

            if (link.type.get() != RE::EDGE_EXTRA_INFO_TYPE::kPortal) {
                return std::nullopt;
            }
            neighbor.mesh = nullptr;
            for (const auto& mesh : meshes) {
                auto* form = static_cast<RE::NavMesh*>(mesh.get());
                if (form->GetFormID() == link.portal.otherMeshID) {
                    neighbor.mesh = form;
                    break;
                }
            }
            neighbor.triangle = link.portal.triangle;
        }
        return Vertices(neighbor) ? std::optional<Location>{neighbor} : std::nullopt;
    }

    std::optional<GroundTriangle> TravelNavigation::RestTriangle(GroundKey key) const
    {
        Location location{};
        for (const auto& mesh : meshes) {
            auto* form = static_cast<RE::NavMesh*>(mesh.get());
            if (form->GetFormID() == static_cast<RE::FormID>(key >> 16)) {
                location = {form, static_cast<std::uint16_t>(key & 0xFFFF)};
                break;
            }
        }
        const auto vertices = Vertices(location);
        if (!vertices) {
            return std::nullopt;
        }
        GroundTriangle result{*vertices, {}};
        for (int edge = 0; edge < 3; ++edge) {
            if (auto next = Neighbor(location, edge)) {
                result.neighbors[edge] =
                    (static_cast<GroundKey>(next->mesh->GetFormID()) << 16) | next->triangle;
            }
        }
        return result;
    }

    RestReachability TravelNavigation::RestArea(RE::NiPoint3 origin, RE::NiPoint3 center, float radius,
                                                float height) const
    {
        auto source = Find(origin);
        if (!source) {
            return {};
        }
        const GroundKey key = (static_cast<GroundKey>(source->mesh->GetFormID()) << 16) | source->triangle;
        return ExploreRestGround({origin.x, origin.y, origin.z}, key,
                                 {{center.x, center.y, center.z},
                                  radius,
                                  height,
                                  std::clamp(radius * 4.0F, 1200.0F, 5000.0F),
                                  2048},
                                 [&](GroundKey at) { return RestTriangle(at); });
    }

    std::optional<RestDestination> TravelNavigation::FindRestDestination(const RestReachability& area,
                                                                         RE::NiPoint3 goal) const
    {

        auto target = Find(goal, 64);
        if (!target) {
            return std::nullopt;
        }
        const GroundKey key = (static_cast<GroundKey>(target->mesh->GetFormID()) << 16) | target->triangle;
        auto distance = area.distance.find(key);
        if (distance == area.distance.end()) {
            return std::nullopt;
        }
        auto triangle = RestTriangle(key);
        if (!triangle) {
            return std::nullopt;
        }
        auto weights = GroundWeights({goal.x, goal.y, goal.z}, triangle->vertices);
        if (!weights) {
            return std::nullopt;
        }
        const float height = (*weights)[0] * triangle->vertices[0].z +
                             (*weights)[1] * triangle->vertices[1].z +
                             (*weights)[2] * triangle->vertices[2].z;
        const GroundPoint point{goal.x, goal.y, height};
        return RestDestination{point, distance->second + GroundDistance(GroundCenter(*triangle), point)};
    }

    std::optional<RE::NiPoint3> TravelNavigation::Trace(RE::NiPoint3 origin, Vec2 goal,
                                                        std::optional<float> stepHeight) const
    {

        auto originLocation = start;
        bool contains = false;
        if (originLocation) {
            if (auto vertices = Vertices(*originLocation)) {
                if (auto weights = GroundWeights({origin.x, origin.y, origin.z}, *vertices)) {
                    const float height = (*weights)[0] * (*vertices)[0].z + (*weights)[1] * (*vertices)[1].z +
                                         (*weights)[2] * (*vertices)[2].z;
                    contains = (*weights)[0] >= -.001F && (*weights)[1] >= -.001F &&
                               (*weights)[2] >= -.001F && std::abs(height - origin.z) < 96;
                }
            }
        }
        if (!contains) {
            originLocation = Find(origin);
        }
        if (!originLocation) {
            return std::nullopt;
        }
        auto key = [](Location l) -> GroundKey {
            return (static_cast<GroundKey>(l.mesh->GetFormID()) << 16) | l.triangle;
        };
        const GroundQuery query = [&](GroundKey id) -> std::optional<GroundTriangle> {
            Location location{};
            for (const auto& mesh : meshes) {
                auto* form = static_cast<RE::NavMesh*>(mesh.get());
                if (form->GetFormID() == static_cast<RE::FormID>(id >> 16)) {
                    location = {form, static_cast<std::uint16_t>(id & 0xFFFF)};
                    break;
                }
            }
            const auto vertices = Vertices(location);
            if (!vertices) {
                return std::nullopt;
            }
            GroundTriangle triangle{*vertices, {}};
            for (int edge = 0; edge < 3; ++edge) {
                if (const auto next = Neighbor(location, edge)) {
                    triangle.neighbors[edge] = key(*next);
                }
            }
            return triangle;
        };
        const auto result =
            stepHeight
                ? TraceGroundStep({origin.x, origin.y, origin.z}, {goal.x, goal.y, *stepHeight},
                                  key(*originLocation), query)
                : TraceGroundCorridor({origin.x, origin.y, origin.z}, goal, key(*originLocation), query);
        return result ? std::optional<RE::NiPoint3>{{result->x, result->y, result->z}} : std::nullopt;
    }

    std::optional<RE::NiPoint3> TravelNavigation::AttachmentPoint(RE::NiPoint3 origin,
                                                                  RE::NiPoint3 goal) const
    {

        return Trace(origin, {goal.x, goal.y}, goal.z);
    }

    std::optional<RE::NiPoint3> TravelNavigation::RestPoint(RE::NiPoint3 p) const
    {
        std::optional<RE::NiPoint3> center;
        for (const Vec2 offset : {Vec2{0, 0}, Vec2{40, 0}, Vec2{-40, 0}, Vec2{0, 40}, Vec2{0, -40}}) {
            RE::NiPoint3 at{p.x + offset.x, p.y + offset.y, p.z};
            auto location = Find(at);
            if (!location) {
                return std::nullopt;
            }
            auto vertices = Vertices(*location);
            if (!vertices) {
                return std::nullopt;
            }
            auto weights = GroundWeights({at.x, at.y, at.z}, *vertices);
            if (!weights) {
                return std::nullopt;
            }
            at.z = (*weights)[0] * (*vertices)[0].z + (*weights)[1] * (*vertices)[1].z +
                   (*weights)[2] * (*vertices)[2].z;
            if (std::abs(at.z - p.z) > 18) {
                return std::nullopt;
            }
            if (!center) {
                center = at;
            }
            if (std::abs(at.z - center->z) > 10) {
                return std::nullopt;
            }
        }
        return center;
    }

    std::optional<RE::NiPoint3> TravelNavigation::Resolve(RE::NiPoint3 origin, Vec2 goal,
                                                          Vec2 direction) const
    {
        const auto right = RightFromForward(direction);
        const Vec2 offset{goal.x - origin.x, goal.y - origin.y};
        const float lateral = offset.x * right.x + offset.y * right.y;
        std::optional<RE::NiPoint3> best;
        float bestScore = std::numeric_limits<float>::max();

        for (float width : {1.0F, 0.5F, 0.0F}) {
            const Vec2 candidate{goal.x - right.x * lateral * (1.0F - width),
                                 goal.y - right.y * lateral * (1.0F - width)};
            auto hit = Trace(origin, candidate);
            if (!hit) {
                continue;
            }
            const float score = Length({goal.x - hit->x, goal.y - hit->y});
            if (score < bestScore) {
                bestScore = score;
                best = hit;
            }
            if (score < 1.0F) {
                break;
            }
        }
        return best;
    }
} // namespace Wayfarer
