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
#include <cctype>

namespace vitasuwayomi {

// Static member definitions (required for ODR-use with std::min)
const int ExtensionsTab::BATCH_SIZE;
const int ExtensionsTab::BATCH_DELAY_MS;
const int ExtensionsTab::ITEMS_PER_PAGE;

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
    m_titleLabel->setGrow(1.0f);
    headerBox->addView(m_titleLabel);

    // Button container for icons
    auto* buttonBox = new brls::Box();
    buttonBox->setAxis(brls::Axis::ROW);
    buttonBox->setAlignItems(brls::AlignItems::FLEX_END);

    // Search button with Start icon above
    auto* searchContainer = new brls::Box();
    searchContainer->setAxis(brls::Axis::COLUMN);
    searchContainer->setAlignItems(brls::AlignItems::CENTER);
    searchContainer->setMarginRight(10);

    // Start button icon - use actual image dimensions (64x16)
    auto* startButtonIcon = new brls::Image();
    startButtonIcon->setWidth(64);
    startButtonIcon->setHeight(16);
    startButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    startButtonIcon->setImageFromFile("app0:resources/images/start_button.png");
    startButtonIcon->setMarginBottom(2);
    searchContainer->addView(startButtonIcon);

    auto* searchBox = new brls::Box();
    searchBox->setFocusable(true);
    searchBox->setPadding(8, 8, 8, 8);
    searchBox->setCornerRadius(4);
    searchBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    m_searchIcon = new brls::Image();
    m_searchIcon->setSize(brls::Size(24, 24));
    m_searchIcon->setImageFromFile("app0:resources/icons/search.png");
    searchBox->addView(m_searchIcon);
    searchBox->registerClickAction([this](brls::View*) {
        brls::sync([this]() {
            showSearchDialog();
        });
        return true;
    });
    searchBox->addGestureRecognizer(new brls::TapGestureRecognizer(searchBox));
    searchContainer->addView(searchBox);
    buttonBox->addView(searchContainer);

    // Refresh button with Triangle icon above
    auto* refreshContainer = new brls::Box();
    refreshContainer->setAxis(brls::Axis::COLUMN);
    refreshContainer->setAlignItems(brls::AlignItems::CENTER);

    // Triangle button icon - use actual image dimensions (16x16)
    auto* triangleButtonIcon = new brls::Image();
    triangleButtonIcon->setWidth(16);
    triangleButtonIcon->setHeight(16);
    triangleButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    triangleButtonIcon->setImageFromFile("app0:resources/images/triangle_button.png");
    triangleButtonIcon->setMarginBottom(2);
    refreshContainer->addView(triangleButtonIcon);

    m_refreshBox = new brls::Box();
    m_refreshBox->setFocusable(true);
    m_refreshBox->setPadding(8, 8, 8, 8);
    m_refreshBox->setCornerRadius(4);
    m_refreshBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    m_refreshIcon = new brls::Image();
    m_refreshIcon->setSize(brls::Size(24, 24));
    m_refreshIcon->setImageFromFile("app0:resources/icons/refresh.png");
    m_refreshBox->addView(m_refreshIcon);
    m_refreshBox->registerClickAction([this](brls::View*) {
        brls::sync([this]() {
            refreshExtensions();
        });
        return true;
    });
    m_refreshBox->addGestureRecognizer(new brls::TapGestureRecognizer(m_refreshBox));
    refreshContainer->addView(m_refreshBox);
    buttonBox->addView(refreshContainer);

    headerBox->addView(buttonBox);

    this->addView(headerBox);

    // Register Start button to open search dialog
    // Use brls::sync to defer IME opening to avoid crash during controller input handling
    this->registerAction("Search", brls::ControllerButton::BUTTON_START, [this](brls::View* view) {
        brls::sync([this]() {
            showSearchDialog();
        });
        return true;
    });

    // Register Triangle (Y) button to refresh extensions
    // Use brls::sync to defer refresh to avoid crash during controller input handling
    this->registerAction("Refresh", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        brls::sync([this]() {
            refreshExtensions();
        });
        return true;
    });

    // Scrolling content area
    m_scrollFrame = new brls::ScrollingFrame();
    m_scrollFrame->setGrow(1.0f);

    // Content box inside scroll frame
    m_listBox = new brls::Box();
    m_listBox->setAxis(brls::Axis::COLUMN);
    m_scrollFrame->setContentView(m_listBox);

    this->addView(m_scrollFrame);

    // Search results page (hidden initially)
    m_searchResultsFrame = new brls::ScrollingFrame();
    m_searchResultsFrame->setGrow(1.0f);
    m_searchResultsFrame->setVisibility(brls::Visibility::GONE);

    m_searchResultsBox = new brls::Box();
    m_searchResultsBox->setAxis(brls::Axis::COLUMN);
    m_searchResultsFrame->setContentView(m_searchResultsBox);

    // Register Circle (B) button on search results box to go back
    m_searchResultsBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this]() {
            hideSearchResults();
        });
        return true;
    });

    this->addView(m_searchResultsFrame);

    // Register Circle (B) button to go back from search results
    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        if (m_isSearchActive) {
            brls::sync([this]() {
                hideSearchResults();
            });
            return true;
        }
        return false;  // Let default back behavior happen
    });

    // Use fast mode for initial load (single query, client-side filtering)
    loadExtensionsFast();
}

