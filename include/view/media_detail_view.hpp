/**
 * VitaSuwayomi - Manga Detail View
 * Shows detailed information about a manga including chapters list
 */

#pragma once

#include <borealis.hpp>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class MangaDetailView : public brls::Box {
public:
    MangaDetailView(const Manga& manga);
    ~MangaDetailView();

    static brls::View* create();

    void refresh();

    // Override to refresh chapter data when returning from reader
    void willAppear(bool resetState) override;

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

    // Chapter list display
    void populateChaptersList();
    void onChapterSelected(const Chapter& chapter);
    void markChapterRead(const Chapter& chapter);
    void downloadChapter(const Chapter& chapter);
    void deleteChapterDownload(const Chapter& chapter);

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

    // Chapters list
    brls::ScrollingFrame* m_chaptersScroll = nullptr;
    brls::Box* m_chaptersBox = nullptr;
    brls::Label* m_chaptersLabel = nullptr;

    // Chapter sort/filter
    brls::Button* m_sortBtn = nullptr;
    brls::Image* m_sortIcon = nullptr;
    brls::Button* m_filterBtn = nullptr;
    bool m_sortDescending = true;  // Default: newest first
    bool m_filterDownloaded = false;
    bool m_filterUnread = false;

    // Currently visible chapter action icon (shown on focused row)
    brls::Image* m_currentFocusedIcon = nullptr;

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
};

// Alias for backward compatibility
using MediaDetailView = MangaDetailView;

} // namespace vitasuwayomi
