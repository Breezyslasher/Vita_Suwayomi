/**
 * VitaSuwayomi - Login Activity implementation
 * Handles connection to Suwayomi server
 */

#include "activity/login_activity.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"

#include <memory>

namespace vitasuwayomi {

LoginActivity::LoginActivity() {
    brls::Logger::debug("LoginActivity created");
}

brls::View* LoginActivity::createContentView() {
    return brls::View::createFromXMLResource("activity/login.xml");
}

void LoginActivity::onContentAvailable() {
    brls::Logger::debug("LoginActivity content available");

    // Pre-fill saved connection details from settings
    const AppSettings& settings = Application::getInstance().getSettings();
    if (m_serverUrl.empty()) {
        if (!settings.localServerUrl.empty()) {
            m_serverUrl = settings.localServerUrl;
        } else if (!settings.remoteServerUrl.empty()) {
            m_serverUrl = settings.remoteServerUrl;
        }
    }
    m_username = Application::getInstance().getAuthUsername();
    m_password = Application::getInstance().getAuthPassword();
    m_authMode = static_cast<AuthMode>(settings.authMode);

    // Set initial values
    if (titleLabel) {
        titleLabel->setText("VitaSuwayomi");
    }

    if (statusLabel) {
        if (!m_serverUrl.empty()) {
            statusLabel->setText("Saved server found - tap Connect or go Offline");
        } else {
            statusLabel->setText("Enter your Suwayomi server URL");
        }
    }

    // Server URL input
    if (serverLabel) {
        serverLabel->setText(std::string("Server: ") + (m_serverUrl.empty() ? "Not set" : m_serverUrl));
        serverLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_serverUrl = text;
                serverLabel->setText(std::string("Server: ") + text);
            }, "Enter Server URL", "http://your-server:4567", 256, m_serverUrl);
            return true;
        });
        serverLabel->addGestureRecognizer(new brls::TapGestureRecognizer(serverLabel));
    }

    // Username input (optional for Suwayomi basic auth)
    if (usernameLabel) {
        usernameLabel->setText(std::string("Username: ") + (m_username.empty() ? "(optional)" : m_username));
        usernameLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForText([this](std::string text) {
                m_username = text;
                usernameLabel->setText(std::string("Username: ") + (text.empty() ? "(optional)" : text));
            }, "Enter Username (optional)", "", 128, m_username);
            return true;
        });
        usernameLabel->addGestureRecognizer(new brls::TapGestureRecognizer(usernameLabel));
    }

    // Password input (optional for Suwayomi basic auth)
    if (passwordLabel) {
        passwordLabel->setText(std::string("Password: ") + (m_password.empty() ? "(optional)" : "********"));
        passwordLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForPassword([this](std::string text) {
                m_password = text;
                passwordLabel->setText(std::string("Password: ") + (text.empty() ? "(optional)" : "********"));
            }, "Enter Password (optional)", "", 128, "");
            return true;
        });
        passwordLabel->addGestureRecognizer(new brls::TapGestureRecognizer(passwordLabel));
    }

    // Auth mode display (auto-detected on connect)
    if (authModeLabel) {
        if (m_authMode == AuthMode::NONE && m_serverUrl.empty()) {
            authModeLabel->setText("Auth Mode: Auto-detect");
        } else {
            authModeLabel->setText("Auth Mode: " + getAuthModeName(m_authMode));
        }
        // Still allow manual override if user taps
        authModeLabel->registerClickAction([this](brls::View* view) {
            // Cycle through auth modes (manual override)
            int currentMode = static_cast<int>(m_authMode);
            currentMode = (currentMode + 1) % 4;
            m_authMode = static_cast<AuthMode>(currentMode);
            authModeLabel->setText("Auth Mode: " + getAuthModeName(m_authMode));
            return true;
        });
        authModeLabel->addGestureRecognizer(new brls::TapGestureRecognizer(authModeLabel));
    }

    // Connect button
    if (loginButton) {
        loginButton->setText("Connect");
        loginButton->registerClickAction([this](brls::View* view) {
            onConnectPressed();
            return true;
        });
    }

    // Test connection button
    if (testButton) {
        testButton->setText("Test");
        testButton->registerClickAction([this](brls::View* view) {
            onTestConnectionPressed();
            return true;
        });
    }

    // Offline mode button
    if (offlineButton) {
        offlineButton->setText("Offline");
        offlineButton->registerClickAction([this](brls::View* view) {
            onOfflinePressed();
            return true;
        });
    }
}

