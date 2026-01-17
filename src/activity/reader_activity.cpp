/**
 * VitaSuwayomi - Manga Reader Activity implementation
 * NOBORU-style UI with tap to show/hide controls and touch navigation
 */

#include "activity/reader_activity.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"
#include "utils/async.hpp"

#include <borealis.hpp>
#include <cmath>

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

    // Set manga title in top bar
    if (mangaLabel) {
        mangaLabel->setText(m_mangaTitle);
    }

    // Start with controls hidden, page counter visible
    hideControls();
    showPageCounter();

    // Close reader with Circle button (back)
    this->registerAction("Close", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::Application::popActivity();
        return true;
    });

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

    // Toggle controls with Y or Start button (NOBORU style)
    this->registerAction("Toggle Controls", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        toggleControls();
        return true;
    });

    this->registerAction("Menu", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        toggleControls();
        return true;
    });

    // NOBORU-style touch handling
    // Uses PanGestureRecognizer to track touch movement
    // - Swipe threshold: 10px to detect swipe start
    // - Page turn threshold: 90px to trigger page change
    if (pageImage) {
        pageImage->setFocusable(true);

        // Pan gesture for swipe detection (NOBORU style)
        pageImage->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::START) {
                    m_isPanning = false;  // Will be set to true if we move enough
                    m_touchStart = status.position;
                    m_touchCurrent = status.position;
                    brls::Logger::info("Touch START at ({}, {})", status.position.x, status.position.y);
                } else if (status.state == brls::GestureState::END) {
                    m_touchCurrent = status.position;

                    // Calculate distance moved (NOBORU style)
                    float dx = m_touchCurrent.x - m_touchStart.x;
                    float dy = m_touchCurrent.y - m_touchStart.y;
                    float distance = std::sqrt(dx * dx + dy * dy);

                    brls::Logger::info("Touch END: start=({},{}), end=({},{}), dx={}, dy={}, dist={}",
                        m_touchStart.x, m_touchStart.y,
                        m_touchCurrent.x, m_touchCurrent.y,
                        dx, dy, distance);

                    // NOBORU thresholds: 10px for swipe detection, 90px for page turn
                    const float SWIPE_DETECT_THRESHOLD = 10.0f;
                    const float PAGE_TURN_THRESHOLD = 90.0f;

                    if (distance < SWIPE_DETECT_THRESHOLD) {
                        // Too short - treat as tap
                        brls::Logger::info("Tap detected (distance {} < {})", distance, SWIPE_DETECT_THRESHOLD);
                        toggleControls();
                    } else {
                        // It's a swipe - check if long enough to turn page
                        m_isPanning = true;

                        float absX = std::abs(dx);
                        float absY = std::abs(dy);

                        if (absX > absY) {
                            // Horizontal swipe
                            if (absX >= PAGE_TURN_THRESHOLD) {
                                brls::Logger::info("Horizontal page turn: dx={}", dx);
                                if (dx > 0) {
                                    // Swipe right
                                    if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                        nextPage();
                                    } else {
                                        previousPage();
                                    }
                                } else {
                                    // Swipe left
                                    if (m_settings.direction == ReaderDirection::RIGHT_TO_LEFT) {
                                        previousPage();
                                    } else {
                                        nextPage();
                                    }
                                }
                            } else {
                                brls::Logger::info("Horizontal swipe too short ({} < {})", absX, PAGE_TURN_THRESHOLD);
                            }
                        } else {
                            // Vertical swipe
                            if (absY >= PAGE_TURN_THRESHOLD) {
                                brls::Logger::info("Vertical page turn: dy={}", dy);
                                if (dy > 0) {
                                    // Swipe down
                                    previousPage();
                                } else {
                                    // Swipe up
                                    nextPage();
                                }
                            } else {
                                brls::Logger::info("Vertical swipe too short ({} < {})", absY, PAGE_TURN_THRESHOLD);
                            }
                        }
                    }

                    m_isPanning = false;
                }
            }, brls::PanAxis::ANY));
    }

    // Container fallback with same logic
    if (container) {
        container->addGestureRecognizer(new brls::PanGestureRecognizer(
            [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
                if (status.state == brls::GestureState::START) {
                    m_touchStart = status.position;
                } else if (status.state == brls::GestureState::END) {
                    float dx = status.position.x - m_touchStart.x;
                    float dy = status.position.y - m_touchStart.y;
                    float distance = std::sqrt(dx * dx + dy * dy);

                    const float SWIPE_DETECT_THRESHOLD = 10.0f;
                    const float PAGE_TURN_THRESHOLD = 90.0f;

                    if (distance < SWIPE_DETECT_THRESHOLD) {
                        toggleControls();
                    } else if (std::abs(dx) > std::abs(dy) && std::abs(dx) >= PAGE_TURN_THRESHOLD) {
                        if (dx > 0) {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? nextPage() : previousPage();
                        } else {
                            m_settings.direction == ReaderDirection::RIGHT_TO_LEFT ? previousPage() : nextPage();
                        }
                    } else if (std::abs(dy) >= PAGE_TURN_THRESHOLD) {
                        dy > 0 ? previousPage() : nextPage();
                    }
                }
            }, brls::PanAxis::ANY));
    }

    // Back button in top bar
    if (backBtn) {
        backBtn->registerClickAction([this](brls::View*) {
            brls::Application::popActivity();
            return true;
        });
        backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(backBtn));
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
        prevChapterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(prevChapterBtn));
    }

    if (nextChapterBtn) {
        nextChapterBtn->registerClickAction([this](brls::View*) {
            nextChapter();
            return true;
        });
        nextChapterBtn->addGestureRecognizer(new brls::TapGestureRecognizer(nextChapterBtn));
    }

    // Set up settings button
    if (settingsBtn) {
        settingsBtn->registerClickAction([this](brls::View*) {
            showSettings();
            return true;
        });
        settingsBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsBtn));
    }

    // Update direction label
    updateDirectionLabel();

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

        // Update UI labels
        if (chapterLabel) {
            std::string label = m_chapterName.empty() ?
                "Chapter " + std::to_string(m_chapterIndex + 1) : m_chapterName;
            chapterLabel->setText(label);
        }

        if (chapterProgress) {
            chapterProgress->setText("Ch. " + std::to_string(m_chapterIndex + 1) +
                                     " of " + std::to_string(m_totalChapters));
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

    // Show page counter when navigating
    showPageCounter();
    schedulePageCounterHide();

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
    // Update page counter (top-right overlay)
    if (pageLabel) {
        pageLabel->setText(std::to_string(m_currentPage + 1) + "/" +
                          std::to_string(m_pages.size()));
    }

    // Update slider page label (in bottom bar)
    if (sliderPageLabel) {
        sliderPageLabel->setText("Page " + std::to_string(m_currentPage + 1) +
                                 " of " + std::to_string(m_pages.size()));
    }

    // Update slider position
    if (pageSlider && !m_pages.empty()) {
        float progress = static_cast<float>(m_currentPage) /
                        static_cast<float>(std::max(1, static_cast<int>(m_pages.size()) - 1));
        pageSlider->setProgress(progress);
    }
}

