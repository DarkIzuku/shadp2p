// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "p2p_port_config.h"

#include <charconv>
#include <limits>
#include <system_error>

#include <fmt/format.h>

namespace Libraries::Net {

namespace {

bool ParsePort(std::string_view value, u16* out) {
    u32 parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (value.empty() || result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed == 0 || parsed > std::numeric_limits<u16>::max()) {
        return false;
    }
    *out = static_cast<u16>(parsed);
    return true;
}

bool ParseEnvironment(std::string_view value, P2PPortConfig* out, std::string* error) {
    const size_t separator = value.find('-');
    const std::string_view first_value = value.substr(0, separator);
    const std::string_view last_value =
        separator == std::string_view::npos ? first_value : value.substr(separator + 1);

    u16 first{};
    u16 last{};
    if (!ParsePort(first_value, &first) || !ParsePort(last_value, &last) || last < first) {
        if (error != nullptr) {
            *error = fmt::format("invalid SHADPS4_P2P_PORT='{}'", value);
        }
        return false;
    }

    *out = P2PPortConfig{first, last, P2PPortConfigSource::Environment};
    return true;
}

} // namespace

bool ResolveP2PPortConfig(u32 configured_port, u32 configured_range_end,
                          std::string_view legacy_environment, P2PPortConfig* out,
                          std::string* error) {
    if (out == nullptr) {
        return false;
    }

    if (configured_port == 0 && configured_range_end == 0) {
        if (legacy_environment.empty()) {
            *out = {};
            return true;
        }
        return ParseEnvironment(legacy_environment, out, error);
    }

    if (configured_port == 0 || configured_port > std::numeric_limits<u16>::max() ||
        configured_range_end > std::numeric_limits<u16>::max()) {
        if (error != nullptr) {
            *error =
                fmt::format("invalid P2P port range {}-{}", configured_port, configured_range_end);
        }
        return false;
    }

    const u32 effective_end = configured_range_end == 0 ? configured_port : configured_range_end;
    if (effective_end < configured_port) {
        if (error != nullptr) {
            *error = fmt::format("invalid P2P port range {}-{}", configured_port, effective_end);
        }
        return false;
    }

    *out = P2PPortConfig{static_cast<u16>(configured_port), static_cast<u16>(effective_end),
                         P2PPortConfigSource::Settings};
    return true;
}

P2PPortSelection SelectP2PPhysicalPort(const P2PPortConfig& config, const P2PBindCallback& bind,
                                       const P2PPortRetryCallback& retry) {
    if (!bind) {
        return {false, 0, 0, "P2P bind callback is unavailable"};
    }

    if (config.IsAutomatic()) {
        const P2PBindAttempt attempt = bind(0);
        if (attempt.status == P2PBindStatus::Success) {
            return {true, attempt.selected_port, 0, {}};
        }
        return {false, 0, attempt.error,
                fmt::format("automatic P2P port bind failed (error={})", attempt.error)};
    }

    for (u32 port = config.first; port <= config.last; ++port) {
        const P2PBindAttempt attempt = bind(static_cast<u16>(port));
        if (attempt.status == P2PBindStatus::Success) {
            return {true, attempt.selected_port, 0, {}};
        }
        if (attempt.status == P2PBindStatus::FatalError) {
            return {false, 0, attempt.error,
                    fmt::format("P2P port {} bind failed (error={})", port, attempt.error)};
        }
        if (port < config.last && retry) {
            retry(static_cast<u16>(port), static_cast<u16>(port + 1));
        }
    }

    return {false, 0, 0,
            config.IsRange()
                ? fmt::format("P2P port range {}-{} is exhausted", config.first, config.last)
                : fmt::format("P2P port {} is already in use", config.first)};
}

bool P2PVirtualPortRegistry::Register(void* owner, u16 requested_port, u16* actual_port) {
    if (owner == nullptr || actual_port == nullptr) {
        return false;
    }

    u16 port = requested_port;
    if (port == 0) {
        for (u32 attempt = 0; attempt < 35000; ++attempt) {
            if (next_ephemeral_port >= ReservedSignalingPort) {
                next_ephemeral_port = 30000;
            }
            const u16 candidate = next_ephemeral_port++;
            if (candidate == ReservedSignalingPort || owners.contains(candidate)) {
                continue;
            }
            port = candidate;
            break;
        }
    }

    if (port == 0 || port == ReservedSignalingPort || owners.contains(port)) {
        return false;
    }

    owners.emplace(port, owner);
    *actual_port = port;
    return true;
}

void P2PVirtualPortRegistry::Unregister(void* owner, u16 port) {
    const auto it = owners.find(port);
    if (it != owners.end() && it->second == owner) {
        owners.erase(it);
    }
}

void* P2PVirtualPortRegistry::Find(u16 port) const {
    const auto it = owners.find(port);
    return it != owners.end() ? it->second : nullptr;
}

} // namespace Libraries::Net