void ExtensionsTab::onFocusGained() {
    brls::Box::onFocusGained();

    // Refresh UI if an extension operation was performed while away
    if (m_needsRefresh) {
        m_needsRefresh = false;
        brls::Logger::debug("ExtensionsTab: Refreshing UI after extension operation");
        refreshUIFromCache();
    }
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

ExtensionsTab::SectionState& ExtensionsTab::getSectionState(SectionType type) {
    switch (type) {
        case SectionType::Updates: return m_updatesSection;
        case SectionType::Installed: return m_installedSection;
        default: return m_updatesSection;  // Fallback
    }
}

const std::vector<Extension>& ExtensionsTab::getSectionExtensions(SectionType type) {
    switch (type) {
        case SectionType::Updates: return m_updates;
        case SectionType::Installed: return m_installed;
        default: return m_updates;  // Fallback
    }
}

brls::Box* ExtensionsTab::createCollapsibleSectionHeader(const std::string& title, int count,
                                                          SectionType sectionType) {
    auto& state = getSectionState(sectionType);

    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::FLEX_START);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(10, 15, 10, 15);
    header->setMarginTop(5);
    header->setMarginBottom(5);
    header->setGrow(1.0f);
    header->setBackgroundColor(nvgRGBA(0, 100, 80, 255));  // Teal section header
    header->setCornerRadius(6);
    header->setFocusable(true);

    // Expand/collapse arrow
    auto* arrowLabel = new brls::Label();
    arrowLabel->setText(state.expanded ? "v" : ">");
    arrowLabel->setFontSize(16);
    arrowLabel->setTextColor(nvgRGB(255, 255, 255));
    arrowLabel->setMarginRight(10);
    header->addView(arrowLabel);

    // Section title label
    auto* titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(18);
    titleLabel->setTextColor(nvgRGB(255, 255, 255));
    titleLabel->setGrow(1.0f);
    header->addView(titleLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(count));
    countLabel->setFontSize(14);
    countLabel->setTextColor(nvgRGB(200, 255, 200));
    header->addView(countLabel);

    // Store reference to header for updating arrow
    state.headerBox = header;

    // Click to expand/collapse - capture by value (enum is safe to copy)
    header->registerClickAction([this, sectionType, arrowLabel](brls::View*) {
        toggleSection(sectionType);
        // Update arrow
        auto& s = getSectionState(sectionType);
        arrowLabel->setText(s.expanded ? "v" : ">");
        return true;
    });

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

brls::Box* ExtensionsTab::createAvailableSectionHeader(const std::string& title, int count) {
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::FLEX_START);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(10, 15, 10, 15);
    header->setMarginTop(5);
    header->setMarginBottom(5);
    header->setGrow(1.0f);
    header->setBackgroundColor(nvgRGBA(0, 100, 80, 255));  // Teal section header
    header->setCornerRadius(6);
    header->setFocusable(true);

    // Expand/collapse arrow
    auto* arrowLabel = new brls::Label();
    arrowLabel->setText(m_availableSection.expanded ? "v" : ">");
    arrowLabel->setFontSize(16);
    arrowLabel->setTextColor(nvgRGB(255, 255, 255));
    arrowLabel->setMarginRight(10);
    header->addView(arrowLabel);

    // Section title label
    auto* titleLabel = new brls::Label();
    titleLabel->setText(title);
    titleLabel->setFontSize(18);
    titleLabel->setTextColor(nvgRGB(255, 255, 255));
    titleLabel->setGrow(1.0f);
    header->addView(titleLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(count));
    countLabel->setFontSize(14);
    countLabel->setTextColor(nvgRGB(200, 255, 200));
    header->addView(countLabel);

    // Store reference to header for updating arrow
    m_availableSection.headerBox = header;

    // Click to expand/collapse
    header->registerClickAction([this, arrowLabel](brls::View*) {
        toggleAvailableSection();
        arrowLabel->setText(m_availableSection.expanded ? "v" : ">");
        return true;
    });

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

void ExtensionsTab::toggleAvailableSection() {
    m_availableSection.expanded = !m_availableSection.expanded;

    if (m_availableSection.contentBox) {
        m_availableSection.contentBox->clearViews();

        if (m_availableSection.expanded) {
            // Show language group headers only (extensions loaded on demand)
            for (const auto& langCode : m_cachedSortedLanguages) {
                const auto& langExtensions = m_cachedGrouped[langCode];
                auto* langHeader = createCollapsibleLanguageHeader(langCode, langExtensions.size(), langCode);
                m_availableSection.contentBox->addView(langHeader);

                // Create content box for this language (hidden initially)
                m_languageSections[langCode].contentBox = new brls::Box();
                m_languageSections[langCode].contentBox->setAxis(brls::Axis::COLUMN);
                m_availableSection.contentBox->addView(m_languageSections[langCode].contentBox);
            }
        }
    }
}

brls::Box* ExtensionsTab::createCollapsibleLanguageHeader(const std::string& langCode, int count,
                                                           const std::string& langKey) {
    // Initialize section state if not exists
    if (m_languageSections.find(langKey) == m_languageSections.end()) {
        m_languageSections[langKey] = SectionState();
    }
    auto& state = m_languageSections[langKey];

    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::FLEX_START);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(6, 12, 6, 12);
    header->setMarginTop(8);
    header->setMarginBottom(3);
    header->setMarginLeft(10);
    header->setGrow(1.0f);
    header->setBackgroundColor(nvgRGBA(50, 50, 50, 255));
    header->setCornerRadius(4);
    header->setFocusable(true);

    // Expand/collapse arrow
    auto* arrowLabel = new brls::Label();
    arrowLabel->setText(state.expanded ? "v" : ">");
    arrowLabel->setFontSize(12);
    arrowLabel->setTextColor(nvgRGB(180, 180, 180));
    arrowLabel->setMarginRight(8);
    header->addView(arrowLabel);

    // Language name label
    auto* langLabel = new brls::Label();
    langLabel->setText(getLanguageDisplayName(langCode));
    langLabel->setFontSize(14);
    langLabel->setTextColor(nvgRGB(180, 180, 180));
    langLabel->setGrow(1.0f);
    header->addView(langLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(count));
    countLabel->setFontSize(11);
    countLabel->setTextColor(nvgRGB(120, 120, 120));
    header->addView(countLabel);

    state.headerBox = header;

    // Click to expand/collapse
    header->registerClickAction([this, langKey, arrowLabel](brls::View*) {
        toggleLanguageSection(langKey);
        // Update arrow
        auto& s = m_languageSections[langKey];
        arrowLabel->setText(s.expanded ? "v" : ">");
        return true;
    });

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

void ExtensionsTab::toggleSection(SectionType sectionType) {
    auto& state = getSectionState(sectionType);
    const auto& extensions = getSectionExtensions(sectionType);

    if (!state.contentBox) return;

    state.expanded = !state.expanded;
    state.contentBox->clearViews();

    if (state.expanded) {
        state.itemsShown = 0;
        int itemsToShow = std::min((int)extensions.size(), ITEMS_PER_PAGE);

        for (int i = 0; i < itemsToShow; i++) {
            auto* item = createExtensionItem(extensions[i]);
            if (item) {
                state.contentBox->addView(item);
                state.itemsShown++;
            }
        }

        // Add "Show more" button if there are more items
        if (state.itemsShown < (int)extensions.size()) {
            auto* showMoreBtn = createShowMoreButton(sectionType);
            state.contentBox->addView(showMoreBtn);
        }

        // Update D-pad navigation for settings buttons
        updateSettingsButtonNavigation();
    }
}

void ExtensionsTab::toggleLanguageSection(const std::string& langKey) {
    auto it = m_languageSections.find(langKey);
    if (it == m_languageSections.end()) return;

    auto& state = it->second;
    if (!state.contentBox) return;

    state.expanded = !state.expanded;
    state.contentBox->clearViews();

    if (state.expanded) {
        auto groupIt = m_cachedGrouped.find(langKey);
        if (groupIt == m_cachedGrouped.end()) return;

        const auto& extensions = groupIt->second;
        state.itemsShown = 0;
        int itemsToShow = std::min((int)extensions.size(), ITEMS_PER_PAGE);

        for (int i = 0; i < itemsToShow; i++) {
            auto* item = createExtensionItem(extensions[i]);
            if (item) {
                state.contentBox->addView(item);
                state.itemsShown++;
            }
        }

        // Add "Show more" button if there are more items
        if (state.itemsShown < (int)extensions.size()) {
            auto* showMoreBtn = createLanguageShowMoreButton(langKey);
            if (showMoreBtn) {
                state.contentBox->addView(showMoreBtn);
            }
        }

        // Update D-pad navigation for settings buttons
        updateSettingsButtonNavigation();
    }
}

void ExtensionsTab::showMoreItems(SectionType sectionType) {
    auto& state = getSectionState(sectionType);
    const auto& extensions = getSectionExtensions(sectionType);

    if (!state.contentBox) return;

    int startIdx = state.itemsShown;
    int endIdx = std::min(startIdx + ITEMS_PER_PAGE, (int)extensions.size());

    for (int i = startIdx; i < endIdx; i++) {
        auto* item = createExtensionItem(extensions[i]);
        if (item) {
            state.contentBox->addView(item);
            state.itemsShown++;
        }
    }
}

brls::Box* ExtensionsTab::createShowMoreButton(SectionType sectionType) {
    auto& state = getSectionState(sectionType);
    const auto& extensions = getSectionExtensions(sectionType);

    auto* showMoreBox = new brls::Box();
    showMoreBox->setAxis(brls::Axis::ROW);
    showMoreBox->setJustifyContent(brls::JustifyContent::CENTER);
    showMoreBox->setPadding(10, 15, 10, 15);
    showMoreBox->setMarginTop(5);
    showMoreBox->setGrow(1.0f);
    showMoreBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    showMoreBox->setCornerRadius(4);
    showMoreBox->setFocusable(true);

    auto* label = new brls::Label();
    int remaining = extensions.size() - state.itemsShown;
    label->setText("Show more (" + std::to_string(remaining) + " remaining)");
    label->setFontSize(14);
    label->setTextColor(nvgRGB(100, 200, 180));
    showMoreBox->addView(label);

    // Capture by value (enum is safe to copy)
    showMoreBox->registerClickAction([this, sectionType](brls::View*) {
        auto& state = getSectionState(sectionType);
        const auto& extensions = getSectionExtensions(sectionType);

        // Remove the show more button (last view)
        if (state.contentBox && state.contentBox->getChildren().size() > 0) {
            auto& children = state.contentBox->getChildren();
            state.contentBox->removeView(children.back());
        }

        // Add more items
        showMoreItems(sectionType);

        // Add another "Show more" if still more items
        if (state.itemsShown < (int)extensions.size()) {
            auto* newShowMore = createShowMoreButton(sectionType);
            state.contentBox->addView(newShowMore);
        }

        // Update D-pad navigation for settings buttons
        updateSettingsButtonNavigation();

        return true;
    });

    showMoreBox->addGestureRecognizer(new brls::TapGestureRecognizer(showMoreBox));

    return showMoreBox;
}

void ExtensionsTab::showMoreLanguageItems(const std::string& langKey) {
    auto it = m_languageSections.find(langKey);
    if (it == m_languageSections.end()) return;

    auto& state = it->second;
    auto groupIt = m_cachedGrouped.find(langKey);
    if (groupIt == m_cachedGrouped.end()) return;

    const auto& extensions = groupIt->second;
    if (!state.contentBox) return;

    int startIdx = state.itemsShown;
    int endIdx = std::min(startIdx + ITEMS_PER_PAGE, (int)extensions.size());

    for (int i = startIdx; i < endIdx; i++) {
        auto* item = createExtensionItem(extensions[i]);
        if (item) {
            state.contentBox->addView(item);
            state.itemsShown++;
        }
    }
}

brls::Box* ExtensionsTab::createLanguageShowMoreButton(const std::string& langKey) {
    auto& state = m_languageSections[langKey];
    auto groupIt = m_cachedGrouped.find(langKey);
    if (groupIt == m_cachedGrouped.end()) return nullptr;

    const auto& extensions = groupIt->second;

    auto* showMoreBox = new brls::Box();
    showMoreBox->setAxis(brls::Axis::ROW);
    showMoreBox->setJustifyContent(brls::JustifyContent::CENTER);
    showMoreBox->setPadding(8, 12, 8, 12);
    showMoreBox->setMarginTop(5);
    showMoreBox->setMarginLeft(20);
    showMoreBox->setGrow(1.0f);
    showMoreBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    showMoreBox->setCornerRadius(4);
    showMoreBox->setFocusable(true);

    auto* label = new brls::Label();
    int remaining = extensions.size() - state.itemsShown;
    label->setText("Show more (" + std::to_string(remaining) + " remaining)");
    label->setFontSize(12);
    label->setTextColor(nvgRGB(100, 200, 180));
    showMoreBox->addView(label);

    // Capture langKey by value (string copy is safe)
    showMoreBox->registerClickAction([this, langKey](brls::View*) {
        auto it = m_languageSections.find(langKey);
        if (it == m_languageSections.end()) return true;

        auto& state = it->second;
        auto groupIt = m_cachedGrouped.find(langKey);
        if (groupIt == m_cachedGrouped.end()) return true;

        const auto& extensions = groupIt->second;

        // Remove the show more button (last view)
        if (state.contentBox && state.contentBox->getChildren().size() > 0) {
            auto& children = state.contentBox->getChildren();
            state.contentBox->removeView(children.back());
        }

        // Add more items
        showMoreLanguageItems(langKey);

        // Add another "Show more" if still more items
        if (state.itemsShown < (int)extensions.size()) {
            auto* newShowMore = createLanguageShowMoreButton(langKey);
            if (newShowMore) {
                state.contentBox->addView(newShowMore);
            }
        }

        // Update D-pad navigation for settings buttons
        updateSettingsButtonNavigation();

        return true;
    });

    showMoreBox->addGestureRecognizer(new brls::TapGestureRecognizer(showMoreBox));

    return showMoreBox;
}

void ExtensionsTab::populateUnifiedList() {
    if (!m_listBox) return;

    // Clear existing views and tracking data
    m_listBox->clearViews();
    m_extensionItems.clear();
    m_currentBatchIndex = 0;
    m_isPopulating = false;

    // Reset section states
    m_updatesSection = SectionState();
    m_installedSection = SectionState();
    m_availableSection = SectionState();
    m_languageSections.clear();

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
        return;
    }

    // Create collapsible sections - only headers are created initially
    // Content is loaded only when user expands a section

    // Updates section (auto-expand if small, collapse if large)
    if (!m_updates.empty()) {
        m_updatesSection.expanded = (m_updates.size() <= ITEMS_PER_PAGE);
        auto* header = createCollapsibleSectionHeader("Updates Available", m_updates.size(), SectionType::Updates);
        m_listBox->addView(header);

        m_updatesSection.contentBox = new brls::Box();
        m_updatesSection.contentBox->setAxis(brls::Axis::COLUMN);
        m_listBox->addView(m_updatesSection.contentBox);

        if (m_updatesSection.expanded) {
            // Temporarily set to false so toggleSection will expand it
            m_updatesSection.expanded = false;
            toggleSection(SectionType::Updates);
        }
    }

    // Installed section (auto-expand if small, collapse if large)
    if (!m_installed.empty()) {
        m_installedSection.expanded = (m_installed.size() <= ITEMS_PER_PAGE);
        auto* header = createCollapsibleSectionHeader("Installed", m_installed.size(), SectionType::Installed);
        m_listBox->addView(header);

        m_installedSection.contentBox = new brls::Box();
        m_installedSection.contentBox->setAxis(brls::Axis::COLUMN);
        m_listBox->addView(m_installedSection.contentBox);

        if (m_installedSection.expanded) {
            // Temporarily set to false so toggleSection will expand it
            m_installedSection.expanded = false;
            toggleSection(SectionType::Installed);
        }
    }

    // Available section - always start collapsed (this is the big one with 500+ items)
    if (!m_uninstalled.empty()) {
        m_availableSection.expanded = false;
        auto* header = createAvailableSectionHeader("Available to Install", m_uninstalled.size());
        m_listBox->addView(header);

        m_availableSection.contentBox = new brls::Box();
        m_availableSection.contentBox->setAxis(brls::Axis::COLUMN);
        m_listBox->addView(m_availableSection.contentBox);
    }

    brls::Logger::debug("ExtensionsTab: Created collapsible sections - {} updates, {} installed, {} available",
                        m_updates.size(), m_installed.size(), m_uninstalled.size());
}

