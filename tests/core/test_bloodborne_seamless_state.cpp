// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <gtest/gtest.h>
#include "core/bloodborne_seamless_state.h"

namespace Core::Bloodborne {
namespace {

SeamlessMatchingSnapshot HostMatching() {
    return {.controlConnected = true,
            .serverSupportsControl = true,
            .inRoom = true,
            .roomOwner = true,
            .explicitDisconnect = false,
            .localUserId = 1,
            .roomId = 91};
}

SeamlessMatchingSnapshot GuestMatching() {
    auto matching = HostMatching();
    matching.roomOwner = false;
    matching.localUserId = 2;
    return matching;
}

SeamlessTravelEvent Destination() {
    SeamlessTravelEvent event;
    event.sourceMap = 0x18010000;
    event.destinationMap = 0x15000000;
    event.warpParamId = 2102952;
    event.mode = 2;
    return event;
}

TEST(BloodborneSeamlessState, TraditionalModeDoesNothing) {
    SeamlessTravelStateMachine machine;
    EXPECT_EQ(machine.OnHostTravelDetected(Destination(), HostMatching(), 100).type,
              SeamlessActionType::None);
    EXPECT_FALSE(machine.ShouldGuardMatchingStop(HostMatching(), 100));
}

TEST(BloodborneSeamlessState, ResponderRolesPreserveCapturedBellSemantics) {
    const auto cooperator = SelectSeamlessResponderPolicy(false);
    EXPECT_EQ(cooperator.role, SeamlessPeerRole::Cooperator);
    EXPECT_EQ(cooperator.summonType, 0);
    EXPECT_EQ(cooperator.goodsId, 205);
    EXPECT_EQ(cooperator.effectId, 9005);

    const auto invader = SelectSeamlessResponderPolicy(true);
    EXPECT_EQ(invader.role, SeamlessPeerRole::Invader);
    EXPECT_EQ(invader.summonType, 2);
    EXPECT_EQ(invader.goodsId, 225);
    EXPECT_EQ(invader.effectId, 9025);
    EXPECT_TRUE(ShouldNormalizeSeamlessAppearance(cooperator.role));
    EXPECT_FALSE(ShouldNormalizeSeamlessAppearance(invader.role));
    EXPECT_TRUE(ShouldRestoreSeamlessGuestHealth(cooperator.role));
    EXPECT_TRUE(ShouldRestoreSeamlessGuestHealth(invader.role));
    EXPECT_FALSE(ShouldRestoreSeamlessGuestHealth(SeamlessPeerRole::Host));
    EXPECT_FALSE(ShouldRestoreSeamlessGuestHealth(SeamlessPeerRole::Unknown));

    const auto cooperator_params = SelectSeamlessGuestParamPolicy(cooperator.role);
    ASSERT_TRUE(cooperator_params.has_value());
    EXPECT_EQ(cooperator_params->activeEffectId, 9006);
    EXPECT_EQ(cooperator_params->stateInfo, 272);
    EXPECT_FLOAT_EQ(cooperator_params->maxHpRate, 1.0F);
    EXPECT_TRUE(cooperator_params->normalizeAppearance);

    const auto invader_params = SelectSeamlessGuestParamPolicy(invader.role);
    ASSERT_TRUE(invader_params.has_value());
    EXPECT_EQ(invader_params->activeEffectId, 9026);
    EXPECT_EQ(invader_params->stateInfo, 276);
    EXPECT_FLOAT_EQ(invader_params->maxHpRate, 1.0F);
    EXPECT_FALSE(invader_params->normalizeAppearance);
    EXPECT_FALSE(SelectSeamlessGuestParamPolicy(SeamlessPeerRole::Host).has_value());
}

TEST(BloodborneSeamlessState, InteractionTraceDisabledHasNoFunctionalOutput) {
    HunterDreamInteractionTraceState trace;
    HunterDreamInteractionTraceSnapshot value;
    value.map = 0x15000000;
    value.entityId = 2'100'950;
    EXPECT_EQ(trace.Observe(value, 100), HunterDreamInteractionTraceDecision::Suppressed);
    EXPECT_EQ(trace.EntryCount(), 0);
}

TEST(BloodborneSeamlessState, InteractionTraceDeduplicatesAndLogsTransitions) {
    HunterDreamInteractionTraceState::Options options;
    options.repeatAfterMs = 5'000;
    HunterDreamInteractionTraceState trace(options);
    trace.SetEnabled(true);
    HunterDreamInteractionTraceSnapshot value;
    value.phase = HunterDreamInteractionPhase::Prompt;
    value.kind = HunterDreamInteractionKind::NormalHeadstone;
    value.map = 0x15000000;
    value.entityId = 2'100'950;
    EXPECT_EQ(trace.Observe(value, 100), HunterDreamInteractionTraceDecision::Emit);
    EXPECT_EQ(trace.Observe(value, 101), HunterDreamInteractionTraceDecision::Suppressed);
    value.available = true;
    EXPECT_EQ(trace.Observe(value, 102), HunterDreamInteractionTraceDecision::Emit);
    EXPECT_EQ(trace.Observe(value, 103), HunterDreamInteractionTraceDecision::Suppressed);
    EXPECT_EQ(trace.Observe(value, 5'103), HunterDreamInteractionTraceDecision::Emit);
}

TEST(BloodborneSeamlessState, InteractionTraceExpiresStaleEntriesAndBoundsStorage) {
    HunterDreamInteractionTraceState::Options options;
    options.repeatAfterMs = 100;
    options.staleAfterMs = 500;
    options.maxEntries = 2;
    HunterDreamInteractionTraceState trace(options);
    trace.SetEnabled(true);
    HunterDreamInteractionTraceSnapshot value;
    value.map = 0x15000000;
    value.phase = HunterDreamInteractionPhase::Event;
    value.eventBank = 3;
    value.eventCommand = 24;
    value.entityId = 1;
    EXPECT_EQ(trace.Observe(value, 100), HunterDreamInteractionTraceDecision::Emit);
    value.entityId = 2;
    EXPECT_EQ(trace.Observe(value, 200), HunterDreamInteractionTraceDecision::Emit);
    value.entityId = 3;
    EXPECT_EQ(trace.Observe(value, 300), HunterDreamInteractionTraceDecision::Emit);
    EXPECT_EQ(trace.EntryCount(), 2);
    value.entityId = 4;
    EXPECT_EQ(trace.Observe(value, 1'000), HunterDreamInteractionTraceDecision::Emit);
    EXPECT_EQ(trace.EntryCount(), 1);
}

TEST(BloodborneSeamlessState, InteractionTraceResetsOnWorldAndSessionChanges) {
    HunterDreamInteractionTraceState trace;
    trace.SetEnabled(true);
    HunterDreamInteractionTraceSnapshot value;
    value.phase = HunterDreamInteractionPhase::Candidate;
    value.map = 0x15000000;
    value.entityId = 2'100'954;
    EXPECT_EQ(trace.Observe(value, 100), HunterDreamInteractionTraceDecision::Emit);
    value.map = 0x18010000;
    EXPECT_EQ(trace.Observe(value, 101), HunterDreamInteractionTraceDecision::WorldChanged);
    EXPECT_EQ(trace.EntryCount(), 1);
    trace.ResetForSessionEnd();
    EXPECT_EQ(trace.EntryCount(), 0);
    value.map = 0x15000000;
    EXPECT_EQ(trace.Observe(value, 102), HunterDreamInteractionTraceDecision::Emit);
    trace.ResetForWorld(0x15000000);
    EXPECT_EQ(trace.EntryCount(), 0);
}

TEST(BloodborneSeamlessState, InteractionClassificationUsesOnlyCapturedDreamContracts) {
    EXPECT_EQ(ClassifyHunterDreamInteraction(2'100'950, -1, -1, -1),
              HunterDreamInteractionKind::NormalHeadstone);
    EXPECT_EQ(ClassifyHunterDreamInteraction(2'100'960, -1, -1, -1),
              HunterDreamInteractionKind::ChaliceHeadstone);
    EXPECT_EQ(ClassifyHunterDreamInteraction(-1, 12'107'200, -1, -1),
              HunterDreamInteractionKind::ChaliceHeadstone);
    EXPECT_EQ(ClassifyHunterDreamInteraction(-1, -1, 2003, 49),
              HunterDreamInteractionKind::WorldTravel);
    EXPECT_EQ(ClassifyHunterDreamInteraction(2'100'232, -1, 3, 24),
              HunterDreamInteractionKind::Unknown);
}

TEST(BloodborneSeamlessState, InteractionRoleClassificationKeepsRolesDistinct) {
    EXPECT_EQ(ClassifyHunterDreamInteractionRole(false, false, false, false, false),
              HunterDreamInteractionRole::Solo);
    EXPECT_EQ(ClassifyHunterDreamInteractionRole(true, true, true, false, false),
              HunterDreamInteractionRole::Host);
    EXPECT_EQ(ClassifyHunterDreamInteractionRole(true, false, false, true, false),
              HunterDreamInteractionRole::Cooperator);
    EXPECT_EQ(ClassifyHunterDreamInteractionRole(true, false, false, false, true),
              HunterDreamInteractionRole::Invader);
    EXPECT_EQ(ClassifyHunterDreamInteractionRole(true, false, false, false, false),
              HunterDreamInteractionRole::Unknown);
}

TEST(BloodborneSeamlessState, CrossMapSummonCommitsOnceAfterSignaling) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_NE(generation, 0);
    EXPECT_TRUE(machine.BindRole(SeamlessPeerRole::Cooperator, generation));
    EXPECT_TRUE(machine.OnRoomJoinStarted(105, 17, generation));
    EXPECT_TRUE(machine.OnSignalingEstablished(106, 17, generation));
    EXPECT_TRUE(machine.OnClaimAccepted(110, generation));
    EXPECT_TRUE(machine.OnRoomJoined(130, 17, generation));
    EXPECT_FALSE(machine.OnSignalingEstablished(139, 16, generation));
    EXPECT_TRUE(machine.OnSignalingEstablished(140, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 141), PendingCrossMapSummonDecision::Commit);
    EXPECT_TRUE(machine.BeginCommit(generation));
    EXPECT_TRUE(machine.MarkReloadStarted(generation));
    EXPECT_FALSE(machine.MarkReloadStarted(generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 142),
              PendingCrossMapSummonDecision::DuplicateReload);
    EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 143),
              PendingCrossMapSummonDecision::ApplyPlacement);
    EXPECT_TRUE(machine.BeginPlacementApply(generation));
    EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 144),
              PendingCrossMapSummonDecision::VerifyPlacement);
    EXPECT_TRUE(machine.MarkPlacementVerified(0x15000000, generation));
    EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 145),
              PendingCrossMapSummonDecision::PlacementComplete);
    EXPECT_TRUE(machine.MarkRemoteInserted(generation));
    const auto result = machine.Snapshot();
    EXPECT_EQ(result.phase, PendingCrossMapSummonPhase::Complete);
    EXPECT_EQ(result.roomId, 17);
    EXPECT_EQ(result.reloadCount, 1);
}

