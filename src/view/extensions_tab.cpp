/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Shows unified list: updates first, then installed, then uninstalled (sorted by language)
 * Language filtering and sorting follows global settings from Settings tab
 */

#include "view/extensions_tab.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>
#include <algorithm>

namespace vitasuwayomi {

// Language code to display name mapping
std::string ExtensionsTab::getLanguageDisplayName(const std::string& langCode) {
    static const std::map<std::string, std::string> languageNames = {
        {"all", "All Languages"},
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
        {"pl", "Polish"},
        {"tr", "Turkish"},
        {"nl", "Dutch"},
        {"uk", "Ukrainian"},
        {"cs", "Czech"},
        {"ro", "Romanian"},
        {"bg", "Bulgarian"},
        {"hu", "Hungarian"},
        {"el", "Greek"},
        {"he", "Hebrew"},
        {"fa", "Persian"},
        {"hi", "Hindi"},
        {"bn", "Bengali"},
        {"ms", "Malay"},
        {"fil", "Filipino"},
        {"my", "Burmese"},
        {"localsourcelang", "Local Source"},
        {"other", "Other"},
        {"multi", "Multi-language"}
    };

    auto it = languageNames.find(langCode);
    if (it != languageNames.end()) {
        return it->second;
    }

    // Return the code itself if not found (capitalize first letter)
    if (!langCode.empty()) {
        std::string result = langCode;
        result[0] = std::toupper(result[0]);
        return result;
    }

    return "Unknown";
}

ExtensionsTab::ExtensionsTab() {
    // Programmatic layout (like SourceBrowseTab)
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20, 30, 20, 30);

    // Header with title and refresh button
    auto* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setMarginBottom(15);

    // Title
    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Extensions");
    m_titleLabel->setFontSize(24);
    headerBox->addView(m_titleLabel);

    // Refresh button
    m_refreshBtn = new brls::Button();
    m_refreshBtn->setText("Refresh");
    m_refreshBtn->registerClickAction([this](brls::View*) {
        refreshExtensions();
        return true;
    });
    headerBox->addView(m_refreshBtn);

    this->addView(headerBox);

    // Scrolling content area
    m_scrollFrame = new brls::ScrollingFrame();
    m_scrollFrame->setGrow(1.0f);

    // Content box inside scroll frame
    m_listBox = new brls::Box();
    m_listBox->setAxis(brls::Axis::COLUMN);
    m_scrollFrame->setContentView(m_listBox);

    this->addView(m_scrollFrame);

    // Use fast mode for initial load (single query, client-side filtering)
    loadExtensionsFast();
}

void ExtensionsTab::onFocusGained() {
    brls::Box::onFocusGained();
}

