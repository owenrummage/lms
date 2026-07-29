#pragma once

#include <Wt/WResource.h>

namespace lms::db { class IDb; }

namespace lms
{
    class PublicShareResource final : public Wt::WResource
    {
    public:
        explicit PublicShareResource(db::IDb& db) : _db{ db } {}
        ~PublicShareResource() override { beingDeleted(); }

    private:
        void handleRequest(const Wt::Http::Request& request, Wt::Http::Response& response) override;
        db::IDb& _db;
    };
}