void ReaderActivity::updateDirectionLabel() {
    if (directionLabel) {
        switch (m_settings.direction) {
            case ReaderDirection::LEFT_TO_RIGHT:
                directionLabel->setText("LTR");
                break;
            case ReaderDirection::RIGHT_TO_LEFT:
                directionLabel->setText("RTL");
                break;
            case ReaderDirection::TOP_TO_BOTTOM:
                directionLabel->setText("TTB");
                break;
        }
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
    // Hide page counter when controls are visible (it's redundant)
    hidePageCounter();
    m_controlsVisible = true;
}

void ReaderActivity::hideControls() {
    if (topBar) {
        topBar->setVisibility(brls::Visibility::GONE);
    }
    if (bottomBar) {
        bottomBar->setVisibility(brls::Visibility::GONE);
    }
    // Show page counter when controls are hidden
    showPageCounter();
    m_controlsVisible = false;
}

void ReaderActivity::showPageCounter() {
    if (pageCounter) {
        pageCounter->setVisibility(brls::Visibility::VISIBLE);
    }
}

void ReaderActivity::hidePageCounter() {
    if (pageCounter) {
        pageCounter->setVisibility(brls::Visibility::GONE);
    }
}

void ReaderActivity::schedulePageCounterHide() {
    // Auto-hide page counter after 1.5 seconds (like NOBORU)
    // Note: In a real implementation, you'd use a timer/delayed callback
    // For now, the page counter stays visible until controls are shown
}

void ReaderActivity::showSettings() {
    auto* dialog = new brls::Dialog("Reader Settings");

    // Image rotation option (0, 90, 180, 270 degrees)
    std::string rotText;
    switch (m_settings.rotation) {
        case ImageRotation::ROTATE_0: rotText = "Rotation: 0"; break;
        case ImageRotation::ROTATE_90: rotText = "Rotation: 90"; break;
        case ImageRotation::ROTATE_180: rotText = "Rotation: 180"; break;
        case ImageRotation::ROTATE_270: rotText = "Rotation: 270"; break;
    }
    dialog->addButton(rotText, [this]() {
        // Cycle rotation - apply first, then notify
        switch (m_settings.rotation) {
            case ImageRotation::ROTATE_0:
                m_settings.rotation = ImageRotation::ROTATE_90;
                break;
            case ImageRotation::ROTATE_90:
                m_settings.rotation = ImageRotation::ROTATE_180;
                break;
            case ImageRotation::ROTATE_180:
                m_settings.rotation = ImageRotation::ROTATE_270;
                break;
            case ImageRotation::ROTATE_270:
                m_settings.rotation = ImageRotation::ROTATE_0;
                break;
        }
        applySettings();
    });

    // Reading direction option (LTR for western, RTL for manga)
    std::string dirText;
    switch (m_settings.direction) {
        case ReaderDirection::LEFT_TO_RIGHT:
            dirText = "Direction: LTR";
            break;
        case ReaderDirection::RIGHT_TO_LEFT:
            dirText = "Direction: RTL";
            break;
        case ReaderDirection::TOP_TO_BOTTOM:
            dirText = "Direction: TTB";
            break;
    }
    dialog->addButton(dirText, [this]() {
        // Cycle reading direction
        switch (m_settings.direction) {
            case ReaderDirection::LEFT_TO_RIGHT:
                m_settings.direction = ReaderDirection::RIGHT_TO_LEFT;
                break;
            case ReaderDirection::RIGHT_TO_LEFT:
                m_settings.direction = ReaderDirection::TOP_TO_BOTTOM;
                break;
            case ReaderDirection::TOP_TO_BOTTOM:
                m_settings.direction = ReaderDirection::LEFT_TO_RIGHT;
                break;
        }
        updateDirectionLabel();
    });

    // Scaling mode option
    std::string scaleText;
    switch (m_settings.scaleMode) {
        case ReaderScaleMode::FIT_SCREEN: scaleText = "Scale: Fit"; break;
        case ReaderScaleMode::FIT_WIDTH: scaleText = "Scale: Width"; break;
        case ReaderScaleMode::FIT_HEIGHT: scaleText = "Scale: Height"; break;
        case ReaderScaleMode::ORIGINAL: scaleText = "Scale: Original"; break;
    }
    dialog->addButton(scaleText, [this]() {
        // Cycle scale mode
        switch (m_settings.scaleMode) {
            case ReaderScaleMode::FIT_SCREEN:
                m_settings.scaleMode = ReaderScaleMode::FIT_WIDTH;
                break;
            case ReaderScaleMode::FIT_WIDTH:
                m_settings.scaleMode = ReaderScaleMode::FIT_HEIGHT;
                break;
            case ReaderScaleMode::FIT_HEIGHT:
                m_settings.scaleMode = ReaderScaleMode::ORIGINAL;
                break;
            case ReaderScaleMode::ORIGINAL:
                m_settings.scaleMode = ReaderScaleMode::FIT_SCREEN;
                break;
        }
        applySettings();
    });

    dialog->setCancelable(true);
    dialog->open();
}

void ReaderActivity::applySettings() {
    if (!pageImage) return;

    // Apply scaling mode
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
            pageImage->setScalingType(brls::ImageScalingType::FILL);
            break;
    }

    // Apply rotation
    pageImage->setRotation(static_cast<float>(m_settings.rotation));
}

void ReaderActivity::handleTouch(brls::Point point) {
    // Reserved for advanced touch handling (pinch-to-zoom, etc.)
}

void ReaderActivity::handleTouchNavigation(float x, float screenWidth) {
    // Touch navigation via tap zones is disabled
    // Tapping anywhere now only toggles controls
    // Page navigation is done via swipe gestures or controller buttons
    toggleControls();
}

void ReaderActivity::handleSwipe(brls::Point delta) {
    // Legacy function - swipe handling is now inline in gesture recognizers
    // Kept for compatibility with header declaration
}

} // namespace vitasuwayomi
