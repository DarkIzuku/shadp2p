// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace Core::Bloodborne {

std::optional<std::string> GetSeamlessHostPlacementHeader();
bool SetSeamlessHostPlacementHeader(std::string_view value);
void ClearSeamlessHostPlacementHeader();
void TraceMatching2LeaveRoom(std::uintptr_t return_address, std::uint64_t room_id);
void InstallSeamlessCoopPatches();
void RecordReverseEngineeringImageStage(std::string_view stage, std::uintptr_t image_base,
                                        std::uint64_t image_size, std::uintptr_t executable_base,
                                        std::uint64_t executable_size);
void InstallReverseEngineeringTrace();

} // namespace Core::Bloodborne
