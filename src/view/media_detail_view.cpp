/**
 * VitaSuwayomi - Media Detail View implementation
 */

#include "view/media_detail_view.hpp"
#include "app/suwayomi_client.hpp"
#include "view/media_item_cell.hpp"
#include "view/progress_dialog.hpp"
#include "app/application.hpp"
#include "app/downloads_manager.hpp"
#include "app/temp_file_manager.hpp"
#include "utils/image_loader.hpp"
#include "utils/http_client.hpp"
#include "utils/audio_utils.hpp"
#include "utils/async.hpp"
#include <thread>
#include <algorithm>
#include <fstream>

#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

namespace vitasuwayomi {

MediaDetailView::MediaDetailView(const MediaItem& item)
    : m_item(item) {
    brls::Logger::info("MediaDetailView: Creating for '{}' id='{}' type='{}'",
                       item.title, item.id, item.type);

    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Register back button (B/Circle) to pop this activity
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View* view) {
        brls::Application::popActivity();
        return true;
    }, false, false, brls::Sound::SOUND_BACK);

    // Create scrollable content
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_mainContent = new brls::Box();
    m_mainContent->setAxis(brls::Axis::COLUMN);
    m_mainContent->setPadding(30);

    // Top row - poster and info
    auto* topRow = new brls::Box();
    topRow->setAxis(brls::Axis::ROW);
    topRow->setJustifyContent(brls::JustifyContent::FLEX_START);
    topRow->setAlignItems(brls::AlignItems::FLEX_START);
    topRow->setMarginBottom(20);

    // Left side - poster (square for book/podcast covers)
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::COLUMN);
    leftBox->setWidth(460);
    leftBox->setMarginRight(20);

    m_posterImage = new brls::Image();
    m_posterImage->setWidth(200);
    m_posterImage->setHeight(200);  // Square cover
    m_posterImage->setScalingType(brls::ImageScalingType::FIT);
    leftBox->addView(m_posterImage);

    // Play button - for books and podcast episodes
    if (m_item.mediaType == MediaType::BOOK || m_item.mediaType == MediaType::PODCAST_EPISODE) {
        // Horizontal button row for all buttons
        auto* buttonRow = new brls::Box();
        buttonRow->setAxis(brls::Axis::ROW);
        buttonRow->setMarginTop(20);
        buttonRow->setJustifyContent(brls::JustifyContent::FLEX_START);

        m_playButton = new brls::Button();
        m_playButton->setText("Play");
        m_playButton->setWidth(90);
        m_playButton->setHeight(40);
        m_playButton->setMarginRight(10);
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(true);  // Always auto-resume from last position
            return true;
        });
        buttonRow->addView(m_playButton);

        // Download/Delete button for directly playable content
        AppSettings& settings = Application::getInstance().getSettings();
        bool isDownloaded = DownloadsManager::getInstance().isDownloaded(m_item.id);

        if (!settings.saveToDownloads && !isDownloaded) {
            // Show Download button only if not already downloaded and not auto-saving
            m_downloadButton = new brls::Button();
            m_downloadButton->setText("Download");
            m_downloadButton->setWidth(115);
            m_downloadButton->setHeight(40);
            m_downloadButton->setMarginRight(10);
            m_downloadButton->registerClickAction([this](brls::View* view) {
                onDownload();
                return true;
            });
            buttonRow->addView(m_downloadButton);
        }

        if (isDownloaded) {
            // Show Delete button for downloaded items
            m_deleteButton = new brls::Button();
            m_deleteButton->setText("Delete");
            m_deleteButton->setWidth(100);
            m_deleteButton->setHeight(40);
            m_deleteButton->registerClickAction([this](brls::View* view) {
                onDeleteDownload();
                return true;
            });
            buttonRow->addView(m_deleteButton);
        }

        leftBox->addView(buttonRow);
    }

    // For podcasts, show download options
    if (m_item.mediaType == MediaType::PODCAST) {
        // Horizontal button row for all podcast buttons
        auto* podcastButtonRow = new brls::Box();
        podcastButtonRow->setAxis(brls::Axis::ROW);
        podcastButtonRow->setMarginTop(20);
        podcastButtonRow->setJustifyContent(brls::JustifyContent::FLEX_START);

        m_playButton = new brls::Button();
        m_playButton->setText("Play");
        m_playButton->setWidth(90);
        m_playButton->setHeight(40);
        m_playButton->setMarginRight(10);
        m_playButton->registerClickAction([this](brls::View* view) {
            onPlay(false);
            return true;
        });
        podcastButtonRow->addView(m_playButton);

        // Find New Episodes button - check RSS for new episodes
        m_findEpisodesButton = new brls::Button();
        m_findEpisodesButton->setText("Find New");
        m_findEpisodesButton->setWidth(110);
        m_findEpisodesButton->setHeight(40);
        m_findEpisodesButton->setMarginRight(10);
        m_findEpisodesButton->registerClickAction([this](brls::View* view) {
            findNewEpisodes();
            return true;
        });
        podcastButtonRow->addView(m_findEpisodesButton);

        // Download button for podcasts - always show (will be hidden if all episodes are downloaded in loadChildren)
        m_downloadButton = new brls::Button();
        m_downloadButton->setText("Download");
        m_downloadButton->setWidth(130);
        m_downloadButton->setHeight(40);
        m_downloadButton->setMarginRight(10);
        m_downloadButton->registerClickAction([this](brls::View* view) {
            showDownloadOptions();
            return true;
        });
        podcastButtonRow->addView(m_downloadButton);

        // Delete button for downloaded episodes - initially hidden, shown if any episodes downloaded
        m_deleteButton = new brls::Button();
        m_deleteButton->setText("Remove");
        m_deleteButton->setWidth(130);
        m_deleteButton->setHeight(40);
        m_deleteButton->setVisibility(brls::Visibility::GONE);  // Hidden until we check downloads
        m_deleteButton->registerClickAction([this](brls::View* view) {
            deleteAllDownloadedEpisodes();
            return true;
        });
        podcastButtonRow->addView(m_deleteButton);

        leftBox->addView(podcastButtonRow);
    }

    topRow->addView(leftBox);

    // Right side - details
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::COLUMN);
    rightBox->setGrow(1.0f);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText(m_item.title);
    m_titleLabel->setFontSize(26);
    m_titleLabel->setMarginBottom(10);
    rightBox->addView(m_titleLabel);

    // Author/Series info
    if (!m_item.authorName.empty()) {
        auto* authorLabel = new brls::Label();
        authorLabel->setText("By: " + m_item.authorName);
        authorLabel->setFontSize(18);
        authorLabel->setMarginBottom(10);
        rightBox->addView(authorLabel);
    }

    // Metadata row
    auto* metaBox = new brls::Box();
    metaBox->setAxis(brls::Axis::ROW);
    metaBox->setMarginBottom(15);

    if (!m_item.publishedYear.empty()) {
        m_yearLabel = new brls::Label();
        m_yearLabel->setText(m_item.publishedYear);
        m_yearLabel->setFontSize(16);
        m_yearLabel->setMarginRight(15);
        metaBox->addView(m_yearLabel);
    }

    if (m_item.duration > 0) {
        m_durationLabel = new brls::Label();
        int hours = (int)(m_item.duration / 3600.0f);
        int mins = (int)((m_item.duration - hours * 3600) / 60.0f);
        if (hours > 0) {
            m_durationLabel->setText(std::to_string(hours) + "h " + std::to_string(mins) + "m");
        } else {
            m_durationLabel->setText(std::to_string(mins) + " min");
        }
        m_durationLabel->setFontSize(16);
        metaBox->addView(m_durationLabel);
    }

    rightBox->addView(metaBox);

    // Summary/Description
    if (!m_item.description.empty()) {
        m_summaryLabel = new brls::Label();
        m_summaryLabel->setText(m_item.description);
        m_summaryLabel->setFontSize(16);
        m_summaryLabel->setMarginBottom(20);
        rightBox->addView(m_summaryLabel);
    }

    topRow->addView(rightBox);
    m_mainContent->addView(topRow);

    // Episodes container for podcasts
    if (m_item.mediaType == MediaType::PODCAST) {
        auto* episodesLabel = new brls::Label();
        episodesLabel->setText("Episodes");
        episodesLabel->setFontSize(20);
        episodesLabel->setMarginBottom(10);
        m_mainContent->addView(episodesLabel);

        auto* episodesScroll = new brls::HScrollingFrame();
        episodesScroll->setHeight(180);
        episodesScroll->setMarginBottom(20);

        m_childrenBox = new brls::Box();
        m_childrenBox->setAxis(brls::Axis::ROW);
        m_childrenBox->setJustifyContent(brls::JustifyContent::FLEX_START);

        episodesScroll->setContentView(m_childrenBox);
        m_mainContent->addView(episodesScroll);
    }

    // Chapters container for books
    if (m_item.mediaType == MediaType::BOOK) {
        auto* chaptersLabel = new brls::Label();
        chaptersLabel->setText("Chapters");
        chaptersLabel->setFontSize(20);
        chaptersLabel->setMarginBottom(10);
        chaptersLabel->setMarginTop(10);
        m_mainContent->addView(chaptersLabel);

        // Scrollable chapters list
        m_chaptersScroll = new brls::ScrollingFrame();
        m_chaptersScroll->setHeight(250);
        m_chaptersScroll->setMarginBottom(20);

        m_chaptersBox = new brls::Box();
        m_chaptersBox->setAxis(brls::Axis::COLUMN);

        m_chaptersScroll->setContentView(m_chaptersBox);
        m_mainContent->addView(m_chaptersScroll);
    }

    m_scrollView->setContentView(m_mainContent);
    this->addView(m_scrollView);

    // Load full details
    loadDetails();
}

brls::HScrollingFrame* MediaDetailView::createMediaRow(const std::string& title, brls::Box** contentOut) {
    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(20);
    label->setMarginBottom(10);
    label->setMarginTop(15);
    if (m_musicCategoriesBox) {
        m_musicCategoriesBox->addView(label);
    }

    auto* scrollFrame = new brls::HScrollingFrame();
    scrollFrame->setHeight(150);
    scrollFrame->setMarginBottom(10);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::ROW);
    content->setJustifyContent(brls::JustifyContent::FLEX_START);

    scrollFrame->setContentView(content);
    if (m_musicCategoriesBox) {
        m_musicCategoriesBox->addView(scrollFrame);
    }

    if (contentOut) {
        *contentOut = content;
    }

    return scrollFrame;
}

