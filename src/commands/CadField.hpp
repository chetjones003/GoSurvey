#pragma once

#include "CadEntities.hpp"

#include <cstdint>
#include <string>
#include <string_view>

struct AppCommandState;

/// Context that document-level fields (filename, date, layout tab) need beyond geometry.
struct CadFieldContext {
  std::string_view drawingPath;
  std::string_view activeLayoutTabName;
};

[[nodiscard]] bool CadTextContainsFieldCodes(std::string_view text);

/// Evaluate AutoCAD-style field codes embedded in \p wire for display or R2000 export.
/// When no `%<` markers are present, returns \p wire unchanged.
[[nodiscard]] std::string CadFieldEvaluateWire(const AppCommandState& st, std::string_view wire,
                                                 const CadFieldContext& ctx);

/// Build a GoSurvey-native field wire for \p entityId and \p propName (e.g. `"Area"`, `"Length"`).
[[nodiscard]] std::string CadFieldMakeGoSurveyWire(std::uint64_t entityId, std::string_view propName,
                                                   std::string_view format);

/// Build a GoSurvey-native field wire for survey point \p pointId (the point's display number).
[[nodiscard]] std::string CadFieldMakeGoSurveyPointWire(int pointId, std::string_view propName,
                                                        std::string_view format);

/// Build an AutoCAD `AcVar` field wire (document variables).
[[nodiscard]] std::string CadFieldMakeAcVarWire(std::string_view varName, std::string_view format);

/// Parsed `%<\\GoSurvey Ent … Prop …>%` expression body (without `%<` / `>%`).
struct CadFieldGoSurveyEntBinding {
  std::uint64_t entityId = 0;
  std::string prop;
  std::string format;
  [[nodiscard]] bool valid() const { return entityId != 0 && !prop.empty(); }
};

[[nodiscard]] bool CadFieldTryParseGoSurveyEntWire(std::string_view exprBody,
                                                   CadFieldGoSurveyEntBinding* out);

[[nodiscard]] std::string CadFieldMakeAcObjPropEntWire(std::uint64_t entHandle, std::string_view acdbClass,
                                                       std::string_view propName, std::string_view format);

[[nodiscard]] std::string CadFieldMakeFldIdxWire(std::uint32_t index);

/// For DWG export at R2004+: keep field wires; at R2000 substitute evaluated text.
[[nodiscard]] std::string CadFieldTextForDwgExport(const AppCommandState& st, std::string_view wire,
                                                     const CadFieldContext& ctx, bool r2004OrNewer);

/// Resolved annotation text for viewport/PDF/DWG when \p wire may contain fields.
[[nodiscard]] inline std::string CadAnnotationDisplayText(const AppCommandState& st, std::string_view wire,
                                                          const CadFieldContext& ctx) {
  return CadFieldEvaluateWire(st, wire, ctx);
}

[[nodiscard]] CadFieldContext CadFieldContextFromState(const AppCommandState& st);

[[nodiscard]] std::string CadAnnotationResolvedText(const AppCommandState& st, const CadAnnotation& ann);
