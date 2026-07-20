/**
 * VitaSuwayomi - Settings Tab implementation
 * Settings for manga reader app
 */

#include "view/settings_tab.hpp"
#include "view/options_popover.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "app/downloads_manager.hpp"
#include "utils/library_cache.hpp"
#include "utils/async.hpp"
#include "utils/http_client.hpp"
#include "utils/perf_overlay.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <sstream>
#include <map>

#include "platform/platform.hpp"

#ifdef __vita__
#include <psp2/net/netctl.h>
#endif

// Local-IP discovery on POSIX desktop/Android (Switch/Windows/Vita don't have
// getifaddrs; they fall back to honest "unknown" rather than fabricated data).
#if defined(__linux__) || defined(__ANDROID__)
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#endif

namespace vitasuwayomi {

// Fixed neutral-dark palette for the settings shell (rail + detail chrome),
// mirroring the sibling project's two-pane settings look. The individual
// setting rows below stay as the app's themed borealis cells.
namespace {
namespace tok {
    inline NVGcolor bg()       { return nvgRGB(45, 45, 45); }
    inline NVGcolor railBg()   { return nvgRGB(50, 50, 50); }
    inline NVGcolor raised()   { return nvgRGB(60, 60, 72); }
    inline NVGcolor hairline() { return nvgRGB(67, 67, 74); }
    inline NVGcolor text()     { return nvgRGB(255, 255, 255); }
    inline NVGcolor muted()    { return nvgRGB(163, 163, 163); }
    inline NVGcolor accent()   { return nvgRGB(0xE5, 0xA0, 0x0D); }
}

float railWidthForViewport() {
    const float w = brls::Application::contentWidth;
    if (w >= 1280.0f) return 280.0f;
    if (w >= 1024.0f) return 240.0f;
    if (w >= 800.0f)  return 220.0f;
    if (w >= 560.0f)  return 180.0f;
    return 160.0f;
}
} // namespace

SettingsTab::SettingsTab() {
    m_alive = std::make_shared<bool>(true);

    this->setAxis(brls::Axis::ROW);
    this->setJustifyContent(brls::JustifyContent::FLEX_START);
    this->setAlignItems(brls::AlignItems::STRETCH);
    this->setGrow(1.0f);
    this->setBackgroundColor(tok::bg());

    // ---- Left rail ----------------------------------------------------
    m_railContainer = new brls::Box();
    m_railContainer->setAxis(brls::Axis::COLUMN);
    m_railContainer->setAlignItems(brls::AlignItems::STRETCH);
    m_railContainer->setWidth(railWidthForViewport());
    m_railContainer->setBackgroundColor(tok::railBg());

    auto* railHeader = new brls::Box();
    railHeader->setAxis(brls::Axis::COLUMN);
    railHeader->setPaddingLeft(18);
    railHeader->setPaddingRight(14);
    railHeader->setPaddingTop(18);
    railHeader->setPaddingBottom(14);

    auto* railTitle = new brls::Label();
    railTitle->setText("Settings");
    railTitle->setFontSize(22);
    railTitle->setTextColor(tok::text());
    railHeader->addView(railTitle);

    auto* railHairline = new brls::Box();
    railHairline->setHeight(1);
    railHairline->setMarginTop(12);
    railHairline->setBackgroundColor(tok::hairline());
    railHeader->addView(railHairline);
    m_railContainer->addView(railHeader);

    m_railScroll = new brls::ScrollingFrame();
    m_railScroll->setGrow(1.0f);
    m_railScroll->setFocusable(false);
    m_railBox = new brls::Box();
    m_railBox->setAxis(brls::Axis::COLUMN);
    m_railBox->setAlignItems(brls::AlignItems::STRETCH);
    m_railBox->setPaddingTop(6);
    m_railBox->setPaddingBottom(6);
    m_railScroll->setContentView(m_railBox);
    m_railContainer->addView(m_railScroll);

    // ---- Right detail -------------------------------------------------
    m_detailContainer = new brls::Box();
    m_detailContainer->setAxis(brls::Axis::COLUMN);
    m_detailContainer->setAlignItems(brls::AlignItems::STRETCH);
    m_detailContainer->setGrow(1.0f);
    m_detailContainer->setPaddingLeft(24);
    m_detailContainer->setPaddingRight(24);
    m_detailContainer->setPaddingTop(20);
    m_detailContainer->setPaddingBottom(12);

    auto* detailHeader = new brls::Box();
    detailHeader->setAxis(brls::Axis::ROW);
    detailHeader->setAlignItems(brls::AlignItems::CENTER);
    detailHeader->setMarginBottom(14);

    auto* headerTextCol = new brls::Box();
    headerTextCol->setAxis(brls::Axis::COLUMN);
    headerTextCol->setGrow(1.0f);
    m_detailTitle = new brls::Label();
    m_detailTitle->setFontSize(26);
    m_detailTitle->setTextColor(tok::text());
    headerTextCol->addView(m_detailTitle);
    m_detailSubtitle = new brls::Label();
    m_detailSubtitle->setFontSize(13);
    m_detailSubtitle->setTextColor(tok::muted());
    m_detailSubtitle->setMarginTop(3);
    headerTextCol->addView(m_detailSubtitle);
    detailHeader->addView(headerTextCol);
    m_detailContainer->addView(detailHeader);

    auto* detailHairline = new brls::Box();
    detailHairline->setHeight(1);
    detailHairline->setMarginBottom(10);
    detailHairline->setBackgroundColor(tok::hairline());
    m_detailContainer->addView(detailHairline);

    m_detailScroll = new brls::ScrollingFrame();
    m_detailScroll->setGrow(1.0f);
    m_detailScroll->setFocusable(false);
    m_detailContent = new brls::Box();
    m_detailContent->setAxis(brls::Axis::COLUMN);
    m_detailContent->setAlignItems(brls::AlignItems::STRETCH);
    m_detailScroll->setContentView(m_detailContent);
    m_detailContainer->addView(m_detailScroll);

    this->addView(m_railContainer);
    this->addView(m_detailContainer);

    // ---- Sections: build each into its own detached box ---------------
    struct Sec {
        const char* name;
        const char* subtitle;
        const char* icon;
        void (SettingsTab::*build)();
    };
    const Sec secs[] = {
        {"Server",     "Connection & authentication", "web.png",                    &SettingsTab::createAccountSection},
        {"Tracking",   "AniList, MyAnimeList & more",  "star.png",                   &SettingsTab::createTrackingSection},
        {"Appearance", "Theme & interface",            "show.png",                   &SettingsTab::createUISection},
        {"Library",    "Categories, updates, badges",  "book-multiple.png",          &SettingsTab::createLibrarySection},
        {"Reader",     "Reading mode & defaults",      "book-open-page-variant.png", &SettingsTab::createReaderSection},
        {"Downloads",  "Storage & quality",            "download.png",               &SettingsTab::createDownloadsSection},
        {"Browse",     "Sources & languages",          "search.png",                 &SettingsTab::createBrowseSection},
        {"Sync",       "SyncYomi",                     "refresh.png",                &SettingsTab::createSyncYomiSection},
        {"Backup",     "Export & restore",             "import.png",                 &SettingsTab::createBackupSection},
        {"Statistics", "Reading statistics",           "history.png",                &SettingsTab::createStatisticsSection},
    };
    const int count = static_cast<int>(sizeof(secs) / sizeof(secs[0]));

    for (int i = 0; i < count; i++) {
        auto* box = makeSectionBox();
        m_contentBox = box;                 // redirect the builders' addView target
        (this->*secs[i].build)();
        m_sectionBoxes.push_back(box);      // kept detached; attached on demand
        m_sectionNames.push_back(secs[i].name);
        m_sectionSubtitles.push_back(secs[i].subtitle);

        auto* row = makeRailRow(secs[i].icon, secs[i].name, i);
        m_railBox->addView(row);
        m_railRows.push_back(row);

        // Deterministically route RIGHT from the rail row into this section's
        // first focusable cell. Relying only on the detail pane's default-focus
        // chain left some (taller) sections unreachable with RIGHT; a fixed
        // route makes every section enterable. The cell persists for the tab's
        // lifetime (only the section box is ever detached, never freed early).
        if (brls::View* firstCell = box->getDefaultFocus()) {
            row->setCustomNavigationRoute(brls::FocusDirection::RIGHT, firstCell);
        }
    }

    // Version readout at the bottom of the rail (non-focusable), in place of a
    // dedicated About section.
    m_railBox->addView(makeRailInfoRow("options.png",
                                       std::string("VitaSuwayomi ") + VITA_SUWAYOMI_VERSION));

    showSection(0);
}

brls::Box* SettingsTab::makeSectionBox() {
    auto* box = new brls::Box();
    box->setAxis(brls::Axis::COLUMN);
    box->setAlignItems(brls::AlignItems::STRETCH);
    box->setMarginBottom(20);
    return box;
}

brls::Box* SettingsTab::makeRailRow(const std::string& icon, const std::string& title, int sectionId) {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setHeight(46);
    row->setMarginLeft(8);
    row->setMarginRight(8);
    row->setMarginTop(2);
    row->setMarginBottom(2);
    row->setCornerRadius(10);
    row->setPaddingLeft(12);
    row->setPaddingRight(10);
    row->setFocusable(true);

    auto* bar = new brls::Box();
    bar->setPositionType(brls::PositionType::ABSOLUTE);
    bar->setPositionLeft(0);
    bar->setPositionTop(8);
    bar->setWidth(4);
    bar->setHeight(30);
    bar->setCornerRadius(2);
    bar->setBackgroundColor(tok::accent());
    bar->setVisibility(brls::Visibility::INVISIBLE);
    row->addView(bar);
    m_railBars.push_back(bar);

    auto* img = new brls::Image();
    img->setWidth(20);
    img->setHeight(20);
    img->setScalingType(brls::ImageScalingType::FIT);
    img->setMarginRight(12);
    img->setImageFromRes("icons/" + icon);
    row->addView(img);

    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(15);
    label->setTextColor(tok::text());
    label->setGrow(1.0f);
    row->addView(label);

    auto* chevron = new brls::Image();
    chevron->setWidth(14);
    chevron->setHeight(14);
    chevron->setScalingType(brls::ImageScalingType::FIT);
    chevron->setImageFromRes("icons/right.png");
    row->addView(chevron);

    row->registerClickAction([this, sectionId](brls::View*) { showSection(sectionId); return true; });
    row->addGestureRecognizer(new brls::TapGestureRecognizer(row));
    row->getFocusEvent()->subscribe([this, sectionId](brls::View*) {
        if (m_activeSection != sectionId) showSection(sectionId);
    });
    return row;
}

brls::Box* SettingsTab::makeRailInfoRow(const std::string& icon, const std::string& title) {
    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setHeight(40);
    row->setMarginLeft(8);
    row->setMarginRight(8);
    row->setMarginTop(6);
    row->setMarginBottom(4);
    row->setPaddingLeft(12);
    row->setPaddingRight(10);
    row->setFocusable(false);

    auto* img = new brls::Image();
    img->setWidth(18);
    img->setHeight(18);
    img->setScalingType(brls::ImageScalingType::FIT);
    img->setMarginRight(10);
    img->setImageFromRes("icons/" + icon);
    row->addView(img);

    auto* label = new brls::Label();
    label->setText(title);
    label->setFontSize(13);
    label->setTextColor(tok::muted());
    row->addView(label);
    return row;
}

void SettingsTab::paintRailRowSelection() {
    for (size_t i = 0; i < m_railRows.size(); i++) {
        const bool active = (static_cast<int>(i) == m_activeSection);
        m_railRows[i]->setBackgroundColor(active ? tok::raised() : nvgRGBA(0, 0, 0, 0));
        if (i < m_railBars.size() && m_railBars[i]) {
            m_railBars[i]->setVisibility(active ? brls::Visibility::VISIBLE
                                                : brls::Visibility::INVISIBLE);
        }
    }
}

void SettingsTab::showSection(int sectionId) {
    if (sectionId < 0 || sectionId >= static_cast<int>(m_sectionBoxes.size())) return;

    brls::Box* target = m_sectionBoxes[sectionId];
    if (m_attachedSection != target) {
        // Detach the previous section without freeing it; only the active
        // section is ever parented so focus/layout never walk hidden cells.
        if (m_attachedSection) m_detailContent->removeView(m_attachedSection, false);
        m_detailContent->addView(target);
        m_attachedSection = target;

        // Re-point the focus chain so a later RIGHT-from-rail enters the new
        // section rather than tunnelling into the detached one.
        m_detailContent->setLastFocusedView(target);
        m_detailScroll->setLastFocusedView(m_detailContent);
        m_detailContainer->setLastFocusedView(m_detailScroll);

        // Reset the shared detail scroll to the top; otherwise a new (often
        // shorter) section inherits the previous section's scroll offset and
        // renders clipped past its top until the user scrolls back up.
        m_detailScroll->setContentOffsetY(0.0f, false);
    }

    if (sectionId < static_cast<int>(m_sectionNames.size()))
        m_detailTitle->setText(m_sectionNames[sectionId]);
    if (sectionId < static_cast<int>(m_sectionSubtitles.size()))
        m_detailSubtitle->setText(m_sectionSubtitles[sectionId]);

    m_activeSection = sectionId;
    paintRailRowSelection();
}

SettingsTab::~SettingsTab() {
    if (m_alive) *m_alive = false;

    // Only the attached section box is owned by the view tree; the detached
    // ones have no parent, so free them here.
    for (auto* box : m_sectionBoxes) {
        if (box && box != m_attachedSection) delete box;
    }
}

void SettingsTab::willDisappear(bool resetState) {
    brls::Box::willDisappear(resetState);

    // Invalidate alive flag BEFORE destruction so pending async callbacks bail out
    if (m_alive) *m_alive = false;
}

void SettingsTab::showChoicePopover(const std::string& title,
                                    const std::vector<std::string>& options,
                                    int currentIndex,
                                    std::function<void(int)> onSelect) {
    std::vector<OptionRow> rows;
    for (int i = 0; i < static_cast<int>(options.size()); i++) {
        const bool cur = (i == currentIndex);
        const int idx = i;
        rows.push_back({ cur ? "radio_checked.png" : "radio.png", options[i], "", cur, false,
            [onSelect, idx]() { if (onSelect) onSelect(idx); }});
    }
    rows.push_back({ "back.png", "Cancel", "", false, true, []() {}});
    // Show 5 rows then scroll (matches the Sort By menu; the theme list is long).
    OptionsPopover::show("SETTING", title, std::move(rows), nullptr, 5);
}

void SettingsTab::addSectionSeparator() {
    auto* separator = new brls::Box();
    separator->setHeight(1);
    separator->setMarginTop(12);
    separator->setMarginBottom(4);
    separator->setMarginLeft(8);
    separator->setMarginRight(8);
    separator->setBackgroundColor(Application::getInstance().getSeparatorColor());
    m_contentBox->addView(separator);
}

void SettingsTab::createAccountSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Server");
    m_contentBox->addView(header);

    // Current server info
    m_serverLabel = new brls::Label();
    m_serverLabel->setText("Active: " + (app.getServerUrl().empty() ? "Not connected" : app.getServerUrl()));
    m_serverLabel->setFontSize(18);
    m_serverLabel->setMarginLeft(16);
    m_serverLabel->setMarginBottom(8);
    m_contentBox->addView(m_serverLabel);

    // Local URL configuration
    auto* localUrlCell = new brls::DetailCell();
    localUrlCell->setText("Local URL");
    localUrlCell->setDetailText(settings.localServerUrl.empty() ? "Not set" : settings.localServerUrl);
    localUrlCell->registerClickAction([this, localUrlCell](brls::View* view) {
        showUrlInputDialog("Local Server URL", "Enter local network URL (e.g., http://192.168.1.100:4567)",
            Application::getInstance().getSettings().localServerUrl,
            [this, localUrlCell](const std::string& url) {
                Application::getInstance().getSettings().localServerUrl = url;
                localUrlCell->setDetailText(url.empty() ? "Not set" : url);
                Application::getInstance().saveSettings();
                updateServerLabel();
            });
        return true;
    });
    m_contentBox->addView(localUrlCell);

    // Remote URL configuration
    auto* remoteUrlCell = new brls::DetailCell();
    remoteUrlCell->setText("Remote URL");
    remoteUrlCell->setDetailText(settings.remoteServerUrl.empty() ? "Not set" : settings.remoteServerUrl);
    remoteUrlCell->registerClickAction([this, remoteUrlCell](brls::View* view) {
        showUrlInputDialog("Remote Server URL", "Enter remote URL (e.g., https://myserver.com:4567)",
            Application::getInstance().getSettings().remoteServerUrl,
            [this, remoteUrlCell](const std::string& url) {
                Application::getInstance().getSettings().remoteServerUrl = url;
                remoteUrlCell->setDetailText(url.empty() ? "Not set" : url);
                Application::getInstance().saveSettings();
                updateServerLabel();
            });
        return true;
    });
    m_contentBox->addView(remoteUrlCell);

