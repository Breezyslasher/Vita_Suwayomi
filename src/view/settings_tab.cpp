/**
 * VitaSuwayomi - Settings Tab implementation
 * Settings for manga reader app
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include <algorithm>

// Version defined in CMakeLists.txt or here
#ifndef VITA_SUWAYOMI_VERSION
#define VITA_SUWAYOMI_VERSION "1.0.0"
#endif

namespace vitasuwayomi {

SettingsTab::SettingsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);

    // Create scrolling container
    m_scrollView = new brls::ScrollingFrame();
    m_scrollView->setGrow(1.0f);

    m_contentBox = new brls::Box();
    m_contentBox->setAxis(brls::Axis::COLUMN);
    m_contentBox->setPadding(20);
    m_contentBox->setGrow(1.0f);

    // Create all sections
    createAccountSection();
    createUISection();
    createLibrarySection();
    createReaderSection();
    createDownloadsSection();
    createAboutSection();

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Server");
    m_contentBox->addView(header);

    // Server info cell
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Server: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(16);
    m_contentBox->addView(m_serverLabel);

    // Disconnect button
    auto* disconnectCell = new brls::DetailCell();
    disconnectCell->setText("Disconnect");
    disconnectCell->setDetailText("Disconnect from current server");
    disconnectCell->registerClickAction([this](brls::View* view) {
        onDisconnect();
        return true;
    });
    m_contentBox->addView(disconnectCell);
}

void SettingsTab::createUISection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("User Interface");
    m_contentBox->addView(header);

    // Theme selector
    m_themeSelector = new brls::SelectorCell();
    m_themeSelector->init("Theme", {"System", "Light", "Dark"}, static_cast<int>(settings.theme),
        [this](int index) {
            onThemeChanged(index);
        });
    m_contentBox->addView(m_themeSelector);

    // Show clock toggle
    m_clockToggle = new brls::BooleanCell();
    m_clockToggle->init("Show Clock", settings.showClock, [&settings](bool value) {
        settings.showClock = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_clockToggle);

    // Animations toggle
    m_animationsToggle = new brls::BooleanCell();
    m_animationsToggle->init("Enable Animations", settings.animationsEnabled, [&settings](bool value) {
        settings.animationsEnabled = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_animationsToggle);

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [&settings](bool value) {
        settings.debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_debugLogToggle);
}

void SettingsTab::createLibrarySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Library");
    m_contentBox->addView(header);

    // Hide categories cell
    m_hideCategoriesCell = new brls::DetailCell();
    m_hideCategoriesCell->setText("Hidden Categories");

    // Show count of hidden categories
    size_t hiddenCount = settings.hiddenCategoryIds.size();
    m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");

    m_hideCategoriesCell->registerClickAction([this](brls::View* view) {
        showCategoryVisibilityDialog();
        return true;
    });
    m_contentBox->addView(m_hideCategoriesCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Select which categories to show in library");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(4);
    infoLabel->setMarginBottom(8);
    m_contentBox->addView(infoLabel);

    // Cache Library Data toggle
    auto* cacheDataToggle = new brls::BooleanCell();
    cacheDataToggle->init("Cache Library Data", settings.cacheLibraryData, [&settings](bool value) {
        settings.cacheLibraryData = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cacheDataToggle);

    // Cache Cover Images toggle
    auto* cacheCoverToggle = new brls::BooleanCell();
    cacheCoverToggle->init("Cache Cover Images", settings.cacheCoverImages, [&settings](bool value) {
        settings.cacheCoverImages = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cacheCoverToggle);

    // Cache info label
    auto* cacheInfoLabel = new brls::Label();
    cacheInfoLabel->setText("Caching speeds up library loading");
    cacheInfoLabel->setFontSize(14);
    cacheInfoLabel->setMarginLeft(16);
    cacheInfoLabel->setMarginTop(4);
    m_contentBox->addView(cacheInfoLabel);

    // Clear Cache button
    auto* clearCacheCell = new brls::DetailCell();
    clearCacheCell->setText("Clear Cache");
    clearCacheCell->setDetailText("Delete cached data and images");
    clearCacheCell->registerClickAction([](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Clear all cached data?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Clear", [dialog]() {
            LibraryCache::getInstance().clearAllCache();
            dialog->close();
            brls::Application::notify("Cache cleared");
        });

        dialog->open();
        return true;
    });
    m_contentBox->addView(clearCacheCell);
}

void SettingsTab::showCategoryVisibilityDialog() {
    // Fetch categories from server
    brls::Application::notify("Loading categories...");

    // Get categories synchronously (simple approach)
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Category> categories;

    if (!client.fetchCategories(categories)) {
        brls::Application::notify("Failed to load categories");
        return;
    }

    // Sort by order
    std::sort(categories.begin(), categories.end(),
        [](const Category& a, const Category& b) {
            return a.order < b.order;
        });

    // Create dialog
    brls::Dialog* dialog = new brls::Dialog("Category Visibility");

    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setWidth(450);
    scrollView->setHeight(350);

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(10);

    auto& hiddenIds = Application::getInstance().getSettings().hiddenCategoryIds;

    // Add toggle for each category
    for (const auto& cat : categories) {
        // Skip empty categories
        if (cat.mangaCount <= 0) continue;

        auto* toggle = new brls::BooleanCell();
        std::string catName = cat.name;
        if (catName.length() > 18) {
            catName = catName.substr(0, 16) + "..";
        }

        // Category is visible if NOT in hidden list
        bool isVisible = (hiddenIds.find(cat.id) == hiddenIds.end());

        int catId = cat.id;
        toggle->init(catName + " (" + std::to_string(cat.mangaCount) + ")", isVisible,
            [catId](bool value) {
                auto& hidden = Application::getInstance().getSettings().hiddenCategoryIds;
                if (value) {
                    // Show category (remove from hidden)
                    hidden.erase(catId);
                } else {
                    // Hide category (add to hidden)
                    hidden.insert(catId);
                }
                Application::getInstance().saveSettings();
            });

        contentBox->addView(toggle);
    }

    // If no categories with manga
    if (categories.empty()) {
        auto* label = new brls::Label();
        label->setText("No categories found");
        label->setFontSize(16);
        contentBox->addView(label);
    }

    scrollView->setContentView(contentBox);
    dialog->addView(scrollView);

    dialog->addButton("Done", [dialog, this]() {
        dialog->close();
        // Update the cell detail text
        if (m_hideCategoriesCell) {
            size_t hiddenCount = Application::getInstance().getSettings().hiddenCategoryIds.size();
            m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");
        }
    });

    dialog->open();
}

void SettingsTab::createReaderSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Reader");
    m_contentBox->addView(header);

    // Reading mode selector
    m_readingModeSelector = new brls::SelectorCell();
    m_readingModeSelector->init("Reading Mode",
        {"Left to Right", "Right to Left (Manga)", "Vertical", "Webtoon"},
        static_cast<int>(settings.readingMode),
        [&settings](int index) {
            settings.readingMode = static_cast<ReadingMode>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(m_readingModeSelector);

    // Page scale mode selector
    m_pageScaleModeSelector = new brls::SelectorCell();
    m_pageScaleModeSelector->init("Page Scale",
        {"Fit Screen", "Fit Width", "Fit Height", "Original"},
        static_cast<int>(settings.pageScaleMode),
        [&settings](int index) {
            settings.pageScaleMode = static_cast<PageScaleMode>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(m_pageScaleModeSelector);

    // Reader background selector
    m_readerBgSelector = new brls::SelectorCell();
    m_readerBgSelector->init("Background",
        {"Black", "White", "Gray"},
        static_cast<int>(settings.readerBackground),
        [&settings](int index) {
            settings.readerBackground = static_cast<ReaderBackground>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(m_readerBgSelector);

    // Keep screen on toggle
    auto* keepScreenOnToggle = new brls::BooleanCell();
    keepScreenOnToggle->init("Keep Screen On", settings.keepScreenOn, [&settings](bool value) {
        settings.keepScreenOn = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(keepScreenOnToggle);

    // Show page number toggle
    auto* showPageNumToggle = new brls::BooleanCell();
    showPageNumToggle->init("Show Page Number", settings.showPageNumber, [&settings](bool value) {
        settings.showPageNumber = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(showPageNumToggle);

    // Tap to navigate toggle
    auto* tapNavToggle = new brls::BooleanCell();
    tapNavToggle->init("Tap to Navigate", settings.tapToNavigate, [&settings](bool value) {
        settings.tapToNavigate = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(tapNavToggle);
}

void SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Downloads");
    m_contentBox->addView(header);

    // Download to server toggle (use server-side downloads)
    auto* serverDownloadToggle = new brls::BooleanCell();
    serverDownloadToggle->init("Download to Server", settings.downloadToServer, [&settings](bool value) {
        settings.downloadToServer = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(serverDownloadToggle);

    // Info label for server downloads
    auto* serverDlInfoLabel = new brls::Label();
    serverDlInfoLabel->setText("When enabled, downloads are stored on the Suwayomi server");
    serverDlInfoLabel->setFontSize(14);
    serverDlInfoLabel->setMarginLeft(16);
    serverDlInfoLabel->setMarginTop(4);
    m_contentBox->addView(serverDlInfoLabel);

    // Auto download new chapters toggle
    auto* autoDownloadToggle = new brls::BooleanCell();
    autoDownloadToggle->init("Auto-Download New Chapters", settings.autoDownloadChapters, [&settings](bool value) {
        settings.autoDownloadChapters = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(autoDownloadToggle);

    // WiFi only toggle
    auto* wifiOnlyToggle = new brls::BooleanCell();
    wifiOnlyToggle->init("Download Over WiFi Only", settings.downloadOverWifiOnly, [&settings](bool value) {
        settings.downloadOverWifiOnly = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(wifiOnlyToggle);

    // Concurrent downloads selector
    auto* concurrentSelector = new brls::SelectorCell();
    concurrentSelector->init("Max Concurrent Downloads",
        {"1", "2", "3"},
        settings.maxConcurrentDownloads - 1,
        [&settings](int index) {
            settings.maxConcurrentDownloads = index + 1;
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(concurrentSelector);

    // Delete after read toggle
    auto* deleteAfterReadToggle = new brls::BooleanCell();
    deleteAfterReadToggle->init("Delete After Reading", settings.deleteAfterRead, [&settings](bool value) {
        settings.deleteAfterRead = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(deleteAfterReadToggle);

    // Sync progress now button
    auto* syncNowCell = new brls::DetailCell();
    syncNowCell->setText("Sync Progress Now");
    syncNowCell->setDetailText("Upload offline progress to server");
    syncNowCell->registerClickAction([](brls::View* view) {
        DownloadsManager::getInstance().syncProgressToServer();
        brls::Application::notify("Progress synced to server");
        return true;
    });
    m_contentBox->addView(syncNowCell);

    // Clear all downloads
    m_clearDownloadsCell = new brls::DetailCell();
    m_clearDownloadsCell->setText("Clear All Downloads");
    auto downloads = DownloadsManager::getInstance().getDownloads();
    m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " manga");
    m_clearDownloadsCell->registerClickAction([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Delete all downloaded content?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete All", [dialog, this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 manga");
            }
            dialog->close();
            brls::Application::notify("All downloads deleted");
        });

        dialog->open();
        return true;
    });
    m_contentBox->addView(m_clearDownloadsCell);

    // Downloads storage path info
    auto* pathLabel = new brls::Label();
    pathLabel->setText("Storage: " + DownloadsManager::getInstance().getDownloadsPath());
    pathLabel->setFontSize(14);
    pathLabel->setMarginLeft(16);
    pathLabel->setMarginTop(8);
    m_contentBox->addView(pathLabel);
}

void SettingsTab::createAboutSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("About");
    m_contentBox->addView(header);

    // Version info
    auto* versionCell = new brls::DetailCell();
    versionCell->setText("Version");
    versionCell->setDetailText(VITA_SUWAYOMI_VERSION);
    m_contentBox->addView(versionCell);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("VitaSuwayomi - Manga Reader for PlayStation Vita");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    m_contentBox->addView(descLabel);

    // Server info
    auto* serverInfoLabel = new brls::Label();
    serverInfoLabel->setText("Connects to Suwayomi-Server");
    serverInfoLabel->setFontSize(14);
    serverInfoLabel->setMarginLeft(16);
    serverInfoLabel->setMarginTop(4);
    m_contentBox->addView(serverInfoLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    m_contentBox->addView(creditLabel);
}

void SettingsTab::onDisconnect() {
    brls::Dialog* dialog = new brls::Dialog("Disconnect from server?");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Disconnect", [dialog, this]() {
        dialog->close();

        // Clear server connection
        Application::getInstance().setServerUrl("");
        Application::getInstance().setConnected(false);
        Application::getInstance().saveSettings();

        // Go back to login
        Application::getInstance().pushLoginActivity();
    });

    dialog->open();
}

void SettingsTab::onThemeChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.theme = static_cast<AppTheme>(index);
    app.applyTheme();
    app.saveSettings();
}

} // namespace vitasuwayomi
