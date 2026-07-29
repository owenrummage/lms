/*
 * Copyright (C) 2026 Emeric Poupon
 *
 * This file is part of LMS.
 */

#pragma once

#include <Wt/WContainerWidget.h>

#include "database/objects/PodcastId.hpp"

namespace Wt
{
    class WContainerWidget;
}

namespace lms::ui
{
    class PodcastsView final : public Wt::WContainerWidget
    {
    public:
        PodcastsView();

    private:
        void refreshView();
        void showChannels();
        void showPodcast(db::PodcastId podcastId);

        Wt::WContainerWidget* _content{};
    };
} // namespace lms::ui
