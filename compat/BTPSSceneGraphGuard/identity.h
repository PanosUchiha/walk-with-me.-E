#pragma once
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <span>
#include <vector>

namespace Guard
{
inline bool Sha256(std::span<const unsigned char> bytes, std::array<unsigned char,32>& output)
{
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (bytes.size() > ULONG_MAX || BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return false;
    ULONG length{}, received{};
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&length),
                                sizeof(length), &received, 0) >= 0;
    std::vector<unsigned char> storage(ok ? length : 0);
    if (ok) ok = BCryptCreateHash(algorithm, &hash, storage.data(), length, nullptr, 0, 0) >= 0;
    if (ok) ok = BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0;
    if (ok) ok = BCryptFinishHash(hash, output.data(), static_cast<ULONG>(output.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return ok;
}
}
