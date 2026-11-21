// Drawing content (ADR-0010 / issue #1557 phase 1a): undo and tab switching copy the same set of
// fields. These tests fill one entity of every kind plus non-default styles, layers and drawing
// properties, then require that undo/redo and a tab switch away and back reproduce the serialized
// drawing exactly. They use only the public undo, tab and serializer entry points, so they pass on
// the code before the DrawingContent move as well as after it.

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "CadCommands.hpp"
#include "GsIo.hpp"
#include <GoSurvey/Geometry/brep.hpp>

namespace {

template <typename T, typename = void> struct HasCadPoints : std::false_type {};
template <typename T>
struct HasCadPoints<T, std::void_t<decltype(std::declval<T&>().cadPoints)>> : std::true_type {};

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

  st.drawing.userLinesFlat = {o, 0, 0, o + 1, 1, 0};
  st.drawing.userLineAttrs = attr("line");
  st.drawing.userCirclesCxCyZR = {o, 2, 0, 1.5};
  st.drawing.userCircleAttrs = attr("circle");
  st.drawing.userCircleNormals = {0.f, 0.f, 1.f};
  CadArc arc;
  arc.cx = o;
  arc.r = 2.0 + v;
  arc.sweepRad = 1.f;
  st.drawing.userArcs = {arc};
  st.drawing.userArcAttrs = attr("arc");
  CadEllipse ell;
  ell.cx = o;
  ell.ratio = 0.25f * v;
  st.drawing.userEllipses = {ell};
  st.drawing.userEllAttrs = attr("ellipse");
  st.drawing.userPolylineOffsets = {0, 3};
  st.drawing.userPolylineVerts = {o, 0, 0, o + 1, 0, 0, o + 1, 1, 0};
  st.drawing.userPolylineVertsBulge = {0.f, 0.5f, 0.f};
  st.drawing.userPolylineVertsNormal = {0, 0, 1, 0, 0, 1, 0, 0, 1};
  st.drawing.userPolylineClosed = {static_cast<uint8_t>(v == 1 ? 1 : 0)};
  st.drawing.userPolylineAttrs = attr("polyline");

  st.drawing.featureLineOffsets = {0, 2};
  st.drawing.featureLineVerts = {o, 5, 1, o + 3, 5, 2};
  st.drawing.featureLineClosed = {0};
  st.drawing.featureLineElevPt = {0, 0};
  CadFeatureLineInfo fl;
  fl.description = tag + "featureline";
  st.drawing.featureLineInfo = {fl};
  CadGradingPreset preset;
  preset.name = tag + "grading";
  st.drawing.gradingPresets = {preset};
  st.drawing.featureLineAttrs = attr("featureline");

  CadAnnotation text;
  text.insX = static_cast<float>(o);
  text.text = tag + "text";
  st.drawing.cadAnnotations = {text};
  st.drawing.cadAnnotationAttrs = attr("text");
  CadFilledRegion region;
  region.vertsXyz = {o, 0, 0, o + 1, 0, 0, o, 1, 0};
  region.patternName = tag + "hatch";
  st.drawing.cadFilledRegions = {region};
  st.drawing.cadFilledRegionAttrs = attr("hatch");

