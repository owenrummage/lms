/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#include "PodcastsView.hpp"

#include <algorithm>
#include <chrono>
#include <vector>

#include <Wt/WAnchor.h>
#include <Wt/WPushButton.h>
#include <Wt/WTemplate.h>
#include <Wt/WText.h>

#include "core/Service.hpp"
#include "core/String.hpp"
#include "database/Session.hpp"
#include "database/objects/Podcast.hpp"
#include "database/objects/PodcastEpisode.hpp"
#include "services/podcast/IPodcastService.hpp"

#include "LmsApplication.hpp"
#include "Utils.hpp"
#include "MediaPlayer.hpp"
#include "PlayQueue.hpp"
#include "ShareUtils.hpp"

namespace lms::ui
{
    namespace
    {
        std::string cleanLegacyDescription(std::string_view description)
        {
            constexpr std::string_view cdataPrefix{ "<![CDATA[" };
            constexpr std::string_view cdataSuffix{ "]]>" };

            if (description.starts_with(cdataPrefix) && description.ends_with(cdataSuffix))
                description = description.substr(cdataPrefix.size(), description.size() - cdataPrefix.size() - cdataSuffix.size());

            return std::string{ description };
        }

    } // namespace

    PodcastsView::PodcastsView()
    {
        auto* page{ addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Podcasts.template")) };
        page->addFunction("tr", &Wt::WTemplate::Functions::tr);
        _content = page->bindNew<Wt::WContainerWidget>("content");

        wApp->internalPathChanged().connect(this, [this] { refreshView(); });
        refreshView();
    }

    void PodcastsView::refreshView()
    {
        if (!wApp->internalPathMatches("/podcasts"))
            return;

        constexpr std::string_view prefix{ "/podcasts/" };
        const std::string path{ wApp->internalPath() };
        if (path.starts_with(prefix))
        {
            const auto value{ core::stringUtils::readAs<db::PodcastId::ValueType>(std::string_view{ path }.substr(prefix.size())) };
            if (value)
            {
                showPodcast(db::PodcastId{ *value });
                return;
            }
        }

        showChannels();
    }

    void PodcastsView::showChannels()
    {
        _content->clear();
        auto* channels{ _content->addNew<Wt::WContainerWidget>() };
        channels->addStyleClass("d-grid gap-2");

        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        db::Podcast::find(LmsApp->getDbSession(), [channels](const db::Podcast::pointer& podcast) {
            if (podcast->isDeleteRequested())
                return;

            auto* entry{ channels->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Podcasts.template.channel")) };
            entry->addFunction("tr", &Wt::WTemplate::Functions::tr);
            if (const db::ArtworkId artworkId{ podcast->getArtworkId() }; artworkId.isValid())
                entry->bindWidget("cover", utils::createArtworkImage(artworkId, ArtworkResource::DefaultArtworkType::Release, ArtworkResource::Size::Small));
            else
                entry->bindWidget("cover", utils::createDefaultArtworkImage(ArtworkResource::DefaultArtworkType::Release));
            const Wt::WString title{ podcast->getTitle().empty() ? Wt::WString::tr("Lms.Admin.Podcasts.pending-title") : Wt::WString::fromUTF8(std::string{ podcast->getTitle() }) };
            entry->bindNew<Wt::WAnchor>("title", Wt::WLink{ Wt::LinkType::InternalPath, "/podcasts/" + podcast->getId().toString() }, title);
            entry->bindString("description", cleanLegacyDescription(podcast->getDescription()), Wt::TextFormat::Plain);
            const db::PodcastId podcastId{ podcast->getId() };
            auto* shareBtn{ entry->bindNew<Wt::WPushButton>("share-btn", "<i class=\"fa fa-share-alt\" aria-hidden=\"true\"></i>", Wt::TextFormat::XHTML) };
            shareBtn->setToolTip("Share");
            shareBtn->setAttributeValue("aria-label", "Share " + title);
            shareBtn->clicked().connect([podcastId] { shareUtils::share(podcastId); });
        });

        if (channels->count() == 0)
            channels->addNew<Wt::WText>(Wt::WString::tr("Lms.Podcasts.empty"));
    }

    void PodcastsView::showPodcast(db::PodcastId podcastId)
    {
        _content->clear();

        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
        const db::Podcast::pointer podcast{ db::Podcast::find(LmsApp->getDbSession(), podcastId) };
        if (!podcast || podcast->isDeleteRequested())
        {
            showChannels();
            return;
        }

        auto* header{ _content->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Podcasts.template.channel-header")) };
        header->addFunction("tr", &Wt::WTemplate::Functions::tr);
        if (const db::ArtworkId artworkId{ podcast->getArtworkId() }; artworkId.isValid())
            header->bindWidget("cover", utils::createArtworkImage(artworkId, ArtworkResource::DefaultArtworkType::Release, ArtworkResource::Size::Small));
        else
            header->bindWidget("cover", utils::createDefaultArtworkImage(ArtworkResource::DefaultArtworkType::Release));
        header->bindNew<Wt::WAnchor>("back", Wt::WLink{ Wt::LinkType::InternalPath, "/podcasts" }, Wt::WString::tr("Lms.Podcasts.back"));
        header->bindString("title", std::string{ podcast->getTitle() }, Wt::TextFormat::Plain);
        header->bindString("description", cleanLegacyDescription(podcast->getDescription()), Wt::TextFormat::Plain);
        auto* shareBtn{ header->bindNew<Wt::WPushButton>("share-btn", "<i class=\"fa fa-share-alt\" aria-hidden=\"true\"></i> Share", Wt::TextFormat::XHTML) };
        shareBtn->clicked().connect([podcastId] { shareUtils::share(podcastId); });

        auto* episodes{ _content->addNew<Wt::WContainerWidget>() };
        episodes->addStyleClass("d-grid gap-1 Lms-row-container");

        db::PodcastEpisode::FindParameters params;
        params.setPodcast(podcastId).setSortMode(db::PodcastEpisodeSortMode::PubDateDesc);
        db::PodcastEpisode::find(LmsApp->getDbSession(), params, [this, episodes, podcastId](const db::PodcastEpisode::pointer& episode) {
            auto* entry{ episodes->addNew<Wt::WTemplate>(Wt::WString::tr("Lms.Podcasts.template.episode")) };
            entry->addFunction("tr", &Wt::WTemplate::Functions::tr);
            entry->bindString("title", std::string{ episode->getTitle() }, Wt::TextFormat::Plain);
            entry->bindString("date", episode->getPubDate().isValid() ? episode->getPubDate().date().toString("yyyy-MM-dd") : Wt::WString{}, Wt::TextFormat::Plain);
            entry->bindString("row-class", episode->getAudioRelativeFilePath().empty() ? "opacity-50" : "", Wt::TextFormat::Plain);

            const db::PodcastEpisodeId episodeId{ episode->getId() };
            if (!episode->getAudioRelativeFilePath().empty())
            {
                auto* playBtn{ entry->bindNew<Wt::WPushButton>("action-btn", Wt::WString::tr("Lms.Podcasts.play"), Wt::TextFormat::XHTML) };
                playBtn->clicked().connect([episodeId, podcastId] {
                    std::vector<db::PodcastEpisodeId> episodeIds;
                    {
                        auto transaction{ LmsApp->getDbSession().createReadTransaction() };
                        db::PodcastEpisode::FindParameters params;
                        params.setPodcast(podcastId).setSortMode(db::PodcastEpisodeSortMode::PubDateDesc);
                        db::PodcastEpisode::find(LmsApp->getDbSession(), params, [&episodeIds](const db::PodcastEpisode::pointer& queuedEpisode) {
                            if (!queuedEpisode->getAudioRelativeFilePath().empty())
                                episodeIds.push_back(queuedEpisode->getId());
                        });
                    }
                    const auto it{ std::find(episodeIds.cbegin(), episodeIds.cend(), episodeId) };
                    if (it != episodeIds.cend())
                        LmsApp->getPlayQueue().playPodcastEpisodes(episodeIds, static_cast<std::size_t>(std::distance(episodeIds.cbegin(), it)));
                });
                auto* shareBtn{ entry->bindNew<Wt::WPushButton>("share-btn", "<i class=\"fa fa-share-alt\" aria-hidden=\"true\"></i>", Wt::TextFormat::XHTML) };
                shareBtn->setToolTip("Share");
                shareBtn->setAttributeValue("aria-label", "Share " + std::string{ episode->getTitle() });
                shareBtn->clicked().connect([episodeId] { shareUtils::share(episodeId); });
            }
            else
            {
                entry->bindEmpty("action-btn");
                entry->bindEmpty("share-btn");
            }
        });

        if (episodes->count() == 0)
            episodes->addNew<Wt::WText>(Wt::WString::tr("Lms.Podcasts.no-episodes"));
    }
} // namespace lms::ui