    // Toggle between local and remote (opens the choice popover)
    auto* connCell = new brls::DetailCell();
    connCell->setText("Active Connection");
    connCell->setDetailText(settings.useRemoteUrl ? "Remote" : "Local");
    connCell->registerClickAction([this, connCell](brls::View*) {
        const int cur = Application::getInstance().getSettings().useRemoteUrl ? 1 : 0;
        showChoicePopover("Active Connection", {"Local", "Remote"}, cur,
            [this, connCell](int index) {
                Application& app = Application::getInstance();
                if (index == 0) {
                    app.switchToLocalUrl();
                } else {
                    app.switchToRemoteUrl();
                }
                updateServerLabel();
                connCell->setDetailText(index == 0 ? "Local" : "Remote");
                brls::Application::notify(index == 0 ? "Switched to Local URL" : "Switched to Remote URL");
            });
        return true;
    });
    m_contentBox->addView(connCell);

    // Auto-switch on failure toggle
    auto* autoSwitchToggle = new brls::BooleanCell();
    autoSwitchToggle->init("Auto-Switch on Failure", settings.autoSwitchOnFailure,
        [](bool value) {
            Application::getInstance().getSettings().autoSwitchOnFailure = value;
            Application::getInstance().saveSettings();
            brls::Application::notify(value ? "Auto-switch enabled" : "Auto-switch disabled");
        });
    m_contentBox->addView(autoSwitchToggle);

    // Connection timeout (opens the choice popover)
    auto* timeoutCell = new brls::DetailCell();
    timeoutCell->setText("Connection Timeout");
    timeoutCell->setDetailText(std::to_string(settings.connectionTimeout) + " seconds");
    timeoutCell->registerClickAction([this, timeoutCell](brls::View*) {
        AppSettings& s = Application::getInstance().getSettings();
        int timeoutIndex = 1; // default to 30s
        if (s.connectionTimeout <= 10) timeoutIndex = 0;
        else if (s.connectionTimeout <= 30) timeoutIndex = 1;
        else if (s.connectionTimeout <= 60) timeoutIndex = 2;
        else timeoutIndex = 3;
        showChoicePopover("Connection Timeout",
            {"10 seconds", "30 seconds", "60 seconds", "120 seconds"}, timeoutIndex,
            [timeoutCell](int index) {
                int timeouts[] = {10, 30, 60, 120};
                Application::getInstance().getSettings().connectionTimeout = timeouts[index];
                Application::getInstance().saveSettings();
                timeoutCell->setDetailText(std::to_string(timeouts[index]) + " seconds");
            });
        return true;
    });
    m_contentBox->addView(timeoutCell);

    // Network Test button
    auto* networkTestCell = new brls::DetailCell();
    networkTestCell->setText("Network Test");
    networkTestCell->setDetailText("Test WiFi and server connection");
    networkTestCell->registerClickAction([this](brls::View* view) {
        runNetworkTest();
        return true;
    });
    m_contentBox->addView(networkTestCell);

    // Connection status indicator
    bool isOnline = Application::getInstance().isConnected();
    auto* connectionStatusLabel = new brls::Label();
    connectionStatusLabel->setText(isOnline ? "Status: Connected" : "Status: Offline");
    connectionStatusLabel->setFontSize(16);
    connectionStatusLabel->setMarginLeft(16);
    connectionStatusLabel->setMarginBottom(8);
    m_contentBox->addView(connectionStatusLabel);

    // Reconnect button (full auth restoration, same as app restart)
    auto* reconnectCell = new brls::DetailCell();
    reconnectCell->setText("Reconnect");
    reconnectCell->setDetailText("Restore connection (same as app restart)");
    reconnectCell->registerClickAction([this, connectionStatusLabel](brls::View* view) {
        Application& app = Application::getInstance();
        std::string url = app.getActiveServerUrl();
        if (url.empty()) {
            brls::Application::notify("No server URL configured");
            return true;
        }

        brls::Application::notify("Reconnecting to " + url + "...");

        asyncRun([url, connectionStatusLabel]() {
            Application& app = Application::getInstance();
            SuwayomiClient& client = SuwayomiClient::getInstance();
            client.setServerUrl(url);

            // Step 1: Restore auth session (same as app startup)
            AuthMode authMode = client.getAuthMode();
            if (authMode == AuthMode::SIMPLE_LOGIN) {
                std::string username = app.getAuthUsername();
                std::string password = app.getAuthPassword();
                if (!username.empty() && !password.empty()) {
                    brls::Logger::info("Reconnect: SIMPLE_LOGIN re-establishing session...");
                    if (client.login(username, password)) {
                        brls::Logger::info("Reconnect: SIMPLE_LOGIN session established");
                        app.getSettings().sessionCookie = client.getSessionCookie();
                    } else {
                        brls::Logger::warning("Reconnect: SIMPLE_LOGIN session login failed");
                        client.setSessionCookie("");
                        app.getSettings().sessionCookie = "";
                    }
                }
            } else if (authMode == AuthMode::UI_LOGIN && !client.getRefreshToken().empty()) {
                brls::Logger::info("Reconnect: Attempting to refresh JWT tokens...");
                if (client.refreshToken()) {
                    brls::Logger::info("Reconnect: Token refresh successful");
                    app.getSettings().accessToken = client.getAccessToken();
                    app.getSettings().refreshToken = client.getRefreshToken();
                    app.getSettings().sessionCookie = client.getSessionCookie();
                } else {
                    brls::Logger::warning("Reconnect: Token refresh failed, clearing stale tokens");
                    client.setTokens("", "");
                    app.getSettings().accessToken = "";
                    app.getSettings().refreshToken = "";
                }
            }

            // Step 2: Test connection with protected query (not just public endpoint)
            bool authValid = false;
            if (client.testConnection()) {
                brls::Logger::info("Reconnect: Server reachable, validating auth...");

                if (authMode == AuthMode::NONE) {
                    authValid = true;
                } else if (client.validateAuthWithProtectedQuery()) {
                    brls::Logger::info("Reconnect: Auth validated");
                    authValid = true;
                } else {
                    // Auth failed - try all auth modes automatically
                    brls::Logger::warning("Reconnect: Protected query failed, trying all modes...");
                    std::string username = app.getAuthUsername();
                    std::string password = app.getAuthPassword();

                    if (!username.empty() && !password.empty()) {
                        // Try fresh login with saved mode first
                        if (authMode == AuthMode::SIMPLE_LOGIN || authMode == AuthMode::UI_LOGIN) {
                            client.setAuthMode(authMode);
                            client.logout();
                            if (client.login(username, password) && client.validateAuthWithProtectedQuery()) {
                                brls::Logger::info("Reconnect: Fresh login succeeded with saved mode");
                                app.getSettings().accessToken = client.getAccessToken();
                                app.getSettings().refreshToken = client.getRefreshToken();
                                app.getSettings().sessionCookie = client.getSessionCookie();
                                app.saveSettings();
                                authValid = true;
                            }
                        }

                        if (!authValid) {
                            // Try all modes: UI_LOGIN, SIMPLE_LOGIN, BASIC_AUTH
                            AuthMode tryModes[] = { AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN, AuthMode::BASIC_AUTH };
                            for (AuthMode tryMode : tryModes) {
                                if (tryMode == authMode) continue;
                                brls::Logger::info("Reconnect: Trying auth mode {}", static_cast<int>(tryMode));
                                client.setAuthMode(tryMode);
                                client.logout();

                                if (tryMode == AuthMode::BASIC_AUTH) {
                                    client.setAuthCredentials(username, password);
                                } else {
                                    if (!client.login(username, password)) continue;
                                }

                                if (client.validateAuthWithProtectedQuery()) {
                                    brls::Logger::info("Reconnect: Auth succeeded with mode {}", static_cast<int>(tryMode));
                                    app.getSettings().authMode = static_cast<int>(tryMode);
                                    app.getSettings().accessToken = client.getAccessToken();
                                    app.getSettings().refreshToken = client.getRefreshToken();
                                    app.getSettings().sessionCookie = client.getSessionCookie();
                                    app.saveSettings();
                                    authValid = true;
                                    break;
                                }
                            }
                        }
                    }
                }
            } else {
                // testConnection failed - could be auth mode mismatch
                brls::Logger::warning("Reconnect: Server not reachable, trying auth fallback...");
                std::string username = app.getAuthUsername();
                std::string password = app.getAuthPassword();

                if (!username.empty() && !password.empty()) {
                    if (authMode == AuthMode::SIMPLE_LOGIN || authMode == AuthMode::UI_LOGIN) {
                        client.setAuthMode(authMode);
                        client.logout();
                        if (client.login(username, password) &&
                            client.testConnection() && client.validateAuthWithProtectedQuery()) {
                            brls::Logger::info("Reconnect: Fresh login succeeded");
                            app.getSettings().accessToken = client.getAccessToken();
                            app.getSettings().refreshToken = client.getRefreshToken();
                            app.getSettings().sessionCookie = client.getSessionCookie();
                            app.saveSettings();
                            authValid = true;
                        }
                    }

                    if (!authValid) {
                        AuthMode tryModes[] = { AuthMode::BASIC_AUTH, AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN, AuthMode::NONE };
                        for (AuthMode tryMode : tryModes) {
                            if (tryMode == authMode) continue;
                            brls::Logger::info("Reconnect: Trying auth mode {}", static_cast<int>(tryMode));
                            client.setAuthMode(tryMode);
                            client.logout();

                            if (tryMode == AuthMode::BASIC_AUTH) {
                                client.setAuthCredentials(username, password);
                            } else if (tryMode != AuthMode::NONE) {
                                if (!client.login(username, password)) continue;
                            }

                            if (client.testConnection() && client.validateAuthWithProtectedQuery()) {
                                brls::Logger::info("Reconnect: Succeeded with mode {}", static_cast<int>(tryMode));
                                app.getSettings().authMode = static_cast<int>(tryMode);
                                app.getSettings().accessToken = client.getAccessToken();
                                app.getSettings().refreshToken = client.getRefreshToken();
                                app.getSettings().sessionCookie = client.getSessionCookie();
                                app.saveSettings();
                                authValid = true;
                                break;
                            }
                        }
                    }

                    if (!authValid) {
                        // Restore original auth mode
                        client.setAuthMode(authMode);
                        if (authMode == AuthMode::BASIC_AUTH) {
                            client.setAuthCredentials(app.getAuthUsername(), app.getAuthPassword());
                        }
                    }
                }
            }

            // Step 3: Update state and sync
            brls::sync([authValid, connectionStatusLabel]() {
                Application& app = Application::getInstance();
                if (authValid) {
                    app.setConnected(true);
                    brls::Application::notify("Connected!");
                    if (connectionStatusLabel) {
                        connectionStatusLabel->setText("Status: Connected");
                    }

                    // Sync offline reading progress to server
                    DownloadsManager& dm = DownloadsManager::getInstance();
                    if (!dm.getDownloads().empty()) {
                        brls::Logger::info("Reconnect: Syncing offline reading progress...");
                        vitasuwayomi::asyncRun([]() {
                            DownloadsManager::getInstance().syncProgressToServer();
                        });
                    }

                    // Resume incomplete downloads
                    dm.resumeDownloadsIfNeeded();
                } else {
                    app.setConnected(false);
                    brls::Application::notify("Connection failed");
                    if (connectionStatusLabel) {
                        connectionStatusLabel->setText("Status: Offline");
                    }
                }
            });
        });
        return true;
    });
    m_contentBox->addView(reconnectCell);

    // Disconnect button
    auto* disconnectCell = new brls::DetailCell();
    disconnectCell->setText("Disconnect");
    disconnectCell->setDetailText("Disconnect from current server");
    disconnectCell->registerClickAction([this](brls::View* view) {
        onDisconnect();
        return true;
    });
    m_contentBox->addView(disconnectCell);

    // Check for updates (moved here from the former About section).
    auto* updateCell = new brls::DetailCell();
    updateCell->setText("Check for Updates");
    updateCell->setDetailText("Check GitHub for new releases");
    updateCell->registerClickAction([this](brls::View*) {
        checkForUpdates();
        return true;
    });
    m_contentBox->addView(updateCell);
}

void SettingsTab::createTrackingSection() {
    auto* header = new brls::Header();
    header->setTitle("Tracking");
    m_contentBox->addView(header);

    // "Login to Trackers" cell that fetches trackers and shows login/logout options
    auto* trackerCell = new brls::DetailCell();
    trackerCell->setText("Tracker Accounts");
    trackerCell->setDetailText("Login to MAL, AniList, etc.");
    trackerCell->registerClickAction([this](brls::View* view) {
        if (!Application::getInstance().isConnected()) {
            brls::Application::notify("Connect to server first");
            return true;
        }

        brls::Application::notify("Loading trackers...");

        asyncRun([this, aliveWeak = std::weak_ptr<bool>(m_alive)]() {
            SuwayomiClient& client = SuwayomiClient::getInstance();
            std::vector<Tracker> trackers;

            if (!client.fetchTrackers(trackers)) {
                brls::sync([]() {
                    brls::Application::notify("Failed to load trackers");
                });
                return;
            }

            brls::sync([this, trackers, aliveWeak]() {
                auto alive = aliveWeak.lock();
                if (!alive || !*alive) return;
                if (trackers.empty()) {
                    brls::Application::notify("No trackers available on server");
                    return;
                }

                // Build tracker options list with login status
                std::vector<std::string> options;
                for (const auto& t : trackers) {
                    std::string label = t.name;
                    if (t.isLoggedIn) {
                        label += " (Logged in)";
                    } else {
                        label += " (Not logged in)";
                    }
                    options.push_back(label);
                }

                std::function<void(int)> onTrackerSelected = [this, trackers](int selected) {
                    if (selected < 0 || selected >= static_cast<int>(trackers.size())) return;
                    const Tracker& tracker = trackers[selected];

                    brls::sync([this, tracker]() {
                        if (tracker.isLoggedIn) {
                            // Already logged in — offer logout
                            std::vector<std::string> actions = {"Logout", "Cancel"};
                            std::function<void(int)> onAction = [this, tracker](int action) {
                                if (action == 0) {
                                    // Logout
                                    int trackerId = tracker.id;
                                    std::string trackerName = tracker.name;
                                    asyncRun([trackerId, trackerName]() {
                                        bool ok = SuwayomiClient::getInstance().logoutTracker(trackerId);
                                        brls::sync([ok, trackerName]() {
                                            if (ok) {
                                                brls::Application::notify("Logged out of " + trackerName);
                                            } else {
                                                brls::Application::notify("Failed to log out of " + trackerName);
                                            }
                                        });
                                    });
                                }
                            };
                            std::vector<OptionRow> actionRows;
                            for (size_t i = 0; i < actions.size(); i++) {
                                bool sel = (i == 0);
                                actionRows.push_back({ sel ? "radio_checked.png" : "radio.png", actions[i], "", sel, false,
                                    [onAction, i]() { onAction(static_cast<int>(i)); } });
                            }
                            actionRows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
                            OptionsPopover::show("TRACKER", tracker.name + " (Logged in)", std::move(actionRows));
                        } else {
                            // Not logged in — show credential login dialog
                            // MangaUpdates uses username/password, others use OAuth
                            // On PS Vita we can only do credential-based login
                            int trackerId = tracker.id;
                            std::string trackerName = tracker.name;

                            brls::Application::getImeManager()->openForText(
                                [this, trackerId, trackerName](std::string username) {
                                    if (username.empty()) return;

                                    brls::Application::getImeManager()->openForText(
                                        [trackerId, trackerName, username](std::string password) {
                                            if (password.empty()) return;

                                            asyncRun([trackerId, trackerName, username, password]() {
                                                bool ok = SuwayomiClient::getInstance()
                                                    .loginTrackerCredentials(trackerId, username, password);
                                                brls::sync([ok, trackerName]() {
                                                    if (ok) {
                                                        brls::Application::notify("Logged in to " + trackerName);
                                                    } else {
                                                        brls::Application::notify("Login failed for " + trackerName);
                                                    }
                                                });
                                            });
                                        },
                                        "Password", "", 256, "");
                                },
                                "Username", "", 256, "");
                        }
                    });
                };

                std::vector<OptionRow> rows;
                for (size_t i = 0; i < options.size(); i++) {
                    bool sel = (i == 0);
                    rows.push_back({ sel ? "radio_checked.png" : "radio.png", options[i], "", sel, false,
                        [onTrackerSelected, i]() { onTrackerSelected(static_cast<int>(i)); } });
                }
                rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
                OptionsPopover::show("TRACKING", "Tracker Accounts", std::move(rows));
            });
        });
        return true;
    });
    m_contentBox->addView(trackerCell);
}

