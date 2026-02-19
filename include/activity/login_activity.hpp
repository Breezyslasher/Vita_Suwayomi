/**
 * VitaSuwayomi - Login Activity
 * Handles connection to Suwayomi server
 */

#pragma once

#include <borealis.hpp>
#include <borealis/core/timer.hpp>
#include "app/suwayomi_client.hpp"

namespace vitasuwayomi {

class LoginActivity : public brls::Activity {
public:
    LoginActivity();

    brls::View* createContentView() override;

    void onContentAvailable() override;

private:
    void onConnectPressed();
    void onTestConnectionPressed();
    void onOfflinePressed();

    BRLS_BIND(brls::Label, titleLabel, "login/title");
    BRLS_BIND(brls::Box, inputContainer, "login/input_container");
    BRLS_BIND(brls::Label, serverLabel, "login/server_label");
    BRLS_BIND(brls::Label, usernameLabel, "login/username_label");
    BRLS_BIND(brls::Label, passwordLabel, "login/password_label");
    BRLS_BIND(brls::Label, authModeLabel, "login/auth_mode_label");
    BRLS_BIND(brls::Button, loginButton, "login/login_button");
    BRLS_BIND(brls::Button, testButton, "login/pin_button");
    BRLS_BIND(brls::Button, offlineButton, "login/offline_button");
    BRLS_BIND(brls::Label, statusLabel, "login/status");

    std::string m_serverUrl;
    std::string m_username;
    std::string m_password;
    AuthMode m_authMode = AuthMode::NONE;

    std::string getAuthModeName(AuthMode mode);
};

} // namespace vitasuwayomi
