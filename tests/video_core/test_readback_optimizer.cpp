// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>

#include "video_core/buffer_cache/readback_optimizer.h"

namespace VideoCore {

TEST(PreemptivePagePolicy, PromotesAfterRepeatedFlushes) {
    PreemptivePagePolicy page;
    for (u64 epoch = 1; epoch < 16; ++epoch) {
        EXPECT_NE(page.RecordFlush(epoch), PreemptiveTransition::Promoted);
    }
    EXPECT_EQ(page.RecordFlush(16), PreemptiveTransition::Promoted);
    EXPECT_TRUE(page.IsPreemptive());
}

TEST(PreemptivePagePolicy, DecaysAfterGracePeriod) {
    PreemptivePagePolicy page;
    for (u64 epoch = 1; epoch <= 16; ++epoch) {
        (void)page.RecordFlush(epoch);
    }
    EXPECT_EQ(page.Score(), 16);
    EXPECT_EQ(page.Advance(135), PreemptiveTransition::None);
    EXPECT_EQ(page.Score(), 16);
    EXPECT_EQ(page.Advance(136), PreemptiveTransition::None);
    EXPECT_EQ(page.Score(), 15);
}

TEST(PreemptivePagePolicy, RevokesAnOldPage) {
    PreemptivePagePolicy page;
    for (u64 epoch = 1; epoch <= 16; ++epoch) {
        (void)page.RecordFlush(epoch);
    }
    EXPECT_TRUE(page.IsPreemptive());
    EXPECT_EQ(page.Advance(346), PreemptiveTransition::Revoked);
    EXPECT_FALSE(page.IsPreemptive());
    EXPECT_LE(page.Score(), 8);
}

TEST(PreemptivePagePolicy, RepeatedUseRemainsPreemptive) {
    PreemptivePagePolicy page;
    for (u64 epoch = 1; epoch <= 16; ++epoch) {
        (void)page.RecordFlush(epoch);
    }
    for (u64 epoch = 60; epoch <= 600; epoch += 60) {
        EXPECT_EQ(page.RecordFlush(epoch), PreemptiveTransition::Retained);
        EXPECT_TRUE(page.IsPreemptive());
    }
}

TEST(PreemptivePagePolicy, OldPageLeavesPreemptiveSet) {
    PreemptivePagePolicy old_page;
    PreemptivePagePolicy active_page;
    for (u64 epoch = 1; epoch <= 16; ++epoch) {
        (void)old_page.RecordFlush(epoch);
        (void)active_page.RecordFlush(epoch);
    }
    for (u64 epoch = 60; epoch <= 360; epoch += 60) {
        (void)active_page.RecordFlush(epoch);
        (void)old_page.Advance(epoch);
    }
    EXPECT_FALSE(old_page.IsPreemptive(360));
    EXPECT_TRUE(active_page.IsPreemptive(360));
}

TEST(ReadbackRanges, MergesOverlappingRanges) {
    const std::vector input = {
        ReadbackRange{1, 100, 110},
        ReadbackRange{1, 106, 115},
        ReadbackRange{1, 112, 120},
    };
    const auto result = NormalizeReadbackRanges(input);
    ASSERT_EQ(result.ranges.size(), 1);
    EXPECT_EQ(result.ranges.front(), (ReadbackRange{1, 100, 120}));
    EXPECT_EQ(result.stats.merged_ranges, 2);
    EXPECT_EQ(result.stats.duplicate_bytes_avoided, 7);
}

TEST(ReadbackRanges, RemovesDuplicateRanges) {
    const std::vector input = {
        ReadbackRange{7, 0x1000, 0x2000},
        ReadbackRange{7, 0x1000, 0x2000},
        ReadbackRange{7, 0x1400, 0x1800},
    };
    const auto result = NormalizeReadbackRanges(input);
    ASSERT_EQ(result.ranges.size(), 1);
    EXPECT_EQ(result.stats.deduplicated_ranges, 2);
    EXPECT_EQ(result.stats.duplicate_bytes_avoided, 0x1400);
}

TEST(ReadbackRanges, AdjacentMergeRemainsBounded) {
    const std::vector input = {
        ReadbackRange{1, 0, 32_KB},
        ReadbackRange{1, 32_KB, 64_KB},
        ReadbackRange{1, 64_KB, 68_KB},
    };
    const auto result = NormalizeReadbackRanges(input, 64_KB);
    ASSERT_EQ(result.ranges.size(), 2);
    EXPECT_EQ(result.ranges[0], (ReadbackRange{1, 0, 64_KB}));
    EXPECT_EQ(result.ranges[1], (ReadbackRange{1, 64_KB, 68_KB}));
}

TEST(ReadbackRanges, DoesNotMergeDifferentResources) {
    const std::vector input = {
        ReadbackRange{1, 0, 4096},
        ReadbackRange{2, 0, 4096},
    };
    const auto result = NormalizeReadbackRanges(input);
    EXPECT_EQ(result.ranges.size(), 2);
}

TEST(ReadbackBatch, PreservesCoveredBytesAndUsesOneSubmit) {
    const std::vector input = {
        ReadbackRange{1, 0, 4096},
        ReadbackRange{1, 4096, 8192},
        ReadbackRange{2, 0, 2048},
    };
    const auto result = NormalizeReadbackRanges(input);
    ASSERT_EQ(result.ranges.size(), 2);
    EXPECT_EQ(result.ranges[0].end - result.ranges[0].begin, 8192);
    EXPECT_EQ(result.ranges[1].end - result.ranges[1].begin, 2048);
    EXPECT_EQ(result.SubmitCount(), 1);
}

TEST(ReadbackBatch, EmptyBatchDoesNotSubmit) {
    EXPECT_EQ(NormalizeReadbackRanges(std::span<const ReadbackRange>{}).SubmitCount(), 0);
}

TEST(ReadbackStaging, DoesNotReuseAnInFlightDownload) {
    EXPECT_FALSE(IsPreemptiveDownloadReady(12, 0, false));
    EXPECT_FALSE(IsPreemptiveDownloadReady(12, 11, false));
}

TEST(ReadbackStaging, ReusesOnlyAfterTheDownloadCompleted) {
    EXPECT_TRUE(IsPreemptiveDownloadReady(12, 12, false));
    EXPECT_TRUE(IsPreemptiveDownloadReady(12, 0, true));
    EXPECT_TRUE(IsPreemptiveDownloadReady(0, 0, false));
}

} // namespace VideoCore
