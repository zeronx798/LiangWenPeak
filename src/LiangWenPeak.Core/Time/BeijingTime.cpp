#include "BeijingTime.h"

namespace liangwenpeak::time
{
    namespace
    {
        constexpr auto BeijingUtcOffset = std::chrono::hours{ 8 };
    }

    BeijingTime BeijingTime::Now()
    {
        return FromUtc(std::chrono::system_clock::now());
    }

    BeijingTime BeijingTime::FromUtc(std::chrono::system_clock::time_point const utcTime)
    {
        const auto utcInstant = std::chrono::floor<std::chrono::seconds>(utcTime);
        const auto beijingInstant = utcInstant + BeijingUtcOffset;
        const auto beijingDay = std::chrono::floor<std::chrono::days>(beijingInstant);
        const auto timeOfDay = std::chrono::duration_cast<std::chrono::seconds>(beijingInstant - beijingDay);
        return BeijingTime{ utcInstant, beijingDay, timeOfDay };
    }

    std::chrono::sys_seconds BeijingTime::UtcInstant() const noexcept
    {
        return m_utcInstant;
    }

    std::chrono::sys_days BeijingTime::LocalDate() const noexcept
    {
        return m_localDate;
    }

    std::chrono::weekday BeijingTime::DayOfWeek() const noexcept
    {
        return std::chrono::weekday{ m_localDate };
    }

    std::chrono::seconds BeijingTime::TimeOfDay() const noexcept
    {
        return m_timeOfDay;
    }

    BeijingTime::BeijingTime(
        std::chrono::sys_seconds const utcInstant,
        std::chrono::sys_days const localDate,
        std::chrono::seconds const timeOfDay) noexcept
        : m_utcInstant(utcInstant), m_localDate(localDate), m_timeOfDay(timeOfDay)
    {
    }
}
