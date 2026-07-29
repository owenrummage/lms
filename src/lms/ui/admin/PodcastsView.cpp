/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#include "PodcastsView.hpp"

#include <Wt/WContainerWidget.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WText.h>

#include <vector>

#include "core/Service.hpp"
#include "core/http/UrlValidation.hpp"
#include "database/Session.hpp"
#include "database/objects/Podcast.hpp"
#include "database/objects/PodcastEpisode.hpp"
#include "services/podcast/IPodcastService.hpp"

#include "LmsApplication.hpp"
#include "ModalManager.hpp"

namespace lms::ui
{
    namespace
    {
        Wt::WString getEpisodeStatus(const db::PodcastEpisode::pointer& episode)
        {
            if (episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DeleteRequested)
                return Wt::WString::tr("Lms.Podcasts.status.deleting");
            if (!episode->getAudioRelativeFilePath().empty())
                return Wt::WString::tr("Lms.Podcasts.status.downloaded");
            if (episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DownloadRequested)
                return Wt::WString::tr("Lms.Podcasts.status.downloading");
            return Wt::WString::tr("Lms.Podcasts.status.not-downloaded");
        }

        std::string getEpisodeBadgeClass(const db::PodcastEpisode::pointer& episode)
        {
            if (!episode->getAudioRelativeFilePath().empty())
                return "badge text-bg-success";
            if (episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DownloadRequested)
                return "badge text-bg-info";
            if (episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DeleteRequested)
                return "badge text-bg-warning";
            return "badge text-bg-secondary";
        }
    } // namespace

    PodcastSettingsView::PodcastSettingsView()
        : Wt::WTemplate{ Wt::WString::tr("Lms.Admin.Podcasts.template") }
    {
        addFunction("tr", &Wt::WTemplate::Functions::tr);

        _urlEdit = bindNew<Wt::WLineEdit>("url");
        _urlEdit->setPlaceholderText(Wt::WString::tr("Lms.Admin.Podcasts.feed-url-placeholder"));
        _urlEdit->enterPressed().connect(this, &PodcastSettingsView::addPodcast);

        auto* addBtn{ bindNew<Wt::WPushButton>("add-btn", Wt::WString::tr("Lms.Admin.Podcasts.add-feed")) };
        addBtn->clicked().connect(this, &PodcastSettingsView::addPodcast);

        auto* refreshBtn{ bindNew<Wt::WPushButton>("refresh-btn", Wt::WString::tr("Lms.Admin.Podcasts.refresh-now")) };
        refreshBtn->clicked().connect(this, [] {
            core::Service<podcast::IPodcastService>::get()->refreshPodcasts();
            LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.Podcasts.refresh-started"));
        });

        _podcasts = bindNew<Wt::WContainerWidget>("podcasts");

        wApp->internalPathChanged().connect(this, [this] { refreshView(); });
        refreshView();
    }

    void PodcastSettingsView::addPodcast()
    {
        const std::string url{ _urlEdit->text().toUTF8() };
        if (!core::http::isValidUrl(url))
        {
            LmsApp->notifyMsg(Notification::Type::Warning, Wt::WString::tr("Lms.Admin.Podcasts.invalid-url"));
            return;
        }

        core::Service<podcast::IPodcastService>::get()->addPodcast(url);
        _urlEdit->setText({});
        refreshView();
        LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.Podcasts.feed-added"));
    }

