#include "ShareUtils.hpp"

#include <Wt/WAnchor.h>
#include <Wt/WLineEdit.h>
#include <Wt/WPushButton.h>
#include <Wt/WTemplate.h>

#include "core/String.hpp"
#include "core/UUID.hpp"
#include "database/Session.hpp"
#include "database/objects/PodcastEpisode.hpp"
#include "database/objects/Podcast.hpp"
#include "database/objects/Release.hpp"
#include "database/objects/Share.hpp"
#include "database/objects/Track.hpp"
#include "database/objects/User.hpp"
#include "services/podcast/IPodcastService.hpp"
#include "core/Service.hpp"
#include "core/IConfig.hpp"

#include "LmsApplication.hpp"
#include "ModalManager.hpp"
#include "Utils.hpp"

namespace lms::ui::shareUtils
{
    namespace
    {
        void showShareModal(std::string mediaId)
        {
            std::string token{ core::UUID::generate().toString() + core::UUID::generate().toString() };
            {
                auto transaction{ LmsApp->getDbSession().createWriteTransaction() };
                const db::User::pointer user{ db::User::find(LmsApp->getDbSession(), LmsApp->getUserId()) };
                if (!user || user->getType() == db::UserType::DEMO)
                    return;
                LmsApp->getDbSession().create<db::Share>(token, mediaId, user);
            }

            std::string publicUrl{ core::Service<core::IConfig>::get()->getString("public-url", "") };
            while (publicUrl.ends_with('/'))
                publicUrl.pop_back();
            if (publicUrl.empty())
            {
                const auto& env{ LmsApp->environment() };
                publicUrl = env.urlScheme() + "://" + env.hostName();
            }
            const std::string url{ publicUrl + "/share/" + token };
            auto modal{ std::make_unique<Wt::WTemplate>(Wt::WString::fromUTF8(R"(
<div class="modal fade" tabindex="-1"><div class="modal-dialog"><div class="modal-content">
<div class="modal-header"><h5 class="modal-title">Share</h5>${close-x class="btn-close" data-bs-dismiss="modal" aria-label="Close"}</div>
<div class="modal-body"><p>Anyone with this link can listen or download without signing in.</p><div class="input-group">${url class="form-control" readonly="readonly"}${copy class="btn btn-outline-primary"}</div></div>
<div class="modal-footer">${open class="btn btn-outline-primary" target="_blank" rel="noopener noreferrer"}${close class="btn btn-primary"}</div>
</div></div></div>)")) };
            Wt::WWidget* modalPtr{ modal.get() };
            auto close{ [modalPtr] { LmsApp->getModalManager().dispose(modalPtr); } };
            modal->bindNew<Wt::WPushButton>("close-x")->clicked().connect(close);
            modal->bindNew<Wt::WLineEdit>("url", url);
            auto* copy{ modal->bindNew<Wt::WPushButton>("copy", "Copy link") };
            copy->clicked().connect([url] {
                utils::copyToClipboard(url);
            });
            modal->bindNew<Wt::WAnchor>("open", Wt::WLink{ url }, "Open link");
            modal->bindNew<Wt::WPushButton>("close", "Close")->clicked().connect(close);
            LmsApp->getModalManager().show(std::move(modal));
        }
    }

    void share(db::TrackId trackId)
    {
        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            if (!db::Track::find(LmsApp->getDbSession(), trackId)) return;
        }
        showShareModal("tr-" + trackId.toString());
    }

    void share(db::PodcastEpisodeId episodeId)
    {
        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            const auto episode{ db::PodcastEpisode::find(LmsApp->getDbSession(), episodeId) };
            if (!episode || episode->getAudioRelativeFilePath().empty()) return;
            const auto path{ core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath() };
            if (!std::filesystem::is_regular_file(path)) return;
        }
        showShareModal("podep-" + episodeId.toString());
    }

    void share(db::ReleaseId releaseId)
    {
        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            if (!db::Release::find(LmsApp->getDbSession(), releaseId)) return;
        }
        showShareModal("al-" + releaseId.toString());
    }

    void share(db::PodcastId podcastId)
    {
        {
            auto transaction{ LmsApp->getDbSession().createReadTransaction() };
            if (!db::Podcast::find(LmsApp->getDbSession(), podcastId)) return;
            bool hasDownloadedEpisode{};
            db::PodcastEpisode::find(LmsApp->getDbSession(), db::PodcastEpisode::FindParameters{}.setPodcast(podcastId), [&](const auto& episode) {
                if (episode->getAudioRelativeFilePath().empty()) return;
                const auto path{ core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath() };
                if (std::filesystem::is_regular_file(path)) hasDownloadedEpisode = true;
            });
            if (!hasDownloadedEpisode) return;
        }
        showShareModal("pod-" + podcastId.toString());
    }
}
