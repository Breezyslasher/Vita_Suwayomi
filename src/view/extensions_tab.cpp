/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Uses RecyclerFrame for stable, efficient list rendering with automatic view lifecycle management
 */

#include "view/extensions_tab.hpp"
#include "app/suwayomi_client.hpp"
#include "app/application.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>
#include <algorithm>
#include <cctype>

namespace vitasuwayomi {

// ============================================================================
// ExtensionCell Implementation
// ============================================================================

ExtensionCell::ExtensionCell() {
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(8, 12, 8, 12);
    this->setFocusable(true);

    // Left side: icon and info
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::ROW);
    leftBox->setAlignItems(brls::AlignItems::CENTER);
    leftBox->setGrow(1.0f);
    leftBox->setShrink(1.0f);

    // Icon
    icon = new brls::Image();
    icon->setSize(brls::Size(36, 36));
    icon->setMarginRight(12);
    leftBox->addView(icon);

    // Info box
    auto* infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);
    infoBox->setShrink(1.0f);

    nameLabel = new brls::Label();
    nameLabel->setFontSize(14);
    infoBox->addView(nameLabel);

    detailLabel = new brls::Label();
    detailLabel->setFontSize(10);
    detailLabel->setTextColor(nvgRGB(140, 140, 140));
    infoBox->addView(detailLabel);

    leftBox->addView(infoBox);
    this->addView(leftBox);

    // Right side
    auto* rightBox = new brls::Box();
    rightBox->setAxis(brls::Axis::ROW);
    rightBox->setAlignItems(brls::AlignItems::CENTER);

    // Settings button (created but may be hidden)
    settingsBtn = new brls::Box();
    settingsBtn->setFocusable(true);
    settingsBtn->setPadding(6, 6, 6, 6);
    settingsBtn->setCornerRadius(4);
    settingsBtn->setMarginRight(8);
    settingsBtn->setVisibility(brls::Visibility::GONE);

    auto* settingsIcon = new brls::Image();
    settingsIcon->setSize(brls::Size(20, 20));
    settingsIcon->setImageFromFile("app0:resources/icons/options.png");
    settingsBtn->addView(settingsIcon);
    settingsBtn->addGestureRecognizer(new brls::TapGestureRecognizer(settingsBtn));

    rightBox->addView(settingsBtn);

    // Status label
    statusLabel = new brls::Label();
    statusLabel->setFontSize(11);
    statusLabel->setMarginLeft(8);
    rightBox->addView(statusLabel);

    this->addView(rightBox);
    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

ExtensionCell* ExtensionCell::create() {
    return new ExtensionCell();
}

void ExtensionCell::prepareForReuse() {
    brls::RecyclerCell::prepareForReuse();
    pkgName.clear();
    iconLoaded = false;
    icon->clear();
    nameLabel->setText("");
    detailLabel->setText("");
    statusLabel->setText("");
    settingsBtn->setVisibility(brls::Visibility::GONE);
    this->setMarginLeft(0);
}

// ============================================================================
// ExtensionSectionHeader Implementation
// ============================================================================

ExtensionSectionHeader::ExtensionSectionHeader() {
    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    this->setAlignItems(brls::AlignItems::CENTER);
    this->setPadding(12, 15, 12, 15);
    this->setBackgroundColor(nvgRGB(0, 120, 110));  // Teal
    this->setCornerRadius(4);
    this->setFocusable(true);

    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::ROW);
    leftBox->setAlignItems(brls::AlignItems::CENTER);

    arrowLabel = new brls::Label();
    arrowLabel->setFontSize(12);
    arrowLabel->setMarginRight(8);
    leftBox->addView(arrowLabel);

    titleLabel = new brls::Label();
    titleLabel->setFontSize(16);
    leftBox->addView(titleLabel);

    this->addView(leftBox);

    countLabel = new brls::Label();
    countLabel->setFontSize(14);
    countLabel->setTextColor(nvgRGBA(255, 255, 255, 180));
    this->addView(countLabel);

    this->addGestureRecognizer(new brls::TapGestureRecognizer(this));
}

ExtensionSectionHeader* ExtensionSectionHeader::create() {
    return new ExtensionSectionHeader();
}

// ============================================================================
// ExtensionsDataSource Implementation
// ============================================================================

ExtensionsDataSource::ExtensionsDataSource(ExtensionsTab* tab) : m_tab(tab) {
    rebuildRows();
}

