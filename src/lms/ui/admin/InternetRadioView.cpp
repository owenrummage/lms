#include "InternetRadioView.hpp"

#include <Wt/WContainerWidget.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WText.h>

#include "core/http/UrlValidation.hpp"
#include "database/Session.hpp"
#include "database/objects/InternetRadioStation.hpp"

#include "LmsApplication.hpp"
#include "ModalManager.hpp"

namespace lms::ui
{
    namespace
    {
        bool validateStation(std::string_view name, std::string_view streamUrl, std::string_view homepageUrl)
        {
            if (name.empty())
            {
                LmsApp->notifyMsg(Notification::Type::Warning, Wt::WString::tr("Lms.Admin.InternetRadio.invalid-name"));
                return false;
            }
            if (!core::http::isValidUrl(streamUrl) || (!homepageUrl.empty() && !core::http::isValidUrl(homepageUrl)))
            {
                LmsApp->notifyMsg(Notification::Type::Warning, Wt::WString::tr("Lms.Admin.InternetRadio.invalid-url"));
                return false;
            }
            return true;
        }
    } // namespace

    InternetRadioView::InternetRadioView()
        : Wt::WTemplate{ Wt::WString::tr("Lms.Admin.InternetRadio.template") }
    {
        addFunction("tr", &Wt::WTemplate::Functions::tr);
        _nameEdit = bindNew<Wt::WLineEdit>("name");
        _streamUrlEdit = bindNew<Wt::WLineEdit>("stream-url");
        _homepageUrlEdit = bindNew<Wt::WLineEdit>("homepage-url");
        _nameEdit->setPlaceholderText(Wt::WString::tr("Lms.Admin.InternetRadio.name-placeholder"));
        _streamUrlEdit->setPlaceholderText(Wt::WString::tr("Lms.Admin.InternetRadio.stream-url-placeholder"));
        _homepageUrlEdit->setPlaceholderText(Wt::WString::tr("Lms.Admin.InternetRadio.homepage-url-placeholder"));

        auto* addBtn{ bindNew<Wt::WPushButton>("add-btn", Wt::WString::tr("Lms.Admin.InternetRadio.add")) };
        addBtn->clicked().connect(this, &InternetRadioView::addStation);
        _homepageUrlEdit->enterPressed().connect(this, &InternetRadioView::addStation);
        _stations = bindNew<Wt::WContainerWidget>("stations");

        wApp->internalPathChanged().connect(this, [this] { refreshView(); });
        refreshView();
    }

    void InternetRadioView::addStation()
    {
        const std::string name{ _nameEdit->text().toUTF8() };
        const std::string streamUrl{ _streamUrlEdit->text().toUTF8() };
        const std::string homepageUrl{ _homepageUrlEdit->text().toUTF8() };
        if (!validateStation(name, streamUrl, homepageUrl))
            return;

        {
            auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
            LmsApp->getDbSession().create<db::InternetRadioStation>(name, streamUrl, homepageUrl);
        }
        _nameEdit->setText({});
        _streamUrlEdit->setText({});
        _homepageUrlEdit->setText({});
        refreshView();
        LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.InternetRadio.added"));
    }

