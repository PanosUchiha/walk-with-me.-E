#include "rest_dialogue.h"
#include "formation_controller.h"
#include "rest_dialogue_policy.h"
#include "rest_dialogue_records.h"
#include "travel_space.h"
#include <chrono>
#include <future>

namespace Wayfarer::RestDialogue
{
    namespace
    {
        using InfoFlags = RE::TOPIC_INFO_DATA::TOPIC_INFO_FLAGS;
        struct Branch
        {
            RE::TESTopic* root{};
        };
        struct Scan
        {
            std::size_t branch{}, blockingCursor{};
            bool entered{}, found{}, ready{}, visible{}, deferred{};
            float delay{}, sightDelay{};
            RE::FormID matchTopic{}, matchInfo{};
            std::string matchPrompt;
            std::vector<RE::TESTopic*> pending;
            std::unordered_set<RE::FormID> visited;
            std::unordered_set<RE::BGSDialogueBranch*> activeBlocking;
        };
        std::vector<Branch> branches;
        std::vector<RE::TESTopic*> blockingRoots;
        bool indexed{};
        std::unordered_map<RE::FormID, Scan> scans;
        struct FileCache
        {
            std::future<InfoRecords> pending;
            InfoRecords records;
            bool ready{};
        };
        std::unordered_map<std::string, FileCache> files;
        std::mutex markerMutex;
        std::vector<Marker> markers;

        std::string_view Text(const char* value)
        {
            return value ? value : "";
        }

        void Publish(std::vector<Marker> value = {})
        {
            std::lock_guard lock(markerMutex);
            markers = std::move(value);
        }

        bool Management(RE::TESForm& form, RE::TESQuest& quest)
        {
            const auto* source = form.GetFile(0);
            const auto* questSource = quest.GetFile(0);
            return ManagementSource(source ? source->GetFilename() : "", Text(quest.GetFormEditorID())) ||
                   ManagementSource(questSource ? questSource->GetFilename() : "",
                                    Text(quest.GetFormEditorID()));
        }

        bool Conversation(RE::TESTopic& topic)
        {
            if (topic.IsDeleted() || !topic.ownerQuest || Management(topic, *topic.ownerQuest) ||
                topic.data.type != RE::DIALOGUE_TYPE::kPlayerDialogue) {
                return false;
            }
            using Subtype = RE::DIALOGUE_DATA::Subtype;
            return (topic.data.subtype == Subtype::kCustom || topic.data.subtype == Subtype::kForceGreet ||
                    topic.data.subtype == Subtype::kRumors || topic.data.subtype == Subtype::kUnk3) &&
                   !ManagementTopic(Text(topic.GetFormEditorID()), Text(topic.GetFullName()));
        }

        void Index()
        {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) {
                return;
            }
            for (auto* topic : data->GetFormArray<RE::TESTopic>()) {
                auto* branch = topic ? topic->ownerBranch : nullptr;
                if (!topic || !branch || branch->IsDeleted() || branch->startingTopic != topic ||
                    topic->IsDeleted() || topic->data.type != RE::DIALOGUE_TYPE::kPlayerDialogue) {
                    continue;
                }
                // Even management branches can block personal dialogue temporarily.
                if (branch->flags.any(RE::BGSDialogueBranch::Flag::kBlocking)) {
                    blockingRoots.push_back(topic);
                }
                if (!Conversation(*topic)) {
                    continue;
                }
                // Linked-only branches are not independently available to the player.
                if (!branch->flags.any(RE::BGSDialogueBranch::Flag::kTopLevel,
                                       RE::BGSDialogueBranch::Flag::kBlocking)) {
                    continue;
                }
                branches.push_back({topic});
            }
            indexed = true;
            logger::info("[RestDialogue] Indexed {} conversation branches", branches.size());
        }

