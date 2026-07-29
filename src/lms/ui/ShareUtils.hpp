#pragma once

#include "database/objects/PodcastEpisodeId.hpp"
#include "database/objects/PodcastId.hpp"
#include "database/objects/ReleaseId.hpp"
#include "database/objects/TrackId.hpp"

namespace lms::ui::shareUtils
{
    void share(db::TrackId trackId);
    void share(db::ReleaseId releaseId);
    void share(db::PodcastId podcastId);
    void share(db::PodcastEpisodeId episodeId);
}
