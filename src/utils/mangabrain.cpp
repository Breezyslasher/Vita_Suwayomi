/**
 * VitaSuwayomi - MangaBrain recommendations client implementation
 *
 * Response shapes (api/models.py in the MangaBrain repo):
 *   GET /search?q=…            → {"results": [MediaOut, …]}
 *   GET /recommend/{id}?limit= → {"seed": MediaOut, "results":
 *                                   [{"media": MediaOut, "similarity": f,
 *                                     "components": {…}}, …],
 *                                 "related": […]}
 * MediaOut carries id, medium, title/title_english/title_native,
 * cover_image/cover_image_large and more. When the instance sets
 * MANGABRAIN_AUTH_TOKEN, every request must carry
 * "Authorization: Bearer <token>" or it answers 401.
 */

#include "utils/mangabrain.hpp"
#include "utils/http_client.hpp"
#include "app/application.hpp"

#include <borealis.hpp>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>

namespace vitasuwayomi {
namespace mangabrain {

namespace {

// ── Minimal string-aware JSON scanning ──────────────────────────────────────
// Same approach as the updater's feed parser: no DOM, just careful scanning
// that never mistakes brackets or quotes inside string values (synopses are
// free text) for structure.

// i at the opening quote → index just past the closing quote.
size_t skipString(const std::string& s, size_t i) {
    ++i;
    while (i < s.size()) {
        if (s[i] == '\\') { i += 2; continue; }
        if (s[i] == '"') return i + 1;
        ++i;
    }
    return s.size();
}

// i at '{' or '[' → index just past the matching close bracket.
size_t matchBracket(const std::string& s, size_t i) {
    const char open = s[i], close = (open == '{') ? '}' : ']';
    int depth = 0;
    while (i < s.size()) {
        const char c = s[i];
        if (c == '"') { i = skipString(s, i); continue; }
        if (c == open) depth++;
        else if (c == close && --depth == 0) return i + 1;
        ++i;
    }
    return s.size();
}

// Position of the value for "key" inside object `obj`, or npos.
size_t valuePos(const std::string& obj, const char* key) {
    const std::string want = std::string("\"") + key + "\"";
    size_t i = 0;
    while (i < obj.size()) {
        if (obj[i] == '"') {
            size_t end = skipString(obj, i);
            if (obj.compare(i, want.size(), want) == 0 && i + want.size() == end) {
                size_t p = obj.find_first_not_of(" \t\r\n", end);
                if (p != std::string::npos && obj[p] == ':') {
                    p = obj.find_first_not_of(" \t\r\n", p + 1);
                    return p;
                }
            }
            i = end;
            continue;
        }
        // Never scan for keys inside a nested object/array: "id" must be the
        // seed's id, not the id of the first genre-tag object inside it.
        if (obj[i] == '{' || obj[i] == '[') {
            if (i == 0) { ++i; continue; }   // the object we are inside
            i = matchBracket(obj, i);
            continue;
        }
        ++i;
    }
    return std::string::npos;
}

std::string jsonString(const std::string& obj, const char* key) {
    size_t p = valuePos(obj, key);
    if (p == std::string::npos || p >= obj.size() || obj[p] != '"') return {};
    size_t end = skipString(obj, p);
    std::string raw = obj.substr(p + 1, end - p - 2);
    // Unescape the few sequences that can appear in titles.
    std::string out;
    out.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) { out += raw[i]; continue; }
        const char n = raw[++i];
        switch (n) {
            case 'n': out += ' '; break;
            case 't': out += ' '; break;
            case 'u':
                // Titles are fetched for display in a font without full
                // coverage anyway; drop the escape rather than mis-render it.
                if (i + 4 < raw.size()) i += 4;
                break;
            default: out += n; break;
        }
    }
    return out;
}

double jsonNumber(const std::string& obj, const char* key, double def = 0.0) {
    size_t p = valuePos(obj, key);
    if (p == std::string::npos) return def;
    return std::atof(obj.c_str() + p);
}

// The value of `key` when it is an object or array, brackets included.
std::string jsonBlock(const std::string& obj, const char* key) {
    size_t p = valuePos(obj, key);
    if (p == std::string::npos || p >= obj.size()) return {};
    if (obj[p] != '{' && obj[p] != '[') return {};
    return obj.substr(p, matchBracket(obj, p) - p);
}

// Call fn(object-text) for each top-level object in array text `arr`.
template <typename Fn>
void forEachObject(const std::string& arr, Fn fn) {
    size_t i = 0;
    while (i < arr.size()) {
        const char c = arr[i];
        if (c == '"') { i = skipString(arr, i); continue; }
        if (c == '{') {
            size_t end = matchBracket(arr, i);
            fn(arr.substr(i, end - i));
            i = end;
            continue;
        }
        if (c == '[' && i != 0) { i = matchBracket(arr, i); continue; }
        ++i;
    }
}

// ── Requests ────────────────────────────────────────────────────────────────

std::string baseUrl() {
    std::string url = Application::getInstance().getSettings().mangaBrainUrl;
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

HttpResponse get(const std::string& pathAndQuery, int timeout) {
    HttpClient client;
    client.setTimeout(timeout);
    client.setFollowRedirects(true);
    // The user's own box, same trust model as the Suwayomi server itself
    // (LAN, plain HTTP or self-signed), so TLS verification stays off — and
    // the client is NOT marked internet-facing, so offline mode gags it.
    HttpRequest req;
    req.url = baseUrl() + pathAndQuery;
    req.headers["Accept"] = "application/json";
    const std::string& token = Application::getInstance().getSettings().mangaBrainToken;
    if (!token.empty()) req.headers["Authorization"] = "Bearer " + token;
    return client.request(req);
}

std::string describeFailure(const HttpResponse& resp) {
    if (!resp.error.empty() && resp.statusCode == 0) return resp.error;
    if (resp.statusCode == 401) return "access token rejected (HTTP 401)";
    if (resp.statusCode == 429) return "rate limited (HTTP 429)";
    return "HTTP " + std::to_string(resp.statusCode);
}

// ── The user's MangaBrain accounts ──────────────────────────────────────────
// The server keeps the tracker usernames (GET /settings — the web UI's
// Accounts panel); the web UI then sends "hide my lists", "keep planned" and
// the taste weight per request. We do the same: usernames from the server,
// choices from this app's MangaBrain settings.
struct Accounts {
    std::string anilist, mal, kitsu;
    bool yamtrack = false;
};

std::mutex  s_acctMutex;
std::string s_acctKey;       // url|token the cache was filled for
Accounts    s_acct;
bool        s_acctValid = false;

// Fetched once per server+token and reused: it changes only when the user
// edits the Accounts panel, and one extra round-trip per book is wasteful.
Accounts accounts() {
    const AppSettings& st = Application::getInstance().getSettings();
    const std::string key = st.mangaBrainUrl + "|" + st.mangaBrainToken;
    {
        std::lock_guard<std::mutex> lk(s_acctMutex);
        if (s_acctValid && s_acctKey == key) return s_acct;
    }
    Accounts a;
    HttpResponse resp = get("/settings", 10);
    if (resp.success && resp.statusCode == 200) {
        a.anilist  = jsonString(resp.body, "anilist_username");
        a.mal      = jsonString(resp.body, "mal_username");
        a.kitsu    = jsonString(resp.body, "kitsu_username");
        a.yamtrack = !jsonString(resp.body, "yamtrack_url").empty();
        std::lock_guard<std::mutex> lk(s_acctMutex);
        s_acct = a;
        s_acctKey = key;
        s_acctValid = true;   // only cache a real answer; retry after a failure
    }
    return a;
}

// Query-string tail carrying the user's configured choices. Mirrors the web
// UI's filterParams()/weightParams(): both AniList and MAL users are sent
// when set, Kitsu and Yamtrack go in as named exclusion lists. All of these
// are NOT EXISTS filters server-side, so a list that was never synced simply
// excludes nothing, and a taste weight with no usable list is dropped by the
// server instead of failing the request.
std::string userParams() {
    const AppSettings& st = Application::getInstance().getSettings();
    const bool wantLists = st.mangaBrainExcludeMyLists;
    const bool wantTaste = st.mangaBrainTasteWeight > 0;
    if (!wantLists && !wantTaste) return {};

    const Accounts a = accounts();
    std::string q;
    // The server builds the taste profile from the SAME parameters that do
    // the excluding (anilist_user / mal_user / exclude_list) — the web UI has
    // this coupling too. So the taste boost only takes effect while "hide my
    // lists" is on; without it the server just drops the weight.
    if (wantLists) {
        if (!a.anilist.empty()) q += "&anilist_user=" + HttpClient::urlEncode(a.anilist);
        if (!a.mal.empty())     q += "&mal_user=" + HttpClient::urlEncode(a.mal);
        if (!a.kitsu.empty())   q += "&exclude_list=kitsu";
        if (a.yamtrack)         q += "&exclude_list=yamtrack";
        if (st.mangaBrainKeepPlanned) q += "&keep_planned=true";
    }
    if (wantTaste) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", st.mangaBrainTasteWeight / 100.0);
        q += std::string("&w_taste=") + buf;
    }
    return q;
}

// Pick the display title the way the MangaBrain SPA does: romaji first.
std::string pickTitle(const std::string& media) {
    std::string t = jsonString(media, "title");
    if (t.empty()) t = jsonString(media, "title_english");
    if (t.empty()) t = jsonString(media, "title_native");
    return t;
}

} // namespace

