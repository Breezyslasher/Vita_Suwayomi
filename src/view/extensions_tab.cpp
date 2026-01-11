/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 */

#include "view/extensions_tab.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/image_loader.hpp"

#include <borealis.hpp>

namespace vitasuwayomi {

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
    populateList(m_installed);
}

void ExtensionsTab::showAvailable() {
    m_currentView = ViewMode::AVAILABLE;
    updateButtonStyles();
    populateList(m_available);
}

void ExtensionsTab::showUpdates() {
    m_currentView = ViewMode::UPDATES;
    updateButtonStyles();
    populateList(m_updates);
}

void ExtensionsTab::updateButtonStyles() {
    // Update button styles based on current view
    if (m_installedBtn) {
        m_installedBtn->setStyle(m_currentView == ViewMode::INSTALLED ?
            brls::ButtonStyle::PRIMARY : brls::ButtonStyle::BORDERED);
    }
    if (m_availableBtn) {
        m_availableBtn->setStyle(m_currentView == ViewMode::AVAILABLE ?
            brls::ButtonStyle::PRIMARY : brls::ButtonStyle::BORDERED);
    }
    if (m_updatesBtn) {
        m_updatesBtn->setStyle(m_currentView == ViewMode::UPDATES ?
            brls::ButtonStyle::PRIMARY : brls::ButtonStyle::BORDERED);
    }
}

void ExtensionsTab::populateList(const std::vector<Extension>& extensions) {
    if (!m_listBox) return;

    m_listBox->clearViews();

    if (extensions.empty()) {
        auto* emptyLabel = new brls::Label();
        emptyLabel->setText("No extensions found");
        emptyLabel->setFontSize(16);
        emptyLabel->setMargins(20, 20, 20, 20);
        m_listBox->addView(emptyLabel);
        return;
    }

    for (const auto& ext : extensions) {
        auto* item = createExtensionItem(ext);
        if (item) {
            m_listBox->addView(item);
        }
    }
}

brls::Box* ExtensionsTab::createExtensionItem(const Extension& ext) {
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
    detailLabel->setText(ext.lang + " • v" + ext.versionName);
    detailLabel->setFontSize(11);
    detailLabel->setTextColor(nvgRGB(160, 160, 160));
    infoBox->addView(detailLabel);

    leftBox->addView(infoBox);
    container->addView(leftBox);

    // Right side: action button
    auto* actionBtn = new brls::Button();
    actionBtn->setStyle(brls::ButtonStyle::BORDERED);

    if (ext.installed) {
        if (ext.hasUpdate) {
            actionBtn->addView(new brls::Label("Update"));
            actionBtn->registerClickAction([this, ext](brls::View*) {
                updateExtension(ext);
                return true;
            });
        } else {
            actionBtn->addView(new brls::Label("Uninstall"));
            actionBtn->registerClickAction([this, ext](brls::View*) {
                uninstallExtension(ext);
                return true;
            });
        }
    } else {
        actionBtn->addView(new brls::Label("Install"));
        actionBtn->registerClickAction([this, ext](brls::View*) {
            installExtension(ext);
            return true;
        });
    }

    container->addView(actionBtn);

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
