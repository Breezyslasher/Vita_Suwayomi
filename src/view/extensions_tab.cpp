/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Extensions are grouped by language for better organization
 * Language filtering follows global settings from Settings tab
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
    m_installedBtn = dynamic_cast<brls::Button*>(this->getView("extensions/installed_btn"));
    m_availableBtn = dynamic_cast<brls::Button*>(this->getView("extensions/available_btn"));
    m_updatesBtn = dynamic_cast<brls::Button*>(this->getView("extensions/updates_btn"));
    m_listBox = dynamic_cast<brls::Box*>(this->getView("extensions/list"));

    // Set up button handlers
    if (m_installedBtn) {
        m_installedBtn->registerClickAction([this](brls::View*) {
            showInstalled();
            return true;
        });
    }

    if (m_availableBtn) {
        m_availableBtn->registerClickAction([this](brls::View*) {
            showAvailable();
            return true;
        });
    }

    if (m_updatesBtn) {
        m_updatesBtn->registerClickAction([this](brls::View*) {
            showUpdates();
            return true;
        });
    }

    // Load extensions list
    loadExtensions();
}

void ExtensionsTab::onFocusGained() {
    brls::Box::onFocusGained();
}

void ExtensionsTab::loadExtensions() {
    brls::Logger::debug("Loading extensions list...");

    brls::async([this]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Extension> extensions;

        if (client.fetchExtensionList(extensions)) {
            m_extensions = extensions;

            // Categorize extensions
            m_installed.clear();
            m_available.clear();
            m_updates.clear();

            for (const auto& ext : m_extensions) {
                if (ext.installed) {
                    m_installed.push_back(ext);
                    if (ext.hasUpdate) {
                        m_updates.push_back(ext);
                    }
                } else {
                    m_available.push_back(ext);
                }
            }

            brls::sync([this]() {
                showInstalled();
            });
        } else {
            brls::sync([this]() {
                showError("Failed to load extensions");
            });
        }
    });
}

void ExtensionsTab::showInstalled() {
    m_currentView = ViewMode::INSTALLED;
    updateButtonStyles();
    auto filtered = getFilteredExtensions(m_installed);
    populateListGroupedByLanguage(filtered);
}

void ExtensionsTab::showAvailable() {
    m_currentView = ViewMode::AVAILABLE;
    updateButtonStyles();
    auto filtered = getFilteredExtensions(m_available);
    populateListGroupedByLanguage(filtered);
}

void ExtensionsTab::showUpdates() {
    m_currentView = ViewMode::UPDATES;
    updateButtonStyles();
    auto filtered = getFilteredExtensions(m_updates);
    populateListGroupedByLanguage(filtered);
}