TEST(BloodborneSeamlessState, HandoffBeforeSignalingRemainsPendingForGameThreadCommit) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnClaimAccepted(110, generation));
    ASSERT_TRUE(machine.OnNativeHandoffObserved(0x18010000, 0x15000000,
                                                SeamlessPeerRole::Cooperator, 120, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 121),
              PendingCrossMapSummonDecision::WaitForRoom);
    ASSERT_TRUE(machine.OnRoomJoinStarted(130, 17, generation));
    ASSERT_TRUE(machine.OnRoomJoined(140, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 141),
              PendingCrossMapSummonDecision::WaitForSignaling);
    ASSERT_TRUE(machine.OnSignalingEstablished(150, 17, generation));

    // The native handoff hook is not required to fire a second time. The periodic
    // game-thread tick can now observe the retained handoff and commit it exactly
    // once.
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 151), PendingCrossMapSummonDecision::Commit);
    EXPECT_TRUE(machine.BeginCommit(generation));
    EXPECT_TRUE(machine.MarkReloadStarted(generation));
    EXPECT_EQ(machine.Snapshot().sourceMap, 0x18010000);
    EXPECT_TRUE(machine.Snapshot().nativeHandoffObserved);
}

TEST(BloodborneSeamlessState, SignalingCanCommitWithoutNativeHandoff) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnClaimAccepted(110, generation));
    ASSERT_TRUE(machine.OnRoomJoinStarted(120, 17, generation));
    ASSERT_TRUE(machine.OnRoomJoined(130, 17, generation));
    ASSERT_TRUE(machine.OnSignalingEstablished(140, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 141), PendingCrossMapSummonDecision::Commit);
    EXPECT_TRUE(machine.BeginCommit(generation));
    EXPECT_TRUE(machine.Snapshot().commitIssued);
    EXPECT_FALSE(machine.Snapshot().nativeHandoffObserved);
}