brls::View* MediaDetailView::create() {
    return nullptr; // Factory not used
}

void MediaDetailView::loadDetails() {
    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    // Load full details
    MediaItem fullItem;
    bool loadedFromServer = client.fetchItem(m_item.id, fullItem);

    if (loadedFromServer) {
        m_item = fullItem;

        // Update UI with full details
        if (m_titleLabel && !m_item.title.empty()) {
            m_titleLabel->setText(m_item.title);
        }

        if (m_summaryLabel && !m_item.description.empty()) {
            m_summaryLabel->setText(m_item.description);
        }

        // Update download button state
        if (m_downloadButton && !m_item.audioTracks.empty()) {
            if (DownloadsManager::getInstance().isDownloaded(m_item.id)) {
                m_downloadButton->setText("Downloaded");
            } else {
                m_downloadButton->setText("Download");
            }
        }

        // Create chapters container if it wasn't created in constructor
        // (happens when mediaType wasn't known from library listing)
        if (m_item.mediaType == MediaType::BOOK && !m_chaptersBox && m_mainContent) {
            brls::Logger::debug("Creating chapters container after loading full item details");

            auto* chaptersLabel = new brls::Label();
            chaptersLabel->setText("Chapters");
            chaptersLabel->setFontSize(20);
            chaptersLabel->setMarginBottom(10);
            chaptersLabel->setMarginTop(10);
            m_mainContent->addView(chaptersLabel);

            m_chaptersScroll = new brls::ScrollingFrame();
            m_chaptersScroll->setHeight(250);
            m_chaptersScroll->setMarginBottom(20);

            m_chaptersBox = new brls::Box();
            m_chaptersBox->setAxis(brls::Axis::COLUMN);

            m_chaptersScroll->setContentView(m_chaptersBox);
            m_mainContent->addView(m_chaptersScroll);
        }
    } else {
        // Server fetch failed - try to load metadata from DownloadsManager (offline mode)
        brls::Logger::info("MediaDetailView: Server fetch failed, loading metadata from downloads");
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        auto allDownloads = downloadsMgr.getDownloads();

        // Find matching download (for audiobooks, match itemId; for podcast episodes, we check later)
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED) {
                // For podcasts, we want the first episode's parent info or any episode
                // For audiobooks, this matches directly
                brls::Logger::info("MediaDetailView: Found offline metadata for {}", dl.title);

                // Update title if not already set from m_item
                if (m_titleLabel && !dl.title.empty() && m_item.title.empty()) {
                    m_titleLabel->setText(dl.title);
                    m_item.title = dl.title;
                }

                // Update author
                if (!dl.authorName.empty() && m_item.authorName.empty()) {
                    m_item.authorName = dl.authorName;
                }

                // Update description
                if (m_summaryLabel && !dl.description.empty()) {
                    m_summaryLabel->setText(dl.description);
                    m_item.description = dl.description;
                }

                // Load chapters for audiobooks
                if (!dl.chapters.empty() && m_item.chapters.empty()) {
                    for (const auto& ch : dl.chapters) {
                        Chapter ci;
                        ci.title = ch.title;
                        ci.start = ch.start;
                        ci.end = ch.end;
                        m_item.chapters.push_back(ci);
                    }
                    brls::Logger::info("MediaDetailView: Loaded {} chapters from offline data", m_item.chapters.size());
                }

                // For audiobooks, we only need one match
                if (dl.episodeId.empty()) {
                    break;
                }
            }
        }

        // Create chapters container for audiobooks if we have chapters
        if (m_item.mediaType == MediaType::BOOK && !m_chaptersBox && m_mainContent && !m_item.chapters.empty()) {
            brls::Logger::debug("Creating chapters container for offline audiobook");

            auto* chaptersLabel = new brls::Label();
            chaptersLabel->setText("Chapters");
            chaptersLabel->setFontSize(20);
            chaptersLabel->setMarginBottom(10);
            chaptersLabel->setMarginTop(10);
            m_mainContent->addView(chaptersLabel);

            m_chaptersScroll = new brls::ScrollingFrame();
            m_chaptersScroll->setHeight(250);
            m_chaptersScroll->setMarginBottom(20);

            m_chaptersBox = new brls::Box();
            m_chaptersBox->setAxis(brls::Axis::COLUMN);

            m_chaptersScroll->setContentView(m_chaptersBox);
            m_mainContent->addView(m_chaptersScroll);
        }
    }

    // Load thumbnail - try local cover first if offline or for downloaded content
    if (m_posterImage && !m_item.id.empty()) {
        // Check if we have a local cover from downloads
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        std::string localCoverPath = downloadsMgr.getLocalCoverPath(m_item.id);

        if (!localCoverPath.empty()) {
            // Load local cover
            brls::Logger::info("MediaDetailView: Loading local cover: {}", localCoverPath);
            loadLocalCover(localCoverPath);
        } else if (loadedFromServer) {
            // Fetch from server
            std::string url = client.getCoverUrl(m_item.id, 400, 400);
            ImageLoader::loadAsync(url, [this](brls::Image* image) {
                // Image loaded
            }, m_posterImage);
        }
        // If offline and no local cover, leave poster empty
    }

    // Load children (podcast episodes)
    if (m_item.mediaType == MediaType::PODCAST) {
        loadChildren();
    }

    // Populate chapters for audiobooks
    if (m_item.mediaType == MediaType::BOOK) {
        brls::Logger::debug("Populating chapters for book: {} chapters available", m_item.chapters.size());
        populateChapters();
    }
}

void MediaDetailView::loadChildren() {
    if (!m_childrenBox) return;

    AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

    bool loadedFromServer = client.fetchPodcastEpisodes(m_item.id, m_children);

    // If server fetch failed, try to load downloaded episodes from DownloadsManager
    if (!loadedFromServer) {
        brls::Logger::info("MediaDetailView: Server fetch failed, loading downloaded episodes");
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        auto allDownloads = downloadsMgr.getDownloads();

        m_children.clear();
        for (const auto& dl : allDownloads) {
            if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED && !dl.episodeId.empty()) {
                // Create a MediaItem from the download info
                MediaItem episode;
                episode.id = dl.episodeId;
                episode.podcastId = dl.itemId;
                episode.episodeId = dl.episodeId;
                episode.title = dl.title;
                episode.authorName = dl.authorName;
                episode.duration = dl.duration;
                episode.mediaType = MediaType::PODCAST_EPISODE;
                episode.isDownloaded = true;
                // Use local cover path if available
                if (!dl.localCoverPath.empty()) {
                    episode.coverPath = dl.localCoverPath;
                } else {
                    episode.coverPath = dl.coverUrl;
                }
                m_children.push_back(episode);
                brls::Logger::debug("MediaDetailView: Added downloaded episode: {}", dl.title);
            }
        }

        if (!m_children.empty()) {
            brls::Logger::info("MediaDetailView: Found {} downloaded episodes for podcast", m_children.size());
        }
    }

    if (!m_children.empty()) {
        m_childrenBox->clearViews();

        for (const auto& child : m_children) {
            auto* cell = new MediaItemCell();
            cell->setItem(child);
            cell->setWidth(120);
            cell->setHeight(150);
            cell->setMarginRight(10);

            cell->registerClickAction([this, child](brls::View* view) {
                // Navigate to episode detail or play directly
                if (child.mediaType == MediaType::PODCAST_EPISODE) {
                    // Download then play
                    startDownloadAndPlay(child.podcastId, child.episodeId);
                } else {
                    auto* detailView = new MediaDetailView(child);
                    brls::Application::pushActivity(new brls::Activity(detailView));
                }
                return true;
            });

            m_childrenBox->addView(cell);
        }

        // Update podcast download/delete button visibility now that we know the episodes
        if (m_item.mediaType == MediaType::PODCAST) {
            bool allDownloaded = areAllEpisodesDownloaded();
            bool anyDownloaded = hasAnyDownloadedEpisodes();

            // Hide download button if all episodes are downloaded
            if (m_downloadButton) {
                if (allDownloaded) {
                    m_downloadButton->setVisibility(brls::Visibility::GONE);
                } else {
                    m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
                }
            }

            // Show delete button only if any episodes are downloaded
            if (m_deleteButton) {
                if (anyDownloaded) {
                    m_deleteButton->setVisibility(brls::Visibility::VISIBLE);
                } else {
                    m_deleteButton->setVisibility(brls::Visibility::GONE);
                }
            }
        }
    }
}

void MediaDetailView::loadMusicCategories() {
    // Not used for Audiobookshelf
}

void MediaDetailView::loadLocalCover(const std::string& localPath) {
    if (localPath.empty() || !m_posterImage) return;

#ifdef __vita__
    SceUID fd = sceIoOpen(localPath.c_str(), SCE_O_RDONLY, 0);
    if (fd >= 0) {
        SceOff size = sceIoLseek(fd, 0, SCE_SEEK_END);
        sceIoLseek(fd, 0, SCE_SEEK_SET);

        if (size > 0 && size < 10 * 1024 * 1024) {  // Max 10MB
            std::vector<uint8_t> data(size);
            if (sceIoRead(fd, data.data(), size) == size) {
                m_posterImage->setImageFromMem(data.data(), data.size());
                brls::Logger::debug("MediaDetailView: Local cover loaded ({} bytes)", size);
            }
        }
        sceIoClose(fd);
    } else {
        brls::Logger::warning("MediaDetailView: Failed to open local cover: {}", localPath);
    }
#else
    // Non-Vita: use standard file I/O
    std::ifstream file(localPath, std::ios::binary | std::ios::ate);
    if (file.is_open()) {
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (size > 0 && size < 10 * 1024 * 1024) {
            std::vector<uint8_t> data(size);
            if (file.read(reinterpret_cast<char*>(data.data()), size)) {
                m_posterImage->setImageFromMem(data.data(), data.size());
                brls::Logger::debug("MediaDetailView: Local cover loaded ({} bytes)", size);
            }
        }
        file.close();
    }
#endif
}

