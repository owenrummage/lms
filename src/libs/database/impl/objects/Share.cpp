#include "database/objects/Share.hpp"

#include <Wt/Dbo/Impl.h>
#include <Wt/Dbo/WtSqlTraits.h>

#include "database/Session.hpp"
#include "database/objects/User.hpp"
#include "Utils.hpp"
#include "traits/IdTypeTraits.hpp"
#include "traits/StringViewTraits.hpp"

DBO_INSTANTIATE_TEMPLATES(lms::db::Share)

namespace lms::db
{
    Share::Share(std::string_view token, std::string_view mediaIds, ObjectPtr<User> user)
        : _token{ token }, _mediaIds{ mediaIds }, _created{ Wt::WDateTime::currentDateTime() }, _user{ getDboPtr(user) }
    {
    }

    Share::pointer Share::create(Session& session, std::string_view token, std::string_view mediaIds, ObjectPtr<User> user)
    {
        return session.getDboSession()->add(std::unique_ptr<Share>{ new Share{ token, mediaIds, user } });
    }

    Share::pointer Share::find(Session& session, ShareId id)
    {
        session.checkReadTransaction();
        return utils::fetchQuerySingleResult(session.getDboSession()->query<Wt::Dbo::ptr<Share>>("SELECT s from share s").where("s.id = ?").bind(id));
    }

    Share::pointer Share::find(Session& session, std::string_view token)
    {
        session.checkReadTransaction();
        pointer result;
        utils::forEachQueryResult(session.getDboSession()->find<Share>(), [&](const Wt::Dbo::ptr<Share>& share) {
            if (!result && share->getToken() == token)
                result = share;
        });
        return result;
    }

    void Share::find(Session& session, UserId userId, const std::function<void(const pointer&)>& visitor)
    {
        session.checkReadTransaction();
        utils::forEachQueryResult(session.getDboSession()->find<Share>().where("user_id = ?").bind(userId).orderBy("created DESC"), visitor);
    }
}