TEST(BloodborneSeamlessState, SignalingBeforePlacementIsBoundToPendingSummon) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    ASSERT_TRUE(machine.OnClaimAccepted(100));
    ASSERT_TRUE(machine.OnRoomJoined(101, 17));
    ASSERT_TRUE(machine.OnSignalingEstablished(102, 17));

    const u64 generation = machine.OnPlacementDeferred(0x15000000, 103);
    ASSERT_NE(generation, 0);
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 104), PendingCrossMapSummonDecision::Commit);
    EXPECT_EQ(machine.Snapshot().roomId, 17);
}

TEST(BloodborneSeamlessState, PlacementBeforeSignalingCommitsWhenSignalingArrives) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnClaimAccepted(101, generation));
    ASSERT_TRUE(machine.OnRoomJoined(102, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 103),
              PendingCrossMapSummonDecision::WaitForSignaling);
    ASSERT_TRUE(machine.OnSignalingEstablished(104, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 105), PendingCrossMapSummonDecision::Commit);
}

TEST(BloodborneSeamlessState, RoomBeforeClaimRemainsBoundAndWaits) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnRoomJoined(101, 17, generation));
    ASSERT_TRUE(machine.OnSignalingEstablished(102, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 103),
              PendingCrossMapSummonDecision::WaitForClaim);
    ASSERT_TRUE(machine.OnClaimAccepted(104, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 105), PendingCrossMapSummonDecision::Commit);
}