void MediaDetailView::populateChapters() {
    if (!m_chaptersBox) return;

    m_chaptersBox->clearViews();

    // Check if we have chapters
    if (m_item.chapters.empty()) {
        // Show message if no chapters
        auto* noChaptersLabel = new brls::Label();
        noChaptersLabel->setText("No chapter information available");
        noChaptersLabel->setFontSize(14);
        noChaptersLabel->setTextColor(nvgRGB(150, 150, 150));
        noChaptersLabel->setMarginTop(10);
        m_chaptersBox->addView(noChaptersLabel);
        return;
    }

    // Helper to format time
    auto formatTime = [](float seconds) -> std::string {
        int totalSec = static_cast<int>(seconds);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;

        char buf[32];
        if (hours > 0) {
            snprintf(buf, sizeof(buf), "%d:%02d:%02d", hours, mins, secs);
        } else {
            snprintf(buf, sizeof(buf), "%d:%02d", mins, secs);
        }
        return std::string(buf);
    };

    // Current playback position for highlighting
    float currentTime = m_item.currentTime;

    for (size_t i = 0; i < m_item.chapters.size(); ++i) {
        const auto& chapter = m_item.chapters[i];

        auto* chapterRow = new brls::Box();
        chapterRow->setAxis(brls::Axis::ROW);
        chapterRow->setAlignItems(brls::AlignItems::CENTER);
        chapterRow->setHeight(50);
        chapterRow->setMarginBottom(8);
        chapterRow->setPadding(12);
        chapterRow->setCornerRadius(6);
        chapterRow->setFocusable(true);

        // Highlight current chapter
        bool isCurrentChapter = (currentTime >= chapter.start && currentTime < chapter.end);
        if (isCurrentChapter) {
            chapterRow->setBackgroundColor(nvgRGBA(80, 120, 80, 255));
        } else {
            chapterRow->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
        }

        // Chapter number
        auto* numLabel = new brls::Label();
        numLabel->setText(std::to_string(i + 1));
        numLabel->setFontSize(14);
        numLabel->setWidth(35);
        numLabel->setTextColor(nvgRGB(150, 150, 150));
        chapterRow->addView(numLabel);

        // Chapter title
        auto* titleLabel = new brls::Label();
        std::string title = chapter.title;
        if (title.empty()) {
            title = "Chapter " + std::to_string(i + 1);
        }
        // Truncate if too long
        if (title.length() > 45) {
            title = title.substr(0, 42) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        titleLabel->setGrow(1.0f);
        chapterRow->addView(titleLabel);

        // Duration/time
        auto* timeLabel = new brls::Label();
        float duration = chapter.end - chapter.start;
        timeLabel->setText(formatTime(chapter.start) + " (" + formatTime(duration) + ")");
        timeLabel->setFontSize(13);
        timeLabel->setTextColor(nvgRGB(150, 150, 150));
        chapterRow->addView(timeLabel);

        // Click to play from chapter
        std::string itemId = m_item.id;
        float chapterStart = chapter.start;
        chapterRow->registerClickAction([this, itemId, chapterStart](brls::View*) {
            // Start playback from this chapter's start time (with download)
            startDownloadAndPlay(itemId, "", chapterStart);
            return true;
        });

        m_chaptersBox->addView(chapterRow);
    }
}

void MediaDetailView::onPlay(bool resume) {
    // For podcasts, handle episode selection first
    if (m_item.mediaType == MediaType::PODCAST) {
        std::string podcastId, episodeId;
        if (!m_children.empty()) {
            podcastId = m_children[0].podcastId;
            episodeId = m_children[0].episodeId;
        } else {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            std::vector<MediaItem> episodes;
            if (client.fetchPodcastEpisodes(m_item.id, episodes) && !episodes.empty()) {
                podcastId = episodes[0].podcastId;
                episodeId = episodes[0].episodeId;
            } else {
                brls::Application::notify("No episodes available");
                return;
            }
        }
        // Start download for podcast episode
        startDownloadAndPlay(podcastId, episodeId);
        return;
    }

    // For books and podcast episodes, start download
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    startDownloadAndPlay(itemId, episodeId);
}

void MediaDetailView::startDownloadAndPlay(const std::string& itemId, const std::string& episodeId,
                                            float requestedStartTime, bool downloadOnly) {
    brls::Logger::info("startDownloadAndPlay: itemId={}, episodeId={}, startTime={}, downloadOnly={}",
                       itemId, episodeId, requestedStartTime, downloadOnly);

    // Get settings
    AppSettings& settings = Application::getInstance().getSettings();
    // If downloadOnly mode, always save to downloads folder
    bool useDownloads = downloadOnly ? true : settings.saveToDownloads;

    // Initialize managers
    TempFileManager& tempMgr = TempFileManager::getInstance();
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    tempMgr.init();
    downloadsMgr.init();

    // Check if we have a cached version - ALWAYS check downloads first, then temp
    std::string cachedPath;
    bool isFromDownloads = false;

    // First check downloads folder (regardless of saveToDownloads setting)
    if (downloadsMgr.isDownloaded(itemId, episodeId)) {
        cachedPath = downloadsMgr.getPlaybackPath(itemId);
        isFromDownloads = true;
        brls::Logger::info("Found in downloads: {}", cachedPath);
    }

    // If not in downloads, check temp cache
    if (cachedPath.empty()) {
        cachedPath = tempMgr.getCachedFilePath(itemId, episodeId);
        if (!cachedPath.empty()) {
            // If save to downloads is enabled and we're playing (not download only),
            // delete the temp file and re-download to downloads folder
            if (useDownloads && !downloadOnly) {
                brls::Logger::info("Deleting temp file to re-download to downloads folder: {}", cachedPath);
                tempMgr.deleteTempFile(itemId, episodeId);
                cachedPath.clear();
            } else {
                tempMgr.touchTempFile(itemId, episodeId);
                brls::Logger::info("Found in temp cache: {}", cachedPath);
            }
        }
    }

    // If cached, play immediately (fetch progress from server first if online)
    if (!cachedPath.empty()) {
        brls::Logger::info("Using cached file: {}", cachedPath);

        // Fetch latest progress from server before playing
        float startTime = requestedStartTime;
        if (startTime < 0) {
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.isAuthenticated()) {
                float serverTime = 0.0f, serverProgress = 0.0f;
                bool serverFinished = false;
                if (client.getProgress(itemId, serverTime, serverProgress, serverFinished, episodeId)) {
                    startTime = serverTime;
                    brls::Logger::info("Fetched progress from server: {}s", startTime);
                }
            }

            // Also check local progress if from downloads
            if (isFromDownloads && startTime <= 0) {
                DownloadItem* download = downloadsMgr.getDownload(itemId);
                if (download && download->currentTime > 0) {
                    startTime = download->currentTime;
                    brls::Logger::info("Using local download progress: {}s", startTime);
                }
            }
        }

        Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, cachedPath, startTime);
        return;
    }

    // Need to download - show progress dialog
    auto* progressDialog = ProgressDialog::showDownloading(m_item.title);

    // Store necessary data for the async operation
    std::string title = m_item.title;
    std::string authorName = m_item.authorName;
    std::string itemType = m_item.type;
    float duration = m_item.duration;
    std::string description = m_item.description;
    std::string coverUrl = AudiobookshelfClient::getInstance().getCoverUrl(itemId);

    // Copy chapters for offline use
    std::vector<DownloadChapter> downloadChapters;
    for (const auto& ch : m_item.chapters) {
        DownloadChapter dch;
        dch.title = ch.title;
        dch.start = ch.start;
        dch.end = ch.end;
        downloadChapters.push_back(dch);
    }

    // Run download in background
    asyncRun([this, progressDialog, itemId, episodeId, title, authorName, itemType, duration, useDownloads, requestedStartTime, coverUrl, description, downloadChapters, downloadOnly]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Start playback session
        PlaybackSession session;
        if (!client.startPlaybackSession(itemId, session, episodeId)) {
            brls::Logger::error("Failed to start playback session");
            brls::sync([progressDialog]() {
                progressDialog->setStatus("Failed to start session");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
            return;
        }

        brls::Logger::info("Session started: {} tracks, currentTime={}s, duration={}s",
                          session.audioTracks.size(), session.currentTime, session.duration);

        // Determine file extension and multi-file status
        bool isMultiFile = session.audioTracks.size() > 1;
        std::string mimeType = "audio/mpeg";
        if (!session.audioTracks.empty() && !session.audioTracks[0].mimeType.empty()) {
            mimeType = session.audioTracks[0].mimeType;
        }

        std::string ext = ".mp3";
        if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos ||
            mimeType.find("m4b") != std::string::npos) {
            ext = ".m4a";
        } else if (mimeType.find("flac") != std::string::npos) {
            ext = ".flac";
        } else if (mimeType.find("ogg") != std::string::npos) {
            ext = ".ogg";
        }

        // For multi-file audiobooks, use mp4 container only for m4a sources
        // For mp3/ogg/flac sources, keep the same format (Vita FFmpeg lacks mp4 muxer)
        std::string finalExt = ext;
        if (isMultiFile && ext == ".m4a") {
            finalExt = ".m4b";  // Only use m4b for m4a sources
        }

        // Determine destination path
        TempFileManager& tempMgr = TempFileManager::getInstance();
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        std::string destPath;

        if (useDownloads) {
            std::string filename = itemId;
            if (!episodeId.empty()) {
                filename += "_" + episodeId;
            }
            filename += finalExt;
            destPath = downloadsMgr.getDownloadsPath() + "/" + filename;
        } else {
            tempMgr.cleanupTempFiles();  // Clean up before downloading
            destPath = tempMgr.getTempFilePath(itemId, episodeId, finalExt);
        }

        // Use requested start time if specified, otherwise use session's current time
        float startTime = (requestedStartTime >= 0) ? requestedStartTime : session.currentTime;
        brls::Logger::info("Using startTime={}s (requested={}, session={}s)",
                          startTime, requestedStartTime, session.currentTime);

#ifdef __vita__
        HttpClient httpClient;
        httpClient.setTimeout(300);  // 5 minute timeout
        int64_t totalDownloaded = 0;
        bool downloadSuccess = true;

        if (isMultiFile) {
            // Multi-file audiobook handling
            int numTracks = static_cast<int>(session.audioTracks.size());
            std::vector<std::string> trackFiles;

            // Check if combined file already exists on disk (from previous incomplete registration)
            std::string combinedPath;
            if (useDownloads) {
                combinedPath = downloadsMgr.getDownloadsPath() + "/" + itemId + finalExt;
            } else {
                combinedPath = tempMgr.getTempFilePath(itemId, episodeId, finalExt);
            }

            SceIoStat existingStat;
            if (sceIoGetstat(combinedPath.c_str(), &existingStat) >= 0 && existingStat.st_size > 0) {
                brls::Logger::info("Found existing combined file: {} ({} bytes)", combinedPath, existingStat.st_size);

                // Register it if not already registered
                if (useDownloads) {
                    downloadsMgr.registerCompletedDownload(itemId, episodeId, title, authorName,
                        combinedPath, existingStat.st_size, duration, itemType, coverUrl, description, downloadChapters);
                } else {
                    tempMgr.registerTempFile(itemId, episodeId, combinedPath, title, existingStat.st_size);
                }

                // Play the existing file
                float seekTime = (requestedStartTime >= 0) ? requestedStartTime : session.currentTime;
                brls::sync([progressDialog, itemId, episodeId, combinedPath, seekTime]() {
                    progressDialog->dismiss();
                    Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, combinedPath, seekTime);
                });
                return;
            }

            if (downloadOnly) {
                // Download-only mode: download ALL tracks first, combine, register, no playback
                brls::Logger::info("Download-only mode: Downloading all {} tracks for multi-file audiobook", numTracks);

                // Download all tracks sequentially
                for (int trackIdx = 0; trackIdx < numTracks && downloadSuccess; trackIdx++) {
                    const AudioTrack& track = session.audioTracks[trackIdx];
                    std::string trackUrl = client.getStreamUrl(track.contentUrl, "");

                    if (trackUrl.empty()) {
                        brls::Logger::error("Failed to get URL for track {}", trackIdx);
                        downloadSuccess = false;
                        break;
                    }

                    std::string trackExt = ext;
                    if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                        track.mimeType.find("m4a") != std::string::npos)) {
                        trackExt = ".m4a";
                    }

                    std::string trackPath = tempMgr.getTempFilePath(itemId + "_track" + std::to_string(trackIdx), "", trackExt);
                    trackFiles.push_back(trackPath);

                    int currentTrack = trackIdx;
                    brls::sync([progressDialog, currentTrack, numTracks]() {
                        char buf[64];
                        snprintf(buf, sizeof(buf), "Downloading track %d/%d...", currentTrack + 1, numTracks);
                        progressDialog->setStatus(buf);
                    });

                    SceUID fd = sceIoOpen(trackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                    if (fd < 0) {
                        brls::Logger::error("Failed to create track file: {}", trackPath);
                        downloadSuccess = false;
                        break;
                    }

                    int64_t trackDownloaded = 0;
                    int64_t trackSize = 0;

                    bool trackOk = httpClient.downloadFile(trackUrl,
                        [&](const char* data, size_t size) -> bool {
                            int written = sceIoWrite(fd, data, size);
                            if (written < 0) return false;
                            trackDownloaded += size;

                            // Update progress for this track
                            if (trackSize > 0) {
                                int64_t currentTrackNum = currentTrack;
                                brls::sync([progressDialog, trackDownloaded, trackSize, currentTrackNum, numTracks]() {
                                    char buf[96];
                                    int percent = static_cast<int>((trackDownloaded * 100) / trackSize);
                                    int dlMB = static_cast<int>(trackDownloaded / (1024 * 1024));
                                    int totalMB = static_cast<int>(trackSize / (1024 * 1024));
                                    snprintf(buf, sizeof(buf), "Track %d/%d: %d%% (%d/%d MB)",
                                            static_cast<int>(currentTrackNum) + 1, numTracks, percent, dlMB, totalMB);
                                    progressDialog->setStatus(buf);
                                });
                            }
                            return true;
                        },
                        [&](int64_t size) {
                            if (size > 0) trackSize = size;
                        }
                    );

                    sceIoClose(fd);

                    if (!trackOk) {
                        brls::Logger::error("Failed to download track {}", trackIdx);
                        sceIoRemove(trackPath.c_str());
                        downloadSuccess = false;
                    } else {
                        totalDownloaded += trackDownloaded;
                        brls::Logger::info("Track {}/{} complete ({} bytes)", trackIdx + 1, numTracks, trackDownloaded);
                    }
                }

                // Combine all tracks if all downloaded successfully
                if (downloadSuccess) {
                    brls::sync([progressDialog]() {
                        progressDialog->setStatus("Combining audio files...");
                    });

                    brls::Logger::info("Combining {} tracks into {}", numTracks, destPath);

                    int totalTracks = numTracks;
                    if (concatenateAudioFiles(trackFiles, destPath, [progressDialog, totalTracks](int current, int total) {
                        brls::sync([progressDialog, current, totalTracks]() {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Combining file %d/%d...", current, totalTracks);
                            progressDialog->setStatus(buf);
                        });
                    })) {
                        brls::Logger::info("Successfully combined {} tracks", numTracks);

                        // Get final file size
                        SceIoStat stat;
                        if (sceIoGetstat(destPath.c_str(), &stat) >= 0) {
                            totalDownloaded = stat.st_size;
                        }

                        // Clean up individual track files
                        for (const auto& trackFile : trackFiles) {
                            sceIoRemove(trackFile.c_str());
                        }
                    } else {
                        brls::Logger::error("Failed to combine tracks");
                        downloadSuccess = false;
                        // Clean up partial files
                        for (const auto& trackFile : trackFiles) {
                            sceIoRemove(trackFile.c_str());
                        }
                    }
                } else {
                    // Clean up partial downloads
                    for (const auto& trackFile : trackFiles) {
                        if (!trackFile.empty()) {
                            sceIoRemove(trackFile.c_str());
                        }
                    }
                }

                // Continue to the result handling below (will register download and update UI)

            } else {
                // Play mode: download track containing resume position, start playback, download rest in background
                std::string currentTrackPath;
                int currentTrackIdx = 0;
                float trackSeekTime = 0.0f;  // Initialize to 0 for safety
                bool foundTrack = false;

                brls::Logger::info("Multi-file play mode: Finding track for resume position {}s", startTime);

                // Find which track contains the current playback position
                for (size_t i = 0; i < session.audioTracks.size(); i++) {
                    const AudioTrack& track = session.audioTracks[i];
                    float trackEnd = track.startOffset + track.duration;

                    brls::Logger::debug("Track {}: startOffset={}s, duration={}s, end={}s",
                                       i, track.startOffset, track.duration, trackEnd);

                    if (startTime >= track.startOffset && startTime < trackEnd) {
                        currentTrackIdx = static_cast<int>(i);
                        trackSeekTime = startTime - track.startOffset;
                        foundTrack = true;
                        brls::Logger::info("Resume position {}s is in track {} (offset {}s, seek within track {}s)",
                                          startTime, currentTrackIdx + 1, track.startOffset, trackSeekTime);
                        break;
                    }
                }

                // If no track found (startTime is 0 or beyond all tracks), use first track from start
                if (!foundTrack) {
                    currentTrackIdx = 0;
                    trackSeekTime = 0.0f;
                    brls::Logger::info("No matching track found for {}s, starting from track 1 at 0s", startTime);
                }

                // Download track containing resume position first, then rest in background
                brls::Logger::info("Downloading track {}/{} for multi-file audiobook (seek to {}s within track)",
                                  currentTrackIdx + 1, numTracks, trackSeekTime);

                // First, download the track containing resume position so we can start playing
                {
                    const AudioTrack& track = session.audioTracks[currentTrackIdx];
                    std::string trackUrl = client.getStreamUrl(track.contentUrl, "");

                    if (trackUrl.empty()) {
                        brls::Logger::error("Failed to get URL for current track {}", currentTrackIdx);
                        downloadSuccess = false;
                    } else {
                        std::string trackExt = ext;
                        if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                            track.mimeType.find("m4a") != std::string::npos)) {
                            trackExt = ".m4a";
                        }

                        currentTrackPath = tempMgr.getTempFilePath(itemId + "_track" + std::to_string(currentTrackIdx), "", trackExt);

                        brls::sync([progressDialog, currentTrackIdx, numTracks]() {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Downloading track %d/%d...", currentTrackIdx + 1, numTracks);
                            progressDialog->setStatus(buf);
                        });

                        SceUID fd = sceIoOpen(currentTrackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                        if (fd >= 0) {
                            int64_t totalSize = 0;
                            downloadSuccess = httpClient.downloadFile(trackUrl,
                                [&](const char* data, size_t size) -> bool {
                                    int written = sceIoWrite(fd, data, size);
                                    if (written < 0) return false;
                                    totalDownloaded += size;
                                    if (totalSize > 0) {
                                        brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                            progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                        });
                                    }
                                    return true;
                                },
                                [&](int64_t size) { totalSize = size; }
                            );
                            sceIoClose(fd);
                            if (!downloadSuccess) {
                                sceIoRemove(currentTrackPath.c_str());
                            }
                        } else {
                            downloadSuccess = false;
                        }
                    }
                }

                if (downloadSuccess) {
                    // Start playback immediately with the current track
                    float seekTime = trackSeekTime;
                    brls::sync([progressDialog, itemId, episodeId, currentTrackPath, seekTime]() {
                        progressDialog->dismiss();
                        Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, currentTrackPath, seekTime);
                    });

                    // Now download remaining tracks in background and combine when done
                    std::vector<AudioTrack> allTracks = session.audioTracks;
                    std::string baseExt = ext;
                    std::string combinedExt = finalExt;  // Use the correct extension based on source format
                    std::string finalPath = useDownloads
                        ? downloadsMgr.getDownloadsPath() + "/" + itemId + combinedExt
                        : tempMgr.getTempFilePath(itemId, episodeId, combinedExt);

                    asyncRunLargeStack([allTracks, currentTrackIdx, currentTrackPath, itemId, episodeId, baseExt, finalPath, title, authorName, duration, itemType, useDownloads, coverUrl, description, downloadChapters]() {
                        brls::Logger::info("Background: Downloading remaining tracks...");

                        HttpClient bgHttpClient;
                        bgHttpClient.setTimeout(300);
                        TempFileManager& bgTempMgr = TempFileManager::getInstance();
                        DownloadsManager& bgDownloadsMgr = DownloadsManager::getInstance();

                        std::vector<std::string> allTrackFiles(allTracks.size());
                        allTrackFiles[currentTrackIdx] = currentTrackPath;

                        // Track progress by track count (we don't have size info from AudioTrack)
                        int tracksDownloaded = 1;  // Already downloaded current track
                        int totalTracksToDownload = static_cast<int>(allTracks.size());

                        // Set initial background progress
                        BackgroundDownloadProgress bgProgress;
                        bgProgress.active = true;
                        bgProgress.itemId = itemId;
                        bgProgress.currentTrack = currentTrackIdx + 1;
                        bgProgress.totalTracks = totalTracksToDownload;
                        bgProgress.downloadedBytes = 0;  // Will track per-track progress
                        bgProgress.totalBytes = 0;       // Unknown total
                        bgProgress.status = "Downloading remaining tracks...";
                        Application::getInstance().setBackgroundDownloadProgress(bgProgress);

                        // Download all other tracks
                        AudiobookshelfClient& bgClient = AudiobookshelfClient::getInstance();
                        bool allDownloaded = true;

                        for (size_t i = 0; i < allTracks.size() && allDownloaded; i++) {
                            if (static_cast<int>(i) == currentTrackIdx) continue;  // Already downloaded

                            const AudioTrack& track = allTracks[i];
                            std::string trackUrl = bgClient.getStreamUrl(track.contentUrl, "");

                            if (trackUrl.empty()) {
                                brls::Logger::error("Background: Failed to get URL for track {}", i);
                                allDownloaded = false;
                                break;
                            }

                            std::string trackExt = baseExt;
                            if (!track.mimeType.empty() && (track.mimeType.find("mp4") != std::string::npos ||
                                track.mimeType.find("m4a") != std::string::npos)) {
                                trackExt = ".m4a";
                            }

                            std::string trackPath = bgTempMgr.getTempFilePath(itemId + "_track" + std::to_string(i), "", trackExt);
                            allTrackFiles[i] = trackPath;

                            brls::Logger::info("Background: Downloading track {}/{}...", i + 1, allTracks.size());

                            // Update background progress
                            bgProgress.currentTrack = static_cast<int>(i) + 1;
                            char statusBuf[64];
                            snprintf(statusBuf, sizeof(statusBuf), "Downloading track %d/%d...",
                                    static_cast<int>(i) + 1, static_cast<int>(allTracks.size()));
                            bgProgress.status = statusBuf;
                            Application::getInstance().setBackgroundDownloadProgress(bgProgress);

                            SceUID fd = sceIoOpen(trackPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                            if (fd < 0) {
                                allDownloaded = false;
                                break;
                            }

                            int64_t trackDownloaded = 0;
                            int64_t trackTotalSize = 0;
                            bool trackOk = bgHttpClient.downloadFile(trackUrl,
                                [&](const char* data, size_t size) -> bool {
                                    int written = sceIoWrite(fd, data, size);
                                    if (written < 0) return false;
                                    trackDownloaded += size;
                                    // Update progress periodically (every 64KB)
                                    if ((trackDownloaded % (64 * 1024)) < size) {
                                        bgProgress.downloadedBytes = trackDownloaded;
                                        bgProgress.totalBytes = trackTotalSize;
                                        Application::getInstance().setBackgroundDownloadProgress(bgProgress);
                                    }
                                    return true;
                                },
                                [&](int64_t size) {
                                    if (size > 0) trackTotalSize = size;
                                }
                            );

                            sceIoClose(fd);

                            if (!trackOk) {
                                brls::Logger::error("Background: Failed to download track {}", i);
                                sceIoRemove(trackPath.c_str());
                                allDownloaded = false;
                            } else {
                                tracksDownloaded++;
                                bgProgress.downloadedBytes = 0;  // Reset for next track
                                bgProgress.totalBytes = 0;
                                Application::getInstance().setBackgroundDownloadProgress(bgProgress);
                            }
                        }

                        // Combine all tracks if all downloaded successfully
                        if (allDownloaded) {
                            brls::Logger::info("Background: Combining {} tracks...", allTracks.size());

                            bgProgress.status = "Combining audio files...";
                            Application::getInstance().setBackgroundDownloadProgress(bgProgress);

                            int totalFiles = static_cast<int>(allTrackFiles.size());
                            if (concatenateAudioFiles(allTrackFiles, finalPath, [&bgProgress, totalFiles](int current, int total) {
                                char statusBuf[64];
                                snprintf(statusBuf, sizeof(statusBuf), "Combining file %d/%d...", current, totalFiles);
                                bgProgress.status = statusBuf;
                                bgProgress.currentTrack = current;
                                bgProgress.totalTracks = totalFiles;
                                Application::getInstance().setBackgroundDownloadProgress(bgProgress);
                            })) {
                                brls::Logger::info("Background: Successfully combined into {}", finalPath);

                                // Get total file size
                                SceIoStat stat;
                                int64_t totalSize = 0;
                                if (sceIoGetstat(finalPath.c_str(), &stat) >= 0) {
                                    totalSize = stat.st_size;
                                }

                                // Register the combined file
                                if (useDownloads) {
                                    bgDownloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                                        authorName, finalPath, totalSize, duration, itemType,
                                        coverUrl, description, downloadChapters);
                                } else {
                                    bgTempMgr.registerTempFile(itemId, episodeId, finalPath, title, totalSize);
                                }

                                // Clean up individual track files
                                for (const auto& trackFile : allTrackFiles) {
                                    sceIoRemove(trackFile.c_str());
                                }

                                // Clear background progress
                                Application::getInstance().clearBackgroundDownloadProgress();

                                brls::sync([]() {
                                    brls::Application::notify("Audiobook fully downloaded");
                                });
                            } else {
                                brls::Logger::error("Background: Failed to combine tracks");
                                Application::getInstance().clearBackgroundDownloadProgress();
                            }
                        } else {
                            // Clean up partial downloads
                            for (const auto& trackFile : allTrackFiles) {
                                if (!trackFile.empty()) {
                                    sceIoRemove(trackFile.c_str());
                                }
                            }
                            Application::getInstance().clearBackgroundDownloadProgress();
                        }
                    });

                    // Return early - playback already started
                    return;
                }
            }

        } else {
            // Single file download
            std::string streamUrl;
            if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
                streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            } else {
                streamUrl = client.getDirectStreamUrl(itemId, 0);
            }

            if (streamUrl.empty()) {
                brls::Logger::error("Failed to get stream URL");
                downloadSuccess = false;
            } else {
                SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
                if (fd < 0) {
                    brls::Logger::error("Failed to create file: {}", destPath);
                    downloadSuccess = false;
                } else {
                    int64_t totalSize = 0;

                    downloadSuccess = httpClient.downloadFile(streamUrl,
                        [&](const char* data, size_t size) -> bool {
                            int written = sceIoWrite(fd, data, size);
                            if (written < 0) return false;
                            totalDownloaded += size;

                            // Update progress with speed display
                            if (totalSize > 0) {
                                brls::sync([progressDialog, totalDownloaded, totalSize]() {
                                    progressDialog->updateDownloadProgress(totalDownloaded, totalSize);
                                });
                            }
                            return true;
                        },
                        [&](int64_t size) {
                            totalSize = size;
                            brls::Logger::info("File size: {} bytes", size);
                        }
                    );

                    sceIoClose(fd);

                    if (!downloadSuccess) {
                        sceIoRemove(destPath.c_str());
                    }
                }
            }
        }

        // Handle result
        if (downloadSuccess) {
            // Register the file
            if (useDownloads) {
                downloadsMgr.registerCompletedDownload(itemId, episodeId, title,
                    authorName, destPath, totalDownloaded, duration, itemType,
                    coverUrl, description, downloadChapters);
            } else {
                tempMgr.registerTempFile(itemId, episodeId, destPath, title, totalDownloaded);
            }

            brls::Logger::info("Download complete: {}", destPath);

            if (downloadOnly) {
                // Download only mode - just show success and update button
                brls::sync([progressDialog, this]() {
                    progressDialog->setStatus("Download complete!");
                    if (m_downloadButton) m_downloadButton->setText("Downloaded");
                    brls::delay(1500, [progressDialog]() { progressDialog->dismiss(); });
                });
            } else {
                // Push to player with pre-downloaded file
                float seekTime = startTime;
                brls::sync([progressDialog, itemId, episodeId, destPath, seekTime]() {
                    progressDialog->dismiss();
                    Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, destPath, seekTime);
                });
            }
        } else {
            brls::sync([progressDialog, downloadOnly, this]() {
                progressDialog->setStatus("Download failed");
                if (downloadOnly && m_downloadButton) m_downloadButton->setText("Download");
                brls::delay(2000, [progressDialog]() { progressDialog->dismiss(); });
            });
        }
