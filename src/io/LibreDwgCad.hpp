#pragma once

#include <string>
#include <vector>

struct AppCommandState;

/// Load a drawing through GNU LibreDWG (REQ-170). \p asDxf selects dxf_read_file vs dwg_read_file.
bool ImportLibreCadFile(AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log,
                        bool asDxf);

/// Save through GNU LibreDWG. DWG is R2000 (the encoder this pin actually supports). DXF is ASCII
/// from the same in-memory drawing.
bool ExportLibreCadFile(const AppCommandState& st, const char* pathUtf8, std::vector<std::string>& log,
                        bool asDxf);

/// Internals exposed for testing (issue #140). Not part of the import/export contract.
namespace libredwgcad_detail {

/// Decode a LibreDWG table/entity string. \p utf16le true for R2007+ DWGs where LibreDWG keeps the
/// buffer as UTF-16LE (BITCODE_TU); false when it is a plain byte string. \p raw may be null.
std::string DecodeDwgString(const void* raw, bool utf16le);

/// Map a LibreDWG colour (\p index = signed ACI, negative encodes layer-off; \p method / \p rgb per
/// Dwg_Color) to the stored colour string: "ByLayer", "ByBlock", or "#RRGGBB".
std::string ColorToStorage(int index, unsigned method, unsigned rgb);

}  // namespace libredwgcad_detail
