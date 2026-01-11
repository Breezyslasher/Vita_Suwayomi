/**
 * VitaSuwayomi - Settings Tab
 * Application settings and server info
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class SettingsTab : public brls::Box {
public:
    SettingsTab();

private:
    void createServerSection();
    void createUISection();
    void createReaderSection();
    void createLibrarySection();
    void createDownloadsSection();
    void createExtensionsSection();
    void createTrackingSection();
    void createAboutSection();
    void createDebugSection();

    void onDisconnect();
    void onThemeChanged(int index);
    void onReadingModeChanged(int index);
    void onPageScaleModeChanged(int index);
    void onTriggerLibraryUpdate();
    void onManageCategories();
    void onManageExtensions();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Server section
    brls::Label* m_serverUrlLabel = nullptr;
    brls::Label* m_serverVersionLabel = nullptr;
    brls::DetailCell* m_disconnectCell = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_clockToggle = nullptr;
    brls::BooleanCell* m_animationsToggle = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Reader section
    brls::SelectorCell* m_readingModeSelector = nullptr;  // LTR, RTL, Vertical, Webtoon
    brls::SelectorCell* m_pageScaleSelector = nullptr;    // Fit, Width, Height, Original
    brls::BooleanCell* m_keepScreenOnToggle = nullptr;
    brls::BooleanCell* m_showPageNumberToggle = nullptr;
    brls::BooleanCell* m_tapToNavigateToggle = nullptr;
    brls::SelectorCell* m_backgroundColorSelector = nullptr;  // Black, White, Gray

    // Library section
    brls::DetailCell* m_updateLibraryCell = nullptr;
    brls::DetailCell* m_manageCategoriesCell = nullptr;
    brls::BooleanCell* m_updateOnStartToggle = nullptr;
    brls::BooleanCell* m_updateOnlyWifiToggle = nullptr;
    brls::SelectorCell* m_defaultCategorySelector = nullptr;

    // Downloads section
    brls::BooleanCell* m_downloadToServerToggle = nullptr;  // Download to server vs local
    brls::BooleanCell* m_autoDownloadToggle = nullptr;
    brls::BooleanCell* m_wifiOnlyDownloadToggle = nullptr;
    brls::SelectorCell* m_concurrentDownloadsSelector = nullptr;
    brls::BooleanCell* m_deleteAfterReadToggle = nullptr;
    brls::DetailCell* m_clearDownloadsCell = nullptr;
    brls::Label* m_downloadStatsLabel = nullptr;

    // Extensions section
    brls::DetailCell* m_manageExtensionsCell = nullptr;
    brls::Label* m_installedExtensionsLabel = nullptr;

    // Tracking section
    brls::DetailCell* m_myAnimeListCell = nullptr;
    brls::DetailCell* m_aniListCell = nullptr;
    brls::DetailCell* m_mangaUpdatesCell = nullptr;
};

} // namespace vitasuwayomi