#else
        // Non-Vita: just use streaming URL directly
        std::string streamUrl;
        if (!session.audioTracks.empty() && !session.audioTracks[0].contentUrl.empty()) {
            streamUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
        } else {
            streamUrl = client.getDirectStreamUrl(itemId, 0);
        }

        brls::sync([progressDialog, itemId, episodeId, streamUrl, startTime]() {
            progressDialog->dismiss();
            Application::getInstance().pushPlayerActivityWithFile(itemId, episodeId, streamUrl, startTime);
        });
#endif
    });
}

void MediaDetailView::onDownload() {
    brls::Logger::info("onDownload called for item: {} ({})", m_item.title, m_item.id);

    // Use the same download logic as streaming but save to downloads
    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string episodeId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.episodeId : "";

    // Check if already downloaded (check both itemId and episodeId)
    if (DownloadsManager::getInstance().isDownloaded(itemId, episodeId)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    startDownloadOnly(itemId, episodeId);
}

void MediaDetailView::onDeleteDownload() {
    brls::Logger::info("onDeleteDownload called for item: {} ({})", m_item.title, m_item.id);

    std::string itemId = (m_item.mediaType == MediaType::PODCAST_EPISODE) ? m_item.podcastId : m_item.id;
    std::string title = m_item.title;

    // Show confirmation dialog
    brls::Dialog* dialog = new brls::Dialog("Delete downloaded content?\n\n\"" + title + "\"");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Delete", [dialog, itemId, this]() {
        DownloadsManager::getInstance().deleteDownload(itemId);
        brls::Application::notify("Download deleted");

        // Update UI - hide delete button
        if (m_deleteButton) {
            m_deleteButton->setVisibility(brls::Visibility::GONE);
        }

        // Show download button if saveToDownloads is not enabled
        AppSettings& settings = Application::getInstance().getSettings();
        if (!settings.saveToDownloads) {
            // Re-create the download button if it doesn't exist
            if (!m_downloadButton) {
                // Will be visible on next view load
            }
        }

        dialog->close();
    });

    dialog->open();
}