std::vector<Extension> ExtensionsTab::getFilteredExtensions(const std::vector<Extension>& extensions) {
    const AppSettings& settings = Application::getInstance().getSettings();

    // If global settings has enabled languages, use that filter
    if (!settings.enabledSourceLanguages.empty()) {
        std::vector<Extension> filtered;
        for (const auto& ext : extensions) {
            bool languageMatch = false;

            // Check exact match first
            if (settings.enabledSourceLanguages.count(ext.lang) > 0) {
                languageMatch = true;
            }
            // Check if base language is enabled (e.g., "zh" matches "zh-Hans", "zh-Hant")
            else {
                std::string baseLang = ext.lang;
                size_t dashPos = baseLang.find('-');
                if (dashPos != std::string::npos) {
                    baseLang = baseLang.substr(0, dashPos);
                }

                if (settings.enabledSourceLanguages.count(baseLang) > 0) {
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

    // No filtering - return all
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

void ExtensionsTab::updateButtonStyles() {
    // Update button styles to show active state
    // Selected: teal accent, Unselected: dark gray
    NVGcolor selectedColor = nvgRGBA(0, 150, 136, 255);
    NVGcolor normalColor = nvgRGBA(50, 50, 50, 255);

    if (m_installedBtn) {
        m_installedBtn->setBackgroundColor(m_currentView == ViewMode::INSTALLED ?
            selectedColor : normalColor);
    }
    if (m_availableBtn) {
        m_availableBtn->setBackgroundColor(m_currentView == ViewMode::AVAILABLE ?
            selectedColor : normalColor);
    }
    if (m_updatesBtn) {
        m_updatesBtn->setBackgroundColor(m_currentView == ViewMode::UPDATES ?
            selectedColor : normalColor);
    }
}

brls::Box* ExtensionsTab::createLanguageHeader(const std::string& langCode, int extensionCount) {
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(8, 15, 8, 15);
    header->setMarginTop(10);
    header->setMarginBottom(5);
    header->setBackgroundColor(nvgRGBA(40, 40, 40, 255));
    header->setCornerRadius(4);
    header->setFocusable(true);

    // Language name label
    auto* langLabel = new brls::Label();
    langLabel->setText(getLanguageDisplayName(langCode));
    langLabel->setFontSize(16);
    langLabel->setTextColor(nvgRGB(200, 200, 200));
    header->addView(langLabel);

    // Count label
    auto* countLabel = new brls::Label();
    countLabel->setText(std::to_string(extensionCount) + " extension" + (extensionCount != 1 ? "s" : ""));
    countLabel->setFontSize(12);
    countLabel->setTextColor(nvgRGB(140, 140, 140));
    header->addView(countLabel);

    // Add touch gesture support
    header->addGestureRecognizer(new brls::TapGestureRecognizer(header));

    return header;
}

void ExtensionsTab::populateListGroupedByLanguage(const std::vector<Extension>& extensions) {
    if (!m_listBox) return;

    m_listBox->clearViews();

    if (extensions.empty()) {
        auto* emptyLabel = new brls::Label();
        if (m_currentView == ViewMode::AVAILABLE) {
            emptyLabel->setText("No available extensions to install");
        } else if (m_currentView == ViewMode::UPDATES) {
            emptyLabel->setText("All extensions are up to date");
        } else {
            emptyLabel->setText("No extensions installed");
        }
        emptyLabel->setFontSize(16);
        emptyLabel->setMargins(20, 20, 20, 20);
        m_listBox->addView(emptyLabel);
        return;
    }

    // Group extensions by language
    auto grouped = groupExtensionsByLanguage(extensions);

    // Sort languages: "en" first, then alphabetically by display name
    std::vector<std::pair<std::string, std::vector<Extension>>> sortedGroups(grouped.begin(), grouped.end());
    std::sort(sortedGroups.begin(), sortedGroups.end(),
        [this](const auto& a, const auto& b) {
            // English first
            if (a.first == "en" && b.first != "en") return true;
            if (a.first != "en" && b.first == "en") return false;
            // Then by display name
            return getLanguageDisplayName(a.first) < getLanguageDisplayName(b.first);
        });

    // Determine if this is the Available tab (for click-to-install)
    bool clickToInstall = (m_currentView == ViewMode::AVAILABLE);

    // Add each language group
    for (const auto& pair : sortedGroups) {
        const std::string& langCode = pair.first;
        const std::vector<Extension>& langExtensions = pair.second;

        // Add language header
        auto* header = createLanguageHeader(langCode, langExtensions.size());
        m_listBox->addView(header);

        // Add extensions for this language
        for (const auto& ext : langExtensions) {
            auto* item = createExtensionItem(ext, clickToInstall);
            if (item) {
                m_listBox->addView(item);
            }
        }
    }
}

void ExtensionsTab::populateList(const std::vector<Extension>& extensions) {
    // Redirect to grouped version
    populateListGroupedByLanguage(extensions);
}

brls::Box* ExtensionsTab::createExtensionItem(const Extension& ext, bool clickToInstall) {
    auto* container = new brls::Box();
    container->setAxis(brls::Axis::ROW);
    container->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
    container->setAlignItems(brls::AlignItems::CENTER);
    container->setPadding(10, 15, 10, 15);
    container->setMarginBottom(5);
    container->setFocusable(true);

    // Left side: icon and info
    auto* leftBox = new brls::Box();
    leftBox->setAxis(brls::Axis::ROW);
    leftBox->setAlignItems(brls::AlignItems::CENTER);
    leftBox->setGrow(1.0f);

    // Extension icon
    auto* icon = new brls::Image();
    icon->setSize(brls::Size(40, 40));
    icon->setMarginRight(15);
    if (!ext.iconUrl.empty()) {
        ImageLoader::loadAsync(ext.iconUrl, [](brls::Image* img) {
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
    std::string detailText = getLanguageDisplayName(ext.lang) + " • v" + ext.versionName;
    if (ext.isNsfw) {
        detailText += " • 18+";
    }
    detailLabel->setText(detailText);
    detailLabel->setFontSize(11);
    detailLabel->setTextColor(nvgRGB(160, 160, 160));
    infoBox->addView(detailLabel);

    leftBox->addView(infoBox);
    container->addView(leftBox);

    // Right side: action button or status
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
    }

    container->addView(actionBtn);

    // Add touch gesture support
    container->addGestureRecognizer(new brls::TapGestureRecognizer(container));

    // If click-to-install is enabled (Available tab), make the whole row clickable to install
    if (clickToInstall && !ext.installed) {
        container->registerClickAction([this, ext](brls::View*) {
            installExtension(ext);
            return true;
        });
    }

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
