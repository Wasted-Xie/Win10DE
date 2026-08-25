// pamcheck —— 锁屏密码验证（PAM，KDE-GAP 高优先 #4）。
//
// 用 libpam（服务名 "login"，通用）验证用户密码。注意：
//   - pam_authenticate 需要能读 /etc/shadow → 通常需 root 权限
//     （锁屏进程默认非 root 时验证必然失败，返回 kNoPermission）；
//   - 建议真机以 setuid root 安装 w10lock（或 systemd 服务提权）；
//   - 验证不可用（权限/服务缺失）时由调用方决定回退策略（MVP：
//     UI 提示"验证服务不可用"并回退任意键解锁，安全限制见文档）。

#pragma once

#include <string>

namespace w10de::lock {

// PAM 验证结果。
enum class PamResult {
    Ok,             // 认证通过
    Denied,         // 密码错误
    NoPermission,   // 无权限/服务缺失（无法读 shadow）
    Error,          // 其他错误
};

// 验证 username + password（PAM "login" 服务）。同步阻塞（锁屏场景可接受）。
PamResult pamAuthenticate(const std::string& username, const std::string& password);

// 当前登录用户名（getenv USER 回退 getlogin）。
std::string currentUsername();

}  // namespace w10de::lock
