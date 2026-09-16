// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: Copyright OpenBMC Authors

#include "openbmc/oem_composite_eat.hpp"

#include <boost/beast/http/status.hpp>
#include <boost/system/errc.hpp>
#include <boost/system/error_code.hpp>

#include <gtest/gtest.h>

namespace redfish::composite_eat_utils
{
namespace
{

TEST(CompositeEatNonce, DecodesExactly32Bytes)
{
    std::vector<std::uint8_t> nonce;
    EXPECT_TRUE(
        decodeNonce("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=", nonce));
    ASSERT_EQ(nonce.size(), 32U);
    EXPECT_EQ(nonce.front(), 0U);
    EXPECT_EQ(nonce.back(), 31U);
}

TEST(CompositeEatNonce, RejectsMalformedOrWrongLength)
{
    std::vector<std::uint8_t> nonce;
    EXPECT_FALSE(decodeNonce("not-base64", nonce));
    EXPECT_FALSE(decodeNonce("AAECAwQFBgc=", nonce));
    EXPECT_FALSE(
        decodeNonce("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8", nonce));
    EXPECT_FALSE(decodeNonce(
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=trailing", nonce));
    EXPECT_FALSE(
        decodeNonce("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh_=", nonce));
}

TEST(CompositeEatStatus, AcceptsOnlyContractStates)
{
    EXPECT_EQ(parseStatus("Idle"), Status::idle);
    EXPECT_EQ(parseStatus("InProgress"), Status::inProgress);
    EXPECT_EQ(parseStatus("Ready"), Status::ready);
    EXPECT_EQ(parseStatus("Error"), Status::error);
    EXPECT_EQ(parseStatus("Success"), std::nullopt);
}

TEST(CompositeEatResult, SetsOperationalEnvelope)
{
    nlohmann::json json;
    setResultMetadata(json, "Ready");

    EXPECT_EQ(json["@odata.type"],
              "#OpenBMCCompositeEATBundle.v1_0_0.CompositeEATBundle");
    EXPECT_EQ(json["@odata.id"], resultUri);
    EXPECT_EQ(json["Id"], "CompositeEATBundle");
    EXPECT_EQ(json["Status"], "Ready");
    EXPECT_FALSE(json.contains("CompositeEATBundle"));
}

TEST(CompositeEatResult, HandlesContractStates)
{
    nlohmann::json idle;
    EXPECT_EQ(fillResult(idle, {{"Status", std::string("Idle")},
                                {"Bundle", std::vector<std::uint8_t>{1, 2}}}),
              Result::valid);
    EXPECT_EQ(idle["Status"], "Idle");
    EXPECT_FALSE(idle.contains("CompositeEATBundle"));

    nlohmann::json inProgress;
    EXPECT_EQ(fillResult(inProgress,
                         {{"Status", std::string("InProgress")},
                          {"Bundle", std::vector<std::uint8_t>{1, 2}}}),
              Result::valid);
    EXPECT_FALSE(inProgress.contains("CompositeEATBundle"));

    nlohmann::json ready;
    EXPECT_EQ(fillResult(ready, {{"Status", std::string("Ready")},
                                 {"Bundle", std::vector<std::uint8_t>{1, 2}}}),
              Result::valid);
    EXPECT_EQ(ready["CompositeEATBundle"], "AQI=");
}

TEST(CompositeEatResult, FailsClosedForErrorOrInvalidProperties)
{
    nlohmann::json error = {{"CompositeEATBundle", "stale"}};
    EXPECT_EQ(fillResult(error, {{"Status", std::string("Error")},
                                 {"Bundle", std::vector<std::uint8_t>{1, 2}}}),
              Result::producerError);
    EXPECT_FALSE(error.contains("CompositeEATBundle"));

    nlohmann::json invalid;
    EXPECT_EQ(fillResult(invalid, {}), Result::invalid);
    EXPECT_EQ(fillResult(invalid,
                         {{"Status", std::string("Unknown")},
                          {"Bundle", std::vector<std::uint8_t>{1, 2}}}),
              Result::invalid);
    EXPECT_EQ(fillResult(invalid, {{"Status", std::string("Ready")},
                                   {"Bundle", std::vector<std::uint8_t>{}}}),
              Result::invalid);
}

TEST(CompositeEatAction, MapsGenerateCompletionToHttpResponse)
{
    EXPECT_EQ(actionUri,
              "/redfish/v1/ComponentIntegrity/Actions/Oem/"
              "OpenBMCCompositeEATBundle.Generate");

    auto accepted = std::make_shared<bmcweb::AsyncResp>();
    afterGenerate(accepted, {});
    EXPECT_EQ(accepted->res.result(), boost::beast::http::status::accepted);
    EXPECT_EQ(accepted->res.getHeaderValue("Location"), resultUri);

    auto busy = std::make_shared<bmcweb::AsyncResp>();
    afterGenerate(busy, {EBUSY, boost::system::generic_category()});
    EXPECT_EQ(busy->res.result(),
              boost::beast::http::status::service_unavailable);
    EXPECT_EQ(busy->res.getHeaderValue("Retry-After"), "5");

    auto failed = std::make_shared<bmcweb::AsyncResp>();
    afterGenerate(failed, make_error_code(boost::system::errc::io_error));
    EXPECT_EQ(failed->res.result(),
              boost::beast::http::status::internal_server_error);
}

TEST(CompositeEatCollection, AdvertisesOnlyAnAvailableProducer)
{
    auto absent = std::make_shared<bmcweb::AsyncResp>();
    afterFindProducer(absent, {}, {});
    EXPECT_FALSE(absent->res.jsonValue.contains("Oem"));

    auto wrongService = std::make_shared<bmcweb::AsyncResp>();
    const dbus::utility::MapperGetObject wrongServiceObject = {
        {"xyz.openbmc_project.AttestationProvider", {std::string(interface)}}};
    afterFindProducer(wrongService, {}, wrongServiceObject);
    EXPECT_FALSE(wrongService->res.jsonValue.contains("Oem"));

    auto available = std::make_shared<bmcweb::AsyncResp>();
    const dbus::utility::MapperGetObject object = {
        {std::string(service), {std::string(interface)}}};
    afterFindProducer(available, {}, object);
    const nlohmann::json& openBmc = available->res.jsonValue["Oem"]["OpenBMC"];
    EXPECT_EQ(openBmc["CompositeEATBundle"]["@odata.id"], resultUri);
    EXPECT_EQ(
        openBmc["Actions"]["#OpenBMCCompositeEATBundle.Generate"]
               ["target"],
        actionUri);
}

TEST(CompositeEatResult, DistinguishesMissingResourceFromReadFailure)
{
    auto missing = std::make_shared<bmcweb::AsyncResp>();
    afterGetResult(missing, {EBADR, boost::system::generic_category()}, {});
    EXPECT_EQ(missing->res.result(), boost::beast::http::status::not_found);

    auto failed = std::make_shared<bmcweb::AsyncResp>();
    afterGetResult(failed, make_error_code(boost::system::errc::io_error), {});
    EXPECT_EQ(failed->res.result(),
              boost::beast::http::status::internal_server_error);
}

} // namespace
} // namespace redfish::composite_eat_utils
