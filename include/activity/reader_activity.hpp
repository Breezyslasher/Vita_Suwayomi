/**
 * VitaSuwayomi - Manga Reader Activity
 * Displays manga pages for reading with navigation controls
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"

namespace vitasuwayomi {

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
    void toggleFullscreen();
    void setReadingMode(ReadingMode mode);
    void setPageScaleMode(PageScaleMode mode);

    // Get current state
    int getCurrentPage() const { return m_currentPage; }
    int getPageCount() const { return static_cast<int>(m_pages.size()); }
    int getMangaId() const { return m_mangaId; }
    int getChapterIndex() const { return m_chapterIndex; }

private:
    void loadPages();
    void loadPage(int index);
    void updatePageDisplay();
    void updateProgress();
    void showControls();
    void hideControls();
    void preloadAdjacentPages();
    void markChapterAsRead();

    // UI components
    BRLS_BIND(brls::Image, pageImage, "reader/page_image");
    BRLS_BIND(brls::Box, topBar, "reader/top_bar");
    BRLS_BIND(brls::Box, bottomBar, "reader/bottom_bar");
    BRLS_BIND(brls::Label, pageLabel, "reader/page_label");
    BRLS_BIND(brls::Label, chapterLabel, "reader/chapter_label");
    BRLS_BIND(brls::Slider, pageSlider, "reader/page_slider");
    BRLS_BIND(brls::Button, prevChapterBtn, "reader/prev_chapter");
    BRLS_BIND(brls::Button, nextChapterBtn, "reader/next_chapter");

    // Manga/Chapter info
    int m_mangaId = 0;
    int m_chapterIndex = 0;
    std::string m_mangaTitle;
    std::string m_chapterName;

    // Pages
    std::vector<Page> m_pages;
    int m_currentPage = 0;
    int m_startPage = 0;

    // Reading state
    ReadingMode m_readingMode = ReadingMode::RIGHT_TO_LEFT;
    PageScaleMode m_scaleMode = PageScaleMode::FIT_SCREEN;
    bool m_controlsVisible = false;
    bool m_isFullscreen = false;

    // Chapter navigation
    std::vector<Chapter> m_chapters;
    int m_totalChapters = 0;

    // Image caching (preloaded pages)
    std::map<int, std::string> m_cachedImages;
};

} // namespace vitasuwayomi
