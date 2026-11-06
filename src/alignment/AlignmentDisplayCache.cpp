#include "alignment/AlignmentDisplayCache.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace alignment {
namespace {

/// FNV-1a over the bytes of one plain value.
template <typename T>
void Mix(std::uint64_t* h, const T& value) {
  assert(h != nullptr);
  static_assert(sizeof(T) <= 16, "hash plain fields only");
  std::array<unsigned char, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(T));
  for (const unsigned char b : bytes) {
    *h ^= b;
    *h *= 1099511628211ull;
  }
}

}  // namespace

int AlignmentDisplayCache::Bucket(double maxChordHeight) {
  assert(!std::isnan(maxChordHeight));
  assert(maxChordHeight > 0.0);
  const double h = std::isfinite(maxChordHeight) ? std::clamp(maxChordHeight, 1e-9, 1e9) : 1.0;
  return static_cast<int>(std::floor(2.0 * std::log2(h)));
}

double AlignmentDisplayCache::BucketTolerance(int bucket) {
  assert(bucket > -200);
  assert(bucket < 200);
  return std::exp2(0.5 * static_cast<double>(bucket));
}

std::uint64_t AlignmentDisplayCache::GeometryHash(const Alignment& a) {
  assert(a.Entities().size() < 100000000);
  std::uint64_t h = 1469598103934665603ull;
  for (const Entity& e : a.Entities()) {
    assert(std::isfinite(e.length));
    Mix(&h, static_cast<int>(e.kind));
    Mix(&h, e.start.x);
    Mix(&h, e.start.y);
    Mix(&h, e.startAzimuth);
    Mix(&h, e.length);
    Mix(&h, e.radius);
    Mix(&h, e.startRadius);
    Mix(&h, e.right);
  }
  Mix(&h, a.Entities().size());
  return h;
}

const std::vector<std::vector<Point2>>& AlignmentDisplayCache::Samples(const Alignment& a, double maxChordHeight) {
  assert(maxChordHeight > 0.0);
  assert(a.Entities().size() < 100000000);
  const int bucket = Bucket(maxChordHeight);
  const std::uint64_t hash = GeometryHash(a);
  std::vector<Entry>& buckets = entries_[a.id];
  if (!buckets.empty() && buckets.front().hash != hash) buckets.clear();  // the geometry changed: every zoom is stale
  const auto hit = std::find_if(buckets.begin(), buckets.end(), [&](const Entry& e) { return e.bucket == bucket; });
  if (hit != buckets.end() && hit->entities.size() == a.Entities().size()) return hit->entities;
  if (hit != buckets.end()) buckets.erase(hit);
  if (buckets.size() >= kBucketsPerAlignment) buckets.erase(buckets.begin());  // the least recently made
  buckets.push_back(Entry{});
  Entry& entry = buckets.back();
  entry.hash = hash;
  entry.bucket = bucket;
  entry.entities.reserve(a.Entities().size());
  const double tol = BucketTolerance(bucket);
  for (int i = 0; i < static_cast<int>(a.Entities().size()); ++i) entry.entities.push_back(a.SampleEntityForDisplay(i, tol));
  ++rebuilds_;
  return entry.entities;
}

void AlignmentDisplayCache::Retain(const std::vector<std::uint64_t>& alive) {
  assert(alive.size() < 100000000);
  assert(entries_.size() < 100000000);
  const std::unordered_set<std::uint64_t> keep(alive.begin(), alive.end());
  for (auto it = entries_.begin(); it != entries_.end();) {
    if (keep.count(it->first) == 0) it = entries_.erase(it);
    else ++it;
  }
}

}  // namespace alignment
