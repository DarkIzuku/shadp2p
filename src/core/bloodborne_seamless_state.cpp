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

PendingCrossMapSummonStateMachine::PendingCrossMapSummonStateMachine()
    : PendingCrossMapSummonStateMachine(Options{}) {}

PendingCrossMapSummonStateMachine::PendingCrossMapSummonStateMachine(Options options)
    : m_options(options) {
    m_options.timeoutMs = std::max<s64>(5'000, m_options.timeoutMs);
}

void PendingCrossMapSummonStateMachine::SetEnabled(bool enabled) {
    m_enabled = enabled;
    Reset();
}

void PendingCrossMapSummonStateMachine::Reset() {
    m_pending = {};
    m_deadlineMs = 0;
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
    RefreshDeadline(nowMs);
    return m_pending.generation;
}

bool PendingCrossMapSummonStateMachine::BindRole(SeamlessPeerRole role, u64 generation) {
    if (!m_enabled || role == SeamlessPeerRole::Unknown || !MatchesGeneration(generation))
        return false;
    m_pending.role = role;
    return true;
}

bool PendingCrossMapSummonStateMachine::OnClaimAccepted(s64 nowMs, u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || IsExpired(nowMs))
        return false;
    m_pending.claimAccepted = true;
    m_pending.phase = PendingCrossMapSummonPhase::ClaimAccepted;
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
}

bool PendingCrossMapSummonStateMachine::OnRoomJoinStarted(s64 nowMs, u64 roomId, u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || !m_pending.claimAccepted ||
        IsExpired(nowMs)) {
        return false;
    }
    m_pending.roomJoinStarted = true;
    m_pending.roomJoined = false;
    m_pending.signalingEstablished = false;
    m_pending.roomId = roomId;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return true;
}

bool PendingCrossMapSummonStateMachine::OnRoomJoined(s64 nowMs, u64 roomId, u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || !m_pending.claimAccepted || roomId == 0 ||
        IsExpired(nowMs) || (m_pending.roomId != 0 && m_pending.roomId != roomId)) {
        return false;
    }
    m_pending.roomJoinStarted = true;
    m_pending.roomJoined = true;
    m_pending.roomId = roomId;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return true;
}

bool PendingCrossMapSummonStateMachine::OnSignalingEstablished(s64 nowMs, u64 roomId,
                                                               u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || !m_pending.claimAccepted ||
        !m_pending.roomJoined || roomId == 0 || roomId != m_pending.roomId || IsExpired(nowMs)) {
        return false;
    }
    m_pending.signalingEstablished = true;
    AdvanceReadyPhase();
    RefreshDeadline(nowMs);
    return true;
}

PendingCrossMapSummonDecision PendingCrossMapSummonStateMachine::EvaluateNativeHandoff(
    u32 currentMap, u32 targetMap, s64 nowMs) {
    if (!m_enabled || m_pending.phase == PendingCrossMapSummonPhase::Idle || IsExpired(nowMs))
        return PendingCrossMapSummonDecision::None;
    if (m_pending.phase == PendingCrossMapSummonPhase::Complete)
        return PendingCrossMapSummonDecision::None;
    if (targetMap != m_pending.targetMap)
        return PendingCrossMapSummonDecision::StaleTarget;
    if (currentMap == targetMap) {
        m_pending.phase = m_pending.reloadCount == 0 ? PendingCrossMapSummonPhase::Complete
                                                     : PendingCrossMapSummonPhase::WorldReady;
        return PendingCrossMapSummonDecision::SameMap;
    }
    if (m_pending.reloadCount != 0)
        return PendingCrossMapSummonDecision::DuplicateReload;
    if (!m_pending.claimAccepted)
        return PendingCrossMapSummonDecision::WaitForClaim;
    if (!m_pending.roomJoined)
        return PendingCrossMapSummonDecision::WaitForRoom;
    if (!m_pending.signalingEstablished)
        return PendingCrossMapSummonDecision::WaitForSignaling;
    m_pending.phase = PendingCrossMapSummonPhase::CrossMapCommit;
    RefreshDeadline(nowMs);
    return PendingCrossMapSummonDecision::Commit;
}

bool PendingCrossMapSummonStateMachine::MarkReloadStarted(u64 generation) {
    if (!m_enabled || !MatchesGeneration(generation) || m_pending.reloadCount != 0 ||
        m_pending.phase != PendingCrossMapSummonPhase::CrossMapCommit) {
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
    m_pending.reloadCount = 0;
    m_pending.phase = PendingCrossMapSummonPhase::SignalingEstablished;
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
    if (!MatchesGeneration(generation) ||
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

PendingCrossMapSummonSnapshot PendingCrossMapSummonStateMachine::Snapshot() const {
    return m_pending;
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
        // A guest can answer the server-forwarded Begin before the leader receives its own Begin
        // reply. Adopt the server-authoritative party identity here so the resulting Commit is
        // correlated correctly instead of carrying the leader's still-pending empty identity.
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
