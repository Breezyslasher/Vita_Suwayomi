/**
 * VitaSuwayomi - Settings Tab
 * Application settings for manga reader
 */

#pragma once

#include <borealis.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class SettingsTab : public brls::Box {
public:
    SettingsTab();
    ~SettingsTab();

    void willDisappear(bool resetState) override;

private:
    void addSectionSeparator();
    void createAccountSection();
    void createTrackingSection();
    void createUISection();
    void createLibrarySection();
    void createReaderSection();
    void createDownloadsSection();
    void createBrowseSection();
    void createSyncYomiSection();
    void createBackupSection();
    void createStatisticsSection();
    void createAboutSection();

    // Rail + detail (two-pane) settings shell.
    brls::Box*  makeSectionBox();
    brls::Box*  makeRailRow(const std::string& icon, const std::string& title, int sectionId);
    brls::Box*  makeRailInfoRow(const std::string& icon, const std::string& title);
    void        showSection(int sectionId);
    void        paintRailRowSelection();

    // Radio-style single-choice popover (Options popover with one row per option).
    void        showChoicePopover(const std::string& title,
                                  const std::vector<std::string>& options,
                                  int currentIndex,
                                  std::function<void(int)> onSelect);

    void onDisconnect();
    void showLanguageFilterDialog();
    void updateLanguageFilterCellText();
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
    void checkForUpdates();
    void runNetworkTest();
    void showUpdateDialog(const std::string& newVersion, const std::string& releaseNotes,
                          const std::string& downloadUrl);
    void downloadAndInstallUpdate(const std::string& downloadUrl, const std::string& version);

    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_contentBox = nullptr;   // redirected per-section while pre-building

    // Two-pane shell.
    brls::Box* m_railContainer = nullptr;
    brls::ScrollingFrame* m_railScroll = nullptr;
    brls::Box* m_railBox = nullptr;
    brls::Box* m_detailContainer = nullptr;
    brls::ScrollingFrame* m_detailScroll = nullptr;
    brls::Box* m_detailContent = nullptr;
    brls::Label* m_detailTitle = nullptr;
    brls::Label* m_detailSubtitle = nullptr;
    std::vector<brls::Box*> m_sectionBoxes;
    std::vector<brls::Box*> m_railRows;
    std::vector<brls::Box*> m_railBars;
    std::vector<std::string> m_sectionNames;
    std::vector<std::string> m_sectionSubtitles;
    brls::Box* m_attachedSection = nullptr;
    int m_activeSection = 0;

    // Account/Server section
    brls::Label* m_serverLabel = nullptr;
    brls::SelectorCell* m_urlModeSelector = nullptr;

    // UI section
    brls::SelectorCell* m_themeSelector = nullptr;
    brls::BooleanCell* m_debugLogToggle = nullptr;

    // Library section
    brls::DetailCell* m_hideCategoriesCell = nullptr;
    brls::DetailCell* m_defaultCategorySelector = nullptr;

    // Reader section
    brls::DetailCell* m_readingModeSelector = nullptr;
    brls::DetailCell* m_pageScaleModeSelector = nullptr;
    brls::DetailCell* m_readerBgSelector = nullptr;

    // Downloads section
    brls::DetailCell* m_clearDownloadsCell = nullptr;

    // Browse section
    brls::DetailCell* m_languageFilterCell = nullptr;

    // Async lifetime guard
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
