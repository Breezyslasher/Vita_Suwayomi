/**
 * VitaSuwayomi - In-app updater (implementation)
 *
 * One translation unit. Public surface in app_update.hpp; everything else is
 * file-local. Networking / file I/O / process spawning run on a worker thread;
 * every view mutation is marshalled with brls::sync().
 */

#include "utils/app_update.hpp"

#include <borealis.hpp>
#include "utils/http_client.hpp"
#include "utils/async.hpp"
#include "platform/platform.hpp"
#include "app/application.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// ── Platform headers (gated) ────────────────────────────────────────────────
#if defined(__APPLE__)
#include <TargetConditionals.h>            // must precede any TARGET_OS_* test
#endif
#if defined(__APPLE__) && !TARGET_OS_IPHONE
#define VS_MACOS_DESKTOP 1
#else
#define VS_MACOS_DESKTOP 0
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(__linux__) && !defined(ANDROID)
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>     // chmod (make the replacement AppImage executable)
#include <fcntl.h>
#include <filesystem>     // AppImage self-replace rename/copy
#endif

#if VS_MACOS_DESKTOP
#include <unistd.h>
#include <mach-o/dyld.h>
#endif

#if defined(__SWITCH__)
#include <switch.h>
#include <filesystem>
#endif

#if defined(ANDROID) || defined(__ANDROID__)
#include <SDL.h>
#include <jni.h>
#endif

#if defined(__vita__) || defined(__PSV__)
#include "utils/vita_install.hpp"          // vita::installVpk / vita::launchTitle
#include <psp2/io/fcntl.h>
#endif

#ifndef VITA_SUWAYOMI_VERSION
#define VITA_SUWAYOMI_VERSION "0.0.0"
#endif

using namespace vitasuwayomi;

namespace app_update {
namespace {

// ── Configuration ───────────────────────────────────────────────────────────
constexpr const char* kRepo    = "Breezyslasher/Vita_Suwayomi";
constexpr const char* kCurrent = VITA_SUWAYOMI_VERSION;

std::atomic<bool> s_busy{false};    // one check/install at a time
std::atomic<bool> s_cancel{false};  // the progress dialog's Cancel
std::string       s_selfPath;       // argv[0], captured in main()

// A parsed GitHub release.
struct ReleaseInfo {
    std::string tag;        // tag_name (e.g. "Beta-2.2.1" / "v2.2.1")
    std::string pageUrl;    // html_url
    std::string assetUrl;   // browser_download_url of the matched asset ("" ⇒ none)
    std::string assetName;  // matched asset filename
    int64_t     assetSize = 0;
    std::string notes;      // raw markdown body
    bool        prerelease = false;
};

// ── Tiny, dependency-free JSON helpers (string-aware) ───────────────────────

// Advance past the JSON string starting at s[i]=='"' (i at the opening quote),
// honoring backslash escapes. Returns the index just past the closing quote.
size_t skipJsonString(const std::string& s, size_t i) {
    ++i;  // opening quote
    while (i < s.size()) {
        char c = s[i];
        if (c == '\\') { i += 2; continue; }
        if (c == '"') return i + 1;
        ++i;
    }
    return i;
}

// Given the index just after a key's ':' (whitespace allowed), read a string
// value and unescape common sequences. Returns false if the value isn't a string.
bool readStringValue(const std::string& s, size_t pos, std::string& out) {
    while (pos < s.size() && (s[pos]==' '||s[pos]=='\t'||s[pos]=='\n'||s[pos]=='\r')) ++pos;
    if (pos >= s.size() || s[pos] != '"') return false;
    ++pos;
    out.clear();
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '\\' && pos + 1 < s.size()) {
            char n = s[pos + 1];
            switch (n) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case '/': out += '/';  break;
                case 'u': {  // \uXXXX — keep ASCII, drop the rest
                    if (pos + 5 < s.size()) {
                        int code = (int)strtol(s.substr(pos + 2, 4).c_str(), nullptr, 16);
                        if (code >= 0x20 && code < 0x7f) out += (char)code;
                        pos += 4;
                    }
                    break;
                }
                default: out += n; break;
            }
            pos += 2;
            continue;
        }
        if (c == '"') return true;
        out += c;
        ++pos;
    }
    return true;
}

