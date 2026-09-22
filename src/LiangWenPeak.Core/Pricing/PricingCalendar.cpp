#include "PricingCalendar.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace liangwenpeak::pricing
{
    namespace
    {
        class JsonCursor final
        {
        public:
            explicit JsonCursor(std::string_view const input) noexcept : m_input(input) {}

            [[nodiscard]] bool ParseCalendar(
                int& schemaVersion,
                std::string& timezone,
                std::map<int, std::vector<PricingCalendar::DateRange>>& years) noexcept
            {
                bool hasSchema = false;
                bool hasTimezone = false;
                bool hasYears = false;
                if (!Consume('{'))
                {
                    return false;
                }
                if (!Peek('}'))
                {
                    for (;;)
                    {
                        std::string key;
                        if (!ParseString(key) || !Consume(':'))
                        {
                            return false;
                        }
                        if (key == "schema_version")
                        {
                            hasSchema = ParseInteger(schemaVersion);
                            if (!hasSchema)
                            {
                                return false;
                            }
                        }
                        else if (key == "timezone")
                        {
                            hasTimezone = ParseString(timezone);
                            if (!hasTimezone)
                            {
                                return false;
                            }
                        }
                        else if (key == "years")
                        {
                            hasYears = ParseYears(years);
                            if (!hasYears)
                            {
                                return false;
                            }
                        }
                        else if (!SkipValue())
                        {
                            return false;
                        }

                        if (Consume('}'))
                        {
                            break;
                        }
                        if (!Consume(','))
                        {
                            return false;
                        }
                    }
                }
                else
                {
                    static_cast<void>(Consume('}'));
                }
                SkipWhitespace();
                return hasSchema && hasTimezone && hasYears && m_position == m_input.size();
            }

        private:
            void SkipWhitespace() noexcept
            {
                while (m_position < m_input.size()
                    && std::isspace(static_cast<unsigned char>(m_input[m_position])) != 0)
                {
                    ++m_position;
                }
            }

            [[nodiscard]] bool Peek(char const expected) noexcept
            {
                SkipWhitespace();
                return m_position < m_input.size() && m_input[m_position] == expected;
            }

            [[nodiscard]] bool Consume(char const expected) noexcept
            {
                if (!Peek(expected))
                {
                    return false;
                }
                ++m_position;
                return true;
            }

            [[nodiscard]] bool ParseString(std::string& result) noexcept
            {
                SkipWhitespace();
                if (m_position >= m_input.size() || m_input[m_position++] != '"')
                {
                    return false;
                }
                result.clear();
                while (m_position < m_input.size())
                {
                    const char value = m_input[m_position++];
                    if (value == '"')
                    {
                        return true;
                    }
                    if (static_cast<unsigned char>(value) < 0x20U)
                    {
                        return false;
                    }
                    if (value != '\\')
                    {
                        result.push_back(value);
                        continue;
                    }
                    if (m_position >= m_input.size())
                    {
                        return false;
                    }
                    const char escaped = m_input[m_position++];
                    switch (escaped)
                    {
                    case '"': result.push_back('"'); break;
                    case '\\': result.push_back('\\'); break;
                    case '/': result.push_back('/'); break;
                    case 'b': result.push_back('\b'); break;
                    case 'f': result.push_back('\f'); break;
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default: return false;
                    }
                }
                return false;
            }

            [[nodiscard]] bool ParseInteger(int& result) noexcept
            {
                SkipWhitespace();
                const auto start = m_position;
                if (m_position < m_input.size() && m_input[m_position] == '-')
                {
                    ++m_position;
                }
                while (m_position < m_input.size()
                    && m_input[m_position] >= '0' && m_input[m_position] <= '9')
                {
                    ++m_position;
                }
                if (m_position == start || (m_position == start + 1 && m_input[start] == '-'))
                {
                    return false;
                }
                const auto number = m_input.substr(start, m_position - start);
                const auto parsed = std::from_chars(number.data(), number.data() + number.size(), result);
                return parsed.ec == std::errc{} && parsed.ptr == number.data() + number.size();
            }

            [[nodiscard]] static std::optional<std::chrono::sys_days> ParseDate(
                std::string_view const value) noexcept
            {
                if (value.size() != 10 || value[4] != '-' || value[7] != '-')
                {
                    return std::nullopt;
                }
                const auto parsePart = [value](size_t const start, size_t const length) noexcept
                    -> std::optional<unsigned>
                {
                    unsigned result{};
                    const auto text = value.substr(start, length);
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
                    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
                    {
                        return std::nullopt;
                    }
                    return result;
                };
                const auto year = parsePart(0, 4);
                const auto month = parsePart(5, 2);
                const auto day = parsePart(8, 2);
                if (!year || !month || !day || *year > 9999U)
                {
                    return std::nullopt;
                }
                const std::chrono::year_month_day date{
                    std::chrono::year{ static_cast<int>(*year) },
                    std::chrono::month{ *month },
                    std::chrono::day{ *day } };
                if (!date.ok())
                {
                    return std::nullopt;
                }
                return std::chrono::sys_days{ date };
            }

            [[nodiscard]] bool ParseRange(PricingCalendar::DateRange& range) noexcept
            {
                std::string firstText;
                std::string lastText;
                if (!Consume('[') || !ParseString(firstText) || !Consume(',')
                    || !ParseString(lastText) || !Consume(']'))
                {
                    return false;
                }
                const auto first = ParseDate(firstText);
                const auto last = ParseDate(lastText);
                if (!first || !last || *last < *first)
                {
                    return false;
                }
                range = { *first, *last };
                return true;
            }

            [[nodiscard]] bool ParseRanges(
                std::vector<PricingCalendar::DateRange>& ranges) noexcept
            {
                if (!Consume('['))
                {
                    return false;
                }
                if (Consume(']'))
                {
                    return true;
                }
                for (;;)
                {
                    PricingCalendar::DateRange range{};
                    if (!ParseRange(range))
                    {
                        return false;
                    }
                    ranges.push_back(range);
                    if (Consume(']'))
                    {
                        return true;
                    }
                    if (!Consume(','))
                    {
                        return false;
                    }
                }
            }

            [[nodiscard]] bool ParseYear(
                std::vector<PricingCalendar::DateRange>& ranges) noexcept
            {
                bool hasRanges = false;
                if (!Consume('{'))
                {
                    return false;
                }
                if (Consume('}'))
                {
                    return false;
                }
                for (;;)
                {
                    std::string key;
                    if (!ParseString(key) || !Consume(':'))
                    {
                        return false;
                    }
                    if (key == "all_day_off_peak")
                    {
                        if (hasRanges || !ParseRanges(ranges))
                        {
                            return false;
                        }
                        hasRanges = true;
                    }
                    else if (!SkipValue())
                    {
                        return false;
                    }
                    if (Consume('}'))
                    {
                        return hasRanges;
                    }
                    if (!Consume(','))
                    {
                        return false;
                    }
                }
            }

            [[nodiscard]] bool ParseYears(
                std::map<int, std::vector<PricingCalendar::DateRange>>& years) noexcept
            {
                if (!Consume('{'))
                {
                    return false;
                }
                if (Consume('}'))
                {
                    return true;
                }
                for (;;)
                {
                    std::string yearText;
                    if (!ParseString(yearText) || !Consume(':'))
                    {
                        return false;
                    }
                    int year{};
                    const auto parsed = std::from_chars(
                        yearText.data(), yearText.data() + yearText.size(), year);
                    if (parsed.ec != std::errc{} || parsed.ptr != yearText.data() + yearText.size()
                        || year < 1 || year > 9999 || years.contains(year))
                    {
                        return false;
                    }
                    std::vector<PricingCalendar::DateRange> ranges;
                    if (!ParseYear(ranges))
                    {
                        return false;
                    }
                    years.emplace(year, std::move(ranges));
                    if (Consume('}'))
                    {
                        return true;
                    }
                    if (!Consume(','))
                    {
                        return false;
                    }
                }
            }

            [[nodiscard]] bool SkipValue() noexcept
            {
                SkipWhitespace();
                if (m_position >= m_input.size())
                {
                    return false;
                }
                if (m_input[m_position] == '"')
                {
                    std::string ignored;
                    return ParseString(ignored);
                }
                if (m_input[m_position] == '{')
                {
                    static_cast<void>(Consume('{'));
                    if (Consume('}'))
                    {
                        return true;
                    }
                    for (;;)
                    {
                        std::string key;
                        if (!ParseString(key) || !Consume(':') || !SkipValue())
                        {
                            return false;
                        }
                        if (Consume('}'))
                        {
                            return true;
                        }
                        if (!Consume(','))
                        {
                            return false;
                        }
                    }
                }
                if (m_input[m_position] == '[')
                {
                    static_cast<void>(Consume('['));
                    if (Consume(']'))
                    {
                        return true;
                    }
                    for (;;)
                    {
                        if (!SkipValue())
                        {
                            return false;
                        }
                        if (Consume(']'))
                        {
                            return true;
                        }
                        if (!Consume(','))
                        {
                            return false;
                        }
                    }
                }
                constexpr std::string_view literals[]{ "true", "false", "null" };
                for (auto const literal : literals)
                {
                    if (m_input.substr(m_position, literal.size()) == literal)
                    {
                        m_position += literal.size();
                        return true;
                    }
                }

                const auto start = m_position;
                while (m_position < m_input.size())
                {
                    const char value = m_input[m_position];
                    if ((value >= '0' && value <= '9') || value == '-' || value == '+'
                        || value == '.' || value == 'e' || value == 'E')
                    {
                        ++m_position;
                    }
                    else
                    {
                        break;
                    }
                }
                return m_position > start;
            }

            std::string_view m_input;
            size_t m_position{};
        };
    }

    PricingCalendar PricingCalendar::FromJson(std::string_view const json) noexcept
    {
        PricingCalendar calendar;
        try
        {
            int schemaVersion{};
            std::string timezone;
            std::map<int, std::vector<DateRange>> years;
            JsonCursor cursor{ json };
            if (!cursor.ParseCalendar(schemaVersion, timezone, years)
                || schemaVersion != 1 || timezone != "Asia/Shanghai")
            {
                return calendar;
            }
            calendar.m_years = std::move(years);
            calendar.m_loadedSuccessfully = true;
        }
        catch (...)
        {
            return {};
        }
        return calendar;
    }

    PricingCalendar PricingCalendar::LoadFromFile(std::filesystem::path const& path) noexcept
    {
        try
        {
            std::ifstream input{ path, std::ios::binary };
            if (!input)
            {
                return {};
            }
            const std::string json{
                std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
            return FromJson(json);
        }
        catch (...)
        {
            return {};
        }
    }

    std::filesystem::path PricingCalendar::ManagedFilePath() noexcept
    {
#ifdef _WIN32
        try
        {
            std::wstring buffer(512, L'\0');
            for (;;)
            {
                const DWORD length = ::GetModuleFileNameW(
                    nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0)
                {
                    return {};
                }
                if (length < buffer.size() - 1)
                {
                    buffer.resize(length);
                    return std::filesystem::path{ buffer }.parent_path()
                        / "resources" / "pricing-calendar.json";
                }
                if (buffer.size() > static_cast<size_t>((std::numeric_limits<DWORD>::max)() / 2U))
                {
                    return {};
                }
                buffer.resize(buffer.size() * 2U);
            }
        }
        catch (...)
        {
            return {};
        }
#else
        return std::filesystem::current_path() / "resources" / "pricing-calendar.json";
#endif
    }

    PricingCalendar PricingCalendar::LoadManaged() noexcept
    {
        const auto path = ManagedFilePath();
        return path.empty() ? PricingCalendar{} : LoadFromFile(path);
    }

    bool PricingCalendar::IsAllDayOffPeak(std::chrono::sys_days const date) const noexcept
    {
        for (auto const& [year, ranges] : m_years)
        {
            static_cast<void>(year);
            for (auto const& range : ranges)
            {
                if (date >= range.first && date <= range.last)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool PricingCalendar::HasCalendarData(std::chrono::year const year) const noexcept
    {
        if (m_years.contains(static_cast<int>(year)))
        {
            return true;
        }
        for (auto const& [managedYear, ranges] : m_years)
        {
            static_cast<void>(managedYear);
            for (auto const& range : ranges)
            {
                const auto firstYear = std::chrono::year_month_day{ range.first }.year();
                const auto lastYear = std::chrono::year_month_day{ range.last }.year();
                if (year >= firstYear && year <= lastYear)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool PricingCalendar::LoadedSuccessfully() const noexcept
    {
        return m_loadedSuccessfully;
    }
}
