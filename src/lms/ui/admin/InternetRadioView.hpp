#pragma once

#include <Wt/WTemplate.h>

#include "database/objects/InternetRadioStationId.hpp"

namespace Wt
{
    class WContainerWidget;
    class WLineEdit;
}

namespace lms::ui
{
    class InternetRadioView final : public Wt::WTemplate
    {
    public:
        InternetRadioView();

    private:
        void addStation();
        void refreshView();
        void showEditModal(db::InternetRadioStationId stationId);
        void showDeleteModal(db::InternetRadioStationId stationId);

        Wt::WLineEdit* _nameEdit{};
        Wt::WLineEdit* _streamUrlEdit{};
        Wt::WLineEdit* _homepageUrlEdit{};
        Wt::WContainerWidget* _stations{};
    };
} // namespace lms::ui
