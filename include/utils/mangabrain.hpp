/**
 * VitaSuwayomi - MangaBrain recommendations client
 *
 * Talks to a self-hosted MangaBrain instance
 * (https://github.com/Breezyslasher/MangaBrain): a content-based
 * recommendation engine over the AniList catalog. The flow per manga is
 * two GETs — /search to map the title onto a catalog id, then
 * /recommend/{id} for the ranked similar titles.
 */

#pragma once

#include <string>
#include <vector>

namespace vitasuwayomi {
namespace mangabrain {

struct Recommendation {
    int         id = 0;          // MangaBrain (= AniList) media id
    std::string title;           // best display title
    std::string cover;           // AniList CDN cover URL
    std::string medium;          // manga | manhwa | manhua | light_novel | one_shot
    float       similarity = 0;  // calibrated percentage, 0-100
};

/// True when the feature is enabled in settings and a server URL is set.
bool configured();

/// Map `title` onto the MangaBrain catalog and fetch its ranked similar
/// titles. Blocking — call from a worker thread only. Returns false with
/// `err` set when the server is unreachable or the title has no match.
bool fetchRecommendations(const std::string& title,
                          std::vector<Recommendation>& out,
                          std::string& err);

/// GET /healthz with the configured auth, for the settings Test button.
/// Blocking — call from a worker thread only.
bool testConnection(std::string& err);

} // namespace mangabrain
} // namespace vitasuwayomi