  auto mesh = std::make_shared<CadMesh>();
  mesh->vertsXyz = {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f};
  mesh->indices = {0, 1, 2};
  mesh->sourceName = tag + "mesh";
  st.drawing.cadMeshes = {mesh};
  st.drawing.cadMeshAttrs = attr("mesh");
  auto cloud = std::make_shared<CadPointCloud>();
  cloud->sourcePath = tag + "cloud.e57";
  st.drawing.cadPointClouds = {cloud};
  st.drawing.cadPointCloudAttrs = attr("cloud");
  CadSurface surface;
  surface.name = tag + "surface";
  st.drawing.cadSurfaces = {surface};
  st.drawing.cadSurfaceAttrs = attr("surface");
  st.drawing.cadSolids = {std::make_shared<const brep::Solid>()};
  st.drawing.cadSolidAttrs = attr("solid");
  CadTable table;
  table.insX = static_cast<float>(o);
  table.cells = {tag + "cell", "b"};
  st.drawing.cadTables = {table};
  st.drawing.cadTableAttrs = attr("table");
  CadTableStyle tableStyle;
  tableStyle.name = tag + "tablestyle";
  st.drawing.tableStyles = {tableStyle};
  CadPositionMarker marker;
  marker.x = o;
  marker.label.text = tag + "marker";
  st.drawing.cadPositionMarkers = {marker};
  st.drawing.cadPositionMarkerAttrs = attr("marker");
  CadMultileader leader;
  leader.pathXyz = {static_cast<float>(o), 0.f, 0.f, static_cast<float>(o) + 2.f, 2.f, 0.f};
  leader.styleName = tag + "mleaderstyle";
  st.drawing.cadMultileaders = {leader};
  st.drawing.cadMultileaderAttrs = attr("mleader");
  CadSpline spline;
  spline.degree = 1;
  spline.ctrl = {{o, 0, 0}, {o + 1, 1, 0}};
  spline.knots = {0, 0, 1, 1};
  st.drawing.cadSplines = {spline};
  st.drawing.cadSplineAttrs = attr("spline");
  CadPoint point;
  point.x = o;
  st.drawing.cadPoints = {point};
  st.drawing.cadPointAttrs = attr("point");
  st.drawing.pdMode = 2 + v;
  st.drawing.pdSize = 0.5 * v;

  CadPipeRun run;
  run.name = tag + "piperun";
  run.vertsXyz = {o, 0, 0, o + 5, 0, 0};
  run.nominalSize = "4";
  st.drawing.cadPipeRuns = {run};
  st.drawing.cadPipeRunAttrs = attr("piperun");
  alignment::Alignment al;
  al.id = 100 + v;
  al.name = tag + "alignment";
  st.drawing.alignments = {al};
  alignment::ViewFrameGroup frames;
  frames.id = 200 + v;
  st.drawing.viewFrameGroups = {frames};
  alignment::SampleLine sample;
  sample.id = 300 + v;
  sample.name = tag + "sample";
  st.drawing.sampleLines = {sample};
  alignment::VerticalProfile profile;
  profile.id = 400 + v;
  profile.name = tag + "profile";
  st.drawing.profiles = {profile};
  CadSurfaceProfile surfaceProfile;
  surfaceProfile.id = 500 + v;
  surfaceProfile.name = tag + "egprofile";
  st.drawing.surfaceProfiles = {surfaceProfile};
  CadPipingSystem system;
  system.name = tag + "network";
  system.pipeRunIndices = {0};
  st.drawing.cadPipingSystems = {system};

  CadBlockDefinition def;
  def.name = tag + "block";
  st.drawing.blockDefs = {def};
  CadBlockRef ref;
  ref.defName = def.name;
  st.drawing.cadBlockRefs = {ref};
  st.drawing.cadBlockRefAttrs = attr("blockref");

  SurveyPoint sp;
  sp.id = 10 + v;
  sp.easting = o;
  sp.description = tag + "point";
  st.drawing.surveyPoints = {sp};
  SurveyPointStyleDefinition pointStyle;
  pointStyle.name = tag + "pointstyle";
  st.drawing.surveyPointStyles = {pointStyle};
  SurveyPointLabelStyleDefinition labelStyle;
  labelStyle.name = tag + "labelstyle";
  st.drawing.surveyPointLabelStyles = {labelStyle};
  PointGroup group;
  group.name = tag + "group";
  st.drawing.pointGroups = {group};

  CadLayerRow layer0;
  layer0.name = "0";
  CadLayerRow layer;
  layer.name = tag + "layer";
  layer.color = v == 1 ? "Green" : "Cyan";
  st.drawing.drawingLayerTable = {layer0, layer};
  TextStyle textStyle;
  textStyle.name = tag + "textstyle";
  st.drawing.textStyles = {textStyle};
  SurfaceStyle surfaceStyle;
  surfaceStyle.name = tag + "surfacestyle";
  st.drawing.surfaceStyles = {surfaceStyle};
  AlignmentStyle alignmentStyle;
  alignmentStyle.name = tag + "alignmentstyle";
  st.drawing.alignmentStyles = {alignmentStyle};
  alignment::AlignmentCheckSet checkSet;
  checkSet.name = tag + "checkset";
  st.drawing.alignmentCheckSets = {checkSet};
  AlignmentLabelSet labelSet;
  labelSet.name = tag + "labelset";
  st.drawing.alignmentLabelSets = {labelSet};
  AlignmentLabelStyle alignmentLabelStyle;
  alignmentLabelStyle.name = tag + "alignmentlabelstyle";
  st.drawing.alignmentLabelStyles = {alignmentLabelStyle};
  DimensionStyle dimStyle;
  dimStyle.name = tag + "dimstyle";
  st.drawing.activeDimensionStyle = dimStyle;
  st.drawing.dimensionStyles = {dimStyle};
  MultileaderStyle mleaderStyle;
  mleaderStyle.name = tag + "mleaderstyle";
  st.drawing.activeMultileaderStyle = mleaderStyle;
  st.drawing.multileaderStyles = {mleaderStyle};