bool MediaDetailView::areAllEpisodesDownloaded() {
    if (m_item.mediaType != MediaType::PODCAST) return false;
    if (m_children.empty()) return false;

    DownloadsManager& mgr = DownloadsManager::getInstance();
    for (const auto& ep : m_children) {
        if (!mgr.isDownloaded(m_item.id, ep.episodeId)) {
            return false;
        }
    }
    return true;
}

bool MediaDetailView::hasAnyDownloadedEpisodes() {
    if (m_item.mediaType != MediaType::PODCAST) return false;

    DownloadsManager& mgr = DownloadsManager::getInstance();

    // Check children if available
    if (!m_children.empty()) {
        for (const auto& ep : m_children) {
            if (mgr.isDownloaded(m_item.id, ep.episodeId)) {
                return true;
            }
        }
    }

    // Also check downloads directly for this podcast
    auto downloads = mgr.getDownloads();
    for (const auto& dl : downloads) {
        if (dl.itemId == m_item.id && dl.state == DownloadState::COMPLETED) {
            return true;
        }
    }

    return false;
}

void MediaDetailView::deleteAllDownloadedEpisodes() {
    brls::Logger::info("deleteAllDownloadedEpisodes called for podcast: {} ({})", m_item.title, m_item.id);

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    // Get downloaded episodes for this podcast
    DownloadsManager& mgr = DownloadsManager::getInstance();
    auto downloads = mgr.getDownloads();

    // Collect downloaded episode info: <episodeId, title>
    std::vector<std::pair<std::string, std::string>> downloadedEpisodes;
    for (const auto& dl : downloads) {
        if (dl.itemId == podcastId && dl.state == DownloadState::COMPLETED) {
            downloadedEpisodes.push_back({dl.episodeId, dl.title});
        }
    }

    if (downloadedEpisodes.empty()) {
        brls::Application::notify("No downloaded episodes found");
        return;
    }

    if (downloadedEpisodes.size() == 1) {
        // Single episode - show simple confirmation
        std::string episodeTitle = downloadedEpisodes[0].second;
        std::string episodeId = downloadedEpisodes[0].first;

        brls::Dialog* dialog = new brls::Dialog("Delete downloaded episode?\n\n\"" + episodeTitle + "\"");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete", [dialog, podcastId, episodeId, this]() {
            DownloadsManager::getInstance().deleteDownloadByEpisodeId(podcastId, episodeId);
            brls::Application::notify("Episode deleted");

            // Update UI
            if (m_deleteButton) {
                m_deleteButton->setVisibility(brls::Visibility::GONE);
            }
            if (m_downloadButton) {
                m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
            }

            dialog->close();
        });

        dialog->open();
    } else {
        // Multiple episodes - show dialog with Delete All and clickable episode list
        showDeleteEpisodesDialog(downloadedEpisodes, podcastId, podcastTitle);
    }
}

