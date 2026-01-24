/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Shows unified list: updates first, then installed, then uninstalled (sorted by language)
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <map>

namespace vitasuwayomi {

class ExtensionsTab : public brls::Box {
public:
    ExtensionsTab();

    void onFocusGained() override;

private:
    // Fast mode: single query, client-side filtering (like Kodi addon)
    void loadExtensionsFast();
    // Standard mode: two queries with server-side filtering
    void loadExtensions();
    // Refresh from server (clears cache and reloads)
    void refreshExtensions();

    void populateUnifiedList();
    brls::Box* createSectionHeader(const std::string& title, int count);
    brls::Box* createLanguageHeader(const std::string& langCode, int extensionCount);
    brls::Box* createExtensionItem(const Extension& ext);
    void installExtension(const Extension& ext);
    void updateExtension(const Extension& ext);
    void uninstallExtension(const Extension& ext);
    void showError(const std::string& message);
    void showLoading(const std::string& message);
    std::vector<Extension> getFilteredExtensions(const std::vector<Extension>& extensions, bool forceLanguageFilter = false);
    std::map<std::string, std::vector<Extension>> groupExtensionsByLanguage(const std::vector<Extension>& extensions);
    std::vector<std::string> getSortedLanguages(const std::map<std::string, std::vector<Extension>>& grouped);
    std::string getLanguageDisplayName(const std::string& langCode);

    brls::Label* m_titleLabel = nullptr;
    brls::Box* m_listBox = nullptr;
    brls::Box* m_buttonBox = nullptr;
    brls::Button* m_refreshBtn = nullptr;

    std::vector<Extension> m_extensions;
    std::vector<Extension> m_updates;
    std::vector<Extension> m_installed;
    std::vector<Extension> m_uninstalled;

    // Cache for fast mode
    std::vector<Extension> m_cachedExtensions;
    bool m_cacheLoaded = false;
};

} // namespace vitasuwayomi