int ExtensionsDataSource::numberOfSections(brls::RecyclerFrame* recycler) {
    return 1;
}

int ExtensionsDataSource::numberOfRows(brls::RecyclerFrame* recycler, int section) {
    return static_cast<int>(m_rows.size());
}

float ExtensionsDataSource::heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) {
    if (index.row >= static_cast<int>(m_rows.size())) return 50;

    const auto& row = m_rows[index.row];
    switch (row.type) {
        case ExtensionRow::Type::SectionHeader:
            return 48;
        case ExtensionRow::Type::LanguageHeader:
            return 40;
        case ExtensionRow::Type::ExtensionItem:
            return 56;
    }
    return 50;
}

brls::RecyclerCell* ExtensionsDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) {
    if (index.row >= static_cast<int>(m_rows.size())) return nullptr;

    const auto& row = m_rows[index.row];

    switch (row.type) {
        case ExtensionRow::Type::SectionHeader:
        case ExtensionRow::Type::LanguageHeader: {
            auto* header = dynamic_cast<ExtensionSectionHeader*>(
                recycler->dequeueReusableCell("Header"));
            if (!header) {
                header = ExtensionSectionHeader::create();
                header->reuseIdentifier = "Header";
            }

            header->expanded = row.expanded;
            header->arrowLabel->setText(row.expanded ? "▼" : "▶");
            header->countLabel->setText("(" + std::to_string(row.count) + ")");

            if (row.type == ExtensionRow::Type::SectionHeader) {
                // Main section header (teal)
                if (row.sectionId == "updates") {
                    header->titleLabel->setText("Updates Available");
                } else if (row.sectionId == "installed") {
                    header->titleLabel->setText("Installed");
                } else {
                    header->titleLabel->setText("Available to Install");
                }
                header->setBackgroundColor(nvgRGB(0, 120, 110));
                header->setMarginTop(8);
                header->setMarginLeft(0);
            } else {
                // Language header (dark gray, indented)
                header->titleLabel->setText(m_tab->getLanguageDisplayName(row.languageCode));
                header->setBackgroundColor(nvgRGB(60, 60, 60));
                header->setMarginTop(4);
                header->setMarginLeft(15);
            }

            return header;
        }

        case ExtensionRow::Type::ExtensionItem: {
            auto* cell = dynamic_cast<ExtensionCell*>(
                recycler->dequeueReusableCell("Extension"));
            if (!cell) {
                cell = ExtensionCell::create();
                cell->reuseIdentifier = "Extension";
            }

            const auto& ext = row.extension;
            cell->pkgName = ext.pkgName;
            cell->nameLabel->setText(ext.name);

            // Detail text
            std::string detailText = "v" + ext.versionName;
            if (!ext.installed) {
                detailText = m_tab->getLanguageDisplayName(ext.lang) + " • " + detailText;
            }
            if (ext.isNsfw) {
                detailText += " • 18+";
            }
            cell->detailLabel->setText(detailText);

            // Status and color
            if (ext.installed) {
                if (ext.hasUpdate) {
                    cell->statusLabel->setText("Update");
                    cell->statusLabel->setTextColor(nvgRGB(255, 152, 0));
                } else {
                    cell->statusLabel->setText("Installed");
                    cell->statusLabel->setTextColor(nvgRGB(100, 100, 100));
                }
            } else {
                cell->statusLabel->setText("Install");
                cell->statusLabel->setTextColor(nvgRGB(0, 150, 136));
            }

            // Settings button visibility
            if (ext.installed && ext.hasConfigurableSources) {
                cell->settingsBtn->setVisibility(brls::Visibility::VISIBLE);
                // Set up click handler for settings
                cell->settingsBtn->registerClickAction([this, ext](brls::View*) {
                    brls::sync([this, ext]() {
                        m_tab->onSettingsClicked(ext);
                    });
                    return true;
                });
            } else {
                cell->settingsBtn->setVisibility(brls::Visibility::GONE);
            }

            // Indent for uninstalled items
            cell->setMarginLeft(ext.installed ? 0 : 20);

            // Load icon
            if (!ext.iconUrl.empty() && !cell->iconLoaded) {
                std::string fullUrl = Application::getInstance().getServerUrl() + ext.iconUrl;
                cell->iconLoaded = true;
                ImageLoader::loadAsync(fullUrl, [](brls::Image* img) {}, cell->icon);
            }

            return cell;
        }
    }

    return nullptr;
}

