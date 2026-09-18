#pragma once
#include <spdlog/sinks/rotating_file_sink.h>

namespace Wayfarer::Diagnostics
{
    inline constexpr auto Build = "followers-diag-20260914-r2";
    inline std::shared_ptr<spdlog::logger> log;
    inline void InitializeLog(const std::filesystem::path& directory)
    {
        try {
            auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                (directory / "Walk With Me-followers.log").string(), 8 * 1024 * 1024, 32, false);
            log = std::make_shared<spdlog::logger>("follower-diagnostics", sink);
            log->set_pattern(fmt::format("[%Y-%m-%d %H:%M:%S.%e] [%l] [{} pid=%P] %v", Build));
            log->info("[Session] build={} compiled={} {} pid={} historySeconds=10 historyMax=256 "
                      "burstSeconds=10 persistentHz=1 retention=33x8MiB",
                      Build, __DATE__, __TIME__, GetCurrentProcessId());
            log->flush();
        }
        catch (const std::exception& e) {
            log.reset();
            logger::error("Follower diagnostics unavailable: {}", e.what());
        }
    }
} // namespace Wayfarer::Diagnostics