// Legacy batched population removed - now using collapsible sections with pagination
// See populateUnifiedList() for the new implementation

void ExtensionsTab::populateBatch() {
    // Not used - collapsible sections handle item loading
}

void ExtensionsTab::scheduleNextBatch() {
    // Not used - collapsible sections handle item loading
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

    // Right side box to hold settings icon and status label
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::ROW);
    rightBox->setAlignItems(brls::AlignItems::CENTER);

    // Settings icon for installed extensions (shown when source is configurable)
    brls::Box* settingsBtn = nullptr;
    if (ext.installed) {
        settingsBtn = new brls::Box();
        settingsBtn->setFocusable(true);
        settingsBtn->setPadding(6, 6, 6, 6);
        settingsBtn->setCornerRadius(4);
        settingsBtn->setMarginRight(8);

        auto* settingsIcon = new brls::Image();
        settingsIcon->setSize(brls::Size(20, 20));
        settingsIcon->setImageFromFile("app0:resources/icons/options.png");
        settingsBtn->addView(settingsIcon);

        // Show settings dialog when clicked
        settingsBtn->registerClickAction([this, ext](brls::View*) {
            brls::sync([this, ext]() {
                showSourceSettings(ext);
            });
            return true;
        });
        settingsBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsBtn));

        rightBox->addView(settingsBtn);
    }

    // Status indicator label
    auto* statusLabel = new brls::Label();
    statusLabel->setFontSize(11);
    statusLabel->setMarginLeft(8);

    if (ext.installed) {
        if (ext.hasUpdate) {
            statusLabel->setText("Update");
            statusLabel->setTextColor(nvgRGB(255, 152, 0));  // Orange for updates
            container->registerClickAction([this, ext](brls::View*) {
                updateExtension(ext);
                return true;
            });
        } else {
            statusLabel->setText("Installed");
            statusLabel->setTextColor(nvgRGB(100, 100, 100));  // Gray for installed
            container->registerClickAction([this, ext](brls::View*) {
                uninstallExtension(ext);
                return true;
            });
        }
    } else {
        statusLabel->setText("Install");
        statusLabel->setTextColor(nvgRGB(0, 150, 136));  // Teal for install
        container->registerClickAction([this, ext](brls::View*) {
            installExtension(ext);
            return true;
        });
    }

    rightBox->addView(statusLabel);
    container->addView(rightBox);

    // Add touch gesture support
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container));

    // Track this item for incremental updates and deferred icon loading
    ExtensionItemInfo itemInfo;
    itemInfo.container = container;
    itemInfo.icon = icon;
    itemInfo.settingsBtn = settingsBtn;  // Store for D-pad navigation linking
    itemInfo.pkgName = ext.pkgName;
    itemInfo.iconUrl = ext.iconUrl.empty() ? "" : Application::getInstance().getServerUrl() + ext.iconUrl;
    itemInfo.iconLoaded = false;
    m_extensionItems.push_back(itemInfo);

    size_t itemIndex = m_extensionItems.size() - 1;

    // For installed extensions, load icons immediately
    // For uninstalled extensions, defer loading to focus/hover to save bandwidth
    if (ext.installed && !itemInfo.iconUrl.empty()) {
        auto& item = m_extensionItems[itemIndex];
        item.iconLoaded = true;
        ImageLoader::loadAsync(item.iconUrl, [](brls::Image* img) {}, item.icon);
    } else {
        // Load icon on focus/hover only for uninstalled extensions
        container->getFocusEvent()->subscribe([this, itemIndex, icon](brls::View* view) {
            if (itemIndex < m_extensionItems.size()) {
                auto& item = m_extensionItems[itemIndex];
                if (!item.iconLoaded && !item.iconUrl.empty() && item.icon) {
                    item.iconLoaded = true;
                    ImageLoader::loadAsync(item.iconUrl, [](brls::Image* img) {}, item.icon);
                }
            }
        });
    }

    return container;
}

