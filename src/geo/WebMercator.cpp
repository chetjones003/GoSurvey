// Web Mercator tile maths (REQ-363, ADR-064 (b)).

#include "WebMercator.hpp"

#include <algorithm>
#include <cmath>

namespace geo {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadius = 6378137.0;  // the sphere Web Mercator projects

int TileIndex(double coord, double tileSize, int tilesPerSide) {
  const double t = std::floor(coord / tileSize);
  return static_cast<int>(std::clamp(t, 0.0, static_cast<double>(tilesPerSide - 1)));
}

}  // namespace

double MercatorXFromLongitude(double longitudeDeg) { return kEarthRadius * longitudeDeg * kPi / 180.0; }

double MercatorYFromLatitude(double latitudeDeg) {
  const double lat = std::clamp(latitudeDeg, -kWebMercatorMaxLatitude, kWebMercatorMaxLatitude) * kPi / 180.0;
  return kEarthRadius * std::log(std::tan(kPi / 4.0 + lat / 2.0));
}

double LongitudeFromMercatorX(double x) { return x / kEarthRadius * 180.0 / kPi; }

double LatitudeFromMercatorY(double y) {
  return (2.0 * std::atan(std::exp(y / kEarthRadius)) - kPi / 2.0) * 180.0 / kPi;
}

MercatorBox TileMercatorBox(int z, int x, int y) {
  const double size = 2.0 * kWebMercatorHalfWorld / static_cast<double>(1LL << z);
  MercatorBox b;
  b.minX = -kWebMercatorHalfWorld + x * size;
  b.maxX = b.minX + size;
  b.maxY = kWebMercatorHalfWorld - y * size;  // y grows south
  b.minY = b.maxY - size;
  return b;
}

int ChooseTileLevel(double mercatorMetersPerScreenPixel, int maxLevel) {
  if (!std::isfinite(mercatorMetersPerScreenPixel) || !(mercatorMetersPerScreenPixel > 0.0))
    return 0;
  const double z = std::log2(kWebMercatorLevel0PixelMeters / mercatorMetersPerScreenPixel);
  return static_cast<int>(std::clamp(std::lround(z), 0L, static_cast<long>(maxLevel)));
}

TileRange TilesCovering(const MercatorBox& box, int z) {
  TileRange r;
  r.z = z;
  const int n = 1 << z;
  const double size = 2.0 * kWebMercatorHalfWorld / n;
  const double lo = -kWebMercatorHalfWorld, hi = kWebMercatorHalfWorld;
  if (!(box.maxX > lo && box.minX < hi && box.maxY > lo && box.minY < hi))
    return r;  // off the world: empty
  r.minX = TileIndex(box.minX - lo, size, n);
  r.maxX = TileIndex(box.maxX - lo, size, n);
  r.minY = TileIndex(hi - box.maxY, size, n);  // the north edge is the smallest y
  r.maxY = TileIndex(hi - box.minY, size, n);
  return r;
}

TileRange TilesCoveringAtMost(const MercatorBox& box, int z, int maxTiles) {
  TileRange r = TilesCovering(box, z);
  while (r.z > 0 && r.Count() > maxTiles)
    r = TilesCovering(box, r.z - 1);
  return r;
}

}  // namespace geo
