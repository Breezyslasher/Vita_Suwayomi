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

    // Set authentication if provided
    if (!m_username.empty() && !m_password.empty()) {
        client.setAuthCredentials(m_username, m_password);
    }

    // Try to connect
    if (client.connectToServer(m_serverUrl)) {
        // Save server URL
        Application::getInstance().setServerUrl(m_serverUrl);
        Application::getInstance().setConnected(true);
        Application::getInstance().saveSettings();

        if (statusLabel) statusLabel->setText("Connected!");

        brls::sync([this]() {
            Application::getInstance().pushMainActivity();
        });
    } else {
        if (statusLabel) statusLabel->setText("Connection failed - check URL and server");
    }
}

} // namespace vitasuwayomi