void ExtensionsTab::updateSettingsButtonNavigation() {
    // Collect all non-null settings buttons in order
    std::vector<brls::Box*> settingsButtons;
    for (const auto& item : m_extensionItems) {
        if (item.settingsBtn != nullptr) {
            settingsButtons.push_back(item.settingsBtn);
        }
    }

    // Set up custom navigation routes between settings buttons
    // When navigating DOWN from a settings button, go to the next settings button
    // When navigating UP from a settings button, go to the previous settings button
    for (size_t i = 0; i < settingsButtons.size(); i++) {
        if (i > 0) {
            // Link UP to previous settings button
            settingsButtons[i]->setCustomNavigationRoute(brls::FocusDirection::UP, settingsButtons[i - 1]);
        }
        if (i < settingsButtons.size() - 1) {
            // Link DOWN to next settings button
            settingsButtons[i]->setCustomNavigationRoute(brls::FocusDirection::DOWN, settingsButtons[i + 1]);
        }
    }

    brls::Logger::debug("ExtensionsTab: Set up navigation for {} settings buttons", settingsButtons.size());
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

void ExtensionsTab::refreshUIFromCache() {
    // Rebuild the categorized lists from cached data
    // This is safe to call from any context as it doesn't clear views synchronously
    const AppSettings& settings = Application::getInstance().getSettings();
    std::set<std::string> filterLanguages = settings.enabledSourceLanguages;
    if (filterLanguages.empty()) {
        filterLanguages.insert("en");
    }

    // Prepare search filter if active
    std::string searchLower;
    if (m_isSearchActive && !m_searchQuery.empty()) {
        searchLower = m_searchQuery;
        std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
    }

    // Clear and rebuild extension lists from cache
    m_extensions.clear();
    m_updates.clear();
    m_installed.clear();
    m_uninstalled.clear();
    m_groupingCacheValid = false;

    for (const auto& ext : m_cachedExtensions) {
        // Apply search filter if active
        if (!searchLower.empty()) {
            std::string nameLower = ext.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;  // Skip if name doesn't match search
            }
        }

        if (ext.installed) {
            m_extensions.push_back(ext);
            if (ext.hasUpdate) {
                m_updates.push_back(ext);
            } else {
                m_installed.push_back(ext);
            }
        } else {
            // Filter uninstalled by language
            bool languageMatch = false;
            if (filterLanguages.count(ext.lang) > 0) {
                languageMatch = true;
            } else {
                std::string baseLang = ext.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                }
                if (filterLanguages.count(baseLang) > 0) {
                    languageMatch = true;
                }
            }
            if (ext.lang == "multi" || ext.lang == "all") {
                languageMatch = true;
            }
            if (languageMatch) {
                m_extensions.push_back(ext);
                m_uninstalled.push_back(ext);
            }
        }
    }

    // Rebuild UI - this clears and repopulates the list safely
    populateUnifiedList();
}