void LoginActivity::onTestConnectionPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL first");
        return;
    }

    if (statusLabel) statusLabel->setText("Testing connection...");

    SuwayomiClient& client = SuwayomiClient::getInstance();
    client.setServerUrl(m_serverUrl);

    // Step 1: Check basic reachability
    if (!client.connectToServer(m_serverUrl)) {
        if (statusLabel) statusLabel->setText("Cannot reach server - check URL");
        return;
    }

    // Step 2: Fetch server info (public endpoint)
    ServerInfo info;
    if (client.fetchServerInfo(info)) {
        std::string msg = "Reachable! Server v" + info.version;
        if (statusLabel) statusLabel->setText(msg);
    } else {
        if (statusLabel) statusLabel->setText("Server is reachable!");
    }

    // Step 3: Auto-detect auth mode
    bool serverRequiresAuth = client.checkServerRequiresAuth(m_serverUrl);
    if (!serverRequiresAuth) {
        if (statusLabel) statusLabel->setText("Server OK - no auth required");
        m_authMode = AuthMode::NONE;
        if (authModeLabel) authModeLabel->setText("Auth Mode: None");
        return;
    }

    // Server requires auth - detect the type
    std::string errorMsg;
    AuthMode detectedMode = client.detectServerAuthMode(m_serverUrl, errorMsg);
    std::string modeName = getAuthModeName(detectedMode);

    if (m_username.empty() || m_password.empty()) {
        if (statusLabel) statusLabel->setText("Auth required (" + modeName + ") - enter credentials");
        m_authMode = detectedMode;
        if (authModeLabel) authModeLabel->setText("Auth Mode: " + modeName);
        return;
    }

    if (statusLabel) statusLabel->setText("Server OK, auth: " + modeName);
    m_authMode = detectedMode;
    if (authModeLabel) authModeLabel->setText("Auth Mode: " + modeName);
}

