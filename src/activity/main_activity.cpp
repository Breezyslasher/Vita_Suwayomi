/**
 * VitaABS - Main Activity implementation
 */

#include "activity/main_activity.hpp"
#include "view/home_tab.hpp"
#include "view/library_section_tab.hpp"
#include "view/search_tab.hpp"
#include "view/settings_tab.hpp"
#include "app/downloads_manager.hpp"
#include "app/application.hpp"
#include "app/audiobookshelf_client.hpp"
#include "utils/async.hpp"

#include <algorithm>

namespace vitaabs {

// Cached library sections
static std::vector<Library> s_cachedSections;

// Helper to calculate text width (approximate based on character count)
static int calculateTextWidth(const std::string& text) {
    const int charWidth = 12;
    const int padding = 50;
    return static_cast<int>(text.length()) * charWidth + padding;
}

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity created");
}

brls::View* MainActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/main.xml");
}

void MainActivity::onContentAvailable() {
    brls::Logger::debug("MainActivity content available");

    if (tabFrame) {
        AppSettings& settings = Application::getInstance().getSettings();
        AudiobookshelfClient& client = AudiobookshelfClient::getInstance();

        // Try to fetch libraries - if this fails, we're offline
        std::vector<Library> sections;
        bool isOnline = client.fetchLibraries(sections);

        if (!isOnline || sections.empty()) {
            // Offline mode - show library tabs with downloaded content and Settings
            brls::Logger::info("MainActivity: Offline mode - showing downloaded content");

            // Set sidebar width for offline mode
            brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
            if (sidebar) {
                sidebar->setWidth(220);
            }

            // Check if we have any downloads
            auto downloads = DownloadsManager::getInstance().getDownloads();

            if (downloads.empty()) {
                // No downloads - show offline notice
                tabFrame->addTab("Offline", []() {
                    auto* box = new brls::Box();
                    box->setAxis(brls::Axis::COLUMN);
                    box->setPadding(40);
                    box->setJustifyContent(brls::JustifyContent::CENTER);
                    box->setAlignItems(brls::AlignItems::CENTER);
                    box->setGrow(1.0f);

                    auto* title = new brls::Label();
                    title->setText("No Server Connection");
                    title->setFontSize(24);
                    title->setMarginBottom(20);
                    box->addView(title);

                    auto* msg = new brls::Label();
                    msg->setText("Connect to WiFi and configure your\nAudiobookshelf server in Settings.\n\nNo downloaded content available.");
                    msg->setHorizontalAlign(brls::HorizontalAlign::CENTER);
                    msg->setFontSize(16);
                    box->addView(msg);

                    return box;
                });
            } else {
                // Have downloads - create library tabs for downloaded content
                // Check what types of content we have downloaded
                bool hasBooks = false;
                bool hasPodcasts = false;
                for (const auto& dl : downloads) {
                    if (dl.mediaType == "book" || dl.mediaType.empty()) hasBooks = true;
                    if (dl.mediaType == "podcast" || dl.mediaType == "episode") hasPodcasts = true;
                }

                if (hasBooks) {
                    tabFrame->addTab("Audiobooks", []() {
                        auto* tab = new LibrarySectionTab("offline-books", "Audiobooks (Offline)", "book");
                        return tab;
                    });
                }

                if (hasPodcasts) {
                    tabFrame->addTab("Podcasts", []() {
                        auto* tab = new LibrarySectionTab("offline-podcasts", "Podcasts (Offline)", "podcast");
                        return tab;
                    });
                }
            }

            tabFrame->addTab("Settings", []() { return new SettingsTab(); });

            // Focus first content tab
            tabFrame->focusTab(0);
            return;
        }

        // Online mode - normal flow
        s_cachedSections = sections;

        // Sync progress from server for all downloaded items
        brls::Logger::info("MainActivity: Online - syncing progress from server for downloaded items");
        DownloadsManager::getInstance().syncProgressFromServer();

        // Calculate dynamic sidebar width based on content
        int sidebarWidth = 200;  // Minimum width

        std::vector<std::string> standardTabs = {"Home", "Search", "Downloads", "Settings"};
        for (const auto& tab : standardTabs) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(tab));
        }

        // Check library names for width
        for (const auto& section : sections) {
            sidebarWidth = std::max(sidebarWidth, calculateTextWidth(section.name));
        }

        // Apply sidebar width (with reasonable bounds)
        sidebarWidth = std::min(sidebarWidth, 350);
        brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
        if (sidebar) {
            sidebar->setWidth(sidebarWidth);
        }

        // Add Home tab first (Continue Listening + Recently Added Episodes)
        tabFrame->addTab("Home", []() { return new HomeTab(); });
        brls::Logger::debug("MainActivity: Added Home tab");

        // Add library tabs directly (no Home/Library intermediate screens)
        // Sort by type: Audiobooks first, then Podcasts
        std::vector<Library> bookLibs, podcastLibs;
        for (const auto& lib : sections) {
            if (lib.mediaType == "book") {
                bookLibs.push_back(lib);
            } else if (lib.mediaType == "podcast") {
                podcastLibs.push_back(lib);
            }
        }

        // Add Audiobooks libraries
        for (const auto& lib : bookLibs) {
            std::string id = lib.id;
            std::string name = lib.name;
            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name, "book");
            });
            brls::Logger::debug("MainActivity: Added audiobook library tab: {}", name);
        }

        // Add Podcast libraries
        for (const auto& lib : podcastLibs) {
            std::string id = lib.id;
            std::string name = lib.name;
            tabFrame->addTab(name, [id, name]() {
                return new LibrarySectionTab(id, name, "podcast");
            });
            brls::Logger::debug("MainActivity: Added podcast library tab: {}", name);
        }

        // Utility tabs (no separators)
        tabFrame->addTab("Search", []() { return new SearchTab(); });
        tabFrame->addTab("Settings", []() { return new SettingsTab(); });

        // Focus first tab
        tabFrame->focusTab(0);
    }
}

} // namespace vitaabs
