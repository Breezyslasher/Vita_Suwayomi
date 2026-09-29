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
#include "app/suwayomi_client.hpp"
#include "platform/platform.hpp"
#include "utils/async.hpp"

#include <borealis.hpp>
#include <cctype>
#include <cstdio>
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

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

HttpResponse postJson(const std::string& path, const std::string& body, int timeout) {
    HttpClient client;
    client.setTimeout(timeout);
    client.setFollowRedirects(true);
    HttpRequest req;
    req.url = baseUrl() + path;
    req.method = "POST";
    req.body = body;
    req.headers["Accept"] = "application/json";
    req.headers["Content-Type"] = "application/json";
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
        // The Suwayomi library, pushed by syncLibrary(). A list that was never
        // pushed excludes nothing, so this is safe before the first sync.
        q += "&exclude_list=suwayomi";
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

// ── Identity: which catalog entry is this manga? ────────────────────────────
// MangaBrain's catalog is AniList, so an AniList tracker link IS the catalog
// id. A MAL link is matched to the search result carrying the same id_mal.
// Everything else only counts on an EXACT title match: a wrong id would hide
// an unrelated title from recommendations and skew the taste profile.

constexpr int kTrackerMal = 1;       // Suwayomi TrackerManager.MYANIMELIST
constexpr int kTrackerAniList = 2;   // Suwayomi TrackerManager.ANILIST

// Letters and digits only, lowercased; non-ASCII bytes kept as-is, so
// "Kaguya-sama: Love is War" == "Kaguya-sama - Love Is War" and Japanese /
// Korean titles still compare exactly.
std::string normTitle(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (c >= 0x80) out += (char)c;
        else if (std::isalnum(c)) out += (char)std::tolower(c);
    }
    return out;
}

struct SearchHit {
    int id = 0;
    int idMal = 0;
    std::set<std::string> titles;   // normalized romaji / English / native
};

// Non-anime catalog hits for a title (an anime seed would recommend anime).
bool searchCatalog(const std::string& title, std::vector<SearchHit>& hits, std::string& err) {
    const bool adult = Application::getInstance().getSettings().showNsfwSources;
    HttpResponse r = get("/search?q=" + HttpClient::urlEncode(title) +
                         "&limit=10" + (adult ? "&adult=true" : ""), 15);
    if (!r.success || r.statusCode != 200) { err = describeFailure(r); return false; }
    forEachObject(jsonBlock(r.body, "results"), [&](const std::string& m) {
        if (jsonString(m, "medium") == "anime") return;
        SearchHit h;
        h.id = (int)jsonNumber(m, "id");
        h.idMal = (int)jsonNumber(m, "id_mal");   // null reads as 0
        for (const char* key : {"title", "title_english", "title_native"}) {
            std::string n = normTitle(jsonString(m, key));
            if (!n.empty()) h.titles.insert(n);
        }
        if (h.id > 0) hits.push_back(std::move(h));
    });
    return true;
}

// The one catalog id whose title matches exactly, or 0 when none does — or
// when two different entries do, since then it is still a guess.
int exactTitleMatch(const std::vector<SearchHit>& hits, const std::string& title) {
    const std::string want = normTitle(title);
    if (want.empty()) return 0;
    int found = 0;
    for (const auto& h : hits) {
        if (!h.titles.count(want)) continue;
        if (found && found != h.id) return 0;
        found = h.id;
    }
    return found;
}

int malMatch(const std::vector<SearchHit>& hits, int64_t malId) {
    if (malId <= 0) return 0;
    for (const auto& h : hits) if (h.idMal == malId) return h.id;
    return 0;
}

// ── Title-match cache ───────────────────────────────────────────────────────
// Title lookups are the expensive part of a library sync (one search per
// untracked manga), so results persist across launches: a match is kept
// until the manga's title changes; "no match" is retried after a week, in
// case the catalog gained the title since.

struct TitleCacheEntry {
    std::string titleKey;   // normTitle() of the title it was resolved for
    int catalogId = 0;      // 0 = no exact match
    int64_t checkedAt = 0;  // unix seconds
};

std::mutex s_cacheMutex;
std::map<int, TitleCacheEntry> s_titleCache;
bool s_cacheLoaded = false;
constexpr int64_t kNoMatchRetrySecs = 7 * 24 * 3600;

std::string cachePath() { return platform::path("mangabrain_titles.tsv"); }

