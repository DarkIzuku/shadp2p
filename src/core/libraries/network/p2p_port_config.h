// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "common/types.h"

namespace Libraries::Net {

enum class P2PPortConfigSource {
    Automatic,
    Settings,
    Environment,
};

struct P2PPortConfig {
    u16 first{};
    u16 last{};
    P2PPortConfigSource source{P2PPortConfigSource::Automatic};

    bool IsAutomatic() const {
        return source == P2PPortConfigSource::Automatic;
    }

    bool IsRange() const {
        return !IsAutomatic() && last > first;
    }
};

enum class P2PBindStatus {
    Success,
    PortUnavailable,
    FatalError,
};

struct P2PBindAttempt {
    P2PBindStatus status{P2PBindStatus::FatalError};
    u16 selected_port{};
    int error{};
};

struct P2PPortSelection {
    bool success{};
    u16 selected_port{};
    int error{};
    std::string message;
};

using P2PBindCallback = std::function<P2PBindAttempt(u16)>;
using P2PPortRetryCallback = std::function<void(u16, u16)>;

bool ResolveP2PPortConfig(u32 configured_port, u32 configured_range_end,
                          std::string_view legacy_environment, P2PPortConfig* out,
                          std::string* error);

P2PPortSelection SelectP2PPhysicalPort(const P2PPortConfig& config, const P2PBindCallback& bind,
                                       const P2PPortRetryCallback& retry = {});

class P2PVirtualPortRegistry {
public:
    bool Register(void* owner, u16 requested_port, u16* actual_port);
    void Unregister(void* owner, u16 port);
    void* Find(u16 port) const;

private:
    static constexpr u16 ReservedSignalingPort = 65535;

    std::unordered_map<u16, void*> owners;
    u16 next_ephemeral_port{30000};
};

} // namespace Libraries::Net