TEST(BloodborneSeamlessState, ExpiredCrossMapSummonFailsWithoutReload) {
    PendingCrossMapSummonStateMachine::Options options;
    options.timeoutMs = 5'000;
    PendingCrossMapSummonStateMachine machine(options);
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnNativeHandoffObserved(0x18010000, 0x15000000,
                                                SeamlessPeerRole::Cooperator, 101, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x15000000, 5'102),
              PendingCrossMapSummonDecision::TimedOut);
    EXPECT_EQ(machine.Snapshot().phase, PendingCrossMapSummonPhase::Failed);
    EXPECT_EQ(machine.Snapshot().reloadCount, 0);
}

TEST(BloodborneSeamlessState, SameMapSummonNeverRequestsArtificialReload) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x18010000, 100);
    ASSERT_NE(generation, 0);
    ASSERT_TRUE(machine.OnClaimAccepted(101, generation));
    ASSERT_TRUE(machine.OnRoomJoined(102, 17, generation));
    ASSERT_TRUE(machine.OnSignalingEstablished(103, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 101),
              PendingCrossMapSummonDecision::ApplyPlacement);
    EXPECT_TRUE(machine.BeginPlacementApply(generation));
    EXPECT_FALSE(machine.BeginPlacementApply(generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 104),
              PendingCrossMapSummonDecision::VerifyPlacement);
    EXPECT_TRUE(machine.MarkPlacementVerified(0x18010000, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 105),
              PendingCrossMapSummonDecision::PlacementComplete);
    EXPECT_TRUE(machine.MarkRemoteInserted(generation));
    EXPECT_EQ(machine.Snapshot().reloadCount, 0);
    EXPECT_TRUE(machine.Snapshot().placementApplied);
    EXPECT_TRUE(machine.Snapshot().placementVerified);
}

TEST(BloodborneSeamlessState, SameMapPlacementWaitsForClaimRoomAndSignaling) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x18010000, 100);
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 101),
              PendingCrossMapSummonDecision::WaitForClaim);
    ASSERT_TRUE(machine.OnClaimAccepted(102, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 103),
              PendingCrossMapSummonDecision::WaitForRoom);
    ASSERT_TRUE(machine.OnRoomJoined(104, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 105),
              PendingCrossMapSummonDecision::WaitForSignaling);
    ASSERT_TRUE(machine.OnSignalingEstablished(106, 17, generation));
    EXPECT_EQ(machine.Evaluate(0x18010000, 0x18010000, 107),
              PendingCrossMapSummonDecision::ApplyPlacement);
}

TEST(BloodborneSeamlessState, OldGenerationCannotApplyOrVerifyPlacement) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 oldGeneration = machine.OnPlacementDeferred(0x15000000, 100);
    const u64 generation = machine.OnPlacementDeferred(0x18010000, 110);
    ASSERT_GT(generation, oldGeneration);
    ASSERT_TRUE(machine.OnClaimAccepted(111, generation));
    ASSERT_TRUE(machine.OnRoomJoined(112, 17, generation));
    ASSERT_TRUE(machine.OnSignalingEstablished(113, 17, generation));
    ASSERT_EQ(machine.Evaluate(0x18010000, 0x18010000, 114),
              PendingCrossMapSummonDecision::ApplyPlacement);
    EXPECT_FALSE(machine.BeginPlacementApply(oldGeneration));
    EXPECT_TRUE(machine.BeginPlacementApply(generation));
    EXPECT_FALSE(machine.MarkPlacementVerified(0x18010000, oldGeneration));
    EXPECT_TRUE(machine.MarkPlacementVerified(0x18010000, generation));
}