void ExtensionsTab::loadExtensionsFast() {
    brls::Logger::debug("Loading extensions list (fast mode - single query)...");

    // Show loading state
    brls::sync([this]() {
        showLoading("Loading extensions...");
    });

    brls::async([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        const AppSettings& settings = Application::getInstance().getSettings();

        // Use cached data if available
        std::vector<Extension> allExtensions;
        if (m_cacheLoaded && !m_cachedExtensions.empty()) {
            allExtensions = m_cachedExtensions;
            brls::Logger::debug("Using cached extensions ({} total)", allExtensions.size());
        } else {
            // Single query to fetch ALL extensions (like Kodi addon)
            bool success = client.fetchExtensionList(allExtensions);
            if (!success) {
                brls::sync([this]() {
                    showError("Failed to load extensions");
                });
                return;
            }
            // Cache the results
            m_cachedExtensions = allExtensions;
            m_cacheLoaded = true;
            brls::Logger::debug("Fetched and cached {} extensions", allExtensions.size());
        }

        // Get language filter from settings
        std::set<std::string> filterLanguages = settings.enabledSourceLanguages;

        // Default to English if no languages configured
        if (filterLanguages.empty()) {
            filterLanguages.insert("en");
            brls::Logger::debug("No language filter set, defaulting to English");
        }

        // Clear and rebuild extension lists
        m_extensions.clear();
        m_updates.clear();
        m_installed.clear();
        m_uninstalled.clear();

        // Invalidate grouping cache since data changed
        m_groupingCacheValid = false;

        // Client-side filtering and categorization
        for (const auto& ext : allExtensions) {
            if (ext.installed) {
                // Always show installed extensions
                m_extensions.push_back(ext);
                if (ext.hasUpdate) {
                    m_updates.push_back(ext);
                } else {
                    m_installed.push_back(ext);
                }
            } else {
                // Filter uninstalled by language
                bool languageMatch = false;

                // Check exact match
                if (filterLanguages.count(ext.lang) > 0) {
                    languageMatch = true;
                }
                // Check base language (e.g., "zh" matches "zh-Hans")
                else {
                    std::string baseLang = ext.lang;
                    size_t dashPos = baseLang.find('-');
                    if (dashPos != std::string::npos) {
                        baseLang = baseLang.substr(0, dashPos);
                    }
                    if (filterLanguages.count(baseLang) > 0) {
                        languageMatch = true;
                    }
                }

                // Always show multi-language and "all" language extensions
                if (ext.lang == "multi" || ext.lang == "all") {
                    languageMatch = true;
                }

                if (languageMatch) {
                    m_extensions.push_back(ext);
                    m_uninstalled.push_back(ext);
                }
            }
        }

        brls::Logger::debug("Fast mode: {} installed ({} updates), {} uninstalled (filtered from {} total)",
                           m_installed.size() + m_updates.size(), m_updates.size(),
                           m_uninstalled.size(), allExtensions.size());

        brls::sync([this]() {
            populateUnifiedList();
        });
    });
}

void ExtensionsTab::refreshExtensions() {
    brls::Logger::info("Refreshing extensions from server...");

    // Clear all caches to force fresh fetch
    m_cachedExtensions.clear();
    m_cacheLoaded = false;
    m_groupingCacheValid = false;

    brls::Application::notify("Refreshing extensions...");

    // Reload using fast mode
    loadExtensionsFast();
}

void ExtensionsTab::loadExtensions() {
    brls::Logger::debug("Loading extensions list with server-side filtering...");

    brls::async([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        const AppSettings& settings = Application::getInstance().getSettings();

        // Get language filter from settings
        std::set<std::string> filterLanguages = settings.enabledSourceLanguages;

        // Default to English if no languages configured (to avoid loading hundreds of extensions)
        if (filterLanguages.empty()) {
            filterLanguages.insert("en");
            brls::Logger::debug("No language filter set, defaulting to English");
        }

        // Fetch installed extensions (no language filter needed - always show all installed)
        std::vector<Extension> installedExtensions;
        bool installedSuccess = client.fetchInstalledExtensions(installedExtensions);

        // Fetch uninstalled extensions with server-side language filter
        std::vector<Extension> uninstalledExtensions;
        bool uninstalledSuccess = client.fetchUninstalledExtensions(uninstalledExtensions, filterLanguages);

        if (installedSuccess || uninstalledSuccess) {
            // Clear and rebuild extension lists
            m_extensions.clear();
            m_updates.clear();
            m_installed.clear();
            m_uninstalled.clear();

            // Categorize installed extensions
            for (const auto& ext : installedExtensions) {
                m_extensions.push_back(ext);
                if (ext.hasUpdate) {
                    m_updates.push_back(ext);
                } else {
                    m_installed.push_back(ext);
                }
            }

            // Add uninstalled extensions (already filtered by server)
            for (const auto& ext : uninstalledExtensions) {
                m_extensions.push_back(ext);
                m_uninstalled.push_back(ext);
            }

            brls::Logger::debug("Loaded {} installed ({} updates) and {} uninstalled extensions",
                               installedExtensions.size(), m_updates.size(), uninstalledExtensions.size());

            brls::sync([this]() {
                populateUnifiedList();
            });
        } else {
            brls::sync([this]() {
                showError("Failed to load extensions");
            });
        }
    });
}

