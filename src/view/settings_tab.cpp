/**
 * VitaSuwayomi - Settings Tab implementation
 * Settings for manga reader app
 */

#include "view/settings_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "utils/async.hpp"
#include "utils/http_client.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <thread>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/net/netctl.h>
#endif

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
    createTrackingSection();
    createUISection();
    createLibrarySection();
    createReaderSection();
    createDownloadsSection();
    createBrowseSection();
    createStatisticsSection();
    createBackupSection();
    createAboutSection();

    m_scrollView->setContentView(m_contentBox);
    this->addView(m_scrollView);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Server");
    m_contentBox->addView(header);

    // Current server info
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Active: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(8);
    m_contentBox->addView(m_serverLabel);

    // Local URL configuration
    auto* localUrlCell = new brls::DetailCell();
    localUrlCell->setText("Local URL");
    localUrlCell->setDetailText(settings.localServerUrl.empty() ? "Not set" : settings.localServerUrl);
    localUrlCell->registerClickAction([this, localUrlCell](brls::View* view) {
        showUrlInputDialog("Local Server URL", "Enter local network URL (e.g., http://192.168.1.100:4567)",
            Application::getInstance().getSettings().localServerUrl,
            [this, localUrlCell](const std::string& url) {
                Application::getInstance().getSettings().localServerUrl = url;
                localUrlCell->setDetailText(url.empty() ? "Not set" : url);
                Application::getInstance().saveSettings();
                updateServerLabel();
            });
        return true;
    });
    m_contentBox->addView(localUrlCell);

    // Remote URL configuration
    auto* remoteUrlCell = new brls::DetailCell();
    remoteUrlCell->setText("Remote URL");
    remoteUrlCell->setDetailText(settings.remoteServerUrl.empty() ? "Not set" : settings.remoteServerUrl);
    remoteUrlCell->registerClickAction([this, remoteUrlCell](brls::View* view) {
        showUrlInputDialog("Remote Server URL", "Enter remote URL (e.g., https://myserver.com:4567)",
            Application::getInstance().getSettings().remoteServerUrl,
            [this, remoteUrlCell](const std::string& url) {
                Application::getInstance().getSettings().remoteServerUrl = url;
                remoteUrlCell->setDetailText(url.empty() ? "Not set" : url);
                Application::getInstance().saveSettings();
                updateServerLabel();
            });
        return true;
    });
    m_contentBox->addView(remoteUrlCell);

    // Toggle between local and remote
    m_urlModeSelector = new brls::SelectorCell();
    std::vector<std::string> urlModes = {"Local", "Remote"};
    int currentMode = settings.useRemoteUrl ? 1 : 0;
    m_urlModeSelector->init("Active Connection", urlModes, currentMode,
        [this](int index) {
            Application& app = Application::getInstance();
            if (index == 0) {
                app.switchToLocalUrl();
            } else {
                app.switchToRemoteUrl();
            }
            updateServerLabel();
            brls::Application::notify(index == 0 ? "Switched to Local URL" : "Switched to Remote URL");
        });
    m_contentBox->addView(m_urlModeSelector);

    // Auto-switch on failure toggle
    auto* autoSwitchToggle = new brls::BooleanCell();
    autoSwitchToggle->init("Auto-Switch on Failure", settings.autoSwitchOnFailure,
        [](bool value) {
            Application::getInstance().getSettings().autoSwitchOnFailure = value;
            Application::getInstance().saveSettings();
            brls::Application::notify(value ? "Auto-switch enabled" : "Auto-switch disabled");
        });
    m_contentBox->addView(autoSwitchToggle);

    // Info label for auto-switch
    auto* autoSwitchInfoLabel = new brls::Label();
    autoSwitchInfoLabel->setText("Try alternate URL if connection fails");
    autoSwitchInfoLabel->setFontSize(14);
    autoSwitchInfoLabel->setMarginLeft(16);
    autoSwitchInfoLabel->setMarginTop(4);
    m_contentBox->addView(autoSwitchInfoLabel);

    // Connection timeout selector
    auto* timeoutSelector = new brls::SelectorCell();
    int timeoutIndex = 1; // default to 30s
    if (settings.connectionTimeout <= 10) timeoutIndex = 0;
    else if (settings.connectionTimeout <= 30) timeoutIndex = 1;
    else if (settings.connectionTimeout <= 60) timeoutIndex = 2;
    else timeoutIndex = 3;
    timeoutSelector->init("Connection Timeout",
        {"10 seconds", "30 seconds", "60 seconds", "120 seconds"},
        timeoutIndex,
        [](int index) {
            int timeouts[] = {10, 30, 60, 120};
            Application::getInstance().getSettings().connectionTimeout = timeouts[index];
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(timeoutSelector);

    // Network Test button
    auto* networkTestCell = new brls::DetailCell();
    networkTestCell->setText("Network Test");
    networkTestCell->setDetailText("Test WiFi and server connection");
    networkTestCell->registerClickAction([this](brls::View* view) {
        runNetworkTest();
        return true;
    });
    m_contentBox->addView(networkTestCell);

    // Connection status indicator
    bool isOnline = Application::getInstance().isConnected();
    auto* connectionStatusLabel = new brls::Label();
    connectionStatusLabel->setText(isOnline ? "Status: Connected" : "Status: Offline");
    connectionStatusLabel->setFontSize(16);
    connectionStatusLabel->setMarginLeft(16);
    connectionStatusLabel->setMarginBottom(8);
    m_contentBox->addView(connectionStatusLabel);

    // Reconnect button (try to connect to saved server)
    auto* reconnectCell = new brls::DetailCell();
    reconnectCell->setText("Reconnect");
    reconnectCell->setDetailText("Try to connect to server");
    reconnectCell->registerClickAction([this, connectionStatusLabel](brls::View* view) {
        Application& app = Application::getInstance();
        std::string url = app.getActiveServerUrl();
        if (url.empty()) {
            brls::Application::notify("No server URL configured");
            return true;
        }

        brls::Application::notify("Connecting to " + url + "...");

        asyncRun([url, connectionStatusLabel]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            client.setServerUrl(url);

            bool success = client.testConnection();

            brls::sync([success, connectionStatusLabel]() {
                if (success) {
                    Application::getInstance().setConnected(true);
                    brls::Application::notify("Connected!");
                    if (connectionStatusLabel) {
                        connectionStatusLabel->setText("Status: Connected");
                    }
                } else {
                    brls::Application::notify("Connection failed");
                }
            });
        });
        return true;
    });
    m_contentBox->addView(reconnectCell);

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

void SettingsTab::createTrackingSection() {
    auto* header = new brls::Header();
    header->setTitle("Tracking");
    m_contentBox->addView(header);

    // "Login to Trackers" cell that fetches trackers and shows login/logout options
    auto* trackerCell = new brls::DetailCell();
    trackerCell->setText("Tracker Accounts");
    trackerCell->setDetailText("Login to MAL, AniList, etc.");
    trackerCell->registerClickAction([this](brls::View* view) {
        if (!Application::getInstance().isConnected()) {
            brls::Application::notify("Connect to server first");
            return true;
        }

        brls::Application::notify("Loading trackers...");

        asyncRun([this]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            std::vector<Tracker> trackers;

            if (!client.fetchTrackers(trackers)) {
                brls::sync([]() {
                    brls::Application::notify("Failed to load trackers");
                });
                return;
            }

            brls::sync([this, trackers]() {
                if (trackers.empty()) {
                    brls::Application::notify("No trackers available on server");
                    return;
                }

                // Build tracker options list with login status
                std::vector<std::string> options;
                for (const auto& t : trackers) {
                    std::string label = t.name;
                    if (t.isLoggedIn) {
                        label += " (Logged in)";
                    } else {
                        label += " (Not logged in)";
                    }
                    options.push_back(label);
                }

                auto* dropdown = new brls::Dropdown(
                    "Tracker Accounts", options,
                    [this, trackers](int selected) {
                        if (selected < 0 || selected >= static_cast<int>(trackers.size())) return;
                        const Tracker& tracker = trackers[selected];

                        brls::sync([this, tracker]() {
                            if (tracker.isLoggedIn) {
                                // Already logged in — offer logout
                                std::vector<std::string> actions = {"Logout", "Cancel"};
                                auto* actionDropdown = new brls::Dropdown(
                                    tracker.name + " (Logged in)", actions,
                                    [this, tracker](int action) {
                                        if (action == 0) {
                                            // Logout
                                            int trackerId = tracker.id;
                                            std::string trackerName = tracker.name;
                                            asyncRun([trackerId, trackerName]() {
                                                bool ok = SuwayomiClient::getInstance().logoutTracker(trackerId);
                                                brls::sync([ok, trackerName]() {
                                                    if (ok) {
                                                        brls::Application::notify("Logged out of " + trackerName);
                                                    } else {
                                                        brls::Application::notify("Failed to log out of " + trackerName);
                                                    }
                                                });
                                            });
                                        }
                                    }, 0);
                                brls::Application::pushActivity(new brls::Activity(actionDropdown));
                            } else {
                                // Not logged in — show credential login dialog
                                // MangaUpdates uses username/password, others use OAuth
                                // On PS Vita we can only do credential-based login
                                int trackerId = tracker.id;
                                std::string trackerName = tracker.name;

                                brls::Application::getImeManager()->openForText(
                                    [this, trackerId, trackerName](std::string username) {
                                        if (username.empty()) return;

                                        brls::Application::getImeManager()->openForText(
                                            [trackerId, trackerName, username](std::string password) {
                                                if (password.empty()) return;

                                                asyncRun([trackerId, trackerName, username, password]() {
                                                    bool ok = SuwayomiClient::getInstance()
                                                        .loginTrackerCredentials(trackerId, username, password);
                                                    brls::sync([ok, trackerName]() {
                                                        if (ok) {
                                                            brls::Application::notify("Logged in to " + trackerName);
                                                        } else {
                                                            brls::Application::notify("Login failed for " + trackerName);
                                                        }
                                                    });
                                                });
                                            },
                                            "Password", "", 256, "");
                                    },
                                    "Username", "", 256, "");
                            }
                        });
                    }, 0);
                brls::Application::pushActivity(new brls::Activity(dropdown));
            });
        });
        return true;
    });
    m_contentBox->addView(trackerCell);
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

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [](bool value) {
        Application::getInstance().getSettings().debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
        brls::Application::notify(value ? "Debug logging enabled" : "Debug logging disabled");
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

    // Grid display mode selector
    auto* displayModeSelector = new brls::SelectorCell();
    displayModeSelector->init("Display Mode",
        {"Grid (Cover + Title)", "Compact Grid (Covers Only)", "List View"},
        static_cast<int>(settings.libraryDisplayMode),
        [&settings](int index) {
            settings.libraryDisplayMode = static_cast<LibraryDisplayMode>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(displayModeSelector);

    // Grid size selector
    auto* gridSizeSelector = new brls::SelectorCell();
    gridSizeSelector->init("Grid Size",
        {"Large (4 columns)", "Medium (6 columns)", "Small (8 columns)"},
        static_cast<int>(settings.libraryGridSize),
        [&settings](int index) {
            settings.libraryGridSize = static_cast<LibraryGridSize>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(gridSizeSelector);

    // List row size selector (for list view mode)
    auto* listRowSizeSelector = new brls::SelectorCell();
    listRowSizeSelector->init("List Row Size",
        {"Small (compact)", "Medium (default)", "Large (spacious)", "Auto (fit title)"},
        static_cast<int>(settings.listRowSize),
        [&settings](int index) {
            settings.listRowSize = static_cast<ListRowSize>(index);
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(listRowSizeSelector);

    // Info label for list row size
    auto* listRowInfoLabel = new brls::Label();
    listRowInfoLabel->setText("Auto mode adjusts row height to fit the full title");
    listRowInfoLabel->setFontSize(14);
    listRowInfoLabel->setMarginLeft(16);
    listRowInfoLabel->setMarginTop(4);
    listRowInfoLabel->setMarginBottom(8);
    m_contentBox->addView(listRowInfoLabel);

    // Library grouping mode selector
    auto* groupModeSelector = new brls::SelectorCell();
    groupModeSelector->init("Library Grouping",
        {"By Category (Tabs)", "By Source", "No Grouping (All Manga)"},
        static_cast<int>(settings.libraryGroupMode),
        [&settings](int index) {
            settings.libraryGroupMode = static_cast<LibraryGroupMode>(index);
            Application::getInstance().saveSettings();
            brls::Application::notify("Library grouping updated");
        });
    m_contentBox->addView(groupModeSelector);

    // Info label for grouping mode
    auto* groupModeInfoLabel = new brls::Label();
    groupModeInfoLabel->setText("How manga is organized in the library");
    groupModeInfoLabel->setFontSize(14);
    groupModeInfoLabel->setMarginLeft(16);
    groupModeInfoLabel->setMarginTop(4);
    groupModeInfoLabel->setMarginBottom(8);
    m_contentBox->addView(groupModeInfoLabel);

    // Default library sort mode selector
    auto* defaultSortSelector = new brls::SelectorCell();
    defaultSortSelector->init("Default Sort Mode",
        {"A-Z", "Z-A", "Most Unread", "Least Unread", "Recently Added (Newest)",
         "Recently Added (Oldest)", "Last Read", "Date Updated (Newest)",
         "Date Updated (Oldest)", "Total Chapters", "Downloaded Only"},
        settings.defaultLibrarySortMode,
        [&settings](int index) {
            settings.defaultLibrarySortMode = index;
            Application::getInstance().saveSettings();
        });
    m_contentBox->addView(defaultSortSelector);

    // Info label for default sort mode
    auto* defaultSortInfoLabel = new brls::Label();
    defaultSortInfoLabel->setText("Used when category sort is set to 'Default'");
    defaultSortInfoLabel->setFontSize(14);
    defaultSortInfoLabel->setMarginLeft(16);
    defaultSortInfoLabel->setMarginTop(4);
    defaultSortInfoLabel->setMarginBottom(8);
    m_contentBox->addView(defaultSortInfoLabel);

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

    // Downloads Only Mode toggle
    auto* downloadsOnlyToggle = new brls::BooleanCell();
    downloadsOnlyToggle->init("Downloads Only Mode", settings.downloadsOnlyMode, [](bool value) {
        Application::getInstance().getSettings().downloadsOnlyMode = value;
        Application::getInstance().saveSettings();
        brls::Application::notify(value ? "Showing downloaded only" : "Showing all manga");
    });
    m_contentBox->addView(downloadsOnlyToggle);

    // Downloads Only info label
    auto* downloadsOnlyInfoLabel = new brls::Label();
    downloadsOnlyInfoLabel->setText("When offline, only show locally downloaded manga and chapters");
    downloadsOnlyInfoLabel->setFontSize(14);
    downloadsOnlyInfoLabel->setMarginLeft(16);
    downloadsOnlyInfoLabel->setMarginTop(4);
    downloadsOnlyInfoLabel->setMarginBottom(8);
    m_contentBox->addView(downloadsOnlyInfoLabel);

    // Library Updates header
    auto* updatesHeader = new brls::Header();
    updatesHeader->setTitle("Library Updates");
    m_contentBox->addView(updatesHeader);

    // Update on start toggle
    auto* updateOnStartToggle = new brls::BooleanCell();
    updateOnStartToggle->init("Update Library on Start", settings.updateOnStart, [](bool value) {
        Application::getInstance().getSettings().updateOnStart = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(updateOnStartToggle);

    // Info label for update on start
    auto* updateOnStartInfoLabel = new brls::Label();
    updateOnStartInfoLabel->setText("Automatically check for new chapters on app startup");
    updateOnStartInfoLabel->setFontSize(14);
    updateOnStartInfoLabel->setMarginLeft(16);
    updateOnStartInfoLabel->setMarginTop(4);
    m_contentBox->addView(updateOnStartInfoLabel);

    // Default category selector
    m_defaultCategorySelector = new brls::SelectorCell();
    m_defaultCategorySelector->init("Default Category",
        {"Default (All)", "Loading..."},
        0,
        [](int index) {
            // Will be updated when categories are loaded
        });
    m_defaultCategorySelector->setDetailText("Category to show when opening library");
    m_contentBox->addView(m_defaultCategorySelector);

    // Load categories for the selector asynchronously
    refreshDefaultCategorySelector();

    // Display Badges header
    auto* badgesHeader = new brls::Header();
    badgesHeader->setTitle("Display Badges");
    m_contentBox->addView(badgesHeader);

    // Show unread badge toggle
    auto* showUnreadBadgeToggle = new brls::BooleanCell();
    showUnreadBadgeToggle->init("Show Unread Badge", settings.showUnreadBadge, [](bool value) {
        Application::getInstance().getSettings().showUnreadBadge = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(showUnreadBadgeToggle);

    // Clear Cache button
    auto* clearCacheCell = new brls::DetailCell();
    clearCacheCell->setText("Clear Cache");
    clearCacheCell->setDetailText("Delete cached data and images");
    clearCacheCell->registerClickAction([](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Clear all cached data?");
        dialog->setCancelable(true);  // Allow back button to close dialog

        dialog->addButton("Cancel", []() {});

        dialog->addButton("Clear", []() {
            LibraryCache::getInstance().clearAllCache();
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

    auto& hiddenIds = Application::getInstance().getSettings().hiddenCategoryIds;

    // Create dialog box (matching Manage Categories design)
    auto* dialogBox = new brls::Box();
    dialogBox->setAxis(brls::Axis::COLUMN);
    dialogBox->setWidth(550);
    dialogBox->setHeight(450);
    dialogBox->setPadding(20);
    dialogBox->setBackgroundColor(nvgRGBA(30, 30, 30, 255));
    dialogBox->setCornerRadius(12);

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Hidden Categories");
    titleLabel->setFontSize(22);
    titleLabel->setMarginBottom(10);
    dialogBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Tap to toggle visibility in library");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    infoLabel->setMarginBottom(15);
    dialogBox->addView(infoLabel);

    // Create close button early so it can be captured in lambdas
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Done");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([this](brls::View* view) {
        // Update the cell detail text before closing
        if (m_hideCategoriesCell) {
            size_t hiddenCount = Application::getInstance().getSettings().hiddenCategoryIds.size();
            m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");
        }
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));

    // Register B button on close button
    closeBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        if (m_hideCategoriesCell) {
            size_t hiddenCount = Application::getInstance().getSettings().hiddenCategoryIds.size();
            m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");
        }
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Scrollable category list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* catList = new brls::Box();
    catList->setAxis(brls::Axis::COLUMN);

    // Filter to only show categories with manga
    std::vector<Category> visibleCategories;
    for (const auto& cat : categories) {
        if (cat.mangaCount > 0) {
            visibleCategories.push_back(cat);
        }
    }

    for (size_t i = 0; i < visibleCategories.size(); i++) {
        const auto& cat = visibleCategories[i];

        auto* catRow = new brls::Box();
        catRow->setAxis(brls::Axis::ROW);
        catRow->setAlignItems(brls::AlignItems::CENTER);
        catRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        catRow->setPadding(10, 12, 10, 12);
        catRow->setMarginBottom(6);
        catRow->setCornerRadius(8);
        catRow->setFocusable(true);

        // Category is visible if NOT in hidden list
        bool isVisible = (hiddenIds.find(cat.id) == hiddenIds.end());
        catRow->setBackgroundColor(isVisible ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));

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

        // Status indicator
        auto* statusLabel = new brls::Label();
        statusLabel->setText(isVisible ? "Visible" : "Hidden");
        statusLabel->setFontSize(14);
        statusLabel->setTextColor(isVisible ? nvgRGB(100, 200, 150) : nvgRGB(150, 100, 100));
        statusLabel->setMarginRight(10);
        catRow->addView(statusLabel);

        // Store category id for toggle action
        int catId = cat.id;

        // Click to toggle visibility
        catRow->registerClickAction([catRow, catId, statusLabel](brls::View* view) {
            auto& hidden = Application::getInstance().getSettings().hiddenCategoryIds;
            bool currentlyHidden = (hidden.find(catId) != hidden.end());

            if (currentlyHidden) {
                // Show category (remove from hidden)
                hidden.erase(catId);
                catRow->setBackgroundColor(nvgRGBA(0, 100, 80, 200));
                statusLabel->setText("Visible");
                statusLabel->setTextColor(nvgRGB(100, 200, 150));
            } else {
                // Hide category (add to hidden)
                hidden.insert(catId);
                catRow->setBackgroundColor(nvgRGBA(50, 50, 50, 200));
                statusLabel->setText("Hidden");
                statusLabel->setTextColor(nvgRGB(150, 100, 100));
            }

            Application::getInstance().saveSettings();
            return true;
        });
        catRow->addGestureRecognizer(new brls::TapGestureRecognizer(catRow));

        // Register B button on each row to close dialog
        catRow->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
            if (m_hideCategoriesCell) {
                size_t hiddenCount = Application::getInstance().getSettings().hiddenCategoryIds.size();
                m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");
            }
            brls::Application::popActivity();
            return true;
        }, true);  // hidden action

        catList->addView(catRow);
    }

    // If no categories with manga
    if (visibleCategories.empty()) {
        auto* label = new brls::Label();
        label->setText("No categories with manga found");
        label->setFontSize(16);
        label->setMarginTop(20);
        catList->addView(label);
    }

    scrollView->setContentView(catList);
    dialogBox->addView(scrollView);

    // Add close button to dialog
    dialogBox->addView(closeBtn);

    // Register circle button to close the dialog
    dialogBox->registerAction("Close", brls::ControllerButton::BUTTON_BACK, [this](brls::View*) {
        // Update the cell detail text before closing
        if (m_hideCategoriesCell) {
            size_t hiddenCount = Application::getInstance().getSettings().hiddenCategoryIds.size();
            m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");
        }
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Set up navigation
    auto& catChildren = catList->getChildren();
    if (!catChildren.empty()) {
        // Last category row -> down goes to closeBtn
        catChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
        // closeBtn -> up goes to last category
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, catChildren.back());
    }

    // Push as new activity
    brls::Application::pushActivity(new brls::Activity(dialogBox));
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
        {"Fit Screen", "Fit Width", "Fit Height", "Original Size (1:1)"},
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
    syncNowCell->setDetailText("Bidirectional sync with server");
    syncNowCell->registerClickAction([](brls::View* view) {
        if (!Application::getInstance().isConnected()) {
            brls::Application::notify("Not connected to server");
            return true;
        }
        brls::Application::notify("Syncing progress...");
        vitasuwayomi::asyncRun([]() {
            DownloadsManager::getInstance().syncProgressFromServer();
            brls::sync([]() {
                brls::Application::notify("Progress synced with server");
            });
        });
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
        dialog->setCancelable(true);  // Allow back button to close dialog

        dialog->addButton("Cancel", []() {});

        dialog->addButton("Delete All", [this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 manga");
            }
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
    m_languageFilterCell = new brls::DetailCell();
    m_languageFilterCell->setText("Source Languages");

    // Build description of currently enabled languages
    updateLanguageFilterCellText();

    m_languageFilterCell->registerClickAction([this](brls::View* view) {
        showLanguageFilterDialog();
        return true;
    });
    m_contentBox->addView(m_languageFilterCell);

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

void SettingsTab::updateLanguageFilterCellText() {
    if (!m_languageFilterCell) return;

    const auto& settings = Application::getInstance().getSettings();
    std::string langDesc;
    if (settings.enabledSourceLanguages.empty()) {
        langDesc = "All languages";
    } else {
        for (const auto& lang : settings.enabledSourceLanguages) {
            if (!langDesc.empty()) langDesc += ", ";
            langDesc += lang;
        }
        // Truncate if too long
        if (langDesc.length() > 30) {
            langDesc = langDesc.substr(0, 27) + "...";
        }
    }
    m_languageFilterCell->setDetailText(langDesc);
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
    dialogBox->setFocusable(true);

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

    // Store all rows so we can update "All" visual state when individual languages are toggled
    auto allRows = std::make_shared<std::vector<brls::Box*>>();

    // Track first and last rows for navigation
    brls::Box* firstLangRow = nullptr;
    brls::Box* lastLangRow = nullptr;

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

        allRows->push_back(langRow);

        // Track first and last rows
        if (!firstLangRow) {
            firstLangRow = langRow;
        }
        lastLangRow = langRow;

        std::string langCode = code;
        langRow->registerClickAction([langCode, langRow, allRows](brls::View* view) {
            AppSettings& s = Application::getInstance().getSettings();

            if (langCode == "all") {
                s.enabledSourceLanguages.clear();
                brls::Application::notify("Showing all languages");
                // Update all row visuals - "All" is enabled, others are disabled
                for (size_t i = 0; i < allRows->size(); i++) {
                    (*allRows)[i]->setBackgroundColor(i == 0 ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));
                }
            } else {
                if (s.enabledSourceLanguages.count(langCode) > 0) {
                    s.enabledSourceLanguages.erase(langCode);
                } else {
                    s.enabledSourceLanguages.insert(langCode);
                }

                // Update visual state
                bool nowEnabled = s.enabledSourceLanguages.count(langCode) > 0;
                langRow->setBackgroundColor(nowEnabled ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));

                // Update "All" row visual - disabled when any language is selected
                if (!allRows->empty()) {
                    bool allEnabled = s.enabledSourceLanguages.empty();
                    (*allRows)[0]->setBackgroundColor(allEnabled ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(50, 50, 50, 200));
                }
            }

            Application::getInstance().saveSettings();
            return true;
        });
        langRow->addGestureRecognizer(new brls::TapGestureRecognizer(langRow));

        // Register B button on each row to close dialog
        langRow->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
            updateLanguageFilterCellText();
            brls::Application::popActivity();
            return true;
        }, true);  // hidden action

        langList->addView(langRow);
    }

    scrollView->setContentView(langList);
    dialogBox->addView(scrollView);

    // Close button
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Done");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([this](brls::View* view) {
        updateLanguageFilterCellText();
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));

    // Register B button on close button
    closeBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        updateLanguageFilterCellText();
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    dialogBox->addView(closeBtn);

    // Set up navigation routes between last language row and Done button
    if (lastLangRow) {
        // Last language row DOWN -> closeBtn (Done button)
        lastLangRow->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
        // closeBtn UP -> last language row
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, lastLangRow);
    }

    // Register B button on dialogBox as fallback
    dialogBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        updateLanguageFilterCellText();
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Push as new activity and give focus to the first language row
    brls::Application::pushActivity(new brls::Activity(dialogBox));

    // Give focus to first language row (at top of list)
    if (firstLangRow) {
        brls::Application::giveFocus(firstLangRow);
    }
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

    // Check for Updates button
    auto* updateCell = new brls::DetailCell();
    updateCell->setText("Check for Updates");
    updateCell->setDetailText("Check GitHub for new releases");
    updateCell->registerClickAction([this](brls::View* view) {
        checkForUpdates();
        return true;
    });
    m_contentBox->addView(updateCell);

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
    dialog->setCancelable(true);  // Allow back button to close dialog

    dialog->addButton("Cancel", []() {});

    dialog->addButton("Disconnect", [this]() {
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

void SettingsTab::showUrlInputDialog(const std::string& title, const std::string& hint,
                                      const std::string& currentValue,
                                      std::function<void(const std::string&)> callback) {
    // Create a simple input dialog for URL entry
    brls::Dialog* dialog = new brls::Dialog(title);
    dialog->setCancelable(true);  // Allow back button to close dialog

    // Input field container
    auto* inputBox = new brls::Box();
    inputBox->setAxis(brls::Axis::COLUMN);
    inputBox->setPadding(16);

    // Hint/description text
    auto* hintLabel = new brls::Label();
    hintLabel->setText(hint);
    hintLabel->setFontSize(14);
    hintLabel->setTextColor(nvgRGB(180, 180, 180));
    hintLabel->setMarginBottom(12);
    inputBox->addView(hintLabel);

    // Current value display
    auto* currentLabel = new brls::Label();
    currentLabel->setText(currentValue.empty() ? "(not set)" : currentValue);
    currentLabel->setFontSize(16);
    inputBox->addView(currentLabel);

    dialog->addView(inputBox);

    // Close button - just close without changes (empty lambda, dialog auto-closes)
    dialog->addButton("Close", []() {});

    // Use the current value or prompt for new one via keyboard
    dialog->addButton("Edit", [callback, currentValue]() {
        // Open on-screen keyboard for input (dialog auto-closes before this runs)
        brls::Application::getImeManager()->openForText([callback](std::string text) {
            // Validate URL format
            if (!text.empty()) {
                // Remove trailing slashes
                while (!text.empty() && text.back() == '/') {
                    text.pop_back();
                }
                // Basic URL validation - should start with http:// or https://
                if (text.find("http://") != 0 && text.find("https://") != 0) {
                    brls::Application::notify("URL must start with http:// or https://");
                    return;
                }
            }
            callback(text);
        }, "Enter Server URL", "", 256, currentValue, 0);
    });

    dialog->addButton("Clear", [callback]() {
        // Dialog auto-closes before this runs
        callback("");
        brls::Application::notify("URL cleared");
    });

    dialog->open();
}

void SettingsTab::updateServerLabel() {
    if (m_serverLabel) {
        Application& app = Application::getInstance();
        std::string activeUrl = app.getServerUrl();
        if (activeUrl.empty()) {
            activeUrl = app.getActiveServerUrl();
        }
        m_serverLabel->setText("Active: " + (activeUrl.empty() ? "Not connected" : activeUrl));
    }
}

void SettingsTab::runNetworkTest() {
    brls::Application::notify("Running network test...");

    std::thread([this]() {
        std::string results;

        // --- WiFi Check ---
        bool wifiConnected = false;
        std::string ipAddress = "-";
        std::string dnsServer = "-";
        int wifiLevel = 0;

#ifdef __vita__
        SceNetCtlInfo netInfo;
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &netInfo) >= 0) {
            wifiConnected = true;
            ipAddress = std::string(netInfo.ip_address);
        }
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &netInfo) >= 0) {
            wifiLevel = netInfo.rssi_percentage;
        }
        SceNetCtlInfo dnsInfo {};
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &dnsInfo) >= 0) {
            dnsServer = std::string(dnsInfo.primary_dns);
        }
#else
        // Desktop stub: assume connected
        wifiConnected = true;
        ipAddress = "127.0.0.1";
        dnsServer = "8.8.8.8";
        wifiLevel = 100;
#endif

        results += "=== WiFi ===\n";
        if (wifiConnected) {
            results += "Status: Connected\n";
            results += "IP: " + ipAddress + "\n";
            results += "DNS: " + dnsServer + "\n";
            results += "Signal: " + std::to_string(wifiLevel) + "%\n";
        } else {
            results += "Status: Not Connected\n";
            results += "No WiFi connection detected.\n";
        }

        // --- Internet Check (DNS resolve + HTTP) ---
        results += "\n=== Internet ===\n";
        if (wifiConnected) {
            HttpClient http;
            http.setTimeout(10);
            auto start = std::chrono::steady_clock::now();
            HttpResponse resp = http.get("http://connectivitycheck.gstatic.com/generate_204");
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (resp.success && (resp.statusCode == 204 || resp.statusCode == 200)) {
                results += "Status: Reachable (" + std::to_string(ms) + "ms)\n";
            } else {
                results += "Status: Unreachable\n";
                if (!resp.error.empty())
                    results += "Error: " + resp.error + "\n";
            }
        } else {
            results += "Skipped (no WiFi)\n";
        }

        // --- Server Check ---
        results += "\n=== Server ===\n";
        Application& app = Application::getInstance();
        std::string serverUrl = app.getServerUrl();
        if (serverUrl.empty()) serverUrl = app.getActiveServerUrl();

        if (serverUrl.empty()) {
            results += "No server URL configured.\n";
        } else {
            results += "URL: " + serverUrl + "\n";

            SuwayomiClient& client = SuwayomiClient::getInstance();
            ServerInfo info;

            auto start = std::chrono::steady_clock::now();
            bool ok = client.fetchServerInfo(info);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (ok) {
                results += "Status: Connected (" + std::to_string(ms) + "ms)\n";
                results += "Server: " + info.name + " v" + info.version + "\n";
                results += "Build: " + info.buildType + "\n";
            } else {
                results += "Status: Failed (" + std::to_string(ms) + "ms)\n";
                results += "Could not reach server.\n";
            }
        }

        // Show results dialog on main thread
        brls::sync([results]() {
            brls::Dialog* dialog = new brls::Dialog("Network Test");
            dialog->setCancelable(true);

            auto* box = new brls::Box();
            box->setAxis(brls::Axis::COLUMN);
            box->setPadding(16);
            box->setWidth(brls::View::AUTO);

            auto* label = new brls::Label();
            label->setText(results);
            label->setFontSize(18);
            label->setHorizontalAlign(brls::HorizontalAlign::LEFT);
            box->addView(label);

            dialog->addView(box);
            dialog->addButton("Close", []() {});
            dialog->open();
        });
    }).detach();
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

    // Register B button on create button
    createBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    dialogBox->addView(createBtn);

    // Create close button early so it can be captured in lambdas (added to dialog later)
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));

    // Register B button on close button
    closeBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

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
        catRow->registerAction("Move Up", brls::ControllerButton::BUTTON_LB, [catList, catId, createBtn, closeBtn](brls::View* view) {
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

                    // Update navigation routes after move
                    auto& updatedChildren = catList->getChildren();
                    if (!updatedChildren.empty()) {
                        updatedChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
                        updatedChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
                        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, updatedChildren.front());
                        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, updatedChildren.back());
                    }
                } else {
                    brls::Application::notify("Failed to move category");
                }
            }
            return true;
        });

        // Register R button to move down
        catRow->registerAction("Move Down", brls::ControllerButton::BUTTON_RB, [catList, catId, createBtn, closeBtn](brls::View* view) {
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

                    // Update navigation routes after move
                    auto& updatedChildren = catList->getChildren();
                    if (!updatedChildren.empty()) {
                        updatedChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
                        updatedChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
                        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, updatedChildren.front());
                        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, updatedChildren.back());
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

        // Register B button on each row to close dialog
        catRow->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, true);  // hidden action

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

    // Add close button to dialog (created earlier to allow capture in lambdas)
    dialogBox->addView(closeBtn);

    // Register circle button to close the dialog
    dialogBox->registerAction("Close", brls::ControllerButton::BUTTON_BACK, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Set up navigation: first category should go up to createBtn, last category should go down to closeBtn
    auto& catChildren = catList->getChildren();
    if (!catChildren.empty()) {
        // First category row -> up goes to createBtn
        catChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
        // Last category row -> down goes to closeBtn
        catChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
    }
    // createBtn -> down goes to first category (or closeBtn if no categories)
    if (!catChildren.empty()) {
        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, catChildren.front());
    } else {
        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
    }
    // closeBtn -> up goes to last category (or createBtn if no categories)
    if (!catChildren.empty()) {
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, catChildren.back());
    } else {
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
    }

    // Push as new activity
    brls::Application::pushActivity(new brls::Activity(dialogBox));
}

