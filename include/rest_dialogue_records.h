#pragma once
#include "puff.h"
#include "rest_dialogue_policy.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Wayfarer::RestDialogue
{
    struct InfoMetadata
    {
        bool valid{}, hasPrompt{};
        bool scripted{};
        SpokenContent content{SpokenContent::None};
        std::string prompt;
        std::uint32_t speaker{};
        std::vector<std::uint32_t> links;
    };
    inline std::uint32_t ReadU32(std::span<const std::uint8_t> data, std::size_t at)
    {
        return static_cast<std::uint32_t>(data[at]) | (static_cast<std::uint32_t>(data[at + 1]) << 8) |
               (static_cast<std::uint32_t>(data[at + 2]) << 16) |
               (static_cast<std::uint32_t>(data[at + 3]) << 24);
    }
    inline InfoMetadata ParseInfoMetadata(std::span<const std::uint8_t> data, bool localized)
    {
        InfoMetadata result;
        std::uint32_t extended{};
        std::size_t at{};
        while (at + 6 <= data.size()) {
            const std::string_view type(reinterpret_cast<const char*>(data.data() + at), 4);
            auto size = static_cast<std::uint32_t>(data[at + 4] | (data[at + 5] << 8));
            at += 6;
            if (type == "XXXX") {
                if (extended || size != 4 || at + 4 > data.size()) {
                    return {};
                }
                extended = ReadU32(data, at);
                at += 4;
                continue;
            }
            if (extended) {
                size = extended;
                extended = 0;
            }
            if (size > data.size() - at) {
                return {};
            }
            if (type == "TCLT" || type == "ANAM") {
                if (size != 4) {
                    return {};
                }
                if (type == "TCLT") {
                    result.links.push_back(ReadU32(data, at));
                }
                else {
                    result.speaker = ReadU32(data, at);
                }
            }
            else if (type == "RNAM" && !localized) {
                result.hasPrompt = true;
                result.prompt.assign(reinterpret_cast<const char*>(data.data() + at), size);
                if (!result.prompt.empty() && result.prompt.back() == '\0') {
                    result.prompt.pop_back();
                }
            }
            else if (type == "NAM1") {
                SpokenContent content{};
                if (localized) {
                    if (size != 4) {
                        return {};
                    }
                    // A localized response is a string-table ID, never English
                    // bytes. Preserve it without guessing whether it says "Okay".
                    content = ReadU32(data, at) ? SpokenContent::Conversation : SpokenContent::None;
                }
                else {
                    const std::string_view response(reinterpret_cast<const char*>(data.data() + at), size);
                    content = ResponseContent(response);
                }
                result.content = std::max(result.content, content);
            }
            else if (type == "VMAD") {
                if (size < 6) {
                    return {};
                }
                // Script bindings are supporting evidence only. Never discard
                // quest dialogue merely because it has a fragment attached.
                const bool attachedScripts = data[at + 4] || data[at + 5];
                const bool fragmentWithoutScripts = !attachedScripts && size >= 8 && (data[at + 7] & 3);
                result.scripted = attachedScripts || fragmentWithoutScripts;
            }
            at += size;
        }
        result.valid = at == data.size() && !extended;
        return result.valid ? result : InfoMetadata{};
    }

    using InfoRecords = std::unordered_map<std::uint32_t, InfoMetadata>;

    inline std::vector<std::uint8_t> InflateInfoRecord(std::span<const std::uint8_t> bytes)
    {
        // Compressed TES4 records have a little-endian output size followed by
        // a zlib stream. Validate the wrapper, exact lengths and Adler checksum.
        if (bytes.size() < 10) {
            return {};
        }
        const auto expected = ReadU32(bytes, 0);
        if (!expected || expected > 4 * 1024 * 1024 || (bytes[4] & 15) != 8 || (bytes[4] >> 4) > 7 ||
            (bytes[5] & 32) || ((bytes[4] << 8) + bytes[5]) % 31 != 0) {
            return {};
        }
        std::vector<std::uint8_t> result(expected);
        unsigned long output = expected, input = static_cast<unsigned long>(bytes.size() - 10);
        int status{};
        {
            // Upstream puff initializes a shared fixed-code table lazily.
            static std::mutex inflateMutex;
            std::lock_guard lock(inflateMutex);
            status = puff(result.data(), &output, bytes.data() + 6, &input);
        }
        if (status != 0 || output != expected || input != bytes.size() - 10) {
            return {};
        }
        std::uint32_t a = 1, b = 0;
        for (const auto value : result) {
            a = (a + value) % 65521;
            b = (b + a) % 65521;
        }
        const auto last = bytes.size() - 4;
        const auto checksum = (static_cast<std::uint32_t>(bytes[last]) << 24) |
                              (static_cast<std::uint32_t>(bytes[last + 1]) << 16) |
                              (static_cast<std::uint32_t>(bytes[last + 2]) << 8) | bytes[last + 3];
        return ((b << 16) | a) == checksum ? result : std::vector<std::uint8_t>{};
    }

    // Read only the DIAL groups, on a worker thread. Use record headers instead
    // of engine file offsets, which can point inside an INFO's response data.
    inline InfoRecords ReadInfoRecords(const std::filesystem::path& path, bool localized)
    {
        InfoRecords records;
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) {
            return records;
        }
        const auto end = static_cast<std::uint64_t>(input.tellg());
        auto visit = [&](auto&& self, std::uint64_t at, std::uint64_t limit, unsigned depth) -> bool {
            if (depth > 16) {
                return false;
            }
            while (at + 24 <= limit) {
                std::array<std::uint8_t, 24> header{};
                input.seekg(static_cast<std::streamoff>(at));
                if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) {
                    return false;
                }
                const std::string_view type(reinterpret_cast<const char*>(header.data()), 4);
                const auto size = ReadU32(header, 4);
                if (type == "GRUP") {
                    if (size < 24 || size > limit - at) {
                        return false;
                    }
                    const std::string_view label(reinterpret_cast<const char*>(header.data() + 8), 4);
                    if ((depth || label == "DIAL") && !self(self, at + 24, at + size, depth + 1)) {
                        return false;
                    }
                    at += size;
                    continue;
                }
                if (size > limit - at - 24) {
                    return false;
                }
                if (type == "INFO" && size && size <= 4 * 1024 * 1024 && (ReadU32(header, 8) & 0x20) == 0) {
                    std::vector<std::uint8_t> bytes(size);
                    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) {
                        return false;
                    }
                    if (ReadU32(header, 8) & 0x40000) {
                        bytes = InflateInfoRecord(bytes);
                    }
                    if (!bytes.empty()) {
                        records[ReadU32(header, 12)] = ParseInfoMetadata(bytes, localized);
                    }
                }
                at += 24 + size;
            }
            return at == limit;
        };
        if (!visit(visit, 0, end, 0)) {
            return {};
        }
        return records;
    }
} // namespace Wayfarer::RestDialogue