void SettingsTab::createUISection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("User Interface");
    m_contentBox->addView(header);

    // Theme selector (opens the choice popover)
    static const std::vector<std::string> kThemeNames = {
        "System", "Light", "Dark", "Tachiyomi",
        "Neon Vaporwave", "Catppuccin", "Nord", "Tako",
        "Midnight Dusk", "Green Apple", "Lavender",
        "Matrix", "Mocha", "Sunset", "Aurora",
        "Synthwave", "Ocean"
    };
    auto themeName = [](int i) -> std::string {
        return (i >= 0 && i < static_cast<int>(kThemeNames.size())) ? kThemeNames[i] : "System";
    };
    auto* themeCell = new brls::DetailCell();
    themeCell->setText("Theme");
    themeCell->setDetailText(themeName(static_cast<int>(settings.theme)));
    themeCell->registerClickAction([this, themeCell, themeName](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().theme);
        showChoicePopover("Theme", kThemeNames, cur,
            [this, themeCell, themeName](int index) {
                onThemeChanged(index);
                themeCell->setDetailText(themeName(index));
            });
        return true;
    });
    m_contentBox->addView(themeCell);

    // Debug logging toggle
    m_debugLogToggle = new brls::BooleanCell();
    m_debugLogToggle->init("Debug Logging", settings.debugLogging, [](bool value) {
        Application::getInstance().getSettings().debugLogging = value;
        Application::getInstance().applyLogLevel();
        Application::getInstance().saveSettings();
        brls::Application::notify(value ? "Debug logging enabled" : "Debug logging disabled");
    });
    m_contentBox->addView(m_debugLogToggle);

    // Performance overlay toggle - shows FPS, frame time, texture upload stats
    auto* perfToggle = new brls::BooleanCell();
    perfToggle->init("Performance Overlay", settings.showPerfOverlay, [](bool value) {
        Application::getInstance().getSettings().showPerfOverlay = value;
        PerfOverlay::getInstance().setEnabled(value);
        Application::getInstance().saveSettings();
        brls::Application::notify(value ? "Perf overlay enabled" : "Perf overlay disabled");
    });
    m_contentBox->addView(perfToggle);
}