void SettingsTab::showCreateCategoryDialog() {
    brls::Dialog* dialog = new brls::Dialog("Create New Category");
    dialog->setCancelable(true);  // Allow back button to close dialog

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

    dialog->addButton("Cancel", []() {});

    dialog->addButton("Create", []() {
        // Open software keyboard for input (dialog auto-closes before this runs)
        brls::Application::getImeManager()->openForText([](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.createCategory(text)) {
                brls::Application::notify("Category created: " + text);
            } else {
                brls::Application::notify("Failed to create category");
            }
        }, "Enter category name", "", 50, "", 0);
    });

    dialog->open();
}

void SettingsTab::showEditCategoryDialog(const Category& category) {
    brls::Dialog* dialog = new brls::Dialog("Edit Category: " + category.name);
    dialog->setCancelable(true);  // Allow back button to close dialog

    auto* contentBox = new brls::Box();
    contentBox->setAxis(brls::Axis::COLUMN);
    contentBox->setPadding(20);
    contentBox->setWidth(400);

    auto* infoLabel = new brls::Label();
    infoLabel->setText("Press 'Rename' to change the name");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(150, 150, 150));
    contentBox->addView(infoLabel);

    dialog->addView(contentBox);

    int catId = category.id;
    bool isDefault = category.isDefault;
    std::string catName = category.name;

    dialog->addButton("Cancel", [this]() {
        // Re-open category management dialog
        showCategoryManagementDialog();
    });

    dialog->addButton("Rename", [this, catId, isDefault, catName]() {
        brls::Application::getImeManager()->openForText([this, catId, isDefault](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.updateCategory(catId, text, isDefault)) {
                brls::Application::notify("Category renamed to: " + text);
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
    dialog->setCancelable(true);  // Allow back button to close dialog

    int catId = category.id;

    dialog->addButton("Cancel", [this]() {
        // Re-open category management dialog
        showCategoryManagementDialog();
    });

    dialog->addButton("Delete", [this, catId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.deleteCategory(catId)) {
            brls::Application::notify("Category deleted");
            // Re-open category management dialog to show updated list
            showCategoryManagementDialog();
        } else {
            brls::Application::notify("Failed to delete category");
            showCategoryManagementDialog();
        }
    });

    dialog->open();
}

void SettingsTab::showStorageManagement() {
    // Push storage management view
    brls::Application::notify("Opening storage management...");

    // Create storage view inline (since we may not have the full view ready)
    auto* storageBox = new brls::Box();
    storageBox->setAxis(brls::Axis::COLUMN);
    storageBox->setPadding(20);
    storageBox->setGrow(1.0f);

    auto* titleLabel = new brls::Label();
    titleLabel->setText("Storage Management");
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(20);
    storageBox->addView(titleLabel);

    // Get downloads info
    auto downloads = DownloadsManager::getInstance().getDownloads();
    int64_t totalSize = 0;

    auto* infoLabel = new brls::Label();
    infoLabel->setText("Downloaded manga: " + std::to_string(downloads.size()));
    infoLabel->setFontSize(18);
    infoLabel->setMarginBottom(10);
    storageBox->addView(infoLabel);

    // Clear all button
    auto* clearBtn = new brls::Button();
    clearBtn->setText("Clear All Downloads");
    clearBtn->setMarginTop(20);
    clearBtn->registerClickAction([](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Delete all downloaded content?");
        dialog->setCancelable(true);  // Allow B button to close dialog
        dialog->addButton("Cancel", []() {});
        dialog->addButton("Delete", []() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
            }
            brls::Application::notify("All downloads deleted");
            brls::Application::popActivity();
        });
        dialog->open();
        return true;
    });
    clearBtn->addGestureRecognizer(new brls::TapGestureRecognizer(clearBtn));
    // Register circle button on clearBtn
    clearBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    storageBox->addView(clearBtn);

    // Back button
    auto* backBtn = new brls::Button();
    backBtn->setText("Back");
    backBtn->setMarginTop(20);
    backBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(backBtn));
    // Register circle button on backBtn
    backBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    storageBox->addView(backBtn);

    // Register circle button on storageBox as fallback
    storageBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    brls::Application::pushActivity(new brls::Activity(storageBox));
}