std::vector<Extension> ExtensionsTab::getFilteredExtensions(const std::vector<Extension>& extensions, bool forceLanguageFilter) {
    const AppSettings& settings = Application::getInstance().getSettings();

    // Determine which languages to filter by
    std::set<std::string> filterLanguages = settings.enabledSourceLanguages;

    // For uninstalled extensions (forceLanguageFilter=true), if no language is set,
    // default to English to avoid showing hundreds of extensions in all languages
    if (forceLanguageFilter && filterLanguages.empty()) {
        filterLanguages.insert("en");  // Default to English
        brls::Logger::debug("No language filter set, defaulting to English for uninstalled extensions");
    }

    // If we have languages to filter by, apply the filter
    if (!filterLanguages.empty()) {
        std::vector<Extension> filtered;
        for (const auto& ext : extensions) {
            bool languageMatch = false;

            // Check exact match first
            if (filterLanguages.count(ext.lang) > 0) {
                languageMatch = true;
            }
            // Check if base language is enabled (e.g., "zh" matches "zh-Hans", "zh-Hant")
            else {
                std::string baseLang = ext.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                }

                if (filterLanguages.count(baseLang) > 0) {
                    languageMatch = true;
                }
            }

            // Always show multi-language and "all" language extensions
            if (ext.lang == "multi" || ext.lang == "all") {
                languageMatch = true;
            }

            if (languageMatch) {
                filtered.push_back(ext);
            }
        }
        return filtered;
    }

    // No filtering - return all (only for installed extensions when no language set)
    return extensions;
}

std::map<std::string, std::vector<Extension>> ExtensionsTab::groupExtensionsByLanguage(const std::vector<Extension>& extensions) {
    std::map<std::string, std::vector<Extension>> grouped;

    for (const auto& ext : extensions) {
        std::string lang = ext.lang.empty() ? "other" : ext.lang;
        grouped[lang].push_back(ext);
    }

    // Sort extensions within each language group alphabetically
    for (auto& pair : grouped) {
        std::sort(pair.second.begin(), pair.second.end(),
            [](const Extension& a, const Extension& b) {
                return a.name < b.name;
            });
    }

    return grouped;
}

std::vector<std::string> ExtensionsTab::getSortedLanguages(const std::map<std::string, std::vector<Extension>>& grouped) {
    const AppSettings& settings = Application::getInstance().getSettings();
    std::vector<std::string> languages;

    for (const auto& pair : grouped) {
        languages.push_back(pair.first);
    }

    // Sort languages: enabled languages first (in order), then others alphabetically
    std::sort(languages.begin(), languages.end(),
        [this, &settings](const std::string& a, const std::string& b) {
            bool aEnabled = settings.enabledSourceLanguages.count(a) > 0;
            bool bEnabled = settings.enabledSourceLanguages.count(b) > 0;

            // If both are enabled or both are not enabled, sort by display name
            if (aEnabled == bEnabled) {
                // English first within enabled or non-enabled groups
                if (a == "en" && b != "en") return true;
                if (a != "en" && b == "en") return false;
                return getLanguageDisplayName(a) < getLanguageDisplayName(b);
            }

            // Enabled languages come first
            return aEnabled > bEnabled;
        });

    return languages;
}