void ExtensionsDataSource::didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) {
    if (indexPath.row >= static_cast<int>(m_rows.size())) return;

    const auto& row = m_rows[indexPath.row];

    switch (row.type) {
        case ExtensionRow::Type::SectionHeader:
            brls::sync([this, row]() {
                m_tab->onSectionHeaderClicked(row.sectionId);
            });
            break;

        case ExtensionRow::Type::LanguageHeader:
            brls::sync([this, row]() {
                m_tab->onLanguageHeaderClicked(row.languageCode);
            });
            break;

        case ExtensionRow::Type::ExtensionItem:
            brls::sync([this, row]() {
                m_tab->onExtensionClicked(row.extension);
            });
            break;
    }
}

void ExtensionsDataSource::rebuildRows() {
    m_rows.clear();

    const auto& updates = m_tab->getUpdates();
    const auto& installed = m_tab->getInstalled();
    const auto& grouped = m_tab->getGroupedByLanguage();
    const auto& sortedLangs = m_tab->getSortedLanguages();

    // Updates section
    if (!updates.empty()) {
        ExtensionRow header;
        header.type = ExtensionRow::Type::SectionHeader;
        header.sectionId = "updates";
        header.count = static_cast<int>(updates.size());
        header.expanded = m_tab->isUpdatesExpanded();
        m_rows.push_back(header);

        if (m_tab->isUpdatesExpanded()) {
            for (const auto& ext : updates) {
                ExtensionRow item;
                item.type = ExtensionRow::Type::ExtensionItem;
                item.extension = ext;
                m_rows.push_back(item);
            }
        }
    }

    // Installed section
    if (!installed.empty()) {
        ExtensionRow header;
        header.type = ExtensionRow::Type::SectionHeader;
        header.sectionId = "installed";
        header.count = static_cast<int>(installed.size());
        header.expanded = m_tab->isInstalledExpanded();
        m_rows.push_back(header);

        if (m_tab->isInstalledExpanded()) {
            for (const auto& ext : installed) {
                ExtensionRow item;
                item.type = ExtensionRow::Type::ExtensionItem;
                item.extension = ext;
                m_rows.push_back(item);
            }
        }
    }

    // Available section with language groups
    int totalAvailable = 0;
    for (const auto& pair : grouped) {
        totalAvailable += static_cast<int>(pair.second.size());
    }

    if (totalAvailable > 0) {
        ExtensionRow header;
        header.type = ExtensionRow::Type::SectionHeader;
        header.sectionId = "available";
        header.count = totalAvailable;
        header.expanded = m_tab->isAvailableExpanded();
        m_rows.push_back(header);

        if (m_tab->isAvailableExpanded()) {
            for (const auto& lang : sortedLangs) {
                auto it = grouped.find(lang);
                if (it == grouped.end() || it->second.empty()) continue;

                ExtensionRow langHeader;
                langHeader.type = ExtensionRow::Type::LanguageHeader;
                langHeader.languageCode = lang;
                langHeader.count = static_cast<int>(it->second.size());
                langHeader.expanded = m_tab->isLanguageExpanded(lang);
                m_rows.push_back(langHeader);

                if (m_tab->isLanguageExpanded(lang)) {
                    for (const auto& ext : it->second) {
                        ExtensionRow item;
                        item.type = ExtensionRow::Type::ExtensionItem;
                        item.extension = ext;
                        m_rows.push_back(item);
                    }
                }
            }
        }
    }

    brls::Logger::debug("ExtensionsDataSource: Rebuilt with {} rows", m_rows.size());
}

// ============================================================================
// ExtensionsTab Implementation
// ============================================================================

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

    if (!langCode.empty()) {
        std::string result = langCode;
        result[0] = std::toupper(result[0]);
        return result;
    }

    return "Unknown";
}

bool ExtensionsTab::isLanguageExpanded(const std::string& lang) const {
    auto it = m_languageExpanded.find(lang);
    return it != m_languageExpanded.end() && it->second;
}