void ExtensionsTab::showSearchDialog() {
    // Get user's configured language for the keyboard if available
    brls::Application::getImeManager()->openForText([this](std::string text) {
        if (text.empty()) {
            // User cancelled or entered empty - do nothing
            return;
        }

        m_searchQuery = text;
        m_isSearchActive = true;

        // Show search results on a separate page
        showSearchResults();
    }, "Search Extensions", "", 64, "");
}

void ExtensionsTab::showSearchResults() {
    if (!m_searchResultsBox || !m_searchResultsFrame) return;

    // Convert search query to lowercase for case-insensitive search
    std::string searchLower = m_searchQuery;
    std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);

    brls::Application::notify("Searching: " + m_searchQuery);

    // Filter cached extensions by name
    const AppSettings& settings = Application::getInstance().getSettings();
    std::set<std::string> filterLanguages = settings.enabledSourceLanguages;
    if (filterLanguages.empty()) {
        filterLanguages.insert("en");
    }

    // Collect matching extensions
    std::vector<Extension> searchResults;
    for (const auto& ext : m_cachedExtensions) {
        // Check if name matches search query (case-insensitive)
        std::string nameLower = ext.name;
        std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);

        if (nameLower.find(searchLower) == std::string::npos) {
            continue;  // Skip if name doesn't match search
        }

        if (ext.installed) {
            searchResults.push_back(ext);
        } else {
            // Apply language filter for uninstalled
            bool languageMatch = false;
            if (filterLanguages.count(ext.lang) > 0) {
                languageMatch = true;
            } else {
                std::string baseLang = ext.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                }
                if (filterLanguages.count(baseLang) > 0) {
                    languageMatch = true;
                }
            }
            if (ext.lang == "multi" || ext.lang == "all") {
                languageMatch = true;
            }
            if (languageMatch) {
                searchResults.push_back(ext);
            }
        }
    }

    // Sort results alphabetically
    std::sort(searchResults.begin(), searchResults.end(),
        [](const Extension& a, const Extension& b) { return a.name < b.name; });

    // Clear and populate search results box
    m_searchResultsBox->clearViews();

    // Header with back button and search query
    auto* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setMarginBottom(15);

    // Back button with Circle icon above
    auto* backContainer = new brls::Box();
    backContainer->setAxis(brls::Axis::COLUMN);
    backContainer->setAlignItems(brls::AlignItems::CENTER);
    backContainer->setMarginRight(15);

    // Circle button icon - use actual image dimensions (16x16)
    auto* circleButtonIcon = new brls::Image();
    circleButtonIcon->setWidth(16);
    circleButtonIcon->setHeight(16);
    circleButtonIcon->setScalingType(brls::ImageScalingType::FIT);
    circleButtonIcon->setImageFromFile("app0:resources/images/circle_button.png");
    circleButtonIcon->setMarginBottom(2);
    backContainer->addView(circleButtonIcon);

    auto* backBox = new brls::Box();
    backBox->setFocusable(true);
    backBox->setPadding(8, 12, 8, 12);
    backBox->setCornerRadius(4);
    backBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));
    auto* backLabel = new brls::Label();
    backLabel->setText("< Back");
    backLabel->setFontSize(14);
    backBox->addView(backLabel);
    backBox->registerClickAction([this](brls::View*) {
        brls::sync([this]() {
            hideSearchResults();
        });
        return true;
    });
    backBox->addGestureRecognizer(new brls::TapGestureRecognizer(backBox));
    // Register Circle (B) button action on the back button itself
    backBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View*) {
        brls::sync([this]() {
            hideSearchResults();
        });
        return true;
    });
    backContainer->addView(backBox);
    headerBox->addView(backContainer);

    // Search query title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Search: \"" + m_searchQuery + "\" (" + std::to_string(searchResults.size()) + " results)");
    titleLabel->setFontSize(20);
    titleLabel->setGrow(1.0f);
    headerBox->addView(titleLabel);

    m_searchResultsBox->addView(headerBox);

    // Show results or empty message
    if (searchResults.empty()) {
        auto* emptyLabel = new brls::Label();
        emptyLabel->setText("No extensions found matching \"" + m_searchQuery + "\"");
        emptyLabel->setFontSize(16);
        emptyLabel->setMargins(20, 20, 20, 20);
        emptyLabel->setTextColor(nvgRGB(180, 180, 180));
        m_searchResultsBox->addView(emptyLabel);
    } else {
        // Add each search result
        for (const auto& ext : searchResults) {
            auto* item = createExtensionItem(ext);
            if (item) {
                m_searchResultsBox->addView(item);
            }
        }
    }

    // Hide main list, show search results
    m_scrollFrame->setVisibility(brls::Visibility::GONE);
    m_searchResultsFrame->setVisibility(brls::Visibility::VISIBLE);
    m_titleLabel->setText("Search Results");
}

