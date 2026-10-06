#pragma once

// Web Mercator tile maths for the online map (REQ-363, ADR-064 (b)). Pure: no CS-MAP, no drawing
// state, no network. Web Mercator (EPSG:3857) is the spherical projection every tile service uses;
// "mercator meters" below are its x / y, which are NOT ground meters away from the equator.
//
// Tile (z, x, y): level z splits the world into 2^z × 2^z tiles, x growing east from 180°W and y
// growing SOUTH from the top (≈ 85.05°N) — the XYZ scheme ArcGIS `.../tile/{z}/{y}/{x}` serves.

namespace geo {

/// Half the width of the Web Mercator world in mercator meters (π × 6378137).
inline constexpr double kWebMercatorHalfWorld = 20037508.342789244;
/// The latitude where the square Web Mercator world ends (degrees).
inline constexpr double kWebMercatorMaxLatitude = 85.0511287798066;
/// A level-0 tile's pixel size in mercator meters for 256-pixel tiles.
inline constexpr double kWebMercatorLevel0PixelMeters = 2.0 * kWebMercatorHalfWorld / 256.0;

[[nodiscard]] double MercatorXFromLongitude(double longitudeDeg);
/// Latitude is clamped to ±\ref kWebMercatorMaxLatitude first.
[[nodiscard]] double MercatorYFromLatitude(double latitudeDeg);
[[nodiscard]] double LongitudeFromMercatorX(double x);
[[nodiscard]] double LatitudeFromMercatorY(double y);

struct MercatorBox {
  double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;
};

/// The mercator box of tile (z, x, y).
[[nodiscard]] MercatorBox TileMercatorBox(int z, int x, int y);

/// The level whose 256-pixel tile pixel is nearest \p mercatorMetersPerScreenPixel (in log scale),
/// clamped to [0, \p maxLevel]. A non-finite or non-positive size gives 0.
[[nodiscard]] int ChooseTileLevel(double mercatorMetersPerScreenPixel, int maxLevel);

/// The inclusive tile ranges at level z covering a mercator box, clamped to the world.
struct TileRange {
  int z = 0;
  int minX = 0, minY = 0, maxX = -1, maxY = -1;
  [[nodiscard]] long long Count() const {
    return maxX < minX || maxY < minY ? 0 : static_cast<long long>(maxX - minX + 1) * (maxY - minY + 1);
  }
};
[[nodiscard]] TileRange TilesCovering(const MercatorBox& box, int z);

/// \ref TilesCovering at \p z, or at the finest coarser level whose range holds at most
/// \p maxTiles tiles (REQ-363 item 2). Level 0 is one tile, so this always ends.
[[nodiscard]] TileRange TilesCoveringAtMost(const MercatorBox& box, int z, int maxTiles);

}  // namespace geo
