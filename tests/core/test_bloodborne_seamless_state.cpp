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
}

TEST(BloodborneSeamlessState, CrossMapSummonCommitsOnceAfterSignaling) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x15000000, 100);
    ASSERT_NE(generation, 0);
    EXPECT_TRUE(machine.BindRole(SeamlessPeerRole::Cooperator, generation));
    EXPECT_FALSE(machine.OnRoomJoinStarted(105, 17, generation));
    EXPECT_FALSE(machine.OnSignalingEstablished(106, 17, generation));
    EXPECT_TRUE(machine.OnClaimAccepted(110, generation));
    EXPECT_TRUE(machine.OnRoomJoinStarted(120, 0, generation));
    EXPECT_TRUE(machine.OnRoomJoined(130, 17, generation));
    EXPECT_EQ(machine.EvaluateNativeHandoff(0x18010000, 0x15000000, 131),
              PendingCrossMapSummonDecision::WaitForSignaling);
    EXPECT_FALSE(machine.OnSignalingEstablished(139, 16, generation));
    EXPECT_TRUE(machine.OnSignalingEstablished(140, 17, generation));
    EXPECT_EQ(machine.EvaluateNativeHandoff(0x18010000, 0x15000000, 141),
              PendingCrossMapSummonDecision::Commit);
    EXPECT_TRUE(machine.MarkReloadStarted(generation));
    EXPECT_FALSE(machine.MarkReloadStarted(generation));
    EXPECT_EQ(machine.EvaluateNativeHandoff(0x18010000, 0x15000000, 142),
              PendingCrossMapSummonDecision::DuplicateReload);
    EXPECT_TRUE(machine.MarkWorldReady(0x15000000, generation));
    EXPECT_TRUE(machine.MarkRemoteInserted(generation));
    const auto result = machine.Snapshot();
    EXPECT_EQ(result.phase, PendingCrossMapSummonPhase::Complete);
    EXPECT_EQ(result.roomId, 17);
    EXPECT_EQ(result.reloadCount, 1);
}

TEST(BloodborneSeamlessState, SameMapSummonNeverRequestsArtificialReload) {
    PendingCrossMapSummonStateMachine machine;
    machine.SetEnabled(true);
    const u64 generation = machine.OnPlacementDeferred(0x18010000, 100);
    ASSERT_NE(generation, 0);
    EXPECT_EQ(machine.EvaluateNativeHandoff(0x18010000, 0x18010000, 101),
              PendingCrossMapSummonDecision::SameMap);
    EXPECT_EQ(machine.Snapshot().reloadCount, 0);
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
    EXPECT_EQ(machine.EvaluateNativeHandoff(0x17000000, 0x15000000, 122),
              PendingCrossMapSummonDecision::StaleTarget);
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
