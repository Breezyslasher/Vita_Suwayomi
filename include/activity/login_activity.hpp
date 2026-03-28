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
    void onOfflinePressed();

    brls::View* m_contentView = nullptr;
    brls::Label* titleLabel = nullptr;
    brls::Box* inputContainer = nullptr;
    brls::InputCell* serverInput = nullptr;
    brls::InputCell* usernameInput = nullptr;
    brls::InputCell* passwordInput = nullptr;
    brls::Button* loginButton = nullptr;
    brls::Button* offlineButton = nullptr;
    brls::Label* statusLabel = nullptr;

    std::string m_serverUrl;
    std::string m_username;
    std::string m_password;
    AuthMode m_authMode = AuthMode::NONE;
    bool m_connecting = false;

    std::string getAuthModeName(AuthMode mode);
};

} // namespace vitasuwayomi
