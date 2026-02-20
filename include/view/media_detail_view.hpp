/**
 * VitaSuwayomi - Manga Detail View
 * Shows detailed information about a manga including chapters list
 * Uses RecyclerFrame for efficient virtual chapter list rendering
 */

#pragma once

#include <borealis.hpp>
#include <set>
#include <chrono>
#include <atomic>
#include <memory>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaDetailView;
class ChaptersDataSource;

// Reusable cell for chapter rows (RecyclerView pattern - only visible rows exist)
class ChapterCell : public brls::RecyclerCell {
public:
    ChapterCell();
    static ChapterCell* create();
    void prepareForReuse() override;

    // Public members for data binding by the data source
    brls::Box* infoBox = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Label* subtitleLabel = nullptr;
    brls::Label* readLabel = nullptr;
    brls::Box* dlBtn = nullptr;
    brls::Image* dlIcon = nullptr;
    brls::Label* dlLabel = nullptr;
    brls::Image* xButtonIcon = nullptr;

    // Track which chapter this cell currently represents
    int chapterIndex = -1;
    int rowIndex = -1;
};

// Data source for the chapter RecyclerFrame
class ChaptersDataSource : public brls::RecyclerDataSource {
public:
    ChaptersDataSource(MangaDetailView* view);

    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

private:
    MangaDetailView* m_view;

    void bindCell(ChapterCell* cell, int row);
    void applyDownloadState(ChapterCell* cell, int dlState, int downloadedPages, int pageCount, const Chapter& chapter);
};

class MangaDetailView : public brls::Box {
public:
    MangaDetailView(const Manga& manga);
    ~MangaDetailView();

    static brls::View* create();

    void refresh();

    // Override to refresh chapter data when returning from reader
    void willAppear(bool resetState) override;
    void willDisappear(bool resetState) override;

    // Public accessors for ChaptersDataSource
    const std::vector<Chapter>& getSortedFilteredChapters() const { return m_sortedFilteredChapters; }
    const Manga& getManga() const { return m_manga; }
    void onChapterSelected(const Chapter& chapter);
    void downloadChapter(const Chapter& chapter);
    void deleteChapterDownload(const Chapter& chapter);
    brls::Image* getCurrentFocusedIcon() const { return m_currentFocusedIcon; }
    void setCurrentFocusedIcon(brls::Image* icon) { m_currentFocusedIcon = icon; }
    int getDownloadStateForRow(int row) const;
    std::pair<int,int> getDownloadProgressForRow(int row) const;

private:
    void loadDetails();
    void loadChapters();
    void loadCover();
    void onRead(int chapterIndex = -1);  // -1 means continue from last read
    void onAddToLibrary();
    void onRemoveFromLibrary();
    void onDownloadChapters();
    void onDeleteDownloads();
    void showDownloadOptions();
    void showCategoryDialog();
    void showMangaMenu();

    // Chapter actions
    void markAllRead();
    void markAllUnread();
    void downloadAllChapters();
    void downloadUnreadChapters();
    void deleteAllDownloads();
    void showChapterMenu(const Chapter& chapter);

    // Chapter selection mode
    void toggleSelectionMode();
    void toggleChapterSelection(int chapterIndex);
    void selectChapterRange(int startIndex, int endIndex);
    void clearSelection();
    void markSelectedRead();
    void markSelectedUnread();
    void downloadSelected();
    void deleteSelectedDownloads();
    void showSelectionActionMenu();
    void updateSelectionUI();

    // Chapter list display
    void populateChaptersList();
    void setupChapterNavigation();
    void markChapterRead(const Chapter& chapter);

    // Tracking
    void showTrackingDialog();
    void showTrackerSearchInputDialog(const Tracker& tracker);
    void showTrackerSearchDialog(const Tracker& tracker, const std::string& searchQuery);
    void showTrackEditDialog(const TrackRecord& record, const Tracker& tracker);
    void showTrackerLoginDialog(const Tracker& tracker);
    void loadTrackingData();
    void updateTrackingButtonText();
    void updateTracking();

    Manga m_manga;
    std::vector<Chapter> m_chapters;
    std::vector<Category> m_categories;
    std::vector<TrackRecord> m_trackRecords;
    std::vector<Tracker> m_trackers;

    // Main layout
    brls::ScrollingFrame* m_scrollView = nullptr;
    brls::Box* m_mainContent = nullptr;

    // Header info
    brls::Label* m_titleLabel = nullptr;
    brls::Label* m_authorLabel = nullptr;
    brls::Label* m_artistLabel = nullptr;
    brls::Label* m_statusLabel = nullptr;
    brls::Label* m_sourceLabel = nullptr;
    brls::Label* m_chapterCountLabel = nullptr;
    brls::Label* m_descriptionLabel = nullptr;
    brls::Image* m_coverImage = nullptr;

    // Genre tags
    brls::Box* m_genreBox = nullptr;

    // Action buttons
    brls::Button* m_readButton = nullptr;
    brls::Button* m_libraryButton = nullptr;
    brls::Button* m_trackingButton = nullptr;

    // Chapters list (RecyclerFrame - only creates visible rows)
    brls::RecyclerFrame* m_chaptersRecycler = nullptr;
    ChaptersDataSource* m_chaptersDataSource = nullptr;
    brls::Label* m_chaptersLabel = nullptr;

    // Chapter sort/filter
    brls::Button* m_sortBtn = nullptr;
    brls::Image* m_sortIcon = nullptr;
    brls::Button* m_filterBtn = nullptr;
    brls::Button* m_menuBtn = nullptr;
    bool m_sortDescending = true;  // Default: newest first
    bool m_filterDownloaded = false;
    bool m_filterUnread = false;
    bool m_filterBookmarked = false;
    std::string m_filterScanlator;

    // Chapter selection mode
    bool m_selectionMode = false;
    std::set<int> m_selectedChapters;
    brls::Button* m_selectBtn = nullptr;
    brls::Label* m_selectionCountLabel = nullptr;
    brls::Box* m_selectionBar = nullptr;
    int m_rangeSelectStart = -1;  // For range selection

    // Currently visible chapter action icon (shown on focused row)
    brls::Image* m_currentFocusedIcon = nullptr;

    // Sorted/filtered chapter data for RecyclerFrame
    std::vector<Chapter> m_sortedFilteredChapters;
    std::vector<int> m_chapterDlStates;  // snapshot of download state per filtered chapter
    std::vector<std::pair<int,int>> m_chapterDlProgress;  // {downloadedPages, pageCount} per chapter

    // Track first appearance to avoid duplicate chapter loading
    bool m_firstAppearance = true;

    // Description expand/collapse
    bool m_descriptionExpanded = false;
    std::string m_fullDescription;
    void toggleDescription();

    // Helper to update sort icon
    void updateSortIcon();

    // Update the read button text based on reading progress
    void updateReadButtonText();

    // Cancel all downloading chapters (keep completed)
    void cancelAllDownloading();

    // Reset cover image
    void resetCover();

    // Live download progress tracking (incremental, no full rebuild)
    std::chrono::steady_clock::time_point m_lastProgressRefresh;
    std::atomic<bool> m_progressCallbackActive{false};
    std::shared_ptr<bool> m_alive;

    // Refresh download state snapshot and rebind visible cells (full rebuild)
    void updateChapterDownloadStates();

    // Lightweight: update only download icons on visible cells (no reloadData, no focus loss)
    void refreshVisibleDownloadIcons();

    // Friend for data source access
    friend class ChaptersDataSource;
};

// Alias for backward compatibility
using MediaDetailView = MangaDetailView;

} // namespace vitasuwayomi
