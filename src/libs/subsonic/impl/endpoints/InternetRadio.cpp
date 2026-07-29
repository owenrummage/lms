#include "InternetRadio.hpp"

#include "core/String.hpp"
#include "core/http/UrlValidation.hpp"
#include "database/Session.hpp"
#include "database/objects/InternetRadioStation.hpp"

#include "ParameterParsing.hpp"

namespace lms::api::subsonic
{
    namespace
    {
        db::InternetRadioStationId parseId(RequestContext& context)
        {
            const auto rawId{ getMandatoryParameterAs<std::string>(context.getParameters(), "id") };
            const auto value{ core::stringUtils::readAs<db::InternetRadioStationId::ValueType>(rawId) };
            if (!value)
                throw RequestedDataNotFoundError{};
            return db::InternetRadioStationId{ *value };
        }

        db::InternetRadioStation::pointer findStation(RequestContext& context)
        {
            auto station{ db::InternetRadioStation::find(context.getDbSession(), parseId(context)) };
            if (!station)
                throw RequestedDataNotFoundError{};
            return station;
        }

        void validateUrl(std::string_view value, std::string_view parameter, bool allowEmpty = false)
        {
            if ((value.empty() && !allowEmpty) || (!value.empty() && !core::http::isValidUrl(value)))
                throw BadParameterGenericError{ parameter, "must be a valid absolute HTTP or HTTPS URL" };
        }
    } // namespace

    Response handleGetInternetRadioStations(RequestContext& context)
    {
        Response response{ Response::createOkResponse() };
        auto& stationsNode{ response.createNode("internetRadioStations") };
        stationsNode.createEmptyArrayChild("internetRadioStation");

        auto transaction{ context.getDbSession().createReadTransaction() };
        db::InternetRadioStation::find(context.getDbSession(), [&](const auto& station) {
            Response::Node node;
            node.setAttribute("id", station->getId().toString());
            node.setAttribute("name", station->getName());
            node.setAttribute("streamUrl", station->getStreamUrl());
            if (!station->getHomepageUrl().empty())
                node.setAttribute("homepageUrl", station->getHomepageUrl());
            stationsNode.addArrayChild("internetRadioStation", std::move(node));
        });
        return response;
    }

    Response handleCreateInternetRadioStation(RequestContext& context)
    {
        const auto name{ getMandatoryParameterAs<std::string>(context.getParameters(), "name") };
        const auto streamUrl{ getMandatoryParameterAs<std::string>(context.getParameters(), "streamUrl") };
        const auto homepageUrl{ getParameterAs<std::string>(context.getParameters(), "homepageUrl").value_or("") };
        if (name.empty())
            throw BadParameterGenericError{ "name", "must not be empty" };
        validateUrl(streamUrl, "streamUrl");
        validateUrl(homepageUrl, "homepageUrl", true);

        auto transaction{ context.getDbSession().createWriteTransaction() };
        context.getDbSession().create<db::InternetRadioStation>(name, streamUrl, homepageUrl);
        return Response::createOkResponse();
    }

    Response handleUpdateInternetRadioStation(RequestContext& context)
    {
        auto transaction{ context.getDbSession().createWriteTransaction() };
        auto station{ findStation(context) };
        if (const auto name{ getParameterAs<std::string>(context.getParameters(), "name") })
        {
            if (name->empty())
                throw BadParameterGenericError{ "name", "must not be empty" };
            station.modify()->setName(*name);
        }
        if (const auto streamUrl{ getParameterAs<std::string>(context.getParameters(), "streamUrl") })
        {
            validateUrl(*streamUrl, "streamUrl");
            station.modify()->setStreamUrl(*streamUrl);
        }
        if (const auto homepageUrl{ getParameterAs<std::string>(context.getParameters(), "homepageUrl") })
        {
            validateUrl(*homepageUrl, "homepageUrl", true);
            station.modify()->setHomepageUrl(*homepageUrl);
        }
        return Response::createOkResponse();
    }

    Response handleDeleteInternetRadioStation(RequestContext& context)
    {
        auto transaction{ context.getDbSession().createWriteTransaction() };
        findStation(context).remove();
        return Response::createOkResponse();
    }
} // namespace lms::api::subsonic
