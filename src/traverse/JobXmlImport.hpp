#pragma once

#include "TraverseCalc.hpp"

#include <string>

/// Parses a Trimble JobXML (.jxl / .xml) file and populates \p td (GitHub
/// issue #846 Phase B; generalized for real Trimble Access field exports —
/// GitHub issue #848 follow-up). Grounded in Trimble's own public schema
/// (JobXMLSchema-5.72.xsd, ww2.trimble.com/schema/JobXML/5_7/) — not guessed.
/// XML parsing uses pugixml (third_party/pugixml, see VENDORED.md).
///
/// JobXML's own <Reductions> section already carries Trimble Access's final
/// computed grid coordinates for every point — this importer only reads that
/// section to resolve known-point coordinates for the start/backsight
/// reference, and to tell a repeat shot of an already-known point apart from
/// a plain detail point (see SIDE/CHECK below) — not to skip reducing the
/// traverse itself. The traverse geometry (turned angle, distance) always
/// comes from <FieldBook>'s raw records:
///
///   <StationRecord ID="..."><StationName>...</StationName>...</StationRecord>
///       Occupies a station; ID is referenced by later PointRecords' StationID.
///       The SEQUENCE of StationRecords (the order the crew actually set up,
///       not shot order) is the traverse's backbone — see STN below.
///   <BackBearingRecord ID="..."><Station>/<BackSight>/<Face1HorizontalCircle>...
///       The backsight context for observations at that station: Station is
///       the occupied point's name, BackSight the sighted point's name,
///       Face1HorizontalCircle the circle reading the turned angle is measured
///       from. ID is referenced by later PointRecords' BackBearingID.
///   <PointRecord ID="..."><Name>/<StationID>/<BackBearingID>/<Circle|MTA>...
///       One terrestrial observation, when StationID, BackBearingID, and
///       either a plain Circle (HorizontalCircle/VerticalCircle/EDMDistance,
///       one face) or an MTA element (same three fields, already averaged
///       over every round/face a "Start Rounds" sequence recorded) are
///       present, and no GPS/ECEF/WGS84/resection/offset/other
///       non-terrestrial element is. Classification="BackSight" (or a Name
///       equal to the setup's own backsight) marks a repeat check of the
///       known backsight direction — read for nothing, not even a side shot.
///       Every other qualifying PointRecord is a sight to a point: turned
///       angle = HorizontalCircle − the BackBearingRecord's
///       Face1HorizontalCircle, vertical = VerticalCircle (zenith — raw
///       total-station circle convention, same as RW5's ZE/StarNet's
///       default), distance = EDMDistance (slope). Several PointRecords to
///       the same Name from the same setup (Face1 + Face2, one or more
///       rounds, and/or a final MTA summary) are ONE sight, reduced to a
///       single observation: the MTA reading when the controller produced
///       one (preferred — it's Trimble's own average), otherwise a Face1/
///       Face2 average (the same reduction FbkImport uses for a raw .fbk
///       F1/F2 pair).
///
///   STN (leg vs. side/check shot): a sight is a traverse LEG exactly when it
///       goes from one StationRecord's station to the VERY NEXT
///       StationRecord's station (the setup sequence is ground truth for the
///       chain — not Classification, which Trimble applies inconsistently
///       across a sight's own Face1/Face2/MTA siblings), or from the LAST
///       StationRecord's station back to the very first one (closing a loop
///       — the crew doesn't re-occupy the start monument, just shoots it from
///       the final setup). Every other qualifying sight is a side shot — a
///       CHECK shot (with known coordinates to compare against) when its Name
///       matches a <Reductions><Point>, otherwise a plain SIDE shot. A Name
///       re-shot from more than one setup gets a ".N" suffix for the 2nd/3rd/
///       ... occurrence (same convention FbkImport uses for a repeated
///       closing monument name) since GSTC requires a unique point number per
///       STN declaration.
///
/// All distances in a JobXML file are always in metres and all angles always
/// in decimal degrees (per the schema's own documented units section,
/// regardless of what the job's <AngleUnits>/<DistanceUnits> UI-display
/// preference says) — this importer converts every distance to feet
/// (GoSurvey's working convention, matching FbkImport/StarNetImport/
/// Rw5Import) using the exact international foot (1 m = 1/0.3048 ft); angles
/// need no conversion.
///
/// A PointRecord with any GPS/ECEF/WGS84/resection/offset/other element
/// present, a leg the StationRecord sequence expects but no matching sight
/// was recorded for, a leg whose backsight doesn't match the chain, or any
/// record this importer doesn't recognize, is a defined gap: reported as an
/// import failure naming the record and line, not silently dropped or
/// guessed at. \p td is left unchanged on failure.
bool JobXmlImport(const char* path, TraverseData& td, std::string& errorMsg);
