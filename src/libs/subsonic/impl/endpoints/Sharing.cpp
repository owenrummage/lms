#include "Sharing.hpp"

#include <ctime>

#include "core/String.hpp"
#include "core/UUID.hpp"
#include "database/Session.hpp"
#include "database/objects/Podcast.hpp"
#include "database/objects/Release.hpp"
#include "database/objects/PodcastEpisode.hpp"
#include "database/objects/Share.hpp"
#include "database/objects/Track.hpp"
#include "database/objects/TrackList.hpp"
#include "database/objects/User.hpp"
#include "ParameterParsing.hpp"
#include "RequestContext.hpp"
#include "SubsonicId.hpp"
#include "responses/Song.hpp"
#include "responses/Podcast.hpp"
#include "core/Service.hpp"
#include "services/podcast/IPodcastService.hpp"

namespace lms::api::subsonic
{
    namespace
    {
        std::vector<std::string> mediaIds(const db::Share::pointer& share)
        {
            std::vector<std::string> result;
            for (auto value : core::stringUtils::splitString(share->getMediaIds(), '\n'))
                if (!value.empty()) result.emplace_back(value);
            return result;
        }

        std::vector<db::Track::pointer> resolveTracks(RequestContext& context, const std::vector<std::string>& ids)
        {
            std::vector<db::Track::pointer> tracks;
            for (const std::string& id : ids)
            {
                if (auto trackId = core::stringUtils::readAs<db::TrackId>(id))
                {
                    auto track = db::Track::find(context.getDbSession(), *trackId);
                    if (!track) throw RequestedDataNotFoundError{};
                    tracks.push_back(track);
                }
                else if (auto releaseId = core::stringUtils::readAs<db::ReleaseId>(id))
                {
                    if (!db::Release::find(context.getDbSession(), *releaseId)) throw RequestedDataNotFoundError{};
                    auto values = db::Track::find(context.getDbSession(), db::Track::FindParameters{}.setRelease(*releaseId));
                    tracks.insert(tracks.end(), values.begin(), values.end());
                }
                else if (auto listId = core::stringUtils::readAs<db::TrackListId>(id))
                {
                    auto list = db::TrackList::find(context.getDbSession(), *listId);
                    if (!list || (list->getVisibility() == db::TrackList::Visibility::Private && list->getUserId() != context.getUser()->getId()))
                        throw UserNotAuthorizedError{};
                    for (auto trackId : list->getTrackIds())
                        if (auto track = db::Track::find(context.getDbSession(), trackId)) tracks.push_back(track);
                }
                else if (auto episodeId = core::stringUtils::readAs<db::PodcastEpisodeId>(id))
                {
                    auto episode = db::PodcastEpisode::find(context.getDbSession(), *episodeId);
                    if (!episode || episode->getAudioRelativeFilePath().empty())
                        throw RequestedDataNotFoundError{};
                    const auto path = core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath();
                    if (!std::filesystem::is_regular_file(path))
                        throw RequestedDataNotFoundError{};
                }
                else if (auto podcastId = core::stringUtils::readAs<db::PodcastId>(id))
                {
                    if (!db::Podcast::find(context.getDbSession(), *podcastId)) throw RequestedDataNotFoundError{};
                    bool hasDownloadedEpisode{};
                    db::PodcastEpisode::find(context.getDbSession(), db::PodcastEpisode::FindParameters{}.setPodcast(*podcastId), [&](const auto& episode) {
                        if (episode->getAudioRelativeFilePath().empty()) return;
                        const auto path = core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath();
                        if (std::filesystem::is_regular_file(path)) hasDownloadedEpisode = true;
                    });
                    if (!hasDownloadedEpisode) throw RequestedDataNotFoundError{};
                }
                else
                    throw RequestedDataNotFoundError{};
            }
            return tracks;
        }

        std::optional<db::ShareId> parseShareId(std::string_view value)
        {
            auto parts = core::stringUtils::splitString(value, '-');
            if (parts.size() != 2 || parts[0] != "sh") return {};
            auto raw = core::stringUtils::readAs<db::ShareId::ValueType>(parts[1]);
            return raw ? std::optional<db::ShareId>{ db::ShareId{ *raw } } : std::nullopt;
        }