void SettingsTab::createLibrarySection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Library");
    m_contentBox->addView(header);

    // Manage Categories cell
    auto* manageCategoriesCell = new brls::DetailCell();
    manageCategoriesCell->setText("Manage Categories");
    manageCategoriesCell->setDetailText("Create, edit, delete, reorder");
    manageCategoriesCell->registerClickAction([this](brls::View* view) {
        showCategoryManagementDialog();
        return true;
    });
    m_contentBox->addView(manageCategoriesCell);

    // Hide categories cell
    m_hideCategoriesCell = new brls::DetailCell();
    m_hideCategoriesCell->setText("Hidden Categories");

    // Show count of hidden categories
    size_t hiddenCount = settings.hiddenCategoryIds.size();
    m_hideCategoriesCell->setDetailText(std::to_string(hiddenCount) + " hidden");

    m_hideCategoriesCell->registerClickAction([this](brls::View* view) {
        showCategoryVisibilityDialog();
        return true;
    });
    m_contentBox->addView(m_hideCategoriesCell);

    // Grid display mode (opens the choice popover)
    static const std::vector<std::string> kDisplayModes =
        {"Grid (Cover + Title)", "Compact Grid (Covers Only)", "List View"};
    auto* displayModeCell = new brls::DetailCell();
    displayModeCell->setText("Display Mode");
    displayModeCell->setDetailText(kDisplayModes[static_cast<int>(settings.libraryDisplayMode) % kDisplayModes.size()]);
    displayModeCell->registerClickAction([this, displayModeCell](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().libraryDisplayMode);
        showChoicePopover("Display Mode", kDisplayModes, cur, [displayModeCell](int index) {
            Application::getInstance().getSettings().libraryDisplayMode = static_cast<LibraryDisplayMode>(index);
            Application::getInstance().saveSettings();
            displayModeCell->setDetailText(kDisplayModes[index]);
        });
        return true;
    });
    m_contentBox->addView(displayModeCell);

    // Grid size (opens the choice popover)
    static const std::vector<std::string> kGridSizes =
        {"Large (4 columns)", "Medium (6 columns)", "Small (8 columns)"};
    auto* gridSizeCell = new brls::DetailCell();
    gridSizeCell->setText("Grid Size");
    gridSizeCell->setDetailText(kGridSizes[static_cast<int>(settings.libraryGridSize) % kGridSizes.size()]);
    gridSizeCell->registerClickAction([this, gridSizeCell](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().libraryGridSize);
        showChoicePopover("Grid Size", kGridSizes, cur, [gridSizeCell](int index) {
            Application::getInstance().getSettings().libraryGridSize = static_cast<LibraryGridSize>(index);
            Application::getInstance().saveSettings();
            gridSizeCell->setDetailText(kGridSizes[index]);
        });
        return true;
    });
    m_contentBox->addView(gridSizeCell);

    // Library grouping (opens the choice popover)
    static const std::vector<std::string> kGroupModes =
        {"By Category (Tabs)", "By Source", "No Grouping (All Manga)"};
    auto* groupModeCell = new brls::DetailCell();
    groupModeCell->setText("Library Grouping");
    groupModeCell->setDetailText(kGroupModes[static_cast<int>(settings.libraryGroupMode) % kGroupModes.size()]);
    groupModeCell->registerClickAction([this, groupModeCell](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().libraryGroupMode);
        showChoicePopover("Library Grouping", kGroupModes, cur, [groupModeCell](int index) {
            Application::getInstance().getSettings().libraryGroupMode = static_cast<LibraryGroupMode>(index);
            Application::getInstance().saveSettings();
            groupModeCell->setDetailText(kGroupModes[index]);
            brls::Application::notify("Library grouping updated");
        });
        return true;
    });
    m_contentBox->addView(groupModeCell);

    // Default sort mode (opens the choice popover)
    static const std::vector<std::string> kSortModes =
        {"A-Z", "Z-A", "Most Unread", "Least Unread", "Recently Added (Newest)",
         "Recently Added (Oldest)", "Last Read", "Date Updated (Newest)",
         "Date Updated (Oldest)", "Total Chapters", "Downloaded Only"};
    auto* defaultSortCell = new brls::DetailCell();
    defaultSortCell->setText("Default Sort Mode");
    defaultSortCell->setDetailText(kSortModes[settings.defaultLibrarySortMode % static_cast<int>(kSortModes.size())]);
    defaultSortCell->registerClickAction([this, defaultSortCell](brls::View*) {
        const int cur = Application::getInstance().getSettings().defaultLibrarySortMode;
        showChoicePopover("Default Sort Mode", kSortModes, cur, [defaultSortCell](int index) {
            Application::getInstance().getSettings().defaultLibrarySortMode = index;
            Application::getInstance().saveSettings();
            defaultSortCell->setDetailText(kSortModes[index]);
        });
        return true;
    });
    m_contentBox->addView(defaultSortCell);

    // Cache Library Data toggle
    auto* cacheDataToggle = new brls::BooleanCell();
    cacheDataToggle->init("Cache Library Data", settings.cacheLibraryData, [&settings](bool value) {
        settings.cacheLibraryData = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cacheDataToggle);

    // Cache Cover Images toggle
    auto* cacheCoverToggle = new brls::BooleanCell();
    cacheCoverToggle->init("Cache Cover Images", settings.cacheCoverImages, [&settings](bool value) {
        settings.cacheCoverImages = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cacheCoverToggle);

    // Downloads Only Mode toggle
    auto* downloadsOnlyToggle = new brls::BooleanCell();
    downloadsOnlyToggle->init("Downloads Only Mode", settings.downloadsOnlyMode, [](bool value) {
        Application::getInstance().getSettings().downloadsOnlyMode = value;
        Application::getInstance().saveSettings();
        brls::Application::notify(value ? "Showing downloaded only" : "Showing all manga");
    });
    m_contentBox->addView(downloadsOnlyToggle);

    // Library Updates header
    auto* updatesHeader = new brls::Header();
    updatesHeader->setTitle("Library Updates");
    m_contentBox->addView(updatesHeader);

    // Update on start toggle
    auto* updateOnStartToggle = new brls::BooleanCell();
    updateOnStartToggle->init("Update Library on Start", settings.updateOnStart, [](bool value) {
        Application::getInstance().getSettings().updateOnStart = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(updateOnStartToggle);

    // Default category (opens the choice popover; options are fetched on click)
    m_defaultCategorySelector = new brls::DetailCell();
    m_defaultCategorySelector->setText("Default Category");
    m_defaultCategorySelector->setDetailText("Default (All)");
    m_defaultCategorySelector->registerClickAction([this](brls::View*) {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        std::vector<Category> categories;
        if (!client.fetchCategories(categories)) {
            brls::Application::notify("Failed to load categories");
            return true;
        }
        std::sort(categories.begin(), categories.end(),
            [](const Category& a, const Category& b) { return a.order < b.order; });

        auto names = std::make_shared<std::vector<std::string>>();
        auto ids   = std::make_shared<std::vector<int>>();
        names->push_back("Default (All)");
        ids->push_back(0);
        for (const auto& cat : categories) {
            if (cat.mangaCount > 0) { names->push_back(cat.name); ids->push_back(cat.id); }
        }

        int cur = 0;
        const int curId = Application::getInstance().getSettings().defaultCategoryId;
        for (size_t i = 0; i < ids->size(); i++) if ((*ids)[i] == curId) { cur = static_cast<int>(i); break; }

        showChoicePopover("Default Category", *names, cur, [this, names, ids](int index) {
            if (index < 0 || index >= static_cast<int>(ids->size())) return;
            Application::getInstance().getSettings().defaultCategoryId = (*ids)[index];
            Application::getInstance().saveSettings();
            if (m_defaultCategorySelector) m_defaultCategorySelector->setDetailText((*names)[index]);
        });
        return true;
    });
    m_contentBox->addView(m_defaultCategorySelector);

    // Load categories for the selector asynchronously
    refreshDefaultCategorySelector();

    // Display Badges header
    auto* badgesHeader = new brls::Header();
    badgesHeader->setTitle("Display Badges");
    m_contentBox->addView(badgesHeader);

    // Show unread badge toggle
    auto* showUnreadBadgeToggle = new brls::BooleanCell();
    showUnreadBadgeToggle->init("Show Unread Badge", settings.showUnreadBadge, [](bool value) {
        Application::getInstance().getSettings().showUnreadBadge = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(showUnreadBadgeToggle);

    // Clear Cache button
    auto* clearCacheCell = new brls::DetailCell();
    clearCacheCell->setText("Clear Cache");
    clearCacheCell->setDetailText("Delete cached data and images");
    clearCacheCell->registerClickAction([](brls::View* view) {
        std::vector<OptionRow> rows;
        rows.push_back({ "cross.png", "Clear", "", true, true, []() {
            LibraryCache::getInstance().clearAllCache();
            brls::Application::notify("Cache cleared");
        } });
        rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
        OptionsPopover::show("CONFIRM", "Clear all cached data?", std::move(rows));
        return true;
    });
    m_contentBox->addView(clearCacheCell);
}

void SettingsTab::showCategoryVisibilityDialog() {
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Category> categories;
    if (!client.fetchCategories(categories)) {
        brls::Application::notify("Failed to load categories");
        return;
    }
    std::sort(categories.begin(), categories.end(),
        [](const Category& a, const Category& b) { return a.order < b.order; });

    // Drop stale hidden IDs (categories that no longer exist on the server).
    auto& hiddenIds = Application::getInstance().getSettings().hiddenCategoryIds;
    std::set<int> validIds;
    for (const auto& cat : categories) validIds.insert(cat.id);
    bool cleaned = false;
    for (auto it = hiddenIds.begin(); it != hiddenIds.end(); ) {
        if (validIds.find(*it) == validIds.end()) { it = hiddenIds.erase(it); cleaned = true; }
        else ++it;
    }
    if (cleaned) Application::getInstance().saveSettings();

    if (categories.empty()) {
        brls::Application::notify("No categories available");
        return;
    }

    // One checkable row per category: checked = visible (not hidden). Toggling in
    // place adds/removes the category from the hidden set and updates the cell.
    std::vector<OptionRow> rows;
    for (const auto& cat : categories) {
        const int id = cat.id;
        OptionRow row;
        row.label     = cat.name;
        row.checkable = true;
        row.checked   = hiddenIds.find(id) == hiddenIds.end();
        row.action    = [this, id]() {
            auto& hid = Application::getInstance().getSettings().hiddenCategoryIds;
            if (hid.find(id) == hid.end()) hid.insert(id);   // was visible -> hide
            else                           hid.erase(id);    // was hidden  -> show
            Application::getInstance().saveSettings();
            if (m_hideCategoriesCell)
                m_hideCategoriesCell->setDetailText(std::to_string(hid.size()) + " hidden");
        };
        rows.push_back(std::move(row));
    }
    rows.push_back({ "back.png", "Done", "", false, false, [](){} });

    OptionsPopover::show("LIBRARY", "Hidden Categories", std::move(rows), nullptr, 5);
}

void SettingsTab::createReaderSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Reader (Defaults)");
    m_contentBox->addView(header);

    // Reading mode (opens the choice popover)
    static const std::vector<std::string> kReadingModes =
        {"Left to Right", "Right to Left (Manga)", "Vertical", "Webtoon"};
    m_readingModeSelector = new brls::DetailCell();
    m_readingModeSelector->setText("Reading Mode");
    m_readingModeSelector->setDetailText(kReadingModes[static_cast<int>(settings.readingMode) % kReadingModes.size()]);
    m_readingModeSelector->registerClickAction([this](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().readingMode);
        showChoicePopover("Reading Mode", kReadingModes, cur, [this](int index) {
            Application::getInstance().getSettings().readingMode = static_cast<ReadingMode>(index);
            Application::getInstance().saveSettings();
            if (m_readingModeSelector) m_readingModeSelector->setDetailText(kReadingModes[index]);
        });
        return true;
    });
    m_contentBox->addView(m_readingModeSelector);

    // Page scale (opens the choice popover)
    static const std::vector<std::string> kPageScales =
        {"Fit Screen", "Fit Width", "Fit Height", "Original Size (1:1)"};
    m_pageScaleModeSelector = new brls::DetailCell();
    m_pageScaleModeSelector->setText("Page Scale");
    m_pageScaleModeSelector->setDetailText(kPageScales[static_cast<int>(settings.pageScaleMode) % kPageScales.size()]);
    m_pageScaleModeSelector->registerClickAction([this](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().pageScaleMode);
        showChoicePopover("Page Scale", kPageScales, cur, [this](int index) {
            Application::getInstance().getSettings().pageScaleMode = static_cast<PageScaleMode>(index);
            Application::getInstance().saveSettings();
            if (m_pageScaleModeSelector) m_pageScaleModeSelector->setDetailText(kPageScales[index]);
        });
        return true;
    });
    m_contentBox->addView(m_pageScaleModeSelector);

    // Default rotation (opens the choice popover)
    static const std::vector<std::string> kRotations =
        {"0° (Normal)", "90° (Clockwise)", "180° (Upside Down)", "270° (Counter-Clockwise)"};
    static const int kRotationValues[] = {0, 90, 180, 270};
    auto rotationIndexOf = [](int deg) { for (int i = 0; i < 4; i++) if (kRotationValues[i] == deg) return i; return 0; };
    auto* rotationCell = new brls::DetailCell();
    rotationCell->setText("Default Rotation");
    rotationCell->setDetailText(kRotations[rotationIndexOf(settings.imageRotation)]);
    rotationCell->registerClickAction([this, rotationIndexOf, rotationCell](brls::View*) {
        const int cur = rotationIndexOf(Application::getInstance().getSettings().imageRotation);
        showChoicePopover("Default Rotation", kRotations, cur, [rotationCell](int index) {
            Application::getInstance().getSettings().imageRotation = kRotationValues[index];
            Application::getInstance().saveSettings();
            rotationCell->setDetailText(kRotations[index]);
        });
        return true;
    });
    m_contentBox->addView(rotationCell);

    // Reader background (opens the choice popover)
    static const std::vector<std::string> kReaderBgs = {"Black", "White", "Gray"};
    m_readerBgSelector = new brls::DetailCell();
    m_readerBgSelector->setText("Background");
    m_readerBgSelector->setDetailText(kReaderBgs[static_cast<int>(settings.readerBackground) % kReaderBgs.size()]);
    m_readerBgSelector->registerClickAction([this](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().readerBackground);
        showChoicePopover("Background", kReaderBgs, cur, [this](int index) {
            Application::getInstance().getSettings().readerBackground = static_cast<ReaderBackground>(index);
            Application::getInstance().saveSettings();
            if (m_readerBgSelector) m_readerBgSelector->setDetailText(kReaderBgs[index]);
        });
        return true;
    });
    m_contentBox->addView(m_readerBgSelector);

    // Keep screen on toggle
    auto* keepScreenOnToggle = new brls::BooleanCell();
    keepScreenOnToggle->init("Keep Screen On", settings.keepScreenOn, [&settings](bool value) {
        settings.keepScreenOn = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(keepScreenOnToggle);

    // Show page number toggle
    auto* showPageNumToggle = new brls::BooleanCell();
    showPageNumToggle->init("Show Page Number", settings.showPageNumber, [&settings](bool value) {
        settings.showPageNumber = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(showPageNumToggle);

    // Tap to navigate toggle
    auto* tapNavToggle = new brls::BooleanCell();
    tapNavToggle->init("Tap to Navigate", settings.tapToNavigate, [&settings](bool value) {
        settings.tapToNavigate = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(tapNavToggle);

    // Go to end of prev chapter toggle
    auto* prevChapterEndToggle = new brls::BooleanCell();
    prevChapterEndToggle->init("Go to End on Prev Chapter", settings.goToEndOnPrevChapter, [&settings](bool value) {
        settings.goToEndOnPrevChapter = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(prevChapterEndToggle);

    // Webtoon section header
    auto* webtoonHeader = new brls::Header();
    webtoonHeader->setTitle("Webtoon / Long Strip");
    m_contentBox->addView(webtoonHeader);

    // Crop borders toggle
    auto* cropBordersToggle = new brls::BooleanCell();
    cropBordersToggle->init("Crop Borders", settings.cropBorders, [&settings](bool value) {
        settings.cropBorders = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(cropBordersToggle);

    // Auto-detect webtoon toggle
    auto* webtoonDetectToggle = new brls::BooleanCell();
    webtoonDetectToggle->init("Auto-Detect Webtoon", settings.webtoonDetection, [&settings](bool value) {
        settings.webtoonDetection = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(webtoonDetectToggle);

    // Side padding selector
    static const std::vector<std::string> kSidePaddings = {"None", "5%", "10%", "15%", "20%"};
    auto* paddingCell = new brls::DetailCell();
    paddingCell->setText("Side Padding");
    paddingCell->setDetailText(kSidePaddings[(settings.webtoonSidePadding / 5) % static_cast<int>(kSidePaddings.size())]);
    paddingCell->registerClickAction([this, paddingCell](brls::View*) {
        const int cur = Application::getInstance().getSettings().webtoonSidePadding / 5;
        showChoicePopover("Side Padding", kSidePaddings, cur, [paddingCell](int index) {
            Application::getInstance().getSettings().webtoonSidePadding = index * 5;
            Application::getInstance().saveSettings();
            paddingCell->setDetailText(kSidePaddings[index]);
        });
        return true;
    });
    m_contentBox->addView(paddingCell);

    // Reverse mouse scroll
    auto* reverseScrollToggle = new brls::BooleanCell();
    reverseScrollToggle->init("Reverse Mouse Scroll", settings.reverseMouseScroll, [&settings](bool value) {
        settings.reverseMouseScroll = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(reverseScrollToggle);
}

void SettingsTab::createDownloadsSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Downloads");
    m_contentBox->addView(header);

    // Download location (opens the choice popover)
    static const std::vector<std::string> kDownloadModes = {"Server Only", "Local Only", "Both"};
    auto* downloadModeCell = new brls::DetailCell();
    downloadModeCell->setText("Download Location");
    downloadModeCell->setDetailText(kDownloadModes[static_cast<int>(settings.downloadMode) % kDownloadModes.size()]);
    downloadModeCell->registerClickAction([this, downloadModeCell](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().downloadMode);
        showChoicePopover("Download Location", kDownloadModes, cur, [downloadModeCell](int index) {
            Application::getInstance().getSettings().downloadMode = static_cast<DownloadMode>(index);
            Application::getInstance().saveSettings();
            downloadModeCell->setDetailText(kDownloadModes[index]);
        });
        return true;
    });
    m_contentBox->addView(downloadModeCell);

    // Download quality (opens the choice popover)
    static const std::vector<std::string> kQualities =
        {"Original", "High (1280px)", "Medium (960px)", "Low (720px)"};
    auto* qualityCell = new brls::DetailCell();
    qualityCell->setText("Download Quality");
    qualityCell->setDetailText(kQualities[static_cast<int>(settings.downloadQuality) % kQualities.size()]);
    qualityCell->registerClickAction([this, qualityCell](brls::View*) {
        const int cur = static_cast<int>(Application::getInstance().getSettings().downloadQuality);
        showChoicePopover("Download Quality", kQualities, cur, [qualityCell](int index) {
            Application::getInstance().getSettings().downloadQuality = static_cast<DownloadQuality>(index);
            Application::getInstance().saveSettings();
            qualityCell->setDetailText(kQualities[index]);
        });
        return true;
    });
    m_contentBox->addView(qualityCell);

    // Auto download new chapters toggle
    auto* autoDownloadToggle = new brls::BooleanCell();
    autoDownloadToggle->init("Auto-Download New Chapters", settings.autoDownloadChapters, [&settings](bool value) {
        settings.autoDownloadChapters = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(autoDownloadToggle);

    // Delete after read toggle
    auto* deleteAfterReadToggle = new brls::BooleanCell();
    deleteAfterReadToggle->init("Delete After Reading", settings.deleteAfterRead, [&settings](bool value) {
        settings.deleteAfterRead = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(deleteAfterReadToggle);

    // Auto-resume downloads toggle
    auto* autoResumeToggle = new brls::BooleanCell();
    autoResumeToggle->init("Resume Downloads on Startup", settings.autoResumeDownloads, [&settings](bool value) {
        settings.autoResumeDownloads = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(autoResumeToggle);

    // Page cache toggle (caches decoded pages to disk for faster re-loading)
    auto* pageCacheToggle = new brls::BooleanCell();
    pageCacheToggle->init("Cache Read Pages", settings.pageCacheEnabled, [&settings](bool value) {
        settings.pageCacheEnabled = value;
        Application::getInstance().saveSettings();
        if (!value) {
            // Clear existing page cache when disabled
            LibraryCache::getInstance().clearPageCache();
            brls::Application::notify("Page cache cleared");
        }
    });
    m_contentBox->addView(pageCacheToggle);

    // Sync progress now button
    auto* syncNowCell = new brls::DetailCell();
    syncNowCell->setText("Sync Progress Now");
    syncNowCell->setDetailText("Bidirectional sync with server");
    syncNowCell->registerClickAction([](brls::View* view) {
        if (!Application::getInstance().isConnected()) {
            brls::Application::notify("Not connected to server");
            return true;
        }
        brls::Application::notify("Syncing progress...");
        vitasuwayomi::asyncRun([]() {
            DownloadsManager::getInstance().syncProgressFromServer();
            brls::sync([]() {
                brls::Application::notify("Progress synced with server");
            });
        });
        return true;
    });
    m_contentBox->addView(syncNowCell);

    // Clear all downloads
    m_clearDownloadsCell = new brls::DetailCell();
    m_clearDownloadsCell->setText("Clear All Downloads");
    auto downloads = DownloadsManager::getInstance().getDownloads();
    m_clearDownloadsCell->setDetailText(std::to_string(downloads.size()) + " manga");
    m_clearDownloadsCell->registerClickAction([this](brls::View* view) {
        std::vector<OptionRow> rows;
        rows.push_back({ "cross.png", "Delete All", "", true, true, [this]() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
            }
            if (m_clearDownloadsCell) {
                m_clearDownloadsCell->setDetailText("0 manga");
            }
            brls::Application::notify("All downloads deleted");
        } });
        rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
        OptionsPopover::show("CONFIRM", "Delete all downloaded content?", std::move(rows));
        return true;
    });
    m_contentBox->addView(m_clearDownloadsCell);

    // Downloads storage path info
    auto* pathLabel = new brls::Label();
    pathLabel->setText("Storage: " + DownloadsManager::getInstance().getDownloadsPath());
    pathLabel->setFontSize(14);
    pathLabel->setMarginLeft(16);
    pathLabel->setMarginTop(8);
    m_contentBox->addView(pathLabel);
}

void SettingsTab::createBrowseSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // Section header
    auto* header = new brls::Header();
    header->setTitle("Browse / Sources");
    m_contentBox->addView(header);

    // Source language filter
    m_languageFilterCell = new brls::DetailCell();
    m_languageFilterCell->setText("Source Languages");

    // Build description of currently enabled languages
    updateLanguageFilterCellText();

    m_languageFilterCell->registerClickAction([this](brls::View* view) {
        showLanguageFilterDialog();
        return true;
    });
    m_contentBox->addView(m_languageFilterCell);

    // Show NSFW sources toggle
    auto* nsfwToggle = new brls::BooleanCell();
    nsfwToggle->init("Show NSFW Sources", settings.showNsfwSources, [&settings](bool value) {
        settings.showNsfwSources = value;
        Application::getInstance().saveSettings();
    });
    m_contentBox->addView(nsfwToggle);

    // Max search history (opens the choice popover)
    static const std::vector<std::string> kHistoryOptions = {"5", "10", "15", "20", "30", "50", "100"};
    static const int kHistoryValues[] = {5, 10, 15, 20, 30, 50, 100};
    auto historyIndexOf = [](int v) {
        for (int i = 0; i < static_cast<int>(kHistoryOptions.size()); i++) if (kHistoryValues[i] == v) return i;
        return 3; // default 20
    };
    auto* searchHistoryCell = new brls::DetailCell();
    searchHistoryCell->setText("Max Search History");
    searchHistoryCell->setDetailText(kHistoryOptions[historyIndexOf(settings.maxSearchHistory)]);
    searchHistoryCell->registerClickAction([this, historyIndexOf, searchHistoryCell](brls::View*) {
        const int cur = historyIndexOf(Application::getInstance().getSettings().maxSearchHistory);
        showChoicePopover("Max Search History", kHistoryOptions, cur, [searchHistoryCell](int index) {
            Application::getInstance().getSettings().maxSearchHistory = kHistoryValues[index];
            Application::getInstance().saveSettings();
            searchHistoryCell->setDetailText(kHistoryOptions[index]);
        });
        return true;
    });
    m_contentBox->addView(searchHistoryCell);
}

void SettingsTab::createSyncYomiSection() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    auto* header = new brls::Header();
    header->setTitle("SyncYomi");
    m_contentBox->addView(header);

    auto* descLabel = new brls::Label();
    descLabel->setText("Sync library data with a SyncYomi server");
    descLabel->setFontSize(14);
    descLabel->setMarginLeft(16);
    descLabel->setMarginBottom(8);
    descLabel->setTextColor(Application::getInstance().getSubtitleColor());
    m_contentBox->addView(descLabel);

    auto alive = m_alive;

    // Enable/disable toggle
    auto* enableToggle = new brls::BooleanCell();
    enableToggle->init("Enable SyncYomi", settings.syncYomiEnabled, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncYomiEnabled = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                value, s.syncYomiHost, s.syncYomiApiKey,
                s.syncDataManga, s.syncDataChapters, s.syncDataTracking,
                s.syncDataHistory, s.syncDataCategories);
        });
    });
    m_contentBox->addView(enableToggle);

    // Host URL
    auto* hostCell = new brls::DetailCell();
    hostCell->setText("SyncYomi Host");
    hostCell->setDetailText(settings.syncYomiHost.empty() ? "Not configured" : settings.syncYomiHost);
    hostCell->registerClickAction([this, hostCell, alive](brls::View* view) {
        showUrlInputDialog("SyncYomi Host URL", "https://sync.example.com",
            Application::getInstance().getSettings().syncYomiHost,
            [hostCell, alive](const std::string& newUrl) {
                if (!*alive) return;
                AppSettings& s = Application::getInstance().getSettings();
                s.syncYomiHost = newUrl;
                brls::sync([hostCell, newUrl]() {
                    hostCell->setDetailText(newUrl.empty() ? "Not configured" : newUrl);
                });
                SuwayomiClient::getInstance().updateSyncYomiSettings(
                    s.syncYomiEnabled, newUrl, s.syncYomiApiKey,
                    s.syncDataManga, s.syncDataChapters, s.syncDataTracking,
                    s.syncDataHistory, s.syncDataCategories);
            });
        return true;
    });
    m_contentBox->addView(hostCell);

    // API Key
    auto* apiKeyCell = new brls::DetailCell();
    apiKeyCell->setText("API Key");
    apiKeyCell->setDetailText(settings.syncYomiApiKey.empty() ? "Not set" : "••••••••");
    apiKeyCell->registerClickAction([apiKeyCell, alive](brls::View* view) {
        std::string currentKey = Application::getInstance().getSettings().syncYomiApiKey;
        brls::Application::getImeManager()->openForText([apiKeyCell, alive](std::string text) {
            if (!*alive) return;
            AppSettings& s = Application::getInstance().getSettings();
            s.syncYomiApiKey = text;
            brls::sync([apiKeyCell, text]() {
                apiKeyCell->setDetailText(text.empty() ? "Not set" : "••••••••");
            });
            vitasuwayomi::asyncRun([text]() {
                AppSettings& s = Application::getInstance().getSettings();
                SuwayomiClient::getInstance().updateSyncYomiSettings(
                    s.syncYomiEnabled, s.syncYomiHost, text,
                    s.syncDataManga, s.syncDataChapters, s.syncDataTracking,
                    s.syncDataHistory, s.syncDataCategories);
            });
        }, "Enter API Key", "", 256, currentKey, 0);
        return true;
    });
    m_contentBox->addView(apiKeyCell);

    // Data sync toggles
    auto* syncMangaToggle = new brls::BooleanCell();
    syncMangaToggle->init("Sync Manga", settings.syncDataManga, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncDataManga = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                s.syncYomiEnabled, s.syncYomiHost, s.syncYomiApiKey,
                value, s.syncDataChapters, s.syncDataTracking,
                s.syncDataHistory, s.syncDataCategories);
        });
    });
    m_contentBox->addView(syncMangaToggle);

    auto* syncChaptersToggle = new brls::BooleanCell();
    syncChaptersToggle->init("Sync Chapters", settings.syncDataChapters, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncDataChapters = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                s.syncYomiEnabled, s.syncYomiHost, s.syncYomiApiKey,
                s.syncDataManga, value, s.syncDataTracking,
                s.syncDataHistory, s.syncDataCategories);
        });
    });
    m_contentBox->addView(syncChaptersToggle);

    auto* syncTrackingToggle = new brls::BooleanCell();
    syncTrackingToggle->init("Sync Tracking", settings.syncDataTracking, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncDataTracking = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                s.syncYomiEnabled, s.syncYomiHost, s.syncYomiApiKey,
                s.syncDataManga, s.syncDataChapters, value,
                s.syncDataHistory, s.syncDataCategories);
        });
    });
    m_contentBox->addView(syncTrackingToggle);

    auto* syncHistoryToggle = new brls::BooleanCell();
    syncHistoryToggle->init("Sync History", settings.syncDataHistory, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncDataHistory = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                s.syncYomiEnabled, s.syncYomiHost, s.syncYomiApiKey,
                s.syncDataManga, s.syncDataChapters, s.syncDataTracking,
                value, s.syncDataCategories);
        });
    });
    m_contentBox->addView(syncHistoryToggle);

    auto* syncCategoriesToggle = new brls::BooleanCell();
    syncCategoriesToggle->init("Sync Categories", settings.syncDataCategories, [alive](bool value) {
        if (!*alive) return;
        AppSettings& s = Application::getInstance().getSettings();
        s.syncDataCategories = value;
        vitasuwayomi::asyncRun([value]() {
            AppSettings& s = Application::getInstance().getSettings();
            SuwayomiClient::getInstance().updateSyncYomiSettings(
                s.syncYomiEnabled, s.syncYomiHost, s.syncYomiApiKey,
                s.syncDataManga, s.syncDataChapters, s.syncDataTracking,
                s.syncDataHistory, value);
        });
    });
    m_contentBox->addView(syncCategoriesToggle);

    // Trigger sync button
    auto* syncNowCell = new brls::DetailCell();
    syncNowCell->setText("Sync Now");
    syncNowCell->setDetailText("Trigger SyncYomi sync");
    syncNowCell->registerClickAction([alive](brls::View* view) {
        if (!Application::getInstance().isConnected()) {
            brls::Application::notify("Not connected to server");
            return true;
        }
        brls::Application::notify("Starting sync...");
        vitasuwayomi::asyncRun([alive]() {
            std::string result;
            SuwayomiClient::getInstance().triggerSync(result);
            brls::sync([result, alive]() {
                if (!*alive) return;
                if (result == "SUCCESS") {
                    brls::Application::notify("Sync started successfully");
                } else if (result == "SYNC_IN_PROGRESS") {
                    brls::Application::notify("Sync already in progress");
                } else if (result == "SYNC_DISABLED") {
                    brls::Application::notify("SyncYomi is not enabled");
                } else {
                    brls::Application::notify("Sync failed: " + result);
                }
            });
        });
        return true;
    });
    m_contentBox->addView(syncNowCell);

    // Fetch current server-side settings and update all cells
    vitasuwayomi::asyncRun([alive, enableToggle, hostCell, apiKeyCell,
                            syncMangaToggle, syncChaptersToggle, syncTrackingToggle,
                            syncHistoryToggle, syncCategoriesToggle]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();

        bool enabled = false;
        std::string host, apiKey;
        bool dataManga = true, dataChapters = true, dataTracking = true;
        bool dataHistory = true, dataCategories = true;

        if (client.fetchSyncYomiSettings(enabled, host, apiKey,
                dataManga, dataChapters, dataTracking, dataHistory, dataCategories)) {
            AppSettings& s = Application::getInstance().getSettings();
            s.syncYomiEnabled = enabled;
            s.syncYomiHost = host;
            s.syncYomiApiKey = apiKey;
            s.syncDataManga = dataManga;
            s.syncDataChapters = dataChapters;
            s.syncDataTracking = dataTracking;
            s.syncDataHistory = dataHistory;
            s.syncDataCategories = dataCategories;

            brls::sync([alive, enabled, host, apiKey, dataManga, dataChapters,
                        dataTracking, dataHistory, dataCategories,
                        enableToggle, hostCell, apiKeyCell,
                        syncMangaToggle, syncChaptersToggle, syncTrackingToggle,
                        syncHistoryToggle, syncCategoriesToggle]() {
                if (!*alive) return;
                enableToggle->setOn(enabled, false);
                hostCell->setDetailText(host.empty() ? "Not configured" : host);
                apiKeyCell->setDetailText(apiKey.empty() ? "Not set" : "••••••••");
                syncMangaToggle->setOn(dataManga, false);
                syncChaptersToggle->setOn(dataChapters, false);
                syncTrackingToggle->setOn(dataTracking, false);
                syncHistoryToggle->setOn(dataHistory, false);
                syncCategoriesToggle->setOn(dataCategories, false);
            });
        }
    });
}