void LoginActivity::onConnectPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL");
        return;
    }

    if (statusLabel) statusLabel->setText("Connecting...");

    SuwayomiClient& client = SuwayomiClient::getInstance();
    client.setServerUrl(m_serverUrl);

    bool connected = false;

    // Step 1: Check if server is reachable at all (using basic connectivity test)
    if (statusLabel) statusLabel->setText("Checking server...");
    if (!client.connectToServer(m_serverUrl)) {
        // Server not reachable - provide specific error
        if (statusLabel) statusLabel->setText("Server offline or wrong URL");
        brls::Application::notify("Cannot reach " + m_serverUrl);
        return;
    }

    // Server is reachable - now check auth requirements
    if (statusLabel) statusLabel->setText("Detecting auth mode...");
    bool serverRequiresAuth = client.checkServerRequiresAuth(m_serverUrl);

    if (!serverRequiresAuth) {
        // No auth required - already connected from step 1
        brls::Logger::info("Server does not require auth");
        m_authMode = AuthMode::NONE;
        if (authModeLabel) authModeLabel->setText("Auth Mode: None");
        client.setAuthMode(AuthMode::NONE);
        connected = true;
    } else {
        // Server requires auth - need credentials
        if (m_username.empty() || m_password.empty()) {
            if (statusLabel) statusLabel->setText("Server requires auth - enter username & password");
            return;
        }

        // Step 2: Detect if server supports JWT or only Basic Auth
        bool supportsJWT = client.checkServerSupportsJWTLogin(m_serverUrl);

        if (supportsJWT) {
            // Server supports JWT - try UI_LOGIN first, then SIMPLE_LOGIN
            AuthMode tryModes[] = { AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN };
            bool loginOk = false;

            for (AuthMode tryMode : tryModes) {
                std::string modeName = getAuthModeName(tryMode);
                brls::Logger::info("Trying auth mode: {}", modeName);
                if (statusLabel) statusLabel->setText("Trying " + modeName + "...");

                client.setAuthMode(tryMode);
                client.logout();  // Clear any previous tokens

                if (!client.login(m_username, m_password)) {
                    brls::Logger::info("{}: login failed - bad credentials?", modeName);
                    // Login mutation failed - likely wrong credentials
                    // Don't try more modes, credentials are the issue
                    if (statusLabel) statusLabel->setText("Wrong username or password");
                    brls::Application::notify("Check your credentials");
                    return;
                }

                // Login succeeded - validate with a protected query
                if (client.validateAuthWithProtectedQuery()) {
                    brls::Logger::info("Auto-detected auth mode: {}", modeName);
                    m_authMode = tryMode;
                    if (authModeLabel) authModeLabel->setText("Auth Mode: " + modeName);
                    loginOk = true;
                    connected = true;
                    break;
                }

                brls::Logger::info("{}: login ok but protected query failed, trying next", modeName);
            }

            if (!loginOk) {
                // JWT modes didn't work - try Basic Auth as fallback
                brls::Logger::info("JWT modes failed, trying Basic Auth fallback");
                if (statusLabel) statusLabel->setText("Trying Basic Auth...");
                client.setAuthMode(AuthMode::BASIC_AUTH);
                client.logout();
                client.setAuthCredentials(m_username, m_password);

                if (client.validateAuthWithProtectedQuery()) {
                    brls::Logger::info("Auth succeeded with Basic Auth fallback");
                    m_authMode = AuthMode::BASIC_AUTH;
                    if (authModeLabel) authModeLabel->setText("Auth Mode: Basic Auth");
                    connected = true;
                } else {
                    if (statusLabel) statusLabel->setText("Wrong username or password");
                    brls::Application::notify("All auth modes failed - check credentials");
                    return;
                }
            }
        } else {
            // Server only supports Basic Auth
            brls::Logger::info("Auto-detected: Basic Auth");
            m_authMode = AuthMode::BASIC_AUTH;
            if (authModeLabel) authModeLabel->setText("Auth Mode: Basic Auth");
            client.setAuthMode(AuthMode::BASIC_AUTH);
            client.setAuthCredentials(m_username, m_password);

            // Validate credentials with a protected query
            if (statusLabel) statusLabel->setText("Authenticating...");
            if (!client.validateAuthWithProtectedQuery()) {
                if (statusLabel) statusLabel->setText("Wrong username or password");
                brls::Application::notify("Check your credentials");
                return;
            }
            connected = true;
        }
    }

    if (connected) {
        // Save server URL and auth credentials
        Application::getInstance().setServerUrl(m_serverUrl);
        Application::getInstance().setAuthCredentials(m_username, m_password);
        Application::getInstance().setConnected(true);

        // Save auto-detected auth mode and tokens
        AppSettings& settings = Application::getInstance().getSettings();
        settings.authMode = static_cast<int>(m_authMode);
        settings.accessToken = client.getAccessToken();
        settings.refreshToken = client.getRefreshToken();

        if (settings.localServerUrl.empty()) {
            settings.localServerUrl = m_serverUrl;
        }

        Application::getInstance().saveSettings();

        std::string modeName = getAuthModeName(m_authMode);
        if (statusLabel) statusLabel->setText("Connected! (" + modeName + ")");

        brls::sync([this]() {
            Application::getInstance().pushMainActivity();
        });
    } else {
        if (statusLabel) statusLabel->setText("Connection failed");
    }
}

void LoginActivity::onOfflinePressed() {
    brls::Logger::info("LoginActivity: Entering offline mode");

    // Keep the saved server URL so we can reconnect later, but don't require it
    if (!m_serverUrl.empty()) {
        Application::getInstance().setServerUrl(m_serverUrl);
        Application::getInstance().setAuthCredentials(m_username, m_password);

        AppSettings& settings = Application::getInstance().getSettings();
        settings.authMode = static_cast<int>(m_authMode);
        if (settings.localServerUrl.empty()) {
            settings.localServerUrl = m_serverUrl;
        }
        Application::getInstance().saveSettings();
    }

    // Mark as disconnected (offline)
    Application::getInstance().setConnected(false);

    if (statusLabel) statusLabel->setText("Entering offline mode...");

    brls::sync([]() {
        Application::getInstance().pushMainActivity();
    });
}

std::string LoginActivity::getAuthModeName(AuthMode mode) {
    switch (mode) {
        case AuthMode::NONE:
            return "None";
        case AuthMode::BASIC_AUTH:
            return "Basic Auth";
        case AuthMode::SIMPLE_LOGIN:
            return "Simple Login";
        case AuthMode::UI_LOGIN:
            return "UI Login (JWT)";
        default:
            return "Unknown";
    }
}

} // namespace vitasuwayomi
