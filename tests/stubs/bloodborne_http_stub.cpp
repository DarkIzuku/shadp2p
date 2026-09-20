// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_re.h"
#include "core/debugger.h"

namespace Core::Bloodborne {
namespace {

std::optional<std::string> host_placement;

} // namespace

std::optional<std::string> GetSeamlessHostPlacementHeader() {
    return host_placement;
}

bool SetSeamlessHostPlacementHeader(std::string_view value) {
    host_placement = value;
    return true;
}

void ClearSeamlessHostPlacementHeader() {
    host_placement.reset();
}

void NotifySeamlessSummonClaimAccepted() {}

std::uint64_t NotifySeamlessSummonRoomJoinStarted(std::uint64_t) {
    return 0;
}

void NotifySeamlessSummonRoomJoined(std::uint64_t) {}

void NotifySeamlessSummonSignalingEstablished(std::uint64_t) {}

void NotifySeamlessSummonRoomJoinedForPeer(std::uint64_t, std::uint16_t, std::uint16_t,
                                           std::string_view, std::uint64_t) {}

void NotifySeamlessSummonSignalingEstablishedForPeer(std::uint64_t, std::uint16_t, std::string_view,
                                                     std::uint64_t) {}

void NotifySeamlessNpSignalingEstablished(std::int32_t, std::string_view) {}

bool TraceAndGuardSeamlessSignalingDeactivate(std::uintptr_t, std::int32_t, std::int32_t,
                                              std::string_view, std::int32_t) {
    return false;
}

} // namespace Core::Bloodborne

namespace Core::Debugger {

int GetCurrentPid() {
    return 0;
}

} // namespace Core::Debugger