ExtensionsTab::ExtensionsTab() {
    this->setAxis(brls::Axis::COLUMN);
    this->setPadding(20, 30, 20, 30);

    // Header
    auto* headerBox = new brls::Box();
    headerBox->setAxis(brls::Axis::ROW);
    headerBox->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    headerBox->setAlignItems(brls::AlignItems::CENTER);
    headerBox->setMarginBottom(15);

    m_titleLabel = new brls::Label();
    m_titleLabel->setText("Extensions");
    m_titleLabel->setFontSize(24);
    m_titleLabel->setGrow(1.0f);
    headerBox->addView(m_titleLabel);

    // Buttons
    auto* buttonBox = new brls::Box();
    buttonBox->setAxis(brls::Axis::ROW);
    buttonBox->setAlignItems(brls::AlignItems::FLEX_END);

    // Search button
    auto* searchContainer = new brls::Box();
    searchContainer->setAxis(brls::Axis::COLUMN);
    searchContainer->setAlignItems(brls::AlignItems::CENTER);
    searchContainer->setMarginRight(10);

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
        brls::sync([this]() { showSearchDialog(); });
        return true;
    });
    searchBox->addGestureRecognizer(new brls::TapGestureRecognizer(searchBox));
    searchContainer->addView(searchBox);
    buttonBox->addView(searchContainer);

    // Refresh button
    auto* refreshContainer = new brls::Box();
    refreshContainer->setAxis(brls::Axis::COLUMN);
    refreshContainer->setAlignItems(brls::AlignItems::CENTER);

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
        brls::sync([this]() { refreshExtensions(); });
        return true;
    });
    m_refreshBox->addGestureRecognizer(new brls::TapGestureRecognizer(m_refreshBox));
    refreshContainer->addView(m_refreshBox);
    buttonBox->addView(refreshContainer);

    headerBox->addView(buttonBox);
    this->addView(headerBox);

    // Register hotkeys
    this->registerAction("Search", brls::ControllerButton::BUTTON_START, [this](brls::View*) {
        brls::sync([this]() { showSearchDialog(); });
        return true;
    });

    this->registerAction("Refresh", brls::ControllerButton::BUTTON_Y, [this](brls::View*) {
        brls::sync([this]() { refreshExtensions(); });
        return true;
    });

    // RecyclerFrame for main list
    m_recycler = new brls::RecyclerFrame();
    m_recycler->setGrow(1.0f);
    m_recycler->estimatedRowHeight = 50;
    m_recycler->registerCell("Extension", []() { return ExtensionCell::create(); });
    m_recycler->registerCell("Header", []() { return ExtensionSectionHeader::create(); });
    this->addView(m_recycler);

    // Load extensions
    loadExtensionsFast();
}

void ExtensionsTab::onFocusGained() {
    brls::Box::onFocusGained();

    if (m_needsRefresh) {
        m_needsRefresh = false;
        brls::Logger::debug("ExtensionsTab: Refreshing UI after extension operation");
        refreshUIFromCache();
    }
}

void ExtensionsTab::loadExtensionsFast() {
    brls::Logger::debug("Loading extensions list (fast mode)...");

    showLoading("Loading extensions...");

    brls::async([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        const AppSettings& settings = Application::getInstance().getSettings();

        std::vector<Extension> allExtensions;
        if (m_cacheLoaded && !m_cachedExtensions.empty()) {
            allExtensions = m_cachedExtensions;
            brls::Logger::debug("Using cached extensions ({} total)", allExtensions.size());
        } else {
            bool success = client.fetchExtensionList(allExtensions);
            if (!success) {
                brls::sync([this]() { showError("Failed to load extensions"); });
                return;
            }
            m_cachedExtensions = allExtensions;
            m_cacheLoaded = true;
            brls::Logger::debug("Fetched and cached {} extensions", allExtensions.size());
        }

        std::set<std::string> filterLanguages = settings.enabledSourceLanguages;
        if (filterLanguages.empty()) {
            filterLanguages.insert("en");
        }

        m_updates.clear();
        m_installed.clear();
        m_uninstalled.clear();

        for (const auto& ext : allExtensions) {
            if (ext.installed) {
                if (ext.hasUpdate) {
                    m_updates.push_back(ext);
                } else {
                    m_installed.push_back(ext);
                }
            } else {
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
                    m_uninstalled.push_back(ext);
                }
            }
        }

        // Sort lists alphabetically
        auto sortByName = [](const Extension& a, const Extension& b) {
            return a.name < b.name;
        };
        std::sort(m_updates.begin(), m_updates.end(), sortByName);
        std::sort(m_installed.begin(), m_installed.end(), sortByName);
        std::sort(m_uninstalled.begin(), m_uninstalled.end(), sortByName);

        // Group uninstalled by language
        m_cachedGrouped = groupExtensionsByLanguage(m_uninstalled);
        m_cachedSortedLanguages = getSortedLanguageKeys(m_cachedGrouped);

        brls::Logger::debug("Fast mode: {} updates, {} installed, {} uninstalled",
            m_updates.size(), m_installed.size(), m_uninstalled.size());

        brls::sync([this]() {
            reloadRecycler();
        });
    });
}