void ExtensionsTab::hideSearchResults() {
    m_isSearchActive = false;
    m_searchQuery.clear();

    // Show main list, hide search results
    m_searchResultsFrame->setVisibility(brls::Visibility::GONE);
    m_scrollFrame->setVisibility(brls::Visibility::VISIBLE);
    m_titleLabel->setText("Extensions");
}

void ExtensionsTab::clearSearch() {
    if (!m_isSearchActive) return;
    hideSearchResults();
}

void ExtensionsTab::installExtension(const Extension& ext) {
    brls::Logger::info("Installing extension: {}", ext.name);
    brls::Application::notify("Installing: " + ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.installExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Installed: " + ext.name);
                // Update local cache - UI will refresh when user returns to this tab
                updateExtensionItemStatus(ext.pkgName, true, false);
                m_needsRefresh = true;
            } else {
                brls::Application::notify("Failed to install: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::updateExtension(const Extension& ext) {
    brls::Logger::info("Updating extension: {}", ext.name);
    brls::Application::notify("Updating: " + ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.updateExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Updated: " + ext.name);
                // Update local cache - UI will refresh when user returns to this tab
                updateExtensionItemStatus(ext.pkgName, true, false);
                m_needsRefresh = true;
            } else {
                brls::Application::notify("Failed to update: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::uninstallExtension(const Extension& ext) {
    brls::Logger::info("Uninstalling extension: {}", ext.name);
    brls::Application::notify("Uninstalling: " + ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.uninstallExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Uninstalled: " + ext.name);
                // Update local cache - UI will refresh when user returns to this tab
                updateExtensionItemStatus(ext.pkgName, false, false);
                m_needsRefresh = true;
            } else {
                brls::Application::notify("Failed to uninstall: " + ext.name);
            }
        });
    });
}

void ExtensionsTab::showSourceSettings(const Extension& ext) {
    brls::Logger::info("Opening settings for extension: {}", ext.name);
    brls::Application::notify("Loading settings...");

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Source> sources;

        bool success = client.fetchSourcesForExtension(ext.pkgName, sources);

        brls::sync([this, success, ext, sources]() {
            if (!success || sources.empty()) {
                brls::Application::notify("No configurable sources found");
                return;
            }

            // Filter to only configurable sources
            std::vector<Source> configurableSources;
            for (const auto& src : sources) {
                if (src.isConfigurable) {
                    configurableSources.push_back(src);
                }
            }

            if (configurableSources.empty()) {
                brls::Application::notify("No settings available for this extension");
                return;
            }

            // If only one configurable source, open it directly
            if (configurableSources.size() == 1) {
                showSourcePreferencesDialog(configurableSources[0]);
                return;
            }

            // Multiple sources - show selection dialog
            auto* dialog = new brls::Dialog("Select Source");

            auto* contentBox = new brls::Box();
            contentBox->setAxis(brls::Axis::COLUMN);
            contentBox->setPadding(10, 20, 10, 20);

            auto* titleLabel = new brls::Label();
            titleLabel->setText(ext.name + " - Select Source");
            titleLabel->setFontSize(18);
            titleLabel->setMarginBottom(15);
            contentBox->addView(titleLabel);

            // Create source selection list
            for (const auto& src : configurableSources) {
                auto* sourceBox = new brls::Box();
                sourceBox->setAxis(brls::Axis::ROW);
                sourceBox->setAlignItems(brls::AlignItems::CENTER);
                sourceBox->setFocusable(true);
                sourceBox->setPadding(10, 15, 10, 15);
                sourceBox->setMarginBottom(5);
                sourceBox->setCornerRadius(4);
                sourceBox->setBackgroundColor(nvgRGBA(60, 60, 60, 200));

                auto* sourceLabel = new brls::Label();
                sourceLabel->setText(src.name + " (" + getLanguageDisplayName(src.lang) + ")");
                sourceLabel->setFontSize(14);
                sourceLabel->setGrow(1.0f);
                sourceBox->addView(sourceLabel);

                auto* arrowLabel = new brls::Label();
                arrowLabel->setText(">");
                arrowLabel->setFontSize(14);
                arrowLabel->setTextColor(nvgRGB(150, 150, 150));
                sourceBox->addView(arrowLabel);

                sourceBox->registerClickAction([this, src, dialog](brls::View*) {
                    dialog->dismiss();
                    brls::sync([this, src]() {
                        showSourcePreferencesDialog(src);
                    });
                    return true;
                });
                sourceBox->addGestureRecognizer(new brls::TapGestureRecognizer(sourceBox));

                contentBox->addView(sourceBox);
            }

            dialog->addView(contentBox);
            dialog->addButton("Cancel", []() {});
            dialog->open();
        });
    });
}

