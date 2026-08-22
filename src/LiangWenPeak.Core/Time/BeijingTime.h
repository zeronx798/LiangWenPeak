#pragma once

#include <chrono>

namespace liangwenpeak::time
{
    class BeijingTime final
    {
    public:
        static BeijingTime Now();
        static BeijingTime FromUtc(std::chrono::system_clock::time_point utcTime);

        [[nodiscard]] std::chrono::sys_seconds UtcInstant() const noexcept;
        [[nodiscard]] std::chrono::sys_days LocalDate() const noexcept;
        [[nodiscard]] std::chrono::weekday DayOfWeek() const noexcept;
        [[nodiscard]] std::chrono::seconds TimeOfDay() const noexcept;

    private:
        BeijingTime(
            std::chrono::sys_seconds utcInstant,
            std::chrono::sys_days localDate,
            std::chrono::seconds timeOfDay) noexcept;

        std::chrono::sys_seconds m_utcInstant;
        std::chrono::sys_days m_localDate;
        std::chrono::seconds m_timeOfDay;
    };
}
