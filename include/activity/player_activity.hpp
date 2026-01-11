/**
 * VitaABS - Player Activity
 * Audio playback screen with controls
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include <string>

// Forward declaration
namespace vitaabs { class VideoView; }

namespace vitaabs {

class PlayerActivity : public brls::Activity {
public:
    // Play audiobook/podcast item (single file or book)
    PlayerActivity(const std::string& itemId);

    // Play podcast episode or audiobook with specific episode
    PlayerActivity(const std::string& itemId, const std::string& episodeId,
                   float startTime = -1.0f);

    // Play local downloaded file
    PlayerActivity(const std::string& itemId, bool isLocalFile);

    // Play with pre-downloaded temp file (downloaded before player push)
    PlayerActivity(const std::string& itemId, const std::string& episodeId,
                   const std::string& preDownloadedPath, float startTime = -1.0f);

    // Play direct file path (for debug/testing)
    static PlayerActivity* createForDirectFile(const std::string& filePath);

    brls::View* createContentView() override;

    void onContentAvailable() override;

    void willDisappear(bool resetState) override;

private:
    void loadMedia();
    void loadCoverArt(const std::string& coverUrl);
    void updateProgress();
    void syncProgressToServer();  // Periodic sync to server during playback
    void updatePlayPauseButton();
    void updateSpeedLabel();
    void cyclePlaybackSpeed();
    void togglePlayPause();
    void seek(int seconds);
    std::string formatTime(double seconds);
    std::string formatTimeRemaining(double remaining);
    float getSpeedValue(int index);

    std::string m_itemId;
    std::string m_episodeId;       // For podcast episodes
    std::string m_directFilePath;  // For direct file playback (debug)
    std::string m_tempFilePath;    // Temp file for streaming (downloaded before playback)
    std::string m_coverUrl;        // URL for cover art
    bool m_isPlaying = false;
    bool m_isPhoto = false;
    bool m_isLocalFile = false;   // Playing from local download
    bool m_isDirectFile = false;  // Playing direct file path (debug)
    bool m_isPreDownloaded = false; // File was pre-downloaded before player push
    bool m_destroying = false;    // Flag to prevent timer callbacks during destruction
    bool m_loadingMedia = false;  // Flag to prevent rapid re-entry of loadMedia
    double m_pendingSeek = 0.0;   // Pending seek position (set when resuming)
    double m_totalDuration = 0.0; // Total duration for display
    brls::RepeatingTimer m_updateTimer;
    int m_syncCounter = 0;        // Counter for periodic server sync (every 30 updates = 30 seconds)
    float m_lastSyncedTime = 0.0f; // Last position synced to server
    std::string m_sessionId;      // Active playback session ID (for server sync)

    // Main UI bindings
    BRLS_BIND(brls::Box, playerContainer, "player/container");
    BRLS_BIND(brls::Image, coverImage, "player/cover");
    BRLS_BIND(brls::Label, titleLabel, "player/title");
    BRLS_BIND(brls::Label, authorLabel, "player/author");
    BRLS_BIND(brls::Slider, progressSlider, "player/progress");
    BRLS_BIND(brls::Label, timeElapsedLabel, "player/timeElapsed");
    BRLS_BIND(brls::Label, timeRemainingLabel, "player/timeRemaining");
    BRLS_BIND(brls::Button, btnRewind, "player/btnRewind");
    BRLS_BIND(brls::Button, btnPlayPause, "player/btnPlayPause");
    BRLS_BIND(brls::Button, btnForward, "player/btnForward");
    BRLS_BIND(brls::Label, rewindLabel, "player/rewindLabel");
    BRLS_BIND(brls::Label, forwardLabel, "player/forwardLabel");
    BRLS_BIND(brls::Label, playPauseIcon, "player/playPauseIcon");
    BRLS_BIND(brls::Button, btnSpeed, "player/btnSpeed");
    BRLS_BIND(brls::Label, speedLabel, "player/speedLabel");
    BRLS_BIND(brls::Label, chapterInfoLabel, "player/chapterInfo");

    // Legacy bindings (hidden but needed for compatibility)
    BRLS_BIND(brls::Label, timeLabel, "player/time");
    BRLS_BIND(brls::Box, controlsBox, "player/controls");
    BRLS_BIND(brls::Image, photoImage, "player/photo");
    BRLS_BIND(VideoView, videoView, "player/video");
};

} // namespace vitaabs
