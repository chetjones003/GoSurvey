#pragma once

#include "alignment/Alignment.hpp"

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace alignment {

/// REQ-495.3: display samples of each alignment, kept per alignment and per zoom bucket. A bucket is half an octave of
/// chord tolerance; its samples are made at the bucket's finer edge, so they meet any tolerance in the bucket. The
/// samples are rebuilt only when the alignment's geometry or the bucket changes - never for a pan, a restyle or a
/// relabel. Display only: nothing here feeds a query or an export (REQ-495.1).
class AlignmentDisplayCache {
 public:
  /// The samples of every entity of `a` for chord tolerance `maxChordHeight`, each starting at its entity's start
  /// point. The reference stays valid until the next call for any alignment.
  [[nodiscard]] const std::vector<std::vector<Point2>>& Samples(const Alignment& a, double maxChordHeight);
  /// Drops the entries of alignments whose ids are not in `alive`, so deleted alignments do not keep their samples.
  void Retain(const std::vector<std::uint64_t>& alive);
  /// How many times an alignment's samples were rebuilt; tests use it to see the cache hold.
  [[nodiscard]] std::uint64_t Rebuilds() const { return rebuilds_; }

  /// The zoom bucket of a tolerance and the tolerance its samples are made at (never coarser than the tolerance).
  [[nodiscard]] static int Bucket(double maxChordHeight);
  [[nodiscard]] static double BucketTolerance(int bucket);
  /// A fingerprint of the alignment's geometry: equal for equal entities, different after any geometry edit.
  [[nodiscard]] static std::uint64_t GeometryHash(const Alignment& a);

 private:
  struct Entry {
    std::uint64_t hash = 0;
    int bucket = 0;
    std::vector<std::vector<Point2>> entities;
  };
  /// The most recent buckets each alignment was drawn at (model space and each viewport have their own zoom).
  static constexpr std::size_t kBucketsPerAlignment = 4;
  std::unordered_map<std::uint64_t, std::vector<Entry>> entries_;
  std::uint64_t rebuilds_ = 0;
};

}  // namespace alignment