bool configured() {
    const AppSettings& s = Application::getInstance().getSettings();
    return s.mangaBrainEnabled && !s.mangaBrainUrl.empty();
}

bool testConnection(std::string& err) {
    if (baseUrl().empty()) { err = "no server URL set"; return false; }
    {
        // Also the way to pick up edits made in the web UI's Accounts panel
        // without restarting: the next fetch re-reads /settings.
        std::lock_guard<std::mutex> lk(s_acctMutex);
        s_acctValid = false;
    }
    // /healthz itself is unauthenticated, so also touch an API route: that is
    // what verifies the token, not just that the box is up.
    HttpResponse health = get("/healthz", 10);
    if (!health.success || health.statusCode != 200) {
        err = describeFailure(health);
        return false;
    }
    HttpResponse api = get("/search?q=a&limit=1", 10);
    if (api.statusCode == 401) { err = describeFailure(api); return false; }
    return true;
}

bool fetchRecommendations(const std::string& title,
                          std::vector<Recommendation>& out,
                          std::string& err) {
    out.clear();
    if (baseUrl().empty()) { err = "no server URL set"; return false; }

    const AppSettings& settings = Application::getInstance().getSettings();
    const bool adult = settings.showNsfwSources;
    int limit = settings.mangaBrainMaxResults;
    if (limit < 1) limit = 12;
    if (limit > 50) limit = 50;

    // 1. Title → catalog id. Search every medium and take the first
    //    non-anime hit: the manga groups alone would miss light novels and
    //    one-shots, while anime seeds would recommend anime.
    HttpResponse search = get("/search?q=" + HttpClient::urlEncode(title) +
                              "&limit=10" + (adult ? "&adult=true" : ""), 15);
    if (!search.success || search.statusCode != 200) {
        err = describeFailure(search);
        return false;
    }

    // Candidate seeds in search-rank order, anime excluded (an anime seed
    // would recommend anime).
    std::vector<int> candidates;
    forEachObject(jsonBlock(search.body, "results"), [&](const std::string& media) {
        if (jsonString(media, "medium") == "anime") return;
        const int id = (int)jsonNumber(media, "id");
        if (id > 0 && candidates.size() < 3) candidates.push_back(id);
    });
    if (candidates.empty()) {
        err = "no catalog match for this title";
        return false;
    }

    // 2. Ranked similar titles for the seed, with the user's configured
    //    list exclusions and taste weight. A 409 means that catalog row has
    //    no embedding yet (or a re-embed is mid-flight): try the next match
    //    rather than showing nothing.
    const std::string extra = userParams();
    HttpResponse rec;
    for (int id : candidates) {
        rec = get("/recommend/" + std::to_string(id) +
                  "?limit=" + std::to_string(limit) +
                  (adult ? "&adult=true" : "") + extra, 20);
        if (rec.statusCode != 409) break;
    }
    if (!rec.success || rec.statusCode != 200) {
        err = describeFailure(rec);
        return false;
    }

    forEachObject(jsonBlock(rec.body, "results"), [&](const std::string& item) {
        const std::string media = jsonBlock(item, "media");
        if (media.empty()) return;
        Recommendation r;
        r.id         = (int)jsonNumber(media, "id");
        r.title        = pickTitle(media);
        r.titleEnglish = jsonString(media, "title_english");
        r.titleNative  = jsonString(media, "title_native");
        r.medium     = jsonString(media, "medium");
        r.similarity = (float)jsonNumber(item, "similarity");
        r.cover      = jsonString(media, "cover_image_large");
        if (r.cover.empty()) r.cover = jsonString(media, "cover_image");
        if (!r.title.empty()) out.push_back(std::move(r));
    });

    if (out.empty()) err = "no recommendations returned";
    return !out.empty();
}

} // namespace mangabrain
} // namespace vitasuwayomi
