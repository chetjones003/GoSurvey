// Drawing content (ADR-0010 / issue #1557 phase 1a): undo and tab switching copy the same set of
// fields. These tests fill one entity of every kind plus non-default styles, layers and drawing
// properties, then require that undo/redo and a tab switch away and back reproduce the serialized
// drawing exactly. They use only the public undo, tab and serializer entry points, so they pass on
// the code before the DrawingContent move as well as after it.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include <GoSurvey/Geometry/brep.hpp>

namespace {

/// Assigns (never appends) every drawing-content field from a value that depends on \p v, so two
/// variants differ in every field and a field one copy forgets shows up as a JSON difference.
void FillEveryKind(AppCommandState& st, int v) {
  const std::string tag = "DC" + std::to_string(v) + "-";
  const double o = 10.0 * v;
  auto attr = [&](const char* layer) {
    EntityAttributes a;
    a.layer = tag + layer;
    a.color = v == 1 ? "Red" : "Blue";
    return std::vector<EntityAttributes>{a};
  };

  st.userLinesFlat = {o, 0, 0, o + 1, 1, 0};
  st.userLineAttrs = attr("line");
  st.userCirclesCxCyZR = {o, 2, 0, 1.5};
  st.userCircleAttrs = attr("circle");
  st.userCircleNormals = {0.f, 0.f, 1.f};
  CadArc arc;
  arc.cx = o;
  arc.r = 2.0 + v;
  arc.sweepRad = 1.f;
  st.userArcs = {arc};
  st.userArcAttrs = attr("arc");
  CadEllipse ell;
  ell.cx = o;
  ell.ratio = 0.25f * v;
  st.userEllipses = {ell};
  st.userEllAttrs = attr("ellipse");
  st.userPolylineOffsets = {0, 3};
  st.userPolylineVerts = {o, 0, 0, o + 1, 0, 0, o + 1, 1, 0};
  st.userPolylineVertsBulge = {0.f, 0.5f, 0.f};
  st.userPolylineVertsNormal = {0, 0, 1, 0, 0, 1, 0, 0, 1};
  st.userPolylineClosed = {static_cast<uint8_t>(v == 1 ? 1 : 0)};
  st.userPolylineAttrs = attr("polyline");

  st.featureLineOffsets = {0, 2};
  st.featureLineVerts = {o, 5, 1, o + 3, 5, 2};
  st.featureLineClosed = {0};
  st.featureLineElevPt = {0, 0};
  CadFeatureLineInfo fl;
  fl.description = tag + "featureline";
  st.featureLineInfo = {fl};
  CadGradingPreset preset;
  preset.name = tag + "grading";
  st.gradingPresets = {preset};
  st.featureLineAttrs = attr("featureline");

  CadAnnotation text;
  text.insX = static_cast<float>(o);
  text.text = tag + "text";
  st.cadAnnotations = {text};
  st.cadAnnotationAttrs = attr("text");
  CadFilledRegion region;
  region.vertsXyz = {o, 0, 0, o + 1, 0, 0, o, 1, 0};
  region.patternName = tag + "hatch";
  st.cadFilledRegions = {region};
  st.cadFilledRegionAttrs = attr("hatch");

  auto mesh = std::make_shared<CadMesh>();
  mesh->vertsXyz = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  mesh->indices = {0, 1, 2};
  mesh->sourceName = tag + "mesh";
  st.cadMeshes = {mesh};
  st.cadMeshAttrs = attr("mesh");
  auto cloud = std::make_shared<CadPointCloud>();
  cloud->sourcePath = tag + "cloud.e57";
  st.cadPointClouds = {cloud};
  st.cadPointCloudAttrs = attr("cloud");
  CadSurface surface;
  surface.name = tag + "surface";
  st.cadSurfaces = {surface};
  st.cadSurfaceAttrs = attr("surface");
  st.cadSolids = {std::make_shared<const brep::Solid>()};
  st.cadSolidAttrs = attr("solid");
  CadTable table;
  table.insX = static_cast<float>(o);
  table.cells = {tag + "cell", "b"};
  st.cadTables = {table};
  st.cadTableAttrs = attr("table");
  CadTableStyle tableStyle;
  tableStyle.name = tag + "tablestyle";
  st.tableStyles = {tableStyle};
  CadPositionMarker marker;
  marker.x = o;
  marker.label.text = tag + "marker";
  st.cadPositionMarkers = {marker};
  st.cadPositionMarkerAttrs = attr("marker");
  CadMultileader leader;
  leader.pathXyz = {static_cast<float>(o), 0.f, 0.f, static_cast<float>(o) + 2.f, 2.f, 0.f};
  leader.styleName = tag + "mleaderstyle";
  st.cadMultileaders = {leader};
  st.cadMultileaderAttrs = attr("mleader");
  CadSpline spline;
  spline.degree = 1;
  spline.ctrl = {{o, 0, 0}, {o + 1, 1, 0}};
  spline.knots = {0, 0, 1, 1};
  st.cadSplines = {spline};
  st.cadSplineAttrs = attr("spline");
  CadPoint point;
  point.x = o;
  st.cadPoints = {point};
  st.cadPointAttrs = attr("point");
  st.pdMode = 2 + v;
  st.pdSize = 0.5 * v;

  CadPipeRun run;
  run.name = tag + "piperun";
  run.vertsXyz = {o, 0, 0, o + 5, 0, 0};
  run.nominalSize = "4";
  st.cadPipeRuns = {run};
  st.cadPipeRunAttrs = attr("piperun");
  alignment::Alignment al;
  al.id = 100 + v;
  al.name = tag + "alignment";
  st.alignments = {al};
  alignment::ViewFrameGroup frames;
  frames.id = 200 + v;
  st.viewFrameGroups = {frames};
  alignment::SampleLine sample;
  sample.id = 300 + v;
  sample.name = tag + "sample";
  st.sampleLines = {sample};
  alignment::VerticalProfile profile;
  profile.id = 400 + v;
  profile.name = tag + "profile";
  st.profiles = {profile};
  CadSurfaceProfile surfaceProfile;
  surfaceProfile.id = 500 + v;
  surfaceProfile.name = tag + "egprofile";
  st.surfaceProfiles = {surfaceProfile};
  CadPipingSystem system;
  system.name = tag + "network";
  system.pipeRunIndices = {0};
  st.cadPipingSystems = {system};

  CadBlockDefinition def;
  def.name = tag + "block";
  st.blockDefs = {def};
  CadBlockRef ref;
  ref.defName = def.name;
  st.cadBlockRefs = {ref};
  st.cadBlockRefAttrs = attr("blockref");

  SurveyPoint sp;
  sp.id = 10 + v;
  sp.easting = o;
  sp.description = tag + "point";
  st.surveyPoints = {sp};
  SurveyPointStyleDefinition pointStyle;
  pointStyle.name = tag + "pointstyle";
  st.surveyPointStyles = {pointStyle};
  SurveyPointLabelStyleDefinition labelStyle;
  labelStyle.name = tag + "labelstyle";
  st.surveyPointLabelStyles = {labelStyle};
  PointGroup group;
  group.name = tag + "group";
  st.pointGroups = {group};

  CadLayerRow layer0;
  layer0.name = "0";
  CadLayerRow layer;
  layer.name = tag + "layer";
  layer.color = v == 1 ? "Green" : "Cyan";
  st.drawingLayerTable = {layer0, layer};
  TextStyle textStyle;
  textStyle.name = tag + "textstyle";
  st.textStyles = {textStyle};
  SurfaceStyle surfaceStyle;
  surfaceStyle.name = tag + "surfacestyle";
  st.surfaceStyles = {surfaceStyle};
  AlignmentStyle alignmentStyle;
  alignmentStyle.name = tag + "alignmentstyle";
  st.alignmentStyles = {alignmentStyle};
  alignment::AlignmentCheckSet checkSet;
  checkSet.name = tag + "checkset";
  st.alignmentCheckSets = {checkSet};
  AlignmentLabelSet labelSet;
  labelSet.name = tag + "labelset";
  st.alignmentLabelSets = {labelSet};
  AlignmentLabelStyle alignmentLabelStyle;
  alignmentLabelStyle.name = tag + "alignmentlabelstyle";
  st.alignmentLabelStyles = {alignmentLabelStyle};
  DimensionStyle dimStyle;
  dimStyle.name = tag + "dimstyle";
  st.activeDimensionStyle = dimStyle;
  st.dimensionStyles = {dimStyle};
  MultileaderStyle mleaderStyle;
  mleaderStyle.name = tag + "mleaderstyle";
  st.activeMultileaderStyle = mleaderStyle;
  st.multileaderStyles = {mleaderStyle};

  PdfAttachment pdf;
  pdf.filePath = tag + "underlay.pdf";
  st.pdfAttachments = {pdf};
  PaperLayout sheet;
  sheet.name = tag + "sheet";
  st.paperLayouts = {sheet};
  CadRasterImage raster;
  raster.path = tag + "raster.png";
  st.cadRasterImages = {raster};

  st.worldDocumentOriginX = 1000.0 * v;
  st.worldDocumentOriginY = 2000.0 * v;
  st.drawingInsUnits = v == 1 ? 1 : 6;
  st.modelUnitsPerPlottedInch = 20.f * v;
  st.drawingSettings.zoneCode = tag + "zone";
  st.drawingSettings.markerX = o;
}

/// Names the fixture gives each kind: each must reach the serialized drawing, or a comparison of
/// that JSON could not notice the kind going missing.
const char* const kSerializedMarkers[] = {
    "featureline", "grading", "text", "hatch", "surface", "cell", "tablestyle", "marker",
    "mleaderstyle", "piperun", "alignment", "sample", "profile", "egprofile", "network", "block",
    "point", "pointstyle", "labelstyle", "group", "layer", "textstyle", "surfacestyle",
    "alignmentstyle", "checkset", "labelset", "alignmentlabelstyle", "dimstyle", "sheet",
    "raster.png", "zone",
};

void RequireMarkers(const std::string& json, int v) {
  for (const char* m : kSerializedMarkers) {
    const std::string needle = "DC" + std::to_string(v) + "-" + m;
    INFO(needle);
    CHECK(json.find(needle) != std::string::npos);
  }
}

/// What the serializer does not write (PDF underlays, issue #1554) or writes by reference only.
struct Unserialized {
  std::string pdfPath;
  const void* mesh = nullptr;
  const void* cloud = nullptr;
  const void* solid = nullptr;
};

Unserialized UnserializedOf(const AppCommandState& st) {
  Unserialized u;
  u.pdfPath = st.pdfAttachments.empty() ? "" : st.pdfAttachments[0].filePath;
  u.mesh = st.cadMeshes.empty() ? nullptr : st.cadMeshes[0].get();
  u.cloud = st.cadPointClouds.empty() ? nullptr : st.cadPointClouds[0].get();
  u.solid = st.cadSolids.empty() ? nullptr : st.cadSolids[0].get();
  return u;
}

void RequireSame(const Unserialized& a, const Unserialized& b) {
  CHECK(a.pdfPath == b.pdfPath);
  CHECK(a.mesh == b.mesh);  // shared payload, never cloned (architecture §11.5)
  CHECK(a.cloud == b.cloud);
  CHECK(a.solid == b.solid);
}

} // namespace

