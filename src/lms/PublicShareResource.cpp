#include "PublicShareResource.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>

#include <Wt/Http/Request.h>
#include <Wt/Http/Response.h>

#include "core/FileResourceHandlerCreator.hpp"
#include "core/IResourceHandler.hpp"
#include "core/IZipper.hpp"
#include "core/String.hpp"
#include "database/IDb.hpp"
#include "database/Session.hpp"
#include "database/objects/Podcast.hpp"
#include "database/objects/Release.hpp"
#include "database/objects/PodcastEpisode.hpp"
#include "database/objects/Share.hpp"
#include "database/objects/Track.hpp"
#include "database/objects/TrackList.hpp"
#include "core/Service.hpp"
#include "services/artwork/IArtworkService.hpp"
#include "services/podcast/IPodcastService.hpp"

namespace lms
{
    namespace
    {
        template<typename Id>
        std::optional<Id> parseId(std::string_view value, std::string_view prefix)
        {
            auto parts = core::stringUtils::splitString(value, '-');
            if (parts.size() != 2 || parts[0] != prefix) return {};
            auto raw = core::stringUtils::readAs<typename Id::ValueType>(parts[1]);
            return raw ? std::optional<Id>{ Id{ *raw } } : std::nullopt;
        }

        std::string escape(std::string_view value)
        {
            std::string out;
            for (char c : value)
                switch (c) { case '&': out += "&amp;"; break; case '<': out += "&lt;"; break; case '>': out += "&gt;"; break; case '"': out += "&quot;"; break; case '\'': out += "&#39;"; break; default: out += c; }
            return out;
        }

        struct PublicItem
        {
            std::string id;
            std::string title;
            std::string artist;
            std::string album;
            db::ArtworkId artworkId;
            std::filesystem::path path;
            std::string mimeType;
            std::string downloadName;
        };

        std::string safeFileName(std::string value)
        {
            for (char& c : value)
                if (c == '/' || c == '\\' || c == '"' || c == '\r' || c == '\n') c = '_';
            return value.empty() ? "shared-audio" : value;
        }

        std::string podcastExtension(const db::PodcastEpisode::pointer& episode)
        {
            std::string enclosure{ episode->getEnclosureUrl() };
            if (const auto suffixPos{ enclosure.find_first_of("?#") }; suffixPos != std::string::npos)
                enclosure.resize(suffixPos);
            const std::string extension{ std::filesystem::path{ enclosure }.extension().string() };
            if (!extension.empty() && extension.size() <= 10) return extension;

            const std::string_view mime{ episode->getEnclosureContentType() };
            if (mime == "audio/mpeg" || mime == "audio/mp3") return ".mp3";
            if (mime == "audio/ogg" || mime == "application/ogg") return ".ogg";
            if (mime == "audio/opus") return ".opus";
            if (mime == "audio/mp4" || mime == "audio/x-m4a") return ".m4a";
            if (mime == "audio/flac" || mime == "audio/x-flac") return ".flac";
            if (mime == "audio/wav" || mime == "audio/x-wav") return ".wav";
            return ".audio";
        }