void MediaDetailView::showDeleteEpisodesDialog(
    const std::vector<std::pair<std::string, std::string>>& episodes,
    const std::string& podcastId,
    const std::string& podcastTitle) {

    // Create a dialog with Delete All button and clickable episode list
    auto* dialog = new brls::Dialog("Downloaded Episodes (" + std::to_string(episodes.size()) + ")");

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    // Create content box
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(20);
    content->setWidth(700);

    // Delete All button at top with confirmation
    auto* deleteAllBtn = new brls::Button();
    deleteAllBtn->setText("Delete All Downloaded Episodes");
    deleteAllBtn->setMarginBottom(15);

    // Copy episodes for the lambda
    std::vector<std::pair<std::string, std::string>> episodesCopy = episodes;

    deleteAllBtn->registerClickAction([this, dialog, episodesCopy, podcastId, podcastTitle](brls::View*) {
        // Show confirmation dialog
        brls::Dialog* confirmDialog = new brls::Dialog(
            "Delete all " + std::to_string(episodesCopy.size()) + " downloaded episodes from\n\"" + podcastTitle + "\"?");

        confirmDialog->addButton("Cancel", [confirmDialog]() {
            confirmDialog->close();
        });

        confirmDialog->addButton("Delete All", [this, dialog, confirmDialog, episodesCopy, podcastId]() {
            DownloadsManager& mgr = DownloadsManager::getInstance();

            int deleted = 0;
            for (const auto& ep : episodesCopy) {
                if (mgr.deleteDownloadByEpisodeId(podcastId, ep.first)) {
                    deleted++;
                }
            }

            brls::Application::notify("Deleted " + std::to_string(deleted) + " episode(s)");

            // Update UI
            if (m_deleteButton) {
                m_deleteButton->setVisibility(brls::Visibility::GONE);
            }
            if (m_downloadButton) {
                m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
            }

            confirmDialog->close();
            dialog->dismiss();
        });

        confirmDialog->open();
        return true;
    });
    content->addView(deleteAllBtn);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    content->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Click an episode to delete it:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    content->addView(instructionsLabel);

    // Create scrolling list of episodes - clicking each deletes just that one
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setHeight(300);

    auto* listBox = new brls::Box();
    listBox->setAxis(brls::Axis::COLUMN);

    for (const auto& ep : episodes) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12);
        row->setMarginBottom(8);
        row->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        row->setCornerRadius(6);
        row->setFocusable(true);

        // Episode title
        auto* titleLabel = new brls::Label();
        std::string title = ep.second;
        if (title.length() > 50) {
            title = title.substr(0, 47) + "...";
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        titleLabel->setGrow(1.0f);
        row->addView(titleLabel);

        // Delete indicator
        auto* deleteLabel = new brls::Label();
        deleteLabel->setText("[X]");
        deleteLabel->setFontSize(14);
        deleteLabel->setTextColor(nvgRGB(200, 100, 100));
        deleteLabel->setWidth(40);
        row->addView(deleteLabel);

        // Click to delete this episode
        std::string episodeId = ep.first;
        std::string episodeTitle = ep.second;
        row->registerClickAction([this, dialog, podcastId, episodeId, episodeTitle, row](brls::View*) {
            // Show confirmation for single episode
            brls::Dialog* confirmDialog = new brls::Dialog("Delete this episode?\n\n\"" + episodeTitle + "\"");

            confirmDialog->addButton("Cancel", [confirmDialog]() {
                confirmDialog->close();
            });

            confirmDialog->addButton("Delete", [this, dialog, confirmDialog, podcastId, episodeId, row]() {
                DownloadsManager& mgr = DownloadsManager::getInstance();
                if (mgr.deleteDownloadByEpisodeId(podcastId, episodeId)) {
                    brls::Application::notify("Episode deleted");

                    // Hide this row
                    row->setVisibility(brls::Visibility::GONE);

                    // Check if any episodes remain
                    if (!hasAnyDownloadedEpisodes()) {
                        if (m_deleteButton) {
                            m_deleteButton->setVisibility(brls::Visibility::GONE);
                        }
                        if (m_downloadButton) {
                            m_downloadButton->setVisibility(brls::Visibility::VISIBLE);
                        }
                        dialog->dismiss();
                    }
                } else {
                    brls::Application::notify("Failed to delete episode");
                }

                confirmDialog->close();
            });

            confirmDialog->open();
            return true;
        });

        listBox->addView(row);
    }

    scrollView->setContentView(listBox);
    content->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    content->addView(closeBtn);

    dialog->addView(content);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::startDownloadOnly(const std::string& itemId, const std::string& episodeId) {
    brls::Logger::info("startDownloadOnly: itemId={}, episodeId={}", itemId, episodeId);

    // Check if already downloaded (check both itemId and episodeId)
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    downloadsMgr.init();

    if (downloadsMgr.isDownloaded(itemId, episodeId)) {
        brls::Application::notify("Already downloaded");
        return;
    }

    // Update button state
    if (m_downloadButton) {
        m_downloadButton->setText("Downloading...");
    }

    // Use the same code path as play, but with downloadOnly=true
    // This will save to downloads folder and not start playback
    startDownloadAndPlay(itemId, episodeId, -1.0f, true);
}

