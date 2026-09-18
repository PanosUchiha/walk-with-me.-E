// Exercise the real logging bootstrap without loading Skyrim. Only SKSE's
// directory lookup is substituted; the production sinks and setup run unchanged.
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
using namespace std::literals;
std::filesystem::path testDirectory;
namespace logger
{
    std::optional<std::filesystem::path> log_directory()
    {
        return testDirectory;
    }
    using spdlog::error;
    using spdlog::info;
} // namespace logger
namespace SKSE::stl
{
    void report_and_fail(std::string_view message)
    {
        throw std::runtime_error(std::string(message));
    }
} // namespace SKSE::stl
#include "logging.h"

int main(int argc, char** argv)
{
    if (argc != 3) {
        return 2;
    }
    const std::string mode = argv[2];
    testDirectory = std::filesystem::path(argv[1]) / (mode + "-" + std::to_string(GetCurrentProcessId()));
    std::filesystem::create_directories(testDirectory);
    const auto mainPath = testDirectory / "Bootstrap.log";
    const auto diagnosticPath = testDirectory / "Walk With Me-followers.log";
    HANDLE held = INVALID_HANDLE_VALUE;
    auto lockFile = [&](const std::filesystem::path& file) {
        held = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (held == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("test could not lock file against renaming");
        }
    };
    try {
        if (mode == "existing" || mode == "main-locked") {
            std::ofstream out(mainPath);
            out << "PREVIOUS MAIN SESSION\n";
            out.close();
            if (mode == "main-locked") {
                lockFile(mainPath);
            }
        }
        if (mode == "diagnostic-locked" || mode == "oversized-locked" || mode == "repeat") {
            std::ofstream out(diagnosticPath);
            out << "PREVIOUS DIAGNOSTIC SESSION\n";
            if (mode == "oversized-locked") {
                out << std::string(8 * 1024 * 1024, 'x');
            }
            out.close();
            if (mode != "repeat") {
                lockFile(diagnosticPath);
            }
        }
        if (mode == "diagnostic-unavailable") {
            std::filesystem::create_directory(diagnosticPath);
        }
        SetupLog("Bootstrap");
        logger::info("REGISTRATION CAN PROCEED");
        spdlog::default_logger()->flush();
        if (mode == "repeat") {
            SetupLog("Bootstrap");
            logger::info("SECOND SESSION REGISTRATION");
            spdlog::default_logger()->flush();
        }
        const bool expectedDiagnostic = mode != "diagnostic-unavailable";
        if (bool(Wayfarer::Diagnostics::log) != expectedDiagnostic) {
            throw std::runtime_error("unexpected diagnostic availability");
        }
        std::ifstream primary(mainPath);
        const std::string mainText((std::istreambuf_iterator<char>(primary)), {});
        if (mainText.find(mode == "repeat" ? "SECOND SESSION REGISTRATION" : "REGISTRATION CAN PROCEED") ==
            std::string::npos) {
            throw std::runtime_error("primary log did not reach registration checkpoint");
        }
        if (mode == "repeat" || mode == "diagnostic-locked") {
            std::ifstream retained(diagnosticPath);
            const std::string retainedText((std::istreambuf_iterator<char>(retained)), {});
            if (retainedText.find("PREVIOUS DIAGNOSTIC SESSION") == std::string::npos ||
                retainedText.find("REGISTRATION") == std::string::npos) {
                throw std::runtime_error("retained stream lost a session or did not receive primary events");
            }
        }
        if (mode == "diagnostic-unavailable" &&
            mainText.find("Follower diagnostics unavailable") == std::string::npos) {
            throw std::runtime_error("diagnostic failure was not reported in the main log");
        }
        if (held != INVALID_HANDLE_VALUE) {
            CloseHandle(held);
        }
        std::cout << "Logging bootstrap passed: " << mode << '\n';
        return 0;
    }
    catch (const std::exception& e) {
        if (held != INVALID_HANDLE_VALUE) {
            CloseHandle(held);
        }
        std::cerr << "Logging bootstrap failed: " << mode << ": " << e.what() << '\n';
        return 1;
    }
}