void loadCacheLocked() {
    if (s_cacheLoaded) return;
    s_cacheLoaded = true;
    std::vector<uint8_t> raw = platform::readFile(cachePath());
    std::istringstream in(std::string(raw.begin(), raw.end()));
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream row(line);
        std::string id, key, cat, when;
        if (!std::getline(row, id, '\t') || !std::getline(row, key, '\t') ||
            !std::getline(row, cat, '\t') || !std::getline(row, when, '\t')) continue;
        TitleCacheEntry e;
        e.titleKey = key;
        e.catalogId = std::atoi(cat.c_str());
        e.checkedAt = std::atoll(when.c_str());
        s_titleCache[std::atoi(id.c_str())] = e;
    }
}

void saveCacheLocked() {
    std::string out;
    for (const auto& kv : s_titleCache) {
        out += std::to_string(kv.first) + "\t" + kv.second.titleKey + "\t" +
               std::to_string(kv.second.catalogId) + "\t" +
               std::to_string(kv.second.checkedAt) + "\n";
    }
    platform::writeFile(cachePath(), out);
}

// True with the cached answer (possibly 0 = known no-match) when it is fresh.
bool cachedTitleMatch(int mangaId, const std::string& title, int& catalogId) {
    std::lock_guard<std::mutex> lk(s_cacheMutex);
    loadCacheLocked();
    auto it = s_titleCache.find(mangaId);
    if (it == s_titleCache.end() || it->second.titleKey != normTitle(title)) return false;
    if (it->second.catalogId == 0 &&
        (int64_t)std::time(nullptr) - it->second.checkedAt > kNoMatchRetrySecs) return false;
    catalogId = it->second.catalogId;
    return true;
}

void rememberTitleMatch(int mangaId, const std::string& title, int catalogId) {
    std::lock_guard<std::mutex> lk(s_cacheMutex);
    loadCacheLocked();
    s_titleCache[mangaId] = {normTitle(title), catalogId, (int64_t)std::time(nullptr)};
}

void flushTitleCache() {
    std::lock_guard<std::mutex> lk(s_cacheMutex);
    if (s_cacheLoaded) saveCacheLocked();
}

// ── Library sync ────────────────────────────────────────────────────────────
// Pushes the Suwayomi library to MangaBrain as the generic exclusion list
// "suwayomi" (POST /exclusions/suwayomi), which MangaBrain uses to exclude
// those titles, to seed For-You, and for the taste boost. Ids only: today's
// endpoint stores every posted id as unrated and not-planned.

std::atomic<bool> s_syncing{false};
std::mutex s_syncMutex;
std::string s_lastPushedBody;   // skip the POST when nothing changed
std::string s_syncSummary = "Not synced yet";

// Bounds a first sync of a large, mostly untracked library; the rest are
// looked up on the next syncs.
constexpr int kMaxTitleLookupsPerSync = 150;