void SettingsTab::updateLanguageFilterCellText() {
    if (!m_languageFilterCell) return;

    const auto& settings = Application::getInstance().getSettings();
    std::string langDesc;
    if (settings.enabledSourceLanguages.empty()) {
        langDesc = "All languages";
    } else {
        for (const auto& lang : settings.enabledSourceLanguages) {
            if (!langDesc.empty()) langDesc += ", ";
            langDesc += lang;
        }
        // Truncate if too long
        if (langDesc.length() > 30) {
            langDesc = langDesc.substr(0, 27) + "...";
        }
    }
    m_languageFilterCell->setDetailText(langDesc);
}

void SettingsTab::showLanguageFilterDialog() {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    // All supported languages (ISO 639-1 codes used by Tachiyomi/Suwayomi extensions)
    std::vector<std::pair<std::string, std::string>> languages = {
        {"all", "All Languages"},
        {"multi", "Multi-language"},
        {"en", "English"},
        {"ja", "Japanese"},
        {"ko", "Korean"},
        {"zh", "Chinese"},
        {"zh-Hans", "Chinese (Simplified)"},
        {"zh-Hant", "Chinese (Traditional)"},
        {"es", "Spanish"},
        {"es-419", "Spanish (Latin America)"},
        {"pt", "Portuguese"},
        {"pt-BR", "Portuguese (Brazil)"},
        {"fr", "French"},
        {"de", "German"},
        {"it", "Italian"},
        {"ru", "Russian"},
        {"ar", "Arabic"},
        {"id", "Indonesian"},
        {"th", "Thai"},
        {"vi", "Vietnamese"},
        {"tr", "Turkish"},
        {"pl", "Polish"},
        {"uk", "Ukrainian"},
        {"nl", "Dutch"},
        {"hi", "Hindi"},
        {"bn", "Bengali"},
        {"ms", "Malay"},
        {"my", "Burmese"},
        {"bg", "Bulgarian"},
        {"ca", "Catalan"},
        {"cs", "Czech"},
        {"da", "Danish"},
        {"el", "Greek"},
        {"fa", "Persian"},
        {"fi", "Finnish"},
        {"he", "Hebrew"},
        {"hu", "Hungarian"},
        {"lt", "Lithuanian"},
        {"mn", "Mongolian"},
        {"no", "Norwegian"},
        {"ro", "Romanian"},
        {"sr", "Serbian"},
        {"sv", "Swedish"},
        {"ta", "Tamil"},
        {"te", "Telugu"},
        {"af", "Afrikaans"},
        {"sq", "Albanian"},
        {"am", "Amharic"},
        {"hy", "Armenian"},
        {"az", "Azerbaijani"},
        {"eu", "Basque"},
        {"be", "Belarusian"},
        {"bs", "Bosnian"},
        {"hr", "Croatian"},
        {"et", "Estonian"},
        {"fil", "Filipino"},
        {"gl", "Galician"},
        {"ka", "Georgian"},
        {"gu", "Gujarati"},
        {"is", "Icelandic"},
        {"kn", "Kannada"},
        {"kk", "Kazakh"},
        {"km", "Khmer"},
        {"lo", "Lao"},
        {"lv", "Latvian"},
        {"mk", "Macedonian"},
        {"ml", "Malayalam"},
        {"mr", "Marathi"},
        {"ne", "Nepali"},
        {"pa", "Punjabi"},
        {"si", "Sinhala"},
        {"sk", "Slovak"},
        {"sl", "Slovenian"},
        {"sw", "Swahili"},
        {"ur", "Urdu"},
        {"uz", "Uzbek"}
    };

    if (languages.empty()) return;

    // One checkable row per language: "all" is checked when no specific
    // languages are enabled; toggling it clears the set. Others toggle their
    // membership. Toggles apply in place and refresh the cell summary.
    std::vector<OptionRow> rows;
    for (const auto& [code, name] : languages) {
        const std::string langCode = code;
        OptionRow row;
        row.label     = name;
        row.sub       = code;
        row.checkable = true;
        row.checked   = (code == "all" && settings.enabledSourceLanguages.empty()) ||
                        (code != "all" && settings.enabledSourceLanguages.count(code) > 0);
        row.action    = [this, langCode]() {
            AppSettings& s = Application::getInstance().getSettings();
            if (langCode == "all") {
                s.enabledSourceLanguages.clear();
            } else if (s.enabledSourceLanguages.count(langCode) > 0) {
                s.enabledSourceLanguages.erase(langCode);
            } else {
                s.enabledSourceLanguages.insert(langCode);
            }
            Application::getInstance().saveSettings();
            updateLanguageFilterCellText();
        };
        rows.push_back(std::move(row));
    }
    rows.push_back({ "back.png", "Done", "", false, false, [](){} });

    OptionsPopover::show("BROWSE", "Source Languages", std::move(rows), nullptr, 5);
}

void SettingsTab::createAboutSection() {
    // Section header
    auto* header = new brls::Header();
    header->setTitle("About");
    m_contentBox->addView(header);

    // Version info
    auto* versionCell = new brls::DetailCell();
    versionCell->setText("Version");
    versionCell->setDetailText(VITA_SUWAYOMI_VERSION);
    m_contentBox->addView(versionCell);

    // Check for Updates button
    auto* updateCell = new brls::DetailCell();
    updateCell->setText("Check for Updates");
    updateCell->setDetailText("Check GitHub for new releases");
    updateCell->registerClickAction([this](brls::View* view) {
        checkForUpdates();
        return true;
    });
    m_contentBox->addView(updateCell);

    // App description
    auto* descLabel = new brls::Label();
    descLabel->setText("VitaSuwayomi - Manga Reader for PlayStation Vita");
    descLabel->setFontSize(16);
    descLabel->setMarginLeft(16);
    descLabel->setMarginTop(8);
    m_contentBox->addView(descLabel);

    // Server info
    auto* serverInfoLabel = new brls::Label();
    serverInfoLabel->setText("Connects to Suwayomi-Server");
    serverInfoLabel->setFontSize(14);
    serverInfoLabel->setMarginLeft(16);
    serverInfoLabel->setMarginTop(4);
    m_contentBox->addView(serverInfoLabel);

    // Credit
    auto* creditLabel = new brls::Label();
    creditLabel->setText("UI powered by Borealis");
    creditLabel->setFontSize(14);
    creditLabel->setMarginLeft(16);
    creditLabel->setMarginTop(4);
    creditLabel->setMarginBottom(20);
    m_contentBox->addView(creditLabel);
}

void SettingsTab::onDisconnect() {
    std::vector<OptionRow> rows;
    rows.push_back({ "cross.png", "Disconnect", "", true, true, [this]() {
        // Clear server connection
        Application::getInstance().setServerUrl("");
        Application::getInstance().setConnected(false);
        Application::getInstance().saveSettings();

        // Go back to login
        Application::getInstance().pushLoginActivity();
    } });
    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
    OptionsPopover::show("CONFIRM", "Disconnect from server?", std::move(rows));
}

void SettingsTab::onThemeChanged(int index) {
    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    settings.theme = static_cast<AppTheme>(index);
    app.applyTheme();
    app.saveSettings();
}

void SettingsTab::showUrlInputDialog(const std::string& title, const std::string& hint,
                                      const std::string& currentValue,
                                      std::function<void(const std::string&)> callback) {
    std::vector<OptionRow> rows;

    // Edit — open the on-screen keyboard to enter a new URL. The current value
    // (if any) is shown as the row's trailing sub-value.
    rows.push_back({ "web.png", "Edit", currentValue.empty() ? "" : currentValue, true, false,
        [callback, currentValue]() {
            // Open on-screen keyboard for input (popover fades before this runs)
            brls::Application::getImeManager()->openForText([callback](std::string text) {
                // Validate URL format
                if (!text.empty()) {
                    // Remove trailing slashes
                    while (!text.empty() && text.back() == '/') {
                        text.pop_back();
                    }
                    // Basic URL validation - should start with http:// or https://
                    if (text.find("http://") != 0 && text.find("https://") != 0) {
                        brls::Application::notify("URL must start with http:// or https://");
                        return;
                    }
                }
                callback(text);
            }, "Enter Server URL", "", 256, currentValue, 0);
        } });

    rows.push_back({ "cross.png", "Clear", "", false, true, [callback]() {
        callback("");
        brls::Application::notify("URL cleared");
    } });

    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });

    OptionsPopover::show("", title, std::move(rows));
}

void SettingsTab::updateServerLabel() {
    if (m_serverLabel) {
        Application& app = Application::getInstance();
        std::string activeUrl = app.getServerUrl();
        if (activeUrl.empty()) {
            activeUrl = app.getActiveServerUrl();
        }
        m_serverLabel->setText("Active: " + (activeUrl.empty() ? "Not connected" : activeUrl));
    }
}

namespace {

// Fixed dark palette for the Network Test dialog (per the design spec).
namespace ntcol {
    inline NVGcolor scrim()      { return nvgRGBA(10, 9, 14, 140); }
    inline NVGcolor panel()      { return nvgRGB(28, 28, 31); }
    inline NVGcolor border()     { return nvgRGBA(255, 255, 255, 20); }
    inline NVGcolor success()    { return nvgRGB(78, 204, 163); }
    inline NVGcolor danger()     { return nvgRGB(217, 107, 107); }
    inline NVGcolor accent()     { return nvgRGB(100, 180, 255); }
    inline NVGcolor accentInk()  { return nvgRGB(13, 34, 54); }
    inline NVGcolor muted()      { return nvgRGB(139, 139, 147); }
    inline NVGcolor body()       { return nvgRGB(197, 198, 208); }
    inline NVGcolor heading()    { return nvgRGB(231, 231, 234); }
    inline NVGcolor chipBg()     { return nvgRGB(35, 35, 38); }
    inline NVGcolor chipBorder() { return nvgRGBA(255, 255, 255, 15); }
    inline NVGcolor rowLine()    { return nvgRGBA(255, 255, 255, 13); }
    inline NVGcolor okSub()      { return nvgRGB(159, 217, 197); }
    inline NVGcolor checkInk()   { return nvgRGB(8, 19, 13); }
    inline NVGcolor bannerOk()   { return nvgRGBA(78, 204, 163, 28); }
    inline NVGcolor bannerBad()  { return nvgRGBA(217, 107, 107, 28); }
}

// Check / X glyph drawn with nanovg (the bundled font has no vector check mark).
enum class NtGlyph { Check, Cross };
class NtGlyphIcon : public brls::Box {
public:
    NtGlyphIcon(NtGlyph g, NVGcolor color) : m_glyph(g), m_color(color) {}
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
        brls::Box::draw(vg, x, y, w, h, style, ctx);
        const float s  = (w < h) ? w : h;
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        nvgBeginPath(vg);
        nvgStrokeColor(vg, m_color);
        nvgStrokeWidth(vg, s * 0.12f);
        nvgLineCap(vg, NVG_ROUND);
        nvgLineJoin(vg, NVG_ROUND);
        if (m_glyph == NtGlyph::Check) {
            nvgMoveTo(vg, cx - s * 0.24f, cy + s * 0.02f);
            nvgLineTo(vg, cx - s * 0.06f, cy + s * 0.20f);
            nvgLineTo(vg, cx + s * 0.26f, cy - s * 0.18f);
        } else {
            nvgMoveTo(vg, cx - s * 0.20f, cy - s * 0.20f);
            nvgLineTo(vg, cx + s * 0.20f, cy + s * 0.20f);
            nvgMoveTo(vg, cx + s * 0.20f, cy - s * 0.20f);
            nvgLineTo(vg, cx - s * 0.20f, cy + s * 0.20f);
        }
        nvgStroke(vg);
    }
private:
    NtGlyph  m_glyph;
    NVGcolor m_color;
};

// Translucent host that keeps its render/run closures alive for the dialog's
// lifetime and flags the alive guard false on teardown.
class NtActivity : public brls::Activity {
public:
    NtActivity(brls::Box* content, std::shared_ptr<bool> alive,
               std::vector<std::shared_ptr<void>> keep)
        : brls::Activity(content), m_alive(std::move(alive)), m_keep(std::move(keep)) {}
    bool isTranslucent() override { return true; }
    ~NtActivity() override { if (m_alive) *m_alive = false; }
private:
    std::shared_ptr<bool> m_alive;
    std::vector<std::shared_ptr<void>> m_keep;
};

struct NetTestResult {
    bool wifiConnected = false;
    std::string ip = "-", dns = "-";
    int  wifiSignal = -1;   // -1 = unknown (not derivable on this platform)
    bool internetSkipped = false, internetOk = false;
    long internetMs = 0;
    bool serverConfigured = false, serverOk = false;
    long serverMs = 0;
    std::string serverUrl, serverName, serverVersion;
    std::string testedAt;
};

// Runs the WiFi / Internet / Server checks (blocking; call off the UI thread).
NetTestResult gatherNetTest() {
    using namespace std::chrono;
    NetTestResult r;

    // --- Local network details (only where the platform can report them) ---
#if defined(__vita__)
    // Vita: SceNetCtl gives real IP, signal % and primary DNS.
    SceNetCtlInfo netInfo;
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &netInfo) >= 0) {
        r.wifiConnected = true;
        r.ip = std::string(netInfo.ip_address);
    }
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &netInfo) >= 0) {
        r.wifiSignal = netInfo.rssi_percentage;
    }
    SceNetCtlInfo dnsInfo {};
    if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &dnsInfo) >= 0) {
        r.dns = std::string(dnsInfo.primary_dns);
    }
#elif defined(__linux__) || defined(__ANDROID__)
    // Desktop Linux / Android: first non-loopback IPv4 via getifaddrs. Signal
    // and DNS aren't portably available here, so they stay "unknown".
    struct ifaddrs* ifap = nullptr;
    if (getifaddrs(&ifap) == 0) {
        for (struct ifaddrs* ifa = ifap; ifa != nullptr; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            char buf[INET_ADDRSTRLEN] = {0};
            auto* sin = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof(buf))) {
                r.ip = buf;
                r.wifiConnected = true;
                break;
            }
        }
        freeifaddrs(ifap);
    }
#else
    // Switch / Windows / other: no portable local-network probe here. Leave the
    // details "unknown"; connectivity is inferred from the internet check below.
#endif

    // Internet reachability — always run (never gate on a possibly-unknown WiFi
    // state), so we report real connectivity on every platform.
    {
        HttpClient http;
        http.setTimeout(10);
        auto s = steady_clock::now();
        HttpResponse resp = http.get("http://connectivitycheck.gstatic.com/generate_204");
        auto e = steady_clock::now();
        r.internetMs = static_cast<long>(duration_cast<milliseconds>(e - s).count());
        r.internetOk = resp.success && (resp.statusCode == 204 || resp.statusCode == 200);
    }
    // If we couldn't read a local IP but the internet is reachable, the link is
    // clearly up — reflect that instead of claiming "not connected".
    if (!r.wifiConnected && r.internetOk) r.wifiConnected = true;

    Application& app = Application::getInstance();
    std::string url = app.getServerUrl();
    if (url.empty()) url = app.getActiveServerUrl();
    if (!url.empty()) {
        r.serverConfigured = true;
        r.serverUrl = url;
        SuwayomiClient& client = SuwayomiClient::getInstance();
        ServerInfo info;
        auto s = steady_clock::now();
        bool ok = client.fetchServerInfo(info);
        auto e = steady_clock::now();
        r.serverMs = static_cast<long>(duration_cast<milliseconds>(e - s).count());
        r.serverOk = ok;
        if (ok) { r.serverName = info.name; r.serverVersion = info.version; }
    }

    std::time_t t = std::time(nullptr);
    std::tm tmv {};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmv);
    r.testedAt = buf;
    return r;
}

