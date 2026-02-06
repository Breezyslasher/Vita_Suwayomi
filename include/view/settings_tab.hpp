/**
 * VitaSuwayomi - Settings Tab
 * Application settings for manga reader
 */

#pragma once

#include <borealis.hpp>
#include <functional>
#include "app/suwayomi_client.hpp"

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
    void createBrowseSection();
    void createBackupSection();
    void createStatisticsSection();
    void createAboutSection();

    void onDisconnect();
    void showLanguageFilterDialog();
    void onThemeChanged(int index);
    void showCategoryVisibilityDialog();
    void showCategoryManagementDialog();
    void showCreateCategoryDialog();
    void showEditCategoryDialog(const Category& category);
    void showDeleteCategoryConfirmation(const Category& category);
    void showStorageManagement();
    void showStatisticsView();
    void exportBackup();
    void importBackup();
    void showUrlInputDialog(const std::string& title, const std::string& hint,
                            const std::string& currentValue,
                            std::function<void(const std::string&)> callback);
    void updateServerLabel();
    void refreshDefaultCategorySelector();

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;

    // Account/Server section
    brls::Label* m_serverLabel = nullptr;
    brls::SelectorCell* m_urlModeSelector = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_animationsToggle = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Library section
    brls::DetailCell* m_hideCategoriesCell = nullptr;
    brls::SelectorCell* m_defaultCategorySelector = nullptr;

    // Reader section
    brls::SelectorCell* m_readingModeSelector = nullptr;
    brls::SelectorCell* m_pageScaleModeSelector = nullptr;
    brls::SelectorCell* m_readerBgSelector = nullptr;

    // Downloads section
    brls::DetailCell* m_clearDownloadsCell = nullptr;
};

} // namespace vitasuwayomi
