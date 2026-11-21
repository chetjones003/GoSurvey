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

TEST_CASE("Document drawing entity store stays separate from block content", "[issue1557][drawing-content]") {
  STATIC_REQUIRE(std::is_base_of_v<CadDrawingEntityStore, DrawingContent>);
  STATIC_REQUIRE(std::is_base_of_v<CadEntityStore, CadBlockContent>);
  STATIC_REQUIRE_FALSE(std::is_base_of_v<CadDrawingEntityStore, CadBlockContent>);

  AppCommandState state;
  state.userLinesFlat = {0, 0, 0, 1, 0, 0};
  state.cadPoints = {CadPoint{2, 3, 4}};
  CadTable table;
  table.insX = 5.f;
  state.cadTables = {table};
  DrawingContent content = static_cast<const DrawingContent&>(state);
  CHECK(content.cadPoints.size() == 1);
  CHECK(content.cadPoints.front().x == 2.0);
  CHECK(content.cadTables.size() == 1);

  CadBlockContent block;
  static_cast<CadEntityStore&>(block) = static_cast<const CadEntityStore&>(state);

  CHECK(block.userLinesFlat == state.userLinesFlat);
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
    CHECK_FALSE(st.alignmentStyles.empty());  // phase 1b: undo refills it, as the tab restore does
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

  SECTION("undo falls back to 0 and Standard for a current name that no longer exists") {
    st.currentLayer = "Gone";
    st.alignmentStyleCurrent = "Gone";
    st.activeTextStyleName = "Gone";
    st.defaultPlottedTextHeightInches = 0.2f;
    PushUndoSnapshot(st, "missing names");
    st.currentLayer = "0";
    st.alignmentStyleCurrent = "Standard";
    st.activeTextStyleName = "Standard";
    REQUIRE(DoUndo(st, log));
    REQUIRE(TextStyles::Find(st.textStyles, "Standard") != nullptr);
    CHECK(st.currentLayer == "0");
    CHECK(st.alignmentStyleCurrent == "Standard");
    CHECK(st.activeTextStyleName == "Standard");
    CHECK(st.defaultPlottedTextHeightInches == TextStyles::Find(st.textStyles, "Standard")->heightInches);
  }

  SECTION("a tab restore falls back to 0 and Standard for a current name that no longer exists") {
    st.documents[2].content.currentLayer = "Gone";
    st.documents[2].content.alignmentStyleCurrent = "Gone";
    st.documents[2].content.activeTextStyleName = "Gone";
    st.documents[2].content.defaultPlottedTextHeightInches = 0.2f;
    RestoreDocumentFromSnapshot(st, 2);
    REQUIRE(TextStyles::Find(st.textStyles, "Standard") != nullptr);
    CHECK(st.currentLayer == "0");
    CHECK(st.alignmentStyleCurrent == "Standard");
    CHECK(st.activeTextStyleName == "Standard");
    CHECK(st.defaultPlottedTextHeightInches == TextStyles::Find(st.textStyles, "Standard")->heightInches);
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

TEST_CASE("Undo restores each drawing field that undo used to miss", "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(2);
  st.activeDrawingIdx = 1;
  std::vector<std::string> log;
  CadLayerRow walls;
  walls.name = "Walls";
  st.drawingLayerTable.push_back(walls);

  SECTION("current layer and colour") {
    st.currentLayer = "Walls";
    st.currentColor = "Red";
    PushUndoSnapshot(st, "current layer");
    st.currentLayer = "0";
    st.currentColor = "ByLayer";
    REQUIRE(DoUndo(st, log));
    CHECK(st.currentLayer == "Walls");
    CHECK(st.currentColor == "Red");
  }

  SECTION("active text style and its default height") {
    TextStyle notes;
    notes.name = "Notes";
    notes.heightInches = 0.2f;
    st.textStyles.push_back(notes);
    SetActiveTextStyle(st, "Notes");
    PushUndoSnapshot(st, "text style");
    SetActiveTextStyle(st, "Standard");
    REQUIRE(DoUndo(st, log));
    CHECK(st.activeTextStyleName == "Notes");
    CHECK(st.defaultPlottedTextHeightInches == 0.2f);
  }

  SECTION("alignment style and station format") {
    AlignmentStyle design;
    design.name = "Design";
    st.alignmentStyles.push_back(design);
    st.alignmentStyleCurrent = "Design";
    alignment::StationFormat fmt;
    fmt.delimiter = 'K';
    st.alignmentStationFormat = fmt;
    PushUndoSnapshot(st, "alignment style");
    st.alignmentStyleCurrent = "Standard";
    st.alignmentStationFormat = alignment::StationFormat{};
    REQUIRE(DoUndo(st, log));
    CHECK(st.alignmentStyleCurrent == "Design");
    CHECK(st.alignmentStationFormat.delimiter == 'K');
  }

  SECTION("point visibility") {
    st.pointVisibility.idRanges = "1-500";
    PushUndoSnapshot(st, "hide points");
    st.pointVisibility = projpts::Rules{};
    REQUIRE(DoUndo(st, log));
    CHECK(st.pointVisibility.idRanges == "1-500");
  }

  SECTION("saved page setups") {
    PageSetup setup;
    setup.name = "Plot A";
    st.savedPageSetups.push_back(setup);
    PushUndoSnapshot(st, "page setup");
    st.savedPageSetups.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.savedPageSetups.size() == 1);
    CHECK(st.savedPageSetups[0].name == "Plot A");
  }

  SECTION("named UCS and named views") {
    NamedUcs site;
    site.name = "Site";
    st.ucsNamed.push_back(site);
    NamedView overview;
    overview.name = "Overview";
    st.namedViews.push_back(overview);
    PushUndoSnapshot(st, "named ucs and view");
    st.ucsNamed.clear();
    st.namedViews.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.ucsNamed.size() == 1);
    REQUIRE(st.namedViews.size() == 1);
    CHECK(st.ucsNamed[0].name == "Site");
    CHECK(st.namedViews[0].name == "Overview");
  }

  SECTION("annotation scale list and viewport zoom scale list") {
    CadAnnotationScale scale;
    scale.name = "1:50";
    st.annotationScales.push_back(scale);
    st.viewportZoomScales.push_back(scale);
    PushUndoSnapshot(st, "scales");
    st.annotationScales.clear();
    st.viewportZoomScales.clear();
    REQUIRE(DoUndo(st, log));
    REQUIRE(st.annotationScales.size() == 1);
    REQUIRE(st.viewportZoomScales.size() == 1);
    CHECK(st.annotationScales[0].name == "1:50");
    CHECK(st.viewportZoomScales[0].name == "1:50");
  }

  SECTION("point numbering rewinds with the undo of point creation") {
    st.createPointsNextId = 40;
    REQUIRE(TryPlaceSurveyPoint(st, 1.0, 2.0, 0.0, log));
    REQUIRE(st.surveyPoints.size() == 1);
    CHECK(st.createPointsNextId == 41);
    REQUIRE(DoUndo(st, log));
    CHECK(st.surveyPoints.empty());
    CHECK(st.createPointsNextId == 40);
  }
}

