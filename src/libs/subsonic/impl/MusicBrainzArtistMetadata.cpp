#include "subsonic/MusicBrainzArtistMetadata.hpp"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include <Wt/Json/Array.h>
#include <Wt/Json/Object.h>
#include <Wt/Json/Parser.h>

#include "core/IConfig.hpp"
#include "core/ILogger.hpp"
#include "core/Service.hpp"
#include "core/String.hpp"
#include "core/http/IClient.hpp"
#include "core/UUID.hpp"
#include "database/IDb.hpp"
#include "database/Session.hpp"
#include "database/objects/Release.hpp"

namespace lms::api::subsonic
{
    namespace
    {
        std::string stringValue(const Wt::Json::Object& object, std::string_view key)
        {
            const std::string keyStr{ key };
            return object.type(keyStr) == Wt::Json::Type::String ? static_cast<std::string>(object.get(keyStr)) : std::string{};
        }

        class Service final : public MusicBrainzArtistMetadataService
        {
        public:
            explicit Service(boost::asio::io_context& ioContext, db::IDb& database)
                : _client{ core::http::createClient(ioContext, core::Service<core::IConfig>::get()->getString("musicbrainz-api-base-url", "https://musicbrainz.org")) }
                , _database{ database }
            {
            }

            bool startAlbumResync() override
            {
                std::scoped_lock lock{ _resyncMutex };
                if (_resyncStatus.running)
                    return false;
                if (_resyncThread.joinable())
                    _resyncThread.join();
                _resyncStatus = { .running = true };
                _resyncThread = std::jthread{ [this](std::stop_token stopToken) { resyncAlbums(stopToken); } };
                return true;
            }

            ResyncStatus getAlbumResyncStatus() const override
            {
                std::scoped_lock lock{ _resyncMutex };
                return _resyncStatus;
            }

        private:
            struct ReleaseMetadata
            {
                std::string id;
                std::string title;
                std::string barcode;
                std::string comment;
                std::string groupId;
                std::vector<std::string> labels;
                std::vector<std::string> countries;
                std::vector<std::string> types;
                std::optional<int> mediumCount;
            };

            std::optional<ReleaseMetadata> getReleaseMetadata(std::string_view mbid)
            {
                const auto body{ request(*_client, "MusicBrainz", "/ws/2/release/" + core::stringUtils::urlEncode(mbid) + "?inc=labels+release-groups+media&fmt=json") };
                if (!body) return std::nullopt;
                try
                {
                    Wt::Json::Object release;
                    Wt::Json::parse(*body, release);
                    ReleaseMetadata result;
                    result.id = stringValue(release, "id");
                    result.title = stringValue(release, "title");
                    result.barcode = stringValue(release, "barcode");
                    result.comment = stringValue(release, "disambiguation");
                    result.countries.push_back(stringValue(release, "country"));
                    if (release.type("media") == Wt::Json::Type::Array)
                        result.mediumCount = static_cast<int>(static_cast<const Wt::Json::Array&>(release.get("media")).size());
                    if (release.type("release-group") == Wt::Json::Type::Object)
                    {
                        const Wt::Json::Object& group{ release.get("release-group") };
                        result.groupId = stringValue(group, "id");
                        const std::string primaryType{ stringValue(group, "primary-type") };
                        if (!primaryType.empty()) result.types.push_back(primaryType);
                        if (group.type("secondary-types") == Wt::Json::Type::Array)
                            for (const Wt::Json::Value& value : static_cast<const Wt::Json::Array&>(group.get("secondary-types")))
                                if (value.type() == Wt::Json::Type::String) result.types.push_back(static_cast<std::string>(value));
                    }
                    if (release.type("label-info") == Wt::Json::Type::Array)
                        for (const Wt::Json::Value& value : static_cast<const Wt::Json::Array&>(release.get("label-info")))
                            if (value.type() == Wt::Json::Type::Object)
                            {
                                const Wt::Json::Object& info{ value };
                                if (info.type("label") == Wt::Json::Type::Object)
                                {
                                    const std::string name{ stringValue(static_cast<const Wt::Json::Object&>(info.get("label")), "name") };
                                    if (!name.empty()) result.labels.push_back(name);
                                }
                            }
                    return result.id.empty() ? std::nullopt : std::optional{ std::move(result) };
                }
                catch (const std::exception& e)
                {
                    LMS_LOG(API_SUBSONIC, WARNING, "Cannot parse MusicBrainz release metadata: " << e.what());
                    return std::nullopt;
                }
            }

