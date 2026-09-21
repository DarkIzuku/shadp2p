// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
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

constexpr bool ShouldNormalizeSeamlessAppearance(SeamlessPeerRole role) {
    // Appearance policy is intentionally separate from team/faction/network role.
    // Invaders keep Bloodborne's hostile presentation and are never folded into
    // the cooperative party.
    return role == SeamlessPeerRole::Cooperator;
}

constexpr bool ShouldRestoreSeamlessGuestHealth(SeamlessPeerRole role) {
    // Bloodborne's exact 1.09 game parameters reduce max HP to 70% in both the
    // cooperative guest state (9006) and the invasion guest state (9026).
    // Seamless removes that guest-only penalty for both roles without changing
    // either role's distinct state/team semantics.
    return role == SeamlessPeerRole::Cooperator || role == SeamlessPeerRole::Invader;
}

struct SeamlessGuestParamPolicy {
    s32 activeEffectId = -1;
    u16 stateInfo = 0;
    float maxHpRate = 1.0F;
    bool normalizeAppearance = false;
};

constexpr std::optional<SeamlessGuestParamPolicy> SelectSeamlessGuestParamPolicy(
    SeamlessPeerRole role) {
    switch (role) {
    case SeamlessPeerRole::Cooperator:
        return SeamlessGuestParamPolicy{9006, 272, 1.0F, true};
    case SeamlessPeerRole::Invader:
        return SeamlessGuestParamPolicy{9026, 276, 1.0F, false};
    default:
        return std::nullopt;
    }
}

enum class HunterDreamInteractionRole : u32 {
    Unknown = 0,
    Solo,
    Host,
    Cooperator,
    Invader,
};

constexpr HunterDreamInteractionRole ClassifyHunterDreamInteractionRole(bool inRoom, bool roomOwner,
                                                                        bool hostEffect,
                                                                        bool cooperatorEffect,
                                                                        bool invaderEffect) {
    if (!inRoom)
        return HunterDreamInteractionRole::Solo;
    if (invaderEffect)
        return HunterDreamInteractionRole::Invader;
    if (roomOwner || hostEffect)
        return HunterDreamInteractionRole::Host;
    if (cooperatorEffect)
        return HunterDreamInteractionRole::Cooperator;
    return HunterDreamInteractionRole::Unknown;
}

constexpr bool ShouldBypassHunterDreamClientGuard(bool seamlessEnabled, bool inHuntersDream,
                                                   bool inRoom,
                                                   HunterDreamInteractionRole localRole,
                                                   SeamlessPeerRole transportRole,
                                                   bool invaderEffect, s32 eventBank,
                                                   s32 eventCommand,
                                                   s32 desiredMultiplayerState) {
    if (!seamlessEnabled || !inHuntersDream || !inRoom || invaderEffect)
        return false;
    if (eventBank != 1003 || eventCommand != 6 || desiredMultiplayerState != 1)
        return false;
    if (localRole == HunterDreamInteractionRole::Host ||
        localRole == HunterDreamInteractionRole::Cooperator) {
        return true;
    }
    return localRole == HunterDreamInteractionRole::Unknown &&
           transportRole == SeamlessPeerRole::Cooperator;
}

enum class HunterDreamInteractionKind : u32 {
    Unknown = 0,
    NormalHeadstone,
    ChaliceHeadstone,
    PersonalService,
    WorldTravel,
};

constexpr HunterDreamInteractionKind ClassifyHunterDreamInteraction(s32 entityId, s32 eventId,
                                                                    s32 eventBank,
                                                                    s32 eventCommand) {
    // These IDs come from the decoded CUSA03173 01.09 m21_00_00_00 events. Other
    // Dream objects deliberately remain Unknown until a runtime capture proves
    // their identity.
    if ((entityId >= 2'100'950 && entityId <= 2'100'953) || eventId == 12'107'000)
        return HunterDreamInteractionKind::NormalHeadstone;
    if ((entityId >= 2'100'954 && entityId <= 2'100'960) || eventId == 12'107'100 ||
        eventId == 12'107'200)
        return HunterDreamInteractionKind::ChaliceHeadstone;
    if (eventBank == 2003 && eventCommand == 49)
        return HunterDreamInteractionKind::WorldTravel;
    return HunterDreamInteractionKind::Unknown;
}

