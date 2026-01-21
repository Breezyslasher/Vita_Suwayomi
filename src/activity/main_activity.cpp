/**
 * VitaSuwayomi - Main Activity implementation
 * Main tab-based navigation for the manga reader app
 */

#include "activity/main_activity.hpp"
#include "view/library_section_tab.hpp"
#include "view/extensions_tab.hpp"
#include "view/search_tab.hpp"
#include "view/settings_tab.hpp"
#include "view/downloads_tab.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"

#include <algorithm>

namespace vitasuwayomi {

// Cached categories for library tabs
static std::vector<Category> s_cachedCategories;

MainActivity::MainActivity() {
    brls::Logger::debug("MainActivity created");
}

brls::View* MainActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/main.xml");
}

void MainActivity::onContentAvailable() {
    brls::Logger::debug("MainActivity content available");

    if (tabFrame) {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Set sidebar width
        brls::View* sidebar = tabFrame->getView("brls/tab_frame/sidebar");
        if (sidebar) {
            sidebar->setWidth(200);
        }

        // Check connection status
        bool isOnline = client.isConnected();

        // Add Library tab (shows manga from library with category tabs)
        tabFrame->addTab("Library", []() {
            return new LibrarySectionTab();
        });

        // Add Browse tab (browse manga sources)
        tabFrame->addTab("Browse", []() {
            return new SearchTab();
        });

        // Add Extensions tab (manage extensions)
        tabFrame->addTab("Extensions", []() {
            return new ExtensionsTab();
        });

        // Add Downloads tab (download queue and local downloads)
        tabFrame->addTab("Downloads", []() {
            return new DownloadsTab();
        });

        // Add Settings tab
        tabFrame->addTab("Settings", []() {
            return new SettingsTab();
        });

        // If online, try to load categories for additional library tabs
        if (isOnline) {
            asyncTask<bool>([&client]() {
                std::vector<Category> categories;
                return client.fetchCategories(categories);
            }, [this](bool success) {
                if (success && !s_cachedCategories.empty()) {
                    // Could add category-specific tabs here if needed
                    brls::Logger::info("MainActivity: Loaded {} categories", s_cachedCategories.size());
                }
            });
        }

        brls::Logger::info("MainActivity: Tabs created, isOnline={}", isOnline);
    }
}

} // namespace vitasuwayomi