    void PodcastSettingsView::refreshView()
    {
        if (!wApp->internalPathMatches("/admin/podcasts"))
            return;

        _podcasts->clear();

        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        db::Podcast::find(LmsApp->getDbSession(), [this](const db::Podcast::pointer& podcast) {
            if (podcast->isDeleteRequested())
                return;

            auto* entry{ _podcasts->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.Podcasts.template.entry")) };
            entry->addFunction("tr", &Wt::WTemplate::Functions::tr);
            const db::PodcastId podcastId{ podcast->getId() };
            auto* titleBtn{ entry->bindNew<Wt::WPushButton>("title", podcast->getTitle().empty() ? Wt::WString::tr("Lms.Admin.Podcasts.pending-title") : Wt::WString::fromUTF8(std::string{ podcast->getTitle() })) };
            titleBtn->setStyleClass("btn btn-link p-0 text-start text-truncate");
            titleBtn->clicked().connect(this, [this, podcastId] { showEpisodesModal(podcastId); });
            entry->bindString("url", std::string{ podcast->getUrl() }, Wt::TextFormat::Plain);

            auto* deleteBtn{ entry->bindNew<Wt::WPushButton>("delete-btn", Wt::WString::tr("Lms.template.trash-btn"), Wt::TextFormat::XHTML) };
            deleteBtn->setToolTip(Wt::WString::tr("Lms.delete"));
            deleteBtn->clicked().connect(this, [this, podcastId] { showDeletePodcastModal(podcastId); });
        });
    }

    void PodcastSettingsView::showEpisodesModal(db::PodcastId podcastId)
    {
        auto modal{ std::make_unique<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.Podcasts.template.episodes")) };
        modal->addFunction("tr", &Wt::WTemplate::Functions::tr);
        Wt::WTemplate* modalPtr{ modal.get() };

        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            const db::Podcast::pointer podcast{ db::Podcast::find(LmsApp->getDbSession(), podcastId) };
            modal->bindString("title", podcast ? Wt::WString::fromUTF8(std::string{ podcast->getTitle() }) : Wt::WString::tr("Lms.Admin.Podcasts.episodes"), Wt::TextFormat::Plain);
        }

        auto* episodes{ modal->bindNew<Wt::WContainerWidget>("episodes") };
        populateEpisodes(*episodes, podcastId);

        auto closeModal{ [modalPtr] { LmsApp->getModalManager().dispose(modalPtr); } };
        modal->bindNew<Wt::WPushButton>("close-x")->clicked().connect(closeModal);
        modal->bindNew<Wt::WPushButton>("close-btn", Wt::WString::tr("Lms.Admin.Podcasts.close"))->clicked().connect(closeModal);
        modal->bindNew<Wt::WPushButton>("download-all-btn", Wt::WString::tr("Lms.Admin.Podcasts.download-all"))
            ->clicked()
            .connect(this, [this, episodes, podcastId] {
                std::vector<db::PodcastEpisodeId> episodeIds;
                {
                    auto transaction{ LmsApp->getDbSession().createReadTransaction() };
                    db::PodcastEpisode::FindParameters params;
                    params.setPodcast(podcastId).setSortMode(db::PodcastEpisodeSortMode::PubDateDesc);
                    db::PodcastEpisode::find(LmsApp->getDbSession(), params, [&episodeIds](const db::PodcastEpisode::pointer& episode) {
                        if (episode->getAudioRelativeFilePath().empty()
                            && episode->getManualDownloadState() != db::PodcastEpisode::ManualDownloadState::DownloadRequested
                            && episode->getManualDownloadState() != db::PodcastEpisode::ManualDownloadState::DeleteRequested)
                            episodeIds.push_back(episode->getId());
                    });
                }
                for (const db::PodcastEpisodeId episodeId : episodeIds)
                    core::Service<podcast::IPodcastService>::get()->downloadPodcastEpisode(episodeId);

                populateEpisodes(*episodes, podcastId);
                LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.Podcasts.download-all-requested"));
            });

        LmsApp->getModalManager().show(std::move(modal));
    }

    void PodcastSettingsView::populateEpisodes(Wt::WContainerWidget& container, db::PodcastId podcastId)
    {
        container.clear();

        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        db::PodcastEpisode::FindParameters params;
        params.setPodcast(podcastId).setSortMode(db::PodcastEpisodeSortMode::PubDateDesc);
        db::PodcastEpisode::find(LmsApp->getDbSession(), params, [this, &container, podcastId](const db::PodcastEpisode::pointer& episode) {
            auto* entry{ container.addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.Podcasts.template.episode")) };
            entry->bindString("title", std::string{ episode->getTitle() }, Wt::TextFormat::Plain);
            entry->bindString("date", episode->getPubDate().isValid() ? episode->getPubDate().date().toString("yyyy-MM-dd") : Wt::WString{}, Wt::TextFormat::Plain);

            auto* status{ entry->bindNew<Wt::WText>("status", getEpisodeStatus(episode)) };
            status->setStyleClass(getEpisodeBadgeClass(episode));

            if (!episode->getAudioRelativeFilePath().empty()
                || episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DownloadRequested
                || episode->getManualDownloadState() == db::PodcastEpisode::ManualDownloadState::DeleteRequested)
            {
                entry->bindEmpty("download-btn");
                return;
            }

            const db::PodcastEpisodeId episodeId{ episode->getId() };
            entry->bindNew<Wt::WPushButton>("download-btn", Wt::WString::tr("Lms.Podcasts.download"), Wt::TextFormat::XHTML)
                ->clicked()
                .connect(this, [this, &container, podcastId, episodeId] {
                    core::Service<podcast::IPodcastService>::get()->downloadPodcastEpisode(episodeId);
                    populateEpisodes(container, podcastId);
                    LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Podcasts.download-requested"));
                });
        });

        if (container.count() == 0)
            container.addNew<Wt::WText>(Wt::WString::tr("Lms.Podcasts.no-episodes"));
    }

    void PodcastSettingsView::showDeletePodcastModal(db::PodcastId podcastId)
    {
        auto modal{ std::make_unique<Wt::WTemplate>(Wt::WString::tr("Lms.Admin.Podcasts.template.delete")) };
        modal->addFunction("tr", &Wt::WTemplate::Functions::tr);
        Wt::WTemplate* modalPtr{ modal.get() };

        modal->bindNew<Wt::WPushButton>("delete-btn", Wt::WString::tr("Lms.delete"))
            ->clicked()
            .connect(this, [this, podcastId, modalPtr] {
                core::Service<podcast::IPodcastService>::get()->removePodcast(podcastId);
                LmsApp->getModalManager().dispose(modalPtr);
                refreshView();
                LmsApp->notifyMsg(Notification::Type::Info, Wt::WString::tr("Lms.Admin.Podcasts.feed-deleted"));
            });
        modal->bindNew<Wt::WPushButton>("cancel-btn", Wt::WString::tr("Lms.cancel"))
            ->clicked()
            .connect([modalPtr] { LmsApp->getModalManager().dispose(modalPtr); });

        LmsApp->getModalManager().show(std::move(modal));
    }
} // namespace lms::ui