// Find "key" at top-level of the object slice `obj` and return its string value.
// (Good enough for the GitHub feed where the sought keys precede the body.)
std::string jsonString(const std::string& obj, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = 0;
    while ((pos = obj.find(needle, pos)) != std::string::npos) {
        size_t colon = obj.find(':', pos + needle.size());
        if (colon == std::string::npos) return {};
        std::string val;
        if (readStringValue(obj, colon + 1, val)) return val;
        pos += needle.size();
    }
    return {};
}

int64_t jsonInt(const std::string& obj, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return 0;
    size_t colon = obj.find(':', pos + needle.size());
    if (colon == std::string::npos) return 0;
    ++colon;
    while (colon < obj.size() && (obj[colon]==' '||obj[colon]=='\t')) ++colon;
    return (int64_t)strtoll(obj.c_str() + colon, nullptr, 10);
}

bool jsonBool(const std::string& obj, const char* key) {
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = obj.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = obj.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    ++colon;
    while (colon < obj.size() && (obj[colon]==' '||obj[colon]=='\t'||obj[colon]=='\n'||obj[colon]=='\r')) ++colon;
    return obj.compare(colon, 4, "true") == 0;
}

// Return the [start,end) slice of the balanced {...} or [...] beginning at
// `open` (which must point at '{' or '['), honoring strings/escapes.
size_t matchBracket(const std::string& s, size_t open) {
    char oc = s[open], cc = (oc == '{') ? '}' : ']';
    int depth = 0;
    for (size_t i = open; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') { i = skipJsonString(s, i) - 1; continue; }
        if (c == oc) ++depth;
        else if (c == cc) { if (--depth == 0) return i + 1; }
    }
    return s.size();
}

// Invoke fn(objectSlice) for each top-level object of the JSON array `arr`.
template <typename Fn>
void forEachObject(const std::string& arr, Fn fn) {
    size_t i = arr.find('[');
    if (i == std::string::npos) return;
    ++i;
    while (i < arr.size()) {
        while (i < arr.size() && arr[i] != '{' && arr[i] != ']') ++i;
        if (i >= arr.size() || arr[i] == ']') return;
        size_t end = matchBracket(arr, i);
        fn(arr.substr(i, end - i));
        i = end;
    }
}

// ── Version compare ─────────────────────────────────────────────────────────

// Parse the first three numeric runs of a version-ish string, ignoring any
// leading non-digits ("Beta-1.2.6", "v1.2.6", "1.2.6.1448" → {1,2,6}).
void parseVersion(const std::string& v, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    int idx = 0;
    size_t i = 0;
    while (i < v.size() && idx < 3) {
        if (v[i] >= '0' && v[i] <= '9') {
            int n = 0;
            while (i < v.size() && v[i] >= '0' && v[i] <= '9') { n = n*10 + (v[i]-'0'); ++i; }
            out[idx++] = n;
        } else {
            ++i;
        }
    }
}

bool isNewer(const std::string& tag, const std::string& current) {
    int a[3], b[3];
    parseVersion(tag, a);
    parseVersion(current, b);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}

// ── Per-platform asset suffix ───────────────────────────────────────────────
// Matches the release asset naming in .github/workflows/build-multi-platform.yml
// (VitaSuwayomi.<tag>-<suffix>). Empty ⇒ no in-place asset ⇒ browser/defer.

