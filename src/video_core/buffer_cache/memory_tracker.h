// SPDX-FileCopyrightText: Copyright 2024-2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <atomic>
#include <deque>
#include <mutex>
#include <type_traits>
#include <vector>

#include "common/debug.h"
#include "common/types.h"
#include "core/emulator_settings.h"
#include "video_core/buffer_cache/region_manager.h"

namespace VideoCore {

class MemoryTracker {
public:
    static constexpr size_t MAX_CPU_PAGE_BITS = 40;
    static constexpr size_t NUM_HIGH_PAGES = 1ULL << (MAX_CPU_PAGE_BITS - TRACKER_HIGHER_PAGE_BITS);
    static constexpr size_t MANAGER_POOL_SIZE = 32;
    static constexpr size_t PREEMPTIVE_FLUSH_THRESHOLD = 16;
    static constexpr u64 PREEMPTIVE_SWEEP_INTERVAL = 60;

    struct PreemptiveTransitionStats {
        u64 promoted = 0;
        u64 retained = 0;
        u64 revoked = 0;
    };

public:
    explicit MemoryTracker(PageManager& tracker_) : tracker{&tracker_} {}
    ~MemoryTracker() = default;

    /// Returns true if a region has been modified from the CPU
    bool IsRegionCpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<true>(
            query_cpu_addr, query_size, [](RegionManager* manager, u64 offset, size_t size) {
                std::scoped_lock lk{manager->lock};
                return manager->template IsRegionModified<Type::CPU>(offset, size);
            });
    }

    /// Returns true if a region has been modified from the GPU
    bool IsRegionGpuModified(VAddr query_cpu_addr, u64 query_size) noexcept {
        return IteratePages<false>(
            query_cpu_addr, query_size, [](RegionManager* manager, u64 offset, size_t size) {
                std::scoped_lock lk{manager->lock};
                return manager->template IsRegionModified<Type::GPU>(offset, size);
            });
    }

    /// Mark region as CPU modified, notifying the device_tracker about this change
    void MarkRegionAsCpuModified(VAddr dirty_cpu_addr, u64 query_size) {
        IteratePages<false>(dirty_cpu_addr, query_size,
                            [](RegionManager* manager, u64 offset, size_t size) {
                                std::scoped_lock lk{manager->lock};
                                manager->template ChangeRegionState<Type::CPU, true>(
                                    manager->GetCpuAddr() + offset, size);
                            });
    }

