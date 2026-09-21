// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_seamless_state.h"

#include <algorithm>
#include <cmath>

namespace Core::Bloodborne {
namespace {

bool IsFinitePlacement(const SeamlessTravelEvent& event) {
    return std::isfinite(event.positionX) && std::isfinite(event.positionY) &&
           std::isfinite(event.positionZ) && std::isfinite(event.orientation);
}

bool IsTransitionState(SeamlessTravelState state) {
    return state == SeamlessTravelState::TravelPreparing ||
           state == SeamlessTravelState::Traveling || state == SeamlessTravelState::WorldLoading ||
           state == SeamlessTravelState::Rebinding;
}

} // namespace

HunterDreamInteractionTraceState::HunterDreamInteractionTraceState()
    : HunterDreamInteractionTraceState(Options{}) {}

HunterDreamInteractionTraceState::HunterDreamInteractionTraceState(Options options)
    : m_options(options) {
    m_options.repeatAfterMs = std::max<s64>(100, m_options.repeatAfterMs);
    m_options.staleAfterMs = std::max(m_options.repeatAfterMs, m_options.staleAfterMs);
    m_options.maxEntries = std::max<size_t>(1, m_options.maxEntries);
    m_entries.reserve(m_options.maxEntries);
}

void HunterDreamInteractionTraceState::SetEnabled(bool enabled) {
    m_enabled = enabled;
    m_world = 0;
    m_entries.clear();
}

bool HunterDreamInteractionTraceState::IsEnabled() const {
    return m_enabled;
}

bool HunterDreamInteractionTraceState::SameIdentity(
    const HunterDreamInteractionTraceSnapshot& left,
    const HunterDreamInteractionTraceSnapshot& right) {
    return left.phase == right.phase && left.object == right.object &&
           left.entityId == right.entityId && left.eventId == right.eventId &&
           left.actionButtonId == right.actionButtonId && left.eventBank == right.eventBank &&
           left.eventCommand == right.eventCommand;
}

bool HunterDreamInteractionTraceState::SameState(const HunterDreamInteractionTraceSnapshot& left,
                                                 const HunterDreamInteractionTraceSnapshot& right) {
    return left.kind == right.kind && left.role == right.role && left.map == right.map &&
           left.areaRegion == right.areaRegion && left.promptId == right.promptId &&
           left.gates == right.gates && left.available == right.available &&
           left.selected == right.selected;
}

void HunterDreamInteractionTraceState::Expire(s64 nowMs) {
    std::erase_if(m_entries, [&](const Entry& entry) {
        return nowMs - entry.lastSeenMs > m_options.staleAfterMs;
    });
}

HunterDreamInteractionTraceDecision HunterDreamInteractionTraceState::Observe(
    const HunterDreamInteractionTraceSnapshot& value, s64 nowMs) {
    if (!m_enabled)
        return HunterDreamInteractionTraceDecision::Suppressed;

    const bool worldChanged = m_world != 0 && value.map != 0 && value.map != m_world;
    if (value.map != 0 && value.map != m_world) {
        m_world = value.map;
        m_entries.clear();
    }
    Expire(nowMs);

    const auto existing = std::ranges::find_if(
        m_entries, [&](const Entry& entry) { return SameIdentity(entry.snapshot, value); });
    if (existing != m_entries.end()) {
        const bool unchanged = SameState(existing->snapshot, value);
        existing->lastSeenMs = nowMs;
        if (unchanged && nowMs - existing->lastEmittedMs < m_options.repeatAfterMs)
            return HunterDreamInteractionTraceDecision::Suppressed;
        existing->snapshot = value;
        existing->lastEmittedMs = nowMs;
        return worldChanged ? HunterDreamInteractionTraceDecision::WorldChanged
                            : HunterDreamInteractionTraceDecision::Emit;
    }

    if (m_entries.size() == m_options.maxEntries) {
        const auto oldest = std::ranges::min_element(m_entries, {}, &Entry::lastSeenMs);
        m_entries.erase(oldest);
    }
    m_entries.push_back({value, nowMs, nowMs});
    return worldChanged ? HunterDreamInteractionTraceDecision::WorldChanged
                        : HunterDreamInteractionTraceDecision::Emit;
}

void HunterDreamInteractionTraceState::ResetForWorld(u32 map) {
    m_world = map;
    m_entries.clear();
}

void HunterDreamInteractionTraceState::ResetForSessionEnd() {
    m_world = 0;
    m_entries.clear();
}

size_t HunterDreamInteractionTraceState::EntryCount() const {
    return m_entries.size();
}

PendingCrossMapSummonStateMachine::PendingCrossMapSummonStateMachine()
    : PendingCrossMapSummonStateMachine(Options{}) {}

PendingCrossMapSummonStateMachine::PendingCrossMapSummonStateMachine(Options options)
    : m_options(options) {
    m_options.timeoutMs = std::max<s64>(5'000, m_options.timeoutMs);
    m_options.preRoomSignalingGuardMs =
        std::clamp<s64>(m_options.preRoomSignalingGuardMs, 5'000, m_options.timeoutMs);
}

void PendingCrossMapSummonStateMachine::SetEnabled(bool enabled) {
    m_enabled = enabled;
    Reset();
}

void PendingCrossMapSummonStateMachine::Reset() {
    m_pending = {};
    m_unbound = {};
    m_deadlineMs = 0;
    m_unboundDeadlineMs = 0;
    m_signalingGuardDeadlineMs = 0;
    m_lastEventReason = "reset";
}

bool PendingCrossMapSummonStateMachine::MatchesGeneration(u64 generation) const {
    return m_pending.generation != 0 && (generation == 0 || generation == m_pending.generation);
}

bool PendingCrossMapSummonStateMachine::IsExpired(s64 nowMs) const {
    return m_deadlineMs != 0 && nowMs > m_deadlineMs;
}

void PendingCrossMapSummonStateMachine::RefreshDeadline(s64 nowMs) {
    m_deadlineMs = nowMs + m_options.timeoutMs;
}

u64 PendingCrossMapSummonStateMachine::OnPlacementDeferred(u32 targetMap, s64 nowMs) {
    if (!m_enabled || targetMap == 0)
        return 0;
    if (m_pending.targetMap == targetMap && m_pending.phase != PendingCrossMapSummonPhase::Idle &&
        m_pending.phase != PendingCrossMapSummonPhase::Complete &&
        m_pending.phase != PendingCrossMapSummonPhase::Failed && !IsExpired(nowMs)) {
        RefreshDeadline(nowMs);
        return m_pending.generation;
    }
    m_pending = {};
    m_pending.phase = PendingCrossMapSummonPhase::PlacementDeferred;
    m_pending.generation = ++m_nextGeneration;
    m_pending.targetMap = targetMap;
    m_pending.placementReady = true;
    BindUnboundEvents(nowMs);
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return m_pending.generation;
}

void PendingCrossMapSummonStateMachine::BindUnboundEvents(s64 nowMs) {
    if (m_unboundDeadlineMs == 0 || nowMs > m_unboundDeadlineMs) {
        m_unbound = {};
        m_unboundDeadlineMs = 0;
        return;
    }
    m_pending.claimAccepted = m_unbound.claimAccepted;
    m_pending.roomJoinStarted = m_unbound.roomJoinStarted;
    m_pending.roomJoined = m_unbound.roomJoined;
    m_pending.signalingEstablished = m_unbound.signalingEstablished;
    m_pending.roomId = m_unbound.roomId;
    m_pending.localMemberId = m_unbound.localMemberId;
    m_pending.expectedPeerMemberId = m_unbound.expectedPeerMemberId;
    m_pending.expectedPeerNpid = m_unbound.expectedPeerNpid;
    m_unbound = {};
    m_unboundDeadlineMs = 0;
}

bool PendingCrossMapSummonStateMachine::OnNativeHandoffObserved(u32 currentMap, u32 targetMap,
                                                                SeamlessPeerRole role, s64 nowMs,
                                                                u64 generation) {
    if (!m_enabled || currentMap == 0 || targetMap != m_pending.targetMap ||
        role == SeamlessPeerRole::Unknown || !MatchesGeneration(generation) || IsExpired(nowMs) ||
        m_pending.phase == PendingCrossMapSummonPhase::Complete ||
        m_pending.phase == PendingCrossMapSummonPhase::Failed) {
        return false;
    }
    m_pending.sourceMap = currentMap;
    m_pending.role = role;
    m_pending.nativeHandoffObserved = true;
    if (!m_pending.claimAccepted) {
        m_pending.phase = PendingCrossMapSummonPhase::NativeHandoffObserved;
    } else {
        AdvanceReadyPhase();
    }
    RefreshDeadline(nowMs);
    return true;
}

bool PendingCrossMapSummonStateMachine::BindRole(SeamlessPeerRole role, u64 generation) {
    if (!m_enabled || role == SeamlessPeerRole::Unknown || !MatchesGeneration(generation))
        return false;
    m_pending.role = role;
    return true;
}

bool PendingCrossMapSummonStateMachine::OnClaimAccepted(s64 nowMs, u64 generation) {
    if (!m_enabled)
        return false;
    if (m_pending.phase == PendingCrossMapSummonPhase::Idle) {
        if (generation != 0)
            return false;
        m_unbound.claimAccepted = true;
        m_unboundDeadlineMs = nowMs + m_options.timeoutMs;
        return true;
    }
    if (!MatchesGeneration(generation) || IsExpired(nowMs))
        return false;
    m_pending.claimAccepted = true;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return true;
}

void PendingCrossMapSummonStateMachine::AdvanceReadyPhase() {
    if (m_pending.signalingEstablished && m_pending.roomJoined)
        m_pending.phase = PendingCrossMapSummonPhase::SignalingEstablished;
    else if (m_pending.roomJoined)
        m_pending.phase = PendingCrossMapSummonPhase::RoomJoined;
    else if (m_pending.roomJoinStarted)
        m_pending.phase = PendingCrossMapSummonPhase::RoomJoinStarted;
    else if (m_pending.claimAccepted)
        m_pending.phase = PendingCrossMapSummonPhase::ClaimAccepted;
    else if (m_pending.nativeHandoffObserved)
        m_pending.phase = PendingCrossMapSummonPhase::NativeHandoffObserved;
}

bool PendingCrossMapSummonStateMachine::OnRoomJoinStarted(s64 nowMs, u64 roomId, u64 generation) {
    if (!m_enabled)
        return false;
    if (m_pending.phase == PendingCrossMapSummonPhase::Idle) {
        if (generation != 0)
            return false;
        m_unbound.roomJoinStarted = true;
        m_unbound.roomId = roomId;
        m_unboundDeadlineMs = nowMs + m_options.timeoutMs;
        return true;
    }
    if (!MatchesGeneration(generation) || IsExpired(nowMs)) {
        return false;
    }
    const bool roomChanged = roomId != 0 && m_pending.roomId != 0 && m_pending.roomId != roomId;
    m_pending.roomJoinStarted = true;
    if (roomChanged) {
        m_pending.roomJoined = false;
        m_pending.signalingEstablished = false;
    }
    if (roomId != 0)
        m_pending.roomId = roomId;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return true;
}

bool PendingCrossMapSummonStateMachine::OnRoomJoined(s64 nowMs, u64 roomId, u64 generation) {
    return OnRoomJoinedForPeer(nowMs, roomId, 0, 0, {}, generation);
}

bool PendingCrossMapSummonStateMachine::OnRoomJoinedForPeer(s64 nowMs, u64 roomId,
                                                            u16 localMemberId,
                                                            u16 expectedPeerMemberId,
                                                            std::string_view expectedPeerNpid,
                                                            u64 generation) {
    if (!m_enabled) {
        m_lastEventReason = "disabled";
        return false;
    }
    if (roomId == 0) {
        m_lastEventReason = "invalid_room";
        return false;
    }
    if (m_pending.phase == PendingCrossMapSummonPhase::Idle) {
        if (generation != 0) {
            m_lastEventReason = "stale_generation";
            return false;
        }
        if (m_unbound.roomId != 0 && m_unbound.roomId != roomId) {
            m_lastEventReason = "room_mismatch";
            return false;
        }
        m_unbound.roomJoinStarted = true;
        m_unbound.roomJoined = true;
        m_unbound.roomId = roomId;
        m_unbound.localMemberId = localMemberId;
        m_unbound.expectedPeerMemberId = expectedPeerMemberId;
        m_unbound.expectedPeerNpid = expectedPeerNpid;
        m_unboundDeadlineMs = nowMs + m_options.timeoutMs;
        m_lastEventReason = "accepted_unbound";
        return true;
    }
    if (!MatchesGeneration(generation)) {
        m_lastEventReason = "stale_generation";
        return false;
    }
    if (IsExpired(nowMs)) {
        m_lastEventReason = "expired";
        return false;
    }
    if (m_pending.roomId != 0 && m_pending.roomId != roomId) {
        m_lastEventReason = "room_mismatch";
        return false;
    }
    if (m_pending.expectedPeerMemberId != 0 && expectedPeerMemberId != 0 &&
        m_pending.expectedPeerMemberId != expectedPeerMemberId) {
        m_lastEventReason = "peer_member_mismatch";
        return false;
    }
    if (!m_pending.expectedPeerNpid.empty() && !expectedPeerNpid.empty() &&
        m_pending.expectedPeerNpid != expectedPeerNpid) {
        m_lastEventReason = "peer_npid_mismatch";
        return false;
    }
    m_pending.roomJoinStarted = true;
    m_pending.roomJoined = true;
    m_pending.roomId = roomId;
    if (localMemberId != 0)
        m_pending.localMemberId = localMemberId;
    if (expectedPeerMemberId != 0)
        m_pending.expectedPeerMemberId = expectedPeerMemberId;
    if (!expectedPeerNpid.empty())
        m_pending.expectedPeerNpid = expectedPeerNpid;
    if (m_pending.signalingEstablished) {
        m_signalingGuardDeadlineMs = nowMs + m_options.preRoomSignalingGuardMs;
    }
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    m_lastEventReason = "accepted";
    return true;
}

bool PendingCrossMapSummonStateMachine::OnSignalingEstablished(s64 nowMs, u64 roomId,
                                                               u64 generation) {
    return OnSignalingEstablishedForPeer(nowMs, roomId, 0, {}, generation);
}

bool PendingCrossMapSummonStateMachine::OnSignalingEstablishedForPeer(s64 nowMs, u64 roomId,
                                                                      u16 peerMemberId,
                                                                      std::string_view peerNpid,
                                                                      u64 generation) {
    if (!m_enabled) {
        m_lastEventReason = "disabled";
        return false;
    }
    // Generic sceNpSignaling can become established just before Matching2 publishes
    // its room completion callback. Preserve that fact by peer identity and bind the
    // room later; an anonymous event without either identity is not safe to retain.
    if (roomId == 0 && peerNpid.empty()) {
        m_lastEventReason = "invalid_room";
        return false;
    }
    if (m_pending.phase == PendingCrossMapSummonPhase::Idle) {
        if (generation != 0) {
            m_lastEventReason = "stale_generation";
            return false;
        }
        if (roomId != 0 && m_unbound.roomId != 0 && m_unbound.roomId != roomId) {
            m_lastEventReason = "room_mismatch";
            return false;
        }
        m_unbound.roomJoinStarted = true;
        m_unbound.signalingEstablished = true;
        if (roomId != 0)
            m_unbound.roomId = roomId;
        m_unbound.expectedPeerMemberId = peerMemberId;
        m_unbound.expectedPeerNpid = peerNpid;
        m_unboundDeadlineMs = nowMs + m_options.timeoutMs;
        m_signalingGuardDeadlineMs = nowMs + m_options.preRoomSignalingGuardMs;
        m_lastEventReason = roomId == 0 ? "accepted_unbound_without_room" : "accepted_unbound";
        return true;
    }
    if (!MatchesGeneration(generation)) {
        m_lastEventReason = "stale_generation";
        return false;
    }
    if (IsExpired(nowMs)) {
        m_lastEventReason = "expired";
        return false;
    }
    if (roomId != 0 && m_pending.roomId != 0 && roomId != m_pending.roomId) {
        m_lastEventReason = "room_mismatch";
        return false;
    }
    if (m_pending.expectedPeerMemberId != 0 && peerMemberId != 0 &&
        m_pending.expectedPeerMemberId != peerMemberId) {
        m_lastEventReason = "peer_member_mismatch";
        return false;
    }
    if (!m_pending.expectedPeerNpid.empty() && !peerNpid.empty() &&
        m_pending.expectedPeerNpid != peerNpid) {
        m_lastEventReason = "peer_npid_mismatch";
        return false;
    }
    m_pending.roomJoinStarted = true;
    if (roomId != 0)
        m_pending.roomId = roomId;
    m_pending.signalingEstablished = true;
    if (peerMemberId != 0)
        m_pending.expectedPeerMemberId = peerMemberId;
    if (!peerNpid.empty())
        m_pending.expectedPeerNpid = peerNpid;
    m_signalingGuardDeadlineMs = nowMs + m_options.preRoomSignalingGuardMs;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    m_lastEventReason = roomId == 0 ? "accepted_pending_without_room" : "accepted";
    return true;
}

PendingCrossMapSummonDecision PendingCrossMapSummonStateMachine::Evaluate(u32 currentMap,
                                                                          u32 targetMap,
                                                                          s64 nowMs) {
    if (!m_enabled || m_pending.phase == PendingCrossMapSummonPhase::Idle)
        return PendingCrossMapSummonDecision::None;
    if (m_pending.phase == PendingCrossMapSummonPhase::Complete)
        return PendingCrossMapSummonDecision::None;
    if (IsExpired(nowMs)) {
        m_pending.phase = PendingCrossMapSummonPhase::Failed;
        return PendingCrossMapSummonDecision::TimedOut;
    }
    if (targetMap != m_pending.targetMap)
        return PendingCrossMapSummonDecision::StaleTarget;
    m_pending.sourceMap = currentMap;
    if (!m_pending.placementReady)
        return PendingCrossMapSummonDecision::WaitForPlacement;
    if (!m_pending.claimAccepted)
        return PendingCrossMapSummonDecision::WaitForClaim;
    if (!m_pending.roomJoined)
        return PendingCrossMapSummonDecision::WaitForRoom;
    if (!m_pending.signalingEstablished)
        return PendingCrossMapSummonDecision::WaitForSignaling;
    if (currentMap != targetMap && !m_pending.nativeHandoffObserved)
        return PendingCrossMapSummonDecision::WaitForNativeHandoff;
    if (currentMap == targetMap) {
        if (!m_pending.placementApplied) {
            m_pending.phase = PendingCrossMapSummonPhase::CrossMapCommit;
            RefreshDeadline(nowMs);
            return PendingCrossMapSummonDecision::ApplyPlacement;
        }
        if (!m_pending.placementVerified)
            return PendingCrossMapSummonDecision::VerifyPlacement;
        m_pending.phase = PendingCrossMapSummonPhase::WorldReady;
        return PendingCrossMapSummonDecision::PlacementComplete;
    }
    if (m_pending.reloadCount != 0 || m_pending.commitIssued)
        return PendingCrossMapSummonDecision::DuplicateReload;
    m_pending.phase = PendingCrossMapSummonPhase::CrossMapCommit;
    RefreshDeadline(nowMs);
    return PendingCrossMapSummonDecision::Commit;
}

bool PendingCrossMapSummonStateMachine::BeginPlacementApply(u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || m_pending.placementApplied ||
        m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
        return false;
    }
    m_pending.commitIssued = true;
    m_pending.placementApplied = true;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkPlacementVerified(u32 currentMap, u64 generation) {
    if (!MatchesGeneration(generation) || !m_pending.placementApplied ||
        currentMap != m_pending.targetMap ||
        (m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit &&
         m_pending.phase != PendingCrossMapSummonPhase::ReloadStarted &&
         m_pending.phase != PendingCrossMapSummonPhase::WorldReady)) {
        return false;
    }
    m_pending.placementVerified = true;
    m_pending.phase = PendingCrossMapSummonPhase::WorldReady;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkPlacementFailed(u64 generation) {
    if (!MatchesGeneration(generation) || m_pending.placementVerified ||
        m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
        return false;
    }
    m_pending.phase = PendingCrossMapSummonPhase::Failed;
    return true;
}

bool PendingCrossMapSummonStateMachine::BeginCommit(u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || m_pending.commitIssued ||
        m_pending.reloadCount != 0 ||
        m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
        return false;
    }
    m_pending.commitIssued = true;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkCommitFailed(u64 generation) {
    if (!MatchesGeneration(generation) || !m_pending.commitIssued ||
        m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
        return false;
    }
    m_pending.phase = PendingCrossMapSummonPhase::Failed;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkReloadStarted(u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || m_pending.reloadCount != 0 ||
        !m_pending.commitIssued || m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
        return false;
    }
    m_pending.reloadCount = 1;
    m_pending.phase = PendingCrossMapSummonPhase::ReloadStarted;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkReloadFailed(u64 generation) {
    if (!MatchesGeneration(generation) ||
        m_pending.phase != PendingCrossMapSummonPhase::ReloadStarted)
        return false;
    m_pending.phase = PendingCrossMapSummonPhase::Failed;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkWorldReady(u32 currentMap, u64 generation) {
    if (!MatchesGeneration(generation) || currentMap != m_pending.targetMap ||
        m_pending.phase != PendingCrossMapSummonPhase::ReloadStarted) {
        return false;
    }
    m_pending.phase = PendingCrossMapSummonPhase::WorldReady;
    return true;
}

bool PendingCrossMapSummonStateMachine::MarkRemoteInserted(u64 generation) {
    if (!MatchesGeneration(generation) || !m_pending.placementVerified ||
        (m_pending.phase != PendingCrossMapSummonPhase::WorldReady &&
         m_pending.phase != PendingCrossMapSummonPhase::ReloadStarted)) {
        return false;
    }
    m_pending.phase = PendingCrossMapSummonPhase::RemoteInserted;
    m_pending.phase = PendingCrossMapSummonPhase::Complete;
    return true;
}

bool PendingCrossMapSummonStateMachine::ShouldRetainPlacementOnMissingCreate(s64 nowMs) const {
    return m_enabled && !IsExpired(nowMs) && m_pending.claimAccepted &&
           m_pending.phase != PendingCrossMapSummonPhase::Complete &&
           m_pending.phase != PendingCrossMapSummonPhase::Failed;
}

bool PendingCrossMapSummonStateMachine::ShouldGuardSignalingDeactivate(std::string_view peerNpid,
                                                                       s64 nowMs) const {
    if (!m_enabled || peerNpid.empty() || m_signalingGuardDeadlineMs == 0 ||
        nowMs > m_signalingGuardDeadlineMs) {
        return false;
    }

    // The native game may request cleanup after generic sceNpSignaling reaches
    // MUTUAL_ACTIVATED but before the HTTP claim and Matching2 room callbacks
    // converge. Guard only that exact peer and only while the retained signaling
    // fact belongs to an unfinished summon.
    if (m_pending.phase != PendingCrossMapSummonPhase::Idle) {
        return m_pending.phase != PendingCrossMapSummonPhase::Complete &&
               m_pending.phase != PendingCrossMapSummonPhase::Failed &&
               m_pending.signalingEstablished && !m_pending.expectedPeerNpid.empty() &&
               peerNpid == m_pending.expectedPeerNpid;
    }

    return m_unbound.signalingEstablished && !m_unbound.expectedPeerNpid.empty() &&
           peerNpid == m_unbound.expectedPeerNpid;
}

bool PendingCrossMapSummonStateMachine::OnSignalingDeactivated(std::string_view peerNpid,
                                                               s64 nowMs) {
    if (!m_enabled || peerNpid.empty())
        return false;

    bool changed = false;
    if (m_pending.phase != PendingCrossMapSummonPhase::Idle &&
        !m_pending.expectedPeerNpid.empty() && peerNpid == m_pending.expectedPeerNpid &&
        m_pending.signalingEstablished) {
        m_pending.signalingEstablished = false;
        AdvanceReadyPhase();
        RefreshDeadline(nowMs);
        changed = true;
    }

    if (!m_unbound.expectedPeerNpid.empty() && peerNpid == m_unbound.expectedPeerNpid &&
        m_unbound.signalingEstablished) {
        m_unbound.signalingEstablished = false;
        changed = true;
    }

    if (changed) {
        m_signalingGuardDeadlineMs = 0;
        m_lastEventReason = "signaling_deactivated";
    }
    return changed;
}

std::string_view PendingCrossMapSummonStateMachine::LastEventReason() const {
    return m_lastEventReason;
}

PendingCrossMapSummonSnapshot PendingCrossMapSummonStateMachine::Snapshot() const {
    return m_pending;
}

bool IsSeamlessGuestParamProfileSupported(std::string_view profileName) {
    return profileName == "cusa03173-109-d65f0b4f" ||
           profileName == "cusa03173-109-user-eboot-6764938b";
}

SeamlessTravelStateMachine::SeamlessTravelStateMachine() : SeamlessTravelStateMachine(Options{}) {}

SeamlessTravelStateMachine::SeamlessTravelStateMachine(Options options) : m_options(options) {
    m_options.transitionTimeoutMs = std::max<s64>(5'000, m_options.transitionTimeoutMs);
}

void SeamlessTravelStateMachine::SetEnabled(bool enabled) {
    m_enabled = enabled;
    Reset();
}

bool SeamlessTravelStateMachine::IsEnabled() const {
    return m_enabled;
}

SeamlessTravelState SeamlessTravelStateMachine::State() const {
    return m_state;
}

const std::optional<SeamlessTravelEvent>& SeamlessTravelStateMachine::ActiveTravel() const {
    return m_activeTravel;
}

void SeamlessTravelStateMachine::Reset() {
    m_activeTravel.reset();
    m_lastSequenceId = 0;
    m_deadlineMs = 0;
    m_arrivalSent = false;
    m_failureSent = false;
    m_state = m_enabled ? SeamlessTravelState::Connected : SeamlessTravelState::Disconnected;
}

SeamlessAction SeamlessTravelStateMachine::OnHostTravelDetected(
    const SeamlessTravelEvent& input, const SeamlessMatchingSnapshot& matching, s64 nowMs) {
    if (!m_enabled || !matching.controlConnected || !matching.serverSupportsControl ||
        !matching.inRoom || !matching.roomOwner || matching.explicitDisconnect ||
        matching.localUserId == 0 || matching.roomId == 0 || input.destinationMap == 0 ||
        input.warpParamId < 0 || m_activeTravel.has_value() || input.mode > 8 ||
        !IsFinitePlacement(input)) {
        return {};
    }

    SeamlessTravelEvent event = input;
    event.protocolVersion = 1;
    event.phase = SeamlessTravelPhase::TravelBegin;
    event.partyId.clear();
    event.generation = 0;
    event.sequenceId = m_lastSequenceId + 1;
    event.leaderUserId = matching.localUserId;
    event.activeRoomId = matching.roomId;
    event.timestampMs = nowMs;
    m_activeTravel = event;
    m_lastSequenceId = event.sequenceId;
    m_state = SeamlessTravelState::TravelPreparing;
    m_deadlineMs = nowMs + m_options.transitionTimeoutMs;
    m_arrivalSent = false;
    m_failureSent = false;
    return {SeamlessActionType::SendTravelBegin, event};
}

void SeamlessTravelStateMachine::OnControlReply(SeamlessTravelPhase requestedPhase, bool accepted,
                                                const std::string& partyId, u64 generation,
                                                u64 sequenceId, SeamlessTravelState serverState,
                                                s64 nowMs) {
    if (!m_enabled || !m_activeTravel.has_value())
        return;
    if (!accepted) {
        if (requestedPhase == SeamlessTravelPhase::TravelBegin ||
            requestedPhase == SeamlessTravelPhase::TravelCommit) {
            m_state = SeamlessTravelState::RecoveringRoom;
        }
        return;
    }
    if (requestedPhase == SeamlessTravelPhase::TravelBegin) {
        if (partyId.empty() || generation == 0 || sequenceId < m_lastSequenceId)
            return;
        m_activeTravel->partyId = partyId;
        m_activeTravel->generation = generation;
        m_activeTravel->sequenceId = sequenceId;
        m_lastSequenceId = sequenceId;
        m_deadlineMs = nowMs + m_options.transitionTimeoutMs;
    } else if (partyId != m_activeTravel->partyId || generation != m_activeTravel->generation ||
               sequenceId != m_activeTravel->sequenceId) {
        return;
    }
    if (requestedPhase == SeamlessTravelPhase::TravelCommit)
        m_state = SeamlessTravelState::Traveling;
    if (requestedPhase == SeamlessTravelPhase::TravelArrived &&
        serverState == SeamlessTravelState::Connected) {
        m_state = SeamlessTravelState::Connected;
        m_activeTravel.reset();
        m_deadlineMs = 0;
    }
}

bool SeamlessTravelStateMachine::ValidateCommonEvent(const SeamlessTravelEvent& event,
                                                     u64 sourceUserId,
                                                     const SeamlessMatchingSnapshot& matching,
                                                     s64 nowMs) const {
    if (!m_enabled || !matching.controlConnected || !matching.serverSupportsControl ||
        matching.explicitDisconnect || event.protocolVersion != 1 || sourceUserId == 0 ||
        event.leaderUserId == 0 || event.partyId.empty() || event.generation == 0 ||
        event.sequenceId == 0 || event.destinationMap == 0 || event.warpParamId < 0 ||
        event.mode > 8 || !IsFinitePlacement(event) || event.timestampMs > nowMs + 30'000 ||
        nowMs > event.timestampMs + 120'000) {
        return false;
    }
    if (matching.inRoom && matching.roomId != 0 && event.activeRoomId != matching.roomId &&
        (!m_activeTravel.has_value() || m_activeTravel->partyId.empty())) {
        return false;
    }
    if (m_activeTravel.has_value() && !m_activeTravel->partyId.empty() &&
        (event.partyId != m_activeTravel->partyId ||
         event.generation != m_activeTravel->generation)) {
        return false;
    }
    return true;
}

SeamlessAction SeamlessTravelStateMachine::BuildControlAction(SeamlessActionType type,
                                                              SeamlessTravelPhase phase,
                                                              s64 nowMs) const {
    if (!m_activeTravel.has_value())
        return {};
    SeamlessTravelEvent event = *m_activeTravel;
    event.phase = phase;
    event.timestampMs = nowMs;
    return {type, std::move(event)};
}

SeamlessAction SeamlessTravelStateMachine::OnNotification(const SeamlessTravelEvent& event,
                                                          u64 sourceUserId,
                                                          const SeamlessMatchingSnapshot& matching,
                                                          s64 nowMs) {
    if (!ValidateCommonEvent(event, sourceUserId, matching, nowMs))
        return {};

    if (event.phase == SeamlessTravelPhase::TravelBegin) {
        if (sourceUserId != event.leaderUserId || matching.localUserId == event.leaderUserId ||
            event.sequenceId <= m_lastSequenceId) {
            return {};
        }
        m_activeTravel = event;
        m_lastSequenceId = event.sequenceId;
        m_state = SeamlessTravelState::TravelPreparing;
        m_deadlineMs = nowMs + m_options.transitionTimeoutMs;
        m_arrivalSent = false;
        m_failureSent = false;
        return BuildControlAction(SeamlessActionType::SendTravelReady,
                                  SeamlessTravelPhase::TravelReady, nowMs);
    }
    if (!m_activeTravel.has_value() || event.sequenceId != m_activeTravel->sequenceId)
        return {};

    if (event.phase == SeamlessTravelPhase::TravelReady) {
        if (matching.localUserId != event.leaderUserId || sourceUserId == event.leaderUserId)
            return {};
        // A guest can answer the server-forwarded Begin before the leader receives
        // its own Begin reply. Adopt the server-authoritative party identity here
        // so the resulting Commit is correlated correctly instead of carrying the
        // leader's still-pending empty identity.
        m_activeTravel->partyId = event.partyId;
        m_activeTravel->generation = event.generation;
        m_activeTravel->sequenceId = event.sequenceId;
        m_activeTravel->leaderUserId = event.leaderUserId;
        m_activeTravel->leaderNpid = event.leaderNpid;
        m_activeTravel->activeRoomId = event.activeRoomId;
        return BuildControlAction(SeamlessActionType::SendTravelCommit,
                                  SeamlessTravelPhase::TravelCommit, nowMs);
    }
    if (event.phase == SeamlessTravelPhase::TravelCommit) {
        if (sourceUserId != event.leaderUserId || matching.localUserId == event.leaderUserId)
            return {};
        m_state = SeamlessTravelState::Traveling;
        m_deadlineMs = nowMs + m_options.transitionTimeoutMs;
        return {SeamlessActionType::StartGuestWarp, *m_activeTravel};
    }
    if (event.phase == SeamlessTravelPhase::TravelFailed) {
        m_state = SeamlessTravelState::RecoveringRoom;
        return {};
    }
    if (event.phase == SeamlessTravelPhase::TravelArrived &&
        matching.localUserId == event.leaderUserId) {
        m_state = SeamlessTravelState::Rebinding;
    }
    return {};
}

void SeamlessTravelStateMachine::MarkGuestWarpStarted(s64 nowMs) {
    if (!m_enabled || !m_activeTravel.has_value() || m_state != SeamlessTravelState::Traveling) {
        return;
    }
    m_state = SeamlessTravelState::WorldLoading;
    m_deadlineMs = nowMs + m_options.transitionTimeoutMs;
}

SeamlessAction SeamlessTravelStateMachine::ObserveWorld(u32 currentMap, bool worldReady,
                                                        bool remoteCharacterPresent,
                                                        const SeamlessMatchingSnapshot& matching,
                                                        s64 nowMs) {
    if (!m_enabled || !m_activeTravel.has_value() || !IsTransitionState(m_state) ||
        m_state == SeamlessTravelState::TravelPreparing || matching.explicitDisconnect) {
        return {};
    }
    if (currentMap != m_activeTravel->destinationMap || !worldReady)
        return {};

    m_state = SeamlessTravelState::Rebinding;
    if (!m_arrivalSent) {
        m_arrivalSent = true;
        return BuildControlAction(SeamlessActionType::SendTravelArrived,
                                  SeamlessTravelPhase::TravelArrived, nowMs);
    }
    if (remoteCharacterPresent) {
        m_state = SeamlessTravelState::Connected;
        m_activeTravel.reset();
        m_deadlineMs = 0;
    }
    return {};
}

SeamlessAction SeamlessTravelStateMachine::CheckTimeout(const SeamlessMatchingSnapshot& matching,
                                                        s64 nowMs) {
    if (!m_enabled || !m_activeTravel.has_value() || !IsTransitionState(m_state) ||
        nowMs <= m_deadlineMs || m_failureSent) {
        return {};
    }
    m_failureSent = true;
    m_state = matching.controlConnected && !matching.explicitDisconnect
                  ? SeamlessTravelState::RecoveringRoom
                  : SeamlessTravelState::Disconnected;
    auto action = BuildControlAction(SeamlessActionType::SendTravelFailed,
                                     SeamlessTravelPhase::TravelFailed, nowMs);
    action.event.failureReason = "transition_timeout";
    return action;
}

bool SeamlessTravelStateMachine::ShouldGuardMatchingStop(const SeamlessMatchingSnapshot& matching,
                                                         s64 nowMs) const {
    return m_enabled && m_activeTravel.has_value() && IsTransitionState(m_state) &&
           matching.controlConnected && matching.serverSupportsControl &&
           !matching.explicitDisconnect && nowMs <= m_deadlineMs;
}

} // namespace Core::Bloodborne
