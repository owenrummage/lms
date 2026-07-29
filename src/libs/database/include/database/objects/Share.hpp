#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <Wt/Dbo/Field.h>
#include <Wt/WDateTime.h>

#include "database/Object.hpp"
#include "database/objects/ShareId.hpp"
#include "database/objects/UserId.hpp"

namespace lms::db
{
    class Session;
    class User;

    class Share final : public Object<Share, ShareId>
    {
    public:
        Share() = default;

        static pointer find(Session& session, ShareId id);
        static pointer find(Session& session, std::string_view token);
        static void find(Session& session, UserId userId, const std::function<void(const pointer&)>& visitor);

        std::string_view getToken() const { return _token; }
        std::string_view getMediaIds() const { return _mediaIds; }
        std::string_view getDescription() const { return _description; }
        const Wt::WDateTime& getCreated() const { return _created; }
        const Wt::WDateTime& getExpires() const { return _expires; }
        const Wt::WDateTime& getLastVisited() const { return _lastVisited; }
        long getVisitCount() const { return _visitCount; }
        ObjectPtr<User> getUser() const { return _user; }

        void setDescription(std::string_view value) { _description = value; }
        void setExpires(const Wt::WDateTime& value) { _expires = value; }
        void setLastVisited(const Wt::WDateTime& value) { _lastVisited = value; }
        void incrementVisitCount() { ++_visitCount; }

        template<class Action>
        void persist(Action& a)
        {
            Wt::Dbo::field(a, _token, "token");
            Wt::Dbo::field(a, _mediaIds, "media_ids");
            Wt::Dbo::field(a, _description, "description");
            Wt::Dbo::field(a, _created, "created");
            Wt::Dbo::field(a, _expires, "expires");
            Wt::Dbo::field(a, _lastVisited, "last_visited");
            Wt::Dbo::field(a, _visitCount, "visit_count");
            Wt::Dbo::belongsTo(a, _user, "user", Wt::Dbo::OnDeleteCascade);
        }

    private:
        friend class Session;
        Share(std::string_view token, std::string_view mediaIds, ObjectPtr<User> user);
        static pointer create(Session& session, std::string_view token, std::string_view mediaIds, ObjectPtr<User> user);

        std::string _token;
        std::string _mediaIds;
        std::string _description;
        Wt::WDateTime _created;
        Wt::WDateTime _expires;
        Wt::WDateTime _lastVisited;
        long _visitCount{};
        Wt::Dbo::ptr<User> _user;
    };
}
