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

#include "platform/platform.hpp"

#ifdef __vita__
#include <psp2/net/netctl.h>
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

void SettingsTab::runNetworkTest() {
    brls::Application::notify("Running network test...");

    platform::launchThread([this]() {
        std::string results;

        // --- WiFi Check ---
        bool wifiConnected = false;
        std::string ipAddress = "-";
        std::string dnsServer = "-";
        int wifiLevel = 0;

#ifdef __vita__
        SceNetCtlInfo netInfo;
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_IP_ADDRESS, &netInfo) >= 0) {
            wifiConnected = true;
            ipAddress = std::string(netInfo.ip_address);
        }
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_RSSI_PERCENTAGE, &netInfo) >= 0) {
            wifiLevel = netInfo.rssi_percentage;
        }
        SceNetCtlInfo dnsInfo {};
        if (sceNetCtlInetGetInfo(SCE_NETCTL_INFO_GET_PRIMARY_DNS, &dnsInfo) >= 0) {
            dnsServer = std::string(dnsInfo.primary_dns);
        }
#else
        // Desktop stub: assume connected
        wifiConnected = true;
        ipAddress = "127.0.0.1";
        dnsServer = "8.8.8.8";
        wifiLevel = 100;
#endif

        results += "-- WiFi --\n";
        if (wifiConnected) {
            results += "Status: Connected\n";
            results += "IP: " + ipAddress + "\n";
            results += "DNS: " + dnsServer + "\n";
            results += "Signal: " + std::to_string(wifiLevel) + "%\n";
        } else {
            results += "Status: Not Connected\n";
        }

        // --- Internet Check (DNS resolve + HTTP) ---
        results += "\n-- Internet --\n";
        if (wifiConnected) {
            HttpClient http;
            http.setTimeout(10);
            auto start = std::chrono::steady_clock::now();
            HttpResponse resp = http.get("http://connectivitycheck.gstatic.com/generate_204");
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (resp.success && (resp.statusCode == 204 || resp.statusCode == 200)) {
                results += "Status: Reachable (" + std::to_string(ms) + "ms)\n";
            } else {
                results += "Status: Unreachable\n";
                if (!resp.error.empty())
                    results += "Error: " + resp.error + "\n";
            }
        } else {
            results += "Skipped (no WiFi)\n";
        }

        // --- Server Check ---
        results += "\n-- Server --\n";
        Application& app = Application::getInstance();
        std::string serverUrl = app.getServerUrl();
        if (serverUrl.empty()) serverUrl = app.getActiveServerUrl();

        if (serverUrl.empty()) {
            results += "No server URL configured.\n";
        } else {
            results += "URL: " + serverUrl + "\n";

            SuwayomiClient& client = SuwayomiClient::getInstance();
            ServerInfo info;

            auto start = std::chrono::steady_clock::now();
            bool ok = client.fetchServerInfo(info);
            auto end = std::chrono::steady_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

            if (ok) {
                results += "Status: Connected (" + std::to_string(ms) + "ms)\n";
                results += "Server: " + info.name + " v" + info.version + "\n";
                results += "Build: " + info.buildType + "\n";
            } else {
                results += "Status: Failed (" + std::to_string(ms) + "ms)\n";
                results += "Could not reach server.\n";
            }
        }

        // Show results in a brls::Dialog (proper overlay, no page transition)
        brls::sync([results]() {
            brls::Dialog* dialog = new brls::Dialog("Network Test Results");
            dialog->setCancelable(true);

            auto* contentBox = new brls::Box();
            contentBox->setAxis(brls::Axis::COLUMN);
            contentBox->setPadding(16);

            // Split results by newline and add each as a separate label
            std::istringstream stream(results);
            std::string line;
            while (std::getline(stream, line)) {
                auto* label = new brls::Label();
                if (line.empty()) {
                    label->setText(" ");
                    label->setFontSize(8);
                } else if (line.find("--") == 0) {
                    label->setText(line);
                    label->setFontSize(16);
                    label->setTextColor(Application::getInstance().getHeaderTextColor());
                    label->setMarginTop(5);
                } else {
                    label->setText(line);
                    label->setFontSize(15);
                    label->setTextColor(nvgRGB(200, 200, 200));
                }
                label->setMarginBottom(2);
                contentBox->addView(label);
            }

            dialog->addView(contentBox);
            dialog->addButton("Close", []() {});
            dialog->open();
        });
    });
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

