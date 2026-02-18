/**
 * VitaSuwayomi - Library Section Tab
 * Shows manga library content organized by categories
 * Displays user's categories as tabs at the top for easy navigation
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

// Sort modes for library manga
// Note: DEFAULT (-1) uses the default sort mode from settings
// Other values are 0-10 for specific sort modes
enum class LibrarySortMode {
    DEFAULT = -1,           // Use default sort mode from settings
    TITLE_ASC = 0,          // A-Z
    TITLE_DESC = 1,         // Z-A
    UNREAD_DESC = 2,        // Most unread first
    UNREAD_ASC = 3,         // Least unread first
    RECENTLY_ADDED_DESC = 4,// Recently added (newest first)
    RECENTLY_ADDED_ASC = 5, // Recently added (oldest first)
    LAST_READ = 6,          // Last read (most recent first)
    DATE_UPDATED_DESC = 7,  // Latest chapter upload (newest first)
    DATE_UPDATED_ASC = 8,   // Latest chapter upload (oldest first)
    TOTAL_CHAPTERS = 9,     // Most chapters first
    DOWNLOADED_ONLY = 10,   // Local downloaded count, hiding books with no local downloads
};

class LibrarySectionTab : public brls::Box {
public:
    LibrarySectionTab();

    ~LibrarySectionTab() override;

    void onFocusGained() override;
    void refresh();

private:
    void loadCategories();
    void createCategoryTabs();
    void loadCategoryManga(int categoryId);
    void selectCategory(int categoryId);
    void onMangaSelected(const Manga& manga);
    void triggerLibraryUpdate();
    void updateCategoryButtonStyles();
    void sortMangaList();
    void cycleSortMode();
    void showSortMenu();
    void updateSortButtonText();
    void navigateToPreviousCategory();
    void navigateToNextCategory();
    void scrollToCategoryIndex(int index);
    void updateCategoryButtonTexts();

    // Grouping methods
    void setGroupMode(LibraryGroupMode mode);
    void loadAllManga();
    void loadBySource();
    void showGroupModeMenu();

    // Context menu (Start button / long-press)
    void showMangaContextMenu(const Manga& manga, int index);
    void showDownloadSubmenu(const std::vector<Manga>& mangaList);
    void showChangeCategoryDialog(const std::vector<Manga>& mangaList);
    void showMigrateSourceMenu(const Manga& manga);

    // Selection mode
    void enterSelectionMode(int initialIndex);
    void exitSelectionMode();
    void updateSelectionTitle();

    // Batch actions
    void downloadChapters(const std::vector<Manga>& mangaList, const std::string& mode);
    void downloadNextChapters(const std::vector<Manga>& mangaList, int count);
    void markMangaRead(const std::vector<Manga>& mangaList);
    void markMangaUnread(const std::vector<Manga>& mangaList);
    void removeFromLibrary(const std::vector<Manga>& mangaList);
    void openTracking(const Manga& manga);

    bool m_selectionMode = false;
    int m_selectionExitGeneration = 0;  // Generation counter to cancel pending auto-exit

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    // Currently selected category
    int m_currentCategoryId = 0;
    std::string m_currentCategoryName = "Library";

    // Sort mode
    LibrarySortMode m_sortMode = LibrarySortMode::TITLE_ASC;

    // Group mode
    LibraryGroupMode m_groupMode = LibraryGroupMode::BY_CATEGORY;

    // UI Components
    brls::Label* m_titleLabel = nullptr;

    // Category tabs row
    brls::Box* m_categoryTabsBox = nullptr;        // Outer container (clips)
    brls::Box* m_categoryScrollContainer = nullptr; // Inner container (scrolls)
    std::vector<brls::Button*> m_categoryButtons;
    int m_selectedCategoryIndex = 0;               // Index in m_categories
    float m_categoryScrollOffset = 0.0f;           // Current scroll offset

    // Action buttons
    brls::Button* m_updateBtn = nullptr;
    brls::Button* m_sortBtn = nullptr;
    brls::Image* m_sortIcon = nullptr;

    // Main content grid
    RecyclingGrid* m_contentGrid = nullptr;

    // Data
    std::vector<Manga> m_mangaList;           // Working list (may be filtered)
    std::vector<Manga> m_fullMangaList;       // Complete list (never filtered)
    std::vector<Category> m_categories;       // Visible categories

    // Cached manga state for incremental updates (like downloads tab)
    struct CachedMangaItem {
        int id;
        int unreadCount;
        int64_t lastReadAt;
        int64_t latestChapterUploadDate;
        int chapterCount;
    };
    std::vector<CachedMangaItem> m_cachedMangaList;  // Cached state for comparison
    int m_cachedCategoryId = -1;                     // Category ID for cached data

    // Helper to update manga cells incrementally without full rebuild
    void updateMangaCellsIncrementally(const std::vector<Manga>& newManga);

    bool m_loaded = false;
    bool m_categoriesLoaded = false;
    bool m_focusGridAfterLoad = false;  // Focus first grid item after loading new category
    int m_combinedQueryCategoryId = -1; // Category being fetched by combined query (skip redundant fetch)

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