void ExtensionsTab::refreshExtensions() {
    brls::Logger::info("Refreshing extensions from server...");

    m_cachedExtensions.clear();
    m_cacheLoaded = false;

    brls::Application::notify("Refreshing extensions...");
    loadExtensionsFast();
}

void ExtensionsTab::refreshUIFromCache() {
    if (!m_cacheLoaded) {
        loadExtensionsFast();
        return;
    }

    const AppSettings& settings = Application::getInstance().getSettings();
    std::set<std::string> filterLanguages = settings.enabledSourceLanguages;
    if (filterLanguages.empty()) {
        filterLanguages.insert("en");
    }

    m_updates.clear();
    m_installed.clear();
    m_uninstalled.clear();

    for (const auto& ext : m_cachedExtensions) {
        if (ext.installed) {
            if (ext.hasUpdate) {
                m_updates.push_back(ext);
            } else {
                m_installed.push_back(ext);
            }
        } else {
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
                m_uninstalled.push_back(ext);
            }
        }
    }

    auto sortByName = [](const Extension& a, const Extension& b) {
        return a.name < b.name;
    };
    std::sort(m_updates.begin(), m_updates.end(), sortByName);
    std::sort(m_installed.begin(), m_installed.end(), sortByName);
    std::sort(m_uninstalled.begin(), m_uninstalled.end(), sortByName);

    m_cachedGrouped = groupExtensionsByLanguage(m_uninstalled);
    m_cachedSortedLanguages = getSortedLanguageKeys(m_cachedGrouped);

    reloadRecycler();
}

void ExtensionsTab::reloadRecycler() {
    if (!m_dataSource) {
        m_dataSource = new ExtensionsDataSource(this);
        m_recycler->setDataSource(m_dataSource);
    } else {
        m_dataSource->rebuildRows();
        m_recycler->reloadData();
    }
}

void ExtensionsTab::showLoading(const std::string& message) {
    // For now just log - recycler handles its own state
    brls::Logger::debug("Loading: {}", message);
}

void ExtensionsTab::showError(const std::string& message) {
    brls::Application::notify(message);
}

std::map<std::string, std::vector<Extension>> ExtensionsTab::groupExtensionsByLanguage(
    const std::vector<Extension>& extensions) {
    std::map<std::string, std::vector<Extension>> grouped;
    for (const auto& ext : extensions) {
        grouped[ext.lang].push_back(ext);
    }
    // Sort each group alphabetically
    for (auto& pair : grouped) {
        std::sort(pair.second.begin(), pair.second.end(),
            [](const Extension& a, const Extension& b) { return a.name < b.name; });
    }
    return grouped;
}

std::vector<std::string> ExtensionsTab::getSortedLanguageKeys(
    const std::map<std::string, std::vector<Extension>>& grouped) {
    std::vector<std::string> keys;
    for (const auto& pair : grouped) {
        keys.push_back(pair.first);
    }
    // Sort by display name
    std::sort(keys.begin(), keys.end(), [this](const std::string& a, const std::string& b) {
        return getLanguageDisplayName(a) < getLanguageDisplayName(b);
    });
    return keys;
}

// ============================================================================
// Click Handlers
// ============================================================================

void ExtensionsTab::onSectionHeaderClicked(const std::string& sectionId) {
    if (sectionId == "updates") {
        m_updatesExpanded = !m_updatesExpanded;
    } else if (sectionId == "installed") {
        m_installedExpanded = !m_installedExpanded;
    } else if (sectionId == "available") {
        m_availableExpanded = !m_availableExpanded;
    }
    reloadRecycler();
}

void ExtensionsTab::onLanguageHeaderClicked(const std::string& langCode) {
    m_languageExpanded[langCode] = !isLanguageExpanded(langCode);
    reloadRecycler();
}

