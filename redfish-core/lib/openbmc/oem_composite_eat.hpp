// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors
#pragma once

#include "app.hpp"
#include "async_resp.hpp"
#include "dbus_utility.hpp"
#include "error_messages.hpp"
#include "http/utility.hpp"
#include "logging.hpp"
#include "query.hpp"
#include "registries/privilege_registry.hpp"
#include "utils/json_utils.hpp"

#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cerrno>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace redfish
{
namespace composite_eat_utils
{

inline constexpr std::string_view service = "xyz.openbmc_project.SPDM";
inline constexpr std::string_view objectPath =
    "/xyz/openbmc_project/SPDM/CompositeEATBundle";
inline constexpr std::string_view interface =
    "xyz.openbmc_project.SPDM.CompositeEATBundle";
inline constexpr std::string_view actionUri =
    "/redfish/v1/ComponentIntegrity/Actions/Oem/"
    "OpenBMC.GetCompositeEATBundle";
inline constexpr std::string_view resultUri =
    "/redfish/v1/ComponentIntegrity/CompositeEATBundle";
inline constexpr std::array<std::string_view, 1> interfaces = {interface};
using Property = std::variant<std::string, std::vector<std::uint8_t>>;
using Properties = std::vector<std::pair<std::string, Property>>;

enum class Status
{
    idle,
    inProgress,
    ready,
    error,
};

enum class Result
{
    valid,
    producerError,
    invalid,
};

inline std::optional<Status> parseStatus(std::string_view status)
{
    if (status == "Idle")
    {
        return Status::idle;
    }
    if (status == "InProgress")
    {
        return Status::inProgress;
    }
    if (status == "Ready")
    {
        return Status::ready;
    }
    if (status == "Error")
    {
        return Status::error;
    }
    return std::nullopt;
}

inline bool decodeNonce(std::string_view encoded,
                        std::vector<std::uint8_t>& nonce)
{
    std::string decoded;
    if (!crow::utility::base64Decode(encoded, decoded) || decoded.size() != 32)
    {
        return false;
    }
    nonce.assign(decoded.begin(), decoded.end());
    return true;
}

inline void setResultMetadata(nlohmann::json& json, std::string_view status)
{
    json["@odata.type"] =
        "#OpenBMCCompositeEATBundle.v1_0_0.CompositeEATBundle";
    json["@odata.id"] = resultUri;
    json["Id"] = "CompositeEATBundle";
    json["Name"] = "Platform Composite EAT Bundle";
    json["Status"] = status;
}

inline Result fillResult(nlohmann::json& json, const Properties& properties)
{
    if (!json.is_object())
    {
        json = nlohmann::json::object();
    }

    const std::string* status = nullptr;
    const std::vector<std::uint8_t>* bundle = nullptr;
    for (const auto& [name, value] : properties)
    {
        if (name == "Status")
        {
            status = std::get_if<std::string>(&value);
        }
        else if (name == "Bundle")
        {
            bundle = std::get_if<std::vector<std::uint8_t>>(&value);
        }
    }

    if (status == nullptr)
    {
        return Result::invalid;
    }
    std::optional<Status> parsedStatus = parseStatus(*status);
    if (!parsedStatus)
    {
        return Result::invalid;
    }

    json.erase("CompositeEATBundle");
    if (*parsedStatus == Status::error)
    {
        return Result::producerError;
    }

    setResultMetadata(json, *status);
    if (*parsedStatus == Status::ready)
    {
        if (bundle == nullptr || bundle->empty())
        {
            return Result::invalid;
        }
        json["CompositeEATBundle"] = crow::utility::base64encode(
            std::string(bundle->begin(), bundle->end()));
    }
    return Result::valid;
}

inline void afterFindProducer(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
    const boost::system::error_code& ec,
    const dbus::utility::MapperGetObject& object)
{
    if (ec || object.empty())
    {
        BMCWEB_LOG_DEBUG("Composite EAT producer is unavailable");
        return;
    }

    nlohmann::json& openBmc = asyncResp->res.jsonValue["Oem"]["OpenBMC"];
    openBmc["@odata.type"] =
        "#OpenBMCCompositeEATBundle.v1_0_0.ComponentIntegrityCollection";
    openBmc["CompositeEATBundle"]["@odata.id"] = resultUri;
    openBmc["Actions"]["#OpenBMCCompositeEATBundle.GetCompositeEATBundle"]
           ["target"] = actionUri;
}

inline void addCollectionExtension(
    const std::shared_ptr<bmcweb::AsyncResp>& asyncResp)
{
    dbus::utility::getDbusObject(std::string(objectPath), interfaces,
                                 std::bind_front(afterFindProducer, asyncResp));
}

inline void afterGenerate(const std::shared_ptr<bmcweb::AsyncResp>& asyncResp,
                          const boost::system::error_code& ec)
{
    if (ec.value() == EBUSY)
    {
        asyncResp->res.result(boost::beast::http::status::service_unavailable);
        asyncResp->res.addHeader(boost::beast::http::field::retry_after, "5");
        messages::serviceTemporarilyUnavailable(asyncResp->res, "5");
        return;
    }
    if (ec)
    {
        BMCWEB_LOG_ERROR("Composite EAT generation failed: {}", ec);
        messages::internalError(asyncResp->res);
        return;
    }

    asyncResp->res.result(boost::beast::http::status::accepted);
    asyncResp->res.addHeader(boost::beast::http::field::location, resultUri);
}

} // namespace composite_eat_utils

inline void requestRoutesCompositeEatBundleAction(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/ComponentIntegrity/Actions/Oem/"
                      "OpenBMC.GetCompositeEATBundle")
        .privileges(redfish::privileges::postComponentIntegrityCollection)
        .methods(boost::beast::http::verb::post)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                std::string encodedNonce;
                if (!json_util::readJsonAction(req, asyncResp->res, "Nonce",
                                               encodedNonce))
                {
                    return;
                }

                std::vector<std::uint8_t> nonce;
                if (!composite_eat_utils::decodeNonce(encodedNonce, nonce))
                {
                    messages::actionParameterValueFormatError(
                        asyncResp->res, encodedNonce, "Nonce",
                        "OpenBMC.GetCompositeEATBundle");
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp](const boost::system::error_code& ec) {
                        composite_eat_utils::afterGenerate(asyncResp, ec);
                    },
                    std::string(composite_eat_utils::service),
                    std::string(composite_eat_utils::objectPath),
                    std::string(composite_eat_utils::interface), "Generate",
                    nonce);
            });
}