    /// Unmark region as modified from the host GPU
    void UnmarkRegionAsGpuModified(VAddr dirty_cpu_addr, u64 query_size, bool is_write,
                                   bool record_flush = true) noexcept {
        const u64 epoch = preemptive_epoch.load(std::memory_order_relaxed);
        IteratePages<false>(
            dirty_cpu_addr, query_size,
            [this, is_write, record_flush, epoch](RegionManager* manager, u64 offset, size_t size) {
                std::scoped_lock lk{manager->lock};
                manager->template ChangeRegionState<Type::GPU, false>(
                    manager->GetCpuAddr() + offset, size);
                if (is_write) {
                    manager->template ChangeRegionState<Type::CPU, true>(
                        manager->GetCpuAddr() + offset, size);
                }
                const auto mode =
                    static_cast<GpuReadbacksMode>(EmulatorSettings.GetReadbacksMode());
                const size_t start_page = offset / TRACKER_BYTES_PER_PAGE;
                const size_t end_page = Common::DivCeil(offset + size, TRACKER_BYTES_PER_PAGE);
                if (mode == GpuReadbacksMode::Relaxed) {
                    for (size_t page = start_page; page != end_page; ++page) {
                        if (manager->NumFlushes(page) == PREEMPTIVE_FLUSH_THRESHOLD) {
                            legacy_preemptive_page_count.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                    return;
                }
                if (!record_flush || mode != GpuReadbacksMode::Optimized) {
                    return;
                }
                for (size_t page = start_page; page != end_page; ++page) {
                    RecordTransition(manager->RecordFlush(page, epoch));
                }
            });
    }

    /// Call 'func' for each page that has repeatedly required a GPU-to-CPU flush.
    void ForEachPreemptiveFlushPage(VAddr cpu_addr, u64 size, auto&& func) {
        const u64 epoch = preemptive_epoch.load(std::memory_order_relaxed);
        const auto mode = static_cast<GpuReadbacksMode>(EmulatorSettings.GetReadbacksMode());
        IteratePages<false>(
            cpu_addr, size, [&func, epoch, mode](RegionManager* manager, u64 offset, size_t size) {
                std::scoped_lock lk{manager->lock};
                const size_t start_page = offset / TRACKER_BYTES_PER_PAGE;
                const size_t end_page = Common::DivCeil(offset + size, TRACKER_BYTES_PER_PAGE);
                for (u64 page = start_page; page != end_page; ++page) {
                    const bool is_preemptive =
                        mode == GpuReadbacksMode::Relaxed
                            ? manager->NumFlushes(page) >= PREEMPTIVE_FLUSH_THRESHOLD
                            : manager->IsPagePreemptive(page, epoch);
                    if (is_preemptive) {
                        func(manager->GetCpuAddr() + page * TRACKER_BYTES_PER_PAGE);
                    }
                }
            });
    }

    void AdvancePreemptiveEpoch() {
        if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Optimized) {
            return;
        }
        const u64 epoch = preemptive_epoch.fetch_add(1, std::memory_order_relaxed) + 1;
        if (epoch % PREEMPTIVE_SWEEP_INTERVAL != 0) {
            return;
        }
        for (auto& pool : manager_pool) {
            for (RegionManager& manager : pool) {
                std::scoped_lock lk{manager.lock};
                for (u32 page = 0; page < NUM_PAGES_PER_REGION; ++page) {
                    RecordTransition(manager.AdvancePreemptivePage(page, epoch));
                }
            }
        }
    }

    [[nodiscard]] bool IsPagePreemptive(VAddr cpu_addr) {
        bool preemptive = false;
        const u64 epoch = preemptive_epoch.load(std::memory_order_relaxed);
        IteratePages<false>(cpu_addr, 1, [&](RegionManager* manager, u64 offset, size_t) {
            std::scoped_lock lk{manager->lock};
            preemptive = manager->IsPagePreemptive(offset / TRACKER_BYTES_PER_PAGE, epoch);
        });
        return preemptive;
    }

    [[nodiscard]] u64 NumPreemptivePages() const {
        if (EmulatorSettings.GetReadbacksMode() == GpuReadbacksMode::Relaxed) {
            return legacy_preemptive_page_count.load(std::memory_order_relaxed);
        }
        return preemptive_page_count.load(std::memory_order_relaxed);
    }

    PreemptiveTransitionStats TakePreemptiveTransitionStats() {
        return {
            .promoted = promoted_pages.exchange(0, std::memory_order_relaxed),
            .retained = retained_pages.exchange(0, std::memory_order_relaxed),
            .revoked = revoked_pages.exchange(0, std::memory_order_relaxed),
        };
    }

    /// Removes all protection from a page and ensures GPU data has been flushed if requested
    void InvalidateRegion(VAddr cpu_addr, u64 size, auto&& on_flush) noexcept {
        IteratePages<false>(
            cpu_addr, size, [&on_flush](RegionManager* manager, u64 offset, size_t size) {
                const bool should_flush = [&] {
                    // Perform both the GPU modification check and CPU state change with the lock
                    // in case we are racing with GPU thread trying to mark the page as GPU
                    // modified. If we need to flush the flush function is going to perform CPU
                    // state change.
                    std::scoped_lock lk{manager->lock};
                    if (EmulatorSettings.GetReadbacksMode() != GpuReadbacksMode::Disabled &&
                        manager->template IsRegionModified<Type::GPU>(offset, size)) {
                        return true;
                    }
                    manager->template ChangeRegionState<Type::CPU, true>(
                        manager->GetCpuAddr() + offset, size);
                    return false;
                }();
                if (should_flush) {
                    on_flush();
                }
            });
    }

    /// Call 'func' for each CPU modified range and unmark those pages as CPU modified
    void ForEachUploadRange(VAddr query_cpu_range, u64 query_size, bool is_written, auto&& func,
                            auto&& on_upload) {
        IteratePages<true>(query_cpu_range, query_size,
                           [&func, is_written](RegionManager* manager, u64 offset, size_t size) {
                               manager->lock.lock();
                               manager->template ForEachModifiedRange<Type::CPU, true>(
                                   manager->GetCpuAddr() + offset, size, func);
                               if (!is_written) {
                                   manager->lock.unlock();
                               }
                           });
        on_upload();
        if (!is_written) {
            return;
        }
        IteratePages<false>(query_cpu_range, query_size,
                            [&func, is_written](RegionManager* manager, u64 offset, size_t size) {
                                manager->template ChangeRegionState<Type::GPU, true>(
                                    manager->GetCpuAddr() + offset, size);
                                manager->lock.unlock();
                            });
    }

    /// Call 'func' for each GPU modified range and unmark those pages as GPU modified
    template <bool clear>
    void ForEachDownloadRange(VAddr query_cpu_range, u64 query_size, auto&& func) {
        IteratePages<false>(query_cpu_range, query_size,
                            [&func](RegionManager* manager, u64 offset, size_t size) {
                                std::scoped_lock lk{manager->lock};
                                manager->template ForEachModifiedRange<Type::GPU, clear>(
                                    manager->GetCpuAddr() + offset, size, func);
                            });
    }

private:
    void RecordTransition(PreemptiveTransition transition) {
        switch (transition) {
        case PreemptiveTransition::Promoted:
            promoted_pages.fetch_add(1, std::memory_order_relaxed);
            preemptive_page_count.fetch_add(1, std::memory_order_relaxed);
            break;
        case PreemptiveTransition::Retained:
            retained_pages.fetch_add(1, std::memory_order_relaxed);
            break;
        case PreemptiveTransition::Revoked:
            revoked_pages.fetch_add(1, std::memory_order_relaxed);
            preemptive_page_count.fetch_sub(1, std::memory_order_relaxed);
            break;
        case PreemptiveTransition::None:
            break;
        }
    }

    /**
     * @brief IteratePages Iterates L2 word manager page table.
     * @param cpu_address Start byte cpu address
     * @param size Size in bytes of the region of iterate.
     * @param func Callback for each word manager.
     * @return
     */
    template <bool create_region_on_fail, typename Func>
    bool IteratePages(VAddr cpu_address, size_t size, Func&& func) {
        RENDERER_TRACE;
        using FuncReturn = typename std::invoke_result<Func, RegionManager*, u64, size_t>::type;
        static constexpr bool BOOL_BREAK = std::is_same_v<FuncReturn, bool>;
        std::size_t remaining_size{size};
        std::size_t page_index{cpu_address >> TRACKER_HIGHER_PAGE_BITS};
        u64 page_offset{cpu_address & TRACKER_HIGHER_PAGE_MASK};
        while (remaining_size > 0) {
            const std::size_t copy_amount{
                std::min<std::size_t>(TRACKER_HIGHER_PAGE_SIZE - page_offset, remaining_size)};
            auto* manager{top_tier[page_index]};
            if (manager) {
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            } else if constexpr (create_region_on_fail) {
                CreateRegion(page_index);
                manager = top_tier[page_index];
                if constexpr (BOOL_BREAK) {
                    if (func(manager, page_offset, copy_amount)) {
                        return true;
                    }
                } else {
                    func(manager, page_offset, copy_amount);
                }
            }
            page_index++;
            page_offset = 0;
            remaining_size -= copy_amount;
        }
        return false;
    }

    void CreateRegion(std::size_t page_index) {
        const VAddr base_cpu_addr = page_index << TRACKER_HIGHER_PAGE_BITS;
        if (free_managers.empty()) {
            manager_pool.emplace_back();
            auto& last_pool = manager_pool.back();
            for (size_t i = 0; i < MANAGER_POOL_SIZE; i++) {
                std::construct_at(&last_pool[i], tracker, 0);
                free_managers.push_back(&last_pool[i]);
            }
        }
        // Each manager tracks a 4_MB virtual address space.
        auto* new_manager = free_managers.back();
        new_manager->SetCpuAddress(base_cpu_addr);
        free_managers.pop_back();
        top_tier[page_index] = new_manager;
    }

    PageManager* tracker;
    std::deque<std::array<RegionManager, MANAGER_POOL_SIZE>> manager_pool;
    std::vector<RegionManager*> free_managers;
    std::array<RegionManager*, NUM_HIGH_PAGES> top_tier{};
    std::atomic<u64> preemptive_epoch = 0;
    std::atomic<u64> legacy_preemptive_page_count = 0;
    std::atomic<u64> preemptive_page_count = 0;
    std::atomic<u64> promoted_pages = 0;
    std::atomic<u64> retained_pages = 0;
    std::atomic<u64> revoked_pages = 0;
};

} // namespace VideoCore
