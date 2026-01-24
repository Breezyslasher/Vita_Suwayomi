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
    // Load from XML
    this->inflateFromXMLRes("xml/tabs/extensions.xml");

    // Get references to UI elements
    m_titleLabel = dynamic_cast<brls::Label*>(this->getView("extensions/title"));
    m_listBox = dynamic_cast<brls::Box*>(this->getView("extensions/list"));

    // Load extensions list
    loadExtensions();
}

void ExtensionsTab::onFocusGained() {
    brls::Box::onFocusGained();
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

    m_listBox->clearViews();

    // Installed extensions: show all (no language filter - user installed them)
    // Uninstalled extensions: already filtered server-side by language
    auto filteredUpdates = m_updates;
    auto filteredInstalled = m_installed;
    auto filteredUninstalled = m_uninstalled;  // Already server-side filtered

    // Sort installed and updates alphabetically by name
    std::sort(filteredUpdates.begin(), filteredUpdates.end(),
        [](const Extension& a, const Extension& b) { return a.name < b.name; });
    std::sort(filteredInstalled.begin(), filteredInstalled.end(),
        [](const Extension& a, const Extension& b) { return a.name < b.name; });

    // Section 1: Updates (if any)
    if (!filteredUpdates.empty()) {
        auto* updateHeader = createSectionHeader("Updates Available", filteredUpdates.size());
        m_listBox->addView(updateHeader);

        for (const auto& ext : filteredUpdates) {
            auto* item = createExtensionItem(ext);
            if (item) {
                m_listBox->addView(item);
            }
        }
    }

    // Section 2: Installed extensions
    if (!filteredInstalled.empty()) {
        auto* installedHeader = createSectionHeader("Installed", filteredInstalled.size());
        m_listBox->addView(installedHeader);

        for (const auto& ext : filteredInstalled) {
            auto* item = createExtensionItem(ext);
            if (item) {
                m_listBox->addView(item);
            }
        }
    }

    // Section 3: Uninstalled extensions (grouped by language, sorted by settings)
    if (!filteredUninstalled.empty()) {
        auto* uninstalledHeader = createSectionHeader("Available to Install", filteredUninstalled.size());
        m_listBox->addView(uninstalledHeader);

        // Group by language
        auto grouped = groupExtensionsByLanguage(filteredUninstalled);

        // Get sorted language list based on settings
        auto sortedLanguages = getSortedLanguages(grouped);

        // Add each language group
        for (const auto& langCode : sortedLanguages) {
            const auto& langExtensions = grouped[langCode];

            // Add language header
            auto* langHeader = createLanguageHeader(langCode, langExtensions.size());
            m_listBox->addView(langHeader);

            // Add extensions for this language
            for (const auto& ext : langExtensions) {
                auto* item = createExtensionItem(ext);
                if (item) {
                    m_listBox->addView(item);
                }
            }
        }
    }

    // Show message if everything is empty
    if (filteredUpdates.empty() && filteredInstalled.empty() && filteredUninstalled.empty()) {
        auto* emptyLabel = new brls::Label();
        emptyLabel->setText("No extensions available");
        emptyLabel->setFontSize(16);
        emptyLabel->setMargins(20, 20, 20, 20);
        m_listBox->addView(emptyLabel);
    }
}

brls::Box* ExtensionsTab::createExtensionItem(const Extension& ext) {
    auto* container = new brls::Box();
    container->setAxis(brls::Axis::ROW);
    container->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    container->setAlignItems(brls::AlignItems::CENTER);
    container->setPadding(8, 12, 8, 12);
    container->setMarginBottom(3);
    container->setMarginLeft(ext.installed ? 0 : 20);  // Indent uninstalled items under language headers
    container->setFocusable(true);

    // Left side: icon and info
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::ROW);
    leftBox->setAlignItems(brls::AlignItems::CENTER);
    leftBox->setGrow(1.0f);

    // Extension icon
    auto* icon = new brls::Image();
    icon->setSize(brls::Size(36, 36));
    icon->setMarginRight(12);
    if (!ext.iconUrl.empty()) {
        std::string fullIconUrl = Application::getInstance().getServerUrl() + ext.iconUrl;
        ImageLoader::loadAsync(fullIconUrl, [](brls::Image* img) {
            brls::Logger::debug("ExtensionsTab: Extension icon loaded");
        }, icon);
    }
    leftBox->addView(icon);

    // Extension info
    auto* infoBox = new brls::Box();
    infoBox->setAxis(brls::Axis::COLUMN);

    auto* nameLabel = new brls::Label();
    nameLabel->setText(ext.name);
    nameLabel->setFontSize(14);
    infoBox->addView(nameLabel);

    auto* detailLabel = new brls::Label();
    std::string detailText = "v" + ext.versionName;
    if (!ext.installed) {
        // Don't show language for installed extensions (they're not grouped by language)
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

    return container;
}

void ExtensionsTab::installExtension(const Extension& ext) {
    brls::Logger::info("Installing extension: {}", ext.name);

    brls::async([this, ext]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        bool success = client.installExtension(ext.pkgName);

        brls::sync([this, success, ext]() {
            if (success) {
                brls::Application::notify("Installed: " + ext.name);
                loadExtensions();
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
                loadExtensions();
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
                loadExtensions();
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

} // namespace vitasuwayomi