            void resyncAlbums(std::stop_token stopToken)
            {
                db::Session& session{ _database.getTLSSession() };
                std::vector<db::ReleaseId> ids;
                {
                    auto transaction{ session.createReadTransaction() };
                    ids = db::Release::findIds(session, {});
                }
                {
                    std::scoped_lock lock{ _resyncMutex };
                    _resyncStatus.total = ids.size();
                }

                for (const db::ReleaseId id : ids)
                {
                    if (stopToken.stop_requested()) break;
                    std::string mbid;
                    {
                        auto transaction{ session.createReadTransaction() };
                        const db::Release::pointer release{ db::Release::find(session, id) };
                        if (release && release->getMBID()) mbid = release->getMBID()->toString();
                    }
                    if (mbid.empty())
                    {
                        std::scoped_lock lock{ _resyncMutex };
                        ++_resyncStatus.processed; ++_resyncStatus.skipped;
                        continue;
                    }

                    const auto metadata{ getReleaseMetadata(mbid) };
                    if (!metadata)
                    {
                        std::scoped_lock lock{ _resyncMutex };
                        ++_resyncStatus.processed; ++_resyncStatus.failed;
                        continue;
                    }

                    try
                    {
                        auto transaction{ session.createWriteTransaction() };
                        db::Release::pointer release{ db::Release::find(session, id) };
                        if (release)
                        {
                            auto writable{ release.modify() };
                            if (!metadata->title.empty()) writable->setName(metadata->title);
                            writable->setBarcode(metadata->barcode);
                            writable->setComment(metadata->comment);
                            writable->setTotalDisc(metadata->mediumCount);
                            if (const auto groupId{ core::UUID::fromString(metadata->groupId) }) writable->setGroupMBID(*groupId);
                            writable->clearLabels();
                            for (const std::string& name : metadata->labels)
                            {
                                db::Label::pointer label{ db::Label::find(session, name) };
                                if (!label) label = session.create<db::Label>(name);
                                writable->addLabel(label);
                            }
                            writable->clearCountries();
                            for (const std::string& name : metadata->countries)
                                if (!name.empty())
                                {
                                    db::Country::pointer country{ db::Country::find(session, name) };
                                    if (!country) country = session.create<db::Country>(name);
                                    writable->addCountry(country);
                                }
                            writable->clearReleaseTypes();
                            for (const std::string& name : metadata->types)
                            {
                                db::ReleaseType::pointer type{ db::ReleaseType::find(session, name) };
                                if (!type) type = session.create<db::ReleaseType>(name);
                                writable->addReleaseType(type);
                            }
                        }
                        std::scoped_lock lock{ _resyncMutex };
                        ++_resyncStatus.processed; ++_resyncStatus.updated;
                    }
                    catch (const std::exception& e)
                    {
                        LMS_LOG(API_SUBSONIC, ERROR, "Cannot update release " << id.getValue() << " from MusicBrainz: " << e.what());
                        std::scoped_lock lock{ _resyncMutex };
                        ++_resyncStatus.processed; ++_resyncStatus.failed;
                    }
                }
                std::scoped_lock lock{ _resyncMutex };
                _resyncStatus.running = false;
            }

            std::optional<std::string> request(core::http::IClient& client, std::string_view serviceName, std::string relativeUrl)
            {
                struct State { std::mutex mutex; std::condition_variable cv; bool complete{}; std::optional<std::string> body; };
                const auto state{ std::make_shared<State>() };
                const std::string requestUrl{ relativeUrl };

                {
                    std::unique_lock lock{ _rateLimitMutex };
                    const auto nextRequest{ _lastRequest + std::chrono::seconds{ 1 } };
                    if (const auto now{ std::chrono::steady_clock::now() }; now < nextRequest)
                        std::this_thread::sleep_until(nextRequest);
                    _lastRequest = std::chrono::steady_clock::now();
                }

                core::http::ClientGETRequestParameters request;
                request.relativeUrl = std::move(relativeUrl);
                request.responseBufferSize = 2 * 1024 * 1024;
                request.headers.push_back({ "User-Agent", "LMS/3.79 (https://github.com/epoupon/lms)" });
                request.headers.push_back({ "Accept", "application/json" });
                request.onSuccessFunc = [state](const Wt::Http::Message& message) { std::scoped_lock lock{ state->mutex }; state->body = message.body(); state->complete = true; state->cv.notify_one(); };
                request.onFailureFunc = [state, requestUrl, serviceName = std::string{ serviceName }] {
                    LMS_LOG(API_SUBSONIC, WARNING, serviceName << " request failed: " << requestUrl);
                    std::scoped_lock lock{ state->mutex };
                    state->complete = true;
                    state->cv.notify_one();
                };
                request.onAbortFunc = request.onFailureFunc;
                client.sendGETRequest(std::move(request));
                std::unique_lock lock{ state->mutex };
                if (!state->cv.wait_for(lock, std::chrono::seconds{ 10 }, [&] { return state->complete; }))
                    LMS_LOG(API_SUBSONIC, WARNING, serviceName << " request timed out: " << requestUrl);
                return state->body;
            }

            std::unique_ptr<core::http::IClient> _client;
            std::mutex _rateLimitMutex;
            std::chrono::steady_clock::time_point _lastRequest{};
            db::IDb& _database;
            mutable std::mutex _resyncMutex;
            ResyncStatus _resyncStatus;
            std::jthread _resyncThread;
        };
    } // namespace

    std::unique_ptr<MusicBrainzArtistMetadataService> createMusicBrainzArtistMetadataService(boost::asio::io_context& ioContext, db::IDb& database)
    {
        return std::make_unique<Service>(ioContext, database);
    }
} // namespace lms::api::subsonic