#if defined(__linux__) && !defined(ANDROID)
// AppImage, .deb and the AUR package are the SAME desktop binary — nothing at
// build time says which — so detect the install kind at runtime. AppImage is a
// single user-owned file we can replace in place; Flatpak is sandboxed and
// updates through Flathub, so it (and anything unrecognized) stays browser-only.
enum class LinuxPkg { None, AppImage, Deb, Aur };
std::string selfExePath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    return (n > 0) ? std::string(buf, n) : std::string();
}
LinuxPkg linuxPkg() {
    if (std::getenv("APPIMAGE")) return LinuxPkg::AppImage;   // runtime sets it
    if (std::getenv("FLATPAK_ID") || access("/.flatpak-info", F_OK) == 0)
        return LinuxPkg::None;                                // Flathub-managed
    std::string exe = selfExePath();
    if (!exe.empty()) {
        if (exe.rfind("/usr/lib/VitaSuwayomi/", 0) == 0 && access("/var/lib/dpkg/status", F_OK) == 0)
            return LinuxPkg::Deb;
        if (exe.rfind("/usr/", 0) == 0 && access("/var/lib/pacman", F_OK) == 0)
            return LinuxPkg::Aur;
    }
    return LinuxPkg::None;
}
bool isArm64Linux() {
#if defined(__aarch64__)
    return true;
#else
    return false;
#endif
}
#endif  // linux desktop

#if defined(ANDROID) || defined(__ANDROID__)
bool deviceIsArm64();  // fwd (defined in the Android section)
#endif

std::string assetSuffix() {
#if defined(__vita__) || defined(__PSV__)
    return ".vpk";
#elif defined(__SWITCH__)
  #if defined(USE_DEKO3D)
    return "-nx-deko3d.nro";
  #else
    return "-nx-opengl.nro";
  #endif
#elif defined(ANDROID) || defined(__ANDROID__)
    return deviceIsArm64() ? "-arm64-v8a.apk" : "-armeabi-v7a.apk";
#elif defined(__PS4__)
    return "-ps4.pkg";
#elif defined(__linux__) && !defined(ANDROID)
    switch (linuxPkg()) {
        case LinuxPkg::AppImage: return isArm64Linux() ? "-aarch64.AppImage" : "-x86_64.AppImage";
        case LinuxPkg::Deb:      return isArm64Linux() ? "-Linux_arm64.deb"  : "-Linux_amd64.deb";
        case LinuxPkg::Aur:      return "-Linux.pkg.tar.zst";
        default:                 return {};   // Flatpak / unknown → Flathub / browser
    }
#elif defined(_WIN32)
  #if defined(__i386__) || defined(_M_IX86)
    return "-windows-x86.zip";
  #else
    return "-windows-x64.zip";
  #endif
#elif VS_MACOS_DESKTOP
  #if defined(__aarch64__) || defined(__arm64__)
    return "-macOS-Silicon.dmg";
  #else
    return "-macOS-Intel.dmg";
  #endif
#else
    return {};   // iOS/tvOS/unknown → browser
#endif
}

// Whether this platform installs an asset in place (vs. browser-only). True
// exactly when a non-empty suffix could match; keep in lockstep with the
// dialog primary buttons.
bool inPlaceSupported() { return !assetSuffix().empty(); }

// ── Browser fallback ────────────────────────────────────────────────────────
void openUrl(const std::string& url) {
#if defined(_WIN32)
    ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif VS_MACOS_DESKTOP
    if (fork() == 0) { setsid(); execlp("open", "open", url.c_str(), (char*)nullptr); _exit(127); }
#elif defined(__linux__) && !defined(ANDROID)
    if (fork() == 0) { setsid(); execlp("xdg-open", "xdg-open", url.c_str(), (char*)nullptr); _exit(127); }
#elif defined(ANDROID) || defined(__ANDROID__)
    SDL_OpenURL(url.c_str());
#else
    brls::Application::notify("Visit: " + url);
#endif
}

// ── Settings: skip-this-version ─────────────────────────────────────────────
std::string skippedVersion() {
    return Application::getInstance().getSettings().skippedUpdateVersion;
}
void setSkippedVersion(const std::string& tag) {
    Application::getInstance().getSettings().skippedUpdateVersion = tag;
    Application::getInstance().saveSettings();
}