void SettingsTab::showStatisticsView() {
    // Sync stats from server first
    Application::getInstance().syncStatisticsFromServer();

    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    auto* statsBox = new brls::Box();
    statsBox->setAxis(brls::Axis::COLUMN);
    statsBox->setPadding(20);
    statsBox->setGrow(1.0f);

    auto* titleLabel = new brls::Label();
    titleLabel->setText("Reading Statistics");
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(10);
    statsBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Synced from server and local reading");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(nvgRGB(120, 120, 120));
    infoLabel->setMarginBottom(15);
    statsBox->addView(infoLabel);

    // Total chapters read
    auto* chaptersLabel = new brls::Label();
    chaptersLabel->setText("Chapters Read: " + std::to_string(settings.totalChaptersRead));
    chaptersLabel->setFontSize(18);
    chaptersLabel->setMarginBottom(10);
    statsBox->addView(chaptersLabel);

    // Manga completed
    auto* completedLabel = new brls::Label();
    completedLabel->setText("Manga Completed: " + std::to_string(settings.totalMangaCompleted));
    completedLabel->setFontSize(18);
    completedLabel->setMarginBottom(10);
    statsBox->addView(completedLabel);

    // Reading streak
    auto* streakLabel = new brls::Label();
    streakLabel->setText("Current Streak: " + std::to_string(settings.currentStreak) + " days");
    streakLabel->setFontSize(18);
    streakLabel->setMarginBottom(10);
    statsBox->addView(streakLabel);

    // Longest streak
    auto* longestLabel = new brls::Label();
    longestLabel->setText("Longest Streak: " + std::to_string(settings.longestStreak) + " days");
    longestLabel->setFontSize(18);
    longestLabel->setMarginBottom(10);
    statsBox->addView(longestLabel);

    // Reading time note
    auto* timeNoteLabel = new brls::Label();
    timeNoteLabel->setText("Note: Reading time tracking is local only");
    timeNoteLabel->setFontSize(14);
    timeNoteLabel->setTextColor(nvgRGB(120, 120, 120));
    timeNoteLabel->setMarginBottom(5);
    statsBox->addView(timeNoteLabel);

    // Reading time
    int hours = static_cast<int>(settings.totalReadingTime / 3600);
    int minutes = static_cast<int>((settings.totalReadingTime % 3600) / 60);
    auto* timeLabel = new brls::Label();
    timeLabel->setText("Total Reading Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m");
    timeLabel->setFontSize(18);
    timeLabel->setMarginBottom(20);
    statsBox->addView(timeLabel);

    // Sync from Server button
    auto* syncBtn = new brls::Button();
    syncBtn->setText("Sync from Server");
    syncBtn->registerClickAction([chaptersLabel, completedLabel](brls::View* view) {
        brls::Application::notify("Syncing statistics...");
        Application::getInstance().syncStatisticsFromServer();
        AppSettings& s = Application::getInstance().getSettings();
        chaptersLabel->setText("Chapters Read: " + std::to_string(s.totalChaptersRead));
        completedLabel->setText("Manga Completed: " + std::to_string(s.totalMangaCompleted));
        brls::Application::notify("Statistics synced");
        return true;
    });
    syncBtn->addGestureRecognizer(new brls::TapGestureRecognizer(syncBtn));
    // Register circle button on syncBtn
    syncBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    statsBox->addView(syncBtn);

    // Reset button
    auto* resetBtn = new brls::Button();
    resetBtn->setText("Reset Statistics");
    resetBtn->setMarginTop(10);
    resetBtn->registerClickAction([](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("Reset all reading statistics?");
        dialog->setCancelable(true);  // Allow back button to close dialog
        dialog->addButton("Cancel", []() {});
        dialog->addButton("Reset", []() {
            AppSettings& s = Application::getInstance().getSettings();
            s.totalChaptersRead = 0;
            s.totalMangaCompleted = 0;
            s.currentStreak = 0;
            s.longestStreak = 0;
            s.totalReadingTime = 0;
            s.lastReadDate = 0;
            Application::getInstance().saveSettings();
            brls::Application::notify("Statistics reset");
            brls::Application::popActivity();
        });
        dialog->open();
        return true;
    });
    resetBtn->addGestureRecognizer(new brls::TapGestureRecognizer(resetBtn));
    // Register circle button on resetBtn
    resetBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    statsBox->addView(resetBtn);

    // Back button
    auto* backBtn = new brls::Button();
    backBtn->setText("Back");
    backBtn->setMarginTop(20);
    backBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(backBtn));
    // Register circle button on backBtn
    backBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    statsBox->addView(backBtn);

    // Register circle button on statsBox as fallback
    statsBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    brls::Application::pushActivity(new brls::Activity(statsBox));
}

