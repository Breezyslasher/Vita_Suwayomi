/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <set>

namespace vitasuwayomi {

class ExtensionsTab : public brls::Box {
public:
    ExtensionsTab();

    void onFocusGained() override;

private:
    enum class ViewMode {
        INSTALLED,
        AVAILABLE,
        UPDATES
    };

    void loadExtensions();
    void showInstalled();
    void showAvailable();
    void showUpdates();
    void updateButtonStyles();
    void populateList(const std::vector<Extension>& extensions);
    brls::Box* createExtensionItem(const Extension& ext);
    void installExtension(const Extension& ext);
    void updateExtension(const Extension& ext);
    void uninstallExtension(const Extension& ext);
    void showError(const std::string& message);
    void updateLanguageFilter();
    void filterByLanguage(const std::string& lang);
    std::vector<Extension> getFilteredExtensions(const std::vector<Extension>& extensions);

    brls::Label* m_titleLabel = nullptr;
    brls::Button* m_installedBtn = nullptr;
    brls::Button* m_availableBtn = nullptr;
    brls::Button* m_updatesBtn = nullptr;
    brls::Button* m_langFilterBtn = nullptr;
    brls::Box* m_listBox = nullptr;

    ViewMode m_currentView = ViewMode::INSTALLED;
    std::vector<Extension> m_extensions;
    std::vector<Extension> m_installed;
    std::vector<Extension> m_available;
    std::vector<Extension> m_updates;

    std::set<std::string> m_availableLanguages;
    std::string m_selectedLanguage = "all";  // "all" means no filter
};

} // namespace vitasuwayomi
