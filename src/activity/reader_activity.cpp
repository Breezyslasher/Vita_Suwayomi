/**
 * VitaSuwayomi - Manga Reader Activity implementation
 * NOBORU-style UI with tap to show/hide controls and touch navigation
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

    // Start with controls hidden
    hideControls();

    // Set up controller input
    this->registerAction("Previous Page", brls::ControllerButton::BUTTON_LB, [this](brls::View*) {
        previousPage();
        return true;
    });

    this->registerAction("Next Page", brls::ControllerButton::BUTTON_RB, [this](brls::View*) {
        nextPage();
        return true;
    });

    // Horizontal navigation (left/right) - works in all orientations
    this->registerAction("Previous Page", brls::ControllerButton::BUTTON_LEFT, [this](brls::View*) {
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
            nextPage();
        } else {
            previousPage();
        }
        return true;
    });

    this->registerAction("Next Page", brls::ControllerButton::BUTTON_RIGHT, [this](brls::View*) {
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
            previousPage();
        } else {
            nextPage();
        }
        return true;
    });

    // Vertical navigation (up/down) - works in all orientations
    this->registerAction("Previous Page", brls::ControllerButton::BUTTON_UP, [this](brls::View*) {
        previousPage();
        return true;
    });

    this->registerAction("Next Page", brls::ControllerButton::BUTTON_DOWN, [this](brls::View*) {
        nextPage();
        return true;
    });

    // Toggle controls with Y or Start button
    this->registerAction("Toggle Controls", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        toggleControls();
        return true;
    });

    this->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        toggleControls();
        return true;
    });

    // Touch/click handling on the main container
    if (container) {
        container->registerClickAction([this](brls::View* view) {
            // Get touch position - center tap toggles controls
            // Left/right tap navigates pages
            brls::Application::giveFocus(view);
            toggleControls();
            return true;
        });
    }

    // Touch on the page image for navigation
    if (pageImage) {
        pageImage->setFocusable(true);
        pageImage->registerClickAction([this](brls::View* view) {
            // Toggle controls on tap
            toggleControls();
            return true;
        });
    }

    // Set up page slider
    if (pageSlider) {
        pageSlider->getProgressEvent()->subscribe([this](float progress) {
            if (m_pages.empty()) return;
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

    // Set up settings button
    if (settingsBtn) {
        settingsBtn->registerClickAction([this](brls::View*) {
            showSettings();
            return true;
        });
    }

    // Load pages asynchronously
    loadPages();
}

void ReaderActivity::loadPages() {
    brls::Logger::debug("Loading pages for chapter {}", m_chapterIndex);

    vitasuwayomi::asyncTask<bool>([this]() {
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
            std::string label = m_chapterName.empty() ?
                "Chapter " + std::to_string(m_chapterIndex + 1) : m_chapterName;
            chapterLabel->setText(label);
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

    // Load image
    if (pageImage) {
        int currentPageAtLoad = m_currentPage;
        ImageLoader::loadAsync(imageUrl, [this, index, currentPageAtLoad](brls::Image* img) {
            if (index == currentPageAtLoad) {
                brls::Logger::debug("ReaderActivity: Page {} loaded", index);
            }
        }, pageImage);
    }

    // Preload adjacent pages
    preloadAdjacentPages();
}

void ReaderActivity::preloadAdjacentPages() {
    // Preload next page
    if (m_currentPage + 1 < static_cast<int>(m_pages.size())) {
        std::string nextUrl = m_pages[m_currentPage + 1].imageUrl;
        ImageLoader::preload(nextUrl);
    }

    // Preload previous page
    if (m_currentPage > 0) {
        std::string prevUrl = m_pages[m_currentPage - 1].imageUrl;
        ImageLoader::preload(prevUrl);
    }
}

void ReaderActivity::updatePageDisplay() {
    if (pageLabel) {
        pageLabel->setText(std::to_string(m_currentPage + 1) + " / " +
                          std::to_string(m_pages.size()));
    }

    if (pageSlider && !m_pages.empty()) {
        float progress = static_cast<float>(m_currentPage) /
                        static_cast<float>(std::max(1, static_cast<int>(m_pages.size()) - 1));
        pageSlider->setProgress(progress);
    }
}

void ReaderActivity::updateProgress() {
    // Save reading progress to server
    vitasuwayomi::asyncRun([this]() {
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
        // End of chapter - mark as read
        markChapterAsRead();

        if (m_chapterIndex < m_totalChapters - 1) {
            auto* dialog = new brls::Dialog("End of chapter. Go to next?");
            dialog->addButton("Next Chapter", [this, dialog]() {
                dialog->dismiss();
                nextChapter();
            });
            dialog->addButton("Stay", [dialog]() {
                dialog->dismiss();
            });
            dialog->open();
        } else {
            brls::Application::notify("End of manga");
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
        auto* dialog = new brls::Dialog("Beginning of chapter. Go to previous?");
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
        markChapterAsRead();

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
    vitasuwayomi::asyncRun([this]() {
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
    if (topBar) {
        topBar->setVisibility(brls::Visibility::VISIBLE);
    }
    if (bottomBar) {
        bottomBar->setVisibility(brls::Visibility::VISIBLE);
    }
    m_controlsVisible = true;
}

void ReaderActivity::hideControls() {
    if (topBar) {
        topBar->setVisibility(brls::Visibility::GONE);
    }
    if (bottomBar) {
        bottomBar->setVisibility(brls::Visibility::GONE);
    }
    m_controlsVisible = false;
}

void ReaderActivity::showSettings() {
    auto* dialog = new brls::Dialog("Reader Settings");

    // Page orientation option (Horizontal / Vertical)
    std::string orientText = (m_settings.orientation == PageOrientation::HORIZONTAL) ?
                             "Orientation: Horizontal" : "Orientation: Vertical";
    dialog->addButton(orientText, [this, dialog]() {
        dialog->dismiss();

        // Toggle orientation
        if (m_settings.orientation == PageOrientation::HORIZONTAL) {
            m_settings.orientation = PageOrientation::VERTICAL;
            brls::Application::notify("Orientation: Vertical (Up/Down)");
        } else {
            m_settings.orientation = PageOrientation::HORIZONTAL;
            brls::Application::notify("Orientation: Horizontal (Left/Right)");
        }
    });

    // Reading direction option (cycles through 3 options)
    std::string dirText;
    switch (m_settings.direction) {
        case ReaderDirection::LEFT_TO_RIGHT:
            dirText = "Direction: Left to Right";
            break;
        case ReaderDirection::RIGHT_TO_LEFT:
            dirText = "Direction: Right to Left";
            break;
        case ReaderDirection::TOP_TO_BOTTOM:
            dirText = "Direction: Top to Bottom";
            break;
    }
    dialog->addButton(dirText, [this, dialog]() {
        dialog->dismiss();

        // Cycle reading direction
        switch (m_settings.direction) {
            case ReaderDirection::LEFT_TO_RIGHT:
                m_settings.direction = ReaderDirection::RIGHT_TO_LEFT;
                brls::Application::notify("Direction: Right to Left");
                break;
            case ReaderDirection::RIGHT_TO_LEFT:
                m_settings.direction = ReaderDirection::TOP_TO_BOTTOM;
                m_settings.orientation = PageOrientation::VERTICAL; // Auto-switch to vertical
                brls::Application::notify("Direction: Top to Bottom");
                break;
            case ReaderDirection::TOP_TO_BOTTOM:
                m_settings.direction = ReaderDirection::LEFT_TO_RIGHT;
                brls::Application::notify("Direction: Left to Right");
                break;
        }
    });

    // Scaling mode option
    std::string scaleText;
    switch (m_settings.scaleMode) {
        case ReaderScaleMode::FIT_SCREEN: scaleText = "Scale: Fit Screen"; break;
        case ReaderScaleMode::FIT_WIDTH: scaleText = "Scale: Fit Width"; break;
        case ReaderScaleMode::FIT_HEIGHT: scaleText = "Scale: Fit Height"; break;
        case ReaderScaleMode::ORIGINAL: scaleText = "Scale: Original"; break;
    }
    dialog->addButton(scaleText, [this, dialog]() {
        dialog->dismiss();

        // Cycle scale mode
        switch (m_settings.scaleMode) {
            case ReaderScaleMode::FIT_SCREEN:
                m_settings.scaleMode = ReaderScaleMode::FIT_WIDTH;
                brls::Application::notify("Scale: Fit Width");
                break;
            case ReaderScaleMode::FIT_WIDTH:
                m_settings.scaleMode = ReaderScaleMode::FIT_HEIGHT;
                brls::Application::notify("Scale: Fit Height");
                break;
            case ReaderScaleMode::FIT_HEIGHT:
                m_settings.scaleMode = ReaderScaleMode::ORIGINAL;
                brls::Application::notify("Scale: Original");
                break;
            case ReaderScaleMode::ORIGINAL:
                m_settings.scaleMode = ReaderScaleMode::FIT_SCREEN;
                brls::Application::notify("Scale: Fit Screen");
                break;
        }
        applySettings();
    });

    dialog->addButton("Close", [dialog]() {
        dialog->dismiss();
    });

    dialog->open();
}

void ReaderActivity::applySettings() {
    // Apply scaling mode
    if (pageImage) {
        switch (m_settings.scaleMode) {
            case ReaderScaleMode::FIT_SCREEN:
                pageImage->setScalingType(brls::ImageScalingType::FIT);
                break;
            case ReaderScaleMode::FIT_WIDTH:
                pageImage->setScalingType(brls::ImageScalingType::STRETCH);
                break;
            case ReaderScaleMode::FIT_HEIGHT:
                pageImage->setScalingType(brls::ImageScalingType::FIT);
                break;
            case ReaderScaleMode::ORIGINAL:
                // Use FILL to show original size (centers and fills without scaling down)
                pageImage->setScalingType(brls::ImageScalingType::FILL);
                break;
        }
    }
}

void ReaderActivity::handleTouch(brls::Point point) {
    // This can be used for more advanced touch handling
    // Currently handled by click actions on views
}

void ReaderActivity::handleTouchNavigation(float x, float screenWidth) {
    // Left 30% of screen = previous page
    // Right 30% of screen = next page
    // Center 40% = toggle controls

    float leftThreshold = screenWidth * 0.3f;
    float rightThreshold = screenWidth * 0.7f;

    if (x < leftThreshold) {
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
            nextPage();
        } else {
            previousPage();
        }
    } else if (x > rightThreshold) {
        if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
            previousPage();
        } else {
            nextPage();
        }
    } else {
        toggleControls();
    }
}

} // namespace vitasuwayomi