// Renders the verdict banner + chips + detail grid + footer into `panel`
// (clearing it first, so "Run again" repopulates in place).
void buildNetTestPanel(brls::Box* panel, const NetTestResult& r,
                       std::function<void()> onRunAgain, std::function<void()> onClose) {
    namespace c = ntcol;
    panel->clearViews();

    const bool ok = r.serverOk;
    const NVGcolor mark = ok ? c::success() : c::danger();

    // ---- Verdict banner ----
    auto* banner = new brls::Box();
    banner->setAxis(brls::Axis::ROW);
    banner->setAlignItems(brls::AlignItems::CENTER);
    banner->setPaddingTop(26); banner->setPaddingBottom(26);
    banner->setPaddingLeft(28); banner->setPaddingRight(28);
    banner->setCornerRadius(16);
    banner->setBackgroundColor(ok ? c::bannerOk() : c::bannerBad());

    auto* circle = new brls::Box();
    circle->setWidth(48); circle->setHeight(48);
    circle->setCornerRadius(24);
    circle->setJustifyContent(brls::JustifyContent::CENTER);
    circle->setAlignItems(brls::AlignItems::CENTER);
    circle->setBackgroundColor(mark);
    circle->setMarginRight(16);
    auto* glyph = new NtGlyphIcon(ok ? NtGlyph::Check : NtGlyph::Cross,
                                  ok ? c::checkInk() : nvgRGB(255, 255, 255));
    glyph->setWidth(28); glyph->setHeight(28);
    circle->addView(glyph);
    banner->addView(circle);

    auto* bcol = new brls::Box();
    bcol->setAxis(brls::Axis::COLUMN);
    bcol->setGrow(1.0f);
    auto* btitle = new brls::Label();
    btitle->setText(ok ? "Connected & healthy" : "Can't reach server");
    btitle->setFontSize(19);
    btitle->setTextColor(c::heading());
    bcol->addView(btitle);
    auto* bsub = new brls::Label();
    std::string sub;
    if (ok)                                   sub = "Server responded in " + std::to_string(r.serverMs) + "ms over Wi-Fi";
    else if (!r.wifiConnected)                sub = "Wi-Fi not connected";
    else if (!r.serverConfigured)             sub = "No server configured";
    else if (!r.internetOk && !r.internetSkipped) sub = "No internet connection";
    else                                      sub = "Server unreachable";
    bsub->setText(sub);
    bsub->setFontSize(13);
    bsub->setTextColor(ok ? c::okSub() : c::danger());
    bsub->setMarginTop(3);
    bcol->addView(bsub);
    banner->addView(bcol);
    panel->addView(banner);

    // ---- Three chips ----
    auto* chips = new brls::Box();
    chips->setAxis(brls::Axis::ROW);
    chips->setPaddingTop(18); chips->setPaddingBottom(18);
    chips->setPaddingLeft(22); chips->setPaddingRight(22);
    auto addChip = [&](const std::string& label, bool dotOk,
                       const std::string& value, const std::string& caption, bool last) {
        auto* chip = new brls::Box();
        chip->setAxis(brls::Axis::COLUMN);
        chip->setGrow(1.0f);
        chip->setBackgroundColor(c::chipBg());
        chip->setBorderColor(c::chipBorder());
        chip->setBorderThickness(1.0f);
        chip->setCornerRadius(12);
        chip->setPaddingTop(13); chip->setPaddingBottom(13);
        chip->setPaddingLeft(14); chip->setPaddingRight(14);
        if (!last) chip->setMarginRight(10);

        auto* top = new brls::Box();
        top->setAxis(brls::Axis::ROW);
        top->setAlignItems(brls::AlignItems::CENTER);
        auto* dot = new brls::Box();
        dot->setWidth(8); dot->setHeight(8); dot->setCornerRadius(4);
        dot->setBackgroundColor(dotOk ? c::success() : c::danger());
        dot->setMarginRight(7);
        top->addView(dot);
        auto* lbl = new brls::Label();
        lbl->setText(label); lbl->setFontSize(12); lbl->setTextColor(c::muted());
        top->addView(lbl);
        chip->addView(top);

        auto* val = new brls::Label();
        val->setText(value); val->setFontSize(19); val->setTextColor(c::heading());
        val->setMarginTop(6);
        chip->addView(val);
        auto* cap = new brls::Label();
        cap->setText(caption); cap->setFontSize(12); cap->setTextColor(c::muted());
        cap->setMarginTop(2);
        chip->addView(cap);
        chips->addView(chip);
    };
    // WiFi chip: show the signal % where the platform reports it, otherwise a
    // plain link-up / down state rather than a fabricated percentage.
    if (r.wifiSignal >= 0) {
        addChip("WIFI", r.wifiConnected, std::to_string(r.wifiSignal) + "%", "Signal strength", false);
    } else {
        addChip("WIFI", r.wifiConnected, r.wifiConnected ? "Up" : "Down",
                r.wifiConnected ? "Connected" : "No link", false);
    }
    addChip("INTERNET", r.internetOk,
            r.internetSkipped ? "—" : std::to_string(r.internetMs) + "ms",
            r.internetSkipped ? "Skipped" : "Reachable", false);
    addChip("SERVER", r.serverOk,
            r.serverConfigured ? std::to_string(r.serverMs) + "ms" : "—",
            "Handshake", true);
    panel->addView(chips);

    // ---- Detail grid ----
    auto* grid = new brls::Box();
    grid->setAxis(brls::Axis::COLUMN);
    grid->setPaddingTop(14); grid->setPaddingBottom(14);
    grid->setPaddingLeft(26); grid->setPaddingRight(26);
    auto addRow = [&](const std::string& key, const std::string& value) {
        auto* row = new brls::Box();
        row->setAxis(brls::Axis::ROW);
        row->setAlignItems(brls::AlignItems::CENTER);
        row->setPaddingTop(9); row->setPaddingBottom(9);
        auto* k = new brls::Label();
        k->setText(key); k->setFontSize(13); k->setTextColor(c::muted()); k->setWidth(150);
        row->addView(k);
        auto* v = new brls::Label();
        v->setText(value); v->setFontSize(13); v->setTextColor(c::body()); v->setGrow(1.0f);
        row->addView(v);
        grid->addView(row);
        auto* line = new brls::Box();
        line->setHeight(1);
        line->setAlignSelf(brls::AlignSelf::STRETCH);
        line->setBackgroundColor(c::rowLine());
        grid->addView(line);
    };
    addRow("Local IP", (r.ip == "-") ? "Unknown" : r.ip);
    if (r.dns != "-") addRow("DNS", r.dns);   // hide when the platform can't report it
    addRow("Server URL", r.serverConfigured ? r.serverUrl : "Not configured");
    addRow("Server", (r.serverOk && !r.serverName.empty())
                         ? (r.serverName + " v" + r.serverVersion) : "-");
    addRow("Tested", r.testedAt);
    panel->addView(grid);

    // ---- Footer ----
    auto* footer = new brls::Box();
    footer->setAxis(brls::Axis::ROW);
    footer->setAlignItems(brls::AlignItems::CENTER);
    footer->setPaddingTop(14); footer->setPaddingBottom(14);
    footer->setPaddingLeft(22); footer->setPaddingRight(22);

    auto* again = new brls::Box();
    again->setWidth(52); again->setHeight(44);
    again->setCornerRadius(11);
    again->setJustifyContent(brls::JustifyContent::CENTER);
    again->setAlignItems(brls::AlignItems::CENTER);
    again->setBackgroundColor(c::chipBg());
    again->setBorderColor(c::chipBorder());
    again->setBorderThickness(1.0f);
    again->setHighlightCornerRadius(11);
    again->setHideHighlightBackground(true);   // keep the fill visible when focused
    again->setFocusable(true);
    again->setMarginRight(10);
    auto* refreshImg = new brls::Image();
    refreshImg->setWidth(22); refreshImg->setHeight(22);
    refreshImg->setScalingType(brls::ImageScalingType::FIT);
    refreshImg->setImageFromRes("icons/refresh.png");
    again->addView(refreshImg);
    again->registerClickAction([onRunAgain](brls::View*) { if (onRunAgain) onRunAgain(); return true; });
    again->addGestureRecognizer(new brls::TapGestureRecognizer(again));
    footer->addView(again);

    auto* close = new brls::Box();
    close->setHeight(44);
    close->setGrow(1.0f);
    close->setCornerRadius(11);
    close->setJustifyContent(brls::JustifyContent::CENTER);
    close->setAlignItems(brls::AlignItems::CENTER);
    close->setBackgroundColor(c::accent());
    close->setHighlightCornerRadius(11);
    close->setHideHighlightBackground(true);   // keep the accent fill visible when focused
    close->setFocusable(true);
    auto* closeLbl = new brls::Label();
    closeLbl->setText("Close");
    closeLbl->setFontSize(14);
    closeLbl->setTextColor(c::accentInk());
    close->addView(closeLbl);
    close->registerClickAction([onClose](brls::View*) { if (onClose) onClose(); return true; });
    close->addGestureRecognizer(new brls::TapGestureRecognizer(close));
    footer->addView(close);
    panel->addView(footer);

    brls::Application::giveFocus(close);
}

} // namespace

void SettingsTab::runNetworkTest() {
    // Dialog shell (scrim + panel); "Run again" repopulates the panel in place.
    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(ntcol::scrim());

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    float panelW = 520.0f;
    const float sw = brls::Application::contentWidth;
    if (panelW + 80.0f > sw) panelW = sw - 80.0f;
    panel->setWidth(panelW);
    panel->setBackgroundColor(ntcol::panel());
    panel->setBorderColor(ntcol::border());
    panel->setBorderThickness(1.0f);
    panel->setCornerRadius(16);
    panel->setShadowType(brls::ShadowType::GENERIC);
    scrim->addView(panel);

    auto dialogAlive = std::make_shared<bool>(true);
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim,
        []() { brls::Application::popActivity(); }));
    scrim->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [](brls::View*) { brls::Application::popActivity(); return true; });

    auto render  = std::make_shared<std::function<void(const NetTestResult&)>>();
    auto runTest = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> runTestWeak = runTest;

    *render = [panel, runTestWeak](const NetTestResult& r) {
        buildNetTestPanel(panel, r,
            [runTestWeak]() { if (auto rt = runTestWeak.lock()) (*rt)(); },
            []() { brls::Application::popActivity(); });
    };

    *runTest = [panel, render, dialogAlive]() {
        panel->clearViews();
        auto* loading = new brls::Label();
        loading->setText("Running network test…");
        loading->setFontSize(15);
        loading->setTextColor(ntcol::body());
        loading->setMarginTop(30); loading->setMarginBottom(30);
        loading->setMarginLeft(28); loading->setMarginRight(28);
        panel->addView(loading);

        platform::launchThread([render, dialogAlive]() {
            NetTestResult r = gatherNetTest();
            brls::sync([render, dialogAlive, r]() {
                if (!*dialogAlive) return;
                (*render)(r);
            });
        });
    };

    brls::Application::pushActivity(new NtActivity(scrim, dialogAlive, {render, runTest}));
    (*runTest)();
}

void SettingsTab::showCategoryManagementDialog() {
    // Fetch categories from server
    brls::Application::notify("Loading categories...");

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Category> categories;

    if (!client.fetchCategories(categories)) {
        brls::Application::notify("Failed to load categories");
        return;
    }

    // Sort by order
    std::sort(categories.begin(), categories.end(),
        [](const Category& a, const Category& b) {
            return a.order < b.order;
        });

    // Create dialog box
    auto* dialogBox = new brls::Box();
    dialogBox->setAxis(brls::Axis::COLUMN);
    dialogBox->setWidth(550);
    dialogBox->setHeight(450);
    dialogBox->setPadding(20);
    dialogBox->setBackgroundColor(Application::getInstance().getDialogBackground());
    dialogBox->setCornerRadius(12);

    // Title
    auto* titleLabel = new brls::Label();
    titleLabel->setText("Manage Categories");
    titleLabel->setFontSize(22);
    titleLabel->setMarginBottom(10);
    dialogBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Use L/R to move, X to edit, Triangle to delete");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(Application::getInstance().getSubtitleColor());
    infoLabel->setMarginBottom(15);
    dialogBox->addView(infoLabel);

    // Create New Category button
    auto* createBtn = new brls::Button();
    createBtn->setText("+ Create New Category");
    createBtn->setMarginBottom(15);
    createBtn->registerClickAction([this](brls::View* view) {
        brls::Application::popActivity();
        showCreateCategoryDialog();
        return true;
    });
    createBtn->addGestureRecognizer(new brls::TapGestureRecognizer(createBtn));

    // Register B button on create button
    createBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    dialogBox->addView(createBtn);

    // Create close button early so it can be captured in lambdas (added to dialog later)
    auto* closeBtn = new brls::Button();
    closeBtn->setText("Close");
    closeBtn->setMarginTop(15);
    closeBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    closeBtn->addGestureRecognizer(new brls::TapGestureRecognizer(closeBtn));

    // Register B button on close button
    closeBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Scrollable category list
    auto* scrollView = new brls::ScrollingFrame();
    scrollView->setGrow(1.0f);

    auto* catList = new brls::Box();
    catList->setAxis(brls::Axis::COLUMN);

    for (size_t i = 0; i < categories.size(); i++) {
        const auto& cat = categories[i];

        // Skip the default category (id 0) - it can't be modified
        if (cat.id == 0) continue;

        auto* catRow = new brls::Box();
        catRow->setAxis(brls::Axis::ROW);
        catRow->setAlignItems(brls::AlignItems::CENTER);
        catRow->setJustifyContent(brls::JustifyContent::SPACE_BETWEEN);
        catRow->setPadding(10, 12, 10, 12);
        catRow->setMarginBottom(6);
        catRow->setCornerRadius(8);
        catRow->setBackgroundColor(Application::getInstance().getInactiveRowBackground());
        catRow->setFocusable(true);

        // Category info box
        auto* infoBox = new brls::Box();
        infoBox->setAxis(brls::Axis::COLUMN);
        infoBox->setGrow(1.0f);

        auto* nameLabel = new brls::Label();
        std::string displayName = cat.name;
        if (displayName.length() > 20) {
            displayName = displayName.substr(0, 18) + "..";
        }
        nameLabel->setText(displayName);
        nameLabel->setFontSize(16);
        infoBox->addView(nameLabel);

        auto* countLabel = new brls::Label();
        countLabel->setText(std::to_string(cat.mangaCount) + " manga");
        countLabel->setFontSize(12);
        countLabel->setTextColor(Application::getInstance().getDimTextColor());
        infoBox->addView(countLabel);

        catRow->addView(infoBox);

        // Order indicator (use UI row index, 1-indexed)
        auto* orderLabel = new brls::Label();
        size_t uiRowIndex = catList->getChildren().size();  // 0-indexed position before adding
        orderLabel->setText("#" + std::to_string(uiRowIndex + 1));
        orderLabel->setFontSize(14);
        orderLabel->setTextColor(Application::getInstance().getDimTextColor());
        orderLabel->setMarginRight(10);
        catRow->addView(orderLabel);

        // Store category data for actions
        int catId = cat.id;

        // Register L button to move up
        catRow->registerAction("Move Up", brls::ControllerButton::BUTTON_LB, [catList, catId, createBtn, closeBtn](brls::View* view) {
            // Find current UI position
            auto& children = catList->getChildren();
            int uiIndex = -1;
            for (size_t i = 0; i < children.size(); i++) {
                if (children[i] == view) {
                    uiIndex = static_cast<int>(i);
                    break;
                }
            }

            if (uiIndex > 0) {
                // Server order = UI index + 1 (default category at order 0 is skipped in UI)
                int newServerOrder = uiIndex;  // Moving up: new order = current UI index

                SuwayomiClient& client = SuwayomiClient::getInstance();
                if (client.moveCategoryOrder(catId, newServerOrder)) {
                    // Visual swap
                    brls::View* currentRow = children[uiIndex];
                    brls::View* prevRow = children[uiIndex - 1];

                    catList->removeView(currentRow, false);
                    catList->removeView(prevRow, false);

                    catList->addView(currentRow, uiIndex - 1);
                    catList->addView(prevRow, uiIndex);

                    // Update order labels for swapped rows
                    auto* currentBox = dynamic_cast<brls::Box*>(currentRow);
                    auto* prevBox = dynamic_cast<brls::Box*>(prevRow);
                    if (currentBox && currentBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(currentBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex));
                    }
                    if (prevBox && prevBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(prevBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 1));
                    }

                    // Update navigation routes after move
                    auto& updatedChildren = catList->getChildren();
                    if (!updatedChildren.empty()) {
                        updatedChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
                        updatedChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
                        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, updatedChildren.front());
                        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, updatedChildren.back());
                    }
                } else {
                    brls::Application::notify("Failed to move category");
                }
            }
            return true;
        });

        // Register R button to move down
        catRow->registerAction("Move Down", brls::ControllerButton::BUTTON_RB, [catList, catId, createBtn, closeBtn](brls::View* view) {
            // Find current UI position
            auto& children = catList->getChildren();
            int uiIndex = -1;
            for (size_t i = 0; i < children.size(); i++) {
                if (children[i] == view) {
                    uiIndex = static_cast<int>(i);
                    break;
                }
            }

            if (uiIndex >= 0 && uiIndex < static_cast<int>(children.size()) - 1) {
                // Server order = UI index + 1, moving down: new order = current order + 1
                int newServerOrder = uiIndex + 2;  // UI index + 1 (current) + 1 (move down)

                SuwayomiClient& client = SuwayomiClient::getInstance();
                if (client.moveCategoryOrder(catId, newServerOrder)) {
                    // Visual swap
                    brls::View* currentRow = children[uiIndex];
                    brls::View* nextRow = children[uiIndex + 1];

                    catList->removeView(nextRow, false);
                    catList->removeView(currentRow, false);

                    catList->addView(nextRow, uiIndex);
                    catList->addView(currentRow, uiIndex + 1);

                    // Update order labels for swapped rows
                    auto* currentBox = dynamic_cast<brls::Box*>(currentRow);
                    auto* nextBox = dynamic_cast<brls::Box*>(nextRow);
                    if (currentBox && currentBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(currentBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 2));
                    }
                    if (nextBox && nextBox->getChildren().size() >= 2) {
                        auto* label = dynamic_cast<brls::Label*>(nextBox->getChildren()[1]);
                        if (label) label->setText("#" + std::to_string(uiIndex + 1));
                    }

                    // Update navigation routes after move
                    auto& updatedChildren = catList->getChildren();
                    if (!updatedChildren.empty()) {
                        updatedChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
                        updatedChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
                        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, updatedChildren.front());
                        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, updatedChildren.back());
                    }
                } else {
                    brls::Application::notify("Failed to move category");
                }
            }
            return true;
        });

        // Register X button to edit
        Category catCopy = cat;
        catRow->registerAction("Edit", brls::ControllerButton::BUTTON_X, [this, catCopy](brls::View*) {
            brls::Application::popActivity();
            showEditCategoryDialog(catCopy);
            return true;
        });

        // Register Y button to delete
        catRow->registerAction("Delete", brls::ControllerButton::BUTTON_Y, [this, catCopy](brls::View*) {
            brls::Application::popActivity();
            showDeleteCategoryConfirmation(catCopy);
            return true;
        });

        // Click to edit
        catRow->registerClickAction([this, catCopy](brls::View* view) {
            brls::Application::popActivity();
            showEditCategoryDialog(catCopy);
            return true;
        });
        catRow->addGestureRecognizer(new brls::TapGestureRecognizer(catRow));

        // Register B button on each row to close dialog
        catRow->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
            brls::Application::popActivity();
            return true;
        }, true);  // hidden action

        catList->addView(catRow);
    }

    // If no editable categories
    if (categories.empty() || (categories.size() == 1 && categories[0].id == 0)) {
        auto* label = new brls::Label();
        label->setText("No categories found. Create one!");
        label->setFontSize(16);
        label->setMarginTop(20);
        catList->addView(label);
    }

    scrollView->setContentView(catList);
    dialogBox->addView(scrollView);

    // Add close button to dialog (created earlier to allow capture in lambdas)
    dialogBox->addView(closeBtn);

    // Register circle button to close the dialog
    dialogBox->registerAction("Close", brls::ControllerButton::BUTTON_BACK, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    // Set up navigation: first category should go up to createBtn, last category should go down to closeBtn
    auto& catChildren = catList->getChildren();
    if (!catChildren.empty()) {
        // First category row -> up goes to createBtn
        catChildren.front()->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
        // Last category row -> down goes to closeBtn
        catChildren.back()->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
    }
    // createBtn -> down goes to first category (or closeBtn if no categories)
    if (!catChildren.empty()) {
        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, catChildren.front());
    } else {
        createBtn->setCustomNavigationRoute(brls::FocusDirection::DOWN, closeBtn);
    }
    // closeBtn -> up goes to last category (or createBtn if no categories)
    if (!catChildren.empty()) {
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, catChildren.back());
    } else {
        closeBtn->setCustomNavigationRoute(brls::FocusDirection::UP, createBtn);
    }

    // Push as new activity
    brls::Application::pushActivity(new brls::Activity(dialogBox));
}