inline void requestRoutesCompositeEatBundleResult(App& app)
{
    BMCWEB_ROUTE(app, "/redfish/v1/ComponentIntegrity/CompositeEATBundle/")
        .privileges(redfish::privileges::getComponentIntegrityCollection)
        .methods(boost::beast::http::verb::get)(
            [&app](const crow::Request& req,
                   const std::shared_ptr<bmcweb::AsyncResp>& asyncResp) {
                if (!redfish::setUpRedfishRoute(app, req, asyncResp))
                {
                    return;
                }

                crow::connections::systemBus->async_method_call(
                    [asyncResp](
                        const boost::system::error_code& ec,
                        const composite_eat_utils::Properties& properties) {
                        if (ec)
                        {
                            messages::resourceNotFound(asyncResp->res,
                                                       "CompositeEATBundle",
                                                       "CompositeEATBundle");
                            return;
                        }

                        composite_eat_utils::Result result =
                            composite_eat_utils::fillResult(
                                asyncResp->res.jsonValue, properties);
                        if (result ==
                            composite_eat_utils::Result::producerError)
                        {
                            messages::internalError(asyncResp->res);
                            return;
                        }
                        if (result == composite_eat_utils::Result::invalid)
                        {
                            BMCWEB_LOG_ERROR(
                                "Invalid Composite EAT producer properties");
                            messages::internalError(asyncResp->res);
                        }
                    },
                    std::string(composite_eat_utils::service),
                    std::string(composite_eat_utils::objectPath),
                    "org.freedesktop.DBus.Properties", "GetAll",
                    std::string(composite_eat_utils::interface));
            });
}

} // namespace redfish
