/**
 * WebtoonScrollView implementation
 * Continuous vertical scrolling for webtoon reading
 */

#include "view/webtoon_scroll_view.hpp"
#include "utils/image_loader.hpp"
#include <cmath>

namespace vitasuwayomi {

WebtoonScrollView::WebtoonScrollView() {
    // Set up as a full-size container
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setGrow(1.0f);

    // Setup touch gestures
    setupGestures();
}

WebtoonScrollView::~WebtoonScrollView() {
    clearPages();
}

void WebtoonScrollView::setupGestures() {
    this->setFocusable(true);

    // Pan gesture for scrolling (ANY axis to support rotated views)
    this->addGestureRecognizer(new brls::PanGestureRecognizer(
        [this](brls::PanGestureStatus status, brls::Sound* soundToPlay) {
            if (status.state == brls::GestureState::START) {
                m_isTouching = true;
                m_touchStart = status.position;
                m_touchLast = status.position;
                m_scrollAtTouchStart = m_scrollY;
                m_scrollVelocity = 0.0f;
                m_lastTouchTime = std::chrono::steady_clock::now();
            } else if (status.state == brls::GestureState::STAY) {
                // Calculate raw deltas
                float rawDx = status.position.x - m_touchLast.x;
                float rawDy = status.position.y - m_touchLast.y;

                // Calculate scroll delta based on rotation
                // 0°: Normal vertical scrolling (dy)
                // 90°: Horizontal scrolling, swipe left = scroll down (-dx)
                // 180°: Inverted vertical scrolling (-dy)
                // 270°: Horizontal scrolling, swipe right = scroll down (dx)
                float scrollDelta = 0.0f;
                int rotation = static_cast<int>(m_rotationDegrees);

                if (rotation == 0) {
                    scrollDelta = rawDy;
                } else if (rotation == 90) {
                    scrollDelta = -rawDx;
                } else if (rotation == 180) {
                    scrollDelta = -rawDy;
                } else if (rotation == 270) {
                    scrollDelta = rawDx;
                }

                // Update scroll position
                m_scrollY += scrollDelta;

                // Clamp scroll position
                float maxScroll = 0.0f;
                float minScroll = -(m_totalHeight - m_viewHeight);
                if (minScroll > maxScroll) minScroll = maxScroll;

                m_scrollY = std::max(minScroll, std::min(maxScroll, m_scrollY));

                // Calculate velocity for momentum
                auto now = std::chrono::steady_clock::now();
                auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastTouchTime).count();
                if (dt > 0) {
                    m_scrollVelocity = scrollDelta / (dt / 16.67f);  // Normalize to ~60fps
                }

                m_touchLast = status.position;
                m_lastTouchTime = now;

                // Update visible images and current page
                updateVisibleImages();
                updateCurrentPage();
            } else if (status.state == brls::GestureState::END) {
                m_isTouching = false;

                // Check if it was a tap (minimal movement)
                float dx = status.position.x - m_touchStart.x;
                float dy = status.position.y - m_touchStart.y;
                float distance = std::sqrt(dx * dx + dy * dy);

                if (distance < TAP_THRESHOLD) {
                    // It's a tap - toggle controls via callback
                    m_scrollVelocity = 0.0f;
                    if (m_tapCallback) {
                        m_tapCallback();
                    }
                }
                // Otherwise, momentum will be applied in onFrame()
            }
        }, brls::PanAxis::ANY));
}

void WebtoonScrollView::setPages(const std::vector<Page>& pages, float screenWidth) {
    clearPages();

    m_pages = pages;
    m_viewWidth = screenWidth;
    m_viewHeight = 544.0f;  // PS Vita screen height

    // Calculate available width after padding
    float availableWidth = screenWidth - (m_sidePadding * 2);

    // For webtoons, we'll estimate heights based on typical aspect ratios
    // Images will be FIT_WIDTH, so height depends on aspect ratio
    // Default to 2:3 aspect ratio if unknown (will adjust when images load)
    float defaultHeight = availableWidth * 1.5f;  // 2:3 aspect ratio

    m_totalHeight = 0.0f;
    m_pageHeights.clear();
    m_pageHeights.reserve(pages.size());

    // Create image containers for each page
    for (size_t i = 0; i < pages.size(); i++) {
        auto* pageImg = new RotatableImage();
        pageImg->setWidth(availableWidth);
        pageImg->setHeight(defaultHeight);  // Will be adjusted when image loads
        pageImg->setScalingType(brls::ImageScalingType::FIT);
        pageImg->setBackgroundFillColor(nvgRGBA(26, 26, 46, 255));
        pageImg->setRotation(m_rotationDegrees);  // Apply current rotation

        m_pageImages.push_back(pageImg);
        m_pageHeights.push_back(defaultHeight);
        m_totalHeight += defaultHeight + m_pageGap;
    }

    // Remove trailing gap
    if (!pages.empty() && m_pageGap > 0) {
        m_totalHeight -= m_pageGap;
    }

    // Reset scroll
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_currentPage = 0;
    m_loadedPages.clear();
    m_loadingPages.clear();

    // Load initial visible images
    updateVisibleImages();

    brls::Logger::info("WebtoonScrollView: Set {} pages, totalHeight={}", pages.size(), m_totalHeight);
}