enum class HunterDreamInteractionPhase : u32 {
    Registration = 0,
    Candidate,
    Prompt,
    Select,
    Execute,
    Block,
    Event,
    State,
};

struct HunterDreamInteractionTraceSnapshot {
    HunterDreamInteractionPhase phase = HunterDreamInteractionPhase::State;
    HunterDreamInteractionKind kind = HunterDreamInteractionKind::Unknown;
    HunterDreamInteractionRole role = HunterDreamInteractionRole::Unknown;
    u32 map = 0;
    s32 areaRegion = -1;
    u64 object = 0;
    s32 entityId = -1;
    s32 eventId = -1;
    s32 actionButtonId = -1;
    s32 promptId = -1;
    s32 eventBank = -1;
    s32 eventCommand = -1;
    std::array<u8, 4> gates{};
    bool available = false;
    bool selected = false;
};

enum class HunterDreamInteractionTraceDecision : u32 {
    Suppressed = 0,
    Emit,
    WorldChanged,
};

class HunterDreamInteractionTraceState {
public:
    struct Options {
        s64 repeatAfterMs = 5'000;
        s64 staleAfterMs = 30'000;
        size_t maxEntries = 64;
    };

    HunterDreamInteractionTraceState();
    explicit HunterDreamInteractionTraceState(Options options);

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    HunterDreamInteractionTraceDecision Observe(const HunterDreamInteractionTraceSnapshot& value,
                                                s64 nowMs);
    void ResetForWorld(u32 map);
    void ResetForSessionEnd();
    size_t EntryCount() const;

private:
    struct Entry {
        HunterDreamInteractionTraceSnapshot snapshot;
        s64 lastSeenMs = 0;
        s64 lastEmittedMs = 0;
    };

    static bool SameIdentity(const HunterDreamInteractionTraceSnapshot& left,
                             const HunterDreamInteractionTraceSnapshot& right);
    static bool SameState(const HunterDreamInteractionTraceSnapshot& left,
                          const HunterDreamInteractionTraceSnapshot& right);
    void Expire(s64 nowMs);

    Options m_options;
    bool m_enabled = false;
    u32 m_world = 0;
    std::vector<Entry> m_entries;
};

enum class PendingCrossMapSummonPhase : u32 {
    Idle = 0,
    PlacementDeferred,
    NativeHandoffObserved,
    ClaimAccepted,
    RoomJoinStarted,
    RoomJoined,
    SignalingEstablished,
    CrossMapCommit,
    ReloadStarted,
    WorldReady,
    RemoteInserted,
    Complete,
    Failed,
};

enum class PendingCrossMapSummonDecision : u32 {
    None = 0,
    WaitForPlacement,
    WaitForClaim,
    WaitForRoom,
    WaitForSignaling,
    WaitForNativeHandoff,
    ApplyPlacement,
    VerifyPlacement,
    PlacementComplete,
    Commit,
    DuplicateReload,
    StaleTarget,
    TimedOut,
};

struct PendingCrossMapSummonSnapshot {
    PendingCrossMapSummonPhase phase = PendingCrossMapSummonPhase::Idle;
    SeamlessPeerRole role = SeamlessPeerRole::Unknown;
    u64 generation = 0;
    u64 roomId = 0;
    u16 localMemberId = 0;
    u16 expectedPeerMemberId = 0;
    std::string expectedPeerNpid;
    u32 sourceMap = 0;
    u32 targetMap = 0;
    u32 reloadCount = 0;
    bool placementReady = false;
    bool placementApplied = false;
    bool placementVerified = false;
    bool commitIssued = false;
    bool nativeHandoffObserved = false;
    bool claimAccepted = false;
    bool roomJoinStarted = false;
    bool roomJoined = false;
    bool signalingEstablished = false;
};

