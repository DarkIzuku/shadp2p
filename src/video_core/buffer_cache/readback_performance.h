// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>

#include "common/types.h"

namespace VideoCore {

struct ReadbackPerformanceCounters {
    u64 flush_requests = 0;
    u64 gpu_waits = 0;
    u64 gpu_wait_us_total = 0;
    u64 largest_wait_us = 0;
    u64 download_requests = 0;
    u64 download_ranges = 0;
    u64 download_bytes = 0;
    u64 promoted_pages = 0;
    u64 retained_pages = 0;
    u64 revoked_pages = 0;
    u64 merged_ranges = 0;
    u64 deduplicated_ranges = 0;
    u64 duplicate_bytes_avoided = 0;
    u64 staging_bytes = 0;
    u64 fences_detected = 0;
    u64 fences_triggering_downloads = 0;
    u64 submits = 0;
};

/// Optional, low-frequency diagnostics enabled only by SHADPS4_READBACK_PERF_TRACE=1.
class ReadbackPerformance {
public:
    ReadbackPerformance();
    ~ReadbackPerformance();

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled;
    }

    void RecordFlushRequest();
    void RecordGpuWait(u64 elapsed_us);
    void RecordDownload(u64 ranges, u64 bytes, u64 staging_bytes);
    void RecordTransitions(u64 promoted, u64 retained, u64 revoked);
    void RecordRangeOptimization(u64 merged, u64 deduplicated, u64 duplicate_bytes);
    void RecordFence(bool triggered_downloads);
    void RecordSubmit();
    void MaybeLog(u64 frame, u64 preemptive_pages);

private:
    static void Add(ReadbackPerformanceCounters& destination,
                    const ReadbackPerformanceCounters& source);
    void LogInterval(u64 frame, u64 preemptive_pages);
    void LogSummary() const;

    bool enabled = false;
    u64 last_frame = 0;
    u64 last_preemptive_pages = 0;
    ReadbackPerformanceCounters interval{};
    ReadbackPerformanceCounters total{};
    std::chrono::steady_clock::time_point last_report{};
};

} // namespace VideoCore
