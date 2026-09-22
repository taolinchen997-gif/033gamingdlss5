// Observe the game's existing Reflex integration. Never insert another Sleep,
// change frame caps/markers, or consume DLSSG's presented-frame counter.
// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdio>
#include <windows.h>
#include <streamline/sl_reflex.h>
#include <reshade.hpp>

namespace mfgunlock::reflex {
inline ::PFun_slReflexSetOptions* real_options = nullptr;
inline ::PFun_slReflexSleep* real_sleep = nullptr;
inline ::PFun_slReflexGetState* real_state = nullptr;
inline std::atomic<int> mode{-1};
inline std::atomic<unsigned> cap_us{0};
inline std::atomic<unsigned long long> sleep_calls{0}, sleep_errors{0}, sleep_ticks{0};
inline std::atomic<LONGLONG> sleep_max_ticks{0};
inline std::atomic<unsigned long long> next_report_ms{0};
inline std::atomic<int> report_available{-1}, low_latency_available{-1};
inline std::atomic<unsigned long long> render_latency_us{0}, gpu_frame_us{0};
inline std::atomic<unsigned long long> state_time_ms{0};

inline sl::Result SetOptions(const sl::ReflexOptions& o) {
    if (!real_options) return sl::Result::eErrorNotInitialized;
    const sl::Result result = real_options(o);
    static std::atomic<unsigned long long> previous{~0ull};
    const auto key = (static_cast<unsigned long long>(o.frameLimitUs) << 32) |
        (static_cast<unsigned long long>(static_cast<unsigned>(result)) << 8) | unsigned(o.mode);
    if (result == sl::Result::eOk) { mode.store(int(o.mode)); cap_us.store(o.frameLimitUs); }
    if (previous.exchange(key) != key) {
        char msg[320]; std::snprintf(msg, sizeof(msg),
            "[033-fg] ReflexSetOptions game_mode=%u frameLimitUs=%u markers=%u result=%u; unchanged",
            unsigned(o.mode), o.frameLimitUs, unsigned(o.useMarkersToOptimize), unsigned(result));
        reshade::log::message(reshade::log::level::info, msg);
    }
    return result;
}
inline sl::Result Sleep(const sl::FrameToken& frame) {
    if (!real_sleep) return sl::Result::eErrorNotInitialized;
    LARGE_INTEGER a{}, b{}; QueryPerformanceCounter(&a);
    const sl::Result result = real_sleep(frame);
    QueryPerformanceCounter(&b);
    const LONGLONG elapsed = b.QuadPart - a.QuadPart;
    sleep_calls.fetch_add(1, std::memory_order_relaxed);
    if (result != sl::Result::eOk) sleep_errors.fetch_add(1, std::memory_order_relaxed);
    sleep_ticks.fetch_add(elapsed, std::memory_order_relaxed);
    auto max = sleep_max_ticks.load(std::memory_order_relaxed);
    while (max < elapsed && !sleep_max_ticks.compare_exchange_weak(max, elapsed)) {}
    return result;
}
inline sl::Result GetState(sl::ReflexState& state) {
    if (!real_state) return sl::Result::eErrorNotInitialized;
    const sl::Result result = real_state(state);
    if (result != sl::Result::eOk) return result;
    state_time_ms.store(GetTickCount64());
    low_latency_available.store(state.lowLatencyAvailable ? 1 : 0);
    report_available.store(state.latencyReportAvailable ? 1 : 0);
    unsigned long long latency = 0, gpu = 0;
    if (state.latencyReportAvailable) {
        const sl::ReflexReport* newest = nullptr;
        for (const auto& r : state.frameReport)
            if (r.simStartTime && r.gpuRenderEndTime >= r.simStartTime &&
                (!newest || r.frameID > newest->frameID)) newest = &r;
        if (newest) { latency = newest->gpuRenderEndTime - newest->simStartTime; gpu = newest->gpuFrameTimeUs; }
    }
    render_latency_us.store(latency); gpu_frame_us.store(gpu);
    return result;
}
// Called from existing Present. Log only every five seconds; no new API calls.
inline void Report(unsigned mode_fg, unsigned generated) {
    static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
    if (reporting.test_and_set(std::memory_order_acquire)) return;
    struct Guard { std::atomic_flag& flag; ~Guard(){flag.clear(std::memory_order_release);} } guard{reporting};
    const auto now = GetTickCount64(); auto next = next_report_ms.load();
    if (now < next || !next_report_ms.compare_exchange_strong(next, now + 5000)) return;
    static unsigned long long previous_ms = 0, previous_calls = 0, previous_ticks = 0;
    const auto calls = sleep_calls.load(), ticks = sleep_ticks.load();
    const auto delta = calls - previous_calls;
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    char msg[640]; std::snprintf(msg, sizeof(msg),
        "[033-fg] accepted_mode=%u generated=%u Reflex_mode=%d cap_us=%u Sleep_calls=%llu "
        "Sleep_calls_per_s=%.2f Sleep_avg_ms=%.3f Sleep_max_ms=%.3f Sleep_errors=%llu "
        "Reflex_available=%d latency_report_available=%d report_age_ms=%lld latest_sim_to_gpu_ms=%.3f latest_gpu_frame_ms=%.3f "
        "(unobserved=-1; zero report=unavailable; call rate is not displayed FPS)",
        mode_fg, generated, mode.load(), cap_us.load(), calls,
        previous_ms && now > previous_ms ? double(delta)*1000.0/double(now-previous_ms) : 0.0,
        delta ? double(ticks-previous_ticks)*1000.0/double(frequency.QuadPart)/double(delta) : 0.0,
        double(sleep_max_ticks.exchange(0))*1000.0/double(frequency.QuadPart), sleep_errors.load(),
        low_latency_available.load(), report_available.load(), state_time_ms.load() ? static_cast<long long>(now-state_time_ms.load()) : -1ll,
        double(render_latency_us.load())/1000.0,
        double(gpu_frame_us.load())/1000.0);
    reshade::log::message(reshade::log::level::info, msg);
    previous_ms=now; previous_calls=calls; previous_ticks=ticks;
}
} // namespace mfgunlock::reflex
