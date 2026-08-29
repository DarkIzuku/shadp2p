// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "core/emulator_settings.h"

TEST(P2PSettings, DefaultsToAutomatic) {
    const GeneralSettings settings;
    EXPECT_EQ(settings.p2p_port.value, 0u);
    EXPECT_EQ(settings.p2p_port_range_end.value, 0u);
}

TEST(P2PSettings, SerializesFixedPortAndRange) {
    GeneralSettings settings;
    settings.p2p_port.value = 31317;
    settings.p2p_port_range_end.value = 31327;

    const nlohmann::json json = settings;
    EXPECT_EQ(json.at("p2p_port"), 31317u);
    EXPECT_EQ(json.at("p2p_port_range_end"), 31327u);

    GeneralSettings loaded;
    json.get_to(loaded);
    EXPECT_EQ(loaded.p2p_port.value, 31317u);
    EXPECT_EQ(loaded.p2p_port_range_end.value, 31327u);
}

TEST(P2PSettings, SupportsGameSpecificOverrides) {
    GeneralSettings settings;
    settings.p2p_port.value = 0;
    settings.p2p_port_range_end.value = 0;
    settings.p2p_port.game_specific_value = 31317;
    settings.p2p_port_range_end.game_specific_value = 31327;

    EXPECT_EQ(settings.p2p_port.get(ConfigMode::Global), 0u);
    EXPECT_EQ(settings.p2p_port.get(ConfigMode::Default), 31317u);
    EXPECT_EQ(settings.p2p_port_range_end.get(ConfigMode::Default), 31327u);
}

TEST(P2PSettings, PortsAreAvailableAsGameOverrides) {
    const GeneralSettings settings;
    const auto fields = settings.GetOverrideableFields();
    const auto has = [&](const char* key) {
        return std::any_of(fields.begin(), fields.end(), [key](const OverrideItem& field) {
            return std::string(field.key) == key;
        });
    };

    EXPECT_TRUE(has("p2p_port"));
    EXPECT_TRUE(has("p2p_port_range_end"));
}
