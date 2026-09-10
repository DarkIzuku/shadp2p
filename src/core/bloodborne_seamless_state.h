// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <optional>
#include <string>
#include "common/types.h"

namespace Core::Bloodborne {

enum class SeamlessPeerRole : u32 {
    Unknown = 0,
    Host = 1,
    Cooperator = 2,
    Invader = 3,
};

struct SeamlessResponderPolicy {
    SeamlessPeerRole role = SeamlessPeerRole::Cooperator;
    s32 summonType = 0;
    s32 goodsId = 205;
    s32 effectId = 9005;
};

constexpr SeamlessResponderPolicy SelectSeamlessResponderPolicy(bool sinisterBellActive) {
    // Captured Bloodborne 1.09 contracts: Small Resonant is SummonType 0 and
    // Sinister Resonant is SummonType 2. No unobserved summon values are guessed.
    return sinisterBellActive ? SeamlessResponderPolicy{SeamlessPeerRole::Invader, 2, 225, 9025}
                              : SeamlessResponderPolicy{SeamlessPeerRole::Cooperator, 0, 205, 9005};
}

enum class SeamlessTravelPhase : u32 {
    TravelBegin = 1,
    TravelReady = 2,
    TravelCommit = 3,
    TravelArrived = 4,
    TravelFailed = 5,
    Heartbeat = 6,
    LeaveParty = 7,
};

enum class SeamlessTravelState : u32 {
    Connected = 1,
    TravelPreparing = 2,
    Traveling = 3,
    WorldLoading = 4,
    Rebinding = 5,
    RecoveringRoom = 6,
    RecoveringPeer = 7,
    Disconnected = 8,
};

enum class SeamlessActionType {
    None,
    SendTravelBegin,
    SendTravelReady,
    SendTravelCommit,
    StartGuestWarp,
    SendTravelArrived,
    SendTravelFailed,
};

struct SeamlessTravelEvent {
    u32 protocolVersion = 1;
    SeamlessTravelPhase phase = SeamlessTravelPhase::TravelBegin;
    std::string partyId;
    u64 generation = 0;
    u64 sequenceId = 0;
    u64 leaderUserId = 0;
    std::string leaderNpid;
    u64 activeRoomId = 0;
    u32 sourceMap = 0;
    u32 destinationMap = 0;
    s32 warpParamId = -1;
    u32 mode = 0;
    float positionX = 0.0F;
    float positionY = 0.0F;
    float positionZ = 0.0F;
    float orientation = 0.0F;
    s64 timestampMs = 0;
    std::string failureReason;
};

struct SeamlessMatchingSnapshot {
    bool controlConnected = false;
    bool serverSupportsControl = false;
    bool inRoom = false;
    bool roomOwner = false;
    bool explicitDisconnect = false;
    u64 localUserId = 0;
    u64 roomId = 0;
};

struct SeamlessAction {
    SeamlessActionType type = SeamlessActionType::None;
    SeamlessTravelEvent event;
};

class SeamlessTravelStateMachine {
public:
    struct Options {
        s64 transitionTimeoutMs = 90'000;
    };

    SeamlessTravelStateMachine();
    explicit SeamlessTravelStateMachine(Options options);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    SeamlessTravelState State() const;
    const std::optional<SeamlessTravelEvent>& ActiveTravel() const;

    SeamlessAction OnHostTravelDetected(const SeamlessTravelEvent& event,
                                        const SeamlessMatchingSnapshot& matching, s64 nowMs);
    void OnControlReply(SeamlessTravelPhase requestedPhase, bool accepted,
                        const std::string& partyId, u64 generation, u64 sequenceId,
                        SeamlessTravelState serverState, s64 nowMs);
    SeamlessAction OnNotification(const SeamlessTravelEvent& event, u64 sourceUserId,
                                  const SeamlessMatchingSnapshot& matching, s64 nowMs);
    void MarkGuestWarpStarted(s64 nowMs);
    SeamlessAction ObserveWorld(u32 currentMap, bool worldReady, bool remoteCharacterPresent,
                                const SeamlessMatchingSnapshot& matching, s64 nowMs);
    SeamlessAction CheckTimeout(const SeamlessMatchingSnapshot& matching, s64 nowMs);
    bool ShouldGuardMatchingStop(const SeamlessMatchingSnapshot& matching, s64 nowMs) const;
    void Reset();

private:
    bool ValidateCommonEvent(const SeamlessTravelEvent& event, u64 sourceUserId,
                             const SeamlessMatchingSnapshot& matching, s64 nowMs) const;
    SeamlessAction BuildControlAction(SeamlessActionType type, SeamlessTravelPhase phase,
                                      s64 nowMs) const;

    Options m_options;
    bool m_enabled = false;
    SeamlessTravelState m_state = SeamlessTravelState::Disconnected;
    std::optional<SeamlessTravelEvent> m_activeTravel;
    u64 m_lastSequenceId = 0;
    s64 m_deadlineMs = 0;
    bool m_arrivalSent = false;
    bool m_failureSent = false;
};

} // namespace Core::Bloodborne
