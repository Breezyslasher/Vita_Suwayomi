/**
 * VitaSuwayomi - Audio Utilities
 * FFmpeg-based audio processing functions
 */

#pragma once

#include <string>
#include <vector>
#include <functional>

namespace vitasuwayomi {

/**
 * Concatenate multiple audio files into a single file using FFmpeg
 * Uses stream copy for fast concatenation without re-encoding
 *
 * @param inputFiles Vector of input file paths
 * @param outputPath Output file path
 * @param progressCallback Optional callback for progress updates (current, total)
 * @return true on success, false on failure
 */
bool concatenateAudioFiles(const std::vector<std::string>& inputFiles,
                           const std::string& outputPath,
                           std::function<void(int, int)> progressCallback = nullptr);

} // namespace vitasuwayomi