bool syncLibraryNow(std::string& message) {
    SuwayomiClient& client = SuwayomiClient::getInstance();

    std::vector<Manga> library;
    if (!client.fetchLibraryManga(library)) {
        message = "couldn't load the Suwayomi library";
        return false;
    }
    std::vector<TrackRecord> records;
    if (!client.fetchAllTrackRecords(records)) {
        message = "couldn't load tracker links from Suwayomi";
        return false;
    }
    std::map<int, std::vector<const TrackRecord*>> byManga;
    for (const auto& r : records) byManga[r.mangaId].push_back(&r);

    std::set<int64_t> anilistIds, malIds;
    int viaAniList = 0, viaMal = 0, viaTitle = 0, unmatched = 0, pending = 0, lookups = 0;

    for (const Manga& m : library) {
        int64_t anilist = 0, mal = 0;
        for (const TrackRecord* r : byManga[m.id]) {
            if (r->remoteId <= 0) continue;
            if (r->trackerId == kTrackerAniList) anilist = r->remoteId;
            else if (r->trackerId == kTrackerMal) mal = r->remoteId;
        }
        // Tracker links are exact: AniList ids are catalog ids, and the list
        // accepts MAL manga ids as-is (MangaBrain joins them on id_mal).
        if (anilist > 0) { anilistIds.insert(anilist); ++viaAniList; continue; }
        if (mal > 0)     { malIds.insert(mal);         ++viaMal;     continue; }

        int id = 0;
        if (!cachedTitleMatch(m.id, m.title, id)) {
            if (lookups >= kMaxTitleLookupsPerSync) { ++pending; continue; }
            ++lookups;
            std::vector<SearchHit> hits;
            std::string err;
            if (!searchCatalog(m.title, hits, err)) { ++pending; continue; }  // retry next sync
            id = exactTitleMatch(hits, m.title);
            rememberTitleMatch(m.id, m.title, id);
        }
        if (id > 0) { anilistIds.insert(id); ++viaTitle; }
        else        ++unmatched;
    }
    flushTitleCache();

    std::string body = "{\"anilist_ids\":[";
    bool first = true;
    for (int64_t id : anilistIds) { body += (first ? "" : ",") + std::to_string(id); first = false; }
    body += "],\"mal_manga_ids\":[";
    first = true;
    for (int64_t id : malIds) { body += (first ? "" : ",") + std::to_string(id); first = false; }
    body += "]}";

    char summary[256];
    snprintf(summary, sizeof(summary),
             "%zu titles: %d via AniList, %d via MAL, %d by exact title, %d unmatched%s",
             library.size(), viaAniList, viaMal, viaTitle, unmatched,
             pending ? (", " + std::to_string(pending) + " still to check").c_str() : "");

    {
        std::lock_guard<std::mutex> lk(s_syncMutex);
        if (body == s_lastPushedBody) {
            s_syncSummary = summary;
            message = std::string("Library unchanged - ") + summary;
            return true;
        }
    }

    HttpResponse resp = postJson("/exclusions/suwayomi", body, 30);
    if (!resp.success || resp.statusCode != 200) {
        message = "MangaBrain rejected the library list (" + describeFailure(resp) + ")";
        return false;
    }
    std::lock_guard<std::mutex> lk(s_syncMutex);
    s_lastPushedBody = body;
    s_syncSummary = summary;
    message = std::string("Library synced - ") + summary;
    return true;
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

bool fetchRecommendations(int mangaId, const std::string& title,
                          std::vector<Recommendation>& out,
                          std::string& err) {
    out.clear();
    if (baseUrl().empty()) { err = "no server URL set"; return false; }

    const AppSettings& settings = Application::getInstance().getSettings();
    const bool adult = settings.showNsfwSources;
    int limit = settings.mangaBrainMaxResults;
    if (limit < 1) limit = 12;
    if (limit > 50) limit = 50;

    // 1. Which catalog entry is this manga? Exact answers first, in order:
    //    AniList tracker link, MAL tracker link (matched on id_mal), an
    //    exact title match (cached from library syncs); then, only as a
    //    fallback for the seed, the search's first hits.
    std::vector<int> candidates;
    auto add = [&candidates](int id) {
        if (id > 0 && std::find(candidates.begin(), candidates.end(), id) == candidates.end())
            candidates.push_back(id);
    };

    int64_t malId = 0;
    std::vector<TrackRecord> records;
    if (mangaId > 0 && SuwayomiClient::getInstance().fetchMangaTracking(mangaId, records)) {
        for (const auto& r : records) {
            if (r.remoteId <= 0) continue;
            if (r.trackerId == kTrackerAniList) add((int)r.remoteId);
            else if (r.trackerId == kTrackerMal) malId = r.remoteId;
        }
    }

    std::vector<SearchHit> hits;
    std::string searchErr;
    const bool searched = searchCatalog(title, hits, searchErr);
    add(malMatch(hits, malId));

    int cached = 0;
    if (mangaId > 0 && cachedTitleMatch(mangaId, title, cached)) {
        add(cached);
    } else if (searched) {
        const int exact = exactTitleMatch(hits, title);
        if (mangaId > 0) rememberTitleMatch(mangaId, title, exact);
        add(exact);
    }

    for (size_t i = 0; i < hits.size() && i < 3; ++i) add(hits[i].id);   // guesses

    if (candidates.empty()) {
        err = searched ? "no catalog match for this title" : searchErr;
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

void syncLibraryAsync(std::function<void(bool ok, const std::string& message)> done) {
    auto finish = [done](bool ok, const std::string& msg) {
        if (done) brls::sync([done, ok, msg]() { done(ok, msg); });
    };
    if (!configured())                             { finish(false, "MangaBrain is off"); return; }
    if (Application::getInstance().isOfflineMode()) { finish(false, "offline"); return; }
    if (s_syncing.exchange(true))                  { finish(false, "a sync is already running"); return; }

    asyncRun([finish]() {
        std::string message;
        const bool ok = syncLibraryNow(message);
        flushTitleCache();
        brls::Logger::info("MangaBrain: {}", message);
        s_syncing.store(false);
        finish(ok, message);
    });
}

std::string librarySyncSummary() {
    std::lock_guard<std::mutex> lk(s_syncMutex);
    return s_syncSummary;
}

} // namespace mangabrain
} // namespace vitasuwayomi