        std::vector<PublicItem> resolveItems(db::Session& session, std::string_view stored)
        {
            std::vector<PublicItem> items;
            auto addTrack = [&](const db::Track::pointer& track) {
                const auto release{ track->getRelease() };
                db::ArtworkId artworkId{ track->getPreferredMediaArtworkId() };
                if (!artworkId.isValid()) artworkId = track->getPreferredArtworkId();
                items.push_back({ "tr-" + track->getId().toString(), std::string{ track->getName() }, std::string{ track->getArtistDisplayName() }, release ? std::string{ release->getName() } : std::string{}, artworkId, track->getAbsoluteFilePath(), {}, safeFileName(track->getAbsoluteFilePath().filename().string()) });
            };
            for (auto id : core::stringUtils::splitString(stored, '\n'))
            {
                if (auto trackId = parseId<db::TrackId>(id, "tr"))
                {
                    if (auto track = db::Track::find(session, *trackId)) addTrack(track); else return {};
                }
                else if (auto releaseId = parseId<db::ReleaseId>(id, "al"))
                {
                    if (!db::Release::find(session, *releaseId)) return {};
                    auto values = db::Track::find(session, db::Track::FindParameters{}.setRelease(*releaseId));
                    for (const auto& track : values) addTrack(track);
                }
                else if (auto listId = parseId<db::TrackListId>(id, "pl"))
                {
                    auto list = db::TrackList::find(session, *listId);
                    if (!list) return {};
                    for (auto trackId : list->getTrackIds())
                        if (auto track = db::Track::find(session, trackId)) addTrack(track);
                }
                else if (auto episodeId = parseId<db::PodcastEpisodeId>(id, "podep"))
                {
                    auto episode = db::PodcastEpisode::find(session, *episodeId);
                    if (!episode || episode->getAudioRelativeFilePath().empty()) return {};
                    const auto path = core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath();
                    if (!std::filesystem::is_regular_file(path)) return {};
                    const auto podcast{ episode->getPodcast() };
                    db::ArtworkId artworkId{ episode->getArtworkId() };
                    if (!artworkId.isValid() && podcast) artworkId = podcast->getArtworkId();
                    const std::string artist{ !episode->getAuthor().empty() ? std::string{ episode->getAuthor() } : (podcast ? std::string{ podcast->getAuthor() } : std::string{}) };
                    items.push_back({ "podep-" + episode->getId().toString(), std::string{ episode->getTitle() }, artist, podcast ? std::string{ podcast->getTitle() } : std::string{}, artworkId, path, std::string{ episode->getEnclosureContentType() }, safeFileName(std::string{ episode->getTitle() }) + podcastExtension(episode) });
                }
                else if (auto podcastId = parseId<db::PodcastId>(id, "pod"))
                {
                    if (!db::Podcast::find(session, *podcastId)) return {};
                    db::PodcastEpisode::find(session, db::PodcastEpisode::FindParameters{}.setPodcast(*podcastId), [&](const auto& episode) {
                        if (episode->getAudioRelativeFilePath().empty()) return;
                        const auto path = core::Service<podcast::IPodcastService>::get()->getCachePath() / episode->getAudioRelativeFilePath();
                        if (!std::filesystem::is_regular_file(path)) return;
                        const auto podcast{ episode->getPodcast() };
                        db::ArtworkId artworkId{ episode->getArtworkId() };
                        if (!artworkId.isValid() && podcast) artworkId = podcast->getArtworkId();
                        const std::string artist{ !episode->getAuthor().empty() ? std::string{ episode->getAuthor() } : (podcast ? std::string{ podcast->getAuthor() } : std::string{}) };
                        items.push_back({ "podep-" + episode->getId().toString(), std::string{ episode->getTitle() }, artist, podcast ? std::string{ podcast->getTitle() } : std::string{}, artworkId, path, std::string{ episode->getEnclosureContentType() }, safeFileName(std::string{ episode->getTitle() }) + podcastExtension(episode) });
                    });
                }
                else return {};
            }
            return items;
        }

        bool expired(const db::Share::pointer& share)
        {
            return share->getExpires().isValid() && share->getExpires() <= Wt::WDateTime::currentDateTime();
        }
    }

