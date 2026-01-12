/**
 * VitaSuwayomi - Manga Reader Activity
 * Displays manga pages for reading with navigation controls
 * NOBORU-style UI with tap to show/hide controls
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"

namespace vitasuwayomi {

// Reader scaling modes
enum class ReaderScaleMode {
    FIT_SCREEN,      // Fit entire page on screen
    FIT_WIDTH,       // Fit width, may need vertical scroll
    FIT_HEIGHT,      // Fit height, may need horizontal scroll
    ORIGINAL         // Original size
};

// Page orientation (scroll direction)
enum class PageOrientation {
    HORIZONTAL,      // Swipe left/right to change pages
    VERTICAL         // Scroll up/down to change pages
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
    PageOrientation orientation = PageOrientation::HORIZONTAL;
    ReaderScaleMode scaleMode = ReaderScaleMode::FIT_SCREEN;
    bool keepScreenOn = true;
};

class ReaderActivity : public brls::Activity {
public:
    // Create reader for a specific chapter
    ReaderActivity(int mangaId, int chapterIndex, const std::string& mangaTitle);

    // Create reader starting from a specific page
    ReaderActivity(int mangaId, int chapterIndex, int startPage, const std::string& mangaTitle);

    brls::View* createContentView() override;
    void onContentAvailable() override;

    // Navigation
    void nextPage();
    void previousPage();
    void goToPage(int pageIndex);
    void nextChapter();
    void previousChapter();

    // Controls
    void toggleControls();
    void showSettings();

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

    // Touch handling
    void handleTouch(brls::Point point);
    void handleTouchNavigation(float x, float screenWidth);

    // Page counter auto-hide
    void showPageCounter();
    void hidePageCounter();
    void schedulePageCounterHide();

    // UI components - NOBORU style
    BRLS_BIND(brls::Box, container, "reader/container");
    BRLS_BIND(brls::Image, pageImage, "reader/page_image");
    BRLS_BIND(brls::Box, topBar, "reader/top_bar");
    BRLS_BIND(brls::Box, bottomBar, "reader/bottom_bar");
    BRLS_BIND(brls::Box, pageCounter, "reader/page_counter");
    BRLS_BIND(brls::Label, pageLabel, "reader/page_label");
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

    // Manga/Chapter info
    int m_mangaId = 0;
    int m_chapterIndex = 0;
    std::string m_mangaTitle;
    std::string m_chapterName;

    // Pages
    std::vector<Page> m_pages;
    int m_currentPage = 0;
    int m_startPage = 0;

    // Reader settings
    ReaderSettings m_settings;
    bool m_controlsVisible = false;

    // Chapter navigation
    std::vector<Chapter> m_chapters;
    int m_totalChapters = 0;

    // Image caching (preloaded pages)
    std::map<int, std::string> m_cachedImages;
};

} // namespace vitasuwayomi