void SettingsTab::showCreateCategoryDialog() {
    std::vector<OptionRow> rows;
    rows.push_back({ "tag.png", "Enter Name", "", true, false, []() {
        // Open software keyboard for input (popover fades before this runs)
        brls::Application::getImeManager()->openForText([](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.createCategory(text)) {
                brls::Application::notify("Category created: " + text);
            } else {
                brls::Application::notify("Failed to create category");
            }
        }, "Enter category name", "", 50, "", 0);
    } });
    rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
    OptionsPopover::show("CATEGORY", "Create New Category", std::move(rows));
}

void SettingsTab::showEditCategoryDialog(const Category& category) {
    int catId = category.id;
    bool isDefault = category.isDefault;
    std::string catName = category.name;

    std::vector<OptionRow> rows;
    rows.push_back({ "tag.png", "Enter Name", "", true, false, [this, catId, isDefault, catName]() {
        brls::Application::getImeManager()->openForText([this, catId, isDefault](std::string text) {
            if (text.empty()) {
                brls::Application::notify("Category name cannot be empty");
                return;
            }

            SuwayomiClient& client = SuwayomiClient::getInstance();
            if (client.updateCategory(catId, text, isDefault)) {
                brls::Application::notify("Category renamed to: " + text);
                // Re-open category management dialog to show updated list
                showCategoryManagementDialog();
            } else {
                brls::Application::notify("Failed to rename category");
            }
        }, "Enter new category name", "", 50, catName, 0);
    } });
    rows.push_back({ "back.png", "Cancel", "", false, true, [this]() {
        // Re-open category management dialog
        showCategoryManagementDialog();
    } });
    OptionsPopover::show("CATEGORY", "Edit Category: " + category.name, std::move(rows));
}

void SettingsTab::showDeleteCategoryConfirmation(const Category& category) {
    std::string message = "Delete category '" + category.name + "'?\n\n"
                         "This will remove " + std::to_string(category.mangaCount) +
                         " manga from this category.\nThe manga will remain in your library.";

    int catId = category.id;

    std::vector<OptionRow> rows;
    rows.push_back({ "cross.png", "Delete", "", true, true, [this, catId]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        if (client.deleteCategory(catId)) {
            // Clean up hidden category reference
            auto& hidden = Application::getInstance().getSettings().hiddenCategoryIds;
            hidden.erase(catId);

            // Reset default category if it was the deleted one
            if (Application::getInstance().getSettings().defaultCategoryId == catId) {
                Application::getInstance().getSettings().defaultCategoryId = 0;
            }

            Application::getInstance().saveSettings();

            // Update UI cells
            if (m_hideCategoriesCell) {
                m_hideCategoriesCell->setDetailText(std::to_string(hidden.size()) + " hidden");
            }
            refreshDefaultCategorySelector();

            brls::Application::notify("Category deleted");
            showCategoryManagementDialog();
        } else {
            brls::Application::notify("Failed to delete category");
            showCategoryManagementDialog();
        }
    } });
    rows.push_back({ "back.png", "Cancel", "", false, true, [this]() {
        // Re-open category management dialog
        showCategoryManagementDialog();
    } });
    OptionsPopover::show("CONFIRM", message, std::move(rows));
}

void SettingsTab::showStorageManagement() {
    // Push storage management view
    brls::Application::notify("Opening storage management...");

    // Create storage view inline (since we may not have the full view ready)
    auto* storageBox = new brls::Box();
    storageBox->setAxis(brls::Axis::COLUMN);
    storageBox->setPadding(20);
    storageBox->setGrow(1.0f);

    auto* titleLabel = new brls::Label();
    titleLabel->setText("Storage Management");
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(20);
    storageBox->addView(titleLabel);

    // Get downloads info
    auto downloads = DownloadsManager::getInstance().getDownloads();
    int64_t totalSize = 0;

    auto* infoLabel = new brls::Label();
    infoLabel->setText("Downloaded manga: " + std::to_string(downloads.size()));
    infoLabel->setFontSize(18);
    infoLabel->setMarginBottom(10);
    storageBox->addView(infoLabel);

    // Clear all button
    auto* clearBtn = new brls::Button();
    clearBtn->setText("Clear All Downloads");
    clearBtn->setMarginTop(20);
    clearBtn->registerClickAction([](brls::View* view) {
        std::vector<OptionRow> rows;
        rows.push_back({ "cross.png", "Delete", "", true, true, []() {
            auto downloads = DownloadsManager::getInstance().getDownloads();
            for (const auto& item : downloads) {
                DownloadsManager::getInstance().deleteMangaDownload(item.mangaId);
            }
            brls::Application::notify("All downloads deleted");
            brls::Application::popActivity();
        } });
        rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
        OptionsPopover::show("CONFIRM", "Delete all downloaded content?", std::move(rows));
        return true;
    });
    clearBtn->addGestureRecognizer(new brls::TapGestureRecognizer(clearBtn));
    // Register circle button on clearBtn
    clearBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    storageBox->addView(clearBtn);

    // Back button
    auto* backBtn = new brls::Button();
    backBtn->setText("Back");
    backBtn->setMarginTop(20);
    backBtn->registerClickAction([](brls::View* view) {
        brls::Application::popActivity();
        return true;
    });
    backBtn->addGestureRecognizer(new brls::TapGestureRecognizer(backBtn));
    // Register circle button on backBtn
    backBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    storageBox->addView(backBtn);

    // Register circle button on storageBox as fallback
    storageBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    brls::Application::pushActivity(new brls::Activity(storageBox));
}

namespace {

// Fixed dark palette for the Statistics dashboard (per the design spec).
namespace stcol {
    inline NVGcolor page()       { return nvgRGB(18, 18, 20); }
    inline NVGcolor panel()      { return nvgRGB(28, 28, 31); }
    inline NVGcolor borderCard() { return nvgRGBA(255, 255, 255, 15); }
    inline NVGcolor border()     { return nvgRGBA(255, 255, 255, 20); }
    inline NVGcolor accent()     { return nvgRGB(100, 180, 255); }
    inline NVGcolor success()    { return nvgRGB(78, 204, 163); }
    inline NVGcolor warning()    { return nvgRGB(255, 152, 0); }
    inline NVGcolor muted()      { return nvgRGB(139, 139, 147); }
    inline NVGcolor body()       { return nvgRGB(197, 198, 208); }
    inline NVGcolor heading()    { return nvgRGB(231, 231, 234); }
    inline NVGcolor chip()       { return nvgRGB(35, 35, 38); }
    inline NVGcolor empty()      { return nvgRGB(42, 42, 46); }
    inline NVGcolor emptyBar()   { return nvgRGB(58, 58, 64); }
    inline NVGcolor danger()     { return nvgRGB(217, 107, 107); }
}

// Donut completion ring drawn with nanovg arcs (borealis boxes can't do conic
// gradients). Center labels are ordinary children drawn on top.
class RingView : public brls::Box {
public:
    RingView(float completedFrac, float inProgressFrac)
        : m_comp(completedFrac), m_ip(inProgressFrac) {}
    void draw(NVGcontext* vg, float x, float y, float w, float h,
              brls::Style style, brls::FrameContext* ctx) override {
        const float cx = x + w * 0.5f, cy = y + h * 0.5f;
        const float outer = (w < h ? w : h) * 0.5f;
        const float thick = outer * 0.285f;
        const float rad = outer - thick * 0.5f;
        auto arc = [&](float f0, float f1, NVGcolor col) {
            if (f1 <= f0) return;
            const float a0 = -1.5707963f + f0 * 6.2831853f;
            const float a1 = -1.5707963f + f1 * 6.2831853f;
            nvgBeginPath(vg);
            nvgArc(vg, cx, cy, rad, a0, a1, NVG_CW);
            nvgStrokeColor(vg, col);
            nvgStrokeWidth(vg, thick);
            nvgLineCap(vg, NVG_BUTT);
            nvgStroke(vg);
        };
        float comp = m_comp < 0 ? 0 : (m_comp > 1 ? 1 : m_comp);
        float ip   = m_ip   < 0 ? 0 : m_ip;
        if (comp + ip > 1.0f) ip = 1.0f - comp;
        arc(0.0f, 1.0f, stcol::empty());          // not-started backdrop
        arc(0.0f, comp, stcol::success());         // completed
        arc(comp, comp + ip, stcol::accent());     // in-progress
        brls::Box::draw(vg, x, y, w, h, style, ctx);
    }
private:
    float m_comp, m_ip;
};

class StatsActivity : public brls::Activity {
public:
    StatsActivity(brls::Box* content, std::shared_ptr<bool> alive,
                  std::vector<std::shared_ptr<void>> keep)
        : brls::Activity(content), m_alive(std::move(alive)), m_keep(std::move(keep)) {}
    ~StatsActivity() override { if (m_alive) *m_alive = false; }
private:
    std::shared_ptr<bool> m_alive;
    std::vector<std::shared_ptr<void>> m_keep;
};

struct StatCat { std::string name; int read = 0; int total = 0; };
struct StatsData {
    int chaptersRead = 0, mangaCompleted = 0, unreadRemaining = 0;
    int libSize = 0, completed = 0, inProgress = 0, notStarted = 0;
    std::vector<StatCat> cats;
    bool haveLibrary = false;   // library couldn't be read -> hide derived pieces
};

// Reads persisted counters plus library-derived aggregates (blocking; off UI).
StatsData gatherStats() {
    StatsData d;
    AppSettings& s = Application::getInstance().getSettings();
    d.chaptersRead   = s.totalChaptersRead;
    d.mangaCompleted = s.totalMangaCompleted;

    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Manga> lib;
    if (client.fetchLibraryManga(lib)) {
        d.haveLibrary = true;
        d.libSize = static_cast<int>(lib.size());
        std::map<int, StatCat> catMap;
        for (const auto& m : lib) {
            const int total = m.chapterCount;
            int read = total - m.unreadCount;
            if (read < 0) read = 0;
            d.unreadRemaining += m.unreadCount;
            if (total > 0 && m.unreadCount == 0) d.completed++;
            else if (read > 0)                   d.inProgress++;
            else                                 d.notStarted++;
            for (int cid : m.categoryIds) {
                auto& cs = catMap[cid];
                cs.read  += read;
                cs.total += total;
            }
        }
        std::vector<Category> cats;
        if (client.fetchCategories(cats)) {
            std::sort(cats.begin(), cats.end(),
                [](const Category& a, const Category& b) { return a.order < b.order; });
            for (const auto& cat : cats) {
                auto it = catMap.find(cat.id);
                if (it != catMap.end() && it->second.total > 0) {
                    StatCat sc = it->second;
                    sc.name = cat.name;
                    d.cats.push_back(sc);
                }
            }
        }
    }
    return d;
}

void buildStatsDashboard(brls::Box* content, const StatsData& d,
                         std::function<void()> onSync, std::function<void()> onReset) {
    namespace c = stcol;
    content->clearViews();
    content->setPaddingTop(20); content->setPaddingBottom(26);
    content->setPaddingLeft(40); content->setPaddingRight(40);

    auto pill = [&](const std::string& iconRes, const std::string& text, NVGcolor textColor,
                    std::function<void()> onClick) {
        auto* p = new brls::Box();
        p->setAxis(brls::Axis::ROW);
        p->setAlignItems(brls::AlignItems::CENTER);
        p->setHeight(34);
        p->setCornerRadius(11);
        p->setPaddingLeft(iconRes.empty() ? 14 : 12);
        p->setPaddingRight(14);
        p->setBackgroundColor(c::chip());
        p->setBorderColor(c::border());
        p->setBorderThickness(1.0f);
        p->setHighlightCornerRadius(11);
        p->setHideHighlightBackground(true);
        p->setFocusable(true);
        if (!iconRes.empty()) {
            auto* img = new brls::Image();
            img->setWidth(16); img->setHeight(16);
            img->setScalingType(brls::ImageScalingType::FIT);
            img->setImageFromRes(iconRes);
            img->setMarginRight(8);
            p->addView(img);
        }
        auto* l = new brls::Label();
        l->setText(text); l->setFontSize(13); l->setTextColor(textColor);
        p->addView(l);
        p->registerClickAction([onClick](brls::View*) { if (onClick) onClick(); return true; });
        p->addGestureRecognizer(new brls::TapGestureRecognizer(p));
        return p;
    };

    // ---- Header: breadcrumb + reset + sync ----
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setMarginBottom(22);
    auto* crumb = new brls::Label();
    crumb->setText("Settings  ›  Reading Statistics");
    crumb->setFontSize(22); crumb->setTextColor(c::heading()); crumb->setGrow(1.0f);
    header->addView(crumb);
    auto* resetPill = pill("", "Reset", c::danger(), onReset);
    resetPill->setMarginRight(10);
    header->addView(resetPill);
    header->addView(pill("icons/refresh.png", "Sync", c::body(), onSync));
    content->addView(header);

    // ---- Body: two columns ----
    auto* bodyRow = new brls::Box();
    bodyRow->setAxis(brls::Axis::ROW);
    bodyRow->setAlignItems(brls::AlignItems::FLEX_START);

    // Left: completion ring card
    if (d.haveLibrary && d.libSize > 0) {
        auto* card = new brls::Box();
        card->setAxis(brls::Axis::COLUMN);
        card->setWidth(330);
        card->setMarginRight(26);
        card->setBackgroundColor(c::panel());
        card->setBorderColor(c::borderCard());
        card->setBorderThickness(1.0f);
        card->setCornerRadius(16);
        card->setPadding(26);

        auto* lbl = new brls::Label();
        lbl->setText("LIBRARY COMPLETION");
        lbl->setFontSize(14); lbl->setTextColor(c::muted()); lbl->setMarginBottom(16);
        card->addView(lbl);

        const float compFrac = static_cast<float>(d.completed) / static_cast<float>(d.libSize);
        const float ipFrac   = static_cast<float>(d.inProgress) / static_cast<float>(d.libSize);
        const int pctInt = (d.completed * 100 + d.libSize / 2) / d.libSize;

        auto* ringWrap = new brls::Box();
        ringWrap->setJustifyContent(brls::JustifyContent::CENTER);
        ringWrap->setAlignItems(brls::AlignItems::CENTER);
        ringWrap->setMarginBottom(18);
        auto* ring = new RingView(compFrac, ipFrac);
        ring->setWidth(210); ring->setHeight(210);
        ring->setJustifyContent(brls::JustifyContent::CENTER);
        ring->setAlignItems(brls::AlignItems::CENTER);
        auto* pct = new brls::Label();
        pct->setText(std::to_string(pctInt) + "%");
        pct->setFontSize(40); pct->setTextColor(c::heading());
        ring->addView(pct);
        auto* pcap = new brls::Label();
        pcap->setText("completed"); pcap->setFontSize(13); pcap->setTextColor(c::muted());
        ring->addView(pcap);
        ringWrap->addView(ring);
        card->addView(ringWrap);

        auto legend = [&](NVGcolor swatch, const std::string& label, int count) {
            auto* row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setAlignItems(brls::AlignItems::CENTER);
            row->setMarginTop(8);
            auto* dot = new brls::Box();
            dot->setWidth(12); dot->setHeight(12); dot->setCornerRadius(3);
            dot->setBackgroundColor(swatch); dot->setMarginRight(10);
            row->addView(dot);
            auto* l = new brls::Label();
            l->setText(label); l->setFontSize(13); l->setTextColor(c::body()); l->setGrow(1.0f);
            row->addView(l);
            auto* cnt = new brls::Label();
            cnt->setText(std::to_string(count)); cnt->setFontSize(13); cnt->setTextColor(c::heading());
            row->addView(cnt);
            card->addView(row);
        };
        legend(c::success(), "Completed", d.completed);
        legend(c::accent(),  "In progress", d.inProgress);
        legend(c::empty(),   "Not started", d.notStarted);
        bodyRow->addView(card);
    }

    // Right: headline numbers + categories
    auto* right = new brls::Box();
    right->setAxis(brls::Axis::COLUMN);
    right->setGrow(1.0f);

    auto* statRow = new brls::Box();
    statRow->setAxis(brls::Axis::ROW);
    statRow->setMarginBottom(20);
    auto statCard = [&](const std::string& num, NVGcolor col, const std::string& cap, bool last) {
        auto* card = new brls::Box();
        card->setAxis(brls::Axis::COLUMN);
        card->setGrow(1.0f);
        card->setBackgroundColor(c::panel());
        card->setBorderColor(c::borderCard());
        card->setBorderThickness(1.0f);
        card->setCornerRadius(13);
        card->setPaddingTop(18); card->setPaddingBottom(18);
        card->setPaddingLeft(20); card->setPaddingRight(20);
        if (!last) card->setMarginRight(16);
        auto* n = new brls::Label();
        n->setText(num); n->setFontSize(34); n->setTextColor(col);
        card->addView(n);
        auto* cp = new brls::Label();
        cp->setText(cap); cp->setFontSize(13); cp->setTextColor(c::muted()); cp->setMarginTop(4);
        card->addView(cp);
        statRow->addView(card);
    };
    statCard(std::to_string(d.chaptersRead), c::accent(), "Chapters read", false);
    statCard(std::to_string(d.mangaCompleted), c::success(),
             "Manga completed", !d.haveLibrary);
    if (d.haveLibrary)
        statCard(std::to_string(d.unreadRemaining), c::warning(), "Unread remaining", true);
    right->addView(statRow);

    if (d.haveLibrary && !d.cats.empty()) {
        auto* catCard = new brls::Box();
        catCard->setAxis(brls::Axis::COLUMN);
        catCard->setBackgroundColor(c::panel());
        catCard->setBorderColor(c::borderCard());
        catCard->setBorderThickness(1.0f);
        catCard->setCornerRadius(14);
        catCard->setPaddingTop(20); catCard->setPaddingBottom(20);
        catCard->setPaddingLeft(24); catCard->setPaddingRight(24);
        auto* ct = new brls::Label();
        ct->setText("Progress by category");
        ct->setFontSize(15); ct->setTextColor(c::heading()); ct->setMarginBottom(14);
        catCard->addView(ct);
        for (const auto& cat : d.cats) {
            const float pct = cat.total > 0 ? static_cast<float>(cat.read) / static_cast<float>(cat.total) : 0.0f;
            auto* row = new brls::Box();
            row->setAxis(brls::Axis::COLUMN);
            row->setMarginBottom(14);
            auto* top = new brls::Box();
            top->setAxis(brls::Axis::ROW);
            top->setAlignItems(brls::AlignItems::CENTER);
            top->setMarginBottom(6);
            auto* nm = new brls::Label();
            nm->setText(cat.name); nm->setFontSize(14); nm->setTextColor(c::body()); nm->setGrow(1.0f);
            top->addView(nm);
            auto* rt = new brls::Label();
            rt->setText(std::to_string(cat.read) + " / " + std::to_string(cat.total));
            rt->setFontSize(13); rt->setTextColor(c::muted());
            top->addView(rt);
            row->addView(top);
            auto* track = new brls::Box();
            track->setHeight(9); track->setCornerRadius(4);
            track->setAlignSelf(brls::AlignSelf::STRETCH);
            track->setBackgroundColor(c::chip());
            auto* fill = new brls::Box();
            fill->setHeight(9); fill->setCornerRadius(4);
            const NVGcolor fc = pct >= 1.0f ? c::success()
                              : pct >= 0.1f ? c::accent()
                              : pct >  0.0f ? c::warning() : c::emptyBar();
            fill->setBackgroundColor(fc);
            float wp = pct * 100.0f;
            if (wp < 0.0f) wp = 0.0f; if (wp > 100.0f) wp = 100.0f;
            fill->setWidthPercentage(wp);
            track->addView(fill);
            row->addView(track);
            catCard->addView(row);
        }
        right->addView(catCard);
    }

    bodyRow->addView(right);
    content->addView(bodyRow);

    brls::Application::giveFocus(header);
}

} // namespace