void WebtoonScrollView::clearPages() {
    for (auto* img : m_pageImages) {
        delete img;
    }
    m_pageImages.clear();
    m_pageHeights.clear();
    m_pages.clear();
    m_loadedPages.clear();
    m_loadingPages.clear();
    m_totalHeight = 0.0f;
    m_scrollY = 0.0f;
    m_scrollVelocity = 0.0f;
    m_currentPage = 0;
}

void WebtoonScrollView::scrollToPage(int pageIndex) {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pages.size())) {
        return;
    }

    // Calculate scroll position for this page
    float targetY = -getPageOffset(pageIndex);

    // Clamp scroll position
    float maxScroll = 0.0f;
    float minScroll = -(m_totalHeight - m_viewHeight);
    if (minScroll > maxScroll) minScroll = maxScroll;

    m_scrollY = std::max(minScroll, std::min(maxScroll, targetY));
    m_scrollVelocity = 0.0f;

    updateVisibleImages();
    updateCurrentPage();
}

float WebtoonScrollView::getScrollProgress() const {
    if (m_totalHeight <= m_viewHeight) {
        return 0.0f;
    }
    float scrollable = m_totalHeight - m_viewHeight;
    return std::min(1.0f, std::max(0.0f, -m_scrollY / scrollable));
}

void WebtoonScrollView::setSidePadding(int percent) {
    if (percent < 0 || percent > 20) {
        percent = 0;
    }
    float paddingRatio = percent / 100.0f;
    m_sidePadding = m_viewWidth * paddingRatio;
}

void WebtoonScrollView::setRotation(float degrees) {
    // Normalize to 0, 90, 180, 270
    int normalized = static_cast<int>(degrees) % 360;
    if (normalized < 0) normalized += 360;

    // Snap to nearest 90 degree increment
    if (normalized < 45) {
        m_rotationDegrees = 0.0f;
    } else if (normalized < 135) {
        m_rotationDegrees = 90.0f;
    } else if (normalized < 225) {
        m_rotationDegrees = 180.0f;
    } else if (normalized < 315) {
        m_rotationDegrees = 270.0f;
    } else {
        m_rotationDegrees = 0.0f;
    }

    // Apply rotation to all existing page images
    for (auto* img : m_pageImages) {
        if (img) {
            img->setRotation(m_rotationDegrees);
        }
    }

    brls::Logger::debug("WebtoonScrollView: setRotation({}) -> {} degrees", degrees, m_rotationDegrees);
}

void WebtoonScrollView::onFrame() {
    // Apply momentum scrolling when not touching
    if (!m_isTouching && std::abs(m_scrollVelocity) > MOMENTUM_MIN_VELOCITY) {
        m_scrollY += m_scrollVelocity;
        m_scrollVelocity *= MOMENTUM_FRICTION;

        // Clamp scroll position
        float maxScroll = 0.0f;
        float minScroll = -(m_totalHeight - m_viewHeight);
        if (minScroll > maxScroll) minScroll = maxScroll;

        // Bounce back if out of bounds
        if (m_scrollY > maxScroll) {
            m_scrollY = maxScroll;
            m_scrollVelocity = 0.0f;
        } else if (m_scrollY < minScroll) {
            m_scrollY = minScroll;
            m_scrollVelocity = 0.0f;
        }

        updateVisibleImages();
        updateCurrentPage();
    }
}

float WebtoonScrollView::getPageOffset(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return 0.0f;
    }

    float offset = 0.0f;
    for (int i = 0; i < pageIndex; i++) {
        offset += m_pageHeights[i] + m_pageGap;
    }
    return offset;
}

bool WebtoonScrollView::isPageVisible(int pageIndex) const {
    if (pageIndex < 0 || pageIndex >= static_cast<int>(m_pageHeights.size())) {
        return false;
    }

    float pageTop = getPageOffset(pageIndex);
    float pageBottom = pageTop + m_pageHeights[pageIndex];

    // Current visible range (scrollY is negative when scrolled down)
    float visibleTop = -m_scrollY;
    float visibleBottom = visibleTop + m_viewHeight;

    // Check if page overlaps with visible area
    return (pageBottom > visibleTop && pageTop < visibleBottom);
}