    void InternetRadioView::refreshView()
    {
        if (!wApp->internalPathMatches("/admin/internet-radio"))
            return;
        _stations->clear();
        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        db::InternetRadioStation::find(LmsApp->getDbSession(), [this](const auto& station) {
            auto* entry{ _stations->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.InternetRadio.template.entry")) };
            entry->bindString("name", std::string{ station->getName() }, Wt::TextFormat::Plain);
            entry->bindString("stream-url", std::string{ station->getStreamUrl() }, Wt::TextFormat::Plain);
            entry->bindString("homepage-url", station->getHomepageUrl().empty() ? Wt::WString::tr("Lms.Admin.InternetRadio.no-homepage") : Wt::WString::fromUTF8(std::string{ station->getHomepageUrl() }), Wt::TextFormat::Plain);
            const auto id{ station->getId() };
            entry->bindNew<Wt::WPushButton>("edit-btn", Wt::WString::tr("Lms.template.edit-btn"), Wt::TextFormat::XHTML)
                ->clicked().connect(this, [this, id] { showEditModal(id); });
            entry->bindNew<Wt::WPushButton>("delete-btn", Wt::WString::tr("Lms.template.trash-btn"), Wt::TextFormat::XHTML)
                ->clicked().connect(this, [this, id] { showDeleteModal(id); });
        });
        if (_stations->count() == 0)
            _stations->addNew<Wt::WText>(Wt::WString::tr("Lms.Admin.InternetRadio.empty"))->setStyleClass("text-body-secondary py-3");
    }

    void InternetRadioView::showEditModal(db::InternetRadioStationId stationId)
    {
        auto modal{ std::make_unique<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.InternetRadio.template.edit")) };
        modal->addFunction("tr", &Wt::WTemplate::Functions::tr);
        Wt::WTemplate* modalPtr{ modal.get() };
        auto* nameEdit{ modal->bindNew<Wt::WLineEdit>("name") };
        auto* streamEdit{ modal->bindNew<Wt::WLineEdit>("stream-url") };
        auto* homepageEdit{ modal->bindNew<Wt::WLineEdit>("homepage-url") };
        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            const auto station{ db::InternetRadioStation::find(LmsApp->getDbSession(), stationId) };
            if (!station)
                return;
            nameEdit->setText(Wt::WString::fromUTF8(std::string{ station->getName() }));
            streamEdit->setText(Wt::WString::fromUTF8(std::string{ station->getStreamUrl() }));
            homepageEdit->setText(Wt::WString::fromUTF8(std::string{ station->getHomepageUrl() }));
        }
        auto close{ [modalPtr] { LmsApp->getModalManager().dispose(modalPtr); } };
        modal->bindNew<Wt::WPushButton>("cancel-btn", Wt::WString::tr("Lms.cancel"))->clicked().connect(close);
        modal->bindNew<Wt::WPushButton>("save-btn", Wt::WString::tr("Lms.save"))->clicked().connect(this, [this, stationId, nameEdit, streamEdit, homepageEdit, modalPtr] {
            const std::string name{ nameEdit->text().toUTF8() };
            const std::string streamUrl{ streamEdit->text().toUTF8() };
            const std::string homepageUrl{ homepageEdit->text().toUTF8() };
            if (!validateStation(name, streamUrl, homepageUrl))
                return;
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                auto station{ db::InternetRadioStation::find(LmsApp->getDbSession(), stationId) };
                if (!station)
                    return;
                station.modify()->setName(name);
                station.modify()->setStreamUrl(streamUrl);
                station.modify()->setHomepageUrl(homepageUrl);
            }
            LmsApp->getModalManager().dispose(modalPtr);
            refreshView();
            LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.InternetRadio.saved"));
        });
        LmsApp->getModalManager().show(std::move(modal));
    }

    void InternetRadioView::showDeleteModal(db::InternetRadioStationId stationId)
    {
        auto modal{ std::make_unique<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.InternetRadio.template.delete")) };
        modal->addFunction("tr", &Wt::WTemplate::Functions::tr);
        Wt::WTemplate* modalPtr{ modal.get() };
        modal->bindNew<Wt::WPushButton>("cancel-btn", Wt::WString::tr("Lms.cancel"))->clicked().connect([modalPtr] { LmsApp->getModalManager().dispose(modalPtr); });
        modal->bindNew<Wt::WPushButton>("delete-btn", Wt::WString::tr("Lms.delete"))->clicked().connect(this, [this, stationId, modalPtr] {
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                auto station{ db::InternetRadioStation::find(LmsApp->getDbSession(), stationId) };
                if (station)
                    station.remove();
            }
            LmsApp->getModalManager().dispose(modalPtr);
            refreshView();
            LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.InternetRadio.deleted"));
        });
        LmsApp->getModalManager().show(std::move(modal));
    }
} // namespace lms::ui