  PdfAttachment pdf;
  pdf.filePath = tag + "underlay.pdf";
  st.drawing.pdfAttachments = {pdf};
  PaperLayout sheet;
  sheet.name = tag + "sheet";
  st.drawing.paperLayouts = {sheet};
  CadRasterImage raster;
  raster.path = tag + "raster.png";
  st.drawing.cadRasterImages = {raster};

  st.drawing.worldDocumentOriginX = 1000.0 * v;
  st.drawing.worldDocumentOriginY = 2000.0 * v;
  st.drawing.drawingInsUnits = v == 1 ? 1 : 6;
  st.drawing.modelUnitsPerPlottedInch = 20.f * v;
  st.drawing.drawingSettings.zoneCode = tag + "zone";
  st.drawing.drawingSettings.markerX = o;
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
  u.pdfPath = st.drawing.pdfAttachments.empty() ? "" : st.drawing.pdfAttachments[0].filePath;
  u.mesh = st.drawing.cadMeshes.empty() ? nullptr : st.drawing.cadMeshes[0].get();
  u.cloud = st.drawing.cadPointClouds.empty() ? nullptr : st.drawing.cadPointClouds[0].get();
  u.solid = st.drawing.cadSolids.empty() ? nullptr : st.drawing.cadSolids[0].get();
  return u;
}

void RequireSame(const Unserialized& a, const Unserialized& b) {
  CHECK(a.pdfPath == b.pdfPath);
  CHECK(a.mesh == b.mesh);  // shared payload, never cloned (architecture §11.5)
  CHECK(a.cloud == b.cloud);
  CHECK(a.solid == b.solid);
}

} // namespace

