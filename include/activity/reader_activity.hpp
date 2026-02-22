/**
 * VitaSuwayomi - Manga Reader Activity
 * Displays manga pages for reading with navigation controls
 * NOBORU-style UI with tap to show/hide controls
 */

#pragma once

#include <borealis.hpp>
#include <chrono>
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "view/rotatable_image.hpp"
#include "view/rotatable_label.hpp"
#include "view/webtoon_scroll_view.hpp"

namespace vitasuwayomi {

// Reader scaling modes
enum class ReaderScaleMode {
    FIT_SCREEN,      // Fit entire page on screen
    FIT_WIDTH,       // Fit width, may need vertical scroll
    FIT_HEIGHT,      // Fit height, may need horizontal scroll
    ORIGINAL         // Original size
};

// Image rotation (in degrees)
enum class ImageRotation {
    ROTATE_0 = 0,      // No rotation (default)
    ROTATE_90 = 90,    // 90 degrees clockwise
    ROTATE_180 = 180,  // 180 degrees (upside down)
    ROTATE_270 = 270   // 270 degrees clockwise (90 counter-clockwise)
};

// Reading direction
enum class ReaderDirection {
    LEFT_TO_RIGHT,   // Western comics style
    RIGHT_TO_LEFT,   // Manga style (default)
    TOP_TO_BOTTOM    // Webtoon/vertical style
};

// Reader settings
struct ReaderSettings {
    ReaderDirection direction = ReaderDirection::RIGHT_TO_LEFT;
    ImageRotation rotation = ImageRotation::ROTATE_0;
    ReaderScaleMode scaleMode = ReaderScaleMode::FIT_SCREEN;
    bool keepScreenOn = true;
    bool cropBorders = false;       // Auto-crop white/black borders
    int webtoonSidePadding = 0;     // Side padding percentage (0-20%)
    bool isWebtoonFormat = false;   // Treat as webtoon (vertical scroll, page splitting)
};

class ReaderActivity : public brls::Activity {
public:
    // Create reader for a specific chapter
    ReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle);

    // Create reader starting from a specific page
    ReaderActivity(int mangaId, int chapterIndex, int startPage, const std::string& mangaTitle);

    brls::View* createContentView() override;
    void onContentAvailable() override;
    void willDisappear(bool resetState = false) override;

    // Navigation
    void nextPage();
    void previousPage();
    void goToPage(int pageIndex);
    void nextChapter();
    void previousChapter();

    // Controls
    void toggleControls();
    void showSettings();
    void hideSettings();
    void updateSettingsLabels();

    // Get current state
    int getCurrentPage() const { return m_currentPage; }
    int getPageCount() const { return static_cast<int>(m_pages.size()); }
    int getMangaId() const { return m_mangaId; }
    int getChapterIndex() const { return m_chapterIndex; }

private:
    void loadPages();
    void loadPage(int index);
    void updatePageDisplay();
    void updateDirectionLabel();
    void updateProgress();
    void showControls();
    void hideControls();
    void preloadAdjacentPages();
    void markChapterAsRead();
    void applySettings();
    void saveSettingsToApp();  // Persist settings to AppSettings

    // Touch handling (implemented inline in gesture recognizers)

    // Page counter auto-hide
    void showPageCounter();
    void hidePageCounter();
    void schedulePageCounterHide();
    int m_pageCounterHideGeneration = 0;  // Generation counter to cancel stale auto-hide

    // Update page counter rotation/position
    void updatePageCounterRotation();

    // UI components - NOBORU style
    BRLS_BIND(brls::Box, container, "reader/container");
    BRLS_BIND(RotatableImage, pageImage, "reader/page_image");
    BRLS_BIND(RotatableImage, previewImage, "reader/preview_image");  // Preview page for swipe
    BRLS_BIND(brls::Box, topBar, "reader/top_bar");
    BRLS_BIND(brls::Box, bottomBar, "reader/bottom_bar");
    BRLS_BIND(RotatableLabel, pageCounter, "reader/page_counter");
    BRLS_BIND(brls::Label, mangaLabel, "reader/manga_label");
    BRLS_BIND(brls::Label, chapterLabel, "reader/chapter_label");
    BRLS_BIND(brls::Label, chapterProgress, "reader/chapter_progress");
    BRLS_BIND(brls::Label, sliderPageLabel, "reader/slider_page_label");
    BRLS_BIND(brls::Label, directionLabel, "reader/direction_label");
    BRLS_BIND(brls::Slider, pageSlider, "reader/page_slider");
    BRLS_BIND(brls::Button, backBtn, "reader/back_btn");
    BRLS_BIND(brls::Button, prevChapterBtn, "reader/prev_chapter");
    BRLS_BIND(brls::Button, nextChapterBtn, "reader/next_chapter");
    BRLS_BIND(brls::Button, settingsBtn, "reader/settings_btn");