    void PublicShareResource::handleRequest(const Wt::Http::Request& request, Wt::Http::Response& response)
    {
        const std::string requestPath{ request.pathInfo() };
        auto parts = core::stringUtils::splitString(requestPath, '/');
        parts.erase(std::remove_if(parts.begin(), parts.end(), [](auto part) { return part.empty(); }), parts.end());
        if (!parts.empty() && parts.front() == "share")
            parts.erase(parts.begin());
        if (parts.empty()) { response.setStatus(404); return; }

        std::vector<PublicItem> items;
        db::ShareId shareId;
        std::string description;
        {
            auto transaction = _db.getTLSSession().createReadTransaction();
            auto share = db::Share::find(_db.getTLSSession(), parts[0]);
            if (!share || expired(share)) { response.setStatus(404); return; }
            shareId = share->getId();
            description = share->getDescription();
            items = resolveItems(_db.getTLSSession(), share->getMediaIds());
            if (items.empty()) { response.setStatus(404); return; }
        }

        if (parts.size() >= 2 && parts[1] == "stream")
        {
            if (parts.size() != 3) { response.setStatus(404); return; }
            auto it = std::find_if(items.begin(), items.end(), [&](const auto& item) { return item.id == parts[2]; });
            if (it == items.end()) { response.setStatus(404); return; }
            std::shared_ptr<core::IResourceHandler> handler;
            if (request.continuation()) handler = Wt::cpp17::any_cast<std::shared_ptr<core::IResourceHandler>>(request.continuation()->data());
            else handler = core::createFileResourceHandler(it->path, it->mimeType);
            if (auto* continuation = handler->processRequest(request, response)) continuation->setData(handler);
            return;
        }

        if (parts.size() >= 2 && parts[1] == "artwork")
        {
            if (parts.size() != 3) { response.setStatus(404); return; }
            const auto item{ std::find_if(items.begin(), items.end(), [&](const auto& value) { return value.id == parts[2]; }) };
            if (item == items.end()) { response.setStatus(404); return; }
            auto artworkService{ core::Service<artwork::IArtworkService>::get() };
            auto image{ item->artworkId.isValid() ? artworkService->getImage(item->artworkId, 256) : nullptr };
            if (!image) image = artworkService->getDefaultReleaseArtwork();
            if (!image) { response.setStatus(404); return; }
            response.setMimeType(std::string{ image->getMimeType() });
            response.out().write(reinterpret_cast<const char*>(image->getData().data()), image->getData().size());
            return;
        }

        if (parts.size() == 2 && parts[1] == "download")
        {
            if (items.size() == 1)
            {
                std::shared_ptr<core::IResourceHandler> handler;
                if (request.continuation())
                    handler = Wt::cpp17::any_cast<std::shared_ptr<core::IResourceHandler>>(request.continuation()->data());
                else
                {
                    handler = core::createFileResourceHandler(items.front().path, items.front().mimeType);
                    response.addHeader("Content-Disposition", "attachment; filename=\"" + items.front().downloadName + "\"");
                }
                if (auto* continuation = handler->processRequest(request, response)) continuation->setData(handler);
                return;
            }

            std::shared_ptr<zip::IZipper> zipper;
            if (request.continuation()) zipper = Wt::cpp17::any_cast<std::shared_ptr<zip::IZipper>>(request.continuation()->data());
            else
            {
                zip::EntryContainer entries;
                std::size_t n{};
                for (const auto& item : items)
                    entries.push_back({ std::to_string(++n) + " - " + item.downloadName, item.path });
                zipper = zip::createArchiveZipper(entries);
                response.setMimeType("application/zip");
                response.addHeader("Content-Disposition", "attachment; filename=shared-music.zip");
            }
            zipper->writeSome(response.out());
            if (!zipper->isComplete()) response.createContinuation()->setData(zipper);
            return;
        }

        if (parts.size() != 1) { response.setStatus(404); return; }
        {
            auto transaction = _db.getTLSSession().createWriteTransaction();
            if (auto share = db::Share::find(_db.getTLSSession(), shareId))
            {
                share.modify()->incrementVisitCount();
                share.modify()->setLastVisited(Wt::WDateTime::currentDateTime());
            }
        }

        response.setMimeType("text/html; charset=utf-8");
        auto& out = response.out();
        const std::string shareBaseUrl{ "/share/" + std::string{ parts[0] } + "/" };
        out << "<!doctype html><html><head><base href=\"" << shareBaseUrl << "\"><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>Shared music</title>"
               "<style>"
               ":root{color-scheme:dark;--bg:#102f36;--panel:#173b43;--line:#34545a;--text:#d8e2e3;--muted:#91a9ad;--accent:#41b3ad;--accent2:#65cbc5}"
               "*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif}"
               ".page{max-width:900px;margin:auto;padding:3rem 1.25rem 8rem}header{display:flex;align-items:flex-start;justify-content:space-between;gap:2rem;margin-bottom:2rem}"
               "h1{font-size:clamp(1.8rem,5vw,2.7rem);line-height:1.1;margin:0 0 .55rem}.description{color:var(--muted);margin:0;max-width:38rem;line-height:1.55}"
               "a{color:var(--accent)}.download{flex:none;text-decoration:none;color:var(--accent2);border:1px solid var(--accent);border-radius:.35rem;padding:.65rem .9rem;font-weight:600}"
               ".queue{border-top:1px solid var(--line)}.track{display:grid;grid-template-columns:2rem 3.25rem minmax(0,1fr) auto;align-items:center;gap:.75rem;width:100%;padding:.65rem .6rem;border:0;border-bottom:1px solid var(--line);background:none;color:inherit;text-align:left;cursor:pointer;font:inherit}"
               ".track:hover,.track.active{background:rgba(65,179,173,.09)}.track.active{color:var(--accent2)}.track-number{color:var(--muted);text-align:center}.track.active .track-number{font-size:0}.track.active .track-number:after{content:'';display:inline-block;border-left:8px solid var(--accent);border-top:5px solid transparent;border-bottom:5px solid transparent}.track-title{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.track-action{color:var(--muted);font-size:.8rem}"
               ".track-art{width:3.25rem;height:3.25rem;border-radius:.25rem;object-fit:cover;background:var(--line)}.track-info{min-width:0}.track-meta{color:var(--muted);font-size:.82rem;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;margin-top:.2rem}.track.active .track-meta{color:var(--muted)}"
               ".player{position:fixed;z-index:10;left:0;right:0;bottom:0;min-height:74px;background:var(--panel);box-shadow:0 -8px 28px rgba(0,0,0,.22)}"
               ".seek-wrap{position:absolute;left:0;right:0;top:0;height:7px}.seek-track{position:absolute;inset:0;background:var(--line)}.seek-progress{height:100%;width:0;background:var(--accent)}"
               "input[type=range]{accent-color:var(--accent)}#seek{position:absolute;inset:-7px 0 0;width:100%;height:20px;margin:0;opacity:0;cursor:pointer}"
               ".player-inner{max-width:1050px;min-height:74px;margin:auto;padding:.5rem 1rem;display:grid;grid-template-columns:auto 3.5rem minmax(0,1fr) auto auto;align-items:center;gap:1rem}.now-art{width:3.5rem;height:3.5rem;border-radius:.3rem;object-fit:cover;background:var(--line)}"
               ".controls{display:flex;align-items:center}.control{border:0;background:none;color:var(--accent2);width:2.35rem;height:2.35rem;border-radius:50%;font-size:1rem;cursor:pointer}.control:hover{background:rgba(65,179,173,.12)}#play{font-size:1.15rem;border:1px solid var(--accent);margin:0 .15rem}"
               ".control svg,.volume svg{display:block;margin:auto;fill:currentColor}.play-symbol{display:block;position:relative;width:13px;height:16px;margin:auto}.play-symbol:before{content:'';position:absolute;inset:0;border-left:13px solid currentColor;border-top:8px solid transparent;border-bottom:8px solid transparent}.playing .play-symbol:before{border:0;background:linear-gradient(90deg,currentColor 0 35%,transparent 35% 65%,currentColor 65% 100%)}"
               ".now-playing{text-align:center;min-width:0}.now-title{font-weight:650;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.now-subtitle{color:var(--muted);font-size:.82rem;margin-top:.2rem}.time{color:var(--muted);font-variant-numeric:tabular-nums;white-space:nowrap;font-size:.9rem}.volume{display:flex;align-items:center;gap:.45rem;color:var(--muted)}#volume{width:88px}audio{display:none}"
               "@media(max-width:620px){.page{padding-top:2rem}.player-inner{grid-template-columns:auto 3rem minmax(0,1fr) auto;gap:.4rem}.now-art{width:3rem;height:3rem}.volume{display:none}.time{font-size:.78rem}header{display:block}.download{display:inline-block;margin-top:1.25rem}.control{width:2rem}.now-playing{text-align:left}.track{grid-template-columns:1.5rem 3rem minmax(0,1fr)}.track-art{width:3rem;height:3rem}.track-action{display:none}}"
               "</style></head><body><main class=page><header><div><h1>Shared music</h1>";
        if (!description.empty()) out << "<p class=description>" << escape(description) << "</p>";
        out << "</div><a class=download href=\"download\">Download" << (items.size() > 1 ? " all" : "") << "</a></header><section class=queue aria-label=\"Shared tracks\">";
        std::size_t trackNumber{};
        for (const auto& item : items)
        {
            const std::string metadata{ item.artist + (!item.artist.empty() && !item.album.empty() ? " · " : "") + item.album };
            out << "<button class=track type=button data-src=\"stream/" << item.id << "\" data-art=\"artwork/" << item.id << "\" data-title=\"" << escape(item.title) << "\" data-meta=\"" << escape(metadata) << "\"><span class=track-number>" << ++trackNumber << "</span><img class=track-art src=\"artwork/" << item.id << "\" alt=\"\"><span class=track-info><strong class=track-title>" << escape(item.title) << "</strong><span class=track-meta>" << escape(metadata) << "</span></span><span class=track-action>Play</span></button>";
        }
        out << "</section></main>"
               "<div class=player role=region aria-label=\"Now playing\"><audio id=audio preload=metadata></audio><div class=seek-wrap><div class=seek-track><div class=seek-progress id=progress></div></div><input id=seek type=range min=0 max=1000 value=0 aria-label=\"Seek\"></div>"
               "<div class=player-inner><div class=controls><button class=control id=previous type=button aria-label=\"Previous track\"><svg width=17 height=17 viewBox=\"0 0 17 17\" aria-hidden=true><path d=\"M2 2h2v13H2zm3 6.5L15 2v13z\"/></svg></button><button class=control id=play type=button aria-label=\"Play\"><span class=play-symbol></span></button><button class=control id=next type=button aria-label=\"Next track\"><svg width=17 height=17 viewBox=\"0 0 17 17\" aria-hidden=true><path d=\"M13 2h2v13h-2zM2 2l10 6.5L2 15z\"/></svg></button></div><img class=now-art id=now-art alt=\"\">"
               "<div class=now-playing><div class=now-title id=now-title>Choose a track</div><div class=now-subtitle id=now-subtitle>Shared music</div></div><div class=time><span id=current>0:00</span> / <span id=duration>--:--</span></div>"
               "<label class=volume aria-label=\"Volume\"><svg width=18 height=18 viewBox=\"0 0 18 18\" aria-hidden=true><path d=\"M2 7v4h3l4 4V3L5 7zm9.3-1.3a4.6 4.6 0 010 6.6l1.2 1.2a6.3 6.3 0 000-9z\"/></svg><input id=volume type=range min=0 max=1 step=.01 value=.8></label></div></div>"
               "<script>(()=>{const audio=document.getElementById('audio'),tracks=[...document.querySelectorAll('.track')],play=document.getElementById('play'),seek=document.getElementById('seek'),progress=document.getElementById('progress'),current=document.getElementById('current'),duration=document.getElementById('duration'),title=document.getElementById('now-title'),subtitle=document.getElementById('now-subtitle'),art=document.getElementById('now-art');let selected=-1;"
               "const time=n=>Number.isFinite(n)?Math.floor(n/60)+':'+String(Math.floor(n%60)).padStart(2,'0'):'--:--';"
               "function select(i,autoplay=true){if(!tracks.length)return;i=(i+tracks.length)%tracks.length;selected=i;tracks.forEach((t,n)=>{t.classList.toggle('active',n===i);t.querySelector('.track-action').textContent=n===i?'Selected':'Play'});audio.src=tracks[i].dataset.src;title.textContent=tracks[i].dataset.title;subtitle.textContent=tracks[i].dataset.meta||'Shared music';art.src=tracks[i].dataset.art;audio.load();if(autoplay)audio.play().catch(()=>{});}"
               "function update(){const ratio=audio.duration?audio.currentTime/audio.duration:0;seek.value=ratio*1000;progress.style.width=(ratio*100)+'%';current.textContent=time(audio.currentTime);duration.textContent=time(audio.duration)}"
               "tracks.forEach((t,i)=>t.addEventListener('click',()=>{if(i===selected){audio.paused?audio.play():audio.pause()}else select(i)}));"
               "play.addEventListener('click',()=>{if(selected<0)select(0);else audio.paused?audio.play():audio.pause()});document.getElementById('previous').addEventListener('click',()=>select(selected<0?0:selected-1));document.getElementById('next').addEventListener('click',()=>select(selected<0?0:selected+1));"
               "seek.addEventListener('input',()=>{if(audio.duration)audio.currentTime=audio.duration*seek.value/1000});document.getElementById('volume').addEventListener('input',e=>audio.volume=e.target.value);audio.volume=.8;audio.addEventListener('timeupdate',update);audio.addEventListener('durationchange',update);audio.addEventListener('play',()=>{play.classList.add('playing');play.setAttribute('aria-label','Pause');if(selected>=0)tracks[selected].querySelector('.track-action').textContent='Playing'});audio.addEventListener('pause',()=>{play.classList.remove('playing');play.setAttribute('aria-label','Play');if(selected>=0)tracks[selected].querySelector('.track-action').textContent='Paused'});audio.addEventListener('ended',()=>{if(selected<tracks.length-1)select(selected+1);else{audio.currentTime=0;update()}});if(tracks.length)select(0,false)})();</script></body></html>";
    }
}