TEST(BloodborneSeamlessState, StaleSummonGenerationCannotCommitOrReload) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 oldGeneration = machine.OnPlacementDeferred(0x15000000, 100);
    const u64 currentGeneration = machine.OnPlacementDeferred(0x18010000, 110);
    ASSERT_GT(currentGeneration, oldGeneration);
    EXPECT_FALSE(machine.OnClaimAccepted(120, oldGeneration));
    EXPECT_TRUE(machine.OnClaimAccepted(121, currentGeneration));
    EXPECT_FALSE(machine.MarkReloadStarted(oldGeneration));
    EXPECT_EQ(machine.Evaluate(0x17000000, 0x15000000, 122),
              PendingCrossMapSummonDecision::StaleTarget);
}

TEST(BloodborneSeamlessState, ExactUserEbootSupportsGuestParamPolicy) {
    EXPECT_TRUE(IsSeamlessGuestParamProfileSupported("cusa03173-109-d65f0b4f"));
    EXPECT_TRUE(IsSeamlessGuestParamProfileSupported("cusa03173-109-user-eboot-6764938b"));
    EXPECT_FALSE(IsSeamlessGuestParamProfileSupported("cusa03173-109-reference"));
}

TEST(BloodborneSeamlessState, ExactEbootHashAndCoreSignaturesSelectUserProfile) {
    EXPECT_TRUE(IsExpectedBloodborneEbootSha256(
        "D65F0B4F01D59166AED16F8604196D8B7DD805ABBF0758B356E8F1354C9429F9"));
    EXPECT_TRUE(IsLegacyBloodborneEbootSha256(
        "6764938B23539D29C936BCA9880FC4A774E7B0099CE31C7E8C4B0F8BD0BEFB80"));
    EXPECT_FALSE(IsExpectedBloodborneEbootSha256(
        "0000000000000000000000000000000000000000000000000000000000000000"));
    EXPECT_TRUE(ShouldSelectExactBloodborneProfile(true, true));
    EXPECT_FALSE(ShouldSelectExactBloodborneProfile(false, true));
    EXPECT_FALSE(ShouldSelectExactBloodborneProfile(true, false));
    EXPECT_FALSE(ShouldSelectExactBloodborneProfile(false, false));
}

TEST(BloodborneSeamlessState, RoomAndSignalingEventsConvergeInEitherOrder) {
    for (const bool signalingFirst : {false, true}) {
        PendingCrossMapSummonStateMachine machine;
        machine.SetEnabled(true);
        const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
        ASSERT_TRUE(machine.OnClaimAccepted(101, generation));
        ASSERT_TRUE(machine.OnRoomJoinStarted(102, 17, generation));
        if (signalingFirst) {
            ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(103, 17, 2, "Izuku", generation));
            EXPECT_FALSE(machine.Snapshot().roomJoined);
            ASSERT_TRUE(machine.OnRoomJoinedForPeer(104, 17, 3, 2, "Izuku", generation));
        } else {
            ASSERT_TRUE(machine.OnRoomJoinedForPeer(103, 17, 3, 2, "Izuku", generation));
            ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(104, 17, 2, "Izuku", generation));
        }
        const auto snapshot = machine.Snapshot();
        EXPECT_TRUE(snapshot.roomJoined);
        EXPECT_TRUE(snapshot.signalingEstablished);
        EXPECT_EQ(snapshot.expectedPeerMemberId, 2);
        EXPECT_EQ(snapshot.expectedPeerNpid, "Izuku");
        EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 105),
                  PendingCrossMapSummonDecision::ApplyPlacement);
        EXPECT_EQ(snapshot.reloadCount, 0);
    }
}

TEST(BloodborneSeamlessState, GenericSignalingCanArriveBeforeRoomOrPlacement) {
    for (const bool placementFirst : {false, true}) {
        PendingCrossMapSummonStateMachine machine;
        machine.SetEnabled(true);
        u64 generation = 0;
        if (placementFirst) {
            generation = machine.OnPlacementDeferred(0x15000000, 100);
            ASSERT_TRUE(machine.OnClaimAccepted(101, generation));
        }

        ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(102, 0, 0, "Izuku"));
        EXPECT_EQ(machine.LastEventReason(), placementFirst ? "accepted_pending_without_room"
                                                            : "accepted_unbound_without_room");

        if (!placementFirst) {
            generation = machine.OnPlacementDeferred(0x15000000, 103);
            ASSERT_TRUE(machine.OnClaimAccepted(104, generation));
        }
        EXPECT_TRUE(machine.Snapshot().signalingEstablished);
        EXPECT_FALSE(machine.Snapshot().roomJoined);
        ASSERT_TRUE(machine.OnRoomJoinedForPeer(105, 17, 3, 2, "Izuku", generation));
        EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 106),
                  PendingCrossMapSummonDecision::ApplyPlacement);
    }
}