void SettingsTab::createBackupSection() {
    auto* header = new brls::Header();
    header->setTitle("Backup & Restore");
    m_contentBox->addView(header);

    // Export backup
    auto* exportCell = new brls::DetailCell();
    exportCell->setText("Export Backup");
    exportCell->setDetailText("Save library and settings to file");
    exportCell->registerClickAction([this](brls::View* view) {
        exportBackup();
        return true;
    });
    m_contentBox->addView(exportCell);

    // Import backup
    auto* importCell = new brls::DetailCell();
    importCell->setText("Import Backup");
    importCell->setDetailText("Restore from backup file");
    importCell->registerClickAction([this](brls::View* view) {
        importBackup();
        return true;
    });
    m_contentBox->addView(importCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Backups are saved to ux0:data/VitaSuwayomi/backup/");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(4);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createStatisticsSection() {
    auto* header = new brls::Header();
    header->setTitle("Reading Statistics");
    m_contentBox->addView(header);

    // View statistics
    auto* statsCell = new brls::DetailCell();
    statsCell->setText("View Statistics");
    statsCell->setDetailText("Chapters read, streaks, and more");
    statsCell->registerClickAction([this](brls::View* view) {
        showStatisticsView();
        return true;
    });
    m_contentBox->addView(statsCell);

    // Storage management
    auto* storageCell = new brls::DetailCell();
    storageCell->setText("Storage Management");
    storageCell->setDetailText("View and manage downloaded content");
    storageCell->registerClickAction([this](brls::View* view) {
        showStorageManagement();
        return true;
    });
    m_contentBox->addView(storageCell);
}

void SettingsTab::exportBackup() {
    brls::Application::notify("Creating backup...");

    SuwayomiClient& client = SuwayomiClient::getInstance();

#ifdef __vita__
    // Create backup directory
    sceIoMkdir("ux0:data/VitaSuwayomi/backup", 0777);

    // Generate filename with timestamp
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char filename[64];
    strftime(filename, sizeof(filename), "backup_%Y%m%d_%H%M%S.json", tm_info);

    std::string backupPath = "ux0:data/VitaSuwayomi/backup/" + std::string(filename);

    if (client.exportBackup(backupPath)) {
        brls::Application::notify("Backup saved: " + std::string(filename));
    } else {
        brls::Application::notify("Failed to create backup");
    }
#else
    brls::Application::notify("Backup feature requires PS Vita");
#endif
}

void SettingsTab::importBackup() {
    brls::Dialog* dialog = new brls::Dialog("Import backup?\n\nThis will restore your library and settings from the most recent backup file.");
    dialog->setCancelable(true);  // Allow back button to close dialog

    dialog->addButton("Cancel", []() {});

    dialog->addButton("Import", []() {
        brls::Application::notify("Importing backup...");

        SuwayomiClient& client = SuwayomiClient::getInstance();

#ifdef __vita__
        // Find most recent backup file
        std::string backupDir = "ux0:data/VitaSuwayomi/backup";
        std::string latestBackup;

        SceUID dir = sceIoDopen(backupDir.c_str());
        if (dir >= 0) {
            SceIoDirent entry;
            while (sceIoDread(dir, &entry) > 0) {
                if (SCE_S_ISREG(entry.d_stat.st_mode)) {
                    std::string name = entry.d_name;
                    if (name.find("backup_") == 0 && name.find(".json") != std::string::npos) {
                        if (name > latestBackup) {
                            latestBackup = name;
                        }
                    }
                }
            }
            sceIoDclose(dir);
        }

        if (latestBackup.empty()) {
            brls::Application::notify("No backup file found");
            return;
        }

        std::string backupPath = backupDir + "/" + latestBackup;
        if (client.importBackup(backupPath)) {
            brls::Application::notify("Backup restored: " + latestBackup);
        } else {
            brls::Application::notify("Failed to import backup");
        }
#else
        brls::Application::notify("Backup feature requires PS Vita");
#endif
    });

    dialog->open();
}

void SettingsTab::refreshDefaultCategorySelector() {
    if (!m_defaultCategorySelector) return;

    // Fetch categories asynchronously
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Category> categories;

    if (client.fetchCategories(categories)) {
        // Sort by order
        std::sort(categories.begin(), categories.end(),
            [](const Category& a, const Category& b) {
                return a.order < b.order;
            });

        // Build category names list
        std::vector<std::string> categoryNames;
        std::vector<int> categoryIds;

        categoryNames.push_back("Default (All)");
        categoryIds.push_back(0);

        for (const auto& cat : categories) {
            if (cat.mangaCount > 0) {
                categoryNames.push_back(cat.name);
                categoryIds.push_back(cat.id);
            }
        }

        // Find current selection index
        int currentIndex = 0;
        int currentId = Application::getInstance().getSettings().defaultCategoryId;
        for (size_t i = 0; i < categoryIds.size(); i++) {
            if (categoryIds[i] == currentId) {
                currentIndex = static_cast<int>(i);
                break;
            }
        }

        // Update the selector
        m_defaultCategorySelector->init("Default Category",
            categoryNames,
            currentIndex,
            [categoryIds](int index) {
                if (index >= 0 && index < static_cast<int>(categoryIds.size())) {
                    Application::getInstance().getSettings().defaultCategoryId = categoryIds[index];
                    Application::getInstance().saveSettings();
                }
            });
        m_defaultCategorySelector->setDetailText("Category to show when opening library");
    }
}

void SettingsTab::checkForUpdates() {
    brls::Application::notify("Checking for updates...");

    // GitHub API URL for latest release
    // TODO: Replace with actual repository when published
    const std::string apiUrl = "https://api.github.com/repos/Breezyslasher/Vita_Suwayomi/releases/latest";

    HttpClient client;
    client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
    client.setTimeout(15);

    std::string response;
    if (!client.get(apiUrl, response)) {
        brls::Application::notify("Failed to check for updates");
        return;
    }

    // Simple JSON parsing for tag_name, body, and assets
    auto extractJsonString = [&response](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = response.find(search);
        if (pos == std::string::npos) return "";

        pos += search.length();
        // Skip whitespace
        while (pos < response.length() && (response[pos] == ' ' || response[pos] == '\t' || response[pos] == '\n')) pos++;

        if (pos >= response.length()) return "";

        if (response[pos] == '"') {
            // String value
            pos++;
            std::string result;
            while (pos < response.length() && response[pos] != '"') {
                if (response[pos] == '\\' && pos + 1 < response.length()) {
                    pos++;
                    if (response[pos] == 'n') result += '\n';
                    else if (response[pos] == 'r') result += '\r';
                    else if (response[pos] == 't') result += '\t';
                    else result += response[pos];
                } else {
                    result += response[pos];
                }
                pos++;
            }
            return result;
        }
        return "";
    };

    std::string tagName = extractJsonString("tag_name");
    std::string releaseNotes = extractJsonString("body");

    if (tagName.empty()) {
        brls::Application::notify("Failed to parse update info");
        return;
    }

    // Remove 'v' prefix if present
    std::string newVersion = tagName;
    if (!newVersion.empty() && (newVersion[0] == 'v' || newVersion[0] == 'V')) {
        newVersion = newVersion.substr(1);
    }

    // Compare versions (simple string comparison for now)
    std::string currentVersion = VITA_SUWAYOMI_VERSION;

    // Parse version numbers for proper comparison
    auto parseVersion = [](const std::string& ver) -> int {
        int major = 0, minor = 0, patch = 0;
        std::sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
        return major * 10000 + minor * 100 + patch;
    };

    int currentNum = parseVersion(currentVersion);
    int newNum = parseVersion(newVersion);

    if (newNum <= currentNum) {
        brls::Application::notify("You're running the latest version!");
        return;
    }

    // Find VPK download URL in assets
    std::string downloadUrl;
    size_t assetsPos = response.find("\"assets\"");
    if (assetsPos != std::string::npos) {
        // Look for browser_download_url with .vpk extension
        size_t searchPos = assetsPos;
        while (searchPos < response.length()) {
            size_t urlPos = response.find("\"browser_download_url\"", searchPos);
            if (urlPos == std::string::npos) break;

            urlPos += 23; // Skip past "browser_download_url":
            while (urlPos < response.length() && response[urlPos] != '"') urlPos++;
            if (urlPos >= response.length()) break;
            urlPos++; // Skip opening quote

            size_t urlEnd = response.find("\"", urlPos);
            if (urlEnd == std::string::npos) break;

            std::string url = response.substr(urlPos, urlEnd - urlPos);
            if (url.find(".vpk") != std::string::npos) {
                downloadUrl = url;
                break;
            }
            searchPos = urlEnd + 1;
        }
    }

    if (downloadUrl.empty()) {
        brls::Application::notify("No VPK found in release");
        return;
    }

    // Show update dialog
    showUpdateDialog(newVersion, releaseNotes, downloadUrl);
}

void SettingsTab::showUpdateDialog(const std::string& newVersion, const std::string& releaseNotes,
                                    const std::string& downloadUrl) {
    std::string message = "New version " + newVersion + " available!\n\n";
    if (!releaseNotes.empty()) {
        // Truncate release notes if too long
        std::string notes = releaseNotes;
        if (notes.length() > 300) {
            notes = notes.substr(0, 297) + "...";
        }
        message += notes;
    }

    brls::Dialog* dialog = new brls::Dialog(message);
    dialog->setCancelable(true);  // Allow back button to close dialog

    dialog->addButton("Later", []() {});

    dialog->addButton("Download & Install", [downloadUrl, newVersion, this]() {
        downloadAndInstallUpdate(downloadUrl, newVersion);
    });

    dialog->open();
}

void SettingsTab::downloadAndInstallUpdate(const std::string& downloadUrl, const std::string& version) {
#ifdef __vita__
    brls::Application::notify("Downloading update...");

    // Create update directory
    sceIoMkdir("ux0:data/VitaSuwayomi/updates", 0777);

    std::string vpkPath = "ux0:data/VitaSuwayomi/updates/VitaSuwayomi_" + version + ".vpk";

    HttpClient client;
    client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
    client.setTimeout(300);  // 5 minute timeout for large downloads
    client.setFollowRedirects(true);

    if (client.downloadToFile(downloadUrl, vpkPath)) {
        // Show success dialog with instructions
        std::string msg = "Update downloaded successfully!\n\n"
                          "VPK saved to:\n" + vpkPath + "\n\n"
                          "Please install using VitaShell:\n"
                          "1. Open VitaShell\n"
                          "2. Navigate to ux0:data/VitaSuwayomi/updates/\n"
                          "3. Select the VPK and press X to install\n"
                          "4. Restart VitaSuwayomi after installation";

        brls::Dialog* successDialog = new brls::Dialog(msg);
        successDialog->setCancelable(true);  // Allow back button to close dialog

        successDialog->addButton("OK", []() {});

        successDialog->open();
    } else {
        brls::Application::notify("Failed to download update");
    }
#else
    brls::Application::notify("Update download requires PS Vita");

    // On non-Vita platforms, just show the download URL
    brls::Dialog* dialog = new brls::Dialog("Download from:\n" + downloadUrl);
    dialog->setCancelable(true);  // Allow back button to close dialog
    dialog->addButton("OK", []() {});
    dialog->open();
#endif
}

} // namespace vitasuwayomi
