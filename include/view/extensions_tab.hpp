/**
 * VitaSuwayomi - Extensions Tab
 * Manage Suwayomi extensions (install, update, uninstall)
 * Uses RecyclerFrame for stable, efficient list rendering
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"
#include <map>
#include <functional>

namespace vitasuwayomi {

class ExtensionsTab;

class ExtensionsDataSource;

// Custom cell for extension items
class ExtensionCell : public brls::RecyclerCell {
public:
    ExtensionCell();
    static ExtensionCell* create();
    void prepareForReuse() override;

    // Override navigation for settings button
    brls::View* getNextFocus(brls::FocusDirection direction, brls::View* currentView) override;
    brls::View* getDefaultFocus() override;

    // Public members for data binding
    brls::Image* icon = nullptr;
    brls::Label* nameLabel = nullptr;
    brls::Label* detailLabel = nullptr;
    brls::Label* statusLabel = nullptr;
    brls::Box* settingsBtn = nullptr;

    // Track which extension this cell represents
    std::string pkgName;
    bool iconLoaded = false;

    // Track row index for navigation
    int rowIndex = -1;

    // Track if settings button should be preferred focus (for D-pad navigation)
    static bool s_preferSettingsFocus;

    // Static pointers for settings icon navigation
    static brls::RecyclerFrame* s_recycler;
    static ExtensionsDataSource* s_dataSource;
};

// Section header cell
class ExtensionSectionHeader : public brls::RecyclerCell {
public:
    ExtensionSectionHeader();
    static ExtensionSectionHeader* create();

    brls::Label* titleLabel = nullptr;
    brls::Label* countLabel = nullptr;
    brls::Label* arrowLabel = nullptr;
    bool expanded = false;
};

// Row item type for the flat list
struct ExtensionRow {
    enum class Type {
        SectionHeader,      // Updates Available, Installed, Available to Install
        LanguageHeader,     // English, Japanese, etc (under Available)
        ExtensionItem,      // Actual extension
        SearchHeader        // "Clear Search" header when search is active
    };

    Type type;
    std::string sectionId;      // For headers: "updates", "installed", "available"
    std::string languageCode;   // For language headers
    Extension extension;        // For extension items
    int count = 0;              // For headers: item count
    bool expanded = false;      // For collapsible headers
};

// Data source for RecyclerFrame
class ExtensionsDataSource : public brls::RecyclerDataSource {
public:
    ExtensionsDataSource(ExtensionsTab* tab);

    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

    // Rebuild the flat list from current data
    void rebuildRows();

    // Find next/previous row with a settings button (installed extension with configurable sources)
    // Returns -1 if not found
    int findNextSettingsRow(int currentRow, bool searchDown) const;

    // Check if a row has a settings button
    bool rowHasSettingsButton(int row) const;

private:
    ExtensionsTab* m_tab;
    std::vector<ExtensionRow> m_rows;

    void addExtensionRows(const std::vector<Extension>& extensions, bool indent);
};

class ExtensionsTab : public brls::Box {
public:
    ExtensionsTab();

    void onFocusGained() override;

    // Called by data source
    void onExtensionClicked(const Extension& ext);
    void onSectionHeaderClicked(const std::string& sectionId);
    void onLanguageHeaderClicked(const std::string& langCode);
    void onSettingsClicked(const Extension& ext);
    void onSearchHeaderClicked();

    // Getters for data source
    const std::vector<Extension>& getUpdates() const { return m_updates; }
    const std::vector<Extension>& getInstalled() const { return m_installed; }
    const std::vector<Extension>& getUninstalled() const { return m_uninstalled; }
    const std::map<std::string, std::vector<Extension>>& getGroupedByLanguage() const { return m_cachedGrouped; }
    const std::vector<std::string>& getSortedLanguages() const { return m_cachedSortedLanguages; }

    // Section expansion state
    bool isUpdatesExpanded() const { return m_updatesExpanded; }
    bool isInstalledExpanded() const { return m_installedExpanded; }
    bool isAvailableExpanded() const { return m_availableExpanded; }
    bool isLanguageExpanded(const std::string& lang) const;

    void setUpdatesExpanded(bool e) { m_updatesExpanded = e; }
    void setInstalledExpanded(bool e) { m_installedExpanded = e; }
    void setAvailableExpanded(bool e) { m_availableExpanded = e; }
    void setLanguageExpanded(const std::string& lang, bool e) { m_languageExpanded[lang] = e; }

    // Search state (used by data source)
    bool isSearchActive() const { return m_isSearchActive; }
    const std::string& getSearchQuery() const { return m_searchQuery; }

    // Language name helper (used by data source)
    std::string getLanguageDisplayName(const std::string& langCode);

private:
    // Data loading
    void loadExtensionsFast();
    void refreshExtensions();
    void refreshUIFromCache();

    // Search
    void showSearchDialog();
    void clearSearch();
    void showSearchResults();
    void hideSearchResults();

    // Extension operations
    void installExtension(const Extension& ext);
    void updateExtension(const Extension& ext);
    void uninstallExtension(const Extension& ext);
    void showSourceSettings(const Extension& ext);
    void showSourcePreferencesDialog(const Source& source);

    // Helpers
    void showError(const std::string& message);
    void showLoading(const std::string& message);
    std::map<std::string, std::vector<Extension>> groupExtensionsByLanguage(const std::vector<Extension>& extensions);
    std::vector<std::string> getSortedLanguageKeys(const std::map<std::string, std::vector<Extension>>& grouped);

    // Trigger recycler refresh
    void reloadRecycler();

    // Save/restore focus position for expand/collapse
    int getFocusedRowIndex() const;
    void restoreFocusToRow(int rowIndex);

    // UI elements
    brls::Label* m_titleLabel = nullptr;
    brls::RecyclerFrame* m_recycler = nullptr;
    brls::Box* m_refreshBox = nullptr;
    brls::Image* m_refreshIcon = nullptr;
    brls::Image* m_searchIcon = nullptr;

    // Search results (separate recycler)
    brls::RecyclerFrame* m_searchRecycler = nullptr;
    brls::Box* m_searchHeaderBox = nullptr;
    brls::Label* m_searchTitleLabel = nullptr;

    // Search state
    std::string m_searchQuery;
    bool m_isSearchActive = false;

    // Extension data
    std::vector<Extension> m_updates;
    std::vector<Extension> m_installed;
    std::vector<Extension> m_uninstalled;

    // Cache
    std::vector<Extension> m_cachedExtensions;
    std::map<std::string, std::vector<Extension>> m_cachedGrouped;
    std::vector<std::string> m_cachedSortedLanguages;
    bool m_cacheLoaded = false;
    bool m_needsRefresh = false;

    // Section expansion state
    bool m_updatesExpanded = true;
    bool m_installedExpanded = true;
    bool m_availableExpanded = false;
    std::map<std::string, bool> m_languageExpanded;

    // Data source (owned by recycler, but we need to trigger rebuilds)
    ExtensionsDataSource* m_dataSource = nullptr;
};

} // namespace vitasuwayomi
