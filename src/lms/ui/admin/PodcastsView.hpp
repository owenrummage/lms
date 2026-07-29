/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#pragma once

#include <Wt/WTemplate.h>

#include "database/objects/PodcastId.hpp"

namespace Wt
{
    class WContainerWidget;
    class WLineEdit;
}

namespace lms::ui
{
    class PodcastSettingsView final : public Wt::WTemplate
    {
    public:
        PodcastSettingsView();

    private:
        void addPodcast();
        void refreshView();
        void showEpisodesModal(db::PodcastId podcastId);
        void populateEpisodes(Wt::WContainerWidget& container, db::PodcastId podcastId);
        void showDeletePodcastModal(db::PodcastId podcastId);

        Wt::WLineEdit* _urlEdit{};
        Wt::WContainerWidget* _podcasts{};
    };
} // namespace lms::ui