    // Settings overlay panel
    BRLS_BIND(brls::Box, settingsOverlay, "reader/settings_overlay");
    BRLS_BIND(brls::Box, settingsPanel, "reader/settings_panel");
    BRLS_BIND(brls::Button, settingsFormatBtn, "reader/settings_format_btn");
    BRLS_BIND(brls::Button, settingsDirBtn, "reader/settings_dir_btn");
    BRLS_BIND(brls::Button, settingsRotBtn, "reader/settings_rot_btn");
    BRLS_BIND(brls::Button, settingsScaleBtn, "reader/settings_scale_btn");
    BRLS_BIND(brls::Label, settingsFormatLabel, "reader/settings_format_label");
    BRLS_BIND(brls::Label, settingsDirLabel, "reader/settings_dir_label");
    BRLS_BIND(brls::Label, settingsRotLabel, "reader/settings_rot_label");
    BRLS_BIND(brls::Label, settingsScaleLabel, "reader/settings_scale_label");

    // Webtoon continuous scroll view
    BRLS_BIND(WebtoonScrollView, webtoonScroll, "reader/webtoon_scroll");

    // Manga/Chapter info
    int m_mangaId = 0;
    int m_chapterIndex = 0;      // Chapter ID (server ID, NOT sequential number)
    int m_chapterPosition = -1;  // Position of current chapter in m_chapters list
    std::string m_mangaTitle;
    std::string m_chapterName;

    // Find position of current chapter in m_chapters by matching chapter ID
    void findChapterPosition();
    // Get display string for current chapter number
    std::string getChapterDisplayNumber() const;

    // Pages
    std::vector<Page> m_pages;
    int m_currentPage = 0;
    int m_startPage = 0;

    // Reader settings
    ReaderSettings m_settings;
    bool m_controlsVisible = false;
    bool m_settingsVisible = false;
    bool m_continuousScrollMode = false;  // True when using WebtoonScrollView

    // Alive flag for async callback safety
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

    // Switch between single-page and continuous scroll modes
    void updateReaderMode();

    // Chapter navigation
    std::vector<Chapter> m_chapters;
    int m_totalChapters = 0;

    // Next chapter preloading
    std::vector<Page> m_nextChapterPages;
    bool m_nextChapterLoaded = false;
    void preloadNextChapter();

    // Reader background color support
    void updateMarginColors();

    // Touch gesture tracking
    bool m_isPanning = false;
    brls::Point m_touchStart;
    brls::Point m_touchCurrent;

    // NOBORU-style swipe animation (shows next/prev page sliding in)
    bool m_isSwipeAnimating = false;
    float m_swipeOffset = 0.0f;           // Current swipe offset in pixels
    int m_previewPageIndex = -1;          // Index of page being previewed (-1 = none)
    bool m_swipingToNext = true;          // true = swiping to next page, false = previous
    void updateSwipePreview(float offset);
    void loadPreviewPage(int index);
    void completeSwipeAnimation(bool turnPage);
    void resetSwipeState();

    // NOBORU-style touch controls
    // Double-tap detection
    std::chrono::steady_clock::time_point m_lastTapTime;
    brls::Point m_lastTapPosition;
    static constexpr int DOUBLE_TAP_THRESHOLD_MS = 300;  // Max time between taps
    static constexpr float DOUBLE_TAP_DISTANCE = 50.0f;  // Max distance between taps

    // Zoom state
    bool m_isZoomed = false;
    float m_zoomLevel = 1.0f;
    brls::Point m_zoomOffset = {0, 0};

    // Multi-touch tracking for pinch-to-zoom
    bool m_isPinching = false;
    float m_initialPinchDistance = 0.0f;
    float m_initialZoomLevel = 1.0f;

    // Touch control methods
    void handleDoubleTap(brls::Point position);
    void handlePinchZoom(float scaleFactor);
    void resetZoom();
    void zoomTo(float level, brls::Point center);

    // Error overlay for failed page loads
    brls::Box* m_errorOverlay = nullptr;
    brls::Label* m_errorLabel = nullptr;
    brls::Button* m_retryButton = nullptr;
    int m_pageLoadGeneration = 0;   // Track current load to detect stale timeouts
    bool m_pageLoadSucceeded = false; // Set true when current page loads successfully
    bool m_loadedFromLocal = false;  // True when current chapter was loaded from local downloads
    void showPageError(const std::string& message);
    void hidePageError();
};

} // namespace vitasuwayomi