TEST_CASE("Document drawing entity store stays separate from block content", "[issue1557][drawing-content]") {
  STATIC_REQUIRE(std::is_base_of_v<CadDrawingEntityStore, DrawingContent>);
  STATIC_REQUIRE(std::is_base_of_v<CadEntityStore, CadBlockContent>);
  STATIC_REQUIRE_FALSE(std::is_base_of_v<CadDrawingEntityStore, CadBlockContent>);

  AppCommandState state;
  state.drawing.userLinesFlat = {0, 0, 0, 1, 0, 0};
  state.drawing.cadPoints = {CadPoint{2, 3, 4}};
  CadTable table;
  table.insX = 5.f;
  state.drawing.cadTables = {table};
  DrawingContent content = static_cast<const DrawingContent&>(state.drawing);
  CHECK(content.cadPoints.size() == 1);
  CHECK(content.cadPoints.front().x == 2.0);
  CHECK(content.cadTables.size() == 1);

  CadBlockContent block;
  static_cast<CadEntityStore&>(block) = static_cast<const CadEntityStore&>(state.drawing);

  CHECK(block.userLinesFlat == state.drawing.userLinesFlat);
  STATIC_REQUIRE_FALSE(HasCadPoints<CadBlockContent>::value);
}

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

  SECTION("undo refills empty style tables, alignment styles included") {
    st.drawing.tableStyles.clear();
    st.drawing.surveyPointStyles.clear();
    st.drawing.surveyPointLabelStyles.clear();
    st.drawing.alignmentStyles.clear();
    st.drawing.alignmentLabelSets.clear();
    st.drawing.alignmentLabelStyles.clear();
    st.drawing.dimensionStyles.clear();
    st.drawing.multileaderStyles.clear();
    PushUndoSnapshot(st, "empty tables");
    st.drawing.userLinesFlat = {0, 0, 0, 1, 1, 0};
    st.drawing.userLineAttrs.resize(1);
    REQUIRE(DoUndo(st, log));
    CHECK_FALSE(st.drawing.tableStyles.empty());
    CHECK_FALSE(st.drawing.surveyPointStyles.empty());
    CHECK_FALSE(st.drawing.surveyPointLabelStyles.empty());
    CHECK_FALSE(st.drawing.alignmentLabelSets.empty());
    CHECK_FALSE(st.drawing.alignmentLabelStyles.empty());
    CHECK_FALSE(st.drawing.dimensionStyles.empty());
    CHECK_FALSE(st.drawing.multileaderStyles.empty());
    CHECK_FALSE(st.drawing.alignmentStyles.empty());  // phase 1b: undo refills it, as the tab restore does
  }

  SECTION("a tab restore refills empty style tables, alignment styles included") {
    SaveDocumentToSnapshot(st, 1);
    RestoreDocumentFromSnapshot(st, 2);  // a stored document whose tables were never filled
    CHECK_FALSE(st.drawing.tableStyles.empty());
    CHECK_FALSE(st.drawing.surveyPointStyles.empty());
    CHECK_FALSE(st.drawing.surveyPointLabelStyles.empty());
    CHECK_FALSE(st.drawing.alignmentStyles.empty());
    CHECK_FALSE(st.drawing.alignmentLabelSets.empty());
    CHECK_FALSE(st.drawing.alignmentLabelStyles.empty());
    CHECK_FALSE(st.drawing.dimensionStyles.empty());
    CHECK_FALSE(st.drawing.multileaderStyles.empty());
  }

  SECTION("undo falls back to 0 and Standard for a current name that no longer exists") {
    st.drawing.currentLayer = "Gone";
    st.drawing.alignmentStyleCurrent = "Gone";
    st.drawing.activeTextStyleName = "Gone";
    st.drawing.defaultPlottedTextHeightInches = 0.2f;
    PushUndoSnapshot(st, "missing names");
    st.drawing.currentLayer = "0";
    st.drawing.alignmentStyleCurrent = "Standard";
    st.drawing.activeTextStyleName = "Standard";
    REQUIRE(DoUndo(st, log));
    REQUIRE(TextStyles::Find(st.drawing.textStyles, "Standard") != nullptr);
    CHECK(st.drawing.currentLayer == "0");
    CHECK(st.drawing.alignmentStyleCurrent == "Standard");
    CHECK(st.drawing.activeTextStyleName == "Standard");
    CHECK(st.drawing.defaultPlottedTextHeightInches == TextStyles::Find(st.drawing.textStyles, "Standard")->heightInches);
  }

  SECTION("a tab restore falls back to 0 and Standard for a current name that no longer exists") {
    st.documents[2].content.currentLayer = "Gone";
    st.documents[2].content.alignmentStyleCurrent = "Gone";
    st.documents[2].content.activeTextStyleName = "Gone";
    st.documents[2].content.defaultPlottedTextHeightInches = 0.2f;
    RestoreDocumentFromSnapshot(st, 2);
    REQUIRE(TextStyles::Find(st.drawing.textStyles, "Standard") != nullptr);
    CHECK(st.drawing.currentLayer == "0");
    CHECK(st.drawing.alignmentStyleCurrent == "Standard");
    CHECK(st.drawing.activeTextStyleName == "Standard");
    CHECK(st.drawing.defaultPlottedTextHeightInches == TextStyles::Find(st.drawing.textStyles, "Standard")->heightInches);
  }

  SECTION("undo resets the multileader style draft and resizes labels for a restored plot scale") {
    st.drawing.activeMultileaderStyle.name = "Before";
    st.drawing.modelUnitsPerPlottedInch = 20.f;
    PushUndoSnapshot(st, "style and scale");
    st.drawing.activeMultileaderStyle.name = "After";
    st.mleaderStyleDraft.name = "Draft";
    SetDrawingPlotScale(st, 40.f);
    REQUIRE(st.surveyLabelLayoutCacheMup == 40.f);
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.activeMultileaderStyle.name == "Before");
    CHECK(st.mleaderStyleDraft.name == "Before");
    CHECK(st.drawing.modelUnitsPerPlottedInch == 20.f);
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
  CHECK(st.drawing.userLinesFlat.empty());
  CHECK(st.drawing.cadSolids.empty());
  CHECK(st.drawing.pdfAttachments.empty());
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

