#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

// IO owns format parsing; PointCloud owns the format-neutral .gscloud storage algorithm.
namespace pointcloudcache_io {

/// Build a .gscloud sidecar by streaming one source file in bounded chunks. Optional counters
/// report points streamed and finalized. Cancellation is checked between bounded chunks/blocks.
/// Returns false and sets errorMessage on parsing, storage or cancellation failure; partial
/// output and temporary files are removed. Cache bytes and format-specific messages are unchanged.
[[nodiscard]] bool BuildFromE57(
    const std::string& sourcePath, const std::string& cachePath,
    std::vector<std::string>& progressLog, std::string* errorMessage,
    std::atomic<std::int64_t>* pointsStreamedOut = nullptr,
    std::atomic<std::int64_t>* pointsFinalizedOut = nullptr,
    const std::atomic<bool>* cancelRequested = nullptr);

[[nodiscard]] bool BuildFromPts(
    const std::string& sourcePath, const std::string& cachePath,
    std::vector<std::string>& progressLog, std::string* errorMessage,
    std::atomic<std::int64_t>* pointsStreamedOut = nullptr,
    std::atomic<std::int64_t>* pointsFinalizedOut = nullptr,
    const std::atomic<bool>* cancelRequested = nullptr);

[[nodiscard]] bool BuildFromPtx(
    const std::string& sourcePath, const std::string& cachePath,
    std::vector<std::string>& progressLog, std::string* errorMessage,
    std::atomic<std::int64_t>* pointsStreamedOut = nullptr,
    std::atomic<std::int64_t>* pointsFinalizedOut = nullptr,
    const std::atomic<bool>* cancelRequested = nullptr);

[[nodiscard]] bool BuildFromLas(
    const std::string& sourcePath, const std::string& cachePath,
    std::vector<std::string>& progressLog, std::string* errorMessage,
    std::atomic<std::int64_t>* pointsStreamedOut = nullptr,
    std::atomic<std::int64_t>* pointsFinalizedOut = nullptr,
    const std::atomic<bool>* cancelRequested = nullptr);

[[nodiscard]] bool BuildFromLaz(
    const std::string& sourcePath, const std::string& cachePath,
    std::vector<std::string>& progressLog, std::string* errorMessage,
    std::atomic<std::int64_t>* pointsStreamedOut = nullptr,
    std::atomic<std::int64_t>* pointsFinalizedOut = nullptr,
    const std::atomic<bool>* cancelRequested = nullptr);

}  // namespace pointcloudcache_io
