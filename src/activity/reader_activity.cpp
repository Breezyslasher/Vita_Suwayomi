/**
 * VitaSuwayomi - Manga Reader Activity implementation
 */

#include "activity/reader_activity.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

#include <borealis.hpp>

namespace vitasuwayomi {

ReaderActivity::ReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle)
    : m_mangaId(mangaId)
    , m_chapterIndex(chapterIndex)
    , m_mangaTitle(mangaTitle)
    , m_startPage(0) {
    brls::Logger::info("ReaderActivity: manga={}, chapter={}", mangaId, chapterIndex);
}

ReaderActivity::ReaderActivity(int mangaId, int chapterIndex, int startPage, const std::string& mangaTitle)
    : m_mangaId(mangaId)
    , m_chapterIndex(chapterIndex)
    , m_mangaTitle(mangaTitle)
    , m_startPage(startPage) {
    brls::Logger::info("ReaderActivity: manga={}, chapter={}, startPage={}",
                       mangaId, chapterIndex, startPage);
}

brls::View* ReaderActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/reader.xml");
}

void ReaderActivity::onContentAvailable() {
    brls::Logger::debug("ReaderActivity: content available");

    // Set up input handling
    this->registerAction("Previous Page", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        previousPage();
        return true;
    });

    this->registerAction("Next Page", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        nextPage();
        return true;
    });

    this->registerAction("Previous Page", brls::ControllerButton::BUTTON_LEFT, [this](brls::View*) {
        if (m_readingMode == ReadingMode::RIGHT_TO_LEFT) {
            nextPage();
        } else {
            previousPage();
        }
        return true;
    });

    this->registerAction("Next Page", brls::ControllerButton::BUTTON_RIGHT, [this](brls::View*) {
        if (m_readingMode == ReadingMode::RIGHT_TO_LEFT) {
            previousPage();
        } else {
            nextPage();
        }
        return true;
    });

    this->registerAction("Toggle Controls", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        toggleControls();
        return true;
    });

    this->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        toggleControls();
        return true;
    });

    // Set up page slider if available
    if (pageSlider) {
        pageSlider->getProgressEvent()->subscribe([this](float progress) {
            int targetPage = static_cast<int>(progress * (m_pages.size() - 1));
            if (targetPage != m_currentPage) {
                goToPage(targetPage);
            }
        });
    }

    // Set up chapter navigation buttons
    if (prevChapterBtn) {
        prevChapterBtn->registerClickAction([this](brls::View*) {
            previousChapter();
            return true;
        });
    }

    if (nextChapterBtn) {
        nextChapterBtn->registerClickAction([this](brls::View*) {
            nextChapter();
            return true;
        });
    }

    // Load pages asynchronously
    loadPages();
}

void ReaderActivity::loadPages() {
    brls::Logger::debug("Loading pages for chapter {}", m_chapterIndex);

    vitaabs::asyncTask<bool>([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Fetch chapter info and pages
        if (!client.fetchChapterPages(m_mangaId, m_chapterIndex, m_pages)) {
            brls::Logger::error("Failed to fetch chapter pages");
            return false;
        }

        // Also fetch chapter details for the name
        Chapter chapter;
        if (client.fetchChapter(m_mangaId, m_chapterIndex, chapter)) {
            m_chapterName = chapter.name;
        }

        // Fetch all chapters for navigation
        client.fetchChapters(m_mangaId, m_chapters);
        m_totalChapters = static_cast<int>(m_chapters.size());

        return true;
    }, [this](bool success) {
        if (!success || m_pages.empty()) {
            brls::Logger::error("No pages to display");

            if (chapterLabel) {
                chapterLabel->setText("Failed to load chapter");
            }
            return;
        }

        brls::Logger::info("Loaded {} pages", m_pages.size());

        // Update UI
        if (chapterLabel) {
            chapterLabel->setText(m_chapterName.empty() ?
                "Chapter " + std::to_string(m_chapterIndex + 1) : m_chapterName);
        }

        // Start from saved position or beginning
        m_currentPage = std::min(m_startPage, static_cast<int>(m_pages.size()) - 1);
        m_currentPage = std::max(0, m_currentPage);

        updatePageDisplay();
        loadPage(m_currentPage);
    });
}

void ReaderActivity::loadPage(int index) {
    if (index < 0 || index >= static_cast<int>(m_pages.size())) {
        return;
    }

    std::string imageUrl = m_pages[index].imageUrl;
    brls::Logger::debug("Loading page {} from: {}", index, imageUrl);

    // Check cache first
    auto cacheIt = m_cachedImages.find(index);
    if (cacheIt != m_cachedImages.end()) {
        if (pageImage) {
            // Use cached image
            // Note: This would require proper image caching implementation
        }
    }

    // Load image
    if (pageImage) {
        vitaabs::ImageLoader::loadAsync(imageUrl, [this, index](const std::string& imagePath) {
            if (index == m_currentPage && pageImage) {
                brls::sync([this, imagePath]() {
                    pageImage->setImageFromFile(imagePath);
                });
            }
        });
    }

    // Preload adjacent pages
    preloadAdjacentPages();
}

