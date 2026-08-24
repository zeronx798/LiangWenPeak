#include "SeriesIdentity.h"

#include <windows.h>
#include <bcrypt.h>

#include <stdexcept>

#pragma comment(lib, "bcrypt.lib")

namespace liangwenpeak::balance
{
    namespace
    {
        class AlgorithmHandle final
        {
        public:
            explicit AlgorithmHandle(BCRYPT_ALG_HANDLE value) noexcept : m_value(value) {}
            ~AlgorithmHandle()
            {
                if (m_value != nullptr)
                {
                    ::BCryptCloseAlgorithmProvider(m_value, 0);
                }
            }
            AlgorithmHandle(AlgorithmHandle const&) = delete;
            AlgorithmHandle& operator=(AlgorithmHandle const&) = delete;
            [[nodiscard]] BCRYPT_ALG_HANDLE Get() const noexcept { return m_value; }

        private:
            BCRYPT_ALG_HANDLE m_value{};
        };

        class HashHandle final
        {
        public:
            explicit HashHandle(BCRYPT_HASH_HANDLE value) noexcept : m_value(value) {}
            ~HashHandle()
            {
                if (m_value != nullptr)
                {
                    ::BCryptDestroyHash(m_value);
                }
            }
            HashHandle(HashHandle const&) = delete;
            HashHandle& operator=(HashHandle const&) = delete;
            [[nodiscard]] BCRYPT_HASH_HANDLE Get() const noexcept { return m_value; }

        private:
            BCRYPT_HASH_HANDLE m_value{};
        };

        void CheckStatus(NTSTATUS const status, char const* const operation)
        {
            if (!BCRYPT_SUCCESS(status))
            {
                throw std::runtime_error(operation);
            }
        }

        int HexValue(char const value)
        {
            if (value >= '0' && value <= '9')
            {
                return value - '0';
            }
            if (value >= 'a' && value <= 'f')
            {
                return value - 'a' + 10;
            }
            if (value >= 'A' && value <= 'F')
            {
                return value - 'A' + 10;
            }
            return -1;
        }
    }

    std::vector<std::uint8_t> GenerateHistoryIdentitySecret()
    {
        std::vector<std::uint8_t> secret(HistoryIdentitySecretSize);
        CheckStatus(
            ::BCryptGenRandom(
                nullptr,
                secret.data(),
                static_cast<ULONG>(secret.size()),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG),
            "BCryptGenRandom failed");
        return secret;
    }

    std::string ComputeSeriesId(
        std::span<std::uint8_t const> const secret,
        std::string_view const apiKey)
    {
        if (secret.size() != HistoryIdentitySecretSize || apiKey.empty())
        {
            throw std::invalid_argument("Series identity input is invalid");
        }

        BCRYPT_ALG_HANDLE rawAlgorithm{};
        CheckStatus(
            ::BCryptOpenAlgorithmProvider(
                &rawAlgorithm,
                BCRYPT_SHA256_ALGORITHM,
                nullptr,
                BCRYPT_ALG_HANDLE_HMAC_FLAG),
            "BCryptOpenAlgorithmProvider failed");
        AlgorithmHandle algorithm{ rawAlgorithm };

        DWORD objectLength{};
        DWORD resultLength{};
        CheckStatus(
            ::BCryptGetProperty(
                algorithm.Get(),
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &resultLength,
                0),
            "BCryptGetProperty object length failed");
        DWORD hashLength{};
        CheckStatus(
            ::BCryptGetProperty(
                algorithm.Get(),
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength),
                sizeof(hashLength),
                &resultLength,
                0),
            "BCryptGetProperty hash length failed");

        std::vector<std::uint8_t> objectBuffer(objectLength);
        BCRYPT_HASH_HANDLE rawHash{};
        CheckStatus(
            ::BCryptCreateHash(
                algorithm.Get(),
                &rawHash,
                objectBuffer.data(),
                static_cast<ULONG>(objectBuffer.size()),
                const_cast<PUCHAR>(secret.data()),
                static_cast<ULONG>(secret.size()),
                0),
            "BCryptCreateHash failed");
        HashHandle hash{ rawHash };
        CheckStatus(
            ::BCryptHashData(
                hash.Get(),
                reinterpret_cast<PUCHAR>(const_cast<char*>(apiKey.data())),
                static_cast<ULONG>(apiKey.size()),
                0),
            "BCryptHashData failed");

        std::vector<std::uint8_t> digest(hashLength);
        CheckStatus(
            ::BCryptFinishHash(hash.Get(), digest.data(), static_cast<ULONG>(digest.size()), 0),
            "BCryptFinishHash failed");
        return EncodeHex(digest);
    }

    std::string EncodeHex(std::span<std::uint8_t const> const bytes)
    {
        constexpr char digits[] = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);
        for (auto const byte : bytes)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 0x0f]);
        }
        return result;
    }

    std::vector<std::uint8_t> DecodeHex(std::string_view const text)
    {
        if (text.size() % 2 != 0)
        {
            throw std::invalid_argument("Hex input has an odd length");
        }
        std::vector<std::uint8_t> result;
        result.reserve(text.size() / 2);
        for (size_t index = 0; index < text.size(); index += 2)
        {
            const auto high = HexValue(text[index]);
            const auto low = HexValue(text[index + 1]);
            if (high < 0 || low < 0)
            {
                throw std::invalid_argument("Hex input contains an invalid character");
            }
            result.push_back(static_cast<std::uint8_t>((high << 4) | low));
        }
        return result;
    }
}
