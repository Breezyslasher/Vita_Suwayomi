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

    // Set initial values
    if (titleLabel) {
        titleLabel->setText("VitaSuwayomi");
    }

    if (statusLabel) {
        statusLabel->setText("Enter your Suwayomi server URL");
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
        usernameLabel->setText("Username: (optional)");
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
        passwordLabel->setText("Password: (optional)");
        passwordLabel->registerClickAction([this](brls::View* view) {
            brls::Application::getImeManager()->openForPassword([this](std::string text) {
                m_password = text;
                passwordLabel->setText(std::string("Password: ") + (text.empty() ? "(optional)" : "********"));
            }, "Enter Password (optional)", "", 128, "");
            return true;
        });
        passwordLabel->addGestureRecognizer(new brls::TapGestureRecognizer(passwordLabel));
    }

    // Auth mode selector
    if (authModeLabel) {
        authModeLabel->setText("Auth Mode: " + getAuthModeName(m_authMode));
        authModeLabel->registerClickAction([this](brls::View* view) {
            // Cycle through auth modes
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
}

void LoginActivity::onTestConnectionPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL first");
        return;
    }

    if (statusLabel) statusLabel->setText("Testing connection...");

    SuwayomiClient& client = SuwayomiClient::getInstance();

    // Try to connect and fetch server info
    if (client.connectToServer(m_serverUrl)) {
        ServerInfo info;
        if (client.fetchServerInfo(info)) {
            std::string msg = "Connected! Server v" + info.version;
            if (statusLabel) statusLabel->setText(msg);
        } else {
            if (statusLabel) statusLabel->setText("Server is reachable!");
        }
    } else {
        if (statusLabel) statusLabel->setText("Cannot reach server - check URL");
    }
}

void LoginActivity::onConnectPressed() {
    if (m_serverUrl.empty()) {
        if (statusLabel) statusLabel->setText("Please enter server URL");
        return;
    }

    if (statusLabel) statusLabel->setText("Connecting...");

    SuwayomiClient& client = SuwayomiClient::getInstance();

    // Set auth mode first
    client.setAuthMode(m_authMode);

    // Set server URL
    client.setServerUrl(m_serverUrl);

    bool connected = false;

    // Handle authentication based on mode
    if (m_authMode == AuthMode::NONE) {
        // No auth required, just test connection
        connected = client.connectToServer(m_serverUrl);
    } else if (m_authMode == AuthMode::BASIC_AUTH) {
        // Basic auth - set credentials and connect
        if (!m_username.empty() && !m_password.empty()) {
            client.setAuthCredentials(m_username, m_password);
        }
        connected = client.connectToServer(m_serverUrl);
    } else {
        // JWT-based auth (simple_login or ui_login)
        // First try to connect to server
        if (client.connectToServer(m_serverUrl)) {
            // Then attempt login if credentials are provided
            if (!m_username.empty() && !m_password.empty()) {
                if (statusLabel) statusLabel->setText("Authenticating...");
                connected = client.login(m_username, m_password);
                if (!connected) {
                    if (statusLabel) statusLabel->setText("Authentication failed - check credentials");
                    return;
                }
            } else {
                // No credentials provided for JWT auth
                if (statusLabel) statusLabel->setText("Username and password required for this auth mode");
                return;
            }
        } else {
            if (statusLabel) statusLabel->setText("Cannot reach server - check URL");
            return;
        }
    }

    if (connected) {
        // Save server URL and auth credentials
        Application::getInstance().setServerUrl(m_serverUrl);
        Application::getInstance().setAuthCredentials(m_username, m_password);
        Application::getInstance().setConnected(true);

        // Save auth mode and tokens
        AppSettings& settings = Application::getInstance().getSettings();
        settings.authMode = static_cast<int>(m_authMode);
        settings.accessToken = client.getAccessToken();
        settings.refreshToken = client.getRefreshToken();

        // Also save to localServerUrl in settings for proper persistence
        if (settings.localServerUrl.empty()) {
            settings.localServerUrl = m_serverUrl;
        }

        Application::getInstance().saveSettings();

        if (statusLabel) statusLabel->setText("Connected!");

        brls::sync([this]() {
            Application::getInstance().pushMainActivity();
        });
    } else {
        if (statusLabel) statusLabel->setText("Connection failed - check URL and server");
    }
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
