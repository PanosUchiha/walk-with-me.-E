#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <bcrypt.h>
#include <atomic>
#include <fstream>
#include <iterator>
#include "windows_memory.h"
#include "identity.h"
#include "supported_btps.h"

namespace
{
Guard::Address nodeAsNode{}, leafAsNode{};
std::atomic<unsigned> skipped{};

bool GuardedFindChild(void* object, void* target, int depth) noexcept
{
    Guard::Context context{Guard::SafeRead, nodeAsNode, leafAsNode};
    const bool found = Guard::FindChild(context, reinterpret_cast<Guard::Address>(object),
                                      reinterpret_cast<Guard::Address>(target), depth);
    if (context.invalid || context.unsupported) {
        const auto count = skipped.fetch_add(1, std::memory_order_relaxed) + 1;
        if (count <= 4 || (count <= 65536 && (count & (count - 1)) == 0)) {
            SKSE::log::warn("Skipped unsafe/unsupported child branch: count={} invalid={} unsupported={}",
                            count, context.invalid, context.unsupported);
        }
    }
    return found;
}

bool SupportedFile(HMODULE module)
{
    std::array<wchar_t, 32768> path{};
    const auto count = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) return false;
    std::ifstream input(std::filesystem::path(path.data()), std::ios::binary);
    if (!input) return false;
    std::vector<unsigned char> bytes(std::istreambuf_iterator<char>{input}, {});
    if (bytes.size() != Supported::fileSize) return false;
    std::array<unsigned char, 32> hash{};
    if (!Guard::Sha256(bytes, hash)) return false;
    return hash == Supported::sha256;
}

void Install()
{
    const auto module = GetModuleHandleW(L"BetterThirdPersonSelection.dll");
    if (!module || !SupportedFile(module)) {
        SKSE::log::warn("Inactive: BTPS is absent or its DLL differs from the supported SHA-256 fingerprint.");
        return;
    }
    const REL::Relocation<std::uintptr_t> nodeTable{RE::VTABLE_NiNode[0]};
    const REL::Relocation<std::uintptr_t> leafTable{RE::VTABLE_NiAVObject[0]};
    if (!Guard::SafeRead(nodeTable.address() + 24, &nodeAsNode, sizeof(nodeAsNode)) ||
        !Guard::SafeRead(leafTable.address() + 24, &leafAsNode, sizeof(leafAsNode)) ||
        !nodeAsNode || !leafAsNode || nodeAsNode == leafAsNode) {
        SKSE::log::error("Inactive: could not resolve Skyrim's model-node type checks.");
        return;
    }
    auto* entry = reinterpret_cast<unsigned char*>(module) + Supported::functionRva;
    if (!Guard::InstallJump(entry, reinterpret_cast<Guard::Address>(&GuardedFindChild), Supported::functionBytes)) {
        SKSE::log::warn("Inactive: BTPS traversal bytes differ or memory protection failed. No hook installed.");
        return;
    }
    SKSE::log::info("Active: protected BTPS child traversal at +0x{:X}; selection and original DLL file preserved.",
                   Supported::functionRva);
}
void Message(SKSE::MessagingInterface::Message* event)
{
    if (event && event->type == SKSE::MessagingInterface::kPostPostLoad) Install();
}
}

extern "C" __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* skse, SKSE::PluginInfo* info)
{
    info->infoVersion = SKSE::PluginInfo::kVersion;
    info->name = "BTPSSceneGraphGuard";
    info->version = 1;
    return skse && !skse->IsEditor() && skse->RuntimeVersion() == REL::Version{1,5,97,0};
}
extern "C" __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse)
{
    if (!skse || skse->IsEditor() || skse->RuntimeVersion() != REL::Version{1,5,97,0}) return false;
    SKSE::Init(skse);
    auto directory = SKSE::log::log_directory();
    if (!directory) return false;
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>((*directory / "BTPSSceneGraphGuard.log").string(), true);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("BTPSSceneGraphGuard", std::move(sink)));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    spdlog::flush_on(spdlog::level::info);
    SKSE::log::info("BTPS Scene Graph Guard 0.1.0; Skyrim 1.5.97; waiting for PostPostLoad.");
    auto* messages = SKSE::GetMessagingInterface();
    return messages && messages->RegisterListener(Message);
}