class PendingCrossMapSummonStateMachine {
public:
    struct Options {
        s64 timeoutMs = 120'000;
        // Generic sceNpSignaling can become established several seconds before the
        // game finishes the summon claim / Matching2 room path. Keep only the exact
        // peer alive for this bounded convergence window.
        s64 preRoomSignalingGuardMs = 35'000;
    };

    PendingCrossMapSummonStateMachine();
    explicit PendingCrossMapSummonStateMachine(Options options);

    void SetEnabled(bool enabled);
    u64 OnPlacementDeferred(u32 targetMap, s64 nowMs);
    bool OnNativeHandoffObserved(u32 currentMap, u32 targetMap, SeamlessPeerRole role, s64 nowMs,
                                 u64 generation = 0);
    bool BindRole(SeamlessPeerRole role, u64 generation = 0);
    bool OnClaimAccepted(s64 nowMs, u64 generation = 0);
    bool OnRoomJoinStarted(s64 nowMs, u64 roomId = 0, u64 generation = 0);
    bool OnRoomJoined(s64 nowMs, u64 roomId, u64 generation = 0);
    bool OnSignalingEstablished(s64 nowMs, u64 roomId, u64 generation = 0);
    bool OnRoomJoinedForPeer(s64 nowMs, u64 roomId, u16 localMemberId, u16 expectedPeerMemberId,
                             std::string_view expectedPeerNpid, u64 generation = 0);
    bool OnSignalingEstablishedForPeer(s64 nowMs, u64 roomId, u16 peerMemberId,
                                       std::string_view peerNpid, u64 generation = 0);
    PendingCrossMapSummonDecision Evaluate(u32 currentMap, u32 targetMap, s64 nowMs);
    bool BeginCommit(u64 generation);
    bool MarkCommitFailed(u64 generation);
    bool BeginPlacementApply(u64 generation);
    bool MarkPlacementVerified(u32 currentMap, u64 generation);
    bool MarkPlacementFailed(u64 generation);
    bool MarkReloadStarted(u64 generation);
    bool MarkReloadFailed(u64 generation);
    bool MarkWorldReady(u32 currentMap, u64 generation);
    bool MarkRemoteInserted(u64 generation);
    bool ShouldRetainPlacementOnMissingCreate(s64 nowMs) const;
    bool ShouldGuardSignalingDeactivate(std::string_view peerNpid, s64 nowMs) const;
    bool OnSignalingDeactivated(std::string_view peerNpid, s64 nowMs);
    std::string_view LastEventReason() const;
    PendingCrossMapSummonSnapshot Snapshot() const;
    void Reset();

private:
    bool MatchesGeneration(u64 generation) const;
    bool IsExpired(s64 nowMs) const;
    void RefreshDeadline(s64 nowMs);
    void AdvanceReadyPhase();
    void BindUnboundEvents(s64 nowMs);

    Options m_options;
    bool m_enabled = false;
    PendingCrossMapSummonSnapshot m_pending;
    PendingCrossMapSummonSnapshot m_unbound;
    u64 m_nextGeneration = 0;
    s64 m_deadlineMs = 0;
    s64 m_unboundDeadlineMs = 0;
    s64 m_signalingGuardDeadlineMs = 0;
    std::string_view m_lastEventReason{"not_observed"};
};

bool IsSeamlessGuestParamProfileSupported(std::string_view profileName);

constexpr bool IsExpectedBloodborneEbootSha256(std::string_view sha256) {
    return sha256 == "D65F0B4F01D59166AED16F8604196D8B7DD805ABBF0758B356E8F1354C9429F9";
}

constexpr bool IsLegacyBloodborneEbootSha256(std::string_view sha256) {
    return sha256 == "6764938B23539D29C936BCA9880FC4A774E7B0099CE31C7E8C4B0F8BD0BEFB80";
}

constexpr bool ShouldSelectExactBloodborneProfile(bool hashMatched, bool coreSignaturesMatched) {
    return hashMatched && coreSignaturesMatched;
}

constexpr bool ShouldInstallBloodborneVerifiedHook(bool exactProfile, bool signatureMatched) {
    return exactProfile && signatureMatched;
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
