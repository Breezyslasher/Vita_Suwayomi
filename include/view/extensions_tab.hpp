/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Shows unified list: updates first, then installed, then uninstalled (sorted by language)
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <map>
#include <functional>

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
    // Rebuild UI from cached data (safe to call after extension operations)
    void refreshUIFromCache();
    // Show search dialog to filter extensions by name
    void showSearchDialog();
    // Clear search and show all extensions
    void clearSearch();
    // Show search results on a separate page
    void showSearchResults();
    // Go back from search results to main list
    void hideSearchResults();

    void populateUnifiedList();
    brls::Box* createSectionHeader(const std::string& title, int count);
    brls::Box* createLanguageHeader(const std::string& langCode, int extensionCount);
    brls::Box* createExtensionItem(const Extension& ext);
    void installExtension(const Extension& ext);
    void updateExtension(const Extension& ext);
    void uninstallExtension(const Extension& ext);
    void showSourceSettings(const Extension& ext);
    void showSourcePreferencesDialog(const Source& source);
    void showError(const std::string& message);
    void showLoading(const std::string& message);
    std::vector<Extension> getFilteredExtensions(const std::vector<Extension>& extensions, bool forceLanguageFilter = false);
    std::map<std::string, std::vector<Extension>> groupExtensionsByLanguage(const std::vector<Extension>& extensions);
    std::vector<std::string> getSortedLanguages(const std::map<std::string, std::vector<Extension>>& grouped);
    std::string getLanguageDisplayName(const std::string& langCode);

    brls::Label* m_titleLabel = nullptr;
    brls::Box* m_listBox = nullptr;
    brls::Box* m_refreshBox = nullptr;  // Safe focus target during refresh
    brls::Image* m_refreshIcon = nullptr;
    brls::Image* m_searchIcon = nullptr;
    brls::ScrollingFrame* m_scrollFrame = nullptr;

    // Search results view (separate page)
    brls::ScrollingFrame* m_searchResultsFrame = nullptr;
    brls::Box* m_searchResultsBox = nullptr;
    brls::Box* m_searchHeaderBox = nullptr;
    brls::Label* m_searchTitleLabel = nullptr;

    // Search state
    std::string m_searchQuery;
    bool m_isSearchActive = false;

    std::vector<Extension> m_extensions;
    std::vector<Extension> m_updates;
    std::vector<Extension> m_installed;
    std::vector<Extension> m_uninstalled;

    // Cache for fast mode
    std::vector<Extension> m_cachedExtensions;
    bool m_cacheLoaded = false;
    bool m_needsRefresh = false;  // Set after install/update/uninstall, cleared on focus

    // Performance: Batched rendering
    static const int BATCH_SIZE = 8;          // Items per batch
    static const int BATCH_DELAY_MS = 16;     // ~60fps frame time
    static const int ITEMS_PER_PAGE = 30;     // Items to show initially per section
    int m_currentBatchIndex = 0;
    bool m_isPopulating = false;

    // Performance: Track extension items for incremental updates
    struct ExtensionItemInfo {
        brls::Box* container = nullptr;
        brls::Image* icon = nullptr;
        std::string pkgName;
        std::string iconUrl;
        bool iconLoaded = false;
    };
    std::vector<ExtensionItemInfo> m_extensionItems;

    // Performance: Cached grouped data
    std::map<std::string, std::vector<Extension>> m_cachedGrouped;
    std::vector<std::string> m_cachedSortedLanguages;
    bool m_groupingCacheValid = false;

    // Section identifiers for safe lambda captures
    enum class SectionType { Updates, Installed };

    // Collapsible sections state
    struct SectionState {
        bool expanded = false;
        int itemsShown = 0;
        brls::Box* contentBox = nullptr;
        brls::Box* headerBox = nullptr;
    };
    SectionState m_updatesSection;
    SectionState m_installedSection;
    SectionState m_availableSection;
    std::map<std::string, SectionState> m_languageSections;  // For language groups within Available

    // Collapsible section helpers
    brls::Box* createCollapsibleSectionHeader(const std::string& title, int count, SectionType sectionType);
    brls::Box* createAvailableSectionHeader(const std::string& title, int count);  // Special handling for Available section
    brls::Box* createCollapsibleLanguageHeader(const std::string& langCode, int count,
                                                const std::string& langKey);
    void toggleSection(SectionType sectionType);
    void toggleAvailableSection();  // Special handling for Available section
    void toggleLanguageSection(const std::string& langKey);
    void showMoreItems(SectionType sectionType);
    void showMoreLanguageItems(const std::string& langKey);
    brls::Box* createShowMoreButton(SectionType sectionType);
    brls::Box* createLanguageShowMoreButton(const std::string& langKey);

    // Helper to get section state and extensions by type
    SectionState& getSectionState(SectionType type);
    const std::vector<Extension>& getSectionExtensions(SectionType type);

    // Performance: Deferred icon loading
    void loadVisibleIcons();
    void scheduleNextBatch();
    void populateBatch();

    // Performance: Incremental updates
    void updateExtensionItemStatus(const std::string& pkgName, bool installed, bool hasUpdate);
    ExtensionItemInfo* findExtensionItem(const std::string& pkgName);
};

} // namespace vitasuwayomi
