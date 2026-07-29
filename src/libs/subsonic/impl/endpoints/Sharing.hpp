#pragma once

#include "SubsonicResponse.hpp"

namespace lms::api::subsonic
{
    class RequestContext;
    Response handleCreateShare(RequestContext& context);
    Response handleGetShares(RequestContext& context);
    Response handleUpdateShare(RequestContext& context);
    Response handleDeleteShare(RequestContext& context);
}
