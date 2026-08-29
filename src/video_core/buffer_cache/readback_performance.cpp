// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "video_core/buffer_cache/readback_performance.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#include "common/logging/log.h"
#include "core/emulator_settings.h"

namespace VideoCore {
namespace {

std::string_view ReadbackModeName() {
    switch (static_cast<GpuReadbacksMode>(EmulatorSettings.GetReadbacksMode())) {
    case GpuReadbacksMode::Disabled:
        return "Disabled";
    case GpuReadbacksMode::Relaxed:
        return "Relaxed";
    case GpuReadbacksMode::Precise:
        return "Precise";
    case GpuReadbacksMode::Optimized:
        return "Optimized-v2";
    }
    return "Unknown";
}

} // namespace

ReadbackPerformance::ReadbackPerformance() {
    const char* const trace = std::getenv("SHADPS4_READBACK_PERF_TRACE");
    enabled = trace != nullptr && std::string_view{trace} == "1";
    last_report = std::chrono::steady_clock::now();
    if (enabled) {
        LOG_INFO(Render_Vulkan, "[READBACK PERF] trace=enabled mode={}", ReadbackModeName());
    }
}

ReadbackPerformance::~ReadbackPerformance() {
    if (enabled) {
        LogSummary();
    }
}

void ReadbackPerformance::RecordFlushRequest() {
    if (enabled) {
        ++interval.flush_requests;
    }
}

void ReadbackPerformance::RecordGpuWait(u64 elapsed_us) {
    if (!enabled) {
        return;
    }
    ++interval.gpu_waits;
    interval.gpu_wait_us_total += elapsed_us;
    interval.largest_wait_us = std::max(interval.largest_wait_us, elapsed_us);
}

void ReadbackPerformance::RecordDownload(u64 ranges, u64 bytes, u64 staging_bytes) {
    if (!enabled) {
        return;
    }
    ++interval.download_requests;
    interval.download_ranges += ranges;
    interval.download_bytes += bytes;
    interval.staging_bytes += staging_bytes;
}

void ReadbackPerformance::RecordTransitions(u64 promoted, u64 retained, u64 revoked) {
    if (!enabled) {
        return;
    }
    interval.promoted_pages += promoted;
    interval.retained_pages += retained;
    interval.revoked_pages += revoked;
}

void ReadbackPerformance::RecordRangeOptimization(u64 merged, u64 deduplicated,
                                                  u64 duplicate_bytes) {
    if (!enabled) {
        return;
    }
    interval.merged_ranges += merged;
    interval.deduplicated_ranges += deduplicated;
    interval.duplicate_bytes_avoided += duplicate_bytes;
}

void ReadbackPerformance::RecordFence(bool triggered_downloads) {
    if (!enabled) {
        return;
    }
    ++interval.fences_detected;
    interval.fences_triggering_downloads += triggered_downloads ? 1 : 0;
}

void ReadbackPerformance::RecordSubmit() {
    if (enabled) {
        ++interval.submits;
    }
}

void ReadbackPerformance::MaybeLog(u64 frame, u64 preemptive_pages) {
    if (!enabled) {
        return;
    }
    last_frame = frame;
    last_preemptive_pages = preemptive_pages;
    const auto now = std::chrono::steady_clock::now();
    if (now - last_report < std::chrono::seconds{1}) {
        return;
    }
    LogInterval(frame, preemptive_pages);
    last_report = now;
}

void ReadbackPerformance::Add(ReadbackPerformanceCounters& destination,
                              const ReadbackPerformanceCounters& source) {
    destination.flush_requests += source.flush_requests;
    destination.gpu_waits += source.gpu_waits;
    destination.gpu_wait_us_total += source.gpu_wait_us_total;
    destination.largest_wait_us = std::max(destination.largest_wait_us, source.largest_wait_us);
    destination.download_requests += source.download_requests;
    destination.download_ranges += source.download_ranges;
    destination.download_bytes += source.download_bytes;
    destination.promoted_pages += source.promoted_pages;
    destination.retained_pages += source.retained_pages;
    destination.revoked_pages += source.revoked_pages;
    destination.merged_ranges += source.merged_ranges;
    destination.deduplicated_ranges += source.deduplicated_ranges;
    destination.duplicate_bytes_avoided += source.duplicate_bytes_avoided;
    destination.staging_bytes += source.staging_bytes;
    destination.fences_detected += source.fences_detected;
    destination.fences_triggering_downloads += source.fences_triggering_downloads;
    destination.submits += source.submits;
}

void ReadbackPerformance::LogInterval(u64 frame, u64 preemptive_pages) {
    LOG_INFO(Render_Vulkan,
             "[READBACK PERF] mode={} frame={} flush_requests={} gpu_waits={} "
             "gpu_wait_us_total={} largest_wait_us={} download_requests={} download_ranges={} "
             "download_bytes={} preemptive_pages={} new_preemptive_pages={} "
             "retained_preemptive_pages={} revoked_preemptive_pages={} merged_ranges={} "
             "deduplicated_ranges={} duplicate_bytes_avoided={} staging_bytes={} "
             "fences_detected={} fences_triggering_downloads={} submits={}",
             ReadbackModeName(), frame, interval.flush_requests, interval.gpu_waits,
             interval.gpu_wait_us_total, interval.largest_wait_us, interval.download_requests,
             interval.download_ranges, interval.download_bytes, preemptive_pages,
             interval.promoted_pages, interval.retained_pages, interval.revoked_pages,
             interval.merged_ranges, interval.deduplicated_ranges,
             interval.duplicate_bytes_avoided, interval.staging_bytes, interval.fences_detected,
             interval.fences_triggering_downloads, interval.submits);
    Add(total, interval);
    interval = {};
}

void ReadbackPerformance::LogSummary() const {
    ReadbackPerformanceCounters summary = total;
    Add(summary, interval);
    LOG_INFO(Render_Vulkan,
             "[READBACK PERF SUMMARY] mode={} frames={} flushes={} waits={} "
             "gpu_wait_ms_total={:.3f} largest_wait_ms={:.3f} download_gb={:.3f} "
             "download_requests={} download_ranges={} preemptive_pages={} "
             "preempt_promoted={} preempt_retained={} preempt_revoked={} merged_ranges={} "
             "deduplicated_ranges={} duplicate_mb_avoided={:.3f} staging_mb={:.3f} "
             "fences={} fences_with_downloads={} submits={}",
             ReadbackModeName(), last_frame, summary.flush_requests, summary.gpu_waits,
             static_cast<double>(summary.gpu_wait_us_total) / 1000.0,
             static_cast<double>(summary.largest_wait_us) / 1000.0,
             static_cast<double>(summary.download_bytes) / static_cast<double>(1_GB),
             summary.download_requests, summary.download_ranges, last_preemptive_pages,
             summary.promoted_pages, summary.retained_pages, summary.revoked_pages,
             summary.merged_ranges, summary.deduplicated_ranges,
             static_cast<double>(summary.duplicate_bytes_avoided) / static_cast<double>(1_MB),
             static_cast<double>(summary.staging_bytes) / static_cast<double>(1_MB),
             summary.fences_detected, summary.fences_triggering_downloads, summary.submits);
}

} // namespace VideoCore