TEST_CASE("A tab switch does not carry phase 1b drawing fields into another drawing",
          "[issue1557][drawing-content]") {
  AppCommandState st;
  st.documents.resize(3);
  st.activeDrawingIdx = 1;
  CadLayerRow walls;
  walls.name = "Walls";
  st.drawingLayerTable.push_back(walls);
  TextStyle notes;
  notes.name = "Notes";
  notes.heightInches = 0.2f;
  st.textStyles.push_back(notes);
  AlignmentStyle design;
  design.name = "Design";
  st.alignmentStyles.push_back(design);

  st.currentLayer = "Walls";
  st.currentColor = "Red";
  st.alignmentStyleCurrent = "Design";
  SetActiveTextStyle(st, "Notes");
  alignment::StationFormat fmt;
  fmt.delimiter = 'K';
  st.alignmentStationFormat = fmt;
  st.pointVisibility.idRanges = "1-500";
  PageSetup setup;
  setup.name = "Plot A";
  st.savedPageSetups.push_back(setup);
  NamedUcs site;
  site.name = "Site";
  st.ucsNamed.push_back(site);
  NamedView overview;
  overview.name = "Overview";
  st.namedViews.push_back(overview);
  CadAnnotationScale scale;
  scale.name = "1:50";
  st.annotationScales.push_back(scale);
  st.viewportZoomScales.push_back(scale);
  st.createPointsOpts.startNumber = 40;
  st.createPointsNextId = 42;

  SaveDocumentToSnapshot(st, 1);
  RestoreDocumentFromSnapshot(st, 2);
  st.activeDrawingIdx = 2;
  CHECK(st.currentLayer == "0");
  CHECK(st.currentColor == "ByLayer");
  CHECK(st.alignmentStyleCurrent == "Standard");
  CHECK(st.activeTextStyleName == "Standard");
  CHECK(st.defaultPlottedTextHeightInches == 0.125f);
  CHECK(st.alignmentStationFormat.delimiter == '+');
  CHECK(st.pointVisibility.IsDefault());
  CHECK(st.savedPageSetups.empty());
  CHECK(st.ucsNamed.empty());
  CHECK(st.namedViews.empty());
  CHECK(st.annotationScales.empty());
  CHECK(st.viewportZoomScales.empty());
  CHECK(st.createPointsNextId == 1);
  CHECK(st.createPointsOpts.startNumber == 1);

  SaveDocumentToSnapshot(st, 2);
  RestoreDocumentFromSnapshot(st, 1);
  st.activeDrawingIdx = 1;
  CHECK(st.currentLayer == "Walls");
  CHECK(st.currentColor == "Red");
  CHECK(st.alignmentStyleCurrent == "Design");
  CHECK(st.activeTextStyleName == "Notes");
  CHECK(st.defaultPlottedTextHeightInches == 0.2f);
  CHECK(st.alignmentStationFormat.delimiter == 'K');
  CHECK(st.pointVisibility.idRanges == "1-500");
  REQUIRE(st.savedPageSetups.size() == 1);
  CHECK(st.savedPageSetups[0].name == "Plot A");
  REQUIRE(st.ucsNamed.size() == 1);
  REQUIRE(st.namedViews.size() == 1);
  REQUIRE(st.annotationScales.size() == 1);
  REQUIRE(st.viewportZoomScales.size() == 1);
  CHECK(st.createPointsNextId == 42);
  CHECK(st.createPointsOpts.startNumber == 40);
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
