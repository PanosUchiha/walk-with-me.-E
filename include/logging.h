#pragma once
#include "follower_diagnostic_log.h"

inline void SetupLog(std::string_view a_pluginName)
{
    auto path = logger::log_directory();
    if (!path) {
        SKSE::stl::report_and_fail("Walk With Me could not locate the SKSE log directory."sv);
    }

    *path /= fmt::format("{}.log", a_pluginName);
    // Keep the known-good startup sink. Renaming an existing log here can throw
    // under OneDrive/log readers and cause SKSE to disable the entire plugin.
    auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
    auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
    log->set_level(spdlog::level::info);
    log->flush_on(spdlog::level::info);
    spdlog::set_default_logger(std::move(log));
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    Wayfarer::Diagnostics::InitializeLog(path->parent_path());
    // Also preserve ordinary errors and state transitions in the retained stream.
    // Failure to create the optional diagnostic stream never blocks plugin load.
    if (Wayfarer::Diagnostics::log) {
        spdlog::default_logger()->sinks().push_back(Wayfarer::Diagnostics::log->sinks().front());
    }
    logger::info("[DiagnosticsBuild] {} compiled={} {}", Wayfarer::Diagnostics::Build, __DATE__, __TIME__);
}