void ExtensionsTab::showSourcePreferencesDialog(const Source& source) {
    brls::Logger::info("Opening preferences for source: {} (id: {})", source.name, source.id);

    brls::async([this, source]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<SourcePreference> preferences;

        bool success = client.fetchSourcePreferences(source.id, preferences);

        brls::sync([this, success, source, preferences]() {
            if (!success) {
                brls::Application::notify("Failed to load preferences");
                return;
            }

            if (preferences.empty()) {
                brls::Application::notify("No preferences available");
                return;
            }

            // Create preferences dialog
            auto* dialog = new brls::Dialog("Source Settings");

            auto* scrollFrame = new brls::ScrollingFrame();
            scrollFrame->setMinHeight(300);
            scrollFrame->setMaxHeight(450);

            auto* contentBox = new brls::Box();
            contentBox->setAxis(brls::Axis::COLUMN);
            contentBox->setPadding(10, 20, 10, 20);

            auto* titleLabel = new brls::Label();
            titleLabel->setText(source.name + " Settings");
            titleLabel->setFontSize(18);
            titleLabel->setMarginBottom(15);
            contentBox->addView(titleLabel);

            // Add each preference
            int position = 0;
            for (const auto& pref : preferences) {
                if (!pref.visible) {
                    position++;
                    continue;
                }

                auto* prefBox = new brls::Box();
                prefBox->setAxis(brls::Axis::ROW);
                prefBox->setAlignItems(brls::AlignItems::CENTER);
                prefBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
                prefBox->setFocusable(true);
                prefBox->setPadding(10, 15, 10, 15);
                prefBox->setMarginBottom(5);
                prefBox->setCornerRadius(4);
                prefBox->setBackgroundColor(nvgRGBA(50, 50, 50, 200));

                // Left side: title and summary
                auto* textBox = new brls::Box();
                textBox->setAxis(brls::Axis::COLUMN);
                textBox->setGrow(1.0f);
                textBox->setShrink(1.0f);

                auto* titleLbl = new brls::Label();
                titleLbl->setText(pref.title);
                titleLbl->setFontSize(14);
                textBox->addView(titleLbl);

                if (!pref.summary.empty()) {
                    auto* summaryLbl = new brls::Label();
                    summaryLbl->setText(pref.summary);
                    summaryLbl->setFontSize(10);
                    summaryLbl->setTextColor(nvgRGB(150, 150, 150));
                    textBox->addView(summaryLbl);
                }

                prefBox->addView(textBox);

                // Right side: value/control based on type
                int currentPosition = position;
                int64_t sourceId = source.id;

                switch (pref.type) {
                    case SourcePreferenceType::SWITCH:
                    case SourcePreferenceType::CHECKBOX: {
                        auto* valueLabel = new brls::Label();
                        valueLabel->setText(pref.currentValue ? "ON" : "OFF");
                        valueLabel->setFontSize(12);
                        valueLabel->setTextColor(pref.currentValue ? nvgRGB(0, 200, 150) : nvgRGB(150, 150, 150));
                        prefBox->addView(valueLabel);

                        // Toggle on click
                        prefBox->registerClickAction([this, currentPosition, sourceId, pref, source](brls::View*) {
                            SourcePreferenceChange change;
                            change.position = currentPosition;
                            if (pref.type == SourcePreferenceType::SWITCH) {
                                change.hasSwitchState = true;
                                change.switchState = !pref.currentValue;
                            } else {
                                change.hasCheckBoxState = true;
                                change.checkBoxState = !pref.currentValue;
                            }

                            brls::async([this, sourceId, change, source]() {
                                SuwayomiClient& client = SuwayomiClient::getInstance();
                                bool success = client.updateSourcePreference(sourceId, change);
                                brls::sync([this, success, source]() {
                                    if (success) {
                                        brls::Application::notify("Setting updated");
                                        // Refresh dialog
                                        showSourcePreferencesDialog(source);
                                    } else {
                                        brls::Application::notify("Failed to update setting");
                                    }
                                });
                            });
                            return true;
                        });
                        break;
                    }
                    case SourcePreferenceType::EDIT_TEXT: {
                        auto* valueLabel = new brls::Label();
                        std::string displayText = pref.currentText.empty() ? "(empty)" : pref.currentText;
                        if (displayText.length() > 20) {
                            displayText = displayText.substr(0, 17) + "...";
                        }
                        valueLabel->setText(displayText);
                        valueLabel->setFontSize(12);
                        valueLabel->setTextColor(nvgRGB(150, 200, 255));
                        prefBox->addView(valueLabel);

                        // Open IME on click
                        prefBox->registerClickAction([this, currentPosition, sourceId, pref, source](brls::View*) {
                            std::string dialogTitle = pref.dialogTitle.empty() ? pref.title : pref.dialogTitle;
                            brls::Application::getImeManager()->openForText([this, currentPosition, sourceId, source](std::string text) {
                                SourcePreferenceChange change;
                                change.position = currentPosition;
                                change.hasEditTextState = true;
                                change.editTextState = text;

                                brls::async([this, sourceId, change, source]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    bool success = client.updateSourcePreference(sourceId, change);
                                    brls::sync([this, success, source]() {
                                        if (success) {
                                            brls::Application::notify("Setting updated");
                                            showSourcePreferencesDialog(source);
                                        } else {
                                            brls::Application::notify("Failed to update setting");
                                        }
                                    });
                                });
                            }, dialogTitle, pref.currentText, 256, "");
                            return true;
                        });
                        break;
                    }
                    case SourcePreferenceType::LIST: {
                        // Find current entry name
                        std::string currentEntry = pref.selectedValue;
                        for (size_t i = 0; i < pref.entryValues.size(); i++) {
                            if (pref.entryValues[i] == pref.selectedValue && i < pref.entries.size()) {
                                currentEntry = pref.entries[i];
                                break;
                            }
                        }

                        auto* valueLabel = new brls::Label();
                        if (currentEntry.length() > 20) {
                            currentEntry = currentEntry.substr(0, 17) + "...";
                        }
                        valueLabel->setText(currentEntry);
                        valueLabel->setFontSize(12);
                        valueLabel->setTextColor(nvgRGB(255, 200, 100));
                        prefBox->addView(valueLabel);

                        // Open selection dialog on click
                        prefBox->registerClickAction([this, currentPosition, sourceId, pref, source](brls::View*) {
                            auto* listDialog = new brls::Dialog(pref.title);

                            auto* listBox = new brls::Box();
                            listBox->setAxis(brls::Axis::COLUMN);
                            listBox->setPadding(10, 20, 10, 20);

                            for (size_t i = 0; i < pref.entries.size() && i < pref.entryValues.size(); i++) {
                                auto* entryBox = new brls::Box();
                                entryBox->setFocusable(true);
                                entryBox->setPadding(10, 15, 10, 15);
                                entryBox->setMarginBottom(3);
                                entryBox->setCornerRadius(4);

                                bool isSelected = (pref.entryValues[i] == pref.selectedValue);
                                entryBox->setBackgroundColor(isSelected ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(60, 60, 60, 200));

                                auto* entryLabel = new brls::Label();
                                entryLabel->setText(pref.entries[i]);
                                entryLabel->setFontSize(14);
                                entryBox->addView(entryLabel);

                                std::string entryValue = pref.entryValues[i];
                                entryBox->registerClickAction([this, listDialog, currentPosition, sourceId, entryValue, source](brls::View*) {
                                    listDialog->dismiss();

                                    SourcePreferenceChange change;
                                    change.position = currentPosition;
                                    change.hasListState = true;
                                    change.listState = entryValue;

                                    brls::async([this, sourceId, change, source]() {
                                        SuwayomiClient& client = SuwayomiClient::getInstance();
                                        bool success = client.updateSourcePreference(sourceId, change);
                                        brls::sync([this, success, source]() {
                                            if (success) {
                                                brls::Application::notify("Setting updated");
                                                showSourcePreferencesDialog(source);
                                            } else {
                                                brls::Application::notify("Failed to update setting");
                                            }
                                        });
                                    });
                                    return true;
                                });
                                entryBox->addGestureRecognizer(new brls::TapGestureRecognizer(entryBox));

                                listBox->addView(entryBox);
                            }

                            listDialog->addView(listBox);
                            listDialog->addButton("Cancel", []() {});
                            listDialog->open();
                            return true;
                        });
                        break;
                    }
                    case SourcePreferenceType::MULTI_SELECT_LIST: {
                        auto* valueLabel = new brls::Label();
                        std::string selectedStr = std::to_string(pref.selectedValues.size()) + " selected";
                        valueLabel->setText(selectedStr);
                        valueLabel->setFontSize(12);
                        valueLabel->setTextColor(nvgRGB(200, 150, 255));
                        prefBox->addView(valueLabel);

                        // Open multi-select dialog on click
                        prefBox->registerClickAction([this, currentPosition, sourceId, pref, source](brls::View*) {
                            // Create a shared copy of selected values (shared_ptr for safe lambda capture)
                            auto selectedCopy = std::make_shared<std::vector<std::string>>(pref.selectedValues);

                            auto* multiDialog = new brls::Dialog(pref.title);

                            auto* multiBox = new brls::Box();
                            multiBox->setAxis(brls::Axis::COLUMN);
                            multiBox->setPadding(10, 20, 10, 20);

                            for (size_t i = 0; i < pref.entries.size() && i < pref.entryValues.size(); i++) {
                                auto* entryBox = new brls::Box();
                                entryBox->setFocusable(true);
                                entryBox->setPadding(10, 15, 10, 15);
                                entryBox->setMarginBottom(3);
                                entryBox->setCornerRadius(4);

                                bool isSelected = std::find(selectedCopy->begin(), selectedCopy->end(), pref.entryValues[i]) != selectedCopy->end();
                                entryBox->setBackgroundColor(isSelected ? nvgRGBA(0, 100, 80, 200) : nvgRGBA(60, 60, 60, 200));

                                auto* checkLabel = new brls::Label();
                                checkLabel->setText(isSelected ? "[X] " : "[ ] ");
                                checkLabel->setFontSize(14);
                                entryBox->addView(checkLabel);

                                auto* entryLabel = new brls::Label();
                                entryLabel->setText(pref.entries[i]);
                                entryLabel->setFontSize(14);
                                entryBox->addView(entryLabel);

                                std::string entryValue = pref.entryValues[i];
                                entryBox->registerClickAction([entryBox, checkLabel, entryValue, selectedCopy](brls::View*) {
                                    auto it = std::find(selectedCopy->begin(), selectedCopy->end(), entryValue);
                                    if (it != selectedCopy->end()) {
                                        selectedCopy->erase(it);
                                        entryBox->setBackgroundColor(nvgRGBA(60, 60, 60, 200));
                                        checkLabel->setText("[ ] ");
                                    } else {
                                        selectedCopy->push_back(entryValue);
                                        entryBox->setBackgroundColor(nvgRGBA(0, 100, 80, 200));
                                        checkLabel->setText("[X] ");
                                    }
                                    return true;
                                });
                                entryBox->addGestureRecognizer(new brls::TapGestureRecognizer(entryBox));

                                multiBox->addView(entryBox);
                            }

                            multiDialog->addView(multiBox);
                            multiDialog->addButton("Save", [this, multiDialog, currentPosition, sourceId, selectedCopy, source]() {
                                SourcePreferenceChange change;
                                change.position = currentPosition;
                                change.hasMultiSelectState = true;
                                change.multiSelectState = *selectedCopy;

                                brls::async([this, sourceId, change, source]() {
                                    SuwayomiClient& client = SuwayomiClient::getInstance();
                                    bool success = client.updateSourcePreference(sourceId, change);
                                    brls::sync([this, success, source]() {
                                        if (success) {
                                            brls::Application::notify("Setting updated");
                                            showSourcePreferencesDialog(source);
                                        } else {
                                            brls::Application::notify("Failed to update setting");
                                        }
                                    });
                                });
                            });
                            multiDialog->addButton("Cancel", []() {});
                            multiDialog->open();
                            return true;
                        });
                        break;
                    }
                }

                prefBox->addGestureRecognizer(new brls::TapGestureRecognizer(prefBox));
                contentBox->addView(prefBox);
                position++;
            }

            scrollFrame->setContentView(contentBox);
            dialog->addView(scrollFrame);
            dialog->addButton("Close", []() {});
            dialog->open();
        });
    });
}

void ExtensionsTab::showError(const std::string& message) {
    if (!m_listBox) return;

    // Give focus to the refresh button before clearing views to avoid crash
    // when the currently focused view gets destroyed
    if (m_refreshBox) {
        brls::Application::giveFocus(m_refreshBox);
    }

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

    // Give focus to the refresh button before clearing views to avoid crash
    // when the currently focused view gets destroyed
    if (m_refreshBox) {
        brls::Application::giveFocus(m_refreshBox);
    }

    m_listBox->clearViews();

    auto* loadingLabel = new brls::Label();
    loadingLabel->setText(message);
    loadingLabel->setFontSize(16);
    loadingLabel->setTextColor(nvgRGB(180, 180, 180));
    loadingLabel->setMargins(20, 20, 20, 20);
    m_listBox->addView(loadingLabel);
}

} // namespace vitasuwayomi