TEST_CASE("Undo and redo round-trip every kind of drawing content", "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;

  FillEveryKind(st, 1);
  PushUndoSnapshot(st, "drawing content test");  // assigns entity ids before the capture
  const std::string first = SerializeGoSurveyJson(st);
  const Unserialized firstExtra = UnserializedOf(st);
  RequireMarkers(first, 1);

  FillEveryKind(st, 2);
  const std::string second = SerializeGoSurveyJson(st);
  const Unserialized secondExtra = UnserializedOf(st);
  RequireMarkers(second, 2);
  REQUIRE(first != second);

  REQUIRE(DoUndo(st, log));
  CHECK(SerializeGoSurveyJson(st) == first);
  RequireSame(UnserializedOf(st), firstExtra);

  REQUIRE(DoRedo(st, log));
  CHECK(SerializeGoSurveyJson(st) == second);
  RequireSame(UnserializedOf(st), secondExtra);
}

TEST_CASE("Restores keep their side effects", "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(3);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;

  SECTION("undo refills empty style tables, except alignment styles") {
    st.tableStyles.clear();
    st.surveyPointStyles.clear();
    st.surveyPointLabelStyles.clear();
    st.alignmentStyles.clear();
    st.alignmentLabelSets.clear();
    st.alignmentLabelStyles.clear();
    st.dimensionStyles.clear();
    st.multileaderStyles.clear();
    PushUndoSnapshot(st, "empty tables");
    st.userLinesFlat = {0, 0, 0, 1, 1, 0};
    st.userLineAttrs.resize(1);
    REQUIRE(DoUndo(st, log));
    CHECK_FALSE(st.tableStyles.empty());
    CHECK_FALSE(st.surveyPointStyles.empty());
    CHECK_FALSE(st.surveyPointLabelStyles.empty());
    CHECK_FALSE(st.alignmentLabelSets.empty());
    CHECK_FALSE(st.alignmentLabelStyles.empty());
    CHECK_FALSE(st.dimensionStyles.empty());
    CHECK_FALSE(st.multileaderStyles.empty());
    CHECK(st.alignmentStyles.empty());  // today's behaviour; issue #1557 phase 1b adds this default
  }

  SECTION("a tab restore refills empty style tables, alignment styles included") {
    SaveDocumentToSnapshot(st, 1);
    RestoreDocumentFromSnapshot(st, 2);  // a stored document whose tables were never filled
    CHECK_FALSE(st.tableStyles.empty());
    CHECK_FALSE(st.surveyPointStyles.empty());
    CHECK_FALSE(st.surveyPointLabelStyles.empty());
    CHECK_FALSE(st.alignmentStyles.empty());
    CHECK_FALSE(st.alignmentLabelSets.empty());
    CHECK_FALSE(st.alignmentLabelStyles.empty());
    CHECK_FALSE(st.dimensionStyles.empty());
    CHECK_FALSE(st.multileaderStyles.empty());
  }

  SECTION("undo resets the multileader style draft and resizes labels for a restored plot scale") {
    st.activeMultileaderStyle.name = "Before";
    st.modelUnitsPerPlottedInch = 20.f;
    PushUndoSnapshot(st, "style and scale");
    st.activeMultileaderStyle.name = "After";
    st.mleaderStyleDraft.name = "Draft";
    SetDrawingPlotScale(st, 40.f);
    REQUIRE(st.surveyLabelLayoutCacheMup == 40.f);
    REQUIRE(DoUndo(st, log));
    CHECK(st.activeMultileaderStyle.name == "Before");
    CHECK(st.mleaderStyleDraft.name == "Before");
    CHECK(st.modelUnitsPerPlottedInch == 20.f);
    CHECK(st.surveyLabelLayoutCacheMup == 20.f);  // SetDrawingPlotScale ran on the restore
  }
}

TEST_CASE("A tab switch away and back round-trips every kind of drawing content",
          "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(3);
  st.activeDrawingIdx = 1;

  FillEveryKind(st, 1);
  const std::string first = SerializeGoSurveyJson(st);
  const Unserialized firstExtra = UnserializedOf(st);

  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  st.activeDrawingIdx = 2;
  // The other tab starts empty: nothing from the first drawing carries over.
  CHECK(st.userLinesFlat.empty());
  CHECK(st.cadSolids.empty());
  CHECK(st.pdfAttachments.empty());
  CHECK(SerializeGoSurveyJson(st).find("DC1-") == std::string::npos);

  FillEveryKind(st, 2);
  const std::string second = SerializeGoSurveyJson(st);
  const Unserialized secondExtra = UnserializedOf(st);
  REQUIRE(first != second);

  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  st.activeDrawingIdx = 1;
  CHECK(SerializeGoSurveyJson(st) == first);
  RequireSame(UnserializedOf(st), firstExtra);

  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  st.activeDrawingIdx = 2;
  CHECK(SerializeGoSurveyJson(st) == second);
  RequireSame(UnserializedOf(st), secondExtra);
}