TEST(BloodborneSeamlessState, StaleRoomGenerationAndWrongSignalingPeerAreIgnored) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 stale = machine.OnPlacementDeferred(0x18010000, 100);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 110);
    ASSERT_GT(generation, stale);
    ASSERT_TRUE(machine.OnClaimAccepted(111, generation));
    EXPECT_FALSE(machine.OnRoomJoinedForPeer(112, 17, 3, 2, "Izuku", stale));
    EXPECT_EQ(machine.LastEventReason(), "stale_generation");
    ASSERT_TRUE(machine.OnRoomJoinedForPeer(113, 17, 3, 2, "Izuku", generation));
    EXPECT_FALSE(machine.OnSignalingEstablishedForPeer(114, 17, 4, "OtherPeer", generation));
    EXPECT_EQ(machine.LastEventReason(), "peer_member_mismatch");
    EXPECT_FALSE(machine.Snapshot().signalingEstablished);
    ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(115, 17, 2, "Izuku", generation));
    EXPECT_TRUE(machine.Snapshot().signalingEstablished);
}

TEST(BloodborneSeamlessState, ExactPendingPeerGuardsPrematureDeactivateBeforeAndAfterRoom) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_TRUE(machine.OnClaimAccepted(101, generation));
    ASSERT_TRUE(machine.OnRoomJoinStarted(102, 17, generation));
    ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(103, 17, 2, "Izuku", generation));
    EXPECT_TRUE(machine.ShouldGuardSignalingDeactivate("Izuku", 104));
    EXPECT_FALSE(machine.ShouldGuardSignalingDeactivate("OtherPeer", 104));
    ASSERT_TRUE(machine.OnRoomJoinedForPeer(105, 17, 3, 2, "Izuku", generation));
    EXPECT_TRUE(machine.ShouldGuardSignalingDeactivate("Izuku", 106));
    EXPECT_FALSE(machine.ShouldGuardSignalingDeactivate("OtherPeer", 106));
    EXPECT_FALSE(machine.ShouldGuardSignalingDeactivate("Izuku", 40'106));
}

TEST(BloodborneSeamlessState, UnboundExactPeerSurvivesVanillaCleanupUntilClaimAndRoomConverge) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);

    ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(100, 0, 0, "Izuku"));
    EXPECT_EQ(machine.LastEventReason(), "accepted_unbound_without_room");
    EXPECT_TRUE(machine.ShouldGuardSignalingDeactivate("Izuku", 101));
    EXPECT_FALSE(machine.ShouldGuardSignalingDeactivate("OtherPeer", 101));

    const u64 generation = machine.OnPlacementDeferred(0x15000000, 105);
    ASSERT_NE(generation, 0);
    EXPECT_TRUE(machine.Snapshot().signalingEstablished);
    ASSERT_TRUE(machine.OnClaimAccepted(106, generation));
    EXPECT_TRUE(machine.ShouldGuardSignalingDeactivate("Izuku", 107));
    ASSERT_TRUE(machine.OnRoomJoinStarted(108, 17, generation));
    ASSERT_TRUE(machine.OnRoomJoinedForPeer(109, 17, 3, 2, "Izuku", generation));

    const auto snapshot = machine.Snapshot();
    EXPECT_TRUE(snapshot.claimAccepted);
    EXPECT_TRUE(snapshot.roomJoined);
    EXPECT_TRUE(snapshot.signalingEstablished);
    EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 110),
              PendingCrossMapSummonDecision::ApplyPlacement);
}

