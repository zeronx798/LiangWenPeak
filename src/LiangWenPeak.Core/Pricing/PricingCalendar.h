#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string_view>
#include <vector>

namespace liangwenpeak::pricing
{
    class PricingCalendar final
    {
    public:
        struct DateRange
        {
            std::chrono::sys_days first;
            std::chrono::sys_days last;
        };

        PricingCalendar() = default;

        [[nodiscard]] static PricingCalendar FromJson(std::string_view json) noexcept;
        [[nodiscard]] static PricingCalendar LoadFromFile(
            std::filesystem::path const& path) noexcept;
        [[nodiscard]] static PricingCalendar LoadManaged() noexcept;
        [[nodiscard]] static std::filesystem::path ManagedFilePath() noexcept;

        [[nodiscard]] bool IsAllDayOffPeak(std::chrono::sys_days date) const noexcept;
        [[nodiscard]] bool HasCalendarData(std::chrono::year year) const noexcept;
        [[nodiscard]] bool LoadedSuccessfully() const noexcept;

    private:
        std::map<int, std::vector<DateRange>> m_years;
        bool m_loadedSuccessfully = false;
    };
}
