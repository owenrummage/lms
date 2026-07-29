#pragma once

#include <memory>
#include <string>

#include <boost/asio/io_context.hpp>

namespace lms::db { class IDb; }

namespace lms::api::subsonic
{
    class MusicBrainzArtistMetadataService
    {
    public:
        struct ResyncStatus
        {
            bool running{};
            std::size_t total{};
            std::size_t processed{};
            std::size_t updated{};
            std::size_t skipped{};
            std::size_t failed{};
        };

        virtual ~MusicBrainzArtistMetadataService() = default;
        virtual bool startAlbumResync() = 0;
        virtual ResyncStatus getAlbumResyncStatus() const = 0;
    };

    std::unique_ptr<MusicBrainzArtistMetadataService> createMusicBrainzArtistMetadataService(boost::asio::io_context& ioContext, lms::db::IDb& database);
} // namespace lms::api::subsonic
