#pragma once

#include <functional>
#include <string>
#include <string_view>

#include <Wt/Dbo/Field.h>

#include "database/Object.hpp"
#include "database/objects/InternetRadioStationId.hpp"

namespace lms::db
{
    class Session;

    class InternetRadioStation final : public Object<InternetRadioStation, InternetRadioStationId>
    {
    public:
        InternetRadioStation() = default;

        static pointer find(Session& session, InternetRadioStationId id);
        static void find(Session& session, const std::function<void(const pointer&)>& visitor);

        std::string_view getName() const { return _name; }
        std::string_view getStreamUrl() const { return _streamUrl; }
        std::string_view getHomepageUrl() const { return _homepageUrl; }

        void setName(std::string_view value) { _name = value; }
        void setStreamUrl(std::string_view value) { _streamUrl = value; }
        void setHomepageUrl(std::string_view value) { _homepageUrl = value; }

        template<class Action>
        void persist(Action& a)
        {
            Wt::Dbo::field(a, _name, "name");
            Wt::Dbo::field(a, _streamUrl, "stream_url");
            Wt::Dbo::field(a, _homepageUrl, "homepage_url");
        }

    private:
        friend class Session;
        InternetRadioStation(std::string_view name, std::string_view streamUrl, std::string_view homepageUrl);
        static pointer create(Session& session, std::string_view name, std::string_view streamUrl, std::string_view homepageUrl);

        std::string _name;
        std::string _streamUrl;
        std::string _homepageUrl;
    };
} // namespace lms::db