// ── Progress dialog (built on the UI thread, updated via brls::sync) ─────────
struct ProgressUI {
    brls::Dialog* dialog = nullptr;
    brls::Label*  status = nullptr;
    std::atomic<bool> dismissed{false};
};

std::shared_ptr<ProgressUI> makeProgress(const std::string& title) {
    auto ui = std::make_shared<ProgressUI>();

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(24, 28, 24, 28);
    content->setAlignItems(brls::AlignItems::CENTER);

    auto* t = new brls::Label();
    t->setText(title);
    t->setFontSize(20);
    t->setMarginBottom(12);
    content->addView(t);

    ui->status = new brls::Label();
    ui->status->setText("Starting…");
    ui->status->setFontSize(16);
    ui->status->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    content->addView(ui->status);

    ui->dialog = new brls::Dialog(content);
    ui->dialog->setCancelable(false);
    ui->dialog->addButton("Cancel", [ui]() {
        s_cancel.store(true);
        ui->dismissed.store(true);
    });
    ui->dialog->open();
    return ui;
}

void setProgress(const std::shared_ptr<ProgressUI>& ui, const std::string& text) {
    brls::sync([ui, text]() {
        if (ui->dismissed.load() || !ui->status) return;
        ui->status->setText(text);
    });
}

void finishProgress(const std::shared_ptr<ProgressUI>& ui, std::function<void()> then) {
    brls::sync([ui, then]() {
        if (!ui->dismissed.exchange(true) && ui->dialog) {
            ui->dialog->close([then]() { if (then) then(); });
        } else if (then) {
            then();
        }
    });
}

// ── Simple result dialog ────────────────────────────────────────────────────
void showMessage(const std::string& msg) {
    auto* d = new brls::Dialog(msg);
    d->setCancelable(true);
    d->addButton("OK", []() {});
    d->open();
}

// ── Download the asset with progress + one retry ────────────────────────────
bool downloadAsset(const ReleaseInfo& rel, const std::string& destPath,
                   const std::shared_ptr<ProgressUI>& ui, std::string& err) {
    const int64_t total = rel.assetSize;

    auto attempt = [&]() -> bool {
        std::atomic<int64_t> got{0};
        std::atomic<int64_t> lastShown{-1};
        return platform::writeFileStreamed(destPath, [&](platform::WriteCallback write) -> bool {
            HttpClient client;
            client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
            client.setTimeout(300);
            client.setFollowRedirects(true);
            int64_t liveTotal = total;
            return client.downloadFile(rel.assetUrl,
                [&](const char* data, size_t size) -> bool {
                    if (s_cancel.load()) { err = "Cancelled"; return false; }
                    if (!write(data, size)) { err = "Write failed"; return false; }
                    int64_t g = (got += (int64_t)size);
                    int pct = (liveTotal > 0) ? (int)((g * 100) / liveTotal) : -1;
                    if (pct != lastShown.load()) {
                        lastShown.store(pct);
                        if (pct >= 0)
                            setProgress(ui, "Downloading… " + std::to_string(pct) + "%");
                        else
                            setProgress(ui, "Downloading… " + std::to_string(g / 1024) + " KB");
                    }
                    return true;
                },
                [&](int64_t sz) { if (sz > 0) liveTotal = sz; });
        });
    };

    if (s_cancel.load()) { err = "Cancelled"; return false; }
    if (attempt()) return true;
    if (s_cancel.load()) return false;         // user cancel: no retry
    setProgress(ui, "Retrying download…");
    err.clear();
    return attempt();                          // one automatic retry (truncates)
}

