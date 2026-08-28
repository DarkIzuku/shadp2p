// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/libraries/network/p2p_port_config.h"

namespace Libraries::Net {

TEST(P2PPortConfig, UnconfiguredKeepsAutomaticPortSelection) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(0, 0, {}, &config, nullptr));
    EXPECT_TRUE(config.IsAutomatic());

    u16 requested_port = 65535;
    const auto selection = SelectP2PPhysicalPort(config, [&](u16 port) {
        requested_port = port;
        return P2PBindAttempt{P2PBindStatus::Success, 57338, 0};
    });

    ASSERT_TRUE(selection.success);
    EXPECT_EQ(requested_port, 0);
    EXPECT_EQ(selection.selected_port, 57338);
}

TEST(P2PPortConfig, FixedPortUsesConfiguredValue) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 0, {}, &config, nullptr));

    u16 requested_port = 0;
    const auto selection = SelectP2PPhysicalPort(config, [&](u16 port) {
        requested_port = port;
        return P2PBindAttempt{P2PBindStatus::Success, port, 0};
    });

    ASSERT_TRUE(selection.success);
    EXPECT_EQ(requested_port, 31317);
    EXPECT_EQ(selection.selected_port, 31317);
}

TEST(P2PPortConfig, BusyPortTriesNextPortInRange) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 31327, {}, &config, nullptr));

    std::vector<u16> attempts;
    std::vector<std::pair<u16, u16>> retries;
    const auto selection = SelectP2PPhysicalPort(
        config,
        [&](u16 port) {
            attempts.push_back(port);
            return port == 31317 ? P2PBindAttempt{P2PBindStatus::PortUnavailable, 0, 10048}
                                 : P2PBindAttempt{P2PBindStatus::Success, port, 0};
        },
        [&](u16 busy, u16 next) { retries.emplace_back(busy, next); });

    ASSERT_TRUE(selection.success);
    EXPECT_EQ(attempts, (std::vector<u16>{31317, 31318}));
    EXPECT_EQ(retries, (std::vector<std::pair<u16, u16>>{{31317, 31318}}));
    EXPECT_EQ(selection.selected_port, 31318);
}

TEST(P2PPortConfig, ExhaustedRangeReturnsClearError) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 31319, {}, &config, nullptr));

    const auto selection = SelectP2PPhysicalPort(
        config, [](u16) { return P2PBindAttempt{P2PBindStatus::PortUnavailable, 0, 10048}; });

    EXPECT_FALSE(selection.success);
    EXPECT_NE(selection.message.find("31317-31319"), std::string::npos);
    EXPECT_NE(selection.message.find("exhausted"), std::string::npos);
}

TEST(P2PVirtualPorts, RegistersBloodborneVirtualPort40) {
    P2PVirtualPortRegistry registry;
    int peer{};
    u16 actual{};

    ASSERT_TRUE(registry.Register(&peer, 40, &actual));
    EXPECT_EQ(actual, 40);
    EXPECT_EQ(registry.Find(40), &peer);
}

TEST(P2PVirtualPorts, RegistersMatching2VirtualPort30) {
    P2PVirtualPortRegistry registry;
    int peer{};
    u16 actual{};

    ASSERT_TRUE(registry.Register(&peer, 30, &actual));
    EXPECT_EQ(actual, 30);
    EXPECT_EQ(registry.Find(30), &peer);
}

TEST(P2PVirtualPorts, MultiplePeersShareOnePhysicalPort) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 0, {}, &config, nullptr));

    int bind_count{};
    const auto selection = SelectP2PPhysicalPort(config, [&](u16 port) {
        ++bind_count;
        return P2PBindAttempt{P2PBindStatus::Success, port, 0};
    });
    ASSERT_TRUE(selection.success);

    P2PVirtualPortRegistry registry;
    int peer_a{};
    int peer_b{};
    u16 port_a{};
    u16 port_b{};
    EXPECT_TRUE(registry.Register(&peer_a, 40, &port_a));
    EXPECT_TRUE(registry.Register(&peer_b, 30, &port_b));
    EXPECT_EQ(bind_count, 1);
    EXPECT_EQ(selection.selected_port, 31317);
}

TEST(P2PPortConsistency, StunAdvertisesActuallySelectedPort) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(0, 0, {}, &config, nullptr));

    const auto selection = SelectP2PPhysicalPort(config, [](u16 requested_port) {
        EXPECT_EQ(requested_port, 0);
        return P2PBindAttempt{P2PBindStatus::Success, 56950, 0};
    });

    ASSERT_TRUE(selection.success);
    const u16 stun_source_port = selection.selected_port;
    const u16 advertised_port = selection.selected_port;
    EXPECT_EQ(stun_source_port, advertised_port);
}

TEST(P2PPortConsistency, SignalingUsesSelectedPhysicalPort) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 31327, {}, &config, nullptr));

    const auto selection = SelectP2PPhysicalPort(config, [](u16 port) {
        return port == 31317 ? P2PBindAttempt{P2PBindStatus::PortUnavailable, 0, 10048}
                             : P2PBindAttempt{P2PBindStatus::Success, port, 0};
    });

    ASSERT_TRUE(selection.success);
    const u16 signaling_port = selection.selected_port;
    EXPECT_EQ(signaling_port, 31318);
}

TEST(P2PPortConsistency, Matching2UsesSelectedPhysicalPortWithItsVirtualPort) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 0, {}, &config, nullptr));
    const auto selection = SelectP2PPhysicalPort(
        config, [](u16 port) { return P2PBindAttempt{P2PBindStatus::Success, port, 0}; });
    ASSERT_TRUE(selection.success);

    P2PVirtualPortRegistry registry;
    int matching2_peer{};
    u16 virtual_port{};
    ASSERT_TRUE(registry.Register(&matching2_peer, 30, &virtual_port));
    EXPECT_EQ(selection.selected_port, 31317);
    EXPECT_EQ(virtual_port, 30);
}

TEST(P2PPortConfig, SettingsTakePrecedenceOverLegacyEnvironment) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(31317, 0, "40000", &config, nullptr));
    EXPECT_EQ(config.source, P2PPortConfigSource::Settings);
    EXPECT_EQ(config.first, 31317);
}

TEST(P2PPortConfig, LegacyEnvironmentAcceptsOptionalRange) {
    P2PPortConfig config{};
    ASSERT_TRUE(ResolveP2PPortConfig(0, 0, "31317-31327", &config, nullptr));
    EXPECT_EQ(config.source, P2PPortConfigSource::Environment);
    EXPECT_EQ(config.first, 31317);
    EXPECT_EQ(config.last, 31327);
}

} // namespace Libraries::Net