        RE::FormID RuntimeID(const RE::TESFile& file, RE::FormID raw)
        {
            if (raw <= 0x7FF) {
                return raw;
            }
            const auto index = raw >> 24;
            const auto* owner = index < file.masterCount && file.masterPtrs ? file.masterPtrs[index] : &file;
            if (!owner) {
                return 0;
            }
            return owner->IsLight()
                       ? (0xFE000000U | (static_cast<RE::FormID>(owner->smallFileCompileIndex) << 12) |
                          (raw & 0xFFF))
                       : ((static_cast<RE::FormID>(owner->compileIndex) << 24) | (raw & 0xFFFFFF));
        }

        const InfoMetadata& Metadata(RE::TESTopicInfo& info)
        {
            static const InfoMetadata unavailable;
            const auto* file = info.GetFile();
            if (!file) {
                return unavailable;
            }
            const auto name = std::string(file->GetFilename());
            auto [it, added] = files.try_emplace(name);
            auto& cache = it->second;
            if (added) {
                // The worker sees only a path and a localization flag. All engine
                // forms, conditions and FormID resolution stay on the game thread.
                const bool localized = file->IsLocalized();
                cache.pending = std::async(std::launch::async, [name, localized] {
                    return ReadInfoRecords(std::filesystem::path("Data") / name, localized);
                });
            }
            if (!cache.ready) {
                if (cache.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                    return unavailable;
                }
                try {
                    auto raw = cache.pending.get();
                    for (auto& [id, detail] : raw) {
                        for (auto& link : detail.links) {
                            link = RuntimeID(*file, link);
                        }
                        detail.speaker = RuntimeID(*file, detail.speaker);
                        cache.records.emplace(RuntimeID(*file, id), std::move(detail));
                    }
                }
                catch (const std::exception& error) {
                    logger::warn("[RestDialogue] Could not read {}: {}", name, error.what());
                }
                cache.ready = true;
                logger::info("[RestDialogue] Read {} conversation records from {}", cache.records.size(),
                             name);
            }
            const auto found = cache.records.find(info.GetFormID());
            return found != cache.records.end() ? found->second : unavailable;
        }

        bool Conditions(const RE::TESCondition& conditions, RE::TESQuest& quest, RE::Actor& actor,
                        RE::PlayerCharacter& player)
        {
#ifdef WAYFARER_MODERN_COMMONLIB
            RE::ConditionCheckParams params(&actor, &player);
            params.quest = &quest;
#else
            // Legacy CommonLib omits the final package-data pointer (0x30) and
            // names the quest pointer unk10. Reserve the complete engine layout.
            struct Params
            {
                RE::ConditionCheckParams value;
                void* packageData{};
            } storage{{&actor, &player}};
            static_assert(sizeof(Params) == 0x38);
            auto& params = storage.value;
            params.unk10 = &quest;
#endif
            return ConditionsPass(conditions.head, [&](const auto& item) { return item.IsTrue(params); });
        }

        RE::TESTopicInfo* Response(RE::TESTopic& topic, RE::Actor& actor, RE::PlayerCharacter& player,
                                   bool* deferred = nullptr)
        {
            auto* quest = topic.ownerQuest;
            if (!quest || !quest->IsRunning() || quest->IsDeleted() || !topic.topicInfos ||
                !Conditions(quest->objConditions, *quest, actor, player)) {
                return nullptr;
            }
            for (std::uint32_t i = 0; i < topic.numTopicInfos; ++i) {
                auto* info = topic.topicInfos[i];
                if (!info || info->IsDeleted() ||
                    (info->saidOnce && info->data.flags.any(InfoFlags::kSayOnce))) {
                    continue;
                }
                if (Conditions(info->objConditions, *quest, actor, player)) {
                    const auto& detail = Metadata(*info);
                    if (!detail.valid) {
                        // Unknown or still loading is not evidence of new dialogue,
                        // nor permission to select a lower-priority alternative.
                        if (deferred) {
                            *deferred = true;
                        }
                        return nullptr;
                    }
                    auto* base = actor.GetActorBase();
                    if (detail.speaker && (!base || detail.speaker != base->GetFormID())) {
                        continue;
                    }
                    // A heard, repeatable response still hides lower-priority INFOs.
                    return info;
                }
            }
            return nullptr;
        }