// ── The per-platform install branch ─────────────────────────────────────────
// Returns true if a relaunch/quit was initiated (caller should not touch UI).
bool installDownloaded(const ReleaseInfo& rel, const std::string& path,
                       const std::shared_ptr<ProgressUI>& ui) {
#if defined(__SWITCH__)
    // Unmount the romfs (mapped from the running NRO), then rename over it.
    setProgress(ui, "Installing…");
    romfsExit();
    std::string target = !s_selfPath.empty() ? s_selfPath : platform::path("VitaSuwayomi.nro");
    std::error_code ec;
    std::filesystem::rename(path, target, ec);
    if (ec) { std::filesystem::copy_file(path, target,
              std::filesystem::copy_options::overwrite_existing, ec); }
    finishProgress(ui, []() {
        showMessage("Update installed — relaunch from hbmenu to use it.");
    });
    return false;

#elif defined(__vita__) || defined(__PSV__)
    // A title cannot promote over itself (the installer returns 0x80101114
    // "in use"), so hand the install to the bundled AutoPlugin2 stub (title
    // VSWYUPD01, a different title, so promoting IT is fine). We install and
    // launch the stub, then quit; with VitaSuwayomi closed the stub promotes
    // the downloaded update.vpk and relaunches us. Paths are fixed by
    // convention (see src/updater_stub/main.cpp): the stub reads update.vpk and
    // update_version.txt from the data dir.
    setProgress(ui, "Preparing installer…");
    {
        std::string vpath = platform::path("update_version.txt");
        SceUID vf = sceIoOpen(vpath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (vf >= 0) { sceIoWrite(vf, rel.tag.c_str(), rel.tag.size()); sceIoClose(vf); }
    }
    std::string err;
    int rc = vita::installVpk("app0:updater.vpk", platform::path("updater_stage"), err);
    if (rc != 0) {
        // Couldn't stage the stub — keep the download and fall back to VitaShell.
        finishProgress(ui, [path]() {
            showMessage("Update downloaded.\n\nThe in-app installer couldn't start, "
                        "so open VitaShell and install this file to finish:\n\n" + path);
        });
        return false;
    }
    // Stub installed. Launch it and quit — it takes over from here.
    setProgress(ui, "Installing update…");
    finishProgress(ui, []() {
        auto* d = new brls::Dialog(
            "Installing the update.\n\nVitaSuwayomi will close and reopen on its own "
            "in a few seconds. If it doesn't, relaunch it from the LiveArea.");
        d->setCancelable(false);
        d->addButton("OK", []() {
            vita::launchTitle("VSWYUPD01");
            brls::Application::quit();
        });
        d->open();
    });
    return true;

#elif defined(ANDROID) || defined(__ANDROID__)
    // Hand the APK to the system package installer via JNI (content:// uri).
    setProgress(ui, "Opening installer…");
    JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
    bool handed = false;
    if (env) {
        jclass u = env->FindClass("org/libsdl/app/PlatformUtils");
        if (u) {
            jmethodID m = env->GetStaticMethodID(u, "installApk", "(Ljava/lang/String;)V");
            if (m) {
                jstring js = env->NewStringUTF(path.c_str());
                env->CallStaticVoidMethod(u, m, js);
                env->DeleteLocalRef(js);
                handed = true;
            }
            env->DeleteLocalRef(u);
        }
        if (env->ExceptionCheck()) { env->ExceptionClear(); handed = false; }
    }
    if (handed) {
        finishProgress(ui, []() {});   // system takes over
    } else {
        finishProgress(ui, [rel]() { openUrl(rel.pageUrl); });
    }
    return false;

#elif defined(_WIN32)
    // Detached, windowless cmd unzips over the install dir after MyApp exits.
    setProgress(ui, "Installing…");
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string installDir = exePath;
    size_t slash = installDir.find_last_of("\\/");
    installDir = (slash == std::string::npos) ? "." : installDir.substr(0, slash);

    std::string bat = platform::path("update.bat");
    std::string zip = path;
    for (auto& c : zip)        if (c == '/') c = '\\';
    std::string dir = installDir;
    std::string script;
    script += "@echo off\r\n";
    script += ":waitloop\r\n";
    script += "tasklist /FI \"IMAGENAME eq VitaSuwayomi.exe\" 2>nul | find /I \"VitaSuwayomi.exe\" >nul\r\n";
    script += "if not errorlevel 1 ( ping -n 2 127.0.0.1 >nul & goto waitloop )\r\n";
    script += "where tar >nul 2>&1\r\n";
    script += "if not errorlevel 1 ( tar -xf \"" + zip + "\" -C \"" + dir + "\" ) ^\r\n";
    script += "else ( powershell -NoProfile -NonInteractive -Command ^\r\n";
    script += "  \"Expand-Archive -LiteralPath '" + zip + "' -DestinationPath '" + dir + "' -Force\" )\r\n";
    script += "start \"\" /D \"" + dir + "\" \"" + dir + "\\VitaSuwayomi.exe\"\r\n";
    script += "del \"" + zip + "\" >nul 2>&1\r\n";
    script += "del \"%~f0\" >nul 2>&1\r\n";
    platform::writeFile(bat, script);

    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::string cmd = "cmd.exe /c \"" + bat + "\"";
    std::vector<char> mut(cmd.begin(), cmd.end()); mut.push_back(0);
    CreateProcessA(nullptr, mut.data(), nullptr, nullptr, FALSE,
                   CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    finishProgress(ui, []() { brls::Application::quit(); });
    return true;

#elif defined(__linux__) && !defined(ANDROID)
    switch (linuxPkg()) {
        case LinuxPkg::AppImage: {
            // `path` was downloaded to "<target>.new" on the same filesystem, so
            // rename() atomically swaps it over the running image — the running
            // process keeps the old inode; the new file launches on exec.
            setProgress(ui, "Installing…");
            const char* ai = std::getenv("APPIMAGE");
            std::string target = ai ? std::string(ai) : selfExePath();
            std::string src = path;
            finishProgress(ui, [src, target]() {
                std::error_code ec;
                std::filesystem::rename(src, target, ec);
                if (ec)
                    std::filesystem::copy_file(src, target,
                        std::filesystem::copy_options::overwrite_existing, ec);
                ::chmod(target.c_str(), 0755);
                pid_t pid = fork();   // relaunch the new image, detached
                if (pid == 0) {
                    setsid();
                    sleep(1);         // let this instance quit and free the window
                    execl(target.c_str(), target.c_str(), (char*)nullptr);
                    _exit(127);
                }
                brls::Application::quit();
            });
            return true;
        }
        case LinuxPkg::Deb: {
            setProgress(ui, "Opening installer…");
            std::string p = path;
            finishProgress(ui, [p]() {
                if (fork() == 0) { setsid();
                    execlp("xdg-open", "xdg-open", p.c_str(), (char*)nullptr); _exit(127); }
                showMessage("Your package installer should open to finish the update.");
            });
            return false;
        }
        case LinuxPkg::Aur: {
            std::string p = path;
            finishProgress(ui, [p]() {
                showMessage("Downloaded to:\n" + p +
                            "\n\nInstall with:  sudo pacman -U \"" + p + "\"\n"
                            "(or update via your AUR helper, e.g. yay -Syu)");
            });
            return false;
        }
        default: {
            std::string p = path;
            finishProgress(ui, [p]() {
                showMessage("Downloaded to:\n" + p);
            });
            return false;
        }
    }

#elif VS_MACOS_DESKTOP
    // Detached /bin/sh helper swaps the .app bundle after MyApp exits, reopens.
    setProgress(ui, "Installing…");
    char exe[4096]; uint32_t sz = sizeof(exe);
    std::string bundle;
    if (_NSGetExecutablePath(exe, &sz) == 0) {
        bundle = exe;                                  // …/MyApp.app/Contents/MacOS/VitaSuwayomi
        auto cut = bundle.rfind("/Contents/MacOS/");
        if (cut != std::string::npos) bundle = bundle.substr(0, cut);
    }
    if (bundle.size() < 5 || bundle.substr(bundle.size() - 4) != ".app") {
        finishProgress(ui, [rel]() { openUrl(rel.pageUrl); });   // loose binary → browser
        return false;
    }
    {
        std::string sh = platform::path("update.sh");
        std::string script =
            "#!/bin/sh\n"
            "PID=" + std::to_string(getpid()) + "; DMG='" + path + "'; APP='" + bundle + "'\n"
            "while kill -0 \"$PID\" 2>/dev/null; do sleep 1; done\n"
            "MNT=\"$(mktemp -d /tmp/vswy_dmg.XXXXXX)\"\n"
            "if hdiutil attach \"$DMG\" -nobrowse -readonly -mountpoint \"$MNT\" >/dev/null 2>&1; then\n"
            "  SRC=\"$(find \"$MNT\" -maxdepth 1 -name '*.app' | head -1)\"\n"
            "  if [ -n \"$SRC\" ]; then rm -rf \"$APP.old\";\n"
            "    if mv \"$APP\" \"$APP.old\" 2>/dev/null; then\n"
            "      if ditto \"$SRC\" \"$APP\"; then rm -rf \"$APP.old\";\n"
            "      else rm -rf \"$APP\"; mv \"$APP.old\" \"$APP\" 2>/dev/null; fi\n"
            "    fi\n"
            "  fi\n"
            "  hdiutil detach \"$MNT\" >/dev/null 2>&1 || hdiutil detach \"$MNT\" -force >/dev/null 2>&1\n"
            "fi\n"
            "rmdir \"$MNT\" 2>/dev/null; rm -f \"$DMG\"; open \"$APP\"; rm -f \"$0\"\n";
        platform::writeFile(sh, script);
        std::string shp = sh;
        finishProgress(ui, [shp]() {
            if (fork() == 0) { setsid();
                execl("/bin/sh", "/bin/sh", shp.c_str(), (char*)nullptr); _exit(127); }
            brls::Application::quit();
        });
        return true;
    }

#else
    (void)rel;
    finishProgress(ui, [path]() { showMessage("Downloaded to:\n" + path); });
    return false;
#endif
}

#if !defined(__vita__) && !defined(__PSV__)
std::string downloadExtension() {
    std::string sfx = assetSuffix();
    size_t dot = sfx.find_last_of('.');
    return (dot == std::string::npos) ? std::string(".bin") : sfx.substr(dot);
}
#endif

void startInstall(const ReleaseInfo& rel) {
    auto ui = makeProgress("Updating to " + rel.tag);

    asyncRunLargeStack([rel, ui]() {
#if defined(__vita__) || defined(__PSV__)
        // The updater stub reads a fixed path in the data dir by convention.
        std::string dest = platform::path("update.vpk");
#else
        std::string dest;
#if defined(__linux__) && !defined(ANDROID)
        // AppImage: download next to the running file so the final rename is an
        // atomic same-filesystem swap (and sidesteps ETXTBSY on the running exe).
        if (linuxPkg() == LinuxPkg::AppImage) {
            const char* ai = std::getenv("APPIMAGE");
            dest = ai ? (std::string(ai) + ".new") : platform::path("update.AppImage");
        }
#endif
        if (dest.empty()) {
            platform::createDirRecursive(platform::path("updates"));
            dest = platform::path(std::string("updates/update") + downloadExtension());
        }
#endif

        std::string err;
        bool ok = downloadAsset(rel, dest, ui, err);
        if (!ok) {
            finishProgress(ui, [err]() {
                if (err != "Cancelled")
                    showMessage("Update failed" + (err.empty() ? "" : (": " + err)));
            });
            s_busy.store(false);
            return;
        }
        // Bytes are on disk — from here Cancel is disabled (a half-written
        // target is dangerous).
        installDownloaded(rel, dest, ui);
        s_busy.store(false);
    });
}

// ── The offer dialog ────────────────────────────────────────────────────────
void offerUpdate(const ReleaseInfo& rel, bool manual) {
    std::string msg = "Version " + rel.tag + " is available.\n";
    msg += "You have " + std::string(kCurrent) + ".\n\n";
    if (!rel.notes.empty()) {
        std::string notes = rel.notes;
        if (notes.size() > 400) notes = notes.substr(0, 397) + "...";
        msg += notes;
    }

    auto* d = new brls::Dialog(msg);
    d->setCancelable(true);

    const bool canInstall = !rel.assetUrl.empty();
    if (canInstall) {
        d->addButton("Update now", [rel]() { startInstall(rel); });
    } else {
        d->addButton("Open release page", [rel]() { openUrl(rel.pageUrl); s_busy.store(false); });
    }

    // "Skip this version" only makes sense from the silent path.
    if (!manual) {
        ReleaseInfo r = rel;
        d->addButton("Skip", [r]() { setSkippedVersion(r.tag); s_busy.store(false); });
    }
    d->addButton("Later", []() { s_busy.store(false); });
    d->open();
}

// ── Feed parse ──────────────────────────────────────────────────────────────
// Fill `rel` from the newest non-draft release in the feed. Returns false if
// the feed had no usable release.
bool parseFeed(const std::string& body, ReleaseInfo& rel) {
    bool found = false;
    std::string suffix = assetSuffix();

    forEachObject(body, [&](const std::string& obj) {
        if (found) return;                       // newest first; take the first usable
        if (jsonBool(obj, "draft")) return;

        ReleaseInfo r;
        r.tag        = jsonString(obj, "tag_name");
        r.pageUrl    = jsonString(obj, "html_url");
        r.notes      = jsonString(obj, "body");
        r.prerelease = jsonBool(obj, "prerelease");
        if (r.tag.empty()) return;

        // Match an asset by exact filename tail.
        if (!suffix.empty()) {
            size_t ap = obj.find("\"assets\"");
            if (ap != std::string::npos) {
                size_t lb = obj.find('[', ap);
                if (lb != std::string::npos) {
                    size_t le = matchBracket(obj, lb);
                    std::string assets = obj.substr(lb, le - lb);
                    forEachObject(assets, [&](const std::string& a) {
                        if (!r.assetUrl.empty()) return;
                        std::string name = jsonString(a, "name");
                        if (name.size() >= suffix.size() &&
                            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                            r.assetUrl  = jsonString(a, "browser_download_url");
                            r.assetName = name;
                            r.assetSize = jsonInt(a, "size");
                        }
                    });
                }
            }
        }

        rel = r;
        found = true;
    });

    return found;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

void setSelfPath(const char* argv0) {
    if (argv0) s_selfPath = argv0;
}

void checkForUpdates(bool manual) {
    bool expected = false;
    if (!s_busy.compare_exchange_strong(expected, true)) {
        if (manual) brls::Application::notify("An update check is already running");
        return;
    }
    s_cancel.store(false);

    if (manual) brls::Application::notify("Checking for updates…");

    asyncRun([manual]() {
        HttpClient client;
        client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
        client.setTimeout(20);
        client.setFollowRedirects(true);

        HttpRequest req;
        req.url = std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=10";
        req.headers["Accept"] = "application/vnd.github+json";
        HttpResponse resp = client.request(req);

        if (!resp.success || resp.statusCode < 200 || resp.statusCode >= 300) {
            brls::sync([manual]() {
                if (manual) brls::Application::notify("Couldn't reach the update server");
                s_busy.store(false);
            });
            return;
        }

        ReleaseInfo rel;
        bool have = parseFeed(resp.body, rel) && isNewer(rel.tag, kCurrent);

        brls::sync([manual, have, rel]() {
            if (!have) {
                if (manual) brls::Application::notify("You're on the latest version");
                s_busy.store(false);
                return;
            }
            // Silent check honors a skipped version; manual always shows it.
            if (!manual && rel.tag == skippedVersion()) {
                s_busy.store(false);
                return;
            }
            offerUpdate(rel, manual);
            // s_busy is cleared by whichever dialog button the user presses,
            // or by startInstall when the install flow completes.
        });
    });
}

} // namespace app_update