brls::Box* ExtensionsTab::createSectionHeader(const std::string& title, int count) {
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(10, 15, 10, 15);
    header->setMarginTop(5);
    header->setMarginBottom(5);
    header->setGrow(1.0f);  // Fill available space (respects parent padding unlike setWidthPercentage)
    header->setBackgroundColor(nvgRGBA(0, 100, 80, 255));  // Teal section header
    header->setCornerRadius(6);
    header->setFocusable(true);

    // Section title label
    auto* titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(18);
    titleLabel->setTextColor(nvgRGB(255, 255, 255));
    header->addView(titleLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(count));
    countLabel->setFontSize(14);
    countLabel->setTextColor(nvgRGB(200, 255, 200));
    header->addView(countLabel);

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

brls::Box* ExtensionsTab::createLanguageHeader(const std::string& langCode, int extensionCount) {
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(6, 12, 6, 12);
    header->setMarginTop(8);
    header->setMarginBottom(3);
    header->setMarginLeft(10);
    header->setGrow(1.0f);  // Fill available space (respects margins unlike setWidthPercentage)
    header->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
    header->setCornerRadius(4);
    header->setFocusable(true);

    // Language name label
    auto* langLabel = new brls::Label();
    langLabel->setText(getLanguageDisplayName(langCode));
    langLabel->setFontSize(14);
    langLabel->setTextColor(nvgRGB(180, 180, 180));
    header->addView(langLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(extensionCount));
    countLabel->setFontSize(11);
    countLabel->setTextColor(nvgRGB(120, 120, 120));
    header->addView(countLabel);

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

void ExtensionsTab::populateUnifiedList() {
    if (!m_listBox) return;

    // Clear existing views and tracking data
    m_listBox->clearViews();
    m_extensionItems.clear();
    m_currentBatchIndex = 0;
    m_isPopulating = true;

    // Sort installed and updates alphabetically by name (only once)
    std::sort(m_updates.begin(), m_updates.end(),
        [](const Extension& a, const Extension& b) { return a.name < b.name; });
    std::sort(m_installed.begin(), m_installed.end(),
        [](const Extension& a, const Extension& b) { return a.name < b.name; });

    // Cache grouped/sorted data for uninstalled extensions (only recompute when data changes)
    if (!m_groupingCacheValid) {
        m_cachedGrouped = groupExtensionsByLanguage(m_uninstalled);
        m_cachedSortedLanguages = getSortedLanguages(m_cachedGrouped);
        m_groupingCacheValid = true;
    }

    // Show message if everything is empty
    if (m_updates.empty() && m_installed.empty() && m_uninstalled.empty()) {
        auto* emptyLabel = new brls::Label();
        emptyLabel->setText("No extensions available");
        emptyLabel->setFontSize(16);
        emptyLabel->setMargins(20, 20, 20, 20);
        m_listBox->addView(emptyLabel);
        m_isPopulating = false;
        return;
    }

    // Start batched population
    populateBatch();
}

void ExtensionsTab::populateBatch() {
    if (!m_listBox || !m_isPopulating) return;

    int itemsThisBatch = 0;

    // Build a flat list of extensions with section markers for simpler batching
    // Section indices: 0 = updates, 1 = installed, 2+ = uninstalled by language

    // Calculate flat index ranges
    int totalUpdates = (int)m_updates.size();
    int totalInstalled = (int)m_installed.size();
    int totalUninstalled = 0;
    for (const auto& pair : m_cachedGrouped) {
        totalUninstalled += (int)pair.second.size();
    }
    int totalItems = totalUpdates + totalInstalled + totalUninstalled;

    // Process items based on current batch index
    while (itemsThisBatch < BATCH_SIZE && m_currentBatchIndex < totalItems) {
        int idx = m_currentBatchIndex;

        if (idx < totalUpdates) {
            // Updates section
            if (idx == 0 && !m_updates.empty()) {
                auto* header = createSectionHeader("Updates Available", totalUpdates);
                m_listBox->addView(header);
            }
            auto* item = createExtensionItem(m_updates[idx]);
            if (item) {
                m_listBox->addView(item);
                itemsThisBatch++;
            }
        } else if (idx < totalUpdates + totalInstalled) {
            // Installed section
            int localIdx = idx - totalUpdates;
            if (localIdx == 0 && !m_installed.empty()) {
                auto* header = createSectionHeader("Installed", totalInstalled);
                m_listBox->addView(header);
            }
            auto* item = createExtensionItem(m_installed[localIdx]);
            if (item) {
                m_listBox->addView(item);
                itemsThisBatch++;
            }
        } else {
            // Uninstalled section (grouped by language)
            int uninstalledIdx = idx - totalUpdates - totalInstalled;

            if (uninstalledIdx == 0 && !m_uninstalled.empty()) {
                auto* header = createSectionHeader("Available to Install", totalUninstalled);
                m_listBox->addView(header);
            }

            // Find the correct language group
            int offset = 0;
            for (const auto& langCode : m_cachedSortedLanguages) {
                const auto& langExtensions = m_cachedGrouped[langCode];
                int groupSize = (int)langExtensions.size();

                if (uninstalledIdx < offset + groupSize) {
                    // Add language header if at start of group
                    if (uninstalledIdx == offset) {
                        auto* langHeader = createLanguageHeader(langCode, groupSize);
                        m_listBox->addView(langHeader);
                    }

                    int localIdx = uninstalledIdx - offset;
                    auto* item = createExtensionItem(langExtensions[localIdx]);
                    if (item) {
                        m_listBox->addView(item);
                        itemsThisBatch++;
                    }
                    break;
                }
                offset += groupSize;
            }
        }

        m_currentBatchIndex++;
    }

    // Schedule next batch if more items remain
    if (m_currentBatchIndex < totalItems) {
        scheduleNextBatch();
    } else {
        m_isPopulating = false;
        // Icons now load on focus/hover - no bulk loading needed
        brls::Logger::debug("ExtensionsTab: Finished populating {} items in batches (icons load on focus)", m_extensionItems.size());
    }
}

void ExtensionsTab::scheduleNextBatch() {
    // Use sync with a frame delay for smoother UI
    brls::sync([this]() {
        if (m_isPopulating) {
            populateBatch();
        }
    });
}

brls::Box* ExtensionsTab::createExtensionItem(const Extension& ext) {
    auto* container = new brls::Box();
    container->setAxis(brls::Axis::ROW);
    container->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    container->setAlignItems(brls::AlignItems::CENTER);
    container->setPadding(8, 12, 8, 12);
    container->setMarginBottom(3);
    container->setMarginLeft(ext.installed ? 0 : 20);  // Indent uninstalled items under language headers
    container->setGrow(1.0f);  // Fill available space (respects margins unlike setWidthPercentage)
    container->setFocusable(true);

    // Left side: icon and info
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::ROW);
    leftBox->setAlignItems(brls::AlignItems::CENTER);
    leftBox->setGrow(1.0f);
    leftBox->setShrink(1.0f);  // Allow shrinking to prevent overflow

    // Extension icon - created but loading deferred
    auto* icon = new brls::Image();
    icon->setSize(brls::Size(36, 36));
    icon->setMarginRight(12);
    leftBox->addView(icon);

    // Extension info - simplified layout
    auto* infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setShrink(1.0f);  // Allow shrinking to prevent overflow

    auto* nameLabel = new brls::Label();
    nameLabel->setText(ext.name);
    nameLabel->setFontSize(14);
    infoBox->addView(nameLabel);

    auto* detailLabel = new brls::Label();
    std::string detailText = "v" + ext.versionName;
    if (!ext.installed) {
        detailText = getLanguageDisplayName(ext.lang) + " • " + detailText;
    }
    if (ext.isNsfw) {
        detailText += " • 18+";
    }
    detailLabel->setText(detailText);
    detailLabel->setFontSize(10);
    detailLabel->setTextColor(nvgRGB(140, 140, 140));
    infoBox->addView(detailLabel);

    leftBox->addView(infoBox);
    container->addView(leftBox);

    // Right side: action button
    auto* actionBtn = new brls::Button();

    if (ext.installed) {
        if (ext.hasUpdate) {
            actionBtn->setText("Update");
            actionBtn->setBackgroundColor(nvgRGBA(255, 152, 0, 255));  // Orange for updates
            actionBtn->registerClickAction([this, ext](brls::View*) {
                updateExtension(ext);
                return true;
            });
        } else {
            actionBtn->setText("Uninstall");
            actionBtn->setBackgroundColor(nvgRGBA(100, 100, 100, 255));  // Gray for uninstall
            actionBtn->registerClickAction([this, ext](brls::View*) {
                uninstallExtension(ext);
                return true;
            });
        }
    } else {
        actionBtn->setText("Install");
        actionBtn->setBackgroundColor(nvgRGBA(0, 150, 136, 255));  // Teal for install
        actionBtn->registerClickAction([this, ext](brls::View*) {
            installExtension(ext);
            return true;
        });

        // Make whole row clickable to install for uninstalled extensions
        container->registerClickAction([this, ext](brls::View*) {
            installExtension(ext);
            return true;
        });
    }

    container->addView(actionBtn);

    // Add touch gesture support
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container));

    // Track this item for incremental updates and deferred icon loading
    ExtensionItemInfo itemInfo;
    itemInfo.container = container;
    itemInfo.icon = icon;
    itemInfo.pkgName = ext.pkgName;
    itemInfo.iconUrl = ext.iconUrl.empty() ? "" : Application::getInstance().getServerUrl() + ext.iconUrl;
    itemInfo.iconLoaded = false;
    m_extensionItems.push_back(itemInfo);

    // Load icon on focus/hover only (not all at once)
    size_t itemIndex = m_extensionItems.size() - 1;
    container->getFocusEvent()->subscribe([this, itemIndex, icon](brls::View* view) {
        if (itemIndex < m_extensionItems.size()) {
            auto& item = m_extensionItems[itemIndex];
            if (!item.iconLoaded && !item.iconUrl.empty() && item.icon) {
                item.iconLoaded = true;
                ImageLoader::loadAsync(item.iconUrl, [](brls::Image* img) {}, item.icon);
            }
        }
    });

    return container;
}