void MediaDetailView::batchDownloadEpisodes(const std::vector<MediaItem>& episodes) {
    if (episodes.empty()) {
        brls::Application::notify("No episodes to download");
        return;
    }

    brls::Logger::info("batchDownloadEpisodes: Starting batch download of {} episodes", episodes.size());

    // Show progress dialog
    auto* progressDialog = ProgressDialog::showDownloading("Downloading Episodes");
    progressDialog->setStatus("Preparing downloads...");

    // Store data for async operation - use parent podcast ID for all episodes
    std::string podcastTitle = m_item.title;
    std::string podcastId = m_item.id;  // Parent podcast ID

    asyncRun([this, progressDialog, episodes, podcastTitle, podcastId]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
        downloadsMgr.init();

        int completed = 0;
        int failed = 0;
        int totalEpisodes = static_cast<int>(episodes.size());

        for (size_t i = 0; i < episodes.size(); i++) {
            const auto& ep = episodes[i];
            // Use parent podcast ID, not episode's podcastId field (which may be empty)
            std::string itemId = podcastId;
            std::string episodeId = ep.episodeId;

            brls::Logger::info("batchDownloadEpisodes: Downloading episode {} of {}: {} (episodeId: {})",
                              i + 1, totalEpisodes, ep.title, episodeId);

            // Update progress dialog
            std::string epTitle = ep.title;  // Copy to avoid reference issues
            brls::sync([progressDialog, i, totalEpisodes, epTitle]() {
                char buf[128];
                snprintf(buf, sizeof(buf), "Downloading %d/%d:\n%s",
                        static_cast<int>(i + 1), totalEpisodes, epTitle.c_str());
                progressDialog->setStatus(buf);
                progressDialog->setProgress(static_cast<float>(i) / totalEpisodes);
            });

            // Check if already downloaded (check both itemId and episodeId)
            if (downloadsMgr.isDownloaded(itemId, episodeId)) {
                brls::Logger::info("Episode already downloaded: {}", ep.title);
                completed++;
                continue;
            }

            // Start playback session to get download URL
            PlaybackSession session;
            if (!client.startPlaybackSession(itemId, session, episodeId)) {
                brls::Logger::error("Failed to start session for episode: {}", ep.title);
                failed++;
                continue;
            }

            if (session.audioTracks.empty()) {
                brls::Logger::error("No audio tracks for episode: {}", ep.title);
                failed++;
                continue;
            }

            // Get download URL
            std::string trackUrl = client.getStreamUrl(session.audioTracks[0].contentUrl, "");
            if (trackUrl.empty()) {
                brls::Logger::error("Failed to get download URL for episode: {}", ep.title);
                failed++;
                continue;
            }

            // Determine file extension
            std::string ext = ".mp3";
            std::string mimeType = session.audioTracks[0].mimeType;
            if (mimeType.find("mp4") != std::string::npos || mimeType.find("m4a") != std::string::npos) {
                ext = ".m4a";
            }

            // Destination path
            std::string filename = episodeId.empty() ? itemId : episodeId;
            filename += ext;
            std::string destPath = downloadsMgr.getDownloadsPath() + "/" + filename;

            brls::Logger::info("Downloading to: {}", destPath);

#ifdef __vita__
            HttpClient httpClient;
            httpClient.setTimeout(300);

            SceUID fd = sceIoOpen(destPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
            if (fd < 0) {
                brls::Logger::error("Failed to create file: {}", destPath);
                failed++;
                continue;
            }

            int64_t totalDownloaded = 0;
            int64_t totalSize = 0;
            int currentEpisodeNum = static_cast<int>(i) + 1;

            bool success = httpClient.downloadFile(trackUrl,
                [&](const char* data, size_t size) -> bool {
                    int written = sceIoWrite(fd, data, size);
                    if (written < 0) return false;
                    totalDownloaded += size;

                    // Update progress with detailed MB info like audiobooks
                    if (totalSize > 0) {
                        float episodeProgress = static_cast<float>(totalDownloaded) / totalSize;
                        float overallProgress = (static_cast<float>(i) + episodeProgress) / totalEpisodes;
                        int percent = static_cast<int>(episodeProgress * 100);
                        int dlMB = static_cast<int>(totalDownloaded / (1024 * 1024));
                        int sizeMB = static_cast<int>(totalSize / (1024 * 1024));
                        brls::sync([progressDialog, overallProgress, currentEpisodeNum, totalEpisodes, percent, dlMB, sizeMB, epTitle]() {
                            char buf[160];
                            snprintf(buf, sizeof(buf), "Episode %d/%d: %d%% (%d/%d MB)\n%s",
                                    currentEpisodeNum, totalEpisodes, percent, dlMB, sizeMB, epTitle.c_str());
                            progressDialog->setStatus(buf);
                            progressDialog->setProgress(overallProgress);
                        });
                    }
                    return true;
                },
                [&](int64_t size) { totalSize = size; }
            );

            sceIoClose(fd);

            if (success) {
                // Register the download - use podcast author for episodes
                std::string coverUrl = client.getCoverUrl(itemId);
                // Use the actual podcast author, fall back to podcast title if no author
                std::string podcastAuthor = m_item.authorName.empty() ? m_item.title : m_item.authorName;
                downloadsMgr.registerCompletedDownload(
                    itemId, episodeId, ep.title, podcastAuthor,
                    destPath, totalDownloaded, ep.duration, "episode",
                    coverUrl, "", {}
                );

                // Download cover
                if (!coverUrl.empty()) {
                    downloadsMgr.downloadCoverImage(episodeId.empty() ? itemId : episodeId, coverUrl);
                }

                completed++;
                brls::Logger::info("Downloaded episode: {}", ep.title);
            } else {
                sceIoRemove(destPath.c_str());
                failed++;
                brls::Logger::error("Failed to download episode: {}", ep.title);
            }
#else
            // Non-Vita: simplified download
            completed++;
#endif
        }

        // Ensure state is saved
        downloadsMgr.saveState();

        // Show completion dialog
        brls::sync([progressDialog, completed, failed, totalEpisodes]() {
            char buf[128];
            snprintf(buf, sizeof(buf), "Downloaded %d of %d episodes\n(%d failed)",
                    completed, totalEpisodes, failed);
            progressDialog->setStatus(buf);
            progressDialog->setProgress(1.0f);

            brls::delay(2000, [progressDialog]() {
                progressDialog->dismiss();
            });
        });

        brls::Logger::info("batchDownloadEpisodes: Completed {} of {} episodes", completed, totalEpisodes);
    });
}

void MediaDetailView::showDownloadOptions() {
    if (m_item.mediaType != MediaType::PODCAST) {
        return;
    }

    // Count episodes and find undownloaded ones
    DownloadsManager& downloadsMgr = DownloadsManager::getInstance();
    std::vector<MediaItem> undownloadedEpisodes;

    for (const auto& ep : m_children) {
        bool isDownloaded = downloadsMgr.isDownloaded(m_item.id, ep.episodeId);
        if (!isDownloaded) {
            undownloadedEpisodes.push_back(ep);
        }
    }

    // If only 1 episode total OR only 1 undownloaded episode, download directly
    if (m_children.size() == 1 || undownloadedEpisodes.size() == 1) {
        if (undownloadedEpisodes.empty()) {
            brls::Application::notify("All episodes already downloaded");
            return;
        }
        // Download the single undownloaded episode directly
        batchDownloadEpisodes(undownloadedEpisodes);
        return;
    }

    // If no undownloaded episodes, notify and return
    if (undownloadedEpisodes.empty()) {
        brls::Application::notify("All episodes already downloaded");
        return;
    }

    // Count unheard (not finished) episodes
    int unheardCount = 0;
    for (const auto& ep : m_children) {
        bool isDownloaded = downloadsMgr.isDownloaded(m_item.id, ep.episodeId);
        if (ep.progress < 1.0f && !isDownloaded) {
            unheardCount++;
        }
    }

    // Show full dialog for multiple episodes
    int episodeCount = static_cast<int>(m_children.size());
    std::string title = "Download Episodes (" + std::to_string(episodeCount) + ")";

    auto* dialog = new brls::Dialog(title);

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    // Create content box
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(20);
    content->setWidth(700);

    // Horizontal row for the 3 download options
    auto* optionsRow = new brls::Box();
    optionsRow->setAxis(brls::Axis::ROW);
    optionsRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    optionsRow->setMarginBottom(15);

    // Download All option box
    auto* downloadAllBox = new brls::Box();
    downloadAllBox->setAxis(brls::Axis::COLUMN);
    downloadAllBox->setAlignItems(brls::AlignItems::CENTER);
    downloadAllBox->setJustifyContent(brls::JustifyContent::CENTER);
    downloadAllBox->setPadding(12);
    downloadAllBox->setWidth(210);
    downloadAllBox->setHeight(60);
    downloadAllBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    downloadAllBox->setCornerRadius(6);
    downloadAllBox->setFocusable(true);

    auto* downloadAllLabel = new brls::Label();
    downloadAllLabel->setText("Download All");
    downloadAllLabel->setFontSize(14);
    downloadAllBox->addView(downloadAllLabel);

    auto* downloadAllCount = new brls::Label();
    downloadAllCount->setText("(" + std::to_string(episodeCount) + ")");
    downloadAllCount->setFontSize(12);
    downloadAllCount->setTextColor(nvgRGB(150, 150, 150));
    downloadAllBox->addView(downloadAllCount);

    downloadAllBox->registerClickAction([this, dialog](brls::View*) {
        dialog->dismiss();
        downloadAll();
        return true;
    });
    optionsRow->addView(downloadAllBox);

    // Download Unheard option box
    auto* unheardBox = new brls::Box();
    unheardBox->setAxis(brls::Axis::COLUMN);
    unheardBox->setAlignItems(brls::AlignItems::CENTER);
    unheardBox->setJustifyContent(brls::JustifyContent::CENTER);
    unheardBox->setPadding(12);
    unheardBox->setWidth(210);
    unheardBox->setHeight(60);
    unheardBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    unheardBox->setCornerRadius(6);
    unheardBox->setFocusable(true);

    auto* unheardLabel = new brls::Label();
    unheardLabel->setText("Unheard");
    unheardLabel->setFontSize(14);
    unheardBox->addView(unheardLabel);

    auto* unheardCountLabel = new brls::Label();
    unheardCountLabel->setText("(" + std::to_string(unheardCount) + ")");
    unheardCountLabel->setFontSize(12);
    unheardCountLabel->setTextColor(nvgRGB(150, 150, 150));
    unheardBox->addView(unheardCountLabel);

    unheardBox->registerClickAction([this, dialog](brls::View*) {
        dialog->dismiss();
        downloadUnwatched();
        return true;
    });
    optionsRow->addView(unheardBox);

    // Download Next 5 option box - only show if 5 or more unheard, otherwise show placeholder
    auto* next5Box = new brls::Box();
    next5Box->setAxis(brls::Axis::COLUMN);
    next5Box->setAlignItems(brls::AlignItems::CENTER);
    next5Box->setJustifyContent(brls::JustifyContent::CENTER);
    next5Box->setPadding(12);
    next5Box->setWidth(210);
    next5Box->setHeight(60);
    next5Box->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    next5Box->setCornerRadius(6);

    if (unheardCount >= 5) {
        next5Box->setFocusable(true);

        auto* next5Label = new brls::Label();
        next5Label->setText("Next 5");
        next5Label->setFontSize(14);
        next5Box->addView(next5Label);

        auto* next5SubLabel = new brls::Label();
        next5SubLabel->setText("Unheard");
        next5SubLabel->setFontSize(12);
        next5SubLabel->setTextColor(nvgRGB(150, 150, 150));
        next5Box->addView(next5SubLabel);

        next5Box->registerClickAction([this, dialog](brls::View*) {
            dialog->dismiss();
            downloadUnwatched(5);
            return true;
        });
    } else {
        // Greyed out / disabled look
        next5Box->setFocusable(false);
        next5Box->setBackgroundColor(nvgRGBA(45, 45, 45, 255));

        auto* next5Label = new brls::Label();
        next5Label->setText("Next 5");
        next5Label->setFontSize(14);
        next5Label->setTextColor(nvgRGB(100, 100, 100));
        next5Box->addView(next5Label);

        auto* next5SubLabel = new brls::Label();
        next5SubLabel->setText("(< 5 unheard)");
        next5SubLabel->setFontSize(12);
        next5SubLabel->setTextColor(nvgRGB(80, 80, 80));
        next5Box->addView(next5SubLabel);
    }
    optionsRow->addView(next5Box);

    content->addView(optionsRow);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    content->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Or click an episode to download it:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    content->addView(instructionsLabel);

    // Create scrolling list of episodes
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setHeight(280);

    auto* listBox = new brls::Box();
    listBox->setAxis(brls::Axis::COLUMN);

    for (const auto& ep : m_children) {
        bool isDownloaded = downloadsMgr.isDownloaded(m_item.id, ep.episodeId);

        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPadding(12);
        row->setMarginBottom(8);
        row->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        row->setCornerRadius(6);
        row->setFocusable(true);

        // Episode title
        auto* titleLabel = new brls::Label();
        std::string epTitle = ep.title;
        if (epTitle.length() > 45) {
            epTitle = epTitle.substr(0, 42) + "...";
        }
        titleLabel->setText(epTitle);
        titleLabel->setFontSize(15);
        titleLabel->setGrow(1.0f);
        row->addView(titleLabel);

        // Status indicator
        auto* statusLabel = new brls::Label();
        if (isDownloaded) {
            statusLabel->setText("[OK]");
            statusLabel->setTextColor(nvgRGB(100, 200, 100));
        } else {
            statusLabel->setText("[+]");
            statusLabel->setTextColor(nvgRGB(100, 180, 100));
        }
        statusLabel->setFontSize(14);
        statusLabel->setWidth(40);
        row->addView(statusLabel);

        // Click to download this episode (if not already downloaded)
        std::string episodeId = ep.episodeId;
        std::string episodeTitle = ep.title;
        float episodeDuration = ep.duration;
        row->registerClickAction([this, dialog, episodeId, episodeTitle, episodeDuration, isDownloaded](brls::View*) {
            if (isDownloaded) {
                brls::Application::notify("Already downloaded");
            } else {
                dialog->dismiss();
                // Download single episode
                MediaItem singleEp;
                singleEp.episodeId = episodeId;
                singleEp.title = episodeTitle;
                singleEp.duration = episodeDuration;
                batchDownloadEpisodes({singleEp});
            }
            return true;
        });

        listBox->addView(row);
    }

    scrollView->setContentView(listBox);
    content->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    content->addView(closeBtn);

    dialog->addView(content);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadAll() {
    auto* progressDialog = new ProgressDialog("Preparing Downloads");
    progressDialog->setStatus("Fetching episode list...");
    progressDialog->show();

    std::string podcastId = m_item.id;

    asyncRun([this, progressDialog, podcastId]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> episodes;

        if (client.fetchPodcastEpisodes(podcastId, episodes)) {
            size_t itemCount = episodes.size();
            brls::sync([progressDialog, itemCount]() {
                progressDialog->setStatus("Found " + std::to_string(itemCount) + " episodes");
            });

            brls::Logger::info("downloadAll: Found {} episodes to download", episodes.size());

            // Dismiss the preparation dialog and start batch download
            brls::sync([this, progressDialog, episodes]() {
                progressDialog->dismiss();
                // Use the same download method as the play button
                batchDownloadEpisodes(episodes);
            });
        } else {
            brls::sync([progressDialog]() {
                progressDialog->setStatus("Failed to fetch episodes");
                brls::delay(1500, [progressDialog]() {
                    progressDialog->dismiss();
                });
            });
        }
    });
}

