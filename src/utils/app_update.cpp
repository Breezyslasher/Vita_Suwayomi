/**
 * VitaSuwayomi - In-app updater (implementation)
 *
 * One translation unit. Public surface in app_update.hpp; everything else is
 * file-local. Networking / file I/O / process spawning run on a worker thread;
 * every view mutation is marshalled with brls::sync().
 */

#include "utils/app_update.hpp"

#include <borealis.hpp>
#include <borealis/views/progress_spinner.hpp>
#include <borealis/views/rectangle.hpp>
#include "utils/http_client.hpp"
#include "utils/update_verify.hpp"
#include "utils/async.hpp"
#include "platform/platform.hpp"
#include "app/application.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <algorithm>   // std::count (table-row detection in the notes parser)
#include <cmath>       // std::ceil (notes height estimate)
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
#include <shellapi.h>     // ShellExecuteA (excluded by WIN32_LEAN_AND_MEAN)
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
#include <sys/stat.h>     // chmod (the detached bundle-swap script)
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

#if defined(__PS4__)
#include "utils/ps4_install.hpp"           // ps4::launchUpdater / installUpdaterApp
#include <thread>
#include <chrono>
#endif

// Common: the download retry backs off before its second attempt.
#include <thread>
#include <chrono>

#ifndef VITA_SUWAYOMI_VERSION
#define VITA_SUWAYOMI_VERSION "0.0.0"
#endif

#ifndef VITA_SUWAYOMI_DISPLAY_VERSION
#define VITA_SUWAYOMI_DISPLAY_VERSION VITA_SUWAYOMI_VERSION
#endif

using namespace vitasuwayomi;