void SettingsTab::showStatisticsView() {
    auto* page = new brls::Box();
    page->setAxis(brls::Axis::COLUMN);
    page->setGrow(1.0f);
    page->setBackgroundColor(stcol::page());

    auto* scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setFocusable(false);
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setAlignItems(brls::AlignItems::STRETCH);
    scroll->setContentView(content);
    page->addView(scroll);

    auto alive = std::make_shared<bool>(true);
    page->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [](brls::View*) { brls::Application::popActivity(); return true; });

    auto render  = std::make_shared<std::function<void(const StatsData&)>>();
    auto runSync = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> runSyncWeak = runSync;

    *render = [content, runSyncWeak](const StatsData& d) {
        buildStatsDashboard(content, d,
            [runSyncWeak]() { if (auto r = runSyncWeak.lock()) (*r)(); },
            []() {
                std::vector<OptionRow> rows;
                rows.push_back({ "cross.png", "Reset", "", true, true, []() {
                    AppSettings& s = Application::getInstance().getSettings();
                    s.totalChaptersRead = 0;
                    s.totalMangaCompleted = 0;
                    s.currentStreak = 0;
                    s.longestStreak = 0;
                    s.totalReadingTime = 0;
                    s.lastReadDate = 0;
                    Application::getInstance().saveSettings();
                    brls::Application::notify("Statistics reset");
                    brls::Application::popActivity();
                }});
                rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
                OptionsPopover::show("CONFIRM", "Reset all reading statistics?", std::move(rows));
            });
    };

    *runSync = [content, render, alive]() {
        content->clearViews();
        auto* loading = new brls::Label();
        loading->setText("Loading statistics…");
        loading->setFontSize(15);
        loading->setTextColor(stcol::body());
        loading->setMarginTop(40); loading->setMarginLeft(40);
        content->addView(loading);

        Application::getInstance().syncStatisticsFromServer();
        platform::launchThread([render, alive]() {
            StatsData d = gatherStats();
            brls::sync([render, alive, d]() {
                if (!*alive) return;
                (*render)(d);
            });
        });
    };

    brls::Application::pushActivity(new StatsActivity(page, alive, {render, runSync}));
    (*runSync)();
}

void SettingsTab::createBackupSection() {
    auto* header = new brls::Header();
    header->setTitle("Backup & Restore");
    m_contentBox->addView(header);

    // Export backup
    auto* exportCell = new brls::DetailCell();
    exportCell->setText("Export Backup");
    exportCell->setDetailText("Save library and settings to file");
    exportCell->registerClickAction([this](brls::View* view) {
        exportBackup();
        return true;
    });
    m_contentBox->addView(exportCell);

    // Import backup
    auto* importCell = new brls::DetailCell();
    importCell->setText("Import Backup");
    importCell->setDetailText("Restore from backup file");
    importCell->registerClickAction([this](brls::View* view) {
        importBackup();
        return true;
    });
    m_contentBox->addView(importCell);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Backups are saved to ux0:data/VitaSuwayomi/backup/");
    infoLabel->setFontSize(14);
    infoLabel->setMarginLeft(16);
    infoLabel->setMarginTop(4);
    m_contentBox->addView(infoLabel);
}

void SettingsTab::createStatisticsSection() {
    auto* header = new brls::Header();
    header->setTitle("Reading Statistics");
    m_contentBox->addView(header);

    // View statistics
    auto* statsCell = new brls::DetailCell();
    statsCell->setText("View Statistics");
    statsCell->setDetailText("Chapters read, streaks, and more");
    statsCell->registerClickAction([this](brls::View* view) {
        showStatisticsView();
        return true;
    });
    m_contentBox->addView(statsCell);

    // Storage management
    auto* storageCell = new brls::DetailCell();
    storageCell->setText("Storage Management");
    storageCell->setDetailText("View and manage downloaded content");
    storageCell->registerClickAction([this](brls::View* view) {
        showStorageManagement();
        return true;
    });
    m_contentBox->addView(storageCell);
}

void SettingsTab::exportBackup() {
    brls::Application::notify("Creating backup...");

    SuwayomiClient& client = SuwayomiClient::getInstance();

    // Create backup directory
    std::string backupDir = platform::path("backup");
    platform::createDir(backupDir);

    // Generate filename with timestamp
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    char filename[64];
    strftime(filename, sizeof(filename), "backup_%Y%m%d_%H%M%S.json", tm_info);

    std::string backupPath = backupDir + "/" + std::string(filename);

    if (client.exportBackup(backupPath)) {
        brls::Application::notify("Backup saved: " + std::string(filename));
    } else {
        brls::Application::notify("Failed to create backup");
    }
}

void SettingsTab::importBackup() {
    std::vector<OptionRow> rows;
    rows.push_back({ "import.png", "Import", "Restore most recent backup", true, false, []() {
        brls::Application::notify("Importing backup...");

        SuwayomiClient& client = SuwayomiClient::getInstance();

        // Find most recent backup file
        std::string backupDir = platform::path("backup");
        std::string latestBackup;

        for (const auto& name : platform::listDir(backupDir)) {
            if (name.find("backup_") == 0 && name.find(".json") != std::string::npos) {
                if (name > latestBackup) {
                    latestBackup = name;
                }
            }
        }

        if (latestBackup.empty()) {
            brls::Application::notify("No backup file found");
            return;
        }

        std::string backupPath = backupDir + "/" + latestBackup;
        if (client.importBackup(backupPath)) {
            brls::Application::notify("Backup restored: " + latestBackup);
        } else {
            brls::Application::notify("Failed to import backup");
        }
    }});
    rows.push_back({ "back.png", "Cancel", "", false, true, []() {}});

    OptionsPopover::show("CONFIRM", "Import backup?", std::move(rows));
}

void SettingsTab::refreshDefaultCategorySelector() {
    if (!m_defaultCategorySelector) return;

    // Update the cell's shown value to the current default category's name.
    // (The full option list is fetched when the cell is tapped.)
    SuwayomiClient& client = SuwayomiClient::getInstance();
    std::vector<Category> categories;
    if (!client.fetchCategories(categories)) return;

    const int currentId = Application::getInstance().getSettings().defaultCategoryId;
    std::string currentName = "Default (All)";
    for (const auto& cat : categories) {
        if (cat.id == currentId) { currentName = cat.name; break; }
    }
    m_defaultCategorySelector->setDetailText(currentName);
}

void SettingsTab::checkForUpdates() {
    brls::Application::notify("Checking for updates...");

    // GitHub API URL for latest release
    // TODO: Replace with actual repository when published
    const std::string apiUrl = "https://api.github.com/repos/Breezyslasher/Vita_Suwayomi/releases/latest";

    HttpClient client;
    client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
    client.setTimeout(15);

    std::string response;
    if (!client.get(apiUrl, response)) {
        brls::Application::notify("Failed to check for updates");
        return;
    }

    // Simple JSON parsing for tag_name, body, and assets
    auto extractJsonString = [&response](const std::string& key) -> std::string {
        std::string search = "\"" + key + "\":";
        size_t pos = response.find(search);
        if (pos == std::string::npos) return "";

        pos += search.length();
        // Skip whitespace
        while (pos < response.length() && (response[pos] == ' ' || response[pos] == '\t' || response[pos] == '\n')) pos++;

        if (pos >= response.length()) return "";

        if (response[pos] == '"') {
            // String value
            pos++;
            std::string result;
            while (pos < response.length() && response[pos] != '"') {
                if (response[pos] == '\\' && pos + 1 < response.length()) {
                    pos++;
                    if (response[pos] == 'n') result += '\n';
                    else if (response[pos] == 'r') result += '\r';
                    else if (response[pos] == 't') result += '\t';
                    else result += response[pos];
                } else {
                    result += response[pos];
                }
                pos++;
            }
            return result;
        }
        return "";
    };

    std::string tagName = extractJsonString("tag_name");
    std::string releaseNotes = extractJsonString("body");

    if (tagName.empty()) {
        brls::Application::notify("Failed to parse update info");
        return;
    }

    // Remove 'v' prefix if present
    std::string newVersion = tagName;
    if (!newVersion.empty() && (newVersion[0] == 'v' || newVersion[0] == 'V')) {
        newVersion = newVersion.substr(1);
    }

    // Compare versions (simple string comparison for now)
    std::string currentVersion = VITA_SUWAYOMI_VERSION;

    // Parse version numbers for proper comparison
    auto parseVersion = [](const std::string& ver) -> int {
        int major = 0, minor = 0, patch = 0;
        std::sscanf(ver.c_str(), "%d.%d.%d", &major, &minor, &patch);
        return major * 10000 + minor * 100 + patch;
    };

    int currentNum = parseVersion(currentVersion);
    int newNum = parseVersion(newVersion);

    if (newNum <= currentNum) {
        brls::Application::notify("You're running the latest version!");
        return;
    }

    // Find VPK download URL in assets
    std::string downloadUrl;
    size_t assetsPos = response.find("\"assets\"");
    if (assetsPos != std::string::npos) {
        // Look for browser_download_url with .vpk extension
        size_t searchPos = assetsPos;
        while (searchPos < response.length()) {
            size_t urlPos = response.find("\"browser_download_url\"", searchPos);
            if (urlPos == std::string::npos) break;

            urlPos += 23; // Skip past "browser_download_url":
            while (urlPos < response.length() && response[urlPos] != '"') urlPos++;
            if (urlPos >= response.length()) break;
            urlPos++; // Skip opening quote

            size_t urlEnd = response.find("\"", urlPos);
            if (urlEnd == std::string::npos) break;

            std::string url = response.substr(urlPos, urlEnd - urlPos);
            if (url.find(".vpk") != std::string::npos) {
                downloadUrl = url;
                break;
            }
            searchPos = urlEnd + 1;
        }
    }

    if (downloadUrl.empty()) {
        brls::Application::notify("No VPK found in release");
        return;
    }

    // Show update dialog
    showUpdateDialog(newVersion, releaseNotes, downloadUrl);
}

void SettingsTab::showUpdateDialog(const std::string& newVersion, const std::string& releaseNotes,
                                    const std::string& downloadUrl) {
    std::string message = "New version " + newVersion + " available!\n\n";
    if (!releaseNotes.empty()) {
        // Truncate release notes if too long
        std::string notes = releaseNotes;
        if (notes.length() > 300) {
            notes = notes.substr(0, 297) + "...";
        }
        message += notes;
    }

    brls::Dialog* dialog = new brls::Dialog(message);
    dialog->setCancelable(true);  // Allow back button to close dialog

    dialog->addButton("Later", []() {});

    dialog->addButton("Download & Install", [downloadUrl, newVersion, this]() {
        downloadAndInstallUpdate(downloadUrl, newVersion);
    });

    dialog->open();
}

void SettingsTab::downloadAndInstallUpdate(const std::string& downloadUrl, const std::string& version) {
    brls::Application::notify("Downloading update...");

    // Create update directory
    std::string updateDir = platform::path("updates");
    platform::createDir(updateDir);

    std::string pkgPath = updateDir + "/VitaSuwayomi_" + version + ".vpk";

    HttpClient client;
    client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
    client.setTimeout(300);  // 5 minute timeout for large downloads
    client.setFollowRedirects(true);

    if (client.downloadToFile(downloadUrl, pkgPath)) {
        std::string msg = "Update downloaded successfully!\n\n"
                          "Saved to:\n" + pkgPath + "\n\n"
                          "Please install the update package and restart.";

        brls::Dialog* successDialog = new brls::Dialog(msg);
        successDialog->setCancelable(true);
        successDialog->addButton("OK", []() {});
        successDialog->open();
    } else {
        brls::Application::notify("Failed to download update");
    }
}

} // namespace vitasuwayomi
