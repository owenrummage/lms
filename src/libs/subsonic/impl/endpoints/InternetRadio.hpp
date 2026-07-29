#pragma once

#include "RequestContext.hpp"
#include "SubsonicResponse.hpp"

namespace lms::api::subsonic
{
    Response handleGetInternetRadioStations(RequestContext& context);
    Response handleCreateInternetRadioStation(RequestContext& context);
    Response handleUpdateInternetRadioStation(RequestContext& context);
    Response handleDeleteInternetRadioStation(RequestContext& context);
} // namespace lms::api::subsonic
