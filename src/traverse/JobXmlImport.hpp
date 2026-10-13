#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parses a Trimble JobXML (.jxl / .xml) file and populates \p td (GitHub
/// issue #846, Phase B). Grounded in Trimble's own public schema
/// (JobXMLSchema-5.72.xsd, ww2.trimble.com/schema/JobXML/5_7/) — not guessed.
/// XML parsing uses pugixml (third_party/pugixml, see VENDORED.md).
///
/// JobXML's own <Reductions> section already carries Trimble Access's final
/// computed grid coordinates for every point — this importer only reads that
/// section to resolve known-point coordinates for the start/backsight
/// reference (the same role StarNet's C lines and RW5's SP/OC coordinates
/// play), not to skip reducing the traverse itself. The traverse geometry
/// (turned angle, distance) comes from <FieldBook>'s raw records, matching
/// the StarNet/RW5 importers' own "raw format" scope:
///
///   <StationRecord ID="..."><StationName>...</StationName>...</StationRecord>
///       Occupies a station; ID is referenced by later PointRecords' StationID.
///   <BackBearingRecord ID="..."><Station>/<BackSight>/<Face1HorizontalCircle>...
///       The backsight context for observations at that station: Station is
///       the occupied point's name, BackSight the sighted point's name,
///       Face1HorizontalCircle the circle reading the turned angle is measured
///       from. ID is referenced by later PointRecords' BackBearingID.
///   <PointRecord ID="..."><Name>/<StationID>/<BackBearingID>/<Circle>...
///       One traverse leg, when StationID, BackBearingID, and a plain Circle
///       element (HorizontalCircle/VerticalCircle/EDMDistance) are all
///       present and no GPS/ECEF/WGS84/resection/other non-terrestrial
///       element is: AT = the StationRecord's name, TO = this record's own
///       Name, turned angle = HorizontalCircle − the BackBearingRecord's
///       Face1HorizontalCircle (the same backsight-subtraction FbkImport
///       already does), vertical = VerticalCircle (zenith — raw total-station
///       circle convention, same as RW5's ZE/StarNet's default), distance =
///       EDMDistance (slope).
///
/// All distances in a JobXML file are always in metres and all angles always
/// in decimal degrees (per the schema's own documented units section) — this
/// importer converts every distance to feet (GoSurvey's working convention,
/// matching FbkImport/StarNetImport/Rw5Import) using the exact international
/// foot (1 m = 1/0.3048 ft); angles need no conversion.
///
/// A PointRecord with any GPS/ECEF/WGS84/MTA/resection/offset/other element
/// present, a second independent traverse (more than one "first" station), or
/// any record this importer doesn't recognize, is a defined gap: reported as
/// an import failure naming the record and line, not silently dropped or
/// guessed at. \p td is left unchanged on failure.
bool JobXmlImport(const char* path, TraverseData& td, std::string& errorMsg);