void WebtoonScrollView::updateVisibleImages() {
    // Find visible pages and preload nearby pages
    int firstVisible = -1;
    int lastVisible = -1;

    for (int i = 0; i < static_cast<int>(m_pages.size()); i++) {
        if (isPageVisible(i)) {
            if (firstVisible < 0) firstVisible = i;
            lastVisible = i;
        }
    }

    if (firstVisible < 0) {
        return;  // No visible pages
    }

    // Expand range for preloading
    int loadStart = std::max(0, firstVisible - PRELOAD_PAGES);
    int loadEnd = std::min(static_cast<int>(m_pages.size()) - 1, lastVisible + PRELOAD_PAGES);

    // Load pages in range
    for (int i = loadStart; i <= loadEnd; i++) {
        if (m_loadedPages.count(i) > 0 || m_loadingPages.count(i) > 0) {
            continue;  // Already loaded or loading
        }

        m_loadingPages.insert(i);

        const Page& page = m_pages[i];
        RotatableImage* img = m_pageImages[i];
        int pageIndex = i;

        // Load the image
        if (page.totalSegments > 1) {
            // Segmented webtoon page
            ImageLoader::loadAsyncFullSizeSegment(
                page.imageUrl, page.segment, page.totalSegments,
                [this, pageIndex, img](RotatableImage* loadedImg) {
                    m_loadingPages.erase(pageIndex);
                    m_loadedPages.insert(pageIndex);

                    // Update page height based on actual image dimensions
                    if (img->hasImage()) {
                        float imageWidth = static_cast<float>(img->getImageWidth());
                        float imageHeight = static_cast<float>(img->getImageHeight());

                        if (imageWidth > 0 && imageHeight > 0) {
                            float availableWidth = m_viewWidth - (m_sidePadding * 2);
                            float aspectRatio = imageHeight / imageWidth;
                            float newHeight = availableWidth * aspectRatio;

                            // Update total height
                            float oldHeight = m_pageHeights[pageIndex];
                            m_totalHeight += (newHeight - oldHeight);
                            m_pageHeights[pageIndex] = newHeight;
                            img->setHeight(newHeight);
                        }
                    }

                    brls::Logger::debug("WebtoonScrollView: Loaded segment page {}", pageIndex);
                }, img);
        } else {
            // Regular page
            ImageLoader::loadAsyncFullSize(page.imageUrl,
                [this, pageIndex, img](RotatableImage* loadedImg) {
                    m_loadingPages.erase(pageIndex);
                    m_loadedPages.insert(pageIndex);

                    // Update page height based on actual image dimensions
                    if (img->hasImage()) {
                        float imageWidth = static_cast<float>(img->getImageWidth());
                        float imageHeight = static_cast<float>(img->getImageHeight());

                        if (imageWidth > 0 && imageHeight > 0) {
                            float availableWidth = m_viewWidth - (m_sidePadding * 2);
                            float aspectRatio = imageHeight / imageWidth;
                            float newHeight = availableWidth * aspectRatio;

                            // Update total height
                            float oldHeight = m_pageHeights[pageIndex];
                            m_totalHeight += (newHeight - oldHeight);
                            m_pageHeights[pageIndex] = newHeight;
                            img->setHeight(newHeight);
                        }
                    }

                    brls::Logger::debug("WebtoonScrollView: Loaded page {}", pageIndex);
                }, img);
        }
    }
}

void WebtoonScrollView::updateCurrentPage() {
    // Find the page at the top of the visible area
    float visibleTop = -m_scrollY;

    int newCurrentPage = 0;
    float offset = 0.0f;

    for (int i = 0; i < static_cast<int>(m_pageHeights.size()); i++) {
        float pageBottom = offset + m_pageHeights[i];

        if (pageBottom > visibleTop) {
            newCurrentPage = i;
            break;
        }

        offset = pageBottom + m_pageGap;
    }

    if (newCurrentPage != m_currentPage) {
        m_currentPage = newCurrentPage;

        // Notify progress callback
        if (m_progressCallback) {
            m_progressCallback(m_currentPage, static_cast<int>(m_pages.size()), getScrollProgress());
        }
    }
}

void WebtoonScrollView::draw(NVGcontext* vg, float x, float y, float width, float height,
                              brls::Style style, brls::FrameContext* ctx) {
    // Update view dimensions
    m_viewWidth = width;
    m_viewHeight = height;

    // Draw background
    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGBA(26, 26, 46, 255));
    nvgFill(vg);

    // Save state and set scissor for clipping
    nvgSave(vg);
    nvgScissor(vg, x, y, width, height);

    // Calculate the X position for centered pages
    float availableWidth = width - (m_sidePadding * 2);
    float pageX = x + m_sidePadding;

    // Draw visible pages
    float currentY = y + m_scrollY;  // Start position adjusted by scroll

    for (int i = 0; i < static_cast<int>(m_pageImages.size()); i++) {
        float pageHeight = m_pageHeights[i];
        float pageBottom = currentY + pageHeight;

        // Check if page is in visible area (with some margin for smooth scrolling)
        if (pageBottom >= y - 100 && currentY <= y + height + 100) {
            // Draw this page
            RotatableImage* img = m_pageImages[i];
            if (img) {
                // Update image width in case view resized
                img->setWidth(availableWidth);

                // Draw the image
                img->draw(vg, pageX, currentY, availableWidth, pageHeight, style, ctx);
            }
        }

        currentY = pageBottom + m_pageGap;
    }

    // Restore state
    nvgRestore(vg);

    // Call onFrame for momentum updates
    onFrame();
}

brls::View* WebtoonScrollView::create() {
    return new WebtoonScrollView();
}

} // namespace vitasuwayomi