        bool Unheard(RE::TESTopic& topic, RE::TESTopicInfo& info, std::string_view prompt)
        {
            if (prompt.empty() || info.saidOnce || Management(info, *topic.ownerQuest) ||
                info.data.flags.any(InfoFlags::kRandom, InfoFlags::kRandomEnd, InfoFlags::kInfoRefusal,
                                    static_cast<InfoFlags>(1 << 6), static_cast<InfoFlags>(1 << 8))) {
                return false;
            }
            // Shared responses can be reached through more than one prompt.
            return !info.dataInfo || !info.dataInfo->saidOnce;
        }

        struct Content
        {
            SpokenContent spoken{SpokenContent::None};
            bool valid{}, scripted{}, heard{};
        };

        Content ResponseContent(RE::TESTopicInfo& info)
        {
            Content result;
            std::array<RE::FormID, 16> visited{};
            std::size_t count{};
            for (auto* response = &info; response; response = response->dataInfo) {
                const auto end = visited.begin() + count;
                if (count == visited.size() ||
                    std::find(visited.begin(), end, response->GetFormID()) != end) {
                    return {}; // Malformed shared-response chains are not conversations.
                }
                visited[count++] = response->GetFormID();
                const auto& detail = Metadata(*response);
                if (!detail.valid) {
                    return {};
                }
                result.scripted = result.scripted || detail.scripted;
                result.heard = result.heard || response->saidOnce;
                // DNAM replaces the local response text with a shared response.
                if (!response->dataInfo) {
                    result.spoken = detail.content;
                }
            }
            result.valid = true;
            return result;
        }