void ReaderActivity::preloadAdjacentPages() {
    // Preload next 2 pages and previous 1 page
    std::vector<int> toPreload;

    if (m_currentPage + 1 < static_cast<int>(m_pages.size())) {
        toPreload.push_back(m_currentPage + 1);
    }
    if (m_currentPage + 2 < static_cast<int>(m_pages.size())) {
        toPreload.push_back(m_currentPage + 2);
    }
    if (m_currentPage - 1 >= 0) {
        toPreload.push_back(m_currentPage - 1);
    }

    for (int idx : toPreload) {
        if (m_cachedImages.find(idx) == m_cachedImages.end()) {
            std::string url = m_pages[idx].imageUrl;
            vitaabs::ImageLoader::loadAsync(url, [this, idx](const std::string& path) {
                m_cachedImages[idx] = path;
            });
        }
    }
}

void ReaderActivity::updatePageDisplay() {
    if (pageLabel) {
        pageLabel->setText(std::to_string(m_currentPage + 1) + " / " +
                          std::to_string(m_pages.size()));
    }

    if (pageSlider && !m_pages.empty()) {
        float progress = static_cast<float>(m_currentPage) / static_cast<float>(m_pages.size() - 1);
        pageSlider->setProgress(progress);
    }
}

void ReaderActivity::updateProgress() {
    // Save reading progress to server
    vitaabs::asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        client.updateChapterProgress(m_mangaId, m_chapterIndex, m_currentPage);
    });
}

void ReaderActivity::nextPage() {
    if (m_currentPage < static_cast<int>(m_pages.size()) - 1) {
        m_currentPage++;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else {
        // End of chapter - mark as read and prompt for next chapter
        markChapterAsRead();

        if (m_chapterIndex < m_totalChapters - 1) {
            // Show dialog to go to next chapter
            auto* dialog = new brls::Dialog("End of Chapter\nGo to next chapter?");
            dialog->addButton("Next Chapter", [this, dialog]() {
                dialog->dismiss();
                nextChapter();
            });
            dialog->addButton("Stay", [dialog]() {
                dialog->dismiss();
            });
            dialog->open();
        }
    }
}

void ReaderActivity::previousPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    } else if (m_chapterIndex > 0) {
        // Beginning of chapter - offer previous chapter
        auto* dialog = new brls::Dialog("Beginning of Chapter\nGo to previous chapter?");
        dialog->addButton("Previous Chapter", [this, dialog]() {
            dialog->dismiss();
            previousChapter();
        });
        dialog->addButton("Stay", [dialog]() {
            dialog->dismiss();
        });
        dialog->open();
    }
}

void ReaderActivity::goToPage(int pageIndex) {
    if (pageIndex >= 0 && pageIndex < static_cast<int>(m_pages.size())) {
        m_currentPage = pageIndex;
        updatePageDisplay();
        loadPage(m_currentPage);
        updateProgress();
    }
}

void ReaderActivity::nextChapter() {
    if (m_chapterIndex < m_totalChapters - 1) {
        // Mark current chapter as read
        markChapterAsRead();

        // Navigate to next chapter
        m_chapterIndex++;
        m_currentPage = 0;
        m_pages.clear();
        m_cachedImages.clear();

        loadPages();
    }
}

void ReaderActivity::previousChapter() {
    if (m_chapterIndex > 0) {
        m_chapterIndex--;
        m_currentPage = 0;
        m_pages.clear();
        m_cachedImages.clear();

        loadPages();
    }
}

void ReaderActivity::markChapterAsRead() {
    vitaabs::asyncRun([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        client.markChapterRead(m_mangaId, m_chapterIndex);
    });
}

void ReaderActivity::toggleControls() {
    m_controlsVisible = !m_controlsVisible;

    if (m_controlsVisible) {
        showControls();
    } else {
        hideControls();
    }
}

void ReaderActivity::showControls() {
    if (controlsOverlay) {
        controlsOverlay->setVisibility(brls::Visibility::VISIBLE);
    }
    m_controlsVisible = true;
}

void ReaderActivity::hideControls() {
    if (controlsOverlay) {
        controlsOverlay->setVisibility(brls::Visibility::GONE);
    }
    m_controlsVisible = false;
}

void ReaderActivity::toggleFullscreen() {
    m_isFullscreen = !m_isFullscreen;
    // Implementation depends on borealis fullscreen support
}

void ReaderActivity::setReadingMode(ReadingMode mode) {
    m_readingMode = mode;
    // Update UI based on reading mode
}

void ReaderActivity::setPageScaleMode(PageScaleMode mode) {
    m_scaleMode = mode;
    // Update image scaling
}

} // namespace vitasuwayomi
