// CS-MAP wrapper (REQ-358, ADR-063). The only file that includes a CS-MAP header.

#include "CoordinateSystems.hpp"

#include "cs_map.h"

#include <cstring>
#include <memory>

namespace geo {

namespace {

bool g_loaded = false;
std::string g_error = "The coordinate-system dictionary has not been loaded.";
std::vector<std::string> g_categories;

/// CS-MAP's text for its last error, e.g. "Coordinate system named XYZ not found".
std::string LastCsMapError() {
  char buf[256] = {};
  CS_errmsg(buf, static_cast<int>(sizeof(buf)));
  return buf;
}

std::string Field(const char* s, size_t size) { return std::string(s, strnlen(s, size)); }

/// CS_csdef / CS_csloc results are malloc'd by CS-MAP and released with CS_free; a datum path
/// (CS_dtcsu) owns grid-file handles and is released with CS_dtcls.
struct CsFree {
  void operator()(void* p) const { CS_free(p); }
};
struct DtcClose {
  void operator()(cs_Dtcprm_* p) const { CS_dtcls(p); }
};

GeoResult Failure(std::string why) {
  GeoResult r;
  r.error = std::move(why);
  return r;
}

GeoResult NotLoaded() { return Failure(g_error); }

}  // namespace

bool LoadDictionaries(const std::string& directory) {
  g_loaded = false;
  g_categories.clear();
  CS_recvr();  // drop anything cached from a previous directory
  if (directory.empty() || directory.size() >= 250) {
    g_error = "The coordinate-system dictionary folder was not found.";
    return false;
  }
  if (CS_altdr(directory.c_str()) != 0) {
    g_error = "The coordinate-system dictionary was not found in " + directory + ".";
    return false;
  }
  // The category file is what the Zone group is built from; a directory without it is not usable.
  for (unsigned i = 0;; ++i) {
    const char* name = CS_getCatName(i);
    if (name == nullptr)
      break;
    g_categories.emplace_back(name);
  }
  if (g_categories.empty()) {
    g_error = "The coordinate-system dictionary in " + directory + " has no categories (" +
              LastCsMapError() + ").";
    return false;
  }
  g_loaded = true;
  g_error.clear();
  return true;
}

bool DictionariesLoaded() { return g_loaded; }

const std::string& DictionaryError() { return g_error; }

const std::vector<std::string>& Categories() { return g_categories; }

std::vector<std::string> CoordinateSystemsIn(const std::string& category) {
  std::vector<std::string> out;
  if (!g_loaded)
    return out;
  const int n = CS_getItmNameCount(category.c_str());
  for (int i = 0; i < n; ++i)
    if (const char* code = CS_getItmName(category.c_str(), static_cast<unsigned>(i)))
      out.emplace_back(code);
  return out;
}

std::string CategoryOf(const std::string& code) {
  for (const std::string& category : g_categories)
    for (const std::string& c : CoordinateSystemsIn(category))
      if (c == code)
        return category;
  return {};
}

std::optional<CoordinateSystemInfo> FindCoordinateSystem(const std::string& code) {
  if (!g_loaded || code.empty())
    return std::nullopt;
  std::unique_ptr<cs_Csdef_, CsFree> def(CS_csdef(code.c_str()));
  if (!def)
    return std::nullopt;
  CoordinateSystemInfo info;
  info.code = Field(def->key_nm, sizeof(def->key_nm));
  info.description = Field(def->desc_nm, sizeof(def->desc_nm));
  info.projection = Field(def->prj_knm, sizeof(def->prj_knm));
  info.datum = Field(def->dat_knm, sizeof(def->dat_knm));
  if (info.datum.empty())
    info.datum = Field(def->elp_knm, sizeof(def->elp_knm));
  info.unit = Field(def->unit, sizeof(def->unit));
  info.geographic = info.projection == "LL";
  const double m = info.geographic ? 0.0 : CS_unitlu(cs_UTYP_LEN, def->unit);
  info.metersPerUnit = m > 0.0 ? m : 0.0;
  return info;
}

std::optional<double> EllipsoidSemiMajorMeters(const std::string& code) {
  if (!g_loaded || code.empty())
    return std::nullopt;
  std::unique_ptr<cs_Csprm_, CsFree> cs(CS_csloc(code.c_str()));
  if (!cs || !(cs->datum.e_rad > 0.0))
    return std::nullopt;
  return cs->datum.e_rad;
}

GeoResult GridScaleFactor(const std::string& code, double easting, double northing) {
  if (!g_loaded)
    return NotLoaded();
  std::unique_ptr<cs_Csprm_, CsFree> cs(CS_csloc(code.c_str()));
  if (!cs)
    return Failure(LastCsMapError());
  if (Field(cs->csdef.prj_knm, sizeof(cs->csdef.prj_knm)) == "LL")
    return Failure(code + " is a latitude/longitude system; it has no grid scale factor.");
  const double xy[3] = {easting, northing, 0.0};
  double ll[3] = {};
  if (CS_cs2ll(cs.get(), ll, xy) & cs_CNVRT_DOMN)
    return Failure("The point is outside the mathematical domain of " + code + ".");
  const double k = CS_cssck(cs.get(), ll);
  if (!(k > 0.0))  // CS-MAP returns -1 when it cannot compute one
    return Failure("CS-MAP could not compute the scale factor of " + code + " at this point.");
  GeoResult r;
  r.ok = true;
  r.x = k;
  return r;
}

GeoResult GridToLatLong(const std::string& code, double easting, double northing) {
  if (!g_loaded)
    return NotLoaded();
  std::unique_ptr<cs_Csprm_, CsFree> cs(CS_csloc(code.c_str()));
  if (!cs)
    return Failure(LastCsMapError());
  const double xy[3] = {easting, northing, 0.0};
  double ll[3] = {};
  if (CS_cs2ll(cs.get(), ll, xy) & cs_CNVRT_DOMN)
    return Failure("The point is outside the mathematical domain of " + code + ".");
  GeoResult r;
  r.ok = true;
  r.x = ll[0];
  r.y = ll[1];
  return r;
}

GeoResult LatLongToGrid(const std::string& code, double longitude, double latitude) {
  if (!g_loaded)
    return NotLoaded();
  std::unique_ptr<cs_Csprm_, CsFree> cs(CS_csloc(code.c_str()));
  if (!cs)
    return Failure(LastCsMapError());
  const double ll[3] = {longitude, latitude, 0.0};
  double xy[3] = {};
  if (CS_ll2cs(cs.get(), xy, ll) & cs_CNVRT_DOMN)
    return Failure("The point is outside the mathematical domain of " + code + ".");
  GeoResult r;
  r.ok = true;
  r.x = xy[0];
  r.y = xy[1];
  return r;
}

GeoResult ConvertLatLong(const std::string& fromCode, const std::string& toCode, double longitude,
                         double latitude) {
  if (!g_loaded)
    return NotLoaded();
  std::unique_ptr<cs_Csprm_, CsFree> from(CS_csloc(fromCode.c_str()));
  std::unique_ptr<cs_Csprm_, CsFree> to(CS_csloc(toCode.c_str()));
  if (!from || !to)
    return Failure(LastCsMapError());
  // Fatal on a missing datum or a point outside the grid files: a silent fallback to a coarser
  // method would pass a survey-grade number that is not one.
  std::unique_ptr<cs_Dtcprm_, DtcClose> path(CS_dtcsu(from.get(), to.get(), cs_DTCFLG_DAT_F, cs_DTCFLG_BLK_F));
  if (!path)
    return Failure(LastCsMapError());
  const double in[2] = {longitude, latitude};
  double out[2] = {};
  if (CS_dtcvt(path.get(), in, out) != 0)
    return Failure(LastCsMapError());
  GeoResult r;
  r.ok = true;
  r.x = out[0];
  r.y = out[1];
  return r;
}

}  // namespace geo
