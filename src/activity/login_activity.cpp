/**
 * VitaSuwayomi - Login Activity implementation
 * Handles connection to Suwayomi server
 */

#include "activity/login_activity.hpp"
#include "app/application.hpp"
#include "app/suwayomi_client.hpp"
#include "utils/async.hpp"

#include <cstdio>
#include <vector>
#include <memory>

namespace vitasuwayomi {

LoginActivity::LoginActivity() {
    brls::Logger::debug("LoginActivity created");
}

brls::View* LoginActivity::createContentView() {
    const std::vector<std::string> candidates = {
        std::string(RESOURCE_PREFIX) + "xml/activity/login.xml",
        std::string(RESOURCE_PREFIX) + "activity/login.xml",
        "romfs:/xml/activity/login.xml",
        "romfs:/activity/login.xml",
        "resources/xml/activity/login.xml",
        "resources/activity/login.xml",
        "activity/login.xml",
    };

    for (const auto& path : candidates) {
        if (FILE* f = std::fopen(path.c_str(), "rb")) {
            std::fclose(f);
            brls::Logger::info("LoginActivity: loading XML from '{}'", path);
            return brls::View::createFromXMLFile(path);
        }
    }

    brls::Logger::error("LoginActivity: login.xml not found in any known location");
    auto* fallback = new brls::Label();
    fallback->setText("Login UI missing (resources not packaged).");
    return fallback;
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

    // Connect button
    if (loginButton) {
        loginButton->setText("Connect");
        loginButton->registerClickAction([this](brls::View* view) {
            onConnectPressed();
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

void LoginActivity::onConnectPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL");
        return;
    }

    if (m_connecting) return;
    m_connecting = true;

    if (statusLabel) statusLabel->setText("Connecting...");

    // Capture values for background thread
    std::string serverUrl = m_serverUrl;
    std::string username = m_username;
    std::string password = m_password;

    asyncRun([this, serverUrl, username, password]() {
        SuwayomiClient& client = SuwayomiClient::getInstance();
        client.setServerUrl(serverUrl);

        bool connected = false;
        AuthMode authMode = AuthMode::NONE;

        // Step 1: Check if server is reachable at all
        brls::sync([this]() {
            if (statusLabel) statusLabel->setText("Checking server...");
        });
        if (!client.connectToServer(serverUrl)) {
            brls::sync([this]() {
                if (statusLabel) statusLabel->setText("Cannot reach server");
                m_connecting = false;
            });
            return;
        }

        // Server is reachable - now check auth requirements
        brls::sync([this]() {
            if (statusLabel) statusLabel->setText("Detecting auth mode...");
        });
        bool serverRequiresAuth = client.checkServerRequiresAuth(serverUrl);

        if (!serverRequiresAuth) {
            brls::Logger::info("Server does not require auth");
            authMode = AuthMode::NONE;
            client.setAuthMode(AuthMode::NONE);
            connected = true;
        } else {
            // Server requires auth - need credentials
            if (username.empty() || password.empty()) {
                brls::sync([this]() {
                    if (statusLabel) statusLabel->setText("Server requires auth - enter username & password");
                    m_connecting = false;
                });
                return;
            }

            // Step 2: Detect if server supports JWT or only Basic Auth
            bool supportsJWT = client.checkServerSupportsJWTLogin(serverUrl);

            if (supportsJWT) {
                AuthMode tryModes[] = { AuthMode::UI_LOGIN, AuthMode::SIMPLE_LOGIN };
                bool loginOk = false;

                for (AuthMode tryMode : tryModes) {
                    std::string modeName = getAuthModeName(tryMode);
                    brls::Logger::info("Trying auth mode: {}", modeName);
                    brls::sync([this, modeName]() {
                        if (statusLabel) statusLabel->setText("Trying " + modeName + "...");
                    });

                    client.setAuthMode(tryMode);
                    client.logout();

                    if (!client.login(username, password)) {
                        brls::Logger::info("{}: login call failed, trying next mode", modeName);
                        continue;
                    }

                    if (client.validateAuthWithProtectedQuery()) {
                        brls::Logger::info("Auto-detected auth mode: {}", modeName);
                        authMode = tryMode;
                        loginOk = true;
                        connected = true;
                        break;
                    }

                    brls::Logger::info("{}: login ok but protected query failed, trying next", modeName);
                }

                if (!loginOk) {
                    brls::Logger::info("JWT modes failed, trying Basic Auth fallback");
                    brls::sync([this]() {
                        if (statusLabel) statusLabel->setText("Trying Basic Auth...");
                    });
                    client.setAuthMode(AuthMode::BASIC_AUTH);
                    client.logout();
                    client.setAuthCredentials(username, password);

                    if (client.validateAuthWithProtectedQuery()) {
                        brls::Logger::info("Auth succeeded with Basic Auth fallback");
                        authMode = AuthMode::BASIC_AUTH;
                        connected = true;
                    } else {
                        brls::sync([this]() {
                            if (statusLabel) statusLabel->setText("Wrong username or password");
                            m_connecting = false;
                        });
                        return;
                    }
                }
            } else {
                brls::Logger::info("Auto-detected: Basic Auth");
                authMode = AuthMode::BASIC_AUTH;
                client.setAuthMode(AuthMode::BASIC_AUTH);
                client.setAuthCredentials(username, password);

                brls::sync([this]() {
                    if (statusLabel) statusLabel->setText("Authenticating...");
                });
                if (!client.validateAuthWithProtectedQuery()) {
                    brls::sync([this]() {
                        if (statusLabel) statusLabel->setText("Wrong username or password");
                        m_connecting = false;
                    });
                    return;
                }
                connected = true;
            }
        }

        // Back to UI thread for final result
        brls::sync([this, connected, authMode, serverUrl, username, password]() {
            m_connecting = false;

            if (connected) {
                m_authMode = authMode;

                SuwayomiClient& client = SuwayomiClient::getInstance();
                Application::getInstance().setServerUrl(serverUrl);
                Application::getInstance().setAuthCredentials(username, password);
                Application::getInstance().setConnected(true);

                AppSettings& settings = Application::getInstance().getSettings();
                settings.authMode = static_cast<int>(authMode);
                settings.accessToken = client.getAccessToken();
                settings.refreshToken = client.getRefreshToken();
                settings.sessionCookie = client.getSessionCookie();

                if (settings.localServerUrl.empty()) {
                    settings.localServerUrl = serverUrl;
                }

                // Apply server image settings on first connection (convert to PNG/JPEG only)
                if (!settings.serverImageSettingsApplied) {
                    brls::Logger::info("First connection: applying server image settings...");
                    if (client.applyServerImageSettings()) {
                        settings.serverImageSettingsApplied = true;
                        brls::Logger::info("Server image settings applied successfully");
                    } else {
                        brls::Logger::warning("Failed to apply server image settings, will retry next launch");
                    }
                }

                Application::getInstance().saveSettings();

                std::string modeName = getAuthModeName(authMode);
                if (statusLabel) statusLabel->setText("Connected! (" + modeName + ")");

                Application::getInstance().pushMainActivity();
            } else {
                if (statusLabel) statusLabel->setText("Connection failed");
            }
        });
    });
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
