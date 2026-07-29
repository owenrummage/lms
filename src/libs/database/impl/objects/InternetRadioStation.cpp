#include "database/objects/InternetRadioStation.hpp"

#include <Wt/Dbo/Impl.h>

#include "database/Session.hpp"
#include "Utils.hpp"
#include "traits/IdTypeTraits.hpp"
#include "traits/StringViewTraits.hpp"

DBO_INSTANTIATE_TEMPLATES(lms::db::InternetRadioStation)

namespace lms::db
{
    InternetRadioStation::InternetRadioStation(std::string_view name, std::string_view streamUrl, std::string_view homepageUrl)
        : _name{ name }, _streamUrl{ streamUrl }, _homepageUrl{ homepageUrl }
    {
    }

    InternetRadioStation::pointer InternetRadioStation::create(Session& session, std::string_view name, std::string_view streamUrl, std::string_view homepageUrl)
    {
        return session.getDboSession()->add(std::unique_ptr<InternetRadioStation>{ new InternetRadioStation{ name, streamUrl, homepageUrl } });
    }

    InternetRadioStation::pointer InternetRadioStation::find(Session& session, InternetRadioStationId id)
    {
        session.checkReadTransaction();
        return utils::fetchQuerySingleResult(session.getDboSession()->find<InternetRadioStation>().where("id = ?").bind(id));
    }

    void InternetRadioStation::find(Session& session, const std::function<void(const pointer&)>& visitor)
    {
        session.checkReadTransaction();
        utils::forEachQueryResult(session.getDboSession()->find<InternetRadioStation>().orderBy("name COLLATE NOCASE"), visitor);
    }
} // namespace lms::db