TEST(BloodborneSeamlessState, AllowedDeactivateClearsStaleRetainedSignaling) {
    PendingCrossMapSummonStateMachine::Options options;
    options.preRoomSignalingGuardMs = 5'000;
    PendingCrossMapSummonStateMachine machine(options);
    machine.SetEnabled(true);

    ASSERT_TRUE(machine.OnSignalingEstablishedForPeer(100, 0, 0, "Izuku"));
    EXPECT_TRUE(machine.ShouldGuardSignalingDeactivate("Izuku", 101));
    EXPECT_FALSE(machine.ShouldGuardSignalingDeactivate("Izuku", 5'101));
    EXPECT_TRUE(machine.OnSignalingDeactivated("Izuku", 5'101));

    const u64 generation = machine.OnPlacementDeferred(0x15000000, 5'102);
    ASSERT_TRUE(machine.OnClaimAccepted(5'103, generation));
    EXPECT_FALSE(machine.Snapshot().signalingEstablished);
    EXPECT_EQ(machine.Evaluate(0x15000000, 0x15000000, 5'104),
              PendingCrossMapSummonDecision::WaitForRoom);
}

TEST(BloodborneSeamlessState, InvalidSignatureNeverInstallsVerifiedHook) {
    EXPECT_TRUE(ShouldInstallBloodborneVerifiedHook(true, true));
    EXPECT_FALSE(ShouldInstallBloodborneVerifiedHook(true, false));
    EXPECT_FALSE(ShouldInstallBloodborneVerifiedHook(false, true));
    EXPECT_FALSE(ShouldInstallBloodborneVerifiedHook(false, false));
}

TEST(BloodborneSeamlessState, MissingCreateHeaderRetainsOnlyAcceptedInFlightPlacement) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    EXPECT_FALSE(machine.ShouldRetainPlacementOnMissingCreate(101));
    ASSERT_TRUE(machine.OnClaimAccepted(102, generation));
    EXPECT_TRUE(machine.ShouldRetainPlacementOnMissingCreate(103));
    EXPECT_FALSE(machine.ShouldRetainPlacementOnMissingCreate(120'103));
}

TEST(BloodborneSeamlessState, HostBeginsAndCommitsValidatedTravel) {
    SeamlessTravelStateMachine machine;
    machine.SetEnabled(true);
    auto begin = machine.OnHostTravelDetected(Destination(), HostMatching(), 100);
    ASSERT_EQ(begin.type, SeamlessActionType::SendTravelBegin);
    EXPECT_EQ(begin.event.sequenceId, 1);
    EXPECT_TRUE(machine.ShouldGuardMatchingStop(HostMatching(), 101));
    EXPECT_EQ(machine.ObserveWorld(0x15000000, true, true, HostMatching(), 102).type,
              SeamlessActionType::None);
    EXPECT_EQ(machine.OnHostTravelDetected(Destination(), HostMatching(), 103).type,
              SeamlessActionType::None);
    machine.OnControlReply(SeamlessTravelPhase::TravelBegin, true, "party-1", 4, 1,
                           SeamlessTravelState::TravelPreparing, 110);
    ASSERT_TRUE(machine.ActiveTravel().has_value());
    EXPECT_EQ(machine.ActiveTravel()->partyId, "party-1");

    auto ready = *machine.ActiveTravel();
    ready.phase = SeamlessTravelPhase::TravelReady;
    ready.timestampMs = 120;
    auto commit = machine.OnNotification(ready, 2, HostMatching(), 120);
    EXPECT_EQ(commit.type, SeamlessActionType::SendTravelCommit);
    machine.OnControlReply(SeamlessTravelPhase::TravelCommit, true, "party-1", 4, 99,
                           SeamlessTravelState::Traveling, 125);
    EXPECT_EQ(machine.State(), SeamlessTravelState::TravelPreparing);
    machine.OnControlReply(SeamlessTravelPhase::TravelCommit, true, "party-1", 4, 1,
                           SeamlessTravelState::Traveling, 130);
    EXPECT_EQ(machine.State(), SeamlessTravelState::Traveling);
    EXPECT_TRUE(machine.ShouldGuardMatchingStop(HostMatching(), 130));
}

TEST(BloodborneSeamlessState, HostAcceptsReadyBeforeBeginReply) {
    SeamlessTravelStateMachine machine;
    machine.SetEnabled(true);
    auto begin = machine.OnHostTravelDetected(Destination(), HostMatching(), 100);
    ASSERT_EQ(begin.type, SeamlessActionType::SendTravelBegin);

    auto earlyReady = begin.event;
    earlyReady.phase = SeamlessTravelPhase::TravelReady;
    earlyReady.partyId = "party-1";
    earlyReady.generation = 4;
    earlyReady.leaderUserId = 1;
    earlyReady.activeRoomId = 91;
    earlyReady.timestampMs = 110;
    auto commit = machine.OnNotification(earlyReady, 2, HostMatching(), 110);
    ASSERT_EQ(commit.type, SeamlessActionType::SendTravelCommit);
    EXPECT_EQ(commit.event.partyId, "party-1");
    EXPECT_EQ(commit.event.generation, 4);

    machine.OnControlReply(SeamlessTravelPhase::TravelCommit, true, "party-1", 4, 1,
                           SeamlessTravelState::Traveling, 120);
    EXPECT_EQ(machine.State(), SeamlessTravelState::Traveling);
    machine.OnControlReply(SeamlessTravelPhase::TravelBegin, true, "party-1", 4, 1,
                           SeamlessTravelState::TravelPreparing, 121);
    EXPECT_EQ(machine.State(), SeamlessTravelState::Traveling);
}

TEST(BloodborneSeamlessState, GuestRejectsStaleAndFollowsCommit) {
    SeamlessTravelStateMachine machine;
    machine.SetEnabled(true);
    auto begin = Destination();
    begin.partyId = "party-1";
    begin.generation = 4;
    begin.sequenceId = 12;
    begin.leaderUserId = 1;
    begin.activeRoomId = 91;
    begin.timestampMs = 100;

    auto ready = machine.OnNotification(begin, 1, GuestMatching(), 110);
    ASSERT_EQ(ready.type, SeamlessActionType::SendTravelReady);
    auto duplicate = machine.OnNotification(begin, 1, GuestMatching(), 111);
    EXPECT_EQ(duplicate.type, SeamlessActionType::None);

    begin.phase = SeamlessTravelPhase::TravelCommit;
    begin.timestampMs = 120;
    auto warp = machine.OnNotification(begin, 1, GuestMatching(), 120);
    ASSERT_EQ(warp.type, SeamlessActionType::StartGuestWarp);
    EXPECT_EQ(warp.event.warpParamId, 2102952);
    machine.MarkGuestWarpStarted(121);
    EXPECT_EQ(machine.State(), SeamlessTravelState::WorldLoading);
    EXPECT_TRUE(machine.ShouldGuardMatchingStop(GuestMatching(), 122));
}

TEST(BloodborneSeamlessState, ArrivalAndTimeoutAreBounded) {
    SeamlessTravelStateMachine::Options options;
    options.transitionTimeoutMs = 5'000;
    SeamlessTravelStateMachine machine(options);
    machine.SetEnabled(true);
    auto begin = Destination();
    begin.partyId = "party-1";
    begin.generation = 1;
    begin.sequenceId = 1;
    begin.leaderUserId = 1;
    begin.activeRoomId = 91;
    begin.timestampMs = 100;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 100).type,
              SeamlessActionType::SendTravelReady);
    begin.phase = SeamlessTravelPhase::TravelCommit;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 110).type,
              SeamlessActionType::StartGuestWarp);
    machine.MarkGuestWarpStarted(111);
    auto arrived = machine.ObserveWorld(0x15000000, true, false, GuestMatching(), 200);
    EXPECT_EQ(arrived.type, SeamlessActionType::SendTravelArrived);
    EXPECT_EQ(machine.State(), SeamlessTravelState::Rebinding);
    EXPECT_EQ(machine.ObserveWorld(0x15000000, true, true, GuestMatching(), 210).type,
              SeamlessActionType::None);
    EXPECT_EQ(machine.State(), SeamlessTravelState::Connected);

    machine.Reset();
    begin.phase = SeamlessTravelPhase::TravelBegin;
    begin.sequenceId = 2;
    begin.timestampMs = 300;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 300).type,
              SeamlessActionType::SendTravelReady);
    begin.phase = SeamlessTravelPhase::TravelCommit;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 310).type,
              SeamlessActionType::StartGuestWarp);
    machine.MarkGuestWarpStarted(311);
    auto timeout = machine.CheckTimeout(GuestMatching(), 5'312);
    EXPECT_EQ(timeout.type, SeamlessActionType::SendTravelFailed);
    EXPECT_EQ(timeout.event.failureReason, "transition_timeout");
    EXPECT_FALSE(machine.ShouldGuardMatchingStop(GuestMatching(), 5'312));
}

TEST(BloodborneSeamlessState, MatchingStopGuardRequiresLiveValidatedSession) {
    SeamlessTravelStateMachine machine;
    machine.SetEnabled(true);
    auto begin = Destination();
    begin.partyId = "party-1";
    begin.generation = 1;
    begin.sequenceId = 1;
    begin.leaderUserId = 1;
    begin.activeRoomId = 91;
    begin.timestampMs = 100;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 100).type,
              SeamlessActionType::SendTravelReady);
    begin.phase = SeamlessTravelPhase::TravelCommit;
    ASSERT_EQ(machine.OnNotification(begin, 1, GuestMatching(), 110).type,
              SeamlessActionType::StartGuestWarp);
    machine.MarkGuestWarpStarted(111);
    EXPECT_TRUE(machine.ShouldGuardMatchingStop(GuestMatching(), 112));
    auto disconnected = GuestMatching();
    disconnected.explicitDisconnect = true;
    EXPECT_FALSE(machine.ShouldGuardMatchingStop(disconnected, 112));
}

} // namespace
} // namespace Core::Bloodborne
