/**
 * VitaSuwayomi - Audio Utilities Implementation
 */

#include "utils/audio_utils.hpp"
#include <borealis.hpp>
#include <fstream>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

// FFmpeg
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/timestamp.h>
}

namespace vitasuwayomi {

bool concatenateAudioFiles(const std::vector<std::string>& inputFiles,
                           const std::string& outputPath,
                           std::function<void(int, int)> progressCallback) {
    if (inputFiles.empty()) {
        brls::Logger::error("concatenateAudioFiles: No input files provided");
        return false;
    }

    // If only one file, just rename/copy it
    if (inputFiles.size() == 1) {
        brls::Logger::info("concatenateAudioFiles: Only one file, renaming to output");
#ifdef __vita__
        // Copy file on Vita
        SceUID srcFd = sceIoOpen(inputFiles[0].c_str(), SCE_O_RDONLY, 0);
        if (srcFd < 0) return false;

        SceUID dstFd = sceIoOpen(outputPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (dstFd < 0) {
            sceIoClose(srcFd);
            return false;
        }

        char buffer[8192];
        int bytesRead;
        while ((bytesRead = sceIoRead(srcFd, buffer, sizeof(buffer))) > 0) {
            sceIoWrite(dstFd, buffer, bytesRead);
        }

        sceIoClose(srcFd);
        sceIoClose(dstFd);
#else
        std::rename(inputFiles[0].c_str(), outputPath.c_str());
#endif
        return true;
    }

    brls::Logger::info("concatenateAudioFiles: Combining {} files into {}", inputFiles.size(), outputPath);

    // Determine output format based on extension
    std::string outputExt = ".m4b";
    size_t dotPos = outputPath.rfind('.');
    if (dotPos != std::string::npos) {
        outputExt = outputPath.substr(dotPos);
    }

    // For MP3 files, use simple binary concatenation (MP3 frames are self-contained)
    if (outputExt == ".mp3") {
        brls::Logger::info("concatenateAudioFiles: Using binary concatenation for MP3");

        // Use heap allocation to avoid stack overflow on Vita
        const size_t BUFFER_SIZE = 8192;
        char* buffer = new char[BUFFER_SIZE];
        if (!buffer) {
            brls::Logger::error("concatenateAudioFiles: Failed to allocate buffer");
            return false;
        }

#ifdef __vita__
        SceUID outFd = sceIoOpen(outputPath.c_str(), SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        if (outFd < 0) {
            brls::Logger::error("concatenateAudioFiles: Could not create output file");
            delete[] buffer;
            return false;
        }

        int filesProcessed = 0;

        for (size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++) {
            const std::string& inputFile = inputFiles[fileIdx];
            brls::Logger::info("concatenateAudioFiles: Appending file {}/{}: {}",
                              fileIdx + 1, inputFiles.size(), inputFile);

            SceUID inFd = sceIoOpen(inputFile.c_str(), SCE_O_RDONLY, 0);
            if (inFd < 0) {
                brls::Logger::warning("concatenateAudioFiles: Could not open {}", inputFile);
                continue;
            }

            int bytesRead;
            while ((bytesRead = sceIoRead(inFd, buffer, BUFFER_SIZE)) > 0) {
                sceIoWrite(outFd, buffer, bytesRead);
            }

            sceIoClose(inFd);
            filesProcessed++;

            if (progressCallback) {
                progressCallback(filesProcessed, static_cast<int>(inputFiles.size()));
            }
        }

        sceIoClose(outFd);
        delete[] buffer;
        brls::Logger::info("concatenateAudioFiles: Successfully concatenated {} MP3 files", filesProcessed);
        return filesProcessed > 0;
#else
        std::ofstream outFile(outputPath, std::ios::binary);
        if (!outFile) {
            brls::Logger::error("concatenateAudioFiles: Could not create output file");
            delete[] buffer;
            return false;
        }

        int filesProcessed = 0;

        for (size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++) {
            const std::string& inputFile = inputFiles[fileIdx];
            brls::Logger::info("concatenateAudioFiles: Appending file {}/{}: {}",
                              fileIdx + 1, inputFiles.size(), inputFile);

            std::ifstream inFile(inputFile, std::ios::binary);
            if (!inFile) {
                brls::Logger::warning("concatenateAudioFiles: Could not open {}", inputFile);
                continue;
            }

            while (inFile.read(buffer, BUFFER_SIZE) || inFile.gcount() > 0) {
                outFile.write(buffer, inFile.gcount());
            }

            filesProcessed++;

            if (progressCallback) {
                progressCallback(filesProcessed, static_cast<int>(inputFiles.size()));
            }
        }

        delete[] buffer;
        brls::Logger::info("concatenateAudioFiles: Successfully concatenated {} MP3 files", filesProcessed);
        return filesProcessed > 0;
#endif
    }

    // Create output format context for non-MP3 formats
    AVFormatContext* outputFmtCtx = nullptr;

    // Try multiple formats in order of preference
    // Note: "ipod" format may not be available on all platforms (like Vita)
    const char* formatOptions[] = { nullptr, nullptr, nullptr };
    int numFormats = 0;

    if (outputExt == ".ogg") {
        formatOptions[0] = "ogg";
        numFormats = 1;
    } else {
        // For m4b/m4a, try mp4 first (more compatible), then ipod, then mov
        formatOptions[0] = "mp4";
        formatOptions[1] = "ipod";
        formatOptions[2] = "mov";
        numFormats = 3;
    }

    int ret = -1;
    const char* usedFormat = nullptr;
    for (int i = 0; i < numFormats; i++) {
        ret = avformat_alloc_output_context2(&outputFmtCtx, nullptr, formatOptions[i], outputPath.c_str());
        if (ret >= 0 && outputFmtCtx) {
            usedFormat = formatOptions[i];
            brls::Logger::info("concatenateAudioFiles: Using output format '{}'", usedFormat);
            break;
        }
        if (outputFmtCtx) {
            avformat_free_context(outputFmtCtx);
            outputFmtCtx = nullptr;
        }
    }

    if (ret < 0 || !outputFmtCtx) {
        brls::Logger::error("concatenateAudioFiles: Could not create output context");
        return false;
    }

    // Open first input file to get stream info
    AVFormatContext* firstInputCtx = nullptr;
    ret = avformat_open_input(&firstInputCtx, inputFiles[0].c_str(), nullptr, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        brls::Logger::error("concatenateAudioFiles: Could not open first input: {}", errbuf);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    ret = avformat_find_stream_info(firstInputCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not find stream info");
        avformat_close_input(&firstInputCtx);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Find audio stream and create output stream
    int audioStreamIndex = -1;
    AVStream* outStream = nullptr;

    for (unsigned int i = 0; i < firstInputCtx->nb_streams; i++) {
        if (firstInputCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIndex = i;

            outStream = avformat_new_stream(outputFmtCtx, nullptr);
            if (!outStream) {
                brls::Logger::error("concatenateAudioFiles: Could not create output stream");
                avformat_close_input(&firstInputCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            ret = avcodec_parameters_copy(outStream->codecpar, firstInputCtx->streams[i]->codecpar);
            if (ret < 0) {
                brls::Logger::error("concatenateAudioFiles: Could not copy codec parameters");
                avformat_close_input(&firstInputCtx);
                avformat_free_context(outputFmtCtx);
                return false;
            }

            outStream->codecpar->codec_tag = 0;
            outStream->time_base = firstInputCtx->streams[i]->time_base;
            break;
        }
    }

    avformat_close_input(&firstInputCtx);

    if (audioStreamIndex < 0 || !outStream) {
        brls::Logger::error("concatenateAudioFiles: No audio stream found");
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Open output file
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&outputFmtCtx->pb, outputPath.c_str(), AVIO_FLAG_WRITE);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            brls::Logger::error("concatenateAudioFiles: Could not open output file: {}", errbuf);
            avformat_free_context(outputFmtCtx);
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(outputFmtCtx, nullptr);
    if (ret < 0) {
        brls::Logger::error("concatenateAudioFiles: Could not write header");
        if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
            avio_closep(&outputFmtCtx->pb);
        avformat_free_context(outputFmtCtx);
        return false;
    }

    // Process each input file
    AVPacket* pkt = av_packet_alloc();
    int64_t currentPts = 0;
    int64_t currentDts = 0;
    int64_t packetsWritten = 0;
    int filesProcessed = 0;

    for (size_t fileIdx = 0; fileIdx < inputFiles.size(); fileIdx++) {
        const std::string& inputFile = inputFiles[fileIdx];

        brls::Logger::info("concatenateAudioFiles: Processing file {}/{}: {}",
                          fileIdx + 1, inputFiles.size(), inputFile);

        AVFormatContext* inputFmtCtx = nullptr;
        ret = avformat_open_input(&inputFmtCtx, inputFile.c_str(), nullptr, nullptr);
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            brls::Logger::warning("concatenateAudioFiles: Could not open input {}: {}", inputFile, errbuf);
            continue;
        }

        ret = avformat_find_stream_info(inputFmtCtx, nullptr);
        if (ret < 0) {
            brls::Logger::warning("concatenateAudioFiles: Could not find stream info for {}", inputFile);
            avformat_close_input(&inputFmtCtx);
            continue;
        }

        // Find audio stream in this file
        int inAudioIdx = -1;
        for (unsigned int i = 0; i < inputFmtCtx->nb_streams; i++) {
            if (inputFmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                inAudioIdx = i;
                break;
            }
        }

        if (inAudioIdx < 0) {
            brls::Logger::warning("concatenateAudioFiles: No audio stream in {}", inputFile);
            avformat_close_input(&inputFmtCtx);
            continue;
        }

        AVStream* inStream = inputFmtCtx->streams[inAudioIdx];
        int64_t fileDuration = 0;
        int64_t firstPts = AV_NOPTS_VALUE;

        // Read all packets from this file
        while (av_read_frame(inputFmtCtx, pkt) >= 0) {
            if (pkt->stream_index == inAudioIdx) {
                // Track first PTS for offset calculation
                if (firstPts == AV_NOPTS_VALUE && pkt->pts != AV_NOPTS_VALUE) {
                    firstPts = pkt->pts;
                }

                // Rescale timestamps
                int64_t pts_offset = (firstPts != AV_NOPTS_VALUE) ? firstPts : 0;

                if (pkt->pts != AV_NOPTS_VALUE) {
                    pkt->pts = av_rescale_q(pkt->pts - pts_offset, inStream->time_base, outStream->time_base) + currentPts;
                }
                if (pkt->dts != AV_NOPTS_VALUE) {
                    pkt->dts = av_rescale_q(pkt->dts - pts_offset, inStream->time_base, outStream->time_base) + currentDts;
                }

                pkt->stream_index = 0;
                pkt->duration = av_rescale_q(pkt->duration, inStream->time_base, outStream->time_base);

                // Track max timestamp for next file offset
                if (pkt->pts != AV_NOPTS_VALUE && pkt->pts + pkt->duration > fileDuration) {
                    fileDuration = pkt->pts + pkt->duration;
                }

                ret = av_interleaved_write_frame(outputFmtCtx, pkt);
                if (ret < 0) {
                    // Don't fail on individual packet errors
                }

                packetsWritten++;
            }
            av_packet_unref(pkt);
        }

        // Update offset for next file
        currentPts = fileDuration;
        currentDts = fileDuration;

        avformat_close_input(&inputFmtCtx);
        filesProcessed++;

        // Report progress
        if (progressCallback) {
            progressCallback(filesProcessed, static_cast<int>(inputFiles.size()));
        }
    }

    av_packet_free(&pkt);

    // Write trailer
    av_write_trailer(outputFmtCtx);

    // Cleanup
    if (!(outputFmtCtx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&outputFmtCtx->pb);
    avformat_free_context(outputFmtCtx);

    brls::Logger::info("concatenateAudioFiles: Successfully combined {} files ({} packets)",
                       filesProcessed, packetsWritten);
    return filesProcessed > 0;
}

} // namespace vitasuwayomi