namespace app_update {
namespace {

// ── Configuration ───────────────────────────────────────────────────────────
constexpr const char* kRepo    = "Breezyslasher/Vita_Suwayomi";
// Compare against the DISPLAY version, as the reference does — it is the one
// that corresponds to the release tag ("Beta 2.2.3" vs tag "Beta-2.2.3"). The
// numeric version is a build artifact (it carries a build number, and CI has
// stamped it wrong before), so it is the wrong side of this comparison.
constexpr const char* kCurrent = VITA_SUWAYOMI_DISPLAY_VERSION;

std::atomic<bool> s_busy{false};    // one check/install at a time
std::atomic<bool> s_cancel{false};  // the progress dialog's Cancel
std::string       s_selfPath;       // argv[0], captured in main()

// ── SHA-256 (FIPS 180-4) ────────────────────────────────────────────────────
// Self-contained on purpose: the updater must be able to verify the artifact on
// every platform, including the Windows build, which links no TLS library.
struct Sha256 {
    uint32_t s[8]; uint64_t len; uint8_t buf[64]; size_t n;
    static uint32_t ror(uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }
    void init() {
        static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                                       0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
        for (int i = 0; i < 8; i++) s[i] = iv[i];
        len = 0; n = 0;
    }
    void block(const uint8_t* p) {
        static const uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        uint32_t w[64];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 |
                   (uint32_t)p[i*4+2] << 8 | p[i*4+3];
        for (int i = 16; i < 64; i++) {
            uint32_t s0 = ror(w[i-15],7) ^ ror(w[i-15],18) ^ (w[i-15] >> 3);
            uint32_t s1 = ror(w[i-2],17) ^ ror(w[i-2],19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }
        uint32_t a=s[0],b=s[1],c=s[2],d=s[3],e=s[4],f=s[5],g=s[6],h=s[7];
        for (int i = 0; i < 64; i++) {
            uint32_t S1 = ror(e,6) ^ ror(e,11) ^ ror(e,25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t t1 = h + S1 + ch + k[i] + w[i];
            uint32_t S0 = ror(a,2) ^ ror(a,13) ^ ror(a,22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        s[0]+=a; s[1]+=b; s[2]+=c; s[3]+=d; s[4]+=e; s[5]+=f; s[6]+=g; s[7]+=h;
    }
    void update(const uint8_t* p, size_t l) {
        len += l;
        while (l) {
            size_t take = 64 - n; if (take > l) take = l;
            memcpy(buf + n, p, take); n += take; p += take; l -= take;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    std::string hex() {
        uint64_t bits = len * 8;
        uint8_t pad = 0x80; update(&pad, 1);
        uint8_t z = 0; while (n != 56) update(&z, 1);
        uint8_t b8[8]; for (int i = 0; i < 8; i++) b8[i] = (uint8_t)(bits >> (56 - i*8));
        update(b8, 8);
        static const char* hx = "0123456789abcdef";
        std::string out; out.reserve(64);
        for (int i = 0; i < 8; i++)
            for (int j = 3; j >= 0; j--) {
                uint8_t byte = (uint8_t)(s[i] >> (j*8));
                out += hx[byte >> 4]; out += hx[byte & 15];
            }
        return out;
    }
};

// A parsed GitHub release.
struct ReleaseInfo {
    std::string tag;        // tag_name (e.g. "Beta-2.2.1" / "v2.2.1")
    std::string pageUrl;    // html_url
    std::string assetUrl;   // browser_download_url of the matched asset ("" ⇒ none)
    std::string assetName;  // matched asset filename
    int64_t     assetSize = 0;
    std::string digest;     // "sha256:<hex>" published by GitHub for the asset
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
// Returns false when the string held no number at all. That matters: a version
// we cannot read must never compare as "older than everything", or a build with
// a broken version stamp offers an update on every check, forever.
bool parseVersion(const std::string& v, int out[3]) {
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
    return idx > 0;
}

bool isNewer(const std::string& tag, const std::string& current) {
    int a[3], b[3];
    if (!parseVersion(tag, a) || !parseVersion(current, b)) return false;
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
// Our Android APKs are per-ABI, so match the ABI this build is running as —
// updating a device to the same ABI it already installed always works.
bool deviceIsArm64() {
#if defined(__aarch64__)
    return true;
#else
    return false;
#endif
}
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

#if VS_MACOS_DESKTOP || defined(_WIN32)
// Quote a path for embedding in a generated helper script. These paths are our
// own install/data locations, not attacker-controlled, but a user directory can
// legitimately contain a quote (e.g. /Users/O'Brien) which would otherwise
// break — or silently truncate — the single-quoted assignment.
#if VS_MACOS_DESKTOP
// POSIX sh: close the quote, emit an escaped quote, reopen.  ' -> '\''
std::string shQuote(const std::string& in) {
    std::string out = "'";
    for (char c : in) {
        if (c == '\'') out += "'\\''";
        else           out += c;
    }
    out += "'";
    return out;
}
#endif
#if defined(_WIN32)
// PowerShell single-quoted string: a literal quote is written by doubling it.
std::string psQuote(const std::string& in) {
    std::string out = "'";
    for (char c : in) {
        if (c == '\'') out += "''";
        else           out += c;
    }
    out += "'";
    return out;
}
#endif
#endif

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

// ── Updater UI ──────────────────────────────────────────────────────────────
// Ported from the VitaPlex updater (scrim + panel + step checklist), repainted
// in this app's palette: its Plex gold is replaced by the accent #64B4FF used
// by the History/Browse redesign and the Vita/PS4 updater screens.
namespace tok {
    inline NVGcolor panel()        { return nvgRGB(0x26, 0x26, 0x2a); }
    inline NVGcolor panelLine()    { return nvgRGB(0x45, 0x45, 0x4d); }
    inline NVGcolor hairline()     { return nvgRGB(0x47, 0x47, 0x47); }
    inline NVGcolor accent()       { return nvgRGB(0x64, 0xB4, 0xFF); }
    inline NVGcolor accentBright() { return nvgRGB(0x9A, 0xD2, 0xFF); }
    inline NVGcolor accentInk()    { return nvgRGB(0x0d, 0x22, 0x36); }
    inline NVGcolor tileBg()       { return nvgRGBA(0x64, 0xB4, 0xFF, 33); }
    inline NVGcolor tileBrd()      { return nvgRGBA(0x64, 0xB4, 0xFF, 89); }
    inline NVGcolor cardBg()       { return nvgRGBA(0x64, 0xB4, 0xFF, 23); }
    inline NVGcolor cardBrd()      { return nvgRGBA(0x64, 0xB4, 0xFF, 102); }
    inline NVGcolor green()        { return nvgRGB(0x4E, 0xCC, 0xA3); }
    inline NVGcolor greenBg()      { return nvgRGBA(0x4E, 0xCC, 0xA3, 36); }
    inline NVGcolor greenBrd()     { return nvgRGBA(0x4E, 0xCC, 0xA3, 89); }
    inline NVGcolor text()         { return nvgRGB(0xE7, 0xE7, 0xEA); }
    inline NVGcolor muted()        { return nvgRGB(0xC5, 0xC6, 0xD0); }
    inline NVGcolor muted2()       { return nvgRGB(0x8b, 0x8b, 0x93); }
    inline NVGcolor disabled()     { return nvgRGB(0x6a, 0x6a, 0x70); }
    inline NVGcolor track()        { return nvgRGBA(255, 255, 255, 36); }
    inline NVGcolor btnGray()      { return nvgRGB(0x3e, 0x3e, 0x46); }
    inline NVGcolor scrim()        { return nvgRGBA(10, 9, 14, 150); }
}

// Translucent host so the screen behind shows through the scrim.
class OverlayActivity : public brls::Activity {
public:
    explicit OverlayActivity(brls::Box* content) : brls::Activity(content) {}
    bool isTranslucent() override { return true; }
};

brls::Label* makeLabel(const std::string& text, float size, NVGcolor color,
                       bool singleLine = true) {
    auto* l = new brls::Label();
    l->setText(text);
    l->setFontSize(size);
    l->setTextColor(color);
    l->setSingleLine(singleLine);
    return l;
}

enum class BtnStyle { Accent, Gray, Ghost };

brls::Box* makeButton(const std::string& text, BtnStyle style,
                      std::function<void()> onClick) {
    auto* b = new brls::Box();
    b->setAxis(brls::Axis::ROW);
    b->setJustifyContent(brls::JustifyContent::CENTER);
    b->setAlignItems(brls::AlignItems::CENTER);
    b->setHeight(42.0f);
    b->setCornerRadius(10.0f);
    b->setFocusable(true);
    b->setHighlightCornerRadius(10.0f);
    // Focus = the halo ring only; borealis's highlight fill would wash out an
    // accent-filled button.
    b->setHideHighlightBackground(true);

    NVGcolor fg = tok::text();
    if (style == BtnStyle::Accent) {
        b->setBackgroundColor(tok::accent());
        fg = tok::accentInk();
    } else if (style == BtnStyle::Gray) {
        b->setBackgroundColor(tok::btnGray());
        b->setBorderColor(tok::hairline());
        b->setBorderThickness(1.0f);
    } else {
        fg = tok::muted();
    }
    b->addView(makeLabel(text, 13.5f, fg));

    b->registerClickAction([onClick](brls::View*) {
        if (onClick) onClick();
        return true;
    });
    b->addGestureRecognizer(new brls::TapGestureRecognizer(b));
    return b;
}

std::string mbLabel(int64_t bytes) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", (double)bytes / (1024.0 * 1024.0));
    return buf;
}

float panelWidthFor(float want) {
    float screenW = brls::Application::contentWidth;
    if (screenW <= 0.0f) screenW = 1280.0f;
    if (want + 80.0f > screenW) want = screenW - 80.0f;
    return want;
}

// One row of the progress checklist: a 26px state circle (hairline when
// pending, spinner while active, green check when done), a text line, and an
// optional 5px accent bar underneath.
struct StepRow {
    brls::Box*             icon    = nullptr;
    brls::Label*           glyph   = nullptr;
    brls::ProgressSpinner* spinner = nullptr;
    brls::Label*           text    = nullptr;
    brls::Box*             track   = nullptr;
    brls::Rectangle*       fill    = nullptr;
    float                  barW    = 0.0f;
};

struct ProgressUI {
    StepRow download, install, relaunch;
    brls::Box* cancel = nullptr;
    std::atomic<bool> dismissed{false};
    std::atomic<int>  phase{0};   // 0 = downloading (cancelable), 1 = installing
};

StepRow makeStep(brls::Box* parent, const std::string& label, float barW) {
    StepRow r;
    r.barW = barW;

    auto* row = new brls::Box();
    row->setAxis(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginBottom(6.0f);

    r.icon = new brls::Box();
    r.icon->setWidth(26.0f);
    r.icon->setHeight(26.0f);
    r.icon->setCornerRadius(13.0f);
    r.icon->setJustifyContent(brls::JustifyContent::CENTER);
    r.icon->setAlignItems(brls::AlignItems::CENTER);
    r.icon->setMarginRight(12.0f);
    r.glyph = makeLabel("\xE2\x9C\x93", 13.0f, tok::green());
    r.icon->addView(r.glyph);
    r.spinner = new brls::ProgressSpinner();
    r.spinner->setWidth(16.0f);
    r.spinner->setHeight(16.0f);
    r.icon->addView(r.spinner);
    row->addView(r.icon);

    r.text = makeLabel(label, 12.5f, tok::muted2());
    row->addView(r.text);
    parent->addView(row);

    // The bar sits under the text, aligned past the icon column.
    r.track = new brls::Box();
    r.track->setWidth(barW);
    r.track->setHeight(5.0f);
    r.track->setCornerRadius(2.5f);
    r.track->setBackgroundColor(tok::track());
    r.track->setMarginLeft(38.0f);
    r.track->setMarginBottom(8.0f);
    r.fill = new brls::Rectangle();
    r.fill->setWidth(2.0f);
    r.fill->setHeight(5.0f);
    r.fill->setCornerRadius(2.5f);
    r.fill->setColor(tok::accent());
    r.track->addView(r.fill);
    parent->addView(r.track);

    return r;
}

// Row state changes — UI thread only.
void stepPending(StepRow& r, const std::string& text) {
    r.icon->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    r.icon->setBorderColor(tok::track());
    r.icon->setBorderThickness(1.5f);
    r.glyph->setVisibility(brls::Visibility::GONE);
    r.spinner->setVisibility(brls::Visibility::GONE);
    r.spinner->animate(false);
    r.text->setText(text);
    r.text->setTextColor(tok::muted2());
    r.track->setVisibility(brls::Visibility::GONE);
}

void stepActive(StepRow& r, const std::string& text, float fraction) {
    r.icon->setBackgroundColor(nvgRGBA(0, 0, 0, 0));
    r.icon->setBorderColor(tok::tileBrd());
    r.icon->setBorderThickness(1.5f);
    r.glyph->setVisibility(brls::Visibility::GONE);
    r.spinner->setVisibility(brls::Visibility::VISIBLE);
    r.spinner->animate(true);
    r.text->setText(text);
    r.text->setTextColor(tok::accentBright());
    if (fraction >= 0.0f) {
        float w = r.barW * fraction;
        if (w < 2.0f) w = 2.0f;
        if (w > r.barW) w = r.barW;
        r.fill->setWidth(w);
        r.track->setVisibility(brls::Visibility::VISIBLE);
    } else {
        r.track->setVisibility(brls::Visibility::GONE);
    }
}

void stepDone(StepRow& r, const std::string& text) {
    r.icon->setBackgroundColor(tok::greenBg());
    r.icon->setBorderColor(tok::greenBrd());
    r.icon->setBorderThickness(1.5f);
    r.glyph->setVisibility(brls::Visibility::VISIBLE);
    r.spinner->setVisibility(brls::Visibility::GONE);
    r.spinner->animate(false);
    r.text->setText(text);
    r.text->setTextColor(tok::green());
    r.track->setVisibility(brls::Visibility::GONE);
}

std::shared_ptr<ProgressUI> makeProgress(const std::string& title) {
    auto ui = std::make_shared<ProgressUI>();

    const float panelW = panelWidthFor(428.0f);
    const float barW   = panelW - 36.0f - 38.0f;

    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(tok::scrim());

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(panelW);
    panel->setBackgroundColor(tok::panel());
    panel->setBorderColor(tok::panelLine());
    panel->setBorderThickness(1.0f);
    panel->setCornerRadius(16.0f);
    panel->setShadowType(brls::ShadowType::GENERIC);
    panel->setPadding(16.0f, 18.0f, 12.0f, 18.0f);

    panel->addView(makeLabel(title, 15.0f, tok::text()));
    auto* keepOpen = makeLabel("Keep VitaSuwayomi open until this finishes",
                               11.0f, tok::disabled());
    keepOpen->setMarginTop(3.0f);
    keepOpen->setMarginBottom(14.0f);
    panel->addView(keepOpen);

#if defined(__vita__) || defined(__PSV__)
    const char* relaunchLabel = "Reopens automatically";
#elif defined(__SWITCH__)
    const char* relaunchLabel = "Relaunch to apply";
#elif defined(__PS4__)
    const char* relaunchLabel = "Exit while the system installs";
#elif defined(_WIN32) || VS_MACOS_DESKTOP
    const char* relaunchLabel = "Reopens automatically";
#else
    const char* relaunchLabel = "System installer opens";
#endif

    ui->download = makeStep(panel, "", barW);
    ui->install  = makeStep(panel, "Install", barW);
    ui->relaunch = makeStep(panel, relaunchLabel, barW);
    stepActive(ui->download, "Downloading\xE2\x80\xA6 0%", 0.0f);
    stepPending(ui->install, "Install");
    stepPending(ui->relaunch, relaunchLabel);

    // Cancel is download-only: once the installer is touching the bubble or
    // executable, aborting could leave it half-written.
    auto cancelFn = [ui]() {
        if (ui->phase.load() != 0) return;
        if (ui->dismissed.exchange(true)) return;
        s_cancel.store(true);
        brls::Application::popActivity();
    };
    auto* footer = new brls::Box();
    footer->setAxis(brls::Axis::ROW);
    footer->setJustifyContent(brls::JustifyContent::FLEX_END);
    footer->setMarginTop(4.0f);
    ui->cancel = makeButton("Cancel", BtnStyle::Ghost, cancelFn);
    ui->cancel->setWidth(96.0f);
    footer->addView(ui->cancel);
    panel->addView(footer);

    scrim->addView(panel);
    scrim->registerAction("Cancel", brls::ControllerButton::BUTTON_B,
        [cancelFn](brls::View*) { cancelFn(); return true; });

    brls::Application::pushActivity(new OverlayActivity(scrim));
    brls::Application::giveFocus(ui->cancel);
    return ui;
}

// Drive the download row (phase 0). `fraction` < 0 draws no bar.
void setDownloadProgress(const std::shared_ptr<ProgressUI>& ui,
                         const std::string& text, float fraction) {
    brls::sync([ui, text, fraction]() {
        if (ui->dismissed.load()) return;
        stepActive(ui->download, text, fraction);
    });
}

// Everything after the download drives the install row; the download row is
// marked done the first time we get here.
void setProgress(const std::shared_ptr<ProgressUI>& ui, const std::string& text) {
    brls::sync([ui, text]() {
        if (ui->dismissed.load()) return;
        if (ui->phase.exchange(1) == 0) {
            stepDone(ui->download, "Downloaded");
            // Disabled, not hidden: it keeps focus, and the phase guard makes
            // it inert.
            if (ui->cancel) ui->cancel->setAlpha(0.45f);
        }
        stepActive(ui->install, text, -1.0f);
    });
}

void finishProgress(const std::shared_ptr<ProgressUI>& ui, std::function<void()> then) {
    brls::sync([ui, then]() {
        if (ui->dismissed.exchange(true)) { if (then) then(); }
        else brls::Application::popActivity(brls::TransitionAnimation::FADE, then);
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
// Hashes the bytes as they stream past, so the integrity check costs no extra
// pass over a file that can be hundreds of MB on a memory-tight console.
bool downloadAsset(const ReleaseInfo& rel, const std::string& destPath,
                   const std::shared_ptr<ProgressUI>& ui, std::string& err,
                   std::string& gotDigest) {
    const int64_t total = rel.assetSize;

    auto attempt = [&]() -> bool {
        int64_t finalGot = 0;
        std::atomic<int64_t> got{0};
        std::atomic<int64_t> lastShown{-1};
        Sha256 hash;
        hash.init();
        bool ok = platform::writeFileStreamed(destPath, [&](platform::WriteCallback write) -> bool {
            HttpClient client;
            client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
            // Talks to GitHub, not the user's server, so offline mode ("don't
            // use my server") must not gag it.
            client.setInternetClient(true);
            client.setTimeout(300);
            client.setFollowRedirects(true);
            // Downloading executable code from the public internet: the
            // certificate must be verified (see setVerifyTls).
            client.setVerifyTls(true);
            int64_t liveTotal = total;
            return client.downloadFile(rel.assetUrl,
                [&](const char* data, size_t size) -> bool {
                    if (s_cancel.load()) { err = "Cancelled"; return false; }
                    if (!write(data, size)) { err = "Write failed"; return false; }
                    hash.update(reinterpret_cast<const uint8_t*>(data), size);
                    int64_t g = (got += (int64_t)size);
                    int pct = (liveTotal > 0) ? (int)((g * 100) / liveTotal) : -1;
                    if (pct != lastShown.load()) {
                        lastShown.store(pct);
                        if (pct >= 0)
                            setDownloadProgress(ui, "Downloading\xE2\x80\xA6 " + std::to_string(pct) + "%",
                                                (float)pct / 100.0f);
                        else
                            setDownloadProgress(ui, "Downloading\xE2\x80\xA6 " +
                                                std::to_string(g / 1024) + " KB", -1.0f);
                    }
                    return true;
                },
                [&](int64_t sz) { if (sz > 0) liveTotal = sz; });
        });
        finalGot = got.load();
        // A transfer can end "successfully" short — a truncated response, a
        // proxy cutting the body. The digest check downstream catches that, but
        // only for releases that publish one, so hold the line here too.
        if (ok && total > 0 && finalGot != total) {
            err = "incomplete download (" + std::to_string(finalGot) + "/" +
                  std::to_string(total) + " bytes)";
            ok = false;
        }
        if (ok) gotDigest = hash.hex();
        return ok;
    };

    // Never leave a partial artifact behind: on Windows it sits in the install
    // folder, and everywhere else the next run would find a stale update file.
    auto discard = [&]() { platform::deleteFile(destPath); };

    if (s_cancel.load()) { err = "Cancelled"; return false; }
    if (attempt()) return true;
    if (s_cancel.load()) { discard(); return false; }   // user cancel: no retry
    setDownloadProgress(ui, "Retrying download\xE2\x80\xA6", -1.0f);
    err.clear();
    // A fresh connection clears the transient failures (dropped socket, stale
    // keep-alive); give the far end a moment first, as the reference does.
    std::this_thread::sleep_for(std::chrono::seconds(3));
    if (attempt()) return true;                         // one retry (truncates)
    discard();
    return false;
}

// ── The per-platform install branch ─────────────────────────────────────────
// Returns true if a relaunch/quit was initiated (caller should not touch UI).
bool installDownloaded(const ReleaseInfo& rel, const std::string& path,
                       const std::shared_ptr<ProgressUI>& ui) {
#if defined(__SWITCH__)
    // Unmount the romfs (mapped from the running NRO), then rename over it.
    setProgress(ui, "Installing…");
    romfsExit();
    // argv[0] is only usable if it really is the NRO path — some loaders hand
    // over something else entirely, and writing the update there would drop it
    // in the wrong place (or clobber an unrelated file).
    std::string target = s_selfPath;
    if (target.size() < 4 || target.compare(target.size() - 4, 4, ".nro") != 0)
        target = platform::path("VitaSuwayomi.nro");
    std::error_code ec;
    // Unlink first: the devoptab rename does not implicitly replace an existing
    // file, so renaming straight over the running NRO fails.
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(path, target, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(path, target,
            std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        // Say so instead of quitting as though it worked — the user would
        // otherwise reopen the old build with no idea anything went wrong.
        const std::string why = ec.message();
        finishProgress(ui, [target, why]() {
            showMessage("Update failed: could not replace\n" + target + "\n\n" + why);
        });
        return false;
    }
    // Auto-close, and chain-load the fresh NRO where hbloader supports it (a
    // real auto-relaunch; a clean quit to hbmenu otherwise). No dialog.
    finishProgress(ui, [target]() {
        if (envHasNextLoad())
            envSetNextLoad(target.c_str(), target.c_str());
        brls::Application::quit();
    });
    return true;

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
    // Stub installed. Launch it and quit immediately — no dialog of ours; the
    // stub promotes the update while we're closed and relaunches us. (The Vita
    // shell shows its own "application will close" prompt for the cross-title
    // launch; that's the platform floor, not ours.)
    setProgress(ui, "Installing update…");
    finishProgress(ui, []() {
        vita::launchTitle("VSWYUPD01");
        brls::Application::quit();
    });
    return true;

#elif defined(__PS4__)
    // A running PS4 title can't be replaced in place (BGFT installs into the
    // locked /user/app/<titleid>, and uninstalling the running title kills this
    // process), so hand off to the separate updater helper (VSWY00003): with
    // VitaSuwayomi closed it uninstalls the old title and installs the
    // downloaded pkg via BGFT, which shows its own system progress.
    {
        std::string err;
        // Check it's installed BEFORE launching: launching a title that isn't
        // there pops a system "Cannot start the application" (CE-40841-7)
        // dialog on every attempt.
        if (ps4::isUpdaterInstalled() && ps4::launchUpdater(err) == 0) {
            setProgress(ui, "Handed to updater…");
            // Terminate IMMEDIATELY: the helper waits a few seconds and then
            // uninstalls VSWY00002, so this process must be fully gone by then.
            // An orderly quit() leaves HTTP threads running, and uninstalling a
            // title whose process is still alive is reported as a crash
            // (CE-36329-3).
            finishProgress(ui, []() { std::_Exit(0); });
            return true;
        }
        // The helper isn't installed (it removes itself after each run, so this
        // is the normal path). Its pkg ships inside our own pkg, so install it
        // now, wait for it to become launchable, and go. It's a different
        // title, so this isn't blocked by "already installed".
        std::string setupErr;
        if (ps4::installUpdaterApp(setupErr) == 0) {
            setProgress(ui, "Preparing updater…");
            // BGFT installs in the background. Wait by asking whether the title
            // is installed yet — NOT by trying to launch it.
            bool ready = false;
            for (int i = 0; i < 60 && !ready; ++i) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                ready = ps4::isUpdaterInstalled();
            }
            bool launched = false;
            if (ready) {
                std::this_thread::sleep_for(std::chrono::seconds(3));
                std::string tryErr;
                launched = ps4::launchUpdater(tryErr) == 0;
            }
            if (launched) {
                setProgress(ui, "Handed to updater…");
                finishProgress(ui, []() { std::_Exit(0); });
                return true;
            }
            finishProgress(ui, [path]() {
                showMessage("Update downloaded to\n" + path +
                            "\n\nThe updater is still installing. Try Update again in "
                            "a moment, or close VitaSuwayomi and install that .pkg with "
                            "Itemzflow (or Debug Settings > Install Package).");
            });
            return false;
        }
        finishProgress(ui, [path, setupErr]() {
            showMessage("Update downloaded to\n" + path +
                        "\n\nThe in-app installer couldn't start" +
                        (setupErr.empty() ? "" : (" (" + setupErr + ")")) +
                        ".\nInstall that .pkg with Itemzflow (or Debug Settings > "
                        "Install Package).");
        });
        return false;
    }

#elif defined(ANDROID) || defined(__ANDROID__)
    // Hand the APK to the system package installer via JNI (content:// uri).
    // installApk returns false when the user must still act — on API 26+ it
    // routes them to the "install unknown apps" screen, because without that
    // per-app grant newer Android (Android TV especially) never prompts and the
    // install silently aborts after "Staging app…".
    setProgress(ui, "Handing to system installer…");
    // The JNI hand-off MUST run on the main thread, which is why it lives in
    // the finish callback rather than here: everything above runs on a worker,
    // and FindClass on a thread the VM merely attached resolves against the
    // *system* class loader, which cannot see org/libsdl/app/* at all. From the
    // worker it therefore returned null every time, and we fell through to the
    // browser fallback — the app appeared to just close into a web page and the
    // install-permission prompt was never reached.
    finishProgress(ui, [rel, path]() {
        JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
        bool called = false, launched = false;
        if (env) {
            jclass u = env->FindClass("org/libsdl/app/PlatformUtils");
            if (u) {
                jmethodID m = env->GetStaticMethodID(u, "installApk", "(Ljava/lang/String;)Z");
                if (m) {
                    jstring js = env->NewStringUTF(path.c_str());
                    launched = env->CallStaticBooleanMethod(u, m, js) == JNI_TRUE;
                    env->DeleteLocalRef(js);
                    called = true;
                }
                env->DeleteLocalRef(u);
            }
            if (env->ExceptionCheck()) { env->ExceptionClear(); called = false; launched = false; }
        }
        if (launched) {
            // The system installer took over; do NOT quit — it streams the APK
            // from our in-process provider while staging, and Android
            // force-stops this package itself once the update commits.
            return;
        }
        if (called) {
            // installApk sent us to the "install unknown apps" screen. Without
            // that per-app grant newer Android (Android TV especially) never
            // prompts and the install silently aborts after "Staging app…".
            showMessage("Allow VitaSuwayomi to install apps, then choose Update again.\n\n"
                        "Android needs a one-time \"install unknown apps\" permission "
                        "before it can install the update.");
            return;
        }
        openUrl(rel.pageUrl);
    });
    return false;

#elif defined(_WIN32)
    // Detached, windowless cmd unzips over the install dir after MyApp exits.
    setProgress(ui, "Installing…");
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    std::string installDir = exePath;
    size_t slash = installDir.find_last_of("\\/");
    installDir = (slash == std::string::npos) ? "." : installDir.substr(0, slash);

    auto toWin = [](std::string s) {
        for (char& c : s) if (c == '/') c = '\\';
        return s;
    };
    const std::string zip = toWin(path);
    const std::string dir = toWin(installDir);
    // Keep the script beside the install so the detached cmd can always reach
    // it, exactly like the reference implementation.
    const std::string bat = installDir + "\\vitasuwayomi_update.bat";

    // Block form, not one-liners: `if (...) ^ / else (...)` and
    // `( ... & goto label )` are the two shapes cmd mishandles, and they are
    // why the generated script did nothing. CRLF endings matter too — cmd
    // mis-parses a bare-LF .bat (platform::writeFile writes binary, so the
    // \r\n survive).
    std::string script;
    script += "@echo off\r\n";
    script += ":waitloop\r\n";
    script += "tasklist /FI \"IMAGENAME eq VitaSuwayomi.exe\" 2>nul | find /I \"VitaSuwayomi.exe\" >nul\r\n";
    script += "if not errorlevel 1 (\r\n";
    script += "  ping -n 2 127.0.0.1 >nul\r\n";
    script += "  goto waitloop\r\n";
    script += ")\r\n";
    script += "where tar >nul 2>&1\r\n";
    script += "if not errorlevel 1 (\r\n";
    script += "  tar -xf \"" + zip + "\" -C \"" + dir + "\"\r\n";
    script += ") else (\r\n";
    script += "  powershell -NoProfile -NonInteractive -Command \"Expand-Archive -LiteralPath "
              + psQuote(zip) + " -DestinationPath " + psQuote(dir) + " -Force\"\r\n";
    script += ")\r\n";
    script += "start \"\" /D \"" + dir + "\" \"" + dir + "\\VitaSuwayomi.exe\"\r\n";
    script += "del \"" + zip + "\" >nul 2>&1\r\n";
    script += "del \"%~f0\" >nul 2>&1\r\n";
    if (!platform::writeFile(bat, script)) {
        finishProgress(ui, []() {
            showMessage("Update failed: could not write the updater script next to the app.");
        });
        return false;
    }

    STARTUPINFOA si{}; si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    std::string cmd = "cmd.exe /c \"" + bat + "\"";
    std::vector<char> mut(cmd.begin(), cmd.end()); mut.push_back(0);
    // Windowless; the child has no job object tying it to us, so it survives to
    // do the swap once we are gone.
    if (!CreateProcessA(nullptr, mut.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Quitting now would just close the app and leave the update unapplied.
        finishProgress(ui, []() {
            showMessage("Update failed: could not start the updater script.");
        });
        return false;
    }
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
        case LinuxPkg::Deb:
        case LinuxPkg::Aur: {
            // Hand the package to the system installer, then quit immediately —
            // dpkg/pacman can't swap the binary under a live process, and a
            // lingering window would leave the old build running. No dialog; the
            // detached xdg-open survives the quit. The user reopens after the
            // system install completes.
            setProgress(ui, "Opening installer…");
            std::string p = path;
            finishProgress(ui, [p]() {
                pid_t pid = fork();
                if (pid == 0) { setsid();
                    execlp("xdg-open", "xdg-open", p.c_str(), (char*)nullptr); _exit(127); }
                brls::Application::quit();
            });
            return true;
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
            "PID=" + std::to_string(getpid()) +
            "; DMG=" + shQuote(path) + "; APP=" + shQuote(bundle) + "\n"
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
        if (!platform::writeFile(sh, script)) {
            // Without the script the fork below would exec nothing and we would
            // quit for no reason, so stop here and keep the app running.
            finishProgress(ui, []() {
                showMessage("Update failed: could not write the updater script.");
            });
            return false;
        }
        ::chmod(sh.c_str(), 0755);
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
#elif defined(__PS4__)
        // Likewise the PS4 helper: /data/VitaSuwayomi/update.pkg by convention.
        std::string dest = platform::path("update.pkg");
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
#if defined(_WIN32)
        // Straight into the install folder (absolute), so the detached cmd
        // resolves it no matter what working directory it ends up with.
        {
            char exeBuf[MAX_PATH] = {0};
            GetModuleFileNameA(nullptr, exeBuf, MAX_PATH);
            std::string d = exeBuf;
            size_t sl = d.find_last_of("\\/");
            if (sl != std::string::npos) dest = d.substr(0, sl) + "\\update.zip";
        }
#endif
        if (dest.empty()) {
            platform::createDirRecursive(platform::path("updates"));
            dest = platform::path(std::string("updates/update") + downloadExtension());
        }
#endif

        std::string err;
        std::string gotDigest;
        bool ok = downloadAsset(rel, dest, ui, err, gotDigest);
        if (!ok) {
            finishProgress(ui, [err]() {
                if (err != "Cancelled")
                    showMessage("Update failed" + (err.empty() ? "" : (": " + err)));
            });
            s_busy.store(false);
            return;
        }

        // Integrity gate — never install bytes we haven't checked. GitHub
        // publishes a per-asset SHA-256 ("sha256:<hex>") in the same feed; we
        // hashed the stream as it downloaded. A mismatch means the file was
        // corrupted or substituted, so delete it and stop rather than hand it
        // to an installer that will execute it.
        {
            std::string want = rel.digest;
            const std::string kPrefix = "sha256:";
            if (want.rfind(kPrefix, 0) == 0) want = want.substr(kPrefix.size());
            for (auto& c : want) c = (char)tolower((unsigned char)c);

            if (want.empty()) {
                // Older releases carry no digest. The feed itself came over a
                // verified connection, so continue, but say so in the log.
                brls::Logger::warning("app_update: release {} publishes no asset digest; "
                                      "installing without a checksum check", rel.tag);
            } else if (want != gotDigest) {
                brls::Logger::error("app_update: digest mismatch for {} (want {}, got {})",
                                    rel.assetName, want, gotDigest);
                platform::deleteFile(dest);
                finishProgress(ui, []() {
                    showMessage("Update aborted: the downloaded file failed its integrity "
                                "check.\n\nIt was deleted. Try again, and if this keeps "
                                "happening download the release from GitHub instead.");
                });
                s_busy.store(false);
                return;
            } else {
                brls::Logger::info("app_update: verified SHA-256 of {}", rel.assetName);
            }
        }

        // Authenticity gate. When this build has an update-signing key compiled
        // in (updateSignatureEnforced()), the artifact must also carry a
        // matching detached signature, published beside it as "<asset>.sig".
        // The digest above proves the bytes match what the feed advertised;
        // this proves the release itself came from the project, so it also
        // survives a compromised release host or a future TLS weakness.
        // Inert until a key is added — see docs/update-signing.md.
        if (updateSignatureEnforced()) {
            setProgress(ui, "Verifying signature…");
            HttpClient sigClient;
            sigClient.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
            sigClient.setInternetClient(true);
            sigClient.setVerifyTls(true);
            HttpRequest sigReq;
            sigReq.url             = rel.assetUrl + ".sig";
            sigReq.timeout         = 20;
            sigReq.followRedirects = true;
            HttpResponse sigResp = sigClient.request(sigReq);

            std::string vErr;
            const bool okSig = sigResp.statusCode == 200 && !sigResp.body.empty() &&
                               verifyUpdateFile(dest, sigResp.body, vErr);
            if (!okSig) {
                if (vErr.empty())
                    vErr = "could not fetch signature (HTTP " +
                           std::to_string(sigResp.statusCode) + ")";
                brls::Logger::error("app_update: signature check failed: {}", vErr);
                platform::deleteFile(dest);
                finishProgress(ui, [vErr]() {
                    showMessage("Update aborted: signature check failed.\n\n" + vErr +
                                "\n\nThe download was deleted.");
                });
                s_busy.store(false);
                return;
            }
            brls::Logger::info("app_update: signature verified for {}", rel.assetName);
        }

        // Bytes are on disk and verified — from here Cancel is disabled (a
        // half-written target is dangerous).
        installDownloaded(rel, dest, ui);
        s_busy.store(false);
    });
}

// ── Release notes → sheet lines ─────────────────────────────────────────────
// Ported from the reference's parseNotes. Its shape is hand-written markdown:
// an H1 title, **Date:**/**Status:**/**PRs:** meta lines, a blockquote note,
// then `---`-separated `## Section`s of `- **Lead** — description` bullets.
//
// Release bodies are markdown, and only markdown. The one extra flavour the
// reference never had to read is GitHub's auto-generated body — "## What's
// Changed" over "* <title> by @user in <pull url>" lines and a
// "**Full Changelog**" link — so link handling collapses a pull URL to "#331"
// and drops bare URLs, which are dead weight on a console with no browser.

struct NoteLine {
    enum Kind { Section, Bullet, Para } kind;
    std::string lead;   // bullets only: the bold lead, may be empty
    std::string text;
};

struct ParsedNotes {
    std::string date;   // "August 24, 2026" from the meta lines
    std::string prs;    // "#327, #328"
    std::vector<NoteLine> lines;
    int sections = 0;
};

std::string trimSpace(const std::string& in) {
    size_t a = in.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = in.find_last_not_of(" \t\r\n");
    return in.substr(a, b - a + 1);
}

// Collapse [text](url) → text, turn a pull/issue URL into "#N", drop any other
// bare URL, and strip stray **/` pairs.
std::string cleanInline(const std::string& in) {
    std::string s = in;

    // [text](url) → text
    for (size_t i = 0; (i = s.find('[', i)) != std::string::npos;) {
        size_t close = s.find(']', i);
        if (close == std::string::npos) break;
        if (close + 1 < s.size() && s[close + 1] == '(') {
            size_t end = s.find(')', close);
            if (end != std::string::npos) {
                s = s.substr(0, i) + s.substr(i + 1, close - i - 1) + s.substr(end + 1);
                continue;
            }
        }
        i = close + 1;
    }

    // Bare URLs: "…/pull/331" becomes "#331"; anything else goes.
    for (size_t i = 0; (i = s.find("http", i)) != std::string::npos;) {
        size_t end = s.find_first_of(" \t\r\n)", i);
        if (end == std::string::npos) end = s.size();
        std::string url = s.substr(i, end - i);
        std::string repl;
        for (const char* seg : {"/pull/", "/issues/"}) {
            size_t p = url.find(seg);
            if (p == std::string::npos) continue;
            std::string num = url.substr(p + std::strlen(seg));
            size_t k = 0;
            while (k < num.size() && num[k] >= '0' && num[k] <= '9') ++k;
            if (k > 0) repl = "#" + num.substr(0, k);
            break;
        }
        s = s.substr(0, i) + repl + s.substr(end);
        i += repl.size();
    }

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '`') continue;
        if (s[i] == '*' && i + 1 < s.size() && s[i + 1] == '*') { i++; continue; }
        out += s[i];
    }
    return trimSpace(out);
}

ParsedNotes parseNotes(const std::string& md) {
    ParsedNotes out;
    size_t pos = 0;
    while (pos <= md.size()) {
        size_t eol = md.find('\n', pos);
        std::string line = md.substr(pos, eol == std::string::npos ? std::string::npos
                                                                   : eol - pos);
        pos = (eol == std::string::npos) ? md.size() + 1 : eol + 1;

        line = trimSpace(line);
        if (line.empty()) continue;

        // Meta lines fold into the sheet's header caption; Status is skipped —
        // the Pre-release chip comes from the release JSON instead.
        if (line.rfind("**Date:**", 0) == 0)   { out.date = cleanInline(line.substr(9)); continue; }
        if (line.rfind("**PRs:**", 0) == 0)    { out.prs  = cleanInline(line.substr(8)); continue; }
        if (line.rfind("**Status:**", 0) == 0) continue;

        if (line.rfind("## ", 0) == 0) {
            out.lines.push_back({NoteLine::Section, "", cleanInline(line.substr(3))});
            out.sections++;
            continue;
        }
        // The H1 repeats the tag; blockquotes and rules are separators; a table
        // row survives as a Para, which reads badly, so drop the pipe rules.
        if (line[0] == '#' || line[0] == '>' || line.rfind("---", 0) == 0) continue;
        if (line.find_first_not_of("|-: ") == std::string::npos) continue;  // table rule

        // A markdown table row. Three columns can't line up inside one wrapped
        // label, so join the cells with a separator and let it read as prose.
        if (std::count(line.begin(), line.end(), '|') >= 2) {
            std::string joined;
            size_t c = 0;
            while (c < line.size()) {
                size_t bar = line.find('|', c);
                std::string cell = trimSpace(line.substr(c, bar == std::string::npos
                                                            ? std::string::npos : bar - c));
                if (!cell.empty())
                    joined += (joined.empty() ? "" : " \xC2\xB7 ") + cleanInline(cell);
                if (bar == std::string::npos) break;
                c = bar + 1;
            }
            if (!joined.empty()) out.lines.push_back({NoteLine::Para, "", joined});
            continue;
        }

        if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0) {
            std::string bodyText = line.substr(2);
            NoteLine n{NoteLine::Bullet, "", ""};
            if (bodyText.rfind("**", 0) == 0) {
                size_t close = bodyText.find("**", 2);
                if (close != std::string::npos) {
                    n.lead = cleanInline(bodyText.substr(2, close - 2));
                    std::string rest = bodyText.substr(close + 2);
                    // The lead stands on its own line, so a leading " — "
                    // joiner would dangle at the start of the description.
                    size_t r = 0;
                    while (r < rest.size() &&
                           (rest[r] == ' ' || rest[r] == '-' ||
                            rest.compare(r, 3, "\xE2\x80\x94") == 0)) {
                        r += (rest[r] == ' ' || rest[r] == '-') ? 1 : 3;
                    }
                    n.text = cleanInline(rest.substr(r));
                }
            }
            if (n.lead.empty()) n.text = cleanInline(bodyText);
            if (n.lead.empty() && n.text.empty()) continue;
            out.lines.push_back(std::move(n));
            continue;
        }

        std::string para = cleanInline(line);
        if (para.empty()) continue;      // a line that was only a bare URL
        // "Full Changelog: <url>" loses its URL above and leaves a dangling
        // label. A real paragraph ending in a colon didn't have a link in it.
        if (para.back() == ':' && line.find("http") != std::string::npos) continue;
        out.lines.push_back({NoteLine::Para, "", para});
    }
    return out;
}

// ── Notes height estimate ───────────────────────────────────────────────────
// The sheet sizes itself to its notes. Its ScrollingFrame detaches the content
// view from the layout tree so the content can grow freely, which also means
// the frame has no intrinsic height to lay out against — it has to be told one
// before anything is measured. So walk the parsed lines and add up what they
// will occupy, mirroring the margins and font sizes the sheet actually sets.
// It is an estimate: being a little over just leaves some slack at the bottom,
// and being a little under means the last line scrolls.
float estimateNotesHeight(const ParsedNotes& notes, float panelW) {
    // content padding is (top 16, right 26, bottom 18, left 22)
    const float textW   = panelW - 22.0f - 26.0f;
    const float bulletW = textW - 15.0f;             // dot + its margin

    // Rough average advance for the UI font; the body copy is 12.5px.
    auto wrapped = [](const std::string& s, float width, float fontSize) -> float {
        if (s.empty()) return 0.0f;
        float cols = width / (fontSize * 0.52f);
        if (cols < 8.0f) cols = 8.0f;
        float lines = std::ceil((float)s.size() / cols);
        return lines < 1.0f ? 1.0f : lines;
    };

    float h = 16.0f + 18.0f;                          // content padding
    if (notes.lines.empty()) return h + 40.0f + 22.0f;   // the "no notes" label

    bool first = true;
    for (const NoteLine& n : notes.lines) {
        if (n.kind == NoteLine::Section) {
            h += (first ? 2.0f : 20.0f) + 20.0f + 3.0f;      // margins + row
        } else if (n.kind == NoteLine::Bullet) {
            h += 10.0f;                                       // marginTop
            if (!n.lead.empty()) h += 19.0f;
            if (!n.text.empty()) {
                if (!n.lead.empty()) h += 2.0f;
                h += wrapped(n.text, bulletW, 12.5f) * 17.0f; // 12.5 * 1.35
            }
        } else {
            h += 8.0f + wrapped(n.text, textW, 12.5f) * 17.0f;
        }
        first = false;
    }
    return h;
}

// Clamp the notes area so the sheet is never a sliver and never taller than the
// screen. The chrome is the header (14+38+14), the two hairlines and the footer
// (12+42+13) — subtract it so the PANEL, not the notes, is what fits.
float clampNotesHeight(float wanted, float screenH) {
    const float chrome = 66.0f + 1.0f + 67.0f + 1.0f;
    float maxH = screenH - 76.0f - chrome;
    if (maxH < 120.0f) maxH = 120.0f;
    if (wanted > maxH)  return maxH;
    if (wanted < 96.0f) return 96.0f;
    return wanted;
}

// ── The What's New sheet ────────────────────────────────────────────────────
// Pushed on top of the offer sheet; B returns to it. The header carries the
// tag, date, size and PR range plus a Pre-release chip; the notes scroll with
// up/down while focus stays on the footer, and the primary mirrors the offer's
// action so the user can update from here without going back.
void showNotesSheet(const ReleaseInfo rel) {
    const ParsedNotes notes = parseNotes(rel.notes);

    // A reading surface — wider than the offer, and on a TV across the room.
    float screenW = brls::Application::contentWidth;
    float screenH = brls::Application::contentHeight;
    if (screenW <= 0.0f) screenW = 1280.0f;
    if (screenH <= 0.0f) screenH = 720.0f;
    float panelW = 620.0f;
    if (panelW + 80.0f > screenW) panelW = screenW - 80.0f;

    // The sheet is as tall as its notes, capped so long ones scroll instead of
    // running off the screen. A ScrollingFrame detaches its content view from
    // the tree, so it has no height of its own to measure — the alternative to
    // estimating is what this used to do, which was pin the panel to the full
    // screen and leave a two-line release floating in a wall of empty panel.
    const float wantH  = estimateNotesHeight(notes, panelW);
    const float notesH = clampNotesHeight(wantH, screenH);
    const bool  scrolls = wantH > notesH + 1.0f;

    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(tok::scrim());

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(panelW);
    // No explicit height: the column sizes to the header, the notes area (which
    // IS given a height, below) and the footer.
    panel->setBackgroundColor(tok::panel());
    panel->setBorderColor(tok::panelLine());
    panel->setBorderThickness(1.0f);
    panel->setCornerRadius(16.0f);
    panel->setShadowType(brls::ShadowType::GENERIC);
    panel->setClipsToBounds(true);

    // ── Header ──────────────────────────────────────────────────────────
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(14.0f, 20.0f, 14.0f, 20.0f);

    auto* tile = new brls::Box();
    tile->setWidth(38.0f);
    tile->setHeight(38.0f);
    tile->setCornerRadius(10.0f);
    tile->setBackgroundColor(tok::tileBg());
    tile->setBorderColor(tok::tileBrd());
    tile->setBorderThickness(1.0f);
    tile->setJustifyContent(brls::JustifyContent::CENTER);
    tile->setAlignItems(brls::AlignItems::CENTER);
    tile->addView(makeLabel("\xE2\x89\xA1", 18.0f, tok::accent()));
    tile->setMarginRight(12.0f);
    header->addView(tile);

    auto* titles = new brls::Box();
    titles->setAxis(brls::Axis::COLUMN);
    titles->setShrink(1.0f);
    titles->addView(makeLabel(rel.tag, 16.5f, tok::text()));
    {
        std::string caption = notes.date;
        if (rel.assetSize > 0)
            caption += (caption.empty() ? "" : " \xC2\xB7 ") + mbLabel(rel.assetSize) + " MB";
        if (!notes.prs.empty())
            caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("PRs ") + notes.prs;
        if (!caption.empty()) {
            auto* cap = makeLabel(caption, 12.0f, tok::disabled());
            cap->setMarginTop(2.0f);
            titles->addView(cap);
        }
    }
    header->addView(titles);

    auto* hspacer = new brls::Box();
    hspacer->setGrow(1.0f);
    header->addView(hspacer);

    if (rel.prerelease) {
        auto* chip = new brls::Box();
        chip->setAxis(brls::Axis::ROW);
        chip->setAlignItems(brls::AlignItems::CENTER);
        chip->setHeight(24.0f);
        chip->setPadding(0.0f, 11.0f, 0.0f, 11.0f);
        chip->setCornerRadius(12.0f);
        chip->setBackgroundColor(tok::tileBg());
        chip->setBorderColor(tok::tileBrd());
        chip->setBorderThickness(1.0f);
        chip->addView(makeLabel("Pre-release", 10.5f, tok::accentBright()));
        chip->setMarginLeft(10.0f);
        header->addView(chip);
    }
    panel->addView(header);

    auto* headerRule = new brls::Box();
    headerRule->setHeight(1.0f);
    headerRule->setAlignSelf(brls::AlignSelf::STRETCH);
    headerRule->setBackgroundColor(tok::hairline());
    panel->addView(headerRule);

    // ── Notes area ──────────────────────────────────────────────────────
    auto* scroller = new brls::ScrollingFrame();
    scroller->setHeight(notesH);

    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(16.0f, 26.0f, 18.0f, 22.0f);   // width comes from the frame

    if (notes.lines.empty()) {
        auto* empty = makeLabel("No notes for this release.", 13.5f, tok::muted2());
        empty->setMarginTop(40.0f);
        empty->setHorizontalAlign(brls::HorizontalAlign::CENTER);
        content->addView(empty);
    }

    bool first = true;
    for (const NoteLine& n : notes.lines) {
        if (n.kind == NoteLine::Section) {
            auto* row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setAlignItems(brls::AlignItems::CENTER);
            row->setMarginTop(first ? 2.0f : 20.0f);
            row->setMarginBottom(3.0f);
            auto* tick = new brls::Rectangle();
            tick->setWidth(4.0f);
            tick->setHeight(16.0f);
            tick->setCornerRadius(2.0f);
            tick->setColor(tok::accent());
            tick->setMarginRight(9.0f);
            row->addView(tick);
            row->addView(makeLabel(n.text, 15.0f, tok::text()));
            content->addView(row);
        } else if (n.kind == NoteLine::Bullet) {
            auto* row = new brls::Box();
            row->setAxis(brls::Axis::ROW);
            row->setAlignItems(brls::AlignItems::FLEX_START);
            row->setMarginTop(10.0f);
            auto* dot = new brls::Rectangle();
            dot->setWidth(5.0f);
            dot->setHeight(5.0f);
            dot->setCornerRadius(2.5f);
            dot->setColor(tok::muted2());
            dot->setMarginTop(7.0f);
            dot->setMarginRight(10.0f);
            row->addView(dot);
            auto* col = new brls::Box();
            col->setAxis(brls::Axis::COLUMN);
            col->setShrink(1.0f);
            // A borealis label is one colour throughout, so the bold lead takes
            // its own line and the description wraps below it.
            if (!n.lead.empty())
                col->addView(makeLabel(n.lead, 13.5f, tok::text()));
            if (!n.text.empty()) {
                auto* t = makeLabel(n.text, 12.5f, tok::muted(), false);
                t->setLineHeight(1.35f);
                if (!n.lead.empty()) t->setMarginTop(2.0f);
                col->addView(t);
            }
            row->addView(col);
            content->addView(row);
        } else {
            auto* p = makeLabel(n.text, 12.5f, tok::muted(), false);
            p->setLineHeight(1.35f);
            p->setMarginTop(8.0f);
            content->addView(p);
        }
        first = false;
    }
    scroller->setContentView(content);
    panel->addView(scroller);

    auto* footerRule = new brls::Box();
    footerRule->setHeight(1.0f);
    footerRule->setAlignSelf(brls::AlignSelf::STRETCH);
    footerRule->setBackgroundColor(tok::hairline());
    panel->addView(footerRule);

    // ── Footer ──────────────────────────────────────────────────────────
    auto* footer = new brls::Box();
    footer->setAxis(brls::Axis::ROW);
    footer->setAlignItems(brls::AlignItems::CENTER);
    footer->setPadding(12.0f, 20.0f, 13.0f, 20.0f);

    // Only claim there's more to scroll to when there actually is.
    if (notes.sections > 0) {
        std::string hint = std::to_string(notes.sections) +
                           (notes.sections == 1 ? " section" : " sections");
        if (scrolls) hint = "Scroll for more \xC2\xB7 " + hint;
        footer->addView(makeLabel(hint, 11.5f, tok::disabled()));
    }
    auto* fspacer = new brls::Box();
    fspacer->setGrow(1.0f);
    footer->addView(fspacer);

    brls::Box* primary = nullptr;
    if (!rel.assetUrl.empty()) {
        primary = makeButton("Update now", BtnStyle::Accent, [rel]() {
            // Pop the sheet, then the offer beneath it, then install.
            brls::Application::popActivity(brls::TransitionAnimation::NONE, [rel]() {
                brls::Application::popActivity(brls::TransitionAnimation::NONE,
                                               [rel]() { startInstall(rel); });
            });
        });
    } else {
        primary = makeButton("Open release page", BtnStyle::Accent, [rel]() {
            openUrl(rel.pageUrl);
            s_busy.store(false);
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                []() { brls::Application::popActivity(); });
        });
    }
    primary->setWidth(170.0f);
    footer->addView(primary);

    auto* back = makeButton("Back", BtnStyle::Ghost, []() {
        brls::Application::popActivity();
    });
    back->setWidth(84.0f);
    back->setMarginLeft(8.0f);
    footer->addView(back);
    panel->addView(footer);

    scrim->addView(panel);

    // Up/down scrolls the notes directly — focus stays on the footer buttons.
    auto scrollBy = [scroller, content](float delta) {
        float maxY = content->getHeight() - scroller->getHeight();
        if (maxY < 0.0f) maxY = 0.0f;
        float y = scroller->getContentOffsetY() + delta;
        if (y < 0.0f) y = 0.0f;
        if (y > maxY) y = maxY;
        scroller->setContentOffsetY(y, true);
    };
    scrim->registerAction("Scroll up", brls::ControllerButton::BUTTON_UP,
        [scrollBy](brls::View*) { scrollBy(-72.0f); return true; }, true);
    scrim->registerAction("Scroll down", brls::ControllerButton::BUTTON_DOWN,
        [scrollBy](brls::View*) { scrollBy(72.0f); return true; }, true);
    scrim->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [](brls::View*) { brls::Application::popActivity(); return true; });
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim,
        []() { brls::Application::popActivity(); }));

    brls::Application::pushActivity(new OverlayActivity(scrim));
    brls::Application::giveFocus(primary);
}

// ── The offer dialog ────────────────────────────────────────────────────────
// The offer sheet: scrim + panel, accent strip, icon tile, current→new version
// cards, footer actions. The notes live behind the What's New button, in the
// sheet above. Ported from the VitaPlex updater sheet and repainted in this
// app's accent.
void offerUpdate(const ReleaseInfo& rel, bool manual) {
    // The footer can carry four buttons (Update now / What's New / Skip /
    // Later). At the old 428 they did not fit, and since the row is
    // right-aligned the overflow ran off the LEFT edge — the primary was the
    // one getting clipped. Size the panel to the row it actually has to hold.
    float want = 300.0f;                                  // primary + padding
    if (!rel.notes.empty()) want += 136.0f;               // What's New
    if (!manual)            want += 92.0f;                // Skip
    want += 92.0f;                                        // Later
    const float panelW = panelWidthFor(want);

    auto* scrim = new brls::Box();
    scrim->setAxis(brls::Axis::COLUMN);
    scrim->setWidthPercentage(100.0f);
    scrim->setHeightPercentage(100.0f);
    scrim->setJustifyContent(brls::JustifyContent::CENTER);
    scrim->setAlignItems(brls::AlignItems::CENTER);
    scrim->setBackgroundColor(tok::scrim());

    auto* panel = new brls::Box();
    panel->setAxis(brls::Axis::COLUMN);
    panel->setWidth(panelW);
    panel->setBackgroundColor(tok::panel());
    panel->setBorderColor(tok::panelLine());
    panel->setBorderThickness(1.0f);
    panel->setCornerRadius(16.0f);
    panel->setShadowType(brls::ShadowType::GENERIC);
    // The accent strip runs flush along the top edge; the rounded corners clip it.
    panel->setClipsToBounds(true);

    auto* strip = new brls::Box();
    strip->setHeight(5.0f);
    strip->setAlignSelf(brls::AlignSelf::STRETCH);
    strip->setBackgroundColor(tok::accent());
    panel->addView(strip);

    // ── Header: icon tile + titles ──────────────────────────────────────
    auto* header = new brls::Box();
    header->setAxis(brls::Axis::ROW);
    header->setAlignItems(brls::AlignItems::CENTER);
    header->setPadding(16.0f, 18.0f, 12.0f, 18.0f);

    auto* tile = new brls::Box();
    tile->setWidth(52.0f);
    tile->setHeight(52.0f);
    tile->setCornerRadius(14.0f);
    tile->setBackgroundColor(tok::tileBg());
    tile->setBorderColor(tok::tileBrd());
    tile->setBorderThickness(1.0f);
    tile->setJustifyContent(brls::JustifyContent::CENTER);
    tile->setAlignItems(brls::AlignItems::CENTER);
    tile->addView(makeLabel("\xE2\x86\x93", 22.0f, tok::accent()));
    tile->setMarginRight(14.0f);
    header->addView(tile);

    auto* titles = new brls::Box();
    titles->setAxis(brls::Axis::COLUMN);
    titles->setShrink(1.0f);
    titles->addView(makeLabel("Update available", 16.0f, tok::text()));
    auto* sub = makeLabel("A new version of VitaSuwayomi is ready to install.",
                          11.5f, tok::muted());
    sub->setMarginTop(3.0f);
    titles->addView(sub);
    header->addView(titles);
    panel->addView(header);

    // ── Version cards: current → new ────────────────────────────────────
    const float cardW = (panelW - 36.0f - 34.0f) / 2.0f;
    auto makeCard = [cardW](const char* tag, NVGcolor tagColor, const std::string& value,
                            NVGcolor valueColor, NVGcolor bg, NVGcolor border) {
        auto* card = new brls::Box();
        card->setAxis(brls::Axis::COLUMN);
        card->setWidth(cardW);
        card->setCornerRadius(10.0f);
        card->setBackgroundColor(bg);
        card->setBorderColor(border);
        card->setBorderThickness(1.0f);
        card->setPadding(9.0f, 12.0f, 9.0f, 12.0f);
        card->addView(makeLabel(tag, 9.5f, tagColor));
        auto* v = makeLabel(value, 13.0f, valueColor);
        v->setMarginTop(2.0f);
        card->addView(v);
        return card;
    };

    auto* cards = new brls::Box();
    cards->setAxis(brls::Axis::ROW);
    cards->setAlignItems(brls::AlignItems::CENTER);
    cards->setPadding(0.0f, 18.0f, 0.0f, 18.0f);
    cards->addView(makeCard("INSTALLED", tok::muted2(), kCurrent, tok::muted(),
                            nvgRGBA(255, 255, 255, 12), tok::hairline()));
    auto* arrow = makeLabel("\xE2\x86\x92", 15.0f, tok::muted2());
    arrow->setMarginLeft(9.0f);
    arrow->setMarginRight(9.0f);
    cards->addView(arrow);
    cards->addView(makeCard("NEW", tok::accent(), rel.tag, tok::accentBright(),
                            tok::cardBg(), tok::cardBrd()));
    panel->addView(cards);

    // Release notes are NOT inlined here — a raw markdown (or, for some
    // releases, raw HTML) dump in a 120px window was unreadable. They get the
    // What's New button in the footer and the parsed sheet above.

    // ── Size / platform caption ─────────────────────────────────────────
    std::string caption;
    if (rel.assetSize > 0) caption = mbLabel(rel.assetSize) + " MB download";
#if defined(__vita__) || defined(__PSV__)
    caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("installs in place \xC2\xB7 reopens automatically");
#elif defined(__SWITCH__)
    caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("installs in place \xC2\xB7 relaunch to apply");
#elif defined(ANDROID) || defined(__ANDROID__)
    caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("system installer opens when ready");
#elif defined(__PS4__)
    caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("installs via the PS4 installer \xC2\xB7 exit to finish");
#elif defined(_WIN32) || VS_MACOS_DESKTOP
    caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("installs in place \xC2\xB7 reopens automatically");
#else
    if (rel.assetUrl.empty())
        caption += (caption.empty() ? "" : " \xC2\xB7 ") + std::string("opens the release page in your browser");
#endif
    if (!caption.empty()) {
        auto* cap = makeLabel(caption, 10.5f, tok::disabled(), false);
        cap->setMarginTop(10.0f);
        cap->setMarginLeft(18.0f);
        cap->setMarginRight(18.0f);
        panel->addView(cap);
    }

    // ── Actions ─────────────────────────────────────────────────────────
    // Closing with B means "not right now" and never marks the release skipped;
    // only Skip does that.
    auto dismiss = []() { s_busy.store(false); brls::Application::popActivity(); };

    auto* footer = new brls::Box();
    footer->setAxis(brls::Axis::ROW);
    footer->setAlignItems(brls::AlignItems::CENTER);
    footer->setJustifyContent(brls::JustifyContent::FLEX_END);
    footer->setPadding(14.0f, 18.0f, 16.0f, 18.0f);

    brls::Box* primary = nullptr;
    if (!rel.assetUrl.empty()) {
        ReleaseInfo r = rel;
        primary = makeButton("Update now", BtnStyle::Accent, [r]() {
            brls::Application::popActivity(brls::TransitionAnimation::NONE,
                                           [r]() { startInstall(r); });
        });
    } else {
        std::string url = rel.pageUrl;
        primary = makeButton("Download", BtnStyle::Accent, [url]() {
            openUrl(url);
            s_busy.store(false);
            brls::Application::popActivity();
        });
    }
    // The primary takes whatever the fixed-width buttons leave, so the row can
    // never overflow the panel no matter which of them are present.
    primary->setGrow(1.0f);
    primary->setShrink(1.0f);
    footer->addView(primary);

    // What's New opens the parsed notes sheet on top of this one; B there
    // comes straight back here.
    if (!rel.notes.empty()) {
        ReleaseInfo r = rel;
        auto* whatsNew = makeButton("What's New", BtnStyle::Gray,
                                    [r]() { showNotesSheet(r); });
        whatsNew->setWidth(128.0f);
        whatsNew->setShrink(1.0f);
        whatsNew->setMarginLeft(8.0f);
        footer->addView(whatsNew);
    }

    // "Skip this version" only makes sense on the silent startup check.
    if (!manual) {
        ReleaseInfo r = rel;
        auto* skip = makeButton("Skip", BtnStyle::Gray, [r]() {
            setSkippedVersion(r.tag);
            s_busy.store(false);
            brls::Application::popActivity();
        });
        skip->setWidth(84.0f);
        skip->setShrink(1.0f);
        skip->setMarginLeft(8.0f);
        footer->addView(skip);
    }

    auto* later = makeButton("Later", BtnStyle::Ghost, dismiss);
    later->setWidth(84.0f);
    later->setShrink(1.0f);
    later->setMarginLeft(8.0f);
    footer->addView(later);
    panel->addView(footer);

    scrim->addView(panel);
    scrim->registerAction("Back", brls::ControllerButton::BUTTON_B,
        [dismiss](brls::View*) { dismiss(); return true; });
    scrim->addGestureRecognizer(new brls::TapGestureRecognizer(scrim, dismiss));

    brls::Application::pushActivity(new OverlayActivity(scrim));
    brls::Application::giveFocus(primary);
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
                            r.digest    = jsonString(a, "digest");
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

#if defined(__vita__) || defined(__PSV__)
    // Garbage-collect the updater stub left installed by a previous update —
    // always from the main app, never the stub itself (a title can't uninstall
    // its own running self). No-op on a normal boot when the stub isn't present.
    vita::removeUpdaterStub();
#elif defined(__PS4__)
    // Same idea on PS4: remove the helper title left over from a completed
    // update. A title that uninstalls its own running self is killed mid-call
    // and reported as a crash (CE-36329-3), so this must run from the main app.
    ps4::removeUpdaterApp();
#endif

    if (manual) brls::Application::notify("Checking for updates…");

    asyncRun([manual]() {
        HttpClient client;
        client.setUserAgent("VitaSuwayomi/" VITA_SUWAYOMI_VERSION);
        client.setInternetClient(true);
        client.setTimeout(20);
        client.setFollowRedirects(true);
        // The release feed decides what we download and install, so it must
        // come over a verified connection (see setVerifyTls).
        client.setVerifyTls(true);

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
