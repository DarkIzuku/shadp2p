// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <compare>
#include <span>
#include <vector>

#include "common/types.h"

namespace VideoCore {

enum class PreemptiveTransition {
    None,
    Promoted,
    Retained,
    Revoked,
};

struct PreemptivePolicyConfig {
    u16 promotion_score = 16;
    u16 demotion_score = 8;
    u16 maximum_score = 24;
    u64 grace_epochs = 120;
    u64 decay_interval = 30;
};

class PreemptivePagePolicy {
public:
    PreemptivePagePolicy() = default;

    explicit PreemptivePagePolicy(PreemptivePolicyConfig config_) : config{config_} {}

    [[nodiscard]] PreemptiveTransition RecordFlush(u64 epoch) {
        const bool was_preemptive = preemptive;
        const auto decay_transition = Advance(epoch);
        score = std::min<u16>(config.maximum_score, score + 1);
        last_activity_epoch = epoch;
        last_decay_step = 0;
        if (!preemptive && score >= config.promotion_score) {
            preemptive = true;
            return PreemptiveTransition::Promoted;
        }
        if (preemptive && was_preemptive) {
            return PreemptiveTransition::Retained;
        }
        return decay_transition;
    }

    [[nodiscard]] PreemptiveTransition Advance(u64 epoch) {
        if (score == 0 || epoch < last_activity_epoch + config.grace_epochs) {
            return PreemptiveTransition::None;
        }
        const u64 elapsed = epoch - (last_activity_epoch + config.grace_epochs);
        const u64 decay_step = 1 + elapsed / std::max<u64>(1, config.decay_interval);
        if (decay_step <= last_decay_step) {
            return PreemptiveTransition::None;
        }
        const u64 amount = decay_step - last_decay_step;
        score = amount >= score ? 0 : static_cast<u16>(score - amount);
        last_decay_step = decay_step;
        if (preemptive && score <= config.demotion_score) {
            preemptive = false;
            return PreemptiveTransition::Revoked;
        }
        return PreemptiveTransition::None;
    }

    [[nodiscard]] bool IsPreemptive(u64 epoch) const {
        (void)epoch;
        return preemptive;
    }

    [[nodiscard]] bool IsPreemptive() const {
        return preemptive;
    }

    [[nodiscard]] u16 Score() const {
        return score;
    }

private:
    PreemptivePolicyConfig config{};
    u64 last_activity_epoch = 0;
    u64 last_decay_step = 0;
    u16 score = 0;
    bool preemptive = false;
};

struct ReadbackRange {
    u32 resource = 0;
    u64 begin = 0;
    u64 end = 0;

    auto operator<=>(const ReadbackRange&) const = default;
};

struct ReadbackRangeStats {
    u64 input_ranges = 0;
    u64 output_ranges = 0;
    u64 merged_ranges = 0;
    u64 deduplicated_ranges = 0;
    u64 duplicate_bytes_avoided = 0;
};

struct NormalizedReadbackRanges {
    std::vector<ReadbackRange> ranges;
    ReadbackRangeStats stats;

    [[nodiscard]] u64 SubmitCount() const {
        return ranges.empty() ? 0 : 1;
    }
};

inline NormalizedReadbackRanges NormalizeReadbackRanges(std::span<const ReadbackRange> input,
                                                         u64 maximum_adjacent_span = 64_KB) {
    NormalizedReadbackRanges result;
    result.ranges.reserve(input.size());
    for (const auto& range : input) {
        if (range.end > range.begin) {
            result.ranges.push_back(range);
        }
    }
    result.stats.input_ranges = result.ranges.size();
    std::ranges::sort(result.ranges, [](const ReadbackRange& left, const ReadbackRange& right) {
        if (left.resource != right.resource) {
            return left.resource < right.resource;
        }
        if (left.begin != right.begin) {
            return left.begin < right.begin;
        }
        return left.end < right.end;
    });

    std::vector<ReadbackRange> merged;
    merged.reserve(result.ranges.size());
    for (const auto& range : result.ranges) {
        if (merged.empty() || merged.back().resource != range.resource) {
            merged.push_back(range);
            continue;
        }
        auto& previous = merged.back();
        const bool overlaps = range.begin < previous.end;
        const bool adjacent = range.begin == previous.end;
        const u64 merged_end = std::max(previous.end, range.end);
        const bool bounded_adjacent =
            adjacent && merged_end - previous.begin <= maximum_adjacent_span;
        if (!overlaps && !bounded_adjacent) {
            merged.push_back(range);
            continue;
        }
        if (overlaps) {
            const u64 duplicate_end = std::min(previous.end, range.end);
            result.stats.duplicate_bytes_avoided += duplicate_end - range.begin;
            if (range.end <= previous.end) {
                ++result.stats.deduplicated_ranges;
            }
        }
        previous.end = merged_end;
        ++result.stats.merged_ranges;
    }
    result.ranges = std::move(merged);
    result.stats.output_ranges = result.ranges.size();
    return result;
}

/// Returns true once a preemptive download can be copied out of staging without another wait.
/// `completed_through_tick` is supplied by the staging ring immediately before it reuses memory.
inline bool IsPreemptiveDownloadReady(u64 done_tick, u64 completed_through_tick,
                                      bool scheduler_reports_free) {
    return done_tick == 0 || scheduler_reports_free ||
           (completed_through_tick != 0 && done_tick <= completed_through_tick);
}

} // namespace VideoCore