void ExtensionsTab::loadVisibleIcons() {
    // Load icons for items that haven't been loaded yet
    // This is called periodically or when scrolling
    for (auto& item : m_extensionItems) {
        if (!item.iconLoaded && !item.iconUrl.empty() && item.icon) {
            item.iconLoaded = true;
            ImageLoader::loadAsync(item.iconUrl, [](brls::Image* img) {}, item.icon);
        }
    }
}

ExtensionsTab::ExtensionItemInfo* ExtensionsTab::findExtensionItem(const std::string& pkgName) {
    for (auto& item : m_extensionItems) {
        if (item.pkgName == pkgName) {
            return &item;
        }
    }
    return nullptr;
}

void ExtensionsTab::updateExtensionItemStatus(const std::string& pkgName, bool installed, bool hasUpdate) {
    // Find and update the extension in our data structures
    // This avoids full reload for simple status changes

    // Update cached data
    for (auto& ext : m_cachedExtensions) {
        if (ext.pkgName == pkgName) {
            ext.installed = installed;
            ext.hasUpdate = hasUpdate;
            break;
        }
    }

    // Invalidate grouping cache since status changed
    m_groupingCacheValid = false;
}

void ExtensionsTab::installExtension(const Extension& ext) {
    brls::Logger::info("Installing extension: {}", ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.installExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Installed: " + ext.name);
                // Update local cache instead of full server refetch
                updateExtensionItemStatus(ext.pkgName, true, false);
                // Still need to rebuild UI but data is cached
                loadExtensionsFast();
            } else {
                brls::Application::notify("Failed to install: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::updateExtension(const Extension& ext) {
    brls::Logger::info("Updating extension: {}", ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.updateExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Updated: " + ext.name);
                // Update local cache
                updateExtensionItemStatus(ext.pkgName, true, false);
                loadExtensionsFast();
            } else {
                brls::Application::notify("Failed to update: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::uninstallExtension(const Extension& ext) {
    brls::Logger::info("Uninstalling extension: {}", ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.uninstallExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Uninstalled: " + ext.name);
                // Update local cache
                updateExtensionItemStatus(ext.pkgName, false, false);
                loadExtensionsFast();
            } else {
                brls::Application::notify("Failed to uninstall: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::showError(const std::string& message) {
    if (!m_listBox) return;

    m_listBox->clearViews();

    auto* errorLabel = new brls::Label();
    errorLabel->setText(message);
    errorLabel->setFontSize(16);
    errorLabel->setTextColor(nvgRGB(255, 100, 100));
    errorLabel->setMargins(20, 20, 20, 20);
    m_listBox->addView(errorLabel);
}

void ExtensionsTab::showLoading(const std::string& message) {
    if (!m_listBox) return;

    m_listBox->clearViews();

    auto* loadingLabel = new brls::Label();
    loadingLabel->setText(message);
    loadingLabel->setFontSize(16);
    loadingLabel->setTextColor(nvgRGB(180, 180, 180));
    loadingLabel->setMargins(20, 20, 20, 20);
    m_listBox->addView(loadingLabel);
}

} // namespace vitasuwayomi
