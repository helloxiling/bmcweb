// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "bmcweb_config.h"

#include "app.hpp"
#include "async_resp.hpp"
#include "boost_formatters.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/collection.hpp"
#include "utils/dbus_utils.hpp"
#include "utils/time_utils.hpp"

#include <boost/system/error_code.hpp>
#include <boost/url/format.hpp>
#include <sdbusplus/message/native_types.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace redfish
{

namespace component_integrity_utils
{

inline constexpr std::string_view componentIntegrityPath =
    "/xyz/openbmc_project/component_integrity";
inline constexpr std::string_view componentIntegrityInterface =
    "xyz.openbmc_project.Attestation.ComponentIntegrity";
inline constexpr std::array<std::string_view, 1> componentIntegrityInterfaces =
    {componentIntegrityInterface};
inline constexpr std::array<std::string_view, 5> targetInterfaces = {
    "xyz.openbmc_project.Inventory.Item.Board",
    "xyz.openbmc_project.Inventory.Item.Chassis",
    "xyz.openbmc_project.Inventory.Item.Cpu",
    "xyz.openbmc_project.Inventory.Item.Dimm",
    "xyz.openbmc_project.Inventory.Item.PCIeDevice"};

inline std::optional<std::string> translateSecurityTechnologyType(
    std::string_view type)
{
    if (type.ends_with(".SPDM"))
    {
        return "SPDM";
    }
    if (type.ends_with(".TPM"))
    {
        return "TPM";
    }
    if (type.ends_with(".OEM"))
    {
        return "OEM";
    }
    return std::nullopt;
}

inline bool hasInterface(std::span<const std::string> interfaces,
                         std::string_view expected)
{
    return std::ranges::find(interfaces, expected) != interfaces.end();
}

inline std::optional<std::string> targetComponentUri(
    const sdbusplus::object_path& targetPath,
    std::span<const std::string> interfaces)
{
    const std::string id = targetPath.filename();
    if (id.empty())
    {
        return std::nullopt;
    }

    if (hasInterface(interfaces,
                     "xyz.openbmc_project.Inventory.Item.Chassis") ||
        hasInterface(interfaces, "xyz.openbmc_project.Inventory.Item.Board"))
    {
        return std::string(
            boost::urls::format("/redfish/v1/Chassis/{}", id).buffer());
    }
    if (hasInterface(interfaces, "xyz.openbmc_project.Inventory.Item.Cpu"))
    {
        return std::string(
            boost::urls::format("/redfish/v1/Systems/{}/Processors/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, id)
                .buffer());
    }
    if (hasInterface(interfaces, "xyz.openbmc_project.Inventory.Item.Dimm"))
    {
        return std::string(
            boost::urls::format("/redfish/v1/Systems/{}/Memory/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, id)
                .buffer());
    }
    if (hasInterface(interfaces,
                     "xyz.openbmc_project.Inventory.Item.PCIeDevice"))
    {
        return std::string(
            boost::urls::format("/redfish/v1/Systems/{}/PCIeDevices/{}",
                                BMCWEB_REDFISH_SYSTEM_URI_NAME, id)
                .buffer());
    }
    return std::nullopt;
}

inline bool fillProperties(nlohmann::json& json,
                           const dbus::utility::DBusPropertiesMap& properties)
{
    const bool* enabled = nullptr;
    const std::string* type = nullptr;
    const std::string* typeVersion = nullptr;
    const uint64_t* lastUpdated = nullptr;

    const bool success = sdbusplus::unpackPropertiesNoThrow(
        dbus_utils::UnpackErrorPrinter(), properties, "Enabled", enabled,
        "Type", type, "TypeVersion", typeVersion, "LastUpdated", lastUpdated);
    if (!success || enabled == nullptr || type == nullptr ||
        typeVersion == nullptr || lastUpdated == nullptr)
    {
        return false;
    }

    std::optional<std::string> redfishType =
        translateSecurityTechnologyType(*type);
    if (!redfishType)
    {
        return false;
    }

    json["ComponentIntegrityEnabled"] = *enabled;
    json["ComponentIntegrityType"] = *redfishType;
    json["ComponentIntegrityTypeVersion"] = *typeVersion;
    if (*lastUpdated != 0)
    {
        json["LastUpdated"] = time_utils::getDateTimeUintMs(*lastUpdated);
    }
    return true;
}

} // namespace component_integrity_utils

inline void afterGetComponentTargetObject(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& targetPath, const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_ERROR(
            "ComponentIntegrity target is missing or has no supported "
            "inventory interface: {}",
            ec);
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [service, interfaces] : object)
    {
        static_cast<void>(service);
        std::optional<std::string> target =
            component_integrity_utils::targetComponentUri(targetPath,
                                                          interfaces);
        if (target)
        {
            asyncResp->res.jsonValue["TargetComponentURI"] = *target;
            return;
        }
    }

    BMCWEB_LOG_ERROR(
        "Unable to map ComponentIntegrity target to a Redfish URI");
    messages::internalError(asyncResp->res);
}

inline void afterGetComponentTarget(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperEndPoints& endpoints)
{
    if (ec)
    {
        BMCWEB_LOG_ERROR("Unable to read authenticating association: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }
    if (endpoints.size() != 1)
    {
        BMCWEB_LOG_ERROR("ComponentIntegrity has {} authenticating endpoints; "
                         "exactly one is required",
                         endpoints.size());
        messages::internalError(asyncResp->res);
        return;
    }

    dbus::utility::getDbusObject(
        endpoints.front(), component_integrity_utils::targetInterfaces,
        std::bind_front(afterGetComponentTargetObject, asyncResp,
                        endpoints.front()));
}

inline void afterGetComponentIntegrityProperties(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& objectPath, const boost::system::error_code& ec,
    const dbus::utility::DBusPropertiesMap& properties)
{
    if (ec || !component_integrity_utils::fillProperties(
                  asyncResp->res.jsonValue, properties))
    {
        BMCWEB_LOG_ERROR("Unable to read ComponentIntegrity properties: {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    dbus::utility::getAssociationEndPoints(
        objectPath + "/authenticating",
        std::bind_front(afterGetComponentTarget, asyncResp));
}

inline void getComponentIntegrityData(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const std::string& service,
    const std::string& objectPath)
{
    asyncResp->res.jsonValue["@odata.type"] =
        "#ComponentIntegrity.v1_4_0.ComponentIntegrity";
    asyncResp->res.jsonValue["@odata.id"] =
        boost::urls::format("/redfish/v1/ComponentIntegrity/{}", componentId);
    asyncResp->res.jsonValue["Id"] = componentId;
    asyncResp->res.jsonValue["Name"] = "Component Integrity " + componentId;

    dbus::utility::getAllProperties(
        service, objectPath,
        std::string(component_integrity_utils::componentIntegrityInterface),
        std::bind_front(afterGetComponentIntegrityProperties, asyncResp,
                        objectPath));
}

inline void afterGetComponentIntegrity(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId, const boost::system::error_code& ec,
    const dbus::utility::MapperGetSubTreeResponse& subtree)
{
    if (ec)
    {
        if (ec.value() == EBADR)
        {
            messages::resourceNotFound(asyncResp->res, "ComponentIntegrity",
                                       componentId);
            return;
        }
        BMCWEB_LOG_ERROR("Unable to enumerate ComponentIntegrity objects: {}",
                         ec);
        messages::internalError(asyncResp->res);
        return;
    }

    for (const auto& [objectPath, serviceMap] : subtree)
    {
        if (sdbusplus::object_path(objectPath).filename() != componentId)
        {
            continue;
        }
        for (const auto& [service, interfaces] : serviceMap)
        {
            if (std::ranges::find(
                    interfaces,
                    component_integrity_utils::componentIntegrityInterface) !=
                interfaces.end())
            {
                getComponentIntegrityData(asyncResp, componentId, service,
                                          objectPath);
                return;
            }
        }
    }

    messages::resourceNotFound(asyncResp->res, "ComponentIntegrity",
                               componentId);
}

inline void handleComponentIntegrityGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const std::string& componentId)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    dbus::utility::getSubTree(
        std::string(component_integrity_utils::componentIntegrityPath), 0,
        component_integrity_utils::componentIntegrityInterfaces,
        std::bind_front(afterGetComponentIntegrity, asyncResp, componentId));
}

inline void handleComponentIntegrityCollectionGet(
    App& app, const crow::Request& req,
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    if (!redfish::setUpRedfishRoute(app, req, asyncResp))
    {
        return;
    }

    asyncResp->res.jsonValue["@odata.type"] =
        "#ComponentIntegrityCollection.ComponentIntegrityCollection";
    asyncResp->res.jsonValue["@odata.id"] = "/redfish/v1/ComponentIntegrity";
    asyncResp->res.jsonValue["Name"] = "Component Integrity Collection";
    collection_util::getCollectionMembers(
        asyncResp, boost::urls::url("/redfish/v1/ComponentIntegrity"),
        component_integrity_utils::componentIntegrityInterfaces,
        std::string(component_integrity_utils::componentIntegrityPath));
}

inline void requestRoutesComponentIntegrity(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/ComponentIntegrity/<str>/")
        .privileges(redfish::privileges::getComponentIntegrity)
        .methods(boost::beast::http::verb::get)(
            std::bind_front(handleComponentIntegrityGet, std::ref(app)));
}

inline void requestRoutesComponentIntegrityCollection(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/ComponentIntegrity/")
        .privileges(redfish::privileges::getComponentIntegrityCollection)
        .methods(boost::beast::http::verb::get)(std::bind_front(
            handleComponentIntegrityCollectionGet, std::ref(app)));
}

} // namespace redfish
