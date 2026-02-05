/**
 * VitaSuwayomi - Library Section Tab
 * Shows manga library content organized by categories
 * Displays user's categories as tabs at the top for easy navigation
 */

#pragma once

#include <borealis.hpp>
#include <memory>
#include "app/suwayomi_client.hpp"
#include "view/recycling_grid.hpp"

namespace vitasuwayomi {

// Sort modes for library manga
enum class LibrarySortMode {
    TITLE_ASC,       // A-Z
    TITLE_DESC,      // Z-A
    UNREAD_DESC,     // Most unread first
    UNREAD_ASC,      // Least unread first
    RECENTLY_ADDED,  // Recently added (by ID, higher = newer)
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
    void updateSortButtonText();
    void navigateToPreviousCategory();
    void navigateToNextCategory();
    void scrollToCategoryIndex(int index);
    void updateCategoryButtonTexts();

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
    void markMangaRead(const std::vector<Manga>& mangaList);
    void markMangaUnread(const std::vector<Manga>& mangaList);
    void removeFromLibrary(const std::vector<Manga>& mangaList);
    void openTracking(const Manga& manga);

    bool m_selectionMode = false;

    // Check if this tab is still valid (not destroyed)
    bool isValid() const { return m_alive && *m_alive; }

    // Currently selected category
    int m_currentCategoryId = 0;
    std::string m_currentCategoryName = "Library";

    // Sort mode
    LibrarySortMode m_sortMode = LibrarySortMode::TITLE_ASC;

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
    std::vector<Manga> m_mangaList;
    std::vector<Category> m_categories;       // Visible categories

    bool m_loaded = false;
    bool m_categoriesLoaded = false;

    // Shared pointer to track if this object is still alive
    std::shared_ptr<bool> m_alive;
};

} // namespace vitasuwayomi