void ExtensionsTab::onExtensionClicked(const Extension& ext) {
    if (ext.installed) {
        if (ext.hasUpdate) {
            updateExtension(ext);
        } else {
            uninstallExtension(ext);
        }
    } else {
        installExtension(ext);
    }
}

void ExtensionsTab::onSettingsClicked(const Extension& ext) {
    showSourceSettings(ext);
}

// ============================================================================
// Extension Operations
// ============================================================================

void ExtensionsTab::installExtension(const Extension& ext) {
    brls::Logger::info("Installing extension: {}", ext.name);
    brls::Application::notify("Installing " + ext.name + "...");

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        int maxRetries = 3;
        for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
            brls::Logger::info("Installing extension {} (attempt {}/{})", ext.pkgName, attempt, maxRetries);
            success = client.installExtension(ext.pkgName);
            if (!success && attempt < maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (success) {
            // Update cache
            for (auto& cachedExt : m_cachedExtensions) {
                if (cachedExt.pkgName == ext.pkgName) {
                    cachedExt.installed = true;
                    cachedExt.hasUpdate = false;
                    break;
                }
            }

            brls::sync([this, ext]() {
                brls::Application::notify(ext.name + " installed");
                refreshUIFromCache();
            });
        } else {
            brls::sync([this, ext]() {
                brls::Application::notify("Failed to install " + ext.name);
            });
        }
    });
}

void ExtensionsTab::updateExtension(const Extension& ext) {
    brls::Logger::info("Updating extension: {}", ext.name);
    brls::Application::notify("Updating " + ext.name + "...");

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        int maxRetries = 3;
        for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
            brls::Logger::info("Updating extension {} (attempt {}/{})", ext.pkgName, attempt, maxRetries);
            success = client.updateExtension(ext.pkgName);
            if (!success && attempt < maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (success) {
            for (auto& cachedExt : m_cachedExtensions) {
                if (cachedExt.pkgName == ext.pkgName) {
                    cachedExt.hasUpdate = false;
                    break;
                }
            }

            brls::sync([this, ext]() {
                brls::Application::notify(ext.name + " updated");
                refreshUIFromCache();
            });
        } else {
            brls::sync([this, ext]() {
                brls::Application::notify("Failed to update " + ext.name);
            });
        }
    });
}

void ExtensionsTab::uninstallExtension(const Extension& ext) {
    brls::Logger::info("Uninstalling extension: {}", ext.name);
    brls::Application::notify("Uninstalling " + ext.name + "...");

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool success = false;
        int maxRetries = 3;
        for (int attempt = 1; attempt <= maxRetries && !success; attempt++) {
            brls::Logger::info("Uninstalling extension {} (attempt {}/{})", ext.pkgName, attempt, maxRetries);
            success = client.uninstallExtension(ext.pkgName);
            if (!success && attempt < maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }

        if (success) {
            for (auto& cachedExt : m_cachedExtensions) {
                if (cachedExt.pkgName == ext.pkgName) {
                    cachedExt.installed = false;
                    cachedExt.hasUpdate = false;
                    break;
                }
            }

            brls::sync([this, ext]() {
                brls::Application::notify(ext.name + " uninstalled");
                refreshUIFromCache();
            });
        } else {
            brls::sync([this, ext]() {
                brls::Application::notify("Failed to uninstall " + ext.name);
            });
        }
    });
}

void ExtensionsTab::showSourceSettings(const Extension& ext) {
    brls::Logger::info("Opening settings for extension: {}", ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<Source> sources;
        bool success = client.fetchExtensionSources(ext.pkgName, sources);

        if (!success || sources.empty()) {
            brls::sync([this]() {
                brls::Application::notify("No configurable sources found");
            });
            return;
        }

        brls::sync([this, sources, ext]() {
            if (sources.size() == 1) {
                showSourcePreferencesDialog(sources[0]);
            } else {
                // Show source selection dialog
                auto* dialog = new brls::Dialog("Select Source");

                auto* list = new brls::Box();
                list->setAxis(brls::Axis::COLUMN);
                list->setPadding(10, 15, 10, 15);

                for (const auto& source : sources) {
                    auto* item = new brls::Box();
                    item->setAxis(brls::Axis::ROW);
                    item->setFocusable(true);
                    item->setPadding(10, 10, 10, 10);
                    item->setMarginBottom(5);
                    item->setCornerRadius(4);
                    item->setBackgroundColor(nvgRGBA(60, 60, 60, 255));

                    auto* label = new brls::Label();
                    label->setText(source.name);
                    label->setFontSize(14);
                    item->addView(label);

                    item->registerClickAction([this, dialog, source](brls::View*) {
                        dialog->dismiss();
                        brls::sync([this, source]() {
                            showSourcePreferencesDialog(source);
                        });
                        return true;
                    });
                    item->addGestureRecognizer(new brls::TapGestureRecognizer(item));

                    list->addView(item);
                }

                dialog->addView(list);
                dialog->addButton("Cancel", []() {});
                dialog->open();
            }
        });
    });
}

