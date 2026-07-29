/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#include "RadioView.hpp"

#include <Wt/WAnchor.h>
#include <Wt/WPushButton.h>
#include <Wt/WTemplate.h>
#include <Wt/WText.h>

#include "database/Session.hpp"
#include "database/objects/InternetRadioStation.hpp"

#include "LmsApplication.hpp"
#include "MediaPlayer.hpp"

namespace lms::ui
{
    RadioView::RadioView()
    {
        auto* page{ addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Radio.template")) };
        page->addFunction("tr", &Wt::WTemplate::Functions::tr);
        _stations = page->bindNew<Wt::WContainerWidget>("stations");

        wApp->internalPathChanged().connect(this, [this] { refreshView(); });
        refreshView();
    }

    void RadioView::refreshView()
    {
        if (!wApp->internalPathMatches("/radio"))
            return;

        _stations->clear();
        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        db::InternetRadioStation::find(LmsApp->getDbSession(), [this](const db::InternetRadioStation::pointer& station) {
            auto* entry{ _stations->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Radio.template.station")) };
            entry->bindString("name", std::string{ station->getName() }, Wt::TextFormat::Plain);

            if (!station->getHomepageUrl().empty())
            {
                auto* homepage{ entry->bindNew<Wt::WAnchor>("homepage", Wt::WLink{ std::string{ station->getHomepageUrl() } }, Wt::WString::tr("Lms.Radio.homepage")) };
                homepage->setAttributeValue("target", "_blank");
                homepage->setAttributeValue("rel", "noopener noreferrer");
            }
            else
                entry->bindEmpty("homepage");

            const db::InternetRadioStationId stationId{ station->getId() };
            auto* playBtn{ entry->bindNew<Wt::WPushButton>("play-btn", Wt::WString::tr("Lms.Radio.play"), Wt::TextFormat::XHTML) };
            playBtn->setToolTip(Wt::WString::tr("Lms.Radio.play-station").arg(Wt::WString::fromUTF8(std::string{ station->getName() })));
            playBtn->clicked().connect([stationId] { LmsApp->getMediaPlayer().loadInternetRadio(stationId); });
        });

        if (_stations->count() == 0)
            _stations->addNew<Wt::WText>(Wt::WString::tr("Lms.Radio.empty"))->setStyleClass("text-body-secondary py-3");
    }
} // namespace lms::ui
