#include "BalanceStatisticsTests.h"

#include "Balance/BalanceFormatter.h"
#include "Balance/BalanceForecastService.h"
#include "Balance/BalanceHistoryStore.h"
#include "Balance/BalanceModels.h"
#include "Balance/BalanceSettings.h"
#include "Balance/DecimalAmount.h"
#include "Balance/DeploymentPaths.h"
#include "Balance/SeriesIdentity.h"
#include "Time/BalanceRefreshSchedule.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace std::chrono_literals;
    using liangwenpeak::balance::BalanceForecast;
    using liangwenpeak::balance::BalanceForecastService;
    using liangwenpeak::balance::BalanceHistoryEntry;
    using liangwenpeak::balance::BalanceHistoryStore;
    using liangwenpeak::balance::BalanceSettings;
    using liangwenpeak::balance::DecimalAmount;
    using liangwenpeak::balance::HistoryEntryKind;
    using liangwenpeak::balance::PredictionAlgorithm;

    constexpr char SeriesA[] = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr char SeriesB[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    DecimalAmount Amount(std::string_view const text)
    {
        const auto value = DecimalAmount::TryParse(text);
        if (!value)
        {
            throw std::runtime_error("Invalid test decimal");
        }
        return *value;
    }

    std::chrono::sys_seconds At(std::chrono::seconds const elapsed)
    {
        return std::chrono::sys_seconds{ elapsed };
    }

    BalanceHistoryEntry Sample(
        char const* const series,
        std::chrono::seconds const timestamp,
        char const* const currency,
        std::string_view const balance)
    {
        return BalanceHistoryEntry::Sample(series, At(timestamp), currency, Amount(balance));
    }

    class TemporaryDirectory final
    {
    public:
        explicit TemporaryDirectory(std::string_view const name)
        {
            const auto suffix = std::to_string(
                std::chrono::high_resolution_clock::now().time_since_epoch().count());
            m_path = std::filesystem::temp_directory_path()
                / (std::string{ "LiangWenPeak-" } + std::string{ name } + '-' + suffix);
            std::filesystem::create_directories(m_path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }

        TemporaryDirectory(TemporaryDirectory const&) = delete;
        TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

        [[nodiscard]] std::filesystem::path const& Path() const noexcept
        {
            return m_path;
        }

    private:
        std::filesystem::path m_path;
    };

    void WriteText(std::filesystem::path const& path, std::string const& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << text;
        if (!output)
        {
            throw std::runtime_error("Unable to write test fixture");
        }
    }

    std::string ReadText(std::filesystem::path const& path)
    {
        std::ifstream input(path, std::ios::binary);
        return {
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{} };
    }

    void VerifyDecimalAndIdentity(auto const& expect)
    {
        const auto amount = DecimalAmount::TryParse("13.51000000");
        expect(amount && amount->ToString() == "13.51000000", "decimal amount round-trips eight fixed digits");
        expect(DecimalAmount::TryParse("0.00000001")->ScaledValue() == 1, "decimal amount preserves the smallest unit");
        expect(!DecimalAmount::TryParse("1.234567891"), "decimal amount rejects lossy ninth digits");
        expect(
            DecimalAmount::TryParse("1.2345678900")->ToString() == "1.23456789",
            "decimal amount accepts exact trailing zeros beyond storage precision");
        expect(Amount("10.00000001") > Amount("10.00000000"), "decimal comparison detects exact increases");

        std::vector<std::uint8_t> firstSecret(liangwenpeak::balance::HistoryIdentitySecretSize);
        for (size_t index = 0; index < firstSecret.size(); ++index)
        {
            firstSecret[index] = static_cast<std::uint8_t>(index);
        }
        auto secondSecret = firstSecret;
        secondSecret[0] ^= 0xff;
        const auto first = liangwenpeak::balance::ComputeSeriesId(firstSecret, "sk-test-key");
        const auto repeated = liangwenpeak::balance::ComputeSeriesId(firstSecret, "sk-test-key");
        const auto isolated = liangwenpeak::balance::ComputeSeriesId(secondSecret, "sk-test-key");
        expect(first.size() == 64, "series identity is a SHA-256 hex string");
        expect(first == repeated, "same secret and API key produce the same series identity");
        expect(first != isolated, "different identity secrets isolate the same API key");
        expect(
            liangwenpeak::balance::DecodeHex(liangwenpeak::balance::EncodeHex(firstSecret)) == firstSecret,
            "identity secret hex encoding round-trips");
        expect(
            liangwenpeak::balance::GenerateHistoryIdentitySecret().size()
                == liangwenpeak::balance::HistoryIdentitySecretSize,
            "CNG generates a 256-bit identity secret");
    }

    void VerifyIntervalsAndForecasts(auto const& expect)
    {
        const BalanceForecastService service;

        std::vector<BalanceHistoryEntry> seriesHistory = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 1h, "CNY", "18"),
            Sample(SeriesB, 2h, "CNY", "10"),
            Sample(SeriesB, 3h, "CNY", "9"),
        };
        auto intervals = liangwenpeak::balance::BuildValidIntervals(seriesHistory, "CNY");
        expect(intervals.size() == 2, "series change excludes only the cross-series interval");

        std::vector<BalanceHistoryEntry> rechargeHistory = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 1h, "CNY", "18"),
            Sample(SeriesA, 2h, "CNY", "68"),
            Sample(SeriesA, 3h, "CNY", "66"),
        };
        intervals = liangwenpeak::balance::BuildValidIntervals(rechargeHistory, "CNY");
        expect(intervals.size() == 2, "recharge excludes its interval but preserves both neighboring trends");

        std::vector<BalanceHistoryEntry> markerHistory = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 1h, "CNY", "18"),
            BalanceHistoryEntry::Marker(HistoryEntryKind::ApiOff, At(2h)),
            BalanceHistoryEntry::Marker(HistoryEntryKind::ApiOn, At(3h)),
            Sample(SeriesA, 4h, "CNY", "17"),
            Sample(SeriesA, 5h, "CNY", "16"),
        };
        intervals = liangwenpeak::balance::BuildValidIntervals(markerHistory, "CNY");
        expect(intervals.size() == 2, "API OFF and ON markers break only the crossing interval");

        const std::vector<BalanceHistoryEntry> zeroHistory = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 1h, "CNY", "20"),
        };
        const auto zero = service.Forecast(zeroHistory, "CNY", 1h, PredictionAlgorithm::SlidingAverage);
        expect(zero.hasValidInterval && zero.burnPerHour == 0.0L, "zero consumption is valid forecast data");

        const std::vector<BalanceHistoryEntry> longGap = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 9h, "CNY", "16"),
        };
        const auto last = service.Forecast(longGap, "CNY", 1min, PredictionAlgorithm::LastValidSample);
        expect(
            last.hasValidInterval && std::abs(last.burnPerHour - 4.0L / 9.0L) < 1e-12L,
            "last valid sample uses actual duration for a long gap");

        const auto clipped = service.Forecast(longGap, "CNY", 3h, PredictionAlgorithm::SlidingAverage);
        expect(
            clipped.hasValidInterval && std::abs(clipped.burnPerHour - 4.0L / 9.0L) < 1e-12L,
            "sliding average linearly clips a long interval at the window boundary");

        const std::vector<BalanceHistoryEntry> varying = {
            Sample(SeriesA, 0s, "CNY", "10"),
            Sample(SeriesA, 1h, "CNY", "9"),
            Sample(SeriesA, 2h, "CNY", "6"),
        };
        const auto sliding = service.Forecast(varying, "CNY", 2h, PredictionAlgorithm::SlidingAverage);
        const auto ewma = service.Forecast(varying, "CNY", 2h, PredictionAlgorithm::ExponentialAverage);
        expect(std::abs(sliding.burnPerHour - 2.0L) < 1e-12L, "sliding average weights consumption by valid duration");
        expect(ewma.burnPerHour > sliding.burnPerHour, "EWMA gives the newer faster interval more weight");
        expect(
            std::abs(ewma.burnPerHour - (7.0L / 3.0L)) < 1e-12L,
            "EWMA uses a half-life equal to half the rate window");

        const std::vector<BalanceHistoryEntry> uneven = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 30min, "CNY", "19"),
            Sample(SeriesA, 2h, "CNY", "16"),
        };
        const auto unevenSliding = service.Forecast(
            uneven, "CNY", 2h, PredictionAlgorithm::SlidingAverage);
        expect(
            unevenSliding.hasValidInterval
                && std::abs(unevenSliding.burnPerHour - 2.0L) < 1e-12L,
            "sliding average handles unequal interval durations");
        const auto oneIntervalEwma = service.Forecast(
            longGap, "CNY", 9h, PredictionAlgorithm::ExponentialAverage);
        expect(
            oneIntervalEwma.hasValidInterval
                && std::abs(oneIntervalEwma.burnPerHour - 4.0L / 9.0L) < 1e-12L,
            "a single irregular interval reduces EWMA to that interval rate");

        std::vector<BalanceHistoryEntry> robustHistory;
        auto balance = 100.0L;
        robustHistory.push_back(Sample(SeriesA, 0s, "CNY", "100"));
        for (int index = 1; index <= 10; ++index)
        {
            balance -= index == 5 ? 20.0L : 1.0L;
            const auto text = std::to_string(static_cast<double>(balance));
            robustHistory.push_back(BalanceHistoryEntry::Sample(
                SeriesA, At(std::chrono::hours{ index }), "CNY", *DecimalAmount::TryParse(text)));
        }
        const auto robust = service.Forecast(robustHistory, "CNY", 24h, PredictionAlgorithm::RobustTrend);
        expect(liangwenpeak::balance::HuberTuningConstant == 1.345L, "Huber tuning constant is fixed at 1.345");
        expect(robust.hasValidInterval && robust.burnPerHour >= 0.0L, "Huber forecast is available and clamped nonnegative");
        expect(robust.burnPerHour < 5.0L, "a single burst does not fully control the Huber trend");
        const auto robustZero = service.Forecast(
            zeroHistory, "CNY", 1h, PredictionAlgorithm::RobustTrend);
        expect(
            robustZero.hasValidInterval && robustZero.burnPerHour == 0.0L,
            "Huber treats a zero-consumption interval as valid data");
        const auto robustAcrossSeries = service.Forecast(
            seriesHistory, "CNY", 4h, PredictionAlgorithm::RobustTrend);
        expect(
            robustAcrossSeries.hasValidInterval
                && robustAcrossSeries.intervalCount == 2
                && robustAcrossSeries.burnPerHour >= 0.0L,
            "Huber uses valid intervals on both sides of a series boundary without joining them");
        const auto robustAcrossMarkers = service.Forecast(
            markerHistory, "CNY", 6h, PredictionAlgorithm::RobustTrend);
        expect(
            robustAcrossMarkers.hasValidInterval
                && robustAcrossMarkers.intervalCount == 2
                && robustAcrossMarkers.burnPerHour >= 0.0L,
            "Huber uses valid intervals on both sides of API markers without joining them");

        const std::vector<BalanceHistoryEntry> multiCurrency = {
            Sample(SeriesA, 0s, "CNY", "20"),
            Sample(SeriesA, 0s, "USD", "5"),
            Sample(SeriesA, 1h, "CNY", "18"),
            Sample(SeriesA, 1h, "USD", "4.5"),
        };
        const auto cny = service.Forecast(multiCurrency, "CNY", 1h, PredictionAlgorithm::SlidingAverage);
        const auto usd = service.Forecast(multiCurrency, "USD", 1h, PredictionAlgorithm::SlidingAverage);
        expect(cny.burnPerHour == 2.0L && usd.burnPerHour == 0.5L, "currencies have independent burn rates");
        const auto cnyEta = liangwenpeak::balance::CalculateEta(Amount("18"), Amount("10"), cny);
        const auto usdEta = liangwenpeak::balance::CalculateEta(Amount("4.5"), Amount("2"), usd);
        expect(
            cnyEta.remaining == 4h && usdEta.remaining == 5h,
            "currencies calculate independent ETAs from their own balances, warnings, and rates");
    }

    void VerifyEtaAndFormatting(auto const& expect)
    {
        using liangwenpeak::balance::CalculateEta;
        using liangwenpeak::balance::EtaResult;
        using liangwenpeak::balance::EtaState;
        using liangwenpeak::balance::FormatEta;

        const BalanceForecast unavailable{};
        expect(
            CalculateEta(Amount("9"), Amount("10"), unavailable).state == EtaState::AtThreshold,
            "warning threshold takes priority over insufficient data");
        expect(
            CalculateEta(Amount("20"), Amount("10"), unavailable).state == EtaState::InsufficientData,
            "missing intervals produce an acquiring ETA");
        expect(
            CalculateEta(Amount("20"), Amount("10"), BalanceForecast{ true, 0.0L }).state
                == EtaState::NoConsumption,
            "valid zero burn produces no ETA");

        const BalanceForecast rate{ true, 2.0L, PredictionAlgorithm::SlidingAverage, 1 };
        const auto eta = CalculateEta(Amount("20"), Amount("10"), rate);
        expect(eta.state == EtaState::Estimated && eta.remaining == 5h, "ETA uses latest balance minus warning");

        expect(FormatEta(EtaResult{ EtaState::Estimated, 59s }) == L"\u5c0f\u4e8e1\u5206\u949f", "ETA 59 seconds is less than one minute");
        expect(FormatEta(EtaResult{ EtaState::Estimated, 60s }) == L"\u7ea6 1 \u5206", "ETA 60 seconds is one minute");
        expect(FormatEta(EtaResult{ EtaState::Estimated, 5min + 6s }) == L"\u7ea6 5 \u5206 6 \u79d2", "ETA uses two natural units");
        expect(FormatEta(EtaResult{ EtaState::Estimated, 4h + 6s }) == L"\u7ea6 4 \u65f6 6 \u79d2", "ETA skips zero middle units");
        expect(
            FormatEta(EtaResult{ EtaState::Estimated, std::chrono::hours{ 24 * 63 + 4 } })
                == L"\u7ea6 2 \u6708 3 \u5929",
            "ETA formats months and days");
        expect(
            FormatEta(EtaResult{ EtaState::Estimated, std::chrono::hours{ 24 * 365 } }) == L"\u5927\u4e8e1\u5e74",
            "ETA exactly 365 days is greater than one year");
        expect(
            FormatEta(EtaResult{ EtaState::Estimated, std::chrono::hours{ 24 * 400 } }) == L"\u5927\u4e8e1\u5e74",
            "ETA over 365 days is greater than one year");
        expect(
            liangwenpeak::balance::FormatCurrencyAmount("CNY", Amount("13.51")) == L"\u00a5 13.51",
            "CNY amount uses the yuan symbol");
        expect(
            liangwenpeak::balance::FormatBurnRate("USD", 1.28L) == L"$ 1.28/\u65f6",
            "USD burn rate uses the dollar symbol");
        expect(
            liangwenpeak::balance::FormatCurrencyAmount("EUR", Amount("12.34")) == L"EUR 12.34",
            "unknown currencies use their code as a formatter fallback");
    }

    void VerifySettings(auto const& expect)
    {
        BalanceSettings settings;
        settings.refreshInterval = 10min;
        settings.rateWindow = 30min;
        settings.preferredAlgorithm = PredictionAlgorithm::RobustTrend;
        settings.knownCurrencies = { "CNY", "USD" };
        liangwenpeak::balance::SetWarningBalance(settings, "CNY", Amount("10"));
        liangwenpeak::balance::SetWarningBalance(settings, "USD", Amount("2"));
        liangwenpeak::balance::SettingsDraft draft(settings, true);

        const auto available = liangwenpeak::balance::GetAvailableRateWindows(10min);
        expect(available.front() == 10min, "rate window options filter values below refresh interval");
        draft.SetRefreshInterval(60min);
        expect(draft.Settings().rateWindow == 1h, "raising refresh interval raises an invalid draft window");
        draft.SetRefreshInterval(5min);
        expect(draft.Settings().rateWindow == 1h, "lowering refresh interval preserves the rate window");
        draft.SetRateWindow(5min);
        expect(
            liangwenpeak::balance::GetEffectiveAlgorithm(draft.Settings()) == PredictionAlgorithm::LastValidSample,
            "shortest rate window forces last-valid-sample behavior");
        expect(
            draft.Settings().preferredAlgorithm == PredictionAlgorithm::RobustTrend,
            "forced effective algorithm preserves the preferred algorithm");
        draft.SetRateWindow(30min);
        expect(
            liangwenpeak::balance::GetEffectiveAlgorithm(draft.Settings()) == PredictionAlgorithm::RobustTrend,
            "expanding the window restores the preferred algorithm");

        draft.RequestApiKeyClear();
        draft.Settings().apiFeatureEnabled = false;
        draft.Settings().forecastEnabled = true;
        draft.Settings().selectedCurrency = "USD";
        draft.Settings().knownCurrencies = { "EUR" };
        liangwenpeak::balance::SetWarningBalance(draft.Settings(), "CNY", Amount("99"));
        expect(draft.KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Clear, "API key clear is a draft action");
        draft.Cancel();
        expect(
            draft.KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Keep
                && draft.Settings().apiFeatureEnabled
                && !draft.Settings().forecastEnabled
                && draft.Settings().selectedCurrency == "CNY"
                && draft.Settings().refreshInterval == 10min
                && draft.Settings().rateWindow == 30min
                && draft.Settings().preferredAlgorithm == PredictionAlgorithm::RobustTrend
                && draft.Settings().knownCurrencies == std::vector<std::string>{ "CNY", "USD" }
                && liangwenpeak::balance::GetWarningBalance(draft.Settings(), "CNY") == Amount("10")
                && liangwenpeak::balance::GetWarningBalance(draft.Settings(), "USD") == Amount("2"),
            "cancel discards the complete settings and API key draft");
        draft.RequestApiKeyClear();
        draft.UndoApiKeyClear();
        expect(draft.KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Keep, "undo preserves the stored API key");
        draft.RequestApiKeyClear();
        draft.OnApiKeyInputChanged(true);
        expect(draft.KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Replace, "typing after clear changes the action to replace");

        expect(
            liangwenpeak::balance::GetWarningBalance(settings, "CNY") == Amount("10")
                && liangwenpeak::balance::GetWarningBalance(settings, "USD") == Amount("2")
                && liangwenpeak::balance::GetWarningBalance(settings, "EUR") == Amount("0"),
            "warning balances are independent per currency");

        settings.selectedCurrency = "CNY";
        expect(
            liangwenpeak::balance::ReconcileSelectedCurrency(settings, { "USD", "EUR" })
                && settings.selectedCurrency == "USD",
            "missing selected currency switches to the first response currency");
        expect(
            !liangwenpeak::balance::ReconcileSelectedCurrency(settings, { "EUR", "USD" })
                && settings.selectedCurrency == "USD",
            "an available selected currency remains stable");

        using liangwenpeak::balance::BalanceRefreshReason;
        expect(
            liangwenpeak::balance::WritesHistory(BalanceRefreshReason::ScheduledSample),
            "scheduled refresh writes history");
        expect(
            !liangwenpeak::balance::WritesHistory(BalanceRefreshReason::ManualObservation)
                && !liangwenpeak::balance::WritesHistory(BalanceRefreshReason::StartupObservation)
                && !liangwenpeak::balance::WritesHistory(BalanceRefreshReason::SavedKeyObservation)
                && !liangwenpeak::balance::WritesHistory(BalanceRefreshReason::ApiReenabledObservation),
            "manual and immediate observations never write history");

        const std::vector<liangwenpeak::balance::BalanceValue> responseBalances = {
            { "CNY", Amount("13.51") },
            { "USD", Amount("1.89") },
        };
        for (auto const observationReason : {
                 BalanceRefreshReason::ManualObservation,
                 BalanceRefreshReason::StartupObservation,
                 BalanceRefreshReason::SavedKeyObservation,
                 BalanceRefreshReason::ApiReenabledObservation })
        {
            expect(
                liangwenpeak::balance::CreateHistorySampleBatch(
                    observationReason, At(15min), SeriesA, responseBalances).empty(),
                "observation responses cannot create history samples");
        }
        const auto scheduledBatch = liangwenpeak::balance::CreateHistorySampleBatch(
            BalanceRefreshReason::ScheduledSample,
            At(15min),
            SeriesA,
            responseBalances);
        expect(
            scheduledBatch.size() == 2
                && scheduledBatch[0].timestamp == At(15min)
                && scheduledBatch[1].timestamp == At(15min),
            "a scheduled response records every currency at the planned target timestamp");
        bool missingTargetRejected = false;
        try
        {
            static_cast<void>(liangwenpeak::balance::CreateHistorySampleBatch(
                BalanceRefreshReason::ScheduledSample,
                std::nullopt,
                SeriesA,
                responseBalances));
        }
        catch (std::invalid_argument const&)
        {
            missingTargetRejected = true;
        }
        expect(missingTargetRejected, "scheduled history cannot use an HTTP completion timestamp fallback");

        expect(
            !liangwenpeak::time::IsCurrentRefreshTarget(At(599s), At(600s), 5min)
                && liangwenpeak::time::IsCurrentRefreshTarget(At(600s), At(600s), 5min)
                && liangwenpeak::time::IsCurrentRefreshTarget(At(899s), At(600s), 5min)
                && !liangwenpeak::time::IsCurrentRefreshTarget(At(900s), At(600s), 5min),
            "scheduled targets run at most once and a sleep across another boundary is not backfilled");
    }

    void VerifyHistoryAndPaths(auto const& expect)
    {
        std::cout << "  [History] append/load\n" << std::flush;
        TemporaryDirectory fixture{ "history" };
        BalanceHistoryStore store{ fixture.Path() / "data" };
        expect(!store.Load().archivedInvalidHistory, "missing active history is created cleanly");
        store.AppendSamples({
            Sample(SeriesA, 1h, "CNY", "13.51"),
            Sample(SeriesA, 1h, "USD", "1.89"),
        });
        store.AppendMarker(HistoryEntryKind::ApiOff, At(2h));
        BalanceHistoryStore reloaded{ fixture.Path() / "data" };
        expect(!reloaded.Load().archivedInvalidHistory && reloaded.Entries().size() == 3, "CSV loads samples and API markers");
        expect(
            reloaded.Entries()[0].timestamp == reloaded.Entries()[1].timestamp
                && reloaded.Entries()[0].currency == "CNY"
                && reloaded.Entries()[1].currency == "USD",
            "one scheduled timestamp stores every returned currency");
        expect(
            ReadText(reloaded.ActivePath()).find("@API_OFF,7200,,\n") != std::string::npos,
            "API state markers use the documented four-column CSV schema");
        bool outOfOrderRejected = false;
        try
        {
            reloaded.AppendSamples({ Sample(SeriesA, 30min, "CNY", "13.60") });
        }
        catch (std::invalid_argument const&)
        {
            outOfOrderRejected = true;
        }
        expect(outOfOrderRejected, "active history rejects timestamps older than its append tail");

        {
            std::ofstream append(reloaded.ActivePath(), std::ios::binary | std::ios::app);
            append << "truncated-final";
        }
        BalanceHistoryStore truncated{ fixture.Path() / "data" };
        expect(
            !truncated.Load().archivedInvalidHistory && truncated.Entries().size() == 3,
            "a truncated final append row is ignored");
        truncated.AppendSamples({ Sample(SeriesA, 3h, "CNY", "12.50") });
        BalanceHistoryStore repaired{ fixture.Path() / "data" };
        expect(
            !repaired.Load().archivedInvalidHistory && repaired.Entries().size() == 4,
            "a repaired truncated tail remains appendable on the next launch");

        std::cout << "  [History] corruption archive\n" << std::flush;
        TemporaryDirectory corruptFixture{ "corrupt-history" };
        const auto corruptData = corruptFixture.Path() / "data";
        std::filesystem::create_directories(corruptData / "history");
        WriteText(
            corruptData / "balance-history.csv",
            std::string{ BalanceHistoryStore::Header } + "\n"
                + SeriesA + ",0,CNY,20.00000000\n"
                + "corrupt,middle,row\n"
                + SeriesA + ",3600,CNY,19.00000000\n");
        BalanceHistoryStore corrupt{ corruptData };
        const auto corruptResult = corrupt.Load(At(10h));
        expect(corruptResult.archivedInvalidHistory, "corruption in the middle archives the whole active history");
        expect(
            corruptResult.archivedPath && std::filesystem::exists(*corruptResult.archivedPath),
            "invalid history archive is retained");
        expect(ReadText(corrupt.ActivePath()) == std::string{ BalanceHistoryStore::Header } + "\n", "corruption creates a clean active history");

        std::cout << "  [History] rollover\n" << std::flush;
        TemporaryDirectory rolloverFixture{ "rollover" };
        BalanceHistoryStore rollover{ rolloverFixture.Path() / "data" };
        static_cast<void>(rollover.Load());
        rollover.AppendSamples({ Sample(SeriesA, 0s, "CNY", "20") });
        const auto archive = rollover.Rollover(At(20h));
        expect(std::filesystem::exists(archive), "history rollover retains the previous active file");
        expect(rollover.Entries().empty(), "history rollover clears only the in-memory active set");
        expect(ReadText(rollover.ActivePath()) == std::string{ BalanceHistoryStore::Header } + "\n", "history rollover creates a new active file");
        const std::vector<liangwenpeak::balance::BalanceValue> rolloverObservation = {
            { "CNY", Amount("19") },
        };
        rollover.AppendSamples(liangwenpeak::balance::CreateHistorySampleBatch(
            liangwenpeak::balance::BalanceRefreshReason::ManualObservation,
            At(21h),
            SeriesA,
            rolloverObservation));
        expect(
            rollover.Entries().empty(),
            "a manual observation after reset cannot restart scheduled history");
        rollover.AppendSamples(liangwenpeak::balance::CreateHistorySampleBatch(
            liangwenpeak::balance::BalanceRefreshReason::ScheduledSample,
            At(21h),
            SeriesA,
            rolloverObservation));
        expect(
            rollover.Entries().size() == 1,
            "forecast input contains only the new active history and does not auto-load archives");
        const BalanceForecastService forecastService;
        expect(
            !forecastService.Forecast(
                rollover.Entries(), "CNY", 1h, PredictionAlgorithm::SlidingAverage).hasValidInterval,
            "one scheduled sample after reset still reports insufficient forecast data");
        rollover.AppendSamples(liangwenpeak::balance::CreateHistorySampleBatch(
            liangwenpeak::balance::BalanceRefreshReason::ScheduledSample,
            At(22h),
            SeriesA,
            { { "CNY", Amount("18") } }));
        expect(
            forecastService.Forecast(
                rollover.Entries(), "CNY", 1h, PredictionAlgorithm::SlidingAverage).hasValidInterval,
            "a later scheduled sample forms the first new post-reset interval");

        std::cout << "  [History] rollback\n" << std::flush;
        TemporaryDirectory rollbackFixture{ "rollover-rollback" };
        BalanceHistoryStore rollbackSeed{ rollbackFixture.Path() / "data" };
        static_cast<void>(rollbackSeed.Load());
        rollbackSeed.AppendSamples({ Sample(SeriesA, 0s, "CNY", "20") });
        BalanceHistoryStore failing{
            rollbackFixture.Path() / "data",
            [](std::filesystem::path const&) { throw std::runtime_error("injected create failure"); } };
        static_cast<void>(failing.Load());
        bool threw = false;
        try
        {
            static_cast<void>(failing.Rollover(At(21h)));
        }
        catch (...)
        {
            threw = true;
        }
        expect(threw && std::filesystem::exists(failing.ActivePath()), "failed rollover restores the old active file");
        expect(ReadText(failing.ActivePath()).find(SeriesA) != std::string::npos, "rollover rollback preserves old history content");

        std::cout << "  [History] deployment paths\n" << std::flush;
        const auto portableExecutable = fixture.Path() / "portable" / "app-1.0.0" / "LiangWenPeak.App.exe";
        expect(
            liangwenpeak::balance::ResolveDeploymentRoot(portableExecutable)
                == std::filesystem::absolute(fixture.Path() / "portable").lexically_normal(),
            "portable app resolves its launcher deployment root");
        expect(
            liangwenpeak::balance::ResolveDataRoot(portableExecutable)
                == std::filesystem::absolute(fixture.Path() / "portable" / "data").lexically_normal(),
            "portable app resolves data beside the launcher");
        expect(
            liangwenpeak::balance::ResolveDataRoot(portableExecutable, fixture.Path() / "injected")
                == std::filesystem::absolute(fixture.Path() / "injected").lexically_normal(),
            "data root supports explicit test injection");
    }
}

void VerifyBalanceStatistics(
    std::function<void(bool, std::string_view)> const& expect)
{
    std::cout << "[Balance] Decimal and identity\n" << std::flush;
    VerifyDecimalAndIdentity(expect);
    std::cout << "[Balance] Intervals and forecasts\n" << std::flush;
    VerifyIntervalsAndForecasts(expect);
    std::cout << "[Balance] ETA and formatting\n" << std::flush;
    VerifyEtaAndFormatting(expect);
    std::cout << "[Balance] Settings draft\n" << std::flush;
    VerifySettings(expect);
    std::cout << "[Balance] History and paths\n" << std::flush;
    VerifyHistoryAndPaths(expect);
}
