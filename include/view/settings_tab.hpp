/**
 * VitaSuwayomi - Settings Tab
 * Application settings for manga reader
 */

#pragma once

#include <borealis.hpp>

namespace vitasuwayomi {

class SettingsTab : public brls::Box {
public:
    SettingsTab();

private:
    void createAccountSection();
    void createUISection();
    void createLibrarySection();
    void createReaderSection();
    void createDownloadsSection();
    void createAboutSection();

    void onDisconnect();
    void onThemeChanged(int index);
    void showCategoryVisibilityDialog();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Account/Server section
    brls::Label* m_serverLabel = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_clockToggle = nullptr;
    brls::BooleanCell* m_animationsToggle = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Library section
    brls::DetailCell* m_hideCategoriesCell = nullptr;

    // Reader section
    brls::SelectorCell* m_readingModeSelector = nullptr;
    brls::SelectorCell* m_pageScaleModeSelector = nullptr;
    brls::SelectorCell* m_readerBgSelector = nullptr;

    // Downloads section
    brls::DetailCell* m_clearDownloadsCell = nullptr;
};

} // namespace vitasuwayomi
