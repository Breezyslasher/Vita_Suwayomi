/**
 * VitaSuwayomi - Settings Tab implementation
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "app/temp_file_manager.hpp"
#include "player/mpv_player.hpp"
#include "activity/player_activity.hpp"
#include <set>

// Version defined in CMakeLists.txt or here
#ifndef VITA_ABS_VERSION
#define VITA_ABS_VERSION "2.0.0"
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
    createLayoutSection();
    createContentDisplaySection();
    createPlaybackSection();
    createAudioSection();
    createStreamingSection();
    createDownloadsSection();
    createDebugSection();
    createAboutSection();

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Account");
    m_contentBox->addView(header);

    // User info cell
    m_userLabel = new brls::Label();
    m_userLabel->setText("User: " + (app.getUsername().empty() ? "Not logged in" : app.getUsername()));
    m_userLabel->setFontSize(18);
    m_userLabel->setMarginLeft(16);
    m_userLabel->setMarginBottom(8);
    m_contentBox->addView(m_userLabel);

    // Server info cell
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Server: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(16);
    m_contentBox->addView(m_serverLabel);

    // Logout button
    auto* logoutCell = new brls::DetailCell();
    logoutCell->setText("Logout");
    logoutCell->setDetailText("Sign out from current account");
    logoutCell->registerClickAction([this](brls::View* view) {
        onLogout();
        return true;
    });
    m_contentBox->addView(logoutCell);
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

void SettingsTab::createLayoutSection() {
    // Layout section removed - collapse sidebar and hidden libraries settings removed
}

void SettingsTab::createContentDisplaySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Content Display");
    m_contentBox->addView(header);

    // Show collections toggle
    m_collectionsToggle = new brls::BooleanCell();
    m_collectionsToggle->init("Show Collections", settings.showCollections, [&settings](bool value) {
        settings.showCollections = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_collectionsToggle);

    // Show series toggle
    auto* seriesToggle = new brls::BooleanCell();
    seriesToggle->init("Show Series", settings.showSeries, [&settings](bool value) {
        settings.showSeries = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(seriesToggle);

    // Show authors toggle
    auto* authorsToggle = new brls::BooleanCell();
    authorsToggle->init("Show Authors", settings.showAuthors, [&settings](bool value) {
        settings.showAuthors = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(authorsToggle);

    // Show progress toggle
    auto* progressToggle = new brls::BooleanCell();
    progressToggle->init("Show Progress Bars", settings.showProgress, [&settings](bool value) {
        settings.showProgress = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(progressToggle);

    // Show only downloaded toggle
    auto* downloadedOnlyToggle = new brls::BooleanCell();
    downloadedOnlyToggle->init("Show Only Downloaded", settings.showOnlyDownloaded, [&settings](bool value) {
        settings.showOnlyDownloaded = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(downloadedOnlyToggle);

    // Info label for downloaded only
    auto* downloadedInfoLabel = new brls::Label();
    downloadedInfoLabel->setText("When enabled, library shows only downloaded content");
    downloadedInfoLabel->setFontSize(14);
    downloadedInfoLabel->setMarginLeft(16);
    downloadedInfoLabel->setMarginTop(4);
    m_contentBox->addView(downloadedInfoLabel);
}

void SettingsTab::createPlaybackSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Playback");
    m_contentBox->addView(header);

    // Auto-play next toggle
    m_autoPlayToggle = new brls::BooleanCell();
    m_autoPlayToggle->init("Auto-Play Next Chapter", settings.autoPlayNext, [&settings](bool value) {
        settings.autoPlayNext = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_autoPlayToggle);

    // Resume playback toggle
    m_resumeToggle = new brls::BooleanCell();
    m_resumeToggle->init("Resume Playback", settings.resumePlayback, [&settings](bool value) {
        settings.resumePlayback = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_resumeToggle);

    // Seek interval selector
    m_seekIntervalSelector = new brls::SelectorCell();
    m_seekIntervalSelector->init("Seek Interval",
        {"5 seconds", "10 seconds", "15 seconds", "30 seconds", "60 seconds"},
        settings.seekInterval == 5 ? 0 :
        settings.seekInterval == 10 ? 1 :
        settings.seekInterval == 15 ? 2 :
        settings.seekInterval == 30 ? 3 : 4,
        [this](int index) {
            onSeekIntervalChanged(index);
        });
    m_contentBox->addView(m_seekIntervalSelector);

    // Playback speed selector
    auto* speedSelector = new brls::SelectorCell();
    speedSelector->init("Playback Speed",
        {"0.5x", "0.75x", "1.0x", "1.25x", "1.5x", "1.75x", "2.0x"},
        static_cast<int>(settings.playbackSpeed),
        [&settings](int index) {
            settings.playbackSpeed = static_cast<PlaybackSpeed>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(speedSelector);

    // Podcast auto-complete threshold selector
    auto* podcastCompleteSelector = new brls::SelectorCell();
    podcastCompleteSelector->init("Podcast Auto-Complete",
        {"Disabled", "Last 10 sec", "Last 30 sec", "Last 60 sec", "90%", "95%", "99%"},
        static_cast<int>(settings.podcastAutoComplete),
        [&settings](int index) {
            settings.podcastAutoComplete = static_cast<AutoCompleteThreshold>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(podcastCompleteSelector);

    // Prevent sleep toggle
    auto* sleepToggle = new brls::BooleanCell();
    sleepToggle->init("Prevent Screen Sleep", settings.preventSleep, [&settings](bool value) {
        settings.preventSleep = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(sleepToggle);

    // Show download progress in player
    auto* downloadProgressToggle = new brls::BooleanCell();
    downloadProgressToggle->init("Show Download Progress", settings.showDownloadProgress, [&settings](bool value) {
        settings.showDownloadProgress = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(downloadProgressToggle);
}

void SettingsTab::createAudioSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Audio");
    m_contentBox->addView(header);

    // Audio quality selector
    auto* qualitySelector = new brls::SelectorCell();
    qualitySelector->init("Audio Quality",
        {"Original", "High (256 kbps)", "Medium (128 kbps)", "Low (64 kbps)"},
        static_cast<int>(settings.audioQuality),
        [&settings](int index) {
            settings.audioQuality = static_cast<AudioQuality>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(qualitySelector);

    // Volume boost toggle
    auto* boostToggle = new brls::BooleanCell();
    boostToggle->init("Volume Boost", settings.boostVolume, [&settings](bool value) {
        settings.boostVolume = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(boostToggle);

    // Show chapter list toggle
    auto* chapterToggle = new brls::BooleanCell();
    chapterToggle->init("Show Chapter List", settings.showChapterList, [&settings](bool value) {
        settings.showChapterList = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(chapterToggle);
}

void SettingsTab::createStreamingSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Streaming Cache");
    m_contentBox->addView(header);

    // Description
    auto* descLabel = new brls::Label();
    descLabel->setText("Streamed audio is cached locally for faster replay");
    descLabel->setFontSize(14);
    descLabel->setMarginLeft(16);
    descLabel->setMarginBottom(8);
    m_contentBox->addView(descLabel);

    // Max temp files selector
    auto* maxFilesSelector = new brls::SelectorCell();
    int maxFilesIndex = 0;
    if (settings.maxTempFiles == 3) maxFilesIndex = 0;
    else if (settings.maxTempFiles == 5) maxFilesIndex = 1;
    else if (settings.maxTempFiles == 10) maxFilesIndex = 2;
    else if (settings.maxTempFiles == 20) maxFilesIndex = 3;
    else maxFilesIndex = 1; // Default to 5

    maxFilesSelector->init("Max Cached Files",
        {"3 files", "5 files", "10 files", "20 files"},
        maxFilesIndex,
        [&settings](int index) {
            switch (index) {
                case 0: settings.maxTempFiles = 3; break;
                case 1: settings.maxTempFiles = 5; break;
                case 2: settings.maxTempFiles = 10; break;
                case 3: settings.maxTempFiles = 20; break;
            }
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(maxFilesSelector);

    // Max temp size selector
    auto* maxSizeSelector = new brls::SelectorCell();
    int maxSizeIndex = 0;
    if (settings.maxTempSizeMB == 100) maxSizeIndex = 0;
    else if (settings.maxTempSizeMB == 250) maxSizeIndex = 1;
    else if (settings.maxTempSizeMB == 500) maxSizeIndex = 2;
    else if (settings.maxTempSizeMB == 1000) maxSizeIndex = 3;
    else if (settings.maxTempSizeMB == 0) maxSizeIndex = 4;
    else maxSizeIndex = 2; // Default to 500 MB

    maxSizeSelector->init("Max Cache Size",
        {"100 MB", "250 MB", "500 MB", "1 GB", "Unlimited"},
        maxSizeIndex,
        [&settings](int index) {
            switch (index) {
                case 0: settings.maxTempSizeMB = 100; break;
                case 1: settings.maxTempSizeMB = 250; break;
                case 2: settings.maxTempSizeMB = 500; break;
                case 3: settings.maxTempSizeMB = 1000; break;
                case 4: settings.maxTempSizeMB = 0; break; // 0 = unlimited
            }
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(maxSizeSelector);

    // Save to downloads toggle
    auto* saveToDownloadsToggle = new brls::BooleanCell();
    saveToDownloadsToggle->init("Save to Downloads", settings.saveToDownloads, [&settings](bool value) {
        settings.saveToDownloads = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(saveToDownloadsToggle);

    // Save to downloads description
    auto* saveInfoLabel = new brls::Label();
    saveInfoLabel->setText("When enabled, streamed files are saved permanently");
    saveInfoLabel->setFontSize(14);
    saveInfoLabel->setMarginLeft(16);
    saveInfoLabel->setMarginTop(4);
    m_contentBox->addView(saveInfoLabel);

    // Clear cache button
    TempFileManager& tempMgr = TempFileManager::getInstance();
    tempMgr.init();
    int64_t cacheSize = tempMgr.getTotalTempSize();
    int cacheCount = tempMgr.getTempFileCount();

    auto* clearCacheCell = new brls::DetailCell();
    clearCacheCell->setText("Clear Streaming Cache");

    std::string cacheInfo;
    if (cacheCount == 0) {
        cacheInfo = "Empty";
    } else {
        // Format size
        if (cacheSize >= 1024 * 1024) {
            cacheInfo = std::to_string(cacheSize / (1024 * 1024)) + " MB (" + std::to_string(cacheCount) + " files)";
        } else if (cacheSize >= 1024) {
            cacheInfo = std::to_string(cacheSize / 1024) + " KB (" + std::to_string(cacheCount) + " files)";
        } else {
            cacheInfo = std::to_string(cacheSize) + " bytes (" + std::to_string(cacheCount) + " files)";
        }
    }
    clearCacheCell->setDetailText(cacheInfo);

    clearCacheCell->registerClickAction([clearCacheCell](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Clear all cached streaming files?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Clear", [dialog, clearCacheCell]() {
            TempFileManager::getInstance().clearAllTempFiles();
            if (clearCacheCell) {
                clearCacheCell->setDetailText("Empty");
            }
            dialog->close();
            brls::Application::notify("Streaming cache cleared");
        });

        dialog->open();
        return true;
    });
    m_contentBox->addView(clearCacheCell);
}

void SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Downloads");
    m_contentBox->addView(header);

    // Auto-start downloads toggle
    m_autoStartDownloadsToggle = new brls::BooleanCell();
    m_autoStartDownloadsToggle->init("Auto-Start Downloads", settings.autoStartDownloads, [&settings](bool value) {
        settings.autoStartDownloads = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_autoStartDownloadsToggle);

    // WiFi only toggle
    m_wifiOnlyToggle = new brls::BooleanCell();
    m_wifiOnlyToggle->init("Download Over WiFi Only", settings.downloadOverWifiOnly, [&settings](bool value) {
        settings.downloadOverWifiOnly = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_wifiOnlyToggle);

    // Concurrent downloads selector
    m_concurrentDownloadsSelector = new brls::SelectorCell();
    m_concurrentDownloadsSelector->init("Max Concurrent Downloads",
        {"1", "2", "3"},
        settings.maxConcurrentDownloads - 1,
        [&settings](int index) {
            settings.maxConcurrentDownloads = index + 1;
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(m_concurrentDownloadsSelector);

    // Delete after finish toggle
    m_deleteAfterWatchToggle = new brls::BooleanCell();
    m_deleteAfterWatchToggle->init("Delete After Finishing", settings.deleteAfterFinish, [&settings](bool value) {
        settings.deleteAfterFinish = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_deleteAfterWatchToggle);

    // Sync progress toggle
    m_syncProgressToggle = new brls::BooleanCell();
    m_syncProgressToggle->init("Sync Progress on Connect", settings.syncProgressOnConnect, [&settings](bool value) {
        settings.syncProgressOnConnect = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(m_syncProgressToggle);

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
    m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " items");
    m_clearDownloadsCell->registerClickAction([this](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Delete all downloaded content?");

        dialog->addButton("Cancel", [dialog]() {
            dialog->close();
        });

        dialog->addButton("Delete All", [dialog, this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteDownload(item.itemId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 items");
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

void SettingsTab::createDebugSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("Debug");
    m_contentBox->addView(header);

    // Test local playback button
    auto* testLocalCell = new brls::DetailCell();
    testLocalCell->setText("Test Local Playback");
    testLocalCell->setDetailText("ux0:data/VitaSuwayomi/test.mp3");
    testLocalCell->registerClickAction([this](brls::View* view) {
        onTestLocalPlayback();
        return true;
    });
    m_contentBox->addView(testLocalCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Place test.mp3 or test.mp4 in ux0:data/VitaSuwayomi/");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(8);
    infoLabel->setMarginBottom(16);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createAboutSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("About");
    m_contentBox->addView(header);

    // Version info
    auto* versionCell = new brls::DetailCell();
    versionCell->setText("Version");
    versionCell->setDetailText(VITA_ABS_VERSION);
    m_contentBox->addView(versionCell);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("VitaSuwayomi - Audiobookshelf Client for PlayStation Vita");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    m_contentBox->addView(descLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    m_contentBox->addView(creditLabel);
}

void SettingsTab::onLogout() {
    brls::Dialog* dialog = new brls::Dialog("Are you sure you want to logout?");

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Logout", [dialog, this]() {
        dialog->close();

        // Clear credentials
        AudiobookshelfClient::getInstance().logout();
        Application::getInstance().setAuthToken("");
        Application::getInstance().setServerUrl("");
        Application::getInstance().setUsername("");
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

void SettingsTab::onSeekIntervalChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    switch (index) {
        case 0: settings.seekInterval = 5; break;
        case 1: settings.seekInterval = 10; break;
        case 2: settings.seekInterval = 15; break;
        case 3: settings.seekInterval = 30; break;
        case 4: settings.seekInterval = 60; break;
    }

    app.saveSettings();
}


void SettingsTab::onTestLocalPlayback() {
    brls::Logger::info("SettingsTab: Testing local playback...");

    // Check for test files
    const std::string basePath = "ux0:data/VitaSuwayomi/";
    std::string testFile;

    std::vector<std::string> testFiles = {
        basePath + "test.mp4",
        basePath + "test.mp3",
        basePath + "test.ogg",
        basePath + "test.wav"
    };

    for (const auto& file : testFiles) {
        FILE* f = fopen(file.c_str(), "rb");
        if (f) {
            fclose(f);
            testFile = file;
            brls::Logger::info("SettingsTab: Found test file: {}", testFile);
            break;
        }
    }

    if (testFile.empty()) {
        brls::Application::notify("No test file found in ux0:data/VitaSuwayomi/");
        brls::Logger::error("SettingsTab: No test file found");
        return;
    }

    brls::Logger::info("SettingsTab: Pushing player activity for: {}", testFile);
    PlayerActivity* activity = PlayerActivity::createForDirectFile(testFile);
    brls::Application::pushActivity(activity);
}

// Stub methods for removed Plex-specific features
void SettingsTab::createTranscodeSection() {
    // Removed - Audiobookshelf doesn't use video transcoding
}

void SettingsTab::onQualityChanged(int index) {
    // Removed - Audiobookshelf uses audio quality instead
}

void SettingsTab::onSubtitleSizeChanged(int index) {
    // Removed - Audiobookshelf is audio-only
}

void SettingsTab::onManageSidebarOrder() {
    // Removed - Simplified for Audiobookshelf
}

} // namespace vitasuwayomi
