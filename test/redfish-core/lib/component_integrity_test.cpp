// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "bmcweb_config.h"

#include "component_integrity.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace redfish::component_integrity_utils
{
namespace
{

TEST(ComponentIntegrityPath, SearchesPublicOpenBmcObjectTree)
{
    EXPECT_EQ(componentIntegrityPath, "/xyz/openbmc_project");
}

TEST(TranslateSecurityTechnologyType, MapsPublicInterfaceValues)
{
    EXPECT_EQ(translateSecurityTechnologyType(
                  "xyz.openbmc_project.Attestation.ComponentIntegrity."
                  "SecurityTechnologyType.SPDM"),
              "SPDM");
    EXPECT_EQ(translateSecurityTechnologyType(
                  "xyz.openbmc_project.Attestation.ComponentIntegrity."
                  "SecurityTechnologyType.TPM"),
              "TPM");
    EXPECT_EQ(translateSecurityTechnologyType(
                  "xyz.openbmc_project.Attestation.ComponentIntegrity."
                  "SecurityTechnologyType.OEM"),
              "OEM");
    EXPECT_EQ(translateSecurityTechnologyType(
                  "xyz.openbmc_project.Attestation.ComponentIntegrity."
                  "SecurityTechnologyType.Unknown"),
              std::nullopt);
}

TEST(TargetComponentUri, MapsWellKnownInventoryTypes)
{
    const sdbusplus::object_path path(
        "/xyz/openbmc_project/inventory/system/chassis/component0");
    const std::vector<std::string> chassis = {
        "xyz.openbmc_project.Inventory.Item.Chassis"};
    const std::vector<std::string> board = {
        "xyz.openbmc_project.Inventory.Item.Board"};
    const std::vector<std::string> cpu = {
        "xyz.openbmc_project.Inventory.Item.Cpu"};
    const std::vector<std::string> dimm = {
        "xyz.openbmc_project.Inventory.Item.Dimm"};
    const std::vector<std::string> pcieDevice = {
        "xyz.openbmc_project.Inventory.Item.PCIeDevice"};

    EXPECT_EQ(targetComponentUri(path, chassis),
              "/redfish/v1/Chassis/component0");
    EXPECT_EQ(targetComponentUri(path, board),
              "/redfish/v1/Chassis/component0");
    EXPECT_EQ(targetComponentUri(path, cpu),
              "/redfish/v1/Systems/" +
                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                  "/Processors/component0");
    EXPECT_EQ(targetComponentUri(path, dimm),
              "/redfish/v1/Systems/" +
                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                  "/Memory/component0");
    EXPECT_EQ(targetComponentUri(path, pcieDevice),
              "/redfish/v1/Systems/" +
                  std::string(BMCWEB_REDFISH_SYSTEM_URI_NAME) +
                  "/PCIeDevices/component0");
}

TEST(TargetComponentUri, RejectsUnknownOrEmptyTargets)
{
    const std::vector<std::string> inventoryItem = {
        "xyz.openbmc_project.Inventory.Item"};
    std::vector<std::string> targetInterfaceStrings;
    targetInterfaceStrings.reserve(targetInterfaces.size());
    for (std::string_view interface : targetInterfaces)
    {
        targetInterfaceStrings.emplace_back(interface);
    }

    EXPECT_EQ(
        targetComponentUri(
            sdbusplus::object_path("/xyz/openbmc_project/inventory/component0"),
            inventoryItem),
        std::nullopt);
    EXPECT_EQ(
        targetComponentUri(sdbusplus::object_path("/"), targetInterfaceStrings),
        std::nullopt);
}

TEST(FillProperties, ProducesRequiredRedfishValues)
{
    dbus::utility::DBusPropertiesMap properties = {
        {"Enabled", true},
        {"Type",
         std::string("xyz.openbmc_project.Attestation.ComponentIntegrity."
                     "SecurityTechnologyType.SPDM")},
        {"TypeVersion", std::string("1.2.0")},
        {"LastUpdated", uint64_t{1000}}};
    nlohmann::json json;

    ASSERT_TRUE(fillProperties(json, properties));
    EXPECT_EQ(json["ComponentIntegrityEnabled"], true);
    EXPECT_EQ(json["ComponentIntegrityType"], "SPDM");
    EXPECT_EQ(json["ComponentIntegrityTypeVersion"], "1.2.0");
    EXPECT_EQ(json["LastUpdated"], time_utils::getDateTimeUintMs(1000));
}

TEST(FillProperties, RejectsUnknownOrMissingValues)
{
    dbus::utility::DBusPropertiesMap unknownType = {
        {"Enabled", true},
        {"Type",
         std::string("xyz.openbmc_project.Attestation.ComponentIntegrity."
                     "SecurityTechnologyType.Unknown")},
        {"TypeVersion", std::string()},
        {"LastUpdated", uint64_t{0}}};
    nlohmann::json json;
    EXPECT_FALSE(fillProperties(json, unknownType));

    dbus::utility::DBusPropertiesMap missingType = {
        {"Enabled", true},
        {"TypeVersion", std::string("1.2.0")},
        {"LastUpdated", uint64_t{0}}};
    EXPECT_FALSE(fillProperties(json, missingType));
}

} // namespace
} // namespace redfish::component_integrity_utils