        void Step(Scan& scan, RE::Actor& actor, RE::PlayerCharacter& player)
        {
            if (scan.blockingCursor < blockingRoots.size()) {
                auto* root = blockingRoots[scan.blockingCursor++];
                if (Response(*root, actor, player, &scan.deferred)) {
                    scan.activeBlocking.insert(root->ownerBranch);
                }
                return;
            }
            if (scan.branch >= branches.size()) {
                const bool ready = scan.found && !scan.deferred;
                if (ready != scan.ready && Settings::GetSingleton().Get().logDiagnostics) {
                    logger::info("[RestDialogue] {} ({:08X}): {}; topic={:08X}, info={:08X}, prompt=\"{}\"",
                                 actor.GetName(), actor.GetFormID(),
                                 ready ? "unheard conversation" : "cleared", scan.matchTopic, scan.matchInfo,
                                 scan.matchPrompt);
                }
                scan.ready = ready;
                scan.branch = scan.blockingCursor = 0;
                scan.entered = scan.found = scan.deferred = false;
                scan.pending.clear();
                scan.visited.clear();
                scan.activeBlocking.clear();
                scan.delay = 2.0F;
                return;
            }
            auto& branch = branches[scan.branch];
            if (!scan.entered) {
                const auto* exclusive = actor.GetExclusiveBranch();
                if ((exclusive && exclusive != branch.root->ownerBranch) ||
                    (!exclusive && !scan.activeBlocking.empty() &&
                     !scan.activeBlocking.contains(branch.root->ownerBranch))) {
                    ++scan.branch;
                    return;
                }
                scan.entered = true;
                scan.pending = {branch.root};
                scan.visited.clear();
            }
            auto* topic = scan.pending.back();
            scan.pending.pop_back();
            if (scan.visited.insert(topic->GetFormID()).second && Conversation(*topic)) {
                if (auto* info = Response(*topic, actor, player)) {
                    const auto& detail = Metadata(*info);
                    const std::string_view prompt =
                        detail.hasPrompt ? detail.prompt : Text(topic->GetFullName());
                    auto* base = actor.GetActorBase();
                    const bool correctSpeaker =
                        !detail.speaker || (base && base->GetFormID() == detail.speaker);
                    if (correctSpeaker && !ManagementTopic(Text(topic->GetFormEditorID()), prompt)) {
                        const auto content = ResponseContent(*info);
                        const bool conversation =
                            content.valid && ConversationResponse(content.spoken, content.scripted);
                        const auto action = NextResponseAction(
                            content.valid, content.heard, conversation && Unheard(*topic, *info, prompt),
                            !info->data.flags.any(static_cast<InfoFlags>(1), InfoFlags::kRandom,
                                                  InfoFlags::kRandomEnd));
                        if (action == ResponseAction::Notify) {
                            scan.found = true;
                            scan.matchTopic = topic->GetFormID();
                            scan.matchInfo = info->GetFormID();
                            scan.matchPrompt = prompt;
                            scan.branch = branches.size();
                            return;
                        }
                        // Only follow authored links from the selected response.
                        // Bit 0 is Goodbye (the legacy CommonLib enum misnames it).
                        // Unheard hubs may lead to real dialogue. Heard responses
                        // stop the search, including their unused alternative links.
                        if (action == ResponseAction::FollowLinks) {
                            for (auto id : detail.links) {
                                if (!scan.visited.contains(id) &&
                                    scan.visited.size() + scan.pending.size() < 256) {
                                    if (auto* next = RE::TESForm::LookupByID<RE::TESTopic>(id)) {
                                        scan.pending.push_back(next);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (scan.pending.empty()) {
                ++scan.branch;
                scan.entered = false;
            }
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
                const auto& children = node->GetChildren();
                for (const auto& child : children) {
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
    } // namespace

    void Reset()
    {
        scans.clear();
        Publish();
    }

    void Update(float dt)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* ui = RE::UI::GetSingleton();
        if (!Menu::IsInstalled() || !Settings::GetSingleton().Get().restDialogueIcons || !player || !ui ||
            !VisibleUI(*ui) || player->IsInCombat() || player->IsDead() || !player->Is3DLoaded()) {
            Reset(); // Also invalidate detections immediately when a conversation starts.
            return;
        }
        const auto actors = FormationController::GetSingleton().RestDialogueActors();
        if (actors.empty()) {
            Reset();
            return;
        }
        if (!indexed) {
            Index();
        }
        std::erase_if(scans, [&](const auto& entry) {
            return std::none_of(actors.begin(), actors.end(), [&](const auto& handle) {
                auto actor = handle.get();
                return actor && actor->GetFormID() == entry.first;
            });
        });
        auto* cameraOwner = RE::PlayerCamera::GetSingleton();
        auto* camera = Camera(cameraOwner ? cameraOwner->cameraRoot.get() : nullptr);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1);
        std::vector<Marker> frame;
        for (const auto& handle : actors) {
            auto actor = handle.get();
            if (!actor || actor->IsDead() || actor->IsDisabled() || !actor->Is3DLoaded() ||
                actor->IsInCombat() || actor->GetCurrentScene() || actor->IsHostileToActor(player) ||
                !InSameTravelSpace(*actor, *player) ||
                actor->GetPosition().GetDistance(player->GetPosition()) > 1800.0F) {
                if (actor) {
                    scans.erase(actor->GetFormID());
                }
                continue;
            }
            auto& scan = scans[actor->GetFormID()];
            scan.delay = std::max(0.0F, scan.delay - dt);
            // At least one step per companion prevents starvation in a large party.
            if (scan.delay <= 0) {
                do {
                    Step(scan, *actor, *player);
                } while (scan.delay <= 0 && std::chrono::steady_clock::now() < deadline);
            }
            if (!scan.ready || !camera) {
                continue;
            }
            scan.sightDelay -= dt;
            if (scan.sightDelay <= 0) {
                bool unused{};
                scan.visible = player->HasLineOfSight(actor.get(), unused);
                scan.sightDelay = .25F;
            }
            if (!scan.visible) {
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
                frame.push_back({x, 1.0F - y, actor->GetFormID()});
            }
        }
        Publish(std::move(frame));
    }

    std::vector<Marker> ReadMarkers()
    {
        // Native pause menus can stop player updates; hide the last frame even
        // when Update() has not had an opportunity to invalidate it yet.
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !VisibleUI(*ui)) {
            return {};
        }
        std::lock_guard lock(markerMutex);
        return markers;
    }
} // namespace Wayfarer::RestDialogue