void ExtensionsTab::showSourcePreferencesDialog(const Source& source) {
    brls::Logger::info("Fetching preferences for source: {}", source.name);

    brls::async([this, source]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        std::vector<SourcePreference> prefs;
        bool success = client.fetchSourcePreferences(source.id, prefs);

        if (!success) {
            brls::sync([this]() {
                brls::Application::notify("Failed to load source settings");
            });
            return;
        }

        if (prefs.empty()) {
            brls::sync([this, source]() {
                brls::Application::notify(source.name + " has no configurable settings");
            });
            return;
        }

        brls::sync([this, source, prefs]() {
            auto* dialog = new brls::Dialog(source.name + " Settings");

            auto* scrollFrame = new brls::ScrollingFrame();
            scrollFrame->setHeight(400);

            auto* list = new brls::Box();
            list->setAxis(brls::Axis::COLUMN);
            list->setPadding(10, 15, 10, 15);

            for (const auto& pref : prefs) {
                auto* prefBox = new brls::Box();
                prefBox->setAxis(brls::Axis::COLUMN);
                prefBox->setMarginBottom(15);

                auto* titleLabel = new brls::Label();
                titleLabel->setText(pref.title.empty() ? pref.key : pref.title);
                titleLabel->setFontSize(14);
                prefBox->addView(titleLabel);

                if (!pref.summary.empty()) {
                    auto* summaryLabel = new brls::Label();
                    summaryLabel->setText(pref.summary);
                    summaryLabel->setFontSize(11);
                    summaryLabel->setTextColor(nvgRGB(140, 140, 140));
                    prefBox->addView(summaryLabel);
                }

                // Value display
                auto* valueBox = new brls::Box();
                valueBox->setAxis(brls::Axis::ROW);
                valueBox->setFocusable(true);
                valueBox->setPadding(8, 10, 8, 10);
                valueBox->setMarginTop(5);
                valueBox->setCornerRadius(4);
                valueBox->setBackgroundColor(nvgRGBA(60, 60, 60, 255));

                auto* valueLabel = new brls::Label();
                valueLabel->setFontSize(13);

                if (pref.type == "CheckBoxPreference" || pref.type == "SwitchPreferenceCompat") {
                    valueLabel->setText(pref.currentValue == "true" ? "Enabled" : "Disabled");
                } else if (pref.type == "ListPreference") {
                    valueLabel->setText(pref.currentValue);
                } else {
                    valueLabel->setText(pref.currentValue);
                }

                valueBox->addView(valueLabel);
                valueBox->addGestureRecognizer(new brls::TapGestureRecognizer(valueBox));

                prefBox->addView(valueBox);
                list->addView(prefBox);
            }

            scrollFrame->setContentView(list);
            dialog->addView(scrollFrame);
            dialog->addButton("Close", []() {});
            dialog->open();
        });
    });
}

// ============================================================================
// Search
// ============================================================================

void ExtensionsTab::showSearchDialog() {
    brls::Swkbd::openForText([this](std::string text) {
        if (text.empty()) {
            clearSearch();
            return;
        }

        m_searchQuery = text;
        std::transform(m_searchQuery.begin(), m_searchQuery.end(), m_searchQuery.begin(), ::tolower);
        m_isSearchActive = true;

        brls::Application::notify("Searching for: " + text);
        showSearchResults();
    }, "Search Extensions", "", 64, "", 0, "Search", "");
}

void ExtensionsTab::clearSearch() {
    m_searchQuery.clear();
    m_isSearchActive = false;
}

void ExtensionsTab::showSearchResults() {
    // For now, just filter and reload
    // A more complete implementation would use a separate data source
    refreshUIFromCache();
}

void ExtensionsTab::hideSearchResults() {
    clearSearch();
    refreshUIFromCache();
}

} // namespace vitasuwayomi
