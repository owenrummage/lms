/*
 * Copyright (C) 2025 Emeric Poupon
 *
 * This file is part of LMS.
 *
 * LMS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LMS is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LMS.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "RequestContext.hpp"

#include "ParameterParsing.hpp"
#include "SubsonicResourceConfig.hpp"
#include "SubsonicResponse.hpp"
#include "database/objects/MediaLibrary.hpp"
#include "database/objects/User.hpp"
#include "core/IConfig.hpp"
#include "core/Service.hpp"

namespace lms::api::subsonic
{
    namespace
    {
        void checkProtocolVersion(ProtocolVersion client, ProtocolVersion server)
        {
            if (client.major > server.major)
                throw ServerMustUpgradeError{};
            if (client.major < server.major)
                throw ClientMustUpgradeError{};
            if (client.minor > server.minor)
                throw ServerMustUpgradeError{};
            if (client.minor == server.minor)
            {
                if (client.patch > server.patch)
                    throw ServerMustUpgradeError{};
            }
        }
    } // namespace

    RequestContext::RequestContext(const Wt::Http::Request& request, db::Session& dbSession, const SubsonicResourceConfig& config)
        : _request{ request }
        , _dbSession{ dbSession }
        , _config{ config }
        , _clientName{ getMandatoryParameterAs<std::string>(_request.getParameterMap(), "c") }
        , _clientProtocolVersion{ getMandatoryParameterAs<ProtocolVersion>(_request.getParameterMap(), "v") }
        , _responseFormat{ getParameterAs<std::string>(request.getParameterMap(), "f").value_or("xml") == "json" ? ResponseFormat::json : ResponseFormat::xml }
        , _isOpenSubsonicEnabled{ !_config.openSubsonicDisabledClients.contains(_clientName) }
    {
        checkProtocolVersion(_clientProtocolVersion, serverProtocolVersion);
    }

    std::string RequestContext::getPublicBaseUrl() const
    {
        std::string publicUrl{ core::Service<core::IConfig>::get()->getString("public-url", "") };
        while (publicUrl.ends_with('/'))
            publicUrl.pop_back();
        if (!publicUrl.empty())
            return publicUrl;

        std::string scheme{ _request.headerValue("X-Forwarded-Proto") };
        if (scheme.empty())
            scheme = _request.urlScheme();
        std::string host{ _request.headerValue("X-Forwarded-Host") };
        if (host.empty())
            host = _request.hostName();
        if (host.find(':') == std::string::npos && !_request.serverPort().empty()
            && !((scheme == "http" && _request.serverPort() == "80") || (scheme == "https" && _request.serverPort() == "443")))
            host += ":" + _request.serverPort();
        return scheme + "://" + host;
    }

    RequestContext::~RequestContext() = default;

    const RequestContext::ParameterMap& RequestContext::getParameters() const
    {
        return _request.getParameterMap();
    }

    std::istream& RequestContext::getBody() const
    {
        return _request.in();
    }

    db::Session& RequestContext::getDbSession()
    {
        return _dbSession;
    }

    void RequestContext::setUser(const db::ObjectPtr<db::User>& user)
    {
        _user = user;
    }

    db::ObjectPtr<db::User> RequestContext::getUser() const
    {
        return _user;
    }

    bool RequestContext::isMediaLibraryAllowed(db::MediaLibraryId libraryId) const
    {
        return _user && libraryId.isValid() && _user->hasMediaLibrary(libraryId);
    }

    void RequestContext::applyUserLibraryFilter(db::Filters& filters, db::MediaLibraryId requestedLibrary) const
    {
        if (requestedLibrary.isValid())
        {
            if (!isMediaLibraryAllowed(requestedLibrary))
                throw RequestedDataNotFoundError{};
            filters.setMediaLibrary(requestedLibrary);
            return;
        }

        std::vector<db::MediaLibraryId> libraryIds;
        if (_user)
        {
            for (const db::MediaLibrary::pointer& library : _user->getMediaLibraries())
                libraryIds.push_back(library->getId());
        }
        filters.setMediaLibraries(std::move(libraryIds));
    }

    std::string RequestContext::getClientIpAddr() const
    {
        return _request.clientAddress();
    }

    std::string_view RequestContext::getClientName() const
    {
        return _clientName;
    }

    ResponseFormat RequestContext::getResponseFormat() const
    {
        return _responseFormat;
    }

    bool RequestContext::isOpenSubsonicEnabled() const
    {
        return _isOpenSubsonicEnabled;
    }
} // namespace lms::api::subsonic
