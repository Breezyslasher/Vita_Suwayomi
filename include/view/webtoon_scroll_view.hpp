/**
 * WebtoonScrollView - Continuous vertical scrolling view for webtoon reading
 * Displays all pages as one long continuous strip with smooth scrolling
 */

#pragma once

#include <borealis.hpp>
#include <vector>
#include <set>
#include <functional>
#include "view/rotatable_image.hpp"
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

// Callback when scroll position changes (for progress tracking)
using ScrollProgressCallback = std::function<void(int currentPage, int totalPages, float scrollPercent)>;

// Callback when user taps (for toggling controls)
using TapCallback = std::function<void()>;

class WebtoonScrollView : public brls::Box {
public:
    WebtoonScrollView();
    ~WebtoonScrollView();

    /**
     * Set the pages to display
     * @param pages Vector of page data with image URLs
     * @param screenWidth Width available for pages
     */
    void setPages(const std::vector<Page>& pages, float screenWidth);

    /**
     * Clear all pages and reset scroll
     */
    void clearPages();

    /**
     * Scroll to a specific page index
     */
    void scrollToPage(int pageIndex);

    /**
     * Get the currently visible page (topmost page in view)
     */
    int getCurrentPage() const { return m_currentPage; }

    /**
     * Get total page count
     */
    int getPageCount() const { return static_cast<int>(m_pages.size()); }

    /**
     * Get current scroll position (0.0 to 1.0)
     */
    float getScrollProgress() const;

    /**
     * Set callback for scroll progress updates
     */
    void setProgressCallback(ScrollProgressCallback callback) { m_progressCallback = callback; }

    /**
     * Set callback for tap gesture (to toggle controls)
     */
    void setTapCallback(TapCallback callback) { m_tapCallback = callback; }

    /**
     * Set side padding percentage (0-20)
     */
    void setSidePadding(int percent);

    /**
     * Set rotation for all page images (0, 90, 180, 270 degrees)
     */
    void setRotation(float degrees);

    /**
     * Get current rotation
     */
    float getRotation() const { return m_rotationDegrees; }

    /**
     * Handle frame update for progressive loading
     */
    void onFrame();

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override;

    static brls::View* create();

private:
    // Setup touch gestures for scrolling
    void setupGestures();

    // Load images that are visible or near visible
    void updateVisibleImages();

    // Check if a page is in the visible range
    bool isPageVisible(int pageIndex) const;

    // Calculate the offset for a page (Y for vertical, X for horizontal)
    float getPageOffset(int pageIndex) const;

    // Check if layout should be horizontal (at 90 or 270 rotation)
    bool isHorizontalLayout() const;

    // Update current page based on scroll position
    void updateCurrentPage();

    // Apply momentum scrolling
    void applyMomentum();

    // Pages data
    std::vector<Page> m_pages;

    // Image containers for each page
    std::vector<RotatableImage*> m_pageImages;

    // Content box that holds all images
    brls::Box* m_contentBox = nullptr;

    // Scroll state
    float m_scrollY = 0.0f;           // Current scroll position (negative = scrolled down)
    float m_scrollVelocity = 0.0f;    // Current scroll velocity for momentum
    float m_totalHeight = 0.0f;       // Total content height
    float m_viewHeight = 0.0f;        // Visible area height
    float m_viewWidth = 0.0f;         // Visible area width

    // Touch tracking
    bool m_isTouching = false;
    brls::Point m_touchStart;
    brls::Point m_touchLast;
    float m_scrollAtTouchStart = 0.0f;
    std::chrono::steady_clock::time_point m_lastTouchTime;

    // Page tracking
    int m_currentPage = 0;
    std::set<int> m_loadedPages;      // Pages that have been loaded
    std::set<int> m_loadingPages;     // Pages currently being loaded

    // Layout
    float m_sidePadding = 0.0f;       // Padding on each side
    float m_pageGap = 0.0f;           // Gap between pages (0 for seamless webtoon)
    std::vector<float> m_pageHeights; // Height of each page
    float m_rotationDegrees = 0.0f;   // Image rotation (0, 90, 180, 270)

    // Callbacks
    ScrollProgressCallback m_progressCallback;
    TapCallback m_tapCallback;

    // Preload buffer - how many pages ahead/behind to load
    static constexpr int PRELOAD_PAGES = 3;

    // Momentum friction
    static constexpr float MOMENTUM_FRICTION = 0.95f;
    static constexpr float MOMENTUM_MIN_VELOCITY = 0.5f;

    // Touch thresholds
    static constexpr float TAP_THRESHOLD = 15.0f;
};

} // namespace vitasuwayomi
