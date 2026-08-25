// pamcheck.cpp —— PAM 验证实现。

#include "lock/pamcheck.h"

#include <cstdlib>
#include <cstring>

#include <pwd.h>
#include <security/pam_appl.h>
#include <unistd.h>

namespace w10de::lock {

namespace {

// PAM conv 回调：把密码作为应答返回（只处理提示获取密码的请求）。
struct PasswordHolder {
    std::string password;
};

int pamConv(int numMsg, const pam_message** msg, pam_response** resp,
            void* appdataPtr) {
    auto* holder = static_cast<PasswordHolder*>(appdataPtr);
    if (numMsg <= 0 || numMsg > 32) {
        return PAM_CONV_ERR;
    }
    auto* replies = static_cast<pam_response*>(
        calloc(static_cast<size_t>(numMsg), sizeof(pam_response)));
    if (replies == nullptr) {
        return PAM_BUF_ERR;
    }
    for (int i = 0; i < numMsg; ++i) {
        // 提示类型为密码（PAM_PROMPT_ECHO_OFF）时填密码；其余（信息类）
        // 留空应答。
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF) {
            replies[i].resp = strdup(holder->password.c_str());
            if (replies[i].resp == nullptr) {
                for (int j = 0; j < i; ++j) {
                    free(replies[j].resp);
                }
                free(replies);
                return PAM_BUF_ERR;
            }
        } else {
            replies[i].resp = nullptr;
        }
    }
    *resp = replies;
    return PAM_SUCCESS;
}

}  // namespace

PamResult pamAuthenticate(const std::string& username, const std::string& password) {
    if (username.empty()) {
        return PamResult::Error;
    }
    struct pam_conv conv = {pamConv, nullptr};
    PasswordHolder holder{password};
    conv.appdata_ptr = &holder;

    pam_handle_t* pamh = nullptr;
    // 服务名 "login"（通用登录栈；多数发行版提供 /etc/pam.d/login）。
    const int startRc = pam_start("login", username.c_str(), &conv, &pamh);
    if (startRc != PAM_SUCCESS) {
        return PamResult::Error;
    }
    const int authRc = pam_authenticate(pamh, 0);
    pam_end(pamh, authRc);

    switch (authRc) {
    case PAM_SUCCESS:
        return PamResult::Ok;
    case PAM_AUTH_ERR:
    case PAM_USER_UNKNOWN:
        return PamResult::Denied;
    case PAM_PERM_DENIED:
    case PAM_AUTHTOK_RECOVERY_ERR:
        // 读不到 shadow（非 root）或服务栈拒绝。
        return PamResult::NoPermission;
    default:
        return PamResult::Error;
    }
}

std::string currentUsername() {
    const char* user = std::getenv("USER");
    if (user != nullptr && *user != '\0') {
        return user;
    }
    if (const char* login = getlogin()) {
        return login;
    }
    const struct passwd* pw = getpwuid(getuid());
    return pw != nullptr ? pw->pw_name : std::string();
}

}  // namespace w10de::lock