        db::Share::pointer ownedShare(RequestContext& context, std::string_view value)
        {
            auto id = parseShareId(value);
            if (!id) throw RequestedDataNotFoundError{};
            auto share = db::Share::find(context.getDbSession(), *id);
            if (!share) throw RequestedDataNotFoundError{};
            if (share->getUser()->getId() != context.getUser()->getId()) throw UserNotAuthorizedError{};
            return share;
        }

        Response::Node shareNode(RequestContext& context, const db::Share::pointer& share)
        {
            Response::Node node;
            node.setAttribute("id", "sh-" + share->getId().toString());
            node.setAttribute("url", context.getPublicBaseUrl() + "/share/" + std::string{ share->getToken() });
            node.setAttribute("username", share->getUser()->getLoginName());
            node.setAttribute("created", core::stringUtils::toISO8601String(share->getCreated()));
            node.setAttribute("visitCount", share->getVisitCount());
            if (!share->getDescription().empty()) node.setAttribute("description", share->getDescription());
            if (share->getExpires().isValid()) node.setAttribute("expires", core::stringUtils::toISO8601String(share->getExpires()));
            if (share->getLastVisited().isValid()) node.setAttribute("lastVisited", core::stringUtils::toISO8601String(share->getLastVisited()));
            for (const auto& track : resolveTracks(context, mediaIds(share)))
                node.addArrayChild("entry", createSongNode(context, track, true));
            for (const auto& id : mediaIds(share))
                if (auto episodeId = core::stringUtils::readAs<db::PodcastEpisodeId>(id))
                {
                    auto episode = db::PodcastEpisode::find(context.getDbSession(), *episodeId);
                    if (!episode || episode->getAudioRelativeFilePath().empty())
                        throw RequestedDataNotFoundError{};
                    node.addArrayChild("entry", createPodcastEpisodeNode(episode));
                }
                else if (auto podcastId = core::stringUtils::readAs<db::PodcastId>(id))
                    db::PodcastEpisode::find(context.getDbSession(), db::PodcastEpisode::FindParameters{}.setPodcast(*podcastId), [&](const auto& episode) {
                        if (episode->getAudioRelativeFilePath().empty()) return;
                        const auto path = core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath();
                        if (std::filesystem::is_regular_file(path)) node.addArrayChild("entry", createPodcastEpisodeNode(episode));
                    });
            return node;
        }

        void setOptionalFields(RequestContext& context, db::Share::pointer share)
        {
            if (auto description = getParameterAs<std::string>(context.getParameters(), "description"))
                share.modify()->setDescription(*description);
            if (auto expires = getParameterAs<long long>(context.getParameters(), "expires"))
                share.modify()->setExpires(*expires > 0 ? Wt::WDateTime::fromTime_t(static_cast<std::time_t>(*expires / 1000)) : Wt::WDateTime{});
        }
    }

    Response handleCreateShare(RequestContext& context)
    {
        const auto ids = getMandatoryMultiParametersAs<std::string>(context.getParameters(), "id");
        std::string stored;
        for (const auto& id : ids) { if (!stored.empty()) stored += '\n'; stored += id; }
        auto transaction = context.getDbSession().createWriteTransaction();
        resolveTracks(context, ids);
        const std::string token = core::UUID::generate().toString() + core::UUID::generate().toString();
        auto share = context.getDbSession().create<db::Share>(token, stored, context.getUser());
        setOptionalFields(context, share);
        Response response = Response::createOkResponse();
        response.createNode("shares").addArrayChild("share", shareNode(context, share));
        return response;
    }

    Response handleGetShares(RequestContext& context)
    {
        auto transaction = context.getDbSession().createReadTransaction();
        Response response = Response::createOkResponse();
        auto& shares = response.createNode("shares");
        db::Share::find(context.getDbSession(), context.getUser()->getId(), [&](const auto& share) { shares.addArrayChild("share", shareNode(context, share)); });
        return response;
    }

    Response handleUpdateShare(RequestContext& context)
    {
        const auto id = getMandatoryParameterAs<std::string>(context.getParameters(), "id");
        auto transaction = context.getDbSession().createWriteTransaction();
        auto share = ownedShare(context, id);
        setOptionalFields(context, share);
        return Response::createOkResponse();
    }

    Response handleDeleteShare(RequestContext& context)
    {
        const auto id = getMandatoryParameterAs<std::string>(context.getParameters(), "id");
        auto transaction = context.getDbSession().createWriteTransaction();
        ownedShare(context, id).remove();
        return Response::createOkResponse();
    }
}