void SettingsTab::showStatisticsView() {
    // Sync stats from server first
    Application::getInstance().syncStatisticsFromServer();

    Application& app = Application::getInstance();
    AppSettings& settings = app.getSettings();

    auto* statsBox = new brls::Box();
    statsBox->setAxis(brls::Axis::COLUMN);
    statsBox->setPadding(20);
    statsBox->setGrow(1.0f);

    auto* titleLabel = new brls::Label();
    titleLabel->setText("Reading Statistics");
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(10);
    statsBox->addView(titleLabel);

    // Info label
    auto* infoLabel = new brls::Label();
    infoLabel->setText("Synced from server and local reading");
    infoLabel->setFontSize(14);
    infoLabel->setTextColor(Application::getInstance().getDimTextColor());
    infoLabel->setMarginBottom(15);
    statsBox->addView(infoLabel);

    // Total chapters read
    auto* chaptersLabel = new brls::Label();
    chaptersLabel->setText("Chapters Read: " + std::to_string(settings.totalChaptersRead));
    chaptersLabel->setFontSize(18);
    chaptersLabel->setMarginBottom(10);
    statsBox->addView(chaptersLabel);

    // Manga completed
    auto* completedLabel = new brls::Label();
    completedLabel->setText("Manga Completed: " + std::to_string(settings.totalMangaCompleted));
    completedLabel->setFontSize(18);
    completedLabel->setMarginBottom(10);
    statsBox->addView(completedLabel);

    // Reading streak
    auto* streakLabel = new brls::Label();
    streakLabel->setText("Current Streak: " + std::to_string(settings.currentStreak) + " days");
    streakLabel->setFontSize(18);
    streakLabel->setMarginBottom(10);
    statsBox->addView(streakLabel);

    // Longest streak
    auto* longestLabel = new brls::Label();
    longestLabel->setText("Longest Streak: " + std::to_string(settings.longestStreak) + " days");
    longestLabel->setFontSize(18);
    longestLabel->setMarginBottom(10);
    statsBox->addView(longestLabel);

    // Reading time note
    auto* timeNoteLabel = new brls::Label();
    timeNoteLabel->setText("Note: Reading time tracking is local only");
    timeNoteLabel->setFontSize(14);
    timeNoteLabel->setTextColor(Application::getInstance().getDimTextColor());
    timeNoteLabel->setMarginBottom(5);
    statsBox->addView(timeNoteLabel);

    // Reading time
    int hours = static_cast<int>(settings.totalReadingTime / 3600);
    int minutes = static_cast<int>((settings.totalReadingTime % 3600) / 60);
    auto* timeLabel = new brls::Label();
    timeLabel->setText("Total Reading Time: " + std::to_string(hours) + "h " + std::to_string(minutes) + "m");
    timeLabel->setFontSize(18);
    timeLabel->setMarginBottom(20);
    statsBox->addView(timeLabel);

    // Sync from Server button
    auto* syncBtn = new brls::Button();
    syncBtn->setText("Sync from Server");
    syncBtn->registerClickAction([chaptersLabel, completedLabel](brls::View* view) {
        brls::Application::notify("Syncing statistics...");
        Application::getInstance().syncStatisticsFromServer();
        AppSettings& s = Application::getInstance().getSettings();
        chaptersLabel->setText("Chapters Read: " + std::to_string(s.totalChaptersRead));
        completedLabel->setText("Manga Completed: " + std::to_string(s.totalMangaCompleted));
        brls::Application::notify("Statistics synced");
        return true;
    });
    syncBtn->addGestureRecognizer(new brls::TapGestureRecognizer(syncBtn));
    // Register circle button on syncBtn
    syncBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    statsBox->addView(syncBtn);

    // Reset button
    auto* resetBtn = new brls::Button();
    resetBtn->setText("Reset Statistics");
    resetBtn->setMarginTop(10);
    resetBtn->registerClickAction([](brls::View* view) {
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
        } });
        rows.push_back({ "back.png", "Cancel", "", false, true, [](){} });
        OptionsPopover::show("CONFIRM", "Reset all reading statistics?", std::move(rows));
        return true;
    });
    resetBtn->addGestureRecognizer(new brls::TapGestureRecognizer(resetBtn));
    // Register circle button on resetBtn
    resetBtn->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action
    statsBox->addView(resetBtn);

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
    statsBox->addView(backBtn);

    // Register circle button on statsBox as fallback
    statsBox->registerAction("Back", brls::ControllerButton::BUTTON_B, [](brls::View*) {
        brls::Application::popActivity();
        return true;
    }, true);  // hidden action

    brls::Application::pushActivity(new brls::Activity(statsBox));
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