void MediaDetailView::downloadUnwatched(int maxCount) {
    auto* progressDialog = new ProgressDialog("Preparing Downloads");
    progressDialog->setStatus("Fetching unheard episodes...");
    progressDialog->show();

    std::string podcastId = m_item.id;

    asyncRun([this, progressDialog, podcastId, maxCount]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> unheardEpisodes;

        std::vector<MediaItem> allEpisodes;
        if (client.fetchPodcastEpisodes(podcastId, allEpisodes)) {
            for (auto& ep : allEpisodes) {
                // Episode is unheard if not finished AND has no progress
                if (!ep.isFinished && ep.currentTime == 0) {
                    unheardEpisodes.push_back(ep);
                    if (maxCount > 0 && static_cast<int>(unheardEpisodes.size()) >= maxCount) {
                        break;
                    }
                }
            }
        }

        size_t itemCount = unheardEpisodes.size();
        brls::sync([progressDialog, itemCount]() {
            progressDialog->setStatus("Found " + std::to_string(itemCount) + " unheard");
        });

        if (unheardEpisodes.empty()) {
            brls::sync([progressDialog]() {
                progressDialog->setStatus("No unheard episodes found");
                brls::delay(1500, [progressDialog]() {
                    progressDialog->dismiss();
                });
            });
            return;
        }

        brls::Logger::info("downloadUnwatched: Found {} unheard episodes to download", unheardEpisodes.size());

        // Dismiss the preparation dialog and start batch download
        brls::sync([this, progressDialog, unheardEpisodes]() {
            progressDialog->dismiss();
            // Use the same download method as the play button
            batchDownloadEpisodes(unheardEpisodes);
        });
    });
}

void MediaDetailView::findNewEpisodes() {
    brls::Application::notify("Checking RSS feed for new episodes...");

    std::string podcastId = m_item.id;
    std::string podcastTitle = m_item.title;

    asyncRun([this, podcastId, podcastTitle]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        std::vector<MediaItem> newEpisodes;

        if (client.checkNewEpisodes(podcastId, newEpisodes)) {
            brls::sync([this, newEpisodes, podcastId, podcastTitle]() {
                if (newEpisodes.empty()) {
                    brls::Application::notify("No new episodes found");
                } else {
                    showNewEpisodesDialog(newEpisodes, podcastId, podcastTitle);
                }
            });
        } else {
            brls::sync([]() {
                brls::Application::notify("Failed to check for new episodes");
            });
        }
    });
}

void MediaDetailView::showNewEpisodesDialog(const std::vector<MediaItem>& episodes,
                                             const std::string& podcastId,
                                             const std::string& podcastTitle) {
    // Create a dialog to show new episodes
    auto* dialog = new brls::Dialog("New Episodes (" + std::to_string(episodes.size()) + ")");

    // Register circle button to close dialog
    dialog->registerAction("Back", brls::ControllerButton::BUTTON_B, [dialog](brls::View*) {
        dialog->dismiss();
        return true;
    }, true);

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(15);
    contentBox->setWidth(700);

    // Add "Download All" button at the top
    auto* downloadAllBtn = new brls::Button();
    downloadAllBtn->setText("Download All to Server");
    downloadAllBtn->setMarginBottom(15);
    downloadAllBtn->registerClickAction([this, episodes, podcastId, dialog](brls::View*) {
        dialog->dismiss();
        downloadNewEpisodesToServer(podcastId, episodes);
        return true;
    });
    contentBox->addView(downloadAllBtn);

    // Separator
    auto* separator = new brls::Rectangle();
    separator->setHeight(1);
    separator->setColor(nvgRGB(80, 80, 80));
    separator->setMarginBottom(15);
    contentBox->addView(separator);

    // Instructions
    auto* instructionsLabel = new brls::Label();
    instructionsLabel->setText("Select episodes to add to server:");
    instructionsLabel->setFontSize(14);
    instructionsLabel->setTextColor(nvgRGB(180, 180, 180));
    instructionsLabel->setMarginBottom(10);
    contentBox->addView(instructionsLabel);

    // Create scrollable list of episodes
    auto* scrollFrame = new brls::ScrollingFrame();
    scrollFrame->setHeight(350);

    auto* episodesList = new brls::Box();
    episodesList->setAxis(brls::Axis::COLUMN);

    // Store selected episodes
    auto selectedEpisodes = std::make_shared<std::vector<std::string>>();

    for (const auto& ep : episodes) {
        auto* episodeRow = new brls::Box();
        episodeRow->setAxis(brls::Axis::ROW);
        episodeRow->setAlignItems(brls::AlignItems::CENTER);
        episodeRow->setMarginBottom(15);  // More space between episodes
        episodeRow->setPadding(15);
        episodeRow->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
        episodeRow->setCornerRadius(8);
        episodeRow->setFocusable(true);

        // Episode info
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);
        infoBox->setJustifyContent(brls::JustifyContent::CENTER);

        // Title - show first line
        auto* titleLabel = new brls::Label();
        std::string title = ep.title;
        std::string title2;

        // Split long titles into two lines
        if (title.length() > 50) {
            size_t splitPos = title.rfind(' ', 50);
            if (splitPos != std::string::npos && splitPos > 20) {
                title2 = title.substr(splitPos + 1);
                title = title.substr(0, splitPos);
                // Truncate second line if still too long
                if (title2.length() > 50) {
                    title2 = title2.substr(0, 47) + "...";
                }
            } else {
                // No good split point, just truncate
                title = title.substr(0, 47) + "...";
            }
        }
        titleLabel->setText(title);
        titleLabel->setFontSize(15);
        infoBox->addView(titleLabel);

        // Second line of title if needed
        if (!title2.empty()) {
            auto* title2Label = new brls::Label();
            title2Label->setText(title2);
            title2Label->setFontSize(14);
            title2Label->setTextColor(nvgRGB(200, 200, 200));
            title2Label->setMarginTop(2);
            infoBox->addView(title2Label);
        }

        if (!ep.pubDate.empty()) {
            auto* dateLabel = new brls::Label();
            dateLabel->setText(ep.pubDate);
            dateLabel->setFontSize(12);
            dateLabel->setTextColor(nvgRGB(150, 150, 150));
            dateLabel->setMarginTop(6);
            infoBox->addView(dateLabel);
        }

        episodeRow->addView(infoBox);

        // Status label (shows "Added!" after adding)
        auto* statusLabel = new brls::Label();
        statusLabel->setText("");
        statusLabel->setFontSize(14);
        statusLabel->setTextColor(nvgRGB(100, 200, 100));
        statusLabel->setWidth(80);
        episodeRow->addView(statusLabel);

        // Click on row to add episode - pass full episode data for new RSS episodes
        MediaItem epCopy = ep;
        std::string pId = podcastId;
        episodeRow->registerClickAction([this, epCopy, pId, statusLabel](brls::View*) {
            // Download this single episode to server with full data
            std::vector<MediaItem> eps = {epCopy};
            AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
            if (client.downloadNewEpisodesToServer(pId, eps)) {
                statusLabel->setText("Added!");
                brls::Application::notify("Episode queued for download on server");
            } else {
                brls::Application::notify("Failed to add episode");
            }
            return true;
        });

        episodesList->addView(episodeRow);
    }

    scrollFrame->setContentView(episodesList);
    contentBox->addView(scrollFrame);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([dialog](brls::View*) {
        dialog->dismiss();
        return true;
    });
    contentBox->addView(closeBtn);

    dialog->addView(contentBox);
    brls::Application::pushActivity(new brls::Activity(dialog));
}

void MediaDetailView::downloadNewEpisodesToServer(const std::string& podcastId,
                                                   const std::vector<MediaItem>& episodes) {
    brls::Application::notify("Adding " + std::to_string(episodes.size()) + " episodes to server...");

    asyncRun([podcastId, episodes]() {
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();
        // Use full episode data for new RSS episodes
        bool success = client.downloadNewEpisodesToServer(podcastId, episodes);

        brls::sync([success, episodes]() {
            if (success) {
                brls::Application::notify("Queued " + std::to_string(episodes.size()) + " episodes for download");
            } else {
                brls::Application::notify("Failed to queue episodes");
            }
        });
    });
}

} // namespace vitasuwayomi