TEST_CASE("Undo restores each drawing field that undo used to miss", "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  CadLayerRow walls;
  walls.name = "Walls";
  st.drawing.drawingLayerTable.push_back(walls);

  SECTION("current layer and colour") {
    st.drawing.currentLayer = "Walls";
    st.drawing.currentColor = "Red";
    PushUndoSnapshot(st, "current layer");
    st.drawing.currentLayer = "0";
    st.drawing.currentColor = "ByLayer";
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.currentLayer == "Walls");
    CHECK(st.drawing.currentColor == "Red");
  }

  SECTION("active text style and its default height") {
    TextStyle notes;
    notes.name = "Notes";
    notes.heightInches = 0.2f;
    st.drawing.textStyles.push_back(notes);
    SetActiveTextStyle(st, "Notes");
    PushUndoSnapshot(st, "text style");
    SetActiveTextStyle(st, "Standard");
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.activeTextStyleName == "Notes");
    CHECK(st.drawing.defaultPlottedTextHeightInches == 0.2f);
  }

  SECTION("alignment style and station format") {
    AlignmentStyle design;
    design.name = "Design";
    st.drawing.alignmentStyles.push_back(design);
    st.drawing.alignmentStyleCurrent = "Design";
    alignment::StationFormat fmt;
    fmt.delimiter = 'K';
    st.drawing.alignmentStationFormat = fmt;
    PushUndoSnapshot(st, "alignment style");
    st.drawing.alignmentStyleCurrent = "Standard";
    st.drawing.alignmentStationFormat = alignment::StationFormat{};
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.alignmentStyleCurrent == "Design");
    CHECK(st.drawing.alignmentStationFormat.delimiter == 'K');
  }

  SECTION("point visibility") {
    st.drawing.pointVisibility.idRanges = "1-500";
    PushUndoSnapshot(st, "hide points");
    st.drawing.pointVisibility = projpts::Rules{};
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.pointVisibility.idRanges == "1-500");
  }

  SECTION("saved page setups") {
    PageSetup setup;
    setup.name = "Plot A";
    st.drawing.savedPageSetups.push_back(setup);
    PushUndoSnapshot(st, "page setup");
    st.drawing.savedPageSetups.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.drawing.savedPageSetups.size() == 1);
    CHECK(st.drawing.savedPageSetups[0].name == "Plot A");
  }

  SECTION("named UCS and named views") {
    NamedUcs site;
    site.name = "Site";
    st.drawing.ucsNamed.push_back(site);
    NamedView overview;
    overview.name = "Overview";
    st.drawing.namedViews.push_back(overview);
    PushUndoSnapshot(st, "named ucs and view");
    st.drawing.ucsNamed.clear();
    st.drawing.namedViews.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.drawing.ucsNamed.size() == 1);
    REQUIRE(st.drawing.namedViews.size() == 1);
    CHECK(st.drawing.ucsNamed[0].name == "Site");
    CHECK(st.drawing.namedViews[0].name == "Overview");
  }

  SECTION("annotation scale list and viewport zoom scale list") {
    CadAnnotationScale scale;
    scale.name = "1:50";
    st.drawing.annotationScales.push_back(scale);
    st.drawing.viewportZoomScales.push_back(scale);
    PushUndoSnapshot(st, "scales");
    st.drawing.annotationScales.clear();
    st.drawing.viewportZoomScales.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.drawing.annotationScales.size() == 1);
    REQUIRE(st.drawing.viewportZoomScales.size() == 1);
    CHECK(st.drawing.annotationScales[0].name == "1:50");
    CHECK(st.drawing.viewportZoomScales[0].name == "1:50");
  }

  SECTION("point numbering rewinds with the undo of point creation") {
    st.drawing.createPointsNextId = 40;
    REQUIRE(TryPlaceSurveyPoint(st, 1.0, 2.0, 0.0, log));
    REQUIRE(st.drawing.surveyPoints.size() == 1);
    CHECK(st.drawing.createPointsNextId == 41);
    REQUIRE(DoUndo(st, log));
    CHECK(st.drawing.surveyPoints.empty());
    CHECK(st.drawing.createPointsNextId == 40);
  }
}

