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
              std::nullopt);
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
    EXPECT_EQ(json["SPDM"]["Requester"]["@odata.id"],
              "/redfish/v1/Managers/" +
                  std::string(BMCWEB_REDFISH_MANAGER_URI_NAME));
}

TEST(FillProperties, OmitsConditionalValuesWhenDisabled)
{
    dbus::utility::DBusPropertiesMap properties = {
        {"Enabled", false},
        {"Type",
         std::string("xyz.openbmc_project.Attestation.ComponentIntegrity."
                     "SecurityTechnologyType.SPDM")},
        {"TypeVersion", std::string()},
        {"LastUpdated", uint64_t{0}}};
    nlohmann::json json;

    ASSERT_TRUE(fillProperties(json, properties));
    EXPECT_FALSE(json.contains("SPDM"));
    EXPECT_FALSE(json.contains("LastUpdated"));
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

TEST(ComponentIntegrityDiscovery, RejectsDuplicateMemberIds)
{
    const dbus::utility::MapperGetSubTreePathsResponse uniquePaths = {
        "/xyz/openbmc_project/SPDM/device0",
        "/xyz/openbmc_project/SPDM/device1"};
    const std::optional<std::vector<std::string>> unique =
        getUniqueComponentIds(uniquePaths);
    ASSERT_TRUE(unique);
    EXPECT_EQ(*unique, (std::vector<std::string>{"device0", "device1"}));

    const dbus::utility::MapperGetSubTreePathsResponse duplicatePaths = {
        "/xyz/openbmc_project/SPDM/device0",
        "/xyz/openbmc_project/vendor/device0"};
    EXPECT_FALSE(getUniqueComponentIds(duplicatePaths));
}

TEST(ComponentIntegrityDiscovery, FindsAllMatchingProviders)
{
    const dbus::utility::MapperGetSubTreeResponse subtree = {
        {"/xyz/openbmc_project/SPDM/device0",
         {{"service0", {std::string(componentIntegrityInterface)}}}},
        {"/xyz/openbmc_project/vendor/device0",
         {{"service1", {std::string(componentIntegrityInterface)}}}},
        {"/xyz/openbmc_project/SPDM/device1",
         {{"service2", {std::string(componentIntegrityInterface)}}}}};

    const std::vector<ComponentProvider> providers =
        findComponentProviders(subtree, "device0");
    ASSERT_EQ(providers.size(), 2U);
    EXPECT_EQ(providers[0].service, "service0");
    EXPECT_EQ(providers[1].service, "service1");
}

TEST(ComponentIntegrityCallbacks, HandlesCollectionErrorsAndDuplicateIds)
{
    auto missing = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentIntegrityCollection(
        missing, make_error_code(boost::system::errc::io_error), {});
    EXPECT_EQ(missing->res.jsonValue["Members"], nlohmann::json::array());
    EXPECT_EQ(missing->res.jsonValue["Members@odata.count"], 0);

    auto duplicate = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentIntegrityCollection(
        duplicate, {},
        {"/xyz/openbmc_project/SPDM/device0",
         "/xyz/openbmc_project/vendor/device0"});
    EXPECT_EQ(duplicate->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(ComponentIntegrityCallbacks, HandlesMissingOrAmbiguousMembers)
{
    auto missing = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentIntegrity(missing, "device0", {}, {});
    EXPECT_EQ(missing->res.result(), boost::beast::http::status::not_found);

    const dbus::utility::MapperGetSubTreeResponse duplicateSubtree = {
        {"/xyz/openbmc_project/SPDM/device0",
         {{"service0", {std::string(componentIntegrityInterface)}}}},
        {"/xyz/openbmc_project/vendor/device0",
         {{"service1", {std::string(componentIntegrityInterface)}}}}};
    auto duplicate = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentIntegrity(duplicate, "device0", {}, duplicateSubtree);
    EXPECT_EQ(duplicate->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(ComponentIntegrityCallbacks, RejectsInvalidTargetAssociations)
{
    auto noTarget = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentTarget(noTarget, {}, {});
    EXPECT_EQ(noTarget->res.result(),
              boost::beast::http::status::internal_server_error);

    auto multipleTargets = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentTarget(multipleTargets, {},
                            {"/xyz/openbmc_project/inventory/device0",
                             "/xyz/openbmc_project/inventory/device1"});
    EXPECT_EQ(multipleTargets->res.result(),
              boost::beast::http::status::internal_server_error);

    auto unmappable = std::make_shared<bmcweb::AsyncResp>();
    const dbus::utility::MapperGetObject object = {
        {"inventory.service", {"xyz.openbmc_project.Inventory.Item"}}};
    afterGetComponentTargetObject(
        unmappable, "/xyz/openbmc_project/inventory/device0", {}, object);
    EXPECT_EQ(unmappable->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(ComponentIntegrityCallbacks, HandlesPropertyReadFailure)
{
    auto failed = std::make_shared<bmcweb::AsyncResp>();
    afterGetComponentIntegrityProperties(
        failed, "/xyz/openbmc_project/SPDM/device0",
        make_error_code(boost::system::errc::io_error), {});
    EXPECT_EQ(failed->res.result(),
              boost::beast::http::status::internal_server_error);
}

} // namespace
} // namespace redfish::component_integrity_utils
