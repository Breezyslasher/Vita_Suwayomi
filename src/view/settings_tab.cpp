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
    createBrowseSection();
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

    // Manage Categories cell
    auto* manageCategoriesCell = new brls::DetailCell();
    manageCategoriesCell->setText("Manage Categories");
    manageCategoriesCell->setDetailText("Create, edit, delete, reorder");
    manageCategoriesCell->registerClickAction([this](brls::View* view) {
        showCategoryManagementDialog();
        return true;
    });
    m_contentBox->addView(manageCategoriesCell);

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
    header->setTitle("Reader (Defaults)");
    m_contentBox->addView(header);

    // Info label explaining these are defaults
    auto* readerInfoLabel = new brls::Label();
    readerInfoLabel->setText("These settings are used for new manga. Per-manga settings can be changed in the reader and are synced to the server.");
    readerInfoLabel->setFontSize(14);
    readerInfoLabel->setMarginLeft(16);
    readerInfoLabel->setMarginBottom(12);
    m_contentBox->addView(readerInfoLabel);

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

    // Image rotation selector (default for new manga)
    int rotationIndex = 0;
    switch (settings.imageRotation) {
        case 0: rotationIndex = 0; break;
        case 90: rotationIndex = 1; break;
        case 180: rotationIndex = 2; break;
        case 270: rotationIndex = 3; break;
    }
    auto* rotationSelector = new brls::SelectorCell();
    rotationSelector->init("Default Rotation",
        {"0° (Normal)", "90° (Clockwise)", "180° (Upside Down)", "270° (Counter-Clockwise)"},
        rotationIndex,
        [&settings](int index) {
            switch (index) {
                case 0: settings.imageRotation = 0; break;
                case 1: settings.imageRotation = 90; break;
                case 2: settings.imageRotation = 180; break;
                case 3: settings.imageRotation = 270; break;
            }
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(rotationSelector);

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

    // Webtoon section header
    auto* webtoonHeader = new brls::Header();
    webtoonHeader->setTitle("Webtoon / Long Strip");
    m_contentBox->addView(webtoonHeader);

    // Crop borders toggle
    auto* cropBordersToggle = new brls::BooleanCell();
    cropBordersToggle->init("Crop Borders", settings.cropBorders, [&settings](bool value) {
        settings.cropBorders = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cropBordersToggle);

    // Info label for crop borders
    auto* cropInfoLabel = new brls::Label();
    cropInfoLabel->setText("Automatically removes white/black borders from pages");
    cropInfoLabel->setFontSize(14);
    cropInfoLabel->setMarginLeft(16);
    cropInfoLabel->setMarginTop(4);
    m_contentBox->addView(cropInfoLabel);

    // Auto-detect webtoon toggle
    auto* webtoonDetectToggle = new brls::BooleanCell();
    webtoonDetectToggle->init("Auto-Detect Webtoon", settings.webtoonDetection, [&settings](bool value) {
        settings.webtoonDetection = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(webtoonDetectToggle);

    // Info label for webtoon detection
    auto* detectInfoLabel = new brls::Label();
    detectInfoLabel->setText("Automatically switch to vertical mode for long strip images");
    detectInfoLabel->setFontSize(14);
    detectInfoLabel->setMarginLeft(16);
    detectInfoLabel->setMarginTop(4);
    m_contentBox->addView(detectInfoLabel);

    // Side padding selector
    auto* paddingSelector = new brls::SelectorCell();
    paddingSelector->init("Side Padding",
        {"None", "5%", "10%", "15%", "20%"},
        settings.webtoonSidePadding / 5,
        [&settings](int index) {
            settings.webtoonSidePadding = index * 5;
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(paddingSelector);
}

void SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Downloads");
    m_contentBox->addView(header);

    // Download mode selector
    auto* downloadModeSelector = new brls::SelectorCell();
    downloadModeSelector->init("Download Location",
        {"Server Only", "Local Only", "Both"},
        static_cast<int>(settings.downloadMode),
        [](int index) {
            brls::Logger::info("SettingsTab: Download mode changed to {} (0=Server, 1=Local, 2=Both)", index);
            Application::getInstance().getSettings().downloadMode = static_cast<DownloadMode>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(downloadModeSelector);

    // Info label for download mode
    auto* downloadModeInfoLabel = new brls::Label();
    downloadModeInfoLabel->setText("Server: stored on Suwayomi server | Local: stored on Vita | Both: synced to both");
    downloadModeInfoLabel->setFontSize(14);
    downloadModeInfoLabel->setMarginLeft(16);
    downloadModeInfoLabel->setMarginTop(4);
    m_contentBox->addView(downloadModeInfoLabel);

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

    // Auto-resume downloads toggle
    auto* autoResumeToggle = new brls::BooleanCell();
    autoResumeToggle->init("Resume Downloads on Startup", settings.autoResumeDownloads, [&settings](bool value) {
        settings.autoResumeDownloads = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(autoResumeToggle);

    // Info label for auto-resume
    auto* autoResumeInfoLabel = new brls::Label();
    autoResumeInfoLabel->setText("Automatically resume queued downloads when app restarts");
    autoResumeInfoLabel->setFontSize(14);
    autoResumeInfoLabel->setMarginLeft(16);
    autoResumeInfoLabel->setMarginTop(4);
    m_contentBox->addView(autoResumeInfoLabel);

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

void SettingsTab::createBrowseSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Browse / Sources");
    m_contentBox->addView(header);

    // Source language filter
    auto* languageFilterCell = new brls::DetailCell();
    languageFilterCell->setText("Source Languages");

    // Build description of currently enabled languages
    std::string langDesc;
    if (settings.enabledSourceLanguages.empty()) {
        langDesc = "All languages";
    } else {
        for (const auto& lang : settings.enabledSourceLanguages) {
            if (!langDesc.empty()) langDesc += ", ";
            langDesc += lang;
        }
    }
    languageFilterCell->setDetailText(langDesc);
    languageFilterCell->registerClickAction([this](brls::View* view) {
        showLanguageFilterDialog();
        return true;
    });
    m_contentBox->addView(languageFilterCell);

    // Info label
    auto* langInfoLabel = new brls::Label();
    langInfoLabel->setText("Filter which source languages appear in Browse and Search");
    langInfoLabel->setFontSize(14);
    langInfoLabel->setMarginLeft(16);
    langInfoLabel->setMarginTop(4);
    m_contentBox->addView(langInfoLabel);

    // Show NSFW sources toggle
    auto* nsfwToggle = new brls::BooleanCell();
    nsfwToggle->init("Show NSFW Sources", settings.showNsfwSources, [&settings](bool value) {
        settings.showNsfwSources = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(nsfwToggle);
}

void SettingsTab::showLanguageFilterDialog() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // All supported languages (ISO 639-1 codes used by Tachiyomi/Suwayomi extensions)
    std::vector<std::pair<std::string, std::string>> languages = {
        {"all", "All Languages"},
        {"multi", "Multi-language"},
        {"en", "English"},
        {"ja", "Japanese"},
        {"ko", "Korean"},
        {"zh", "Chinese"},
        {"zh-Hans", "Chinese (Simplified)"},
        {"zh-Hant", "Chinese (Traditional)"},
        {"es", "Spanish"},
        {"es-419", "Spanish (Latin America)"},
        {"pt", "Portuguese"},
        {"pt-BR", "Portuguese (Brazil)"},
        {"fr", "French"},
        {"de", "German"},
        {"it", "Italian"},
        {"ru", "Russian"},
        {"ar", "Arabic"},
        {"id", "Indonesian"},
        {"th", "Thai"},
        {"vi", "Vietnamese"},
        {"tr", "Turkish"},
        {"pl", "Polish"},
        {"uk", "Ukrainian"},
        {"nl", "Dutch"},
        {"hi", "Hindi"},
        {"bn", "Bengali"},
        {"ms", "Malay"},
        {"my", "Burmese"},
        {"bg", "Bulgarian"},
        {"ca", "Catalan"},
        {"cs", "Czech"},
        {"da", "Danish"},
        {"el", "Greek"},
        {"fa", "Persian"},
        {"fi", "Finnish"},
        {"he", "Hebrew"},
        {"hu", "Hungarian"},
        {"lt", "Lithuanian"},
        {"mn", "Mongolian"},
        {"no", "Norwegian"},
        {"ro", "Romanian"},
        {"sr", "Serbian"},
        {"sv", "Swedish"},
        {"ta", "Tamil"},
        {"te", "Telugu"},
        {"af", "Afrikaans"},
        {"sq", "Albanian"},
        {"am", "Amharic"},
        {"hy", "Armenian"},
        {"az", "Azerbaijani"},
        {"eu", "Basque"},
        {"be", "Belarusian"},
        {"bs", "Bosnian"},
        {"hr", "Croatian"},
        {"et", "Estonian"},
        {"fil", "Filipino"},
        {"gl", "Galician"},
        {"ka", "Georgian"},
        {"gu", "Gujarati"},
        {"is", "Icelandic"},
        {"kn", "Kannada"},
        {"kk", "Kazakh"},
        {"km", "Khmer"},
        {"lo", "Lao"},
        {"lv", "Latvian"},
        {"mk", "Macedonian"},
        {"ml", "Malayalam"},
        {"mr", "Marathi"},
        {"ne", "Nepali"},
        {"pa", "Punjabi"},
        {"si", "Sinhala"},
        {"sk", "Slovak"},
        {"sl", "Slovenian"},
        {"sw", "Swahili"},
        {"ur", "Urdu"},
        {"uz", "Uzbek"}
    };

    // Create a scrollable dialog-like view
    auto* dialogBox = new brls::Box();
    dialogBox->setAxis(brls::Axis::COLUMN);
    dialogBox->setWidth(500);
    dialogBox->setHeight(400);
    dialogBox->setPadding(20);
    dialogBox->setBackgroundColor(nvgRGBA(30, 30, 30, 255));
    dialogBox->setCornerRadius(12);

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Select Source Languages");
    titleLabel->setFontSize(22);
    titleLabel->setMarginBottom(15);
    dialogBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Tap languages to toggle. Select 'All' to show all.");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    infoLabel->setMarginBottom(10);
    dialogBox->addView(infoLabel);

    // Scrollable language list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* langList = new brls::Box();
    langList->setAxis(brls::Axis::COLUMN);

    for (const auto& [code, name] : languages) {
        bool isEnabled = (code == "all" && settings.enabledSourceLanguages.empty()) ||
                         (code != "all" && settings.enabledSourceLanguages.count(code) > 0);

        auto* langRow = new brls::Box();
        langRow->setAxis(brls::Axis::ROW);
        langRow->setAlignItems(brls::AlignItems::CENTER);
        langRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        langRow->setPadding(8, 12, 8, 12);
        langRow->setMarginBottom(4);
        langRow->setCornerRadius(8);
        langRow->setBackgroundColor(isEnabled ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));
        langRow->setFocusable(true);

        auto* nameLabel = new brls::Label();
        nameLabel->setText(name);
        nameLabel->setFontSize(16);
        langRow->addView(nameLabel);

        auto* codeLabel = new brls::Label();
        codeLabel->setText(code);
        codeLabel->setFontSize(14);
        codeLabel->setTextColor(nvgRGB(120, 120, 120));
        langRow->addView(codeLabel);

        std::string langCode = code;
        langRow->registerClickAction([langCode, langRow](brls::View* view) {
            AppSettings& s = Application::getInstance().getSettings();

            if (langCode == "all") {
                s.enabledSourceLanguages.clear();
                brls::Application::notify("Showing all languages");
            } else {
                if (s.enabledSourceLanguages.count(langCode) > 0) {
                    s.enabledSourceLanguages.erase(langCode);
                } else {
                    s.enabledSourceLanguages.insert(langCode);
                }

                // Update visual state
                bool nowEnabled = s.enabledSourceLanguages.count(langCode) > 0;
                langRow->setBackgroundColor(nowEnabled ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));
            }

            Application::getInstance().saveSettings();
            return true;
        });
        langRow->addGestureRecognizer(new brls::TapGestureRecognizer(langRow));

        langList->addView(langRow);
    }

    scrollView->setContentView(langList);
    dialogBox->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Done");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));
    dialogBox->addView(closeBtn);

    // Push as new activity
    brls::Application::pushActivity(new brls::Activity(dialogBox));
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

void SettingsTab::showCategoryManagementDialog() {
    // Fetch categories from server
    brls::Application::notify("Loading categories...");

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

    // Create dialog box
    auto* dialogBox = new brls::Box();
    dialogBox->setAxis(brls::Axis::COLUMN);
    dialogBox->setWidth(550);
    dialogBox->setHeight(450);
    dialogBox->setPadding(20);
    dialogBox->setBackgroundColor(nvgRGBA(30, 30, 30, 255));
    dialogBox->setCornerRadius(12);

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Manage Categories");
    titleLabel->setFontSize(22);
    titleLabel->setMarginBottom(10);
    dialogBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Use L/R to move, X to edit, Triangle to delete");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    infoLabel->setMarginBottom(15);
    dialogBox->addView(infoLabel);

    // Create New Category button
    auto* createBtn = new brls::Button();
    createBtn->setText("+ Create New Category");
    createBtn->setMarginBottom(15);
    createBtn->registerClickAction([this](brls::View* view) {
        brls::Application::popActivity();
        showCreateCategoryDialog();
        return true;
    });
    createBtn->addGestureRecognizer(new brls::TapGestureRecognizer(createBtn));
    dialogBox->addView(createBtn);

    // Scrollable category list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* catList = new brls::Box();
    catList->setAxis(brls::Axis::COLUMN);

    for (size_t i = 0; i < categories.size(); i++) {
        const auto& cat = categories[i];

        // Skip the default category (id 0) - it can't be modified
        if (cat.id == 0) continue;

        auto* catRow = new brls::Box();
        catRow->setAxis(brls::Axis::ROW);
        catRow->setAlignItems(brls::AlignItems::CENTER);
        catRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        catRow->setPadding(10, 12, 10, 12);
        catRow->setMarginBottom(6);
        catRow->setCornerRadius(8);
        catRow->setBackgroundColor(nvgRGBA(50, 50, 50, 200));
        catRow->setFocusable(true);

        // Category info box
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);

        auto* nameLabel = new brls::Label();
        std::string displayName = cat.name;
        if (displayName.length() > 20) {
            displayName = displayName.substr(0, 18) + "..";
        }
        nameLabel->setText(displayName);
        nameLabel->setFontSize(16);
        infoBox->addView(nameLabel);

        auto* countLabel = new brls::Label();
        countLabel->setText(std::to_string(cat.mangaCount) + " manga");
        countLabel->setFontSize(12);
        countLabel->setTextColor(nvgRGB(120, 120, 120));
        infoBox->addView(countLabel);

        catRow->addView(infoBox);

        // Order indicator (use UI row index, 1-indexed)
        auto* orderLabel = new brls::Label();
        size_t uiRowIndex = catList->getChildren().size();  // 0-indexed position before adding
        orderLabel->setText("#" + std::to_string(uiRowIndex + 1));
        orderLabel->setFontSize(14);
        orderLabel->setTextColor(nvgRGB(100, 100, 100));
        orderLabel->setMarginRight(10);
        catRow->addView(orderLabel);

        // Store category data for actions
        int catId = cat.id;

        // Register L button to move up
        catRow->registerAction("Move Up", brls::ControllerButton::BUTTON_LB, [catList, catId](brls::View* view) {
            // Find current UI position
            auto& children = catList->getChildren();
            int uiIndex = -1;
            for (size_t i = 0; i < children.size(); i++) {
                if (children[i] == view) {
                    uiIndex = static_cast<int>(i);
                    break;
                }
            }

            if (uiIndex > 0) {
                // Server order = UI index + 1 (default category at order 0 is skipped in UI)
                int newServerOrder = uiIndex;  // Moving up: new order = current UI index

                SuwayomiClient& client = SuwayomiClient::getInstance();
                if (client.moveCategoryOrder(catId, newServerOrder)) {
                    brls::Application::notify("Category moved up");

                    // Visual swap
                    brls::View* currentRow = children[uiIndex];
                    brls::View* prevRow = children[uiIndex - 1];

                    catList->removeView(currentRow, false);
                    catList->removeView(prevRow, false);

                    catList->addView(currentRow, uiIndex - 1);
                    catList->addView(prevRow, uiIndex);

                    // Update order labels for swapped rows
                    auto* currentBox = dynamic_cast<brls::Box*>(currentRow);
                    auto* prevBox = dynamic_cast<brls::Box*>(prevRow);
                    if (currentBox && currentBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(currentBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex));
                    }
                    if (prevBox && prevBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(prevBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 1));
                    }
                } else {
                    brls::Application::notify("Failed to move category");
                }
            }
            return true;
        });

        // Register R button to move down
        catRow->registerAction("Move Down", brls::ControllerButton::BUTTON_RB, [catList, catId](brls::View* view) {
            // Find current UI position
            auto& children = catList->getChildren();
            int uiIndex = -1;
            for (size_t i = 0; i < children.size(); i++) {
                if (children[i] == view) {
                    uiIndex = static_cast<int>(i);
                    break;
                }
            }

            if (uiIndex >= 0 && uiIndex < static_cast<int>(children.size()) - 1) {
                // Server order = UI index + 1, moving down: new order = current order + 1
                int newServerOrder = uiIndex + 2;  // UI index + 1 (current) + 1 (move down)

                SuwayomiClient& client = SuwayomiClient::getInstance();
                if (client.moveCategoryOrder(catId, newServerOrder)) {
                    brls::Application::notify("Category moved down");

                    // Visual swap
                    brls::View* currentRow = children[uiIndex];
                    brls::View* nextRow = children[uiIndex + 1];

                    catList->removeView(nextRow, false);
                    catList->removeView(currentRow, false);

                    catList->addView(nextRow, uiIndex);
                    catList->addView(currentRow, uiIndex + 1);

                    // Update order labels for swapped rows
                    auto* currentBox = dynamic_cast<brls::Box*>(currentRow);
                    auto* nextBox = dynamic_cast<brls::Box*>(nextRow);
                    if (currentBox && currentBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(currentBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 2));
                    }
                    if (nextBox && nextBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(nextBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 1));
                    }
                } else {
                    brls::Application::notify("Failed to move category");
                }
            }
            return true;
        });

        // Register X button to edit
        Category catCopy = cat;
        catRow->registerAction("Edit", brls::ControllerButton::BUTTON_X, [this, catCopy](brls::View*) {
            brls::Application::popActivity();
            showEditCategoryDialog(catCopy);
            return true;
        });

        // Register Y button to delete
        catRow->registerAction("Delete", brls::ControllerButton::BUTTON_Y, [this, catCopy](brls::View*) {
            brls::Application::popActivity();
            showDeleteCategoryConfirmation(catCopy);
            return true;
        });

        // Click to edit
        catRow->registerClickAction([this, catCopy](brls::View* view) {
            brls::Application::popActivity();
            showEditCategoryDialog(catCopy);
            return true;
        });
        catRow->addGestureRecognizer(new brls::TapGestureRecognizer(catRow));

        catList->addView(catRow);
    }

    // If no editable categories
    if (categories.empty() || (categories.size() == 1 && categories[0].id == 0)) {
        auto* label = new brls::Label();
        label->setText("No categories found. Create one!");
        label->setFontSize(16);
        label->setMarginTop(20);
        catList->addView(label);
    }

    scrollView->setContentView(catList);
    dialogBox->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));
    dialogBox->addView(closeBtn);

    // Push as new activity
    brls::Application::pushActivity(new brls::Activity(dialogBox));
}

void SettingsTab::showCreateCategoryDialog() {
    brls::Dialog* dialog = new brls::Dialog("Create New Category");

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(20);
    contentBox->setWidth(400);

    auto* inputLabel = new brls::Label();
    inputLabel->setText("Enter category name:");
    inputLabel->setFontSize(16);
    inputLabel->setMarginBottom(10);
    contentBox->addView(inputLabel);

    // Note: Borealis doesn't have a native text input, so we'll use a workaround
    // with the software keyboard or a preset name approach
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Press A to open keyboard");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    contentBox->addView(infoLabel);

    dialog->addView(contentBox);

    dialog->addButton("Cancel", [dialog]() {
        dialog->close();
    });

    dialog->addButton("Create", [dialog]() {
        // Open software keyboard for input
        brls::Application::getImeManager()->openForText([dialog](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.createCategory(text)) {
                brls::Application::notify("Category created: " + text);
                dialog->close();
            } else {
                brls::Application::notify("Failed to create category");
            }
        }, "Enter category name", "", 50, "", 0);
    });

    dialog->open();
}

void SettingsTab::showEditCategoryDialog(const Category& category) {
    brls::Dialog* dialog = new brls::Dialog("Edit Category: " + category.name);

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(20);
    contentBox->setWidth(400);

    auto* inputLabel = new brls::Label();
    inputLabel->setText("Current name: " + category.name);
    inputLabel->setFontSize(16);
    inputLabel->setMarginBottom(10);
    contentBox->addView(inputLabel);

    auto* infoLabel = new brls::Label();
    infoLabel->setText("Press 'Rename' to change the name");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    contentBox->addView(infoLabel);

    dialog->addView(contentBox);

    int catId = category.id;
    bool isDefault = category.isDefault;
    std::string catName = category.name;

    dialog->addButton("Cancel", [this, dialog]() {
        dialog->close();
        // Re-open category management dialog
        showCategoryManagementDialog();
    });

    dialog->addButton("Rename", [this, dialog, catId, isDefault, catName]() {
        brls::Application::getImeManager()->openForText([this, dialog, catId, isDefault](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.updateCategory(catId, text, isDefault)) {
                brls::Application::notify("Category renamed to: " + text);
                dialog->close();
                // Re-open category management dialog to show updated list
                showCategoryManagementDialog();
            } else {
                brls::Application::notify("Failed to rename category");
            }
        }, "Enter new category name", "", 50, catName, 0);
    });

    dialog->open();
}

void SettingsTab::showDeleteCategoryConfirmation(const Category& category) {
    std::string message = "Delete category '" + category.name + "'?\n\n"
                         "This will remove " + std::to_string(category.mangaCount) +
                         " manga from this category.\nThe manga will remain in your library.";

    brls::Dialog* dialog = new brls::Dialog(message);

    int catId = category.id;

    dialog->addButton("Cancel", [this, dialog]() {
        dialog->close();
        // Re-open category management dialog
        showCategoryManagementDialog();
    });

    dialog->addButton("Delete", [this, dialog, catId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.deleteCategory(catId)) {
            brls::Application::notify("Category deleted");
            dialog->close();
            // Re-open category management dialog to show updated list
            showCategoryManagementDialog();
        } else {
            brls::Application::notify("Failed to delete category");
            dialog->close();
            showCategoryManagementDialog();
        }
    });

    dialog->open();
}

} // namespace vitasuwayomi