TEST_CASE("A tab switch does not carry phase 1b drawing fields into another drawing",
          "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(3);
  st.activeDrawingIdx = 1;
  CadLayerRow walls;
  walls.name = "Walls";
  st.drawing.drawingLayerTable.push_back(walls);
  TextStyle notes;
  notes.name = "Notes";
  notes.heightInches = 0.2f;
  st.drawing.textStyles.push_back(notes);
  AlignmentStyle design;
  design.name = "Design";
  st.drawing.alignmentStyles.push_back(design);

  st.drawing.currentLayer = "Walls";
  st.drawing.currentColor = "Red";
  st.drawing.alignmentStyleCurrent = "Design";
  SetActiveTextStyle(st, "Notes");
  alignment::StationFormat fmt;
  fmt.delimiter = 'K';
  st.drawing.alignmentStationFormat = fmt;
  st.drawing.pointVisibility.idRanges = "1-500";
  PageSetup setup;
  setup.name = "Plot A";
  st.drawing.savedPageSetups.push_back(setup);
  NamedUcs site;
  site.name = "Site";
  st.drawing.ucsNamed.push_back(site);
  NamedView overview;
  overview.name = "Overview";
  st.drawing.namedViews.push_back(overview);
  CadAnnotationScale scale;
  scale.name = "1:50";
  st.drawing.annotationScales.push_back(scale);
  st.drawing.viewportZoomScales.push_back(scale);
  st.drawing.createPointsOpts.startNumber = 40;
  st.drawing.createPointsNextId = 42;

  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  st.activeDrawingIdx = 2;
  CHECK(st.drawing.currentLayer == "0");
  CHECK(st.drawing.currentColor == "ByLayer");
  CHECK(st.drawing.alignmentStyleCurrent == "Standard");
  CHECK(st.drawing.activeTextStyleName == "Standard");
  CHECK(st.drawing.defaultPlottedTextHeightInches == 0.125f);
  CHECK(st.drawing.alignmentStationFormat.delimiter == '+');
  CHECK(st.drawing.pointVisibility.IsDefault());
  CHECK(st.drawing.savedPageSetups.empty());
  CHECK(st.drawing.ucsNamed.empty());
  CHECK(st.drawing.namedViews.empty());
  CHECK(st.drawing.annotationScales.empty());
  CHECK(st.drawing.viewportZoomScales.empty());
  CHECK(st.drawing.createPointsNextId == 1);
  CHECK(st.drawing.createPointsOpts.startNumber == 1);

  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  st.activeDrawingIdx = 1;
  CHECK(st.drawing.currentLayer == "Walls");
  CHECK(st.drawing.currentColor == "Red");
  CHECK(st.drawing.alignmentStyleCurrent == "Design");
  CHECK(st.drawing.activeTextStyleName == "Notes");
  CHECK(st.drawing.defaultPlottedTextHeightInches == 0.2f);
  CHECK(st.drawing.alignmentStationFormat.delimiter == 'K');
  CHECK(st.drawing.pointVisibility.idRanges == "1-500");
  REQUIRE(st.drawing.savedPageSetups.size() == 1);
  CHECK(st.drawing.savedPageSetups[0].name == "Plot A");
  REQUIRE(st.drawing.ucsNamed.size() == 1);
  REQUIRE(st.drawing.namedViews.size() == 1);
  REQUIRE(st.drawing.annotationScales.size() == 1);
  REQUIRE(st.drawing.viewportZoomScales.size() == 1);
  CHECK(st.drawing.createPointsNextId == 42);
  CHECK(st.drawing.createPointsOpts.startNumber == 40);
}

TEST_CASE("Tab-only drawing state stays with its own drawing and is not undone",
          "[issue1557][drawing-content]") {
  AppCommandState st;
  std::vector<std::string> log;
  st.documents.resize(3);
  st.activeDrawingIdx = 1;

  st.viewportSectionClipFrameValid = true;
  st.viewportSectionClipFrame.origin.z = 5.0;
  st.currentViewportZoomScaleIndex = 2;
  st.currentAnnotationScaleIndex = 1;
  CadDwgImportedLight lamp;
  lamp.name = "Lamp";
  st.dwgImportedLights.push_back(lamp);
  st.blockFavorites = {"Valve"};
  st.blockRecent = {"Pump"};

  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  st.activeDrawingIdx = 2;
  CHECK_FALSE(st.viewportSectionClipFrameValid);
  CHECK(st.currentViewportZoomScaleIndex == -1);
  CHECK(st.currentAnnotationScaleIndex == -1);
  CHECK(st.dwgImportedLights.empty());
  CHECK(st.blockFavorites.empty());
  CHECK(st.blockRecent.empty());

  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  st.activeDrawingIdx = 1;
  CHECK(st.viewportSectionClipFrameValid);
  CHECK(st.viewportSectionClipFrame.origin.z == 5.0);
  CHECK(st.currentViewportZoomScaleIndex == 2);
  CHECK(st.currentAnnotationScaleIndex == 1);
  REQUIRE(st.dwgImportedLights.size() == 1);
  CHECK(st.dwgImportedLights[0].name == "Lamp");
  CHECK(st.blockFavorites == std::vector<std::string>{"Valve"});
  CHECK(st.blockRecent == std::vector<std::string>{"Pump"});

  // Undo never reverts tab-only state: a frame pushed at scale 1 and undone after a change to 3 keeps 3.
  st.currentAnnotationScaleIndex = 1;
  PushUndoSnapshot(st, "scale");
  st.currentAnnotationScaleIndex = 3;
  REQUIRE(DoUndo(st, log));
  CHECK(st.currentAnnotationScaleIndex == 3);
  CHECK(st.blockFavorites == std::vector<std::string>{"Valve"});
}

TEST_CASE("Circle records are read and written through the entity store", "[issue1557][phase5][circles]") {
  CadEntityStore store;
  CHECK(store.circleCount() == 0);

  store.pushCircle(CadCircleRecord{1.0, 2.0, 3.0, 4.0});
  store.pushCircle(CadCircleRecord{5.0, 6.0, 7.0, 8.0});
  CHECK(store.circleCount() == 2);
  CHECK(store.userCirclesCxCyZR == std::vector<double>{1, 2, 3, 4, 5, 6, 7, 8});

  const CadCircleRecord second = store.circle(1);
  CHECK(second.cx == 5.0);
  CHECK(second.cy == 6.0);
  CHECK(second.cz == 7.0);
  CHECK(second.r == 8.0);

  store.setCircle(0, CadCircleRecord{9.0, 10.0, 11.0, 12.0});
  CHECK(store.circle(0).r == 12.0);
  CHECK(store.circle(1).cx == 5.0);  // neighbour untouched

  store.eraseCircle(0);
  CHECK(store.circleCount() == 1);
  CHECK(store.circle(0).cx == 5.0);
  CHECK(store.circle(0).r == 8.0);
}

TEST_CASE("Line records are read, written and erased through the entity store", "[issue1557][phase5][lines]") {
  CadEntityStore store;
  CHECK(store.lineCount() == 0);

  store.pushLine(CadLineRecord{1.0, 2.0, 3.0, 4.0, 5.0, 6.0});
  store.pushLine(CadLineRecord{7.0, 8.0, 9.0, 10.0, 11.0, 12.0});
  CHECK(store.lineCount() == 2);
  CHECK(store.userLinesFlat == std::vector<double>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});

  const CadLineRecord second = store.line(1);
  CHECK(second.x0 == 7.0);
  CHECK(second.y0 == 8.0);
  CHECK(second.z0 == 9.0);
  CHECK(second.x1 == 10.0);
  CHECK(second.y1 == 11.0);
  CHECK(second.z1 == 12.0);

  store.setLine(0, CadLineRecord{13.0, 14.0, 15.0, 16.0, 17.0, 18.0});
  CHECK(store.line(0).x1 == 16.0);
  CHECK(store.line(1).x0 == 7.0);  // neighbour untouched

  store.lineComponent(1, CadLineComponent::Z1) = 99.0;
  CHECK(store.line(1).z1 == 99.0);

  store.eraseLine(0);
  CHECK(store.lineCount() == 1);
  CHECK(store.line(0).x0 == 7.0);
  CHECK(store.lineStoreWellFormed());
}

TEST_CASE("Polyline vertices are read, written and erased through the entity store", "[issue1557][phase5][vertices]") {
  CadEntityStore store;
  CHECK(store.vertexCount() == 0);

  store.pushVertex(CadPolyVertex{1.0, 2.0, 3.0});
  store.pushVertex(CadPolyVertex{4.0, 5.0, 6.0});
  store.pushVertex(CadPolyVertex{7.0, 8.0, 9.0});
  CHECK(store.vertexCount() == 3);
  CHECK(store.userPolylineVerts == std::vector<double>{1, 2, 3, 4, 5, 6, 7, 8, 9});

  const CadPolyVertex mid = store.vertex(1);
  CHECK(mid.x == 4.0);
  CHECK(mid.y == 5.0);
  CHECK(mid.z == 6.0);

  store.setVertex(0, CadPolyVertex{10.0, 11.0, 12.0});
  CHECK(store.vertex(0).z == 12.0);
  CHECK(store.vertex(1).x == 4.0);  // neighbour untouched

  store.vertexComponent(2, CadVertexComponent::Y) = 80.0;
  CHECK(store.vertex(2).y == 80.0);
  CHECK(store.vertex(2).x == 7.0);

  store.eraseVertexRange(0, 2);
  CHECK(store.vertexCount() == 1);
  CHECK(store.vertex(0).x == 7.0);
  CHECK(store.vertex(0).y == 80.0);
  CHECK(store.userPolylineVerts.size() % 3 == 0);
}

TEST_CASE("Clearing CAD geometry replaces CAD content but keeps drawing setup", "[issue1557][phase6]") {
  AppCommandState state;
  state.drawing.userLinesFlat = {1, 2, 3, 4, 5, 6};
  state.drawing.userPolylineVerts = {1, 2, 3};
  state.drawing.userPolylineVertsNormal = {0, 1, 0};
  state.drawing.cadTables.emplace_back();
  state.drawing.cadPoints.emplace_back();
  state.drawing.cadSurfaces.emplace_back();
  state.drawing.alignments.emplace_back();
  state.drawing.featureLineVerts = {1, 2, 3};
  state.drawing.annotationScales.emplace_back();
  state.drawing.viewportZoomScales.emplace_back();
  state.drawing.cadPipeRuns.emplace_back();
  state.drawing.drawingLayerTable.push_back(CadLayerRow{});
  state.drawing.textStyles.emplace_back();
  state.drawing.drawingInsUnits = 6;
  state.drawing.surveyPoints.emplace_back();
  state.drawing.paperLayouts.emplace_back();
  const size_t layerCountBefore = state.drawing.drawingLayerTable.size();
  const size_t textStyleCountBefore = state.drawing.textStyles.size();
  state.drawing.worldDocumentOriginX = 123.0;
  state.drawing.worldDocumentOriginY = 456.0;
  state.currentAnnotationScaleIndex = 0;
  state.currentViewportZoomScaleIndex = 0;
  state.nextEntityId = 42;

  ClearCadGeometry(state);

  CHECK(state.drawing.userLinesFlat.empty());
  CHECK(state.drawing.userPolylineVerts.empty());
  CHECK(state.drawing.userPolylineVertsNormal.empty());
  CHECK(state.drawing.cadTables.empty());
  CHECK(state.drawing.cadPoints.empty());
  CHECK(state.drawing.cadSurfaces.empty());
  CHECK(state.drawing.alignments.empty());
  CHECK(state.drawing.featureLineVerts == std::vector<double>{1, 2, 3});
  CHECK(state.drawing.annotationScales.empty());
  CHECK(state.drawing.viewportZoomScales.empty());
  CHECK(state.drawing.cadPipeRuns.empty());
  CHECK(state.drawing.drawingLayerTable.size() == layerCountBefore);
  CHECK(state.drawing.textStyles.size() == textStyleCountBefore);
  CHECK(state.drawing.drawingInsUnits == 6);
  CHECK(state.drawing.surveyPoints.size() == 1);
  CHECK(state.drawing.paperLayouts.size() == 1);
  CHECK(state.drawing.worldDocumentOriginX == 0.0);
  CHECK(state.drawing.worldDocumentOriginY == 0.0);
  CHECK(state.currentAnnotationScaleIndex == -1);
  CHECK(state.currentViewportZoomScaleIndex == -1);
  CHECK(state.nextEntityId == 1);
}
