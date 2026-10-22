#include "TraverseCommandLang.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace travcmd {
namespace {

std::string Fmt(double v, int prec = 6) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", prec, v);
    return buf;
}

std::string ToUpper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Tokenize(const std::string& line) {
    std::vector<std::string> toks;
    std::istringstream ss(line);
    std::string t;
    while (ss >> t) toks.push_back(t);
    return toks;
}

bool ParseIntTok(const std::string& s, int* out) {
    char* ep = nullptr;
    const long v = std::strtol(s.c_str(), &ep, 10);
    if (ep == s.c_str()) return false;
    *out = static_cast<int>(v);
    return true;
}

bool ParseDoubleTok(const std::string& s, double* out) {
    char* ep = nullptr;
    const double v = std::strtod(s.c_str(), &ep);
    if (ep == s.c_str()) return false;
    *out = v;
    return true;
}

}  // namespace

static void AppendCode(std::ostream& out, const std::string& code) {
    if (!code.empty()) out << " '" << code;
}

static bool UsesSourceOrder(const TraverseData& td) {
    if (!td.sourceComments.empty() || !td.rawSets.empty()) return true;
    for (const TraverseLeg& leg : td.legs)
        if (leg.sourceOrder != 0) return true;
    for (const TraverseSideShot& shot : td.sideShots)
        if (shot.sourceOrder != 0) return true;
    for (const NetworkStation& station : td.networkStations)
        if (station.sourceOrder != 0) return true;
    for (const NetworkAngleObs& obs : td.networkAngles)
        if (obs.sourceOrder != 0) return true;
    for (const NetworkDistObs& obs : td.networkDists)
        if (obs.sourceOrder != 0) return true;
    for (const NetworkAzimuthObs& obs : td.networkAzimuths)
        if (obs.sourceOrder != 0) return true;
    for (const NetworkElevDiffObs& obs : td.networkElevDiffs)
        if (obs.sourceOrder != 0) return true;
    for (const NetworkZenithObs& obs : td.networkZeniths)
        if (obs.sourceOrder != 0) return true;
    for (const NetworkDirectionObs& obs : td.networkDirections)
        if (obs.sourceOrder != 0) return true;
    return false;
}

std::string AlignColumns(const std::string& text) {
    struct Row {
        bool passthrough = false;
        std::string raw;
        std::string keyword;
        std::vector<std::string> fields;
        std::string tail;
    };
    std::vector<Row> rows;
    std::unordered_map<std::string, std::vector<size_t>> widths;
    if (text.empty()) return text;
    size_t pos = 0;
    const bool endsWithNewline = text.back() == '\n';
    const size_t limit = endsWithNewline ? text.size() - 1 : text.size();
    while (pos <= limit) {
        const size_t end = text.find('\n', pos);
        const size_t stop = end == std::string::npos ? text.size() : end;
        std::string raw = text.substr(pos, stop - pos);
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();
        Row row;
        row.raw = raw;
        const size_t first = raw.find_first_not_of(" \t");
        const bool comment = first != std::string::npos && first + 1 < raw.size() && raw[first] == '/' &&
                             raw[first + 1] == '/';
        const bool importMarker = first != std::string::npos && raw.compare(first, 7, "#IMPORT") == 0;
        if (first == std::string::npos || comment || importMarker) {
            row.passthrough = true;
        } else {
            const std::vector<std::string> toks = Tokenize(raw);
            if (toks.empty()) {
                row.passthrough = true;
            } else {
                row.keyword = ToUpper(toks[0]);
                if (row.keyword == "DESC" && toks.size() >= 3) {
                    row.fields.push_back(toks[1]);
                    row.fields.push_back(toks[2]);
                    size_t at = 0;
                    for (int skip = 0; skip < 3 && at < raw.size(); ++skip) {
                        while (at < raw.size() && (raw[at] == ' ' || raw[at] == '\t')) ++at;
                        while (at < raw.size() && raw[at] != ' ' && raw[at] != '\t') ++at;
                    }
                    while (at < raw.size() && (raw[at] == ' ' || raw[at] == '\t')) ++at;
                    if (at < raw.size()) row.tail = raw.substr(at);
                } else {
                    size_t quote = raw.find('\'');
                    const size_t tick = raw.find('`');
                    if (tick != std::string::npos && (quote == std::string::npos || tick < quote)) quote = tick;
                    if (row.keyword == "RAWSET") quote = std::string::npos;
                    const std::string head = quote == std::string::npos ? raw : raw.substr(0, quote);
                    const std::vector<std::string> headToks = quote == std::string::npos ? toks : Tokenize(head);
                    for (size_t i = 1; i < headToks.size(); ++i) row.fields.push_back(headToks[i]);
                    if (quote != std::string::npos) row.tail = raw.substr(quote);
                }
                std::vector<size_t>& width = widths[row.keyword];
                if (width.size() < row.fields.size()) width.resize(row.fields.size(), 0);
                for (size_t i = 0; i < row.fields.size(); ++i)
                    if (row.fields[i].size() > width[i]) width[i] = row.fields[i].size();
            }
        }
        rows.push_back(std::move(row));
        if (end == std::string::npos || end >= limit) break;
        pos = end + 1;
    }
    // Commands that name the same stations share one set of columns, counted
    // from the right: a DIR's angle, a DIST's distance, and a ZENITH's angle
    // land together, and DIR's extra set number does not shift them. A control
    // point's coordinates stay in their own columns so they don't open a gap
    // on every shot line.
    auto columnGroup = [](const std::string& keyword) -> int {
        if (keyword == "DIR" || keyword == "DIST" || keyword == "ZENITH") return 1;
        if (keyword == "CTRL") return 2;
        if (keyword == "AZIMUTH" || keyword == "ANGLE" || keyword == "EDIF") return 3;
        return 0;
    };
    size_t keywordWidth = 0;
    std::array<size_t, 4> groupFields{};
    for (const Row& row : rows) {
        if (row.passthrough || row.keyword.empty()) continue;
        keywordWidth = std::max(keywordWidth, row.keyword.size());
        const int group = columnGroup(row.keyword);
        if (group != 0) groupFields[static_cast<size_t>(group)] =
            std::max(groupFields[static_cast<size_t>(group)], row.fields.size());
    }
    std::array<std::vector<size_t>, 4> groupWidth;
    for (size_t g = 1; g < groupFields.size(); ++g) groupWidth[g].assign(groupFields[g], 0);
    for (const Row& row : rows) {
        const int group = row.passthrough ? 0 : columnGroup(row.keyword);
        if (group == 0 || row.fields.empty()) continue;
        const size_t count = groupFields[static_cast<size_t>(group)];
        const size_t start = count - row.fields.size();
        for (size_t i = 0; i < row.fields.size(); ++i)
            groupWidth[static_cast<size_t>(group)][start + i] =
                std::max(groupWidth[static_cast<size_t>(group)][start + i], row.fields[i].size());
    }

    std::string out;
    for (size_t r = 0; r < rows.size(); ++r) {
        const Row& row = rows[r];
        if (r > 0) out.push_back('\n');
        if (row.passthrough || row.keyword.empty()) {
            out += row.raw;
            continue;
        }
        out += row.keyword;
        if (keywordWidth > row.keyword.size()) out.append(keywordWidth - row.keyword.size(), ' ');
        const int group = columnGroup(row.keyword);
        const std::vector<size_t>& sharedWidth = group == 0 ? widths[row.keyword] : groupWidth[static_cast<size_t>(group)];
        if (group != 0 && !sharedWidth.empty()) {
            const size_t start = sharedWidth.size() - row.fields.size();
            for (size_t c = 0; c < sharedWidth.size(); ++c) {
                out += "  ";
                if (c < start) {
                    out.append(sharedWidth[c], ' ');
                } else {
                    const std::string& field = row.fields[c - start];
                    if (sharedWidth[c] > field.size()) out.append(sharedWidth[c] - field.size(), ' ');
                    out += field;
                }
            }
        } else {
            const std::vector<size_t>& width = widths[row.keyword];
            for (size_t i = 0; i < row.fields.size(); ++i) {
                out += "  ";
                const size_t col = i < width.size() ? width[i] : row.fields[i].size();
                if (col > row.fields[i].size()) out.append(col - row.fields[i].size(), ' ');
                out += row.fields[i];
            }
        }
        if (!row.tail.empty()) {
            out += "  ";
            out += row.tail;
        }
    }
    if (endsWithNewline) out.push_back('\n');
    return out;
}

static void AppendOrderedBody(std::ostringstream& out, const TraverseData& td) {
    struct Piece {
        int order = 0;
        int sub = 0;
        int seq = 0;
        std::string text;
    };
    std::vector<Piece> pieces;
    int seq = 0;
    constexpr int kTail = 1000000000;
    auto add = [&](int order, int sub, std::string text) {
        if (order <= 0) order = kTail;
        pieces.push_back(Piece{order, sub, seq++, std::move(text)});
    };
    for (const TraverseData::SourceComment& note : td.sourceComments) {
        if (note.blankLine) add(note.beforeOrder, -1, "\n");
        else add(note.beforeOrder, -1, "// " + note.text + "\n");
    }

    auto writeFix = [](const NetworkComponent& component) {
        if (component.fix == NetworkFix::Fixed) return std::string("!");
        if (component.fix == NetworkFix::Free) return std::string("*");
        return Fmt(component.stdError, 4);
    };

    for (size_t i = 0; i < td.legs.size(); ++i) {
        const TraverseLeg& leg = td.legs[i];
        const int at = (i == 0) ? td.startStationId : td.legs[i - 1].stationId;
        const int to = leg.stationId;
        std::ostringstream block;
        block << "\nSTN " << at << " " << to << "\n";
        if (leg.hasHorizAngle) {
            const std::string from = (i == 0) ? "REF"
                                               : std::to_string((i == 1) ? td.startStationId
                                                                          : td.legs[i - 2].stationId);
            block << "HANG " << from << " " << at << " " << to << " " << Fmt(leg.horizAngleDeg);
            if (!leg.hasHorizDist && !leg.hasSlopeDist) AppendCode(block, leg.code);
            block << "\n";
        }
        if (leg.hasHorizDist) {
            block << "HDIST " << at << " " << to << " " << Fmt(leg.horizDist);
            AppendCode(block, leg.code);
            block << "\n";
        }
        if (leg.hasSlopeDist) {
            block << "SDIST " << at << " " << to << " " << Fmt(leg.slopeDist);
            if (!leg.hasHorizDist) AppendCode(block, leg.code);
            block << "\n";
        }
        if (leg.hasVertAngle)
            block << "VANG " << at << " " << to << " " << Fmt(leg.vertAngleDeg)
                  << " " << (leg.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!leg.description.empty())
            block << "DESC " << at << " " << to << " " << leg.description << "\n";
        if (leg.hasPrismConstant || leg.hasPrismError)
            block << "PRISM " << at << " " << to
                  << " " << Fmt(leg.hasPrismConstant ? leg.prismConstant : 0.0, 4)
                  << " " << Fmt(leg.hasPrismError ? leg.prismError : 0.0, 4) << "\n";
        if (leg.hasPoleError)
            block << "POLE " << at << " " << to << " " << Fmt(leg.poleError, 4) << "\n";
        if (leg.hasTsWeights)
            block << "TSERR " << at << " " << to
                  << " " << Fmt(leg.tsWeights.sigmaAngleSec, 3)
                  << " " << Fmt(leg.tsWeights.sigmaDistConstFt, 4)
                  << " " << Fmt(leg.tsWeights.sigmaDistPpm, 3) << "\n";
        add(leg.sourceOrder, 0, block.str());
    }

    for (const TraverseSideShot& ss : td.sideShots) {
        std::ostringstream block;
        block << "\nSTN " << ss.atStationId << " " << ss.stationId << " "
              << (ss.isCheckShot ? "CHECK" : "SIDE");
        if (ss.isCheckShot && ss.hasKnownCoords)
            block << " " << Fmt(ss.knownEasting) << " " << Fmt(ss.knownNorthing);
        block << "\n";
        if (ss.hasHorizAngle) {
            block << "HANG REF " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizAngleDeg);
            if (!ss.hasHorizDist && !ss.hasSlopeDist) AppendCode(block, ss.code);
            block << "\n";
        }
        if (ss.hasHorizDist) {
            block << "HDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizDist);
            AppendCode(block, ss.code);
            block << "\n";
        }
        if (ss.hasSlopeDist) {
            block << "SDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.slopeDist);
            if (!ss.hasHorizDist) AppendCode(block, ss.code);
            block << "\n";
        }
        if (ss.hasVertAngle)
            block << "VANG " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.vertAngleDeg)
                  << " " << (ss.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!ss.description.empty())
            block << "DESC " << ss.atStationId << " " << ss.stationId << " " << ss.description << "\n";
        add(ss.sourceOrder, 1, block.str());
    }

    for (const NetworkStation& station : td.networkStations) {
        std::ostringstream line;
        line << "CTRL " << station.id << " " << Fmt(station.easting.value) << " "
             << Fmt(station.northing.value);
        if (station.elevation.present) {
            line << " " << Fmt(station.elevation.value) << " " << writeFix(station.easting) << " "
                 << writeFix(station.northing) << " " << writeFix(station.elevation);
        } else if (station.easting.fix != NetworkFix::Fixed || station.northing.fix != NetworkFix::Fixed) {
            line << " " << writeFix(station.easting) << " " << writeFix(station.northing);
        }
        AppendCode(line, station.code);
        line << "\n";
        add(station.sourceOrder, 2, line.str());
    }
    for (const NetworkAzimuthObs& azimuth : td.networkAzimuths) {
        std::ostringstream line;
        line << "AZIMUTH " << azimuth.fromId << " " << azimuth.toId << " " << Fmt(azimuth.azimuthDeg);
        if (azimuth.stdErrorSec > 0.0) line << " " << Fmt(azimuth.stdErrorSec, 3);
        AppendCode(line, azimuth.code);
        line << "\n";
        add(azimuth.sourceOrder, 3, line.str());
    }
    for (const NetworkAngleObs& angle : td.networkAngles) {
        std::ostringstream line;
        line << "ANGLE " << angle.atId << " " << angle.fromId << " " << angle.toId << " "
             << Fmt(angle.angleDeg);
        if (angle.stdErrorSec > 0.0) line << " " << Fmt(angle.stdErrorSec, 3);
        AppendCode(line, angle.code);
        line << "\n";
        add(angle.sourceOrder, 4, line.str());
    }
    for (const NetworkDirectionObs& dir : td.networkDirections) {
        std::ostringstream line;
        line << "DIR " << dir.setId << " " << dir.atId << " " << dir.toId << " " << Fmt(dir.directionDeg);
        if (dir.stdErrorSec > 0.0) line << " " << Fmt(dir.stdErrorSec, 3);
        AppendCode(line, dir.code);
        line << "\n";
        add(dir.sourceOrder, 5, line.str());
    }
    for (const NetworkDistObs& dist : td.networkDists) {
        std::ostringstream line;
        line << "DIST " << dist.fromId << " " << dist.toId << " " << Fmt(dist.distance);
        if (dist.stdError > 0.0) line << " " << Fmt(dist.stdError, 4);
        AppendCode(line, dist.code);
        line << "\n";
        add(dist.sourceOrder, 6, line.str());
    }
    for (const NetworkZenithObs& zenith : td.networkZeniths) {
        std::ostringstream line;
        line << "ZENITH " << zenith.fromId << " " << zenith.toId << " " << Fmt(zenith.zenithDeg);
        if (zenith.stdErrorSec > 0.0) line << " " << Fmt(zenith.stdErrorSec, 3);
        AppendCode(line, zenith.code);
        line << "\n";
        add(zenith.sourceOrder, 7, line.str());
    }
    for (const NetworkElevDiffObs& diff : td.networkElevDiffs) {
        std::ostringstream line;
        line << "EDIF " << diff.fromId << " " << diff.toId << " " << Fmt(diff.deltaZ);
        if (diff.stdError > 0.0) line << " " << Fmt(diff.stdError, 4);
        AppendCode(line, diff.code);
        line << "\n";
        add(diff.sourceOrder, 8, line.str());
    }

    for (const TraverseRawSet& raw : td.rawSets) {
        std::ostringstream line;
        line << std::setprecision(std::numeric_limits<double>::max_digits10);
        line << "RAWSET " << raw.atStationId << " " << raw.toStationId << " " << raw.measurements.setNo;
        const TraverseMeasSet& set = raw.measurements;
        if (set.hasF1) line << " F1 " << set.f1HzDec << " " << set.f1VaDec << " " << set.f1Sd;
        if (set.hasF2) line << " F2 " << set.f2HzDec << " " << set.f2VaDec << " " << set.f2Sd;
        line << " SOURCE " << raw.sourceId << "\n";
        add(raw.sourceOrder, 9, line.str());
    }

    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.sub != b.sub) return a.sub < b.sub;
        return a.seq < b.seq;
    });
    for (const Piece& piece : pieces) out << piece.text;
}

static std::string WithGraphMetadata(const std::string& text, const TraverseData& td);

std::string Serialize(const TraverseData& td, const LsaWeights& weights) {
    std::ostringstream out;
    out << "// GoSurvey Traverse Command (GSTC) \xe2\x80\x94 edit and re-apply.\n";
    for (const std::string& option : td.importerSkippedInlineOptions)
        out << "// Inline option from STAR*NET (not applied): " << option << "\n";

    for (const TraverseRawSource& source : td.rawSources)
        out << "// @GSTC-SOURCE " << source.id << " " << nlohmann::json(source.path).dump() << "\n";
    out << "STATION " << td.startStationId
        << " " << Fmt(td.startEasting)
        << " " << Fmt(td.startNorthing)
        << " " << Fmt(td.startElevation) << "\n";
    if (td.hasStartBearing)
        out << "BEARING " << Fmt(td.startBearingDeg) << "\n";
    if (td.hasEndCoordinates)
        out << "ENDPOINT " << Fmt(td.endEasting) << " " << Fmt(td.endNorthing)
            << " " << Fmt(td.endElevation) << "\n";

    out << "#LOOP " << (td.isClosedLoop ? "ON" : "OFF") << "\n";
    out << "#SCALE " << (td.applyScaleFactor ? "ON" : "OFF")
        << " " << (td.scaleFactorKind == TraverseData::ScaleFactorKind::GridOnly ? "GRID" : "COMBINED")
        << "\n";
    out << "#ERRORS " << Fmt(weights.sigmaAngleSec, 3)
        << " " << Fmt(weights.sigmaDistConstFt, 4)
        << " " << Fmt(weights.sigmaDistPpm, 3)
        << "\n";

    if (UsesSourceOrder(td)) {
        AppendOrderedBody(out, td);
        return WithGraphMetadata(AlignColumns(out.str()), td);
    }

    for (size_t i = 0; i < td.legs.size(); ++i) {
        const TraverseLeg& leg = td.legs[i];
        const int at = (i == 0) ? td.startStationId : td.legs[i - 1].stationId;
        const int to = leg.stationId;

        out << "\nSTN " << at << " " << to << "\n";

        if (leg.hasHorizAngle) {
            const std::string from = (i == 0) ? "REF"
                                               : std::to_string((i == 1) ? td.startStationId
                                                                          : td.legs[i - 2].stationId);
            out << "HANG " << from << " " << at << " " << to << " " << Fmt(leg.horizAngleDeg);
            if (!leg.hasHorizDist && !leg.hasSlopeDist) AppendCode(out, leg.code);
            out << "\n";
        }
        if (leg.hasHorizDist) {
            out << "HDIST " << at << " " << to << " " << Fmt(leg.horizDist);
            AppendCode(out, leg.code);
            out << "\n";
        }
        if (leg.hasSlopeDist) {
            out << "SDIST " << at << " " << to << " " << Fmt(leg.slopeDist);
            if (!leg.hasHorizDist) AppendCode(out, leg.code);
            out << "\n";
        }
        if (leg.hasVertAngle)
            out << "VANG " << at << " " << to << " " << Fmt(leg.vertAngleDeg)
                << " " << (leg.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!leg.description.empty())
            out << "DESC " << at << " " << to << " " << leg.description << "\n";
        if (leg.hasPrismConstant || leg.hasPrismError)
            out << "PRISM " << at << " " << to
                << " " << Fmt(leg.hasPrismConstant ? leg.prismConstant : 0.0, 4)
                << " " << Fmt(leg.hasPrismError ? leg.prismError : 0.0, 4) << "\n";
        if (leg.hasPoleError)
            out << "POLE " << at << " " << to << " " << Fmt(leg.poleError, 4) << "\n";
        if (leg.hasTsWeights)
            out << "TSERR " << at << " " << to
                << " " << Fmt(leg.tsWeights.sigmaAngleSec, 3)
                << " " << Fmt(leg.tsWeights.sigmaDistConstFt, 4)
                << " " << Fmt(leg.tsWeights.sigmaDistPpm, 3) << "\n";
    }

    for (const TraverseSideShot& ss : td.sideShots) {
        out << "\nSTN " << ss.atStationId << " " << ss.stationId << " "
            << (ss.isCheckShot ? "CHECK" : "SIDE");
        if (ss.isCheckShot && ss.hasKnownCoords)
            out << " " << Fmt(ss.knownEasting) << " " << Fmt(ss.knownNorthing);
        out << "\n";

        if (ss.hasHorizAngle) {
            out << "HANG REF " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizAngleDeg);
            if (!ss.hasHorizDist && !ss.hasSlopeDist) AppendCode(out, ss.code);
            out << "\n";
        }
        if (ss.hasHorizDist) {
            out << "HDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.horizDist);
            AppendCode(out, ss.code);
            out << "\n";
        }
        if (ss.hasSlopeDist) {
            out << "SDIST " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.slopeDist);
            if (!ss.hasHorizDist) AppendCode(out, ss.code);
            out << "\n";
        }
        if (ss.hasVertAngle)
            out << "VANG " << ss.atStationId << " " << ss.stationId << " " << Fmt(ss.vertAngleDeg)
                << " " << (ss.isZenithAngle ? "ZENITH" : "ELEV") << "\n";
        if (!ss.description.empty())
            out << "DESC " << ss.atStationId << " " << ss.stationId << " " << ss.description << "\n";
    }

    auto writeFix = [](const NetworkComponent& component) {
        if (component.fix == NetworkFix::Fixed) return std::string("!");
        if (component.fix == NetworkFix::Free) return std::string("*");
        return Fmt(component.stdError, 4);
    };
    for (const NetworkStation& station : td.networkStations) {
        out << "CTRL " << station.id << " " << Fmt(station.easting.value) << " "
            << Fmt(station.northing.value);
        if (station.elevation.present) {
            out << " " << Fmt(station.elevation.value) << " " << writeFix(station.easting) << " "
                << writeFix(station.northing) << " " << writeFix(station.elevation);
        } else if (station.easting.fix != NetworkFix::Fixed || station.northing.fix != NetworkFix::Fixed) {
            out << " " << writeFix(station.easting) << " " << writeFix(station.northing);
        }
        AppendCode(out, station.code);
        out << "\n";
    }
    for (const NetworkAngleObs& angle : td.networkAngles) {
        out << "ANGLE " << angle.atId << " " << angle.fromId << " " << angle.toId << " "
            << Fmt(angle.angleDeg);
        if (angle.stdErrorSec > 0.0) out << " " << Fmt(angle.stdErrorSec, 3);
        AppendCode(out, angle.code);
        out << "\n";
    }
    for (const NetworkDistObs& dist : td.networkDists) {
        out << "DIST " << dist.fromId << " " << dist.toId << " " << Fmt(dist.distance);
        if (dist.stdError > 0.0) out << " " << Fmt(dist.stdError, 4);
        AppendCode(out, dist.code);
        out << "\n";
    }
    for (const NetworkAzimuthObs& azimuth : td.networkAzimuths) {
        out << "AZIMUTH " << azimuth.fromId << " " << azimuth.toId << " " << Fmt(azimuth.azimuthDeg);
        if (azimuth.stdErrorSec > 0.0) out << " " << Fmt(azimuth.stdErrorSec, 3);
        AppendCode(out, azimuth.code);
        out << "\n";
    }
    for (const NetworkElevDiffObs& diff : td.networkElevDiffs) {
        out << "EDIF " << diff.fromId << " " << diff.toId << " " << Fmt(diff.deltaZ);
        if (diff.stdError > 0.0) out << " " << Fmt(diff.stdError, 4);
        AppendCode(out, diff.code);
        out << "\n";
    }
    for (const NetworkZenithObs& zenith : td.networkZeniths) {
        out << "ZENITH " << zenith.fromId << " " << zenith.toId << " " << Fmt(zenith.zenithDeg);
        if (zenith.stdErrorSec > 0.0) out << " " << Fmt(zenith.stdErrorSec, 3);
        AppendCode(out, zenith.code);
        out << "\n";
    }
    for (const NetworkDirectionObs& dir : td.networkDirections) {
        out << "DIR " << dir.setId << " " << dir.atId << " " << dir.toId << " " << Fmt(dir.directionDeg);
        if (dir.stdErrorSec > 0.0) out << " " << Fmt(dir.stdErrorSec, 3);
        AppendCode(out, dir.code);
        out << "\n";
    }
    return WithGraphMetadata(AlignColumns(out.str()), td);
}

bool Parse(const std::string& text, TraverseData& td, LsaWeights& weights, std::string& errorMsg) {
    TraverseData newTd;
    bool hasGraphMetadata = false;
    newTd.hasStartBearing = false;
    LsaWeights newWeights;
    bool sawStation = false;

    std::vector<int> rawSetLines;
    std::vector<TraverseLeg> newLegs;
    std::unordered_map<int, size_t> toIndex;  // TO station -> index in newLegs
    std::vector<TraverseSideShot> newSideShots;
    int nextSourceOrder = 1;
    std::vector<TraverseData::SourceComment> pendingNotes;
    auto flushNotes = [&](int before) {
        for (TraverseData::SourceComment note : pendingNotes) {
            note.beforeOrder = before;
            newTd.sourceComments.push_back(note);
        }
        pendingNotes.clear();
    };
    auto assignOrder = [&]() {
        const int order = nextSourceOrder++;
        flushNotes(order);
        return order;
    };
    std::unordered_map<int, size_t> sideIndex;  // TO (the shot's own point) -> index in newSideShots

    // The station occupied for the leg at newLegs[idx] — the previous leg's TO,
    // or the start station for the first leg.
    auto expectedAt = [&](size_t idx) -> int {
        return (idx == 0) ? newTd.startStationId : newLegs[idx - 1].stationId;
    };

    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();

        const size_t firstNonSpace = line.find_first_not_of(" \t");
        if (firstNonSpace == std::string::npos) {
            TraverseData::SourceComment blank;
            blank.blankLine = true;
            pendingNotes.push_back(blank);
            continue;
        }
        if (firstNonSpace + 1 < line.size() && line[firstNonSpace] == '/' && line[firstNonSpace + 1] == '/') {
            const std::string metadata = line.substr(firstNonSpace + 2);
            std::istringstream sourceLine(metadata);
            std::string marker;
            sourceLine >> marker;
            if (marker == "@GSTC-NODE" || marker == "@GSTC-GROUP") {
                hasGraphMetadata = true;
                continue;
            }
            if (marker == "@GSTC-SOURCE") {
                TraverseRawSource source;
                sourceLine >> source.id;
                std::string path;
                std::getline(sourceLine, path);
                const auto value = nlohmann::json::parse(path, nullptr, false);
                if (source.id.empty() || !value.is_string() ||
                    std::any_of(newTd.rawSources.begin(), newTd.rawSources.end(),
                        [&](const TraverseRawSource& old) { return old.id == source.id; })) {
                    errorMsg = "line " + std::to_string(lineNo) + ": invalid or duplicate source metadata";
                    return false;
                }
                source.path = value.get<std::string>();
                newTd.rawSources.push_back(std::move(source));
                continue;
            }
            const std::string note = "Inline option from STAR*NET (not applied):";
            std::string body = line.substr(firstNonSpace + 2);
            const size_t noteAt = body.find(note);
            if (noteAt != std::string::npos) {
                std::string option = body.substr(noteAt + note.size());
                const size_t start = option.find_first_not_of(" \t");
                if (start != std::string::npos)
                    newTd.importerSkippedInlineOptions.push_back(option.substr(start));
                continue;
            }
            const size_t bodyStart = body.find_first_not_of(" \t");
            if (bodyStart != std::string::npos) body = body.substr(bodyStart);
            else body.clear();
            const size_t bodyEnd = body.find_last_not_of(" \t");
            if (bodyEnd != std::string::npos) body.resize(bodyEnd + 1);
            if (body.find("GoSurvey Traverse Command") == std::string::npos && !body.empty()) {
                TraverseData::SourceComment note;
                note.text = body;
                pendingNotes.push_back(note);
            }
            continue;
        }
        if (line[firstNonSpace] == '#') {
            line = line.substr(firstNonSpace + 1);
            const size_t optionStart = line.find_first_not_of(" \t");
            if (optionStart == std::string::npos) continue;
            line = line.substr(optionStart);
        }

        std::string lineCode;
        {
            const auto fields = Tokenize(line);
            const bool rawSet = !fields.empty() && ToUpper(fields[0]) == "RAWSET";
            size_t quote = line.find('\'');
            const size_t tick = line.find('`');
            if (tick != std::string::npos && (quote == std::string::npos || tick < quote)) quote = tick;
            if (!rawSet && quote != std::string::npos) {
                lineCode = line.substr(quote + 1);
                const size_t codeStart = lineCode.find_first_not_of(" \t");
                lineCode = codeStart == std::string::npos ? std::string() : lineCode.substr(codeStart);
                line.resize(quote);
                while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
            }
        }

        auto toks = Tokenize(line);
        if (toks.empty()) continue;
        const std::string directive = ToUpper(toks[0]);

        auto fail = [&](const std::string& msg) -> bool {
            errorMsg = "line " + std::to_string(lineNo) + ": " + msg;
            return false;
        };

        if (directive == "RAWSET") {
            TraverseRawSet raw;
            auto positiveId = [](const std::string& token, int& value) {
                char* end = nullptr;
                const long long parsed = std::strtoll(token.c_str(), &end, 10);
                if (end == token.c_str() || *end != '\0' || parsed <= 0 ||
                    parsed > std::numeric_limits<int>::max()) return false;
                value = static_cast<int>(parsed);
                return true;
            };
            if (toks.size() < 10 || !positiveId(toks[1], raw.atStationId) ||
                !positiveId(toks[2], raw.toStationId) || !positiveId(toks[3], raw.measurements.setNo))
                return fail("expected RAWSET <at> <to> <set-number> [F1 hz zenith slope] [F2 hz zenith slope] SOURCE <source-id>");
            size_t i = 4;
            auto face = [&](const char* name, bool& present, double& hz, double& zenith, double& slope) {
                if (i >= toks.size() || ToUpper(toks[i]) != name) return true;
                if (i + 3 >= toks.size()) return false;
                double* values[] = {&hz, &zenith, &slope};
                for (size_t j = 0; j < 3; ++j) {
                    char* end = nullptr;
                    const std::string& token = toks[i + j + 1];
                    *values[j] = std::strtod(token.c_str(), &end);
                    if (end == token.c_str() || *end != '\0' || !std::isfinite(*values[j])) return false;
                }
                present = true;
                i += 4;
                return true;
            };
            TraverseMeasSet& set = raw.measurements;
            if (!face("F1", set.hasF1, set.f1HzDec, set.f1VaDec, set.f1Sd) ||
                !face("F2", set.hasF2, set.f2HzDec, set.f2VaDec, set.f2Sd) ||
                (!set.hasF1 && !set.hasF2) || i + 2 != toks.size() || ToUpper(toks[i]) != "SOURCE")
                return fail("RAWSET requires complete finite F1/F2 tuples and SOURCE <source-id>");
            raw.sourceId = toks[i + 1];
            raw.sourceOrder = assignOrder();
            newTd.rawSets.push_back(std::move(raw));
            rawSetLines.push_back(lineNo);
        } else if (directive == "IMPORT") {
            continue;  // `#IMPORT "path"` — an editor note marking where an import's data begins.
        } else if (directive == "STATION") {
            int id = 0;
            double e, n, z;
            if (toks.size() < 5 || !ParseIntTok(toks[1], &id) || !ParseDoubleTok(toks[2], &e) ||
                !ParseDoubleTok(toks[3], &n) || !ParseDoubleTok(toks[4], &z))
                return fail("expected STATION <id> <e> <n> <z>");
            newTd.startStationId = id;
            newTd.startEasting = e;
            newTd.startNorthing = n;
            newTd.startElevation = z;
            sawStation = true;
        } else if (directive == "BEARING") {
            double deg;
            if (toks.size() < 2 || !ParseDoubleTok(toks[1], &deg))
                return fail("expected BEARING <deg>");
            newTd.startBearingDeg = deg;
            newTd.hasStartBearing = true;
            newTd.startBearingBuf = TraverseFormatBearing(deg);
        } else if (directive == "ENDPOINT") {
            double e, n, z;
            if (toks.size() < 4 || !ParseDoubleTok(toks[1], &e) || !ParseDoubleTok(toks[2], &n) ||
                !ParseDoubleTok(toks[3], &z))
                return fail("expected ENDPOINT <e> <n> <z>");
            newTd.endEasting = e;
            newTd.endNorthing = n;
            newTd.endElevation = z;
            newTd.hasEndCoordinates = true;
        } else if (directive == "LOOP") {
            if (toks.size() < 2) return fail("expected LOOP ON|OFF");
            newTd.isClosedLoop = (ToUpper(toks[1]) == "ON");
        } else if (directive == "SCALE") {
            if (toks.size() < 2) return fail("expected SCALE ON|OFF [COMBINED|GRID]");
            newTd.applyScaleFactor = (ToUpper(toks[1]) == "ON");
            const std::string kind = (toks.size() >= 3) ? ToUpper(toks[2]) : "COMBINED";
            newTd.scaleFactorKind = (kind == "GRID") ? TraverseData::ScaleFactorKind::GridOnly
                                                      : TraverseData::ScaleFactorKind::Combined;
        } else if (directive == "ERRORS") {
            double a, d, p;
            if (toks.size() < 4 || !ParseDoubleTok(toks[1], &a) || !ParseDoubleTok(toks[2], &d) ||
                !ParseDoubleTok(toks[3], &p))
                return fail("expected ERRORS <angle-arcsec> <dist-const> <dist-ppm>");
            newWeights.sigmaAngleSec = a;
            newWeights.sigmaDistConstFt = d;
            newWeights.sigmaDistPpm = p;
        } else if (directive == "STN") {
            int at, to;
            if (toks.size() < 3 || !ParseIntTok(toks[1], &at) || !ParseIntTok(toks[2], &to))
                return fail("expected STN <at> <to>");
            if (toIndex.count(to) || sideIndex.count(to))
                return fail("station " + std::to_string(to) + " already declared with STN");

            const std::string kind = (toks.size() >= 4) ? ToUpper(toks[3]) : "";
            if (kind == "SIDE" || kind == "CHECK") {
                bool atIsOccupied = (at == newTd.startStationId);
                for (const TraverseLeg& l : newLegs)
                    if (l.stationId == at) { atIsOccupied = true; break; }
                if (!atIsOccupied)
                    return fail("AT " + std::to_string(at) + " is not an already-occupied station");

                TraverseSideShot ss;
                ss.atStationId = at;
                ss.stationId = to;
                ss.isZenithAngle = true;
                ss.isCheckShot = (kind == "CHECK");
                if (ss.isCheckShot && toks.size() >= 6) {
                    double ke, kn;
                    if (!ParseDoubleTok(toks[4], &ke) || !ParseDoubleTok(toks[5], &kn))
                        return fail("expected STN <at> <to> CHECK [known-e known-n]");
                    ss.knownEasting = ke;
                    ss.knownNorthing = kn;
                    ss.hasKnownCoords = true;
                }
                ss.sourceOrder = assignOrder();
                newSideShots.push_back(std::move(ss));
                sideIndex[to] = newSideShots.size() - 1;
            } else if (!kind.empty()) {
                return fail("STN: unknown modifier \"" + toks[3] + "\" (expected SIDE or CHECK)");
            } else {
                const int expected = expectedAt(newLegs.size());
                if (at != expected)
                    return fail("AT " + std::to_string(at) + " doesn't continue the traverse chain (expected " +
                                std::to_string(expected) + ")");
                TraverseLeg leg;
                leg.stationId = to;
                leg.isZenithAngle = true;
                leg.sourceOrder = assignOrder();
                newLegs.push_back(std::move(leg));
                toIndex[to] = newLegs.size() - 1;
            }
        } else if (directive == "HANG" || directive == "HDIST" || directive == "SDIST" ||
                   directive == "VANG" || directive == "DESC" || directive == "PRISM" ||
                   directive == "POLE" || directive == "TSERR") {
            // All of these share the same <at> <to> addressing, at a different token
            // offset for HANG (which carries <from> first).
            const size_t atPos = (directive == "HANG") ? 2 : 1;
            const size_t toPos = atPos + 1;
            if (toks.size() <= toPos)
                return fail("expected " + directive + " ... <at> <to> ...");
            int at, to;
            if (!ParseIntTok(toks[atPos], &at) || !ParseIntTok(toks[toPos], &to))
                return fail(directive + ": <at>/<to> must be integers");

            TraverseLeg* legPtr = nullptr;
            TraverseSideShot* sidePtr = nullptr;
            size_t legIdx = 0;
            const auto itLeg = toIndex.find(to);
            if (itLeg != toIndex.end()) {
                legIdx = itLeg->second;
                legPtr = &newLegs[legIdx];
                if (at != expectedAt(legIdx))
                    return fail("AT " + std::to_string(at) + " doesn't match station " +
                                std::to_string(expectedAt(legIdx)) + " occupied for the leg to station " +
                                std::to_string(to));
            } else {
                const auto itSide = sideIndex.find(to);
                if (itSide == sideIndex.end())
                    return fail("station " + std::to_string(to) + " not declared (use STN " +
                                std::to_string(at) + " " + std::to_string(to) + " first)");
                sidePtr = &newSideShots[itSide->second];
                if (at != sidePtr->atStationId)
                    return fail("AT " + std::to_string(at) + " doesn't match the station (" +
                                std::to_string(sidePtr->atStationId) + ") this side/check shot was declared at");
            }
            if (!lineCode.empty()) {
                if (legPtr != nullptr) legPtr->code = lineCode;
                else sidePtr->code = lineCode;
            }

            if (directive == "PRISM" || directive == "POLE" || directive == "TSERR") {
                if (sidePtr)
                    return fail(directive + " does not apply to a side/check shot (no least-squares adjustment uses it)");
            }

            if (directive == "HANG") {
                // toks[1] is FROM: unconstrained for a side/check shot (no "previous leg")
                // and for the first leg (no modeled backsight station — the literal REF is
                // round-tripped there); otherwise must equal the station occupied by the
                // previous leg.
                if (legPtr && legIdx > 0) {
                    int from;
                    if (!ParseIntTok(toks[1], &from))
                        return fail("HANG: <from> must be an integer after the first leg");
                    if (from != expectedAt(legIdx - 1))
                        return fail("FROM " + std::to_string(from) + " doesn't match station " +
                                    std::to_string(expectedAt(legIdx - 1)) + " occupied for the previous leg");
                }
                double ang;
                if (toks.size() < 5 || !ParseDoubleTok(toks[4], &ang))
                    return fail("expected HANG <from> <at> <to> <angle-deg>");
                if (legPtr) {
                    legPtr->horizAngleDeg = ang;
                    legPtr->hasHorizAngle = true;
                    legPtr->horizAngleBuf = Fmt(ang);
                } else {
                    sidePtr->horizAngleDeg = ang;
                    sidePtr->hasHorizAngle = true;
                    sidePtr->horizAngleBuf = Fmt(ang);
                }
            } else if (directive == "HDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected HDIST <at> <to> <dist>");
                if (legPtr) { legPtr->horizDist = dist; legPtr->hasHorizDist = (dist > 0.0); }
                else { sidePtr->horizDist = dist; sidePtr->hasHorizDist = (dist > 0.0); }
            } else if (directive == "SDIST") {
                double dist;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &dist))
                    return fail("expected SDIST <at> <to> <dist>");
                if (legPtr) { legPtr->slopeDist = dist; legPtr->hasSlopeDist = (dist > 0.0); }
                else { sidePtr->slopeDist = dist; sidePtr->hasSlopeDist = (dist > 0.0); }
            } else if (directive == "VANG") {
                double ang;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &ang))
                    return fail("expected VANG <at> <to> <angle-deg> [ZENITH|ELEV]");
                const bool zenith = (toks.size() < 5) || (ToUpper(toks[4]) != "ELEV");
                if (legPtr) {
                    legPtr->vertAngleDeg = ang;
                    legPtr->hasVertAngle = true;
                    legPtr->vertAngleBuf = Fmt(ang);
                    legPtr->isZenithAngle = zenith;
                } else {
                    sidePtr->vertAngleDeg = ang;
                    sidePtr->hasVertAngle = true;
                    sidePtr->vertAngleBuf = Fmt(ang);
                    sidePtr->isZenithAngle = zenith;
                }
            } else if (directive == "DESC") {  // everything after the <to> token, verbatim.
                auto isSpace = [](char c) { return std::isspace(static_cast<unsigned char>(c)); };
                auto skipToken = [&](size_t p) -> size_t {
                    while (p < line.size() && isSpace(line[p])) ++p;
                    while (p < line.size() && !isSpace(line[p])) ++p;
                    return p;
                };
                size_t pos = skipToken(0);       // past DESC
                pos = skipToken(pos);            // past <at>
                pos = skipToken(pos);            // past <to>
                while (pos < line.size() && isSpace(line[pos])) ++pos;
                const std::string desc = (pos < line.size()) ? line.substr(pos) : "";
                if (legPtr) legPtr->description = desc;
                else sidePtr->description = desc;
            } else if (directive == "PRISM") {
                double constant, error;
                if (toks.size() < 5 || !ParseDoubleTok(toks[3], &constant) || !ParseDoubleTok(toks[4], &error))
                    return fail("expected PRISM <at> <to> <constant> <error>");
                legPtr->prismConstant = constant;
                legPtr->hasPrismConstant = (constant != 0.0);
                legPtr->prismError = error;
                legPtr->hasPrismError = (error != 0.0);
            } else if (directive == "POLE") {
                double error;
                if (toks.size() < 4 || !ParseDoubleTok(toks[3], &error))
                    return fail("expected POLE <at> <to> <error>");
                legPtr->poleError = error;
                legPtr->hasPoleError = (error != 0.0);
            } else {  // TSERR
                double a, d, p;
                if (toks.size() < 6 || !ParseDoubleTok(toks[3], &a) || !ParseDoubleTok(toks[4], &d) ||
                    !ParseDoubleTok(toks[5], &p))
                    return fail("expected TSERR <at> <to> <angle-arcsec> <dist-const> <dist-ppm>");
                legPtr->tsWeights.sigmaAngleSec = a;
                legPtr->tsWeights.sigmaDistConstFt = d;
                legPtr->tsWeights.sigmaDistPpm = p;
                legPtr->hasTsWeights = true;
            }
        } else if (directive == "CTRL" || directive == "ANGLE" || directive == "DIST" ||
                   directive == "AZIMUTH" || directive == "EDIF" || directive == "ZENITH" ||
                   directive == "DIR") {
            auto stamp = [&](auto& obs) {
                obs.code = lineCode;
                obs.sourceOrder = assignOrder();
            };
            auto parseFix = [&](const std::string& tok, NetworkComponent& component) -> bool {
                if (tok == "!") {
                    component.fix = NetworkFix::Fixed;
                    component.present = true;
                    return true;
                }
                if (tok == "*") {
                    component.fix = NetworkFix::Free;
                    component.present = true;
                    return true;
                }
                double sigma = 0.0;
                if (!ParseDoubleTok(tok, &sigma) || !(sigma > 0.0)) return false;
                component.fix = NetworkFix::Weighted;
                component.stdError = sigma;
                component.present = true;
                return true;
            };
            if (directive == "CTRL") {
                int id = 0;
                double east = 0.0, north = 0.0, elev = 0.0;
                if (toks.size() < 4 || !ParseIntTok(toks[1], &id) || !ParseDoubleTok(toks[2], &east) ||
                    !ParseDoubleTok(toks[3], &north))
                    return fail("expected CTRL <id> <e> <n> [fixes]");
                NetworkStation station;
                station.id = id;
                station.easting.value = east;
                station.easting.present = true;
                station.northing.value = north;
                station.northing.present = true;
                if (toks.size() == 4) {
                    station.easting.fix = NetworkFix::Fixed;
                    station.northing.fix = NetworkFix::Fixed;
                } else if (toks.size() == 6) {
                    if (!parseFix(toks[4], station.easting) || !parseFix(toks[5], station.northing))
                        return fail("CTRL fix must be !, *, or a positive standard error");
                } else if (toks.size() == 8) {
                    if (!ParseDoubleTok(toks[4], &elev) || !parseFix(toks[5], station.easting) ||
                        !parseFix(toks[6], station.northing) || !parseFix(toks[7], station.elevation))
                        return fail("expected CTRL <id> <e> <n> <z> <e-fix> <n-fix> <z-fix>");
                    station.elevation.value = elev;
                } else {
                    return fail("expected CTRL <id> <e> <n> [fixes]");
                }
                stamp(station);
                newTd.networkStations.push_back(station);
            } else if (directive == "ANGLE") {
                int at = 0, from = 0, to = 0;
                double deg = 0.0;
                if (toks.size() < 5 || !ParseIntTok(toks[1], &at) || !ParseIntTok(toks[2], &from) ||
                    !ParseIntTok(toks[3], &to) || !ParseDoubleTok(toks[4], &deg))
                    return fail("expected ANGLE <at> <from> <to> <deg> [stderr-sec]");
                NetworkAngleObs angle;
                angle.atId = at;
                angle.fromId = from;
                angle.toId = to;
                angle.angleDeg = deg;
                if (toks.size() >= 6 && !ParseDoubleTok(toks[5], &angle.stdErrorSec))
                    return fail("ANGLE standard error must be a number");
                stamp(angle);
                newTd.networkAngles.push_back(angle);
            } else if (directive == "DIST") {
                int from = 0, to = 0;
                double dist = 0.0;
                if (toks.size() < 4 || !ParseIntTok(toks[1], &from) || !ParseIntTok(toks[2], &to) ||
                    !ParseDoubleTok(toks[3], &dist))
                    return fail("expected DIST <from> <to> <dist> [stderr]");
                NetworkDistObs observation;
                observation.fromId = from;
                observation.toId = to;
                observation.distance = dist;
                if (toks.size() >= 5 && !ParseDoubleTok(toks[4], &observation.stdError))
                    return fail("DIST standard error must be a number");
                stamp(observation);
                newTd.networkDists.push_back(observation);
            } else if (directive == "AZIMUTH") {
                int from = 0, to = 0;
                double deg = 0.0;
                if (toks.size() < 4 || !ParseIntTok(toks[1], &from) || !ParseIntTok(toks[2], &to) ||
                    !ParseDoubleTok(toks[3], &deg))
                    return fail("expected AZIMUTH <from> <to> <deg> [stderr-sec]");
                NetworkAzimuthObs azimuth;
                azimuth.fromId = from;
                azimuth.toId = to;
                azimuth.azimuthDeg = deg;
                if (toks.size() >= 5 && !ParseDoubleTok(toks[4], &azimuth.stdErrorSec))
                    return fail("AZIMUTH standard error must be a number");
                stamp(azimuth);
                newTd.networkAzimuths.push_back(azimuth);
            } else if (directive == "EDIF") {
                int from = 0, to = 0;
                double dz = 0.0;
                if (toks.size() < 4 || !ParseIntTok(toks[1], &from) || !ParseIntTok(toks[2], &to) ||
                    !ParseDoubleTok(toks[3], &dz))
                    return fail("expected EDIF <from> <to> <elev-diff> [stderr]");
                NetworkElevDiffObs diff;
                diff.fromId = from;
                diff.toId = to;
                diff.deltaZ = dz;
                if (toks.size() >= 5 && !ParseDoubleTok(toks[4], &diff.stdError))
                    return fail("EDIF standard error must be a number");
                stamp(diff);
                newTd.networkElevDiffs.push_back(diff);
            } else if (directive == "ZENITH") {
                int from = 0, to = 0;
                double deg = 0.0;
                if (toks.size() < 4 || !ParseIntTok(toks[1], &from) || !ParseIntTok(toks[2], &to) ||
                    !ParseDoubleTok(toks[3], &deg))
                    return fail("expected ZENITH <from> <to> <deg> [stderr-sec]");
                NetworkZenithObs zenith;
                zenith.fromId = from;
                zenith.toId = to;
                zenith.zenithDeg = deg;
                if (toks.size() >= 5 && !ParseDoubleTok(toks[4], &zenith.stdErrorSec))
                    return fail("ZENITH standard error must be a number");
                stamp(zenith);
                newTd.networkZeniths.push_back(zenith);
            } else if (directive == "DIR") {
                int setId = 0, at = 0, to = 0;
                double deg = 0.0;
                if (toks.size() < 5 || !ParseIntTok(toks[1], &setId) || !ParseIntTok(toks[2], &at) ||
                    !ParseIntTok(toks[3], &to) || !ParseDoubleTok(toks[4], &deg))
                    return fail("expected DIR <set> <at> <to> <deg> [stderr-sec]");
                NetworkDirectionObs dir;
                dir.setId = setId;
                dir.atId = at;
                dir.toId = to;
                dir.directionDeg = deg;
                if (toks.size() >= 6 && !ParseDoubleTok(toks[5], &dir.stdErrorSec))
                    return fail("DIR standard error must be a number");
                stamp(dir);
                newTd.networkDirections.push_back(dir);
            }
        } else {
            return fail("unknown directive \"" + toks[0] + "\"");
        }
    }

    for (size_t i = 0; i < newTd.rawSets.size(); ++i) {
        const auto& raw = newTd.rawSets[i];
        if (std::none_of(newTd.rawSources.begin(), newTd.rawSources.end(),
                [&](const TraverseRawSource& source) { return source.id == raw.sourceId; })) {
            errorMsg = "line " + std::to_string(rawSetLines[i]) + ": unknown RAWSET source ID " + raw.sourceId;
            return false;
        }
    }
    const bool hasNetwork = !newTd.networkAngles.empty() || !newTd.networkDists.empty() ||
                            !newTd.networkAzimuths.empty() || !newTd.networkElevDiffs.empty() ||
                            !newTd.networkZeniths.empty() || !newTd.networkDirections.empty();
    if (!sawStation && !hasNetwork && newTd.rawSets.empty()) {
        errorMsg = "missing STATION directive";
        return false;
    }
    if (hasNetwork && newTd.networkStations.empty() && !sawStation) {
        errorMsg = "a network measurement needs CTRL or STATION coordinates";
        return false;
    }

    flushNotes(nextSourceOrder);
    newTd.legs = std::move(newLegs);
    newTd.sideShots = std::move(newSideShots);
    if (hasGraphMetadata) {
        CommandDocument document;
        if (!Project(text, document, errorMsg)) return false;
        newTd.graphGroups = document.groups;
        for (const auto& node : document.commands)
            newTd.graphNodes.push_back({node.id, node.text, node.sourceOrder,
                node.x, node.y, node.groupId, node.manualOrder});
    }
    td = std::move(newTd);
    weights = newWeights;
    errorMsg.clear();
    return true;
}

namespace {

std::vector<size_t> StationFieldIndices(const std::string& key) {
    if (key == "STATION" || key == "CTRL") return {1};
    if (key == "ANGLE" || key == "HANG") return {1, 2, 3};
    if (key == "DIR") return {2, 3};
    if (key == "STN" || key == "HDIST" || key == "SDIST" || key == "VANG" ||
        key == "DESC" || key == "PRISM" || key == "POLE" || key == "TSERR" ||
        key == "DIST" || key == "AZIMUTH" || key == "EDIF" || key == "ZENITH" || key == "RAWSET") return {1, 2};
    return {};
}

std::vector<std::string> CommandTokens(const CommandNode& node) {
    auto tokens = Tokenize(node.text);
    if (tokens.empty()) return tokens;
    if (tokens[0][0] == '#') tokens[0].erase(0, 1);
    if (tokens[0].empty()) tokens.erase(tokens.begin());
    if (tokens.empty()) return tokens;
    tokens[0] = ToUpper(tokens[0]);
    for (size_t field : StationFieldIndices(tokens[0])) {
        if (field >= tokens.size()) continue;
        char* end = nullptr;
        const long long value = std::strtoll(tokens[field].c_str(), &end, 10);
        if (end != tokens[field].c_str() && *end == '\0' && value > 0 && value <= std::numeric_limits<int>::max())
            tokens[field] = std::to_string(value);
    }
    return tokens;
}

std::vector<std::string> StationFields(const std::vector<std::string>& tokens) {
    if (tokens.empty()) return {};
    std::vector<std::string> stations;
    for (size_t field : StationFieldIndices(tokens[0]))
        if (field < tokens.size() && tokens[field] != "REF") stations.push_back(tokens[field]);
    return stations;
}

bool SetupCommand(const std::string& key) {
    return key == "STATION" || key == "BEARING" || key == "ENDPOINT" ||
           key == "LOOP" || key == "SCALE" || key == "ERRORS" || key == "IMPORT";
}

}  // namespace

bool Project(const std::string& text, CommandDocument& document, std::string& errorMsg) {
    CommandDocument result;
    std::istringstream input(text);
    std::string line;
    std::vector<std::string> comments;
    std::unordered_set<std::string> reservedIds;
    std::istringstream scan(text);
    while (std::getline(scan, line)) {
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line.compare(first, 2, "//") != 0) continue;
        const auto fields = Tokenize(line.substr(first + 2));
        if (fields.size() >= 2 && fields[0] == "@GSTC-NODE") reservedIds.insert(fields[1]);
    }
    std::unordered_set<std::string> usedIds;
    std::optional<CommandNode> metadata;
    int lineNo = 0;
    auto fail = [&](const std::string& message) {
        errorMsg = "line " + std::to_string(lineNo) + ": " + message;
        return false;
    };
    while (std::getline(input, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t first = line.find_first_not_of(" \t");
        if (first != std::string::npos && line.compare(first, 2, "//") == 0) {
            const auto fields = Tokenize(line.substr(first + 2));
            if (!fields.empty() && fields[0] == "@GSTC-GROUP") {
                if (metadata) return fail("node metadata must immediately precede a command");
                TraverseGraphGroup group;
                std::string marker, label;
                int collapsed = 0;
                std::istringstream values(line.substr(first + 2));
                if (!(values >> marker >> group.id >> group.x >> group.y >> group.width >> group.height >> collapsed) ||
                    !std::isfinite(group.x) || !std::isfinite(group.y) ||
                    !std::isfinite(group.width) || !std::isfinite(group.height) ||
                    group.width < 0 || group.height < 0 || (collapsed != 0 && collapsed != 1))
                    return fail("invalid group metadata");
                std::getline(values, label);
                const auto value = nlohmann::json::parse(label, nullptr, false);
                if (!value.is_string()) return fail("invalid group label");
                if (std::any_of(result.groups.begin(), result.groups.end(),
                    [&](const TraverseGraphGroup& old) { return old.id == group.id; }))
                    return fail("duplicate group ID " + group.id);
                group.collapsed = collapsed != 0;
                group.label = value.get<std::string>();
                result.groups.push_back(std::move(group));
                continue;
            }
            if (!fields.empty() && fields[0] == "@GSTC-SOURCE") {
                if (metadata) return fail("node metadata must immediately precede a command");
                TraverseRawSource source;
                std::string marker, path;
                std::istringstream values(line.substr(first + 2));
                values >> marker >> source.id;
                std::getline(values, path);
                const auto value = nlohmann::json::parse(path, nullptr, false);
                if (source.id.empty() || !value.is_string() ||
                    std::any_of(result.sources.begin(), result.sources.end(),
                        [&](const TraverseRawSource& old) { return old.id == source.id; }))
                    return fail("invalid or duplicate source metadata");
                source.path = value.get<std::string>();
                result.sources.push_back(std::move(source));
                continue;
            }
            if (!fields.empty() && fields[0] == "@GSTC-NODE") {
                if (metadata) return fail("node metadata must immediately precede a command");
                CommandNode node;
                std::string marker, group, rank, extra;
                std::istringstream values(line.substr(first + 2));
                if (!(values >> marker >> node.id >> node.x >> node.y >> group >> rank) ||
                    (values >> extra) || !std::isfinite(node.x) || !std::isfinite(node.y))
                    return fail("invalid node metadata");
                if (!usedIds.insert(node.id).second) return fail("duplicate node ID " + node.id);
                node.groupId = group == "-" ? "" : group;
                if (rank != "-") {
                    char* end = nullptr;
                    const long long value = std::strtoll(rank.c_str(), &end, 10);
                    if (end == rank.c_str() || *end != '\0' || value < std::numeric_limits<int>::min() ||
                        value > std::numeric_limits<int>::max()) return fail("invalid manual order");
                    node.manualOrder = static_cast<int>(value);
                }
                metadata = std::move(node);
                continue;
            }
        }
        if (first == std::string::npos || line.compare(first, 2, "//") == 0) {
            if (metadata) return fail("node metadata must immediately precede a command");
            comments.push_back(line);
            continue;
        }
        CommandNode node;
        if (metadata) {
            node = std::move(*metadata);
            metadata.reset();
        } else {
            size_t suffix = result.commands.size() + 1;
            do { node.id = "command-" + std::to_string(suffix++); }
            while (reservedIds.count(node.id) || usedIds.count(node.id));
            usedIds.insert(node.id);
        }
        node.sourceOrder = result.commands.size();
        node.text = line;
        node.comments = std::move(comments);
        comments.clear();
        const auto tokens = CommandTokens(node);
        if (tokens.empty()) {
            comments = std::move(node.comments);
            comments.push_back(line);
            continue;
        }
        node.setup = SetupCommand(tokens[0]);
        node.stationIds = StationFields(tokens);
        result.commands.push_back(std::move(node));
    }
    if (metadata) return fail("node metadata has no command");
    result.trailingComments = std::move(comments);
    for (const CommandNode& node : result.commands) {
        const auto tokens = CommandTokens(node);
        for (const std::string& id : node.stationIds) {
            auto found = std::find_if(result.stations.begin(), result.stations.end(),
                [&](const StationAnchor& station) { return station.id == id; });
            if (found == result.stations.end()) {
                result.stations.push_back({id, false});
                found = result.stations.end() - 1;
            }
            if ((tokens[0] == "STATION" || tokens[0] == "CTRL") ||
                (tokens[0] == "STN" && tokens.size() > 2 && tokens[2] == id)) found->resolved = true;
        }
    }
    document = std::move(result);
    errorMsg.clear();
    return true;
}

CommandOrder Order(const CommandDocument& document) {
    const size_t count = document.commands.size();
    std::vector<std::vector<size_t>> dependencies(count);
    std::unordered_map<std::string, std::vector<size_t>> providers;
    std::unordered_map<std::string, std::vector<size_t>> legs;
    std::vector<std::vector<std::string>> tokens;
    tokens.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        tokens.push_back(CommandTokens(document.commands[i]));
        const auto& t = tokens.back();
        if (t.size() > 1 && (t[0] == "STATION" || t[0] == "CTRL")) providers[t[1]].push_back(i);
        if (t.size() > 2 && t[0] == "STN") {
            providers[t[2]].push_back(i);
            legs[t[1] + " " + t[2]].push_back(i);
        }
    }
    // A closing leg references an existing control; it does not redefine it.
    for (auto& entry : providers) {
        auto& candidates = entry.second;
        const bool known = std::any_of(candidates.begin(), candidates.end(), [&](size_t i) {
            return tokens[i][0] == "STATION" || tokens[i][0] == "CTRL";
        });
        if (known) candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
            [&](size_t i) { return tokens[i][0] == "STN"; }), candidates.end());
    }
    for (size_t i = 0; i < count; ++i) {
        const auto& t = tokens[i];
        if (t.empty()) continue;
        auto depend = [&](size_t before) {
            if (std::find(dependencies[i].begin(), dependencies[i].end(), before) == dependencies[i].end())
                dependencies[i].push_back(before);
        };
        if (!SetupCommand(t[0])) {
            for (size_t j = 0; j < count; ++j)
                if (!tokens[j].empty() && SetupCommand(tokens[j][0])) depend(j);
        }
        std::vector<std::string> references = StationFields(t);
        if (t[0] == "STATION" || t[0] == "CTRL") references.clear();
        if (t[0] == "STN" && t.size() > 2) references = {t[1]};
        const bool legValue = t[0] == "HANG" || t[0] == "HDIST" || t[0] == "SDIST" || t[0] == "VANG" ||
                              t[0] == "DESC" || t[0] == "PRISM" || t[0] == "POLE" || t[0] == "TSERR";
        if (legValue && t.size() > (t[0] == "HANG" ? 3u : 2u)) {
            const size_t at = t[0] == "HANG" ? 2 : 1;
            for (size_t provider : legs[t[at] + " " + t[at + 1]]) depend(provider);
        } else {
            for (const std::string& station : references)
                for (size_t provider : providers[station]) depend(provider);
        }
    }
    CommandOrder result;
    auto sourceLess = [&](size_t a, size_t b) {
        if (document.commands[a].sourceOrder != document.commands[b].sourceOrder)
            return document.commands[a].sourceOrder < document.commands[b].sourceOrder;
        return a < b;
    };
    auto manualComplete = [&](const std::vector<size_t>& members) {
        std::unordered_set<int> ranks;
        for (size_t member : members) {
            const auto rank = document.commands[member].manualOrder;
            if (!rank || !ranks.insert(*rank).second) return false;
        }
        return true;
    };
    std::vector<std::vector<size_t>> ambiguous;
    for (const auto& entry : providers) {
        if (entry.second.size() > 1) {
            auto members = entry.second;
            std::sort(members.begin(), members.end(), sourceLess);
            ambiguous.push_back(std::move(members));
        }
    }
    std::sort(ambiguous.begin(), ambiguous.end(), [&](const auto& a, const auto& b) { return sourceLess(a[0], b[0]); });
    for (auto members : ambiguous) {
        OrderDiagnostic diagnostic;
        diagnostic.kind = OrderDiagnosticKind::Ambiguous;
        for (size_t member : members) diagnostic.commandIds.push_back(document.commands[member].id);
        diagnostic.overridden = manualComplete(members);
        if (diagnostic.overridden) {
            std::sort(members.begin(), members.end(), [&](size_t a, size_t b) {
                return *document.commands[a].manualOrder < *document.commands[b].manualOrder;
            });
            // An ambiguity override may not reverse an existing dependency.
            for (size_t i = 1; i < members.size(); ++i) {
                std::vector<size_t> stack{members[i - 1]};
                std::vector<bool> seen(count, false);
                while (!stack.empty()) {
                    const size_t node = stack.back();
                    stack.pop_back();
                    if (node == members[i]) { diagnostic.overridden = false; break; }
                    if (seen[node]) continue;
                    seen[node] = true;
                    for (size_t dependency : dependencies[node]) stack.push_back(dependency);
                }
                if (!diagnostic.overridden) break;
            }
            if (diagnostic.overridden)
                for (size_t i = 1; i < members.size(); ++i) dependencies[members[i]].push_back(members[i - 1]);
        }
        result.diagnostics.push_back(std::move(diagnostic));
    }
    // Strongly connected components keep cycle warnings and overrides local.
    // Iterative DFS avoids consuming the call stack on long survey chains.
    std::vector<std::vector<size_t>> dependents(count);
    for (size_t i = 0; i < count; ++i)
        for (size_t dependency : dependencies[i]) dependents[dependency].push_back(i);
    std::vector<bool> visited(count, false);
    std::vector<size_t> finished;
    for (size_t root = 0; root < count; ++root) {
        if (visited[root]) continue;
        std::vector<std::pair<size_t, size_t>> stack{{root, 0}};
        visited[root] = true;
        while (!stack.empty()) {
            auto& frame = stack.back();
            if (frame.second == dependencies[frame.first].size()) {
                finished.push_back(frame.first);
                stack.pop_back();
                continue;
            }
            const size_t next = dependencies[frame.first][frame.second++];
            if (!visited[next]) {
                visited[next] = true;
                stack.push_back({next, 0});
            }
        }
    }
    std::vector<size_t> component(count, count);
    std::vector<std::vector<size_t>> portions;
    for (auto it = finished.rbegin(); it != finished.rend(); ++it) {
        if (component[*it] != count) continue;
        const size_t id = portions.size();
        portions.push_back({});
        std::vector<size_t> stack{*it};
        component[*it] = id;
        while (!stack.empty()) {
            const size_t node = stack.back();
            stack.pop_back();
            portions.back().push_back(node);
            for (size_t next : dependents[node]) {
                if (component[next] != count) continue;
                component[next] = id;
                stack.push_back(next);
            }
        }
        auto& members = portions.back();
        std::sort(members.begin(), members.end(), sourceLess);
        const bool selfCycle = members.size() == 1 &&
            std::find(dependencies[members[0]].begin(), dependencies[members[0]].end(), members[0]) != dependencies[members[0]].end();
        if (members.size() > 1 || selfCycle) {
            OrderDiagnostic diagnostic;
            diagnostic.kind = OrderDiagnosticKind::Cyclic;
            for (size_t member : members) diagnostic.commandIds.push_back(document.commands[member].id);
            diagnostic.overridden = manualComplete(members);
            result.diagnostics.push_back(std::move(diagnostic));
        }
    }
    std::vector<bool> emitted(portions.size(), false);
    while (result.commandIds.size() < count) {
        size_t next = portions.size();
        for (size_t i = 0; i < portions.size(); ++i) {
            if (emitted[i]) continue;
            bool ready = true;
            for (size_t member : portions[i])
                for (size_t dependency : dependencies[member])
                    if (component[dependency] != i && !emitted[component[dependency]]) ready = false;
            if (ready && (next == portions.size() || sourceLess(portions[i][0], portions[next][0]))) next = i;
        }
        if (next == portions.size()) break; // The component graph is acyclic by construction.
        emitted[next] = true;
        auto members = portions[next];
        if (members.size() > 1 && manualComplete(members))
            std::sort(members.begin(), members.end(), [&](size_t a, size_t b) {
                return *document.commands[a].manualOrder < *document.commands[b].manualOrder;
            });
        for (size_t member : members) result.commandIds.push_back(document.commands[member].id);
    }
    return result;
}

std::string Serialize(const CommandDocument& document) {
    const CommandOrder order = Order(document);
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& source : document.sources)
        out << "// @GSTC-SOURCE " << source.id << " " << nlohmann::json(source.path).dump() << "\n";
    for (const auto& group : document.groups)
        out << "// @GSTC-GROUP " << group.id << " " << group.x << " " << group.y << " "
            << group.width << " " << group.height << " " << (group.collapsed ? 1 : 0) << " "
            << nlohmann::json(group.label).dump() << "\n";
    for (const std::string& id : order.commandIds) {
        const auto found = std::find_if(document.commands.begin(), document.commands.end(),
            [&](const CommandNode& node) { return node.id == id; });
        if (found == document.commands.end()) continue;
        for (const std::string& comment : found->comments) out << comment << "\n";
        out << std::setprecision(std::numeric_limits<double>::max_digits10);
        out << "// @GSTC-NODE " << found->id << " " << found->x << " " << found->y << " "
            << (found->groupId.empty() ? "-" : found->groupId) << " "
            << (found->manualOrder ? std::to_string(*found->manualOrder) : "-") << "\n";
        out << found->text << "\n";
    }
    for (const std::string& comment : document.trailingComments) out << comment << "\n";
    return out.str();
}

// Match by directive and survey relationship, not measurement values or position.
// Occurrence order distinguishes repeated observations of the same relationship.
static std::vector<std::string> GraphCommandKey(const std::string& text) {
    CommandNode node;
    node.text = text;
    const auto tokens = CommandTokens(node);
    if (tokens.empty()) return {};
    std::vector<std::string> key{tokens[0]};
    // The first HANG backsight is informational and serializes as REF.
    const auto stations = tokens[0] == "HANG" && tokens.size() >= 4
        ? std::vector<std::string>{tokens[2], tokens[3]} : StationFields(tokens);
    key.insert(key.end(), stations.begin(), stations.end());
    if (tokens[0] == "RAWSET" && tokens.size() >= 6) {
        key.push_back(std::to_string(std::strtoll(tokens[3].c_str(), nullptr, 10)));
        key.push_back(tokens.back());
    } else if (tokens[0] == "DIR" && tokens.size() >= 2) {
        key.push_back(std::to_string(std::strtoll(tokens[1].c_str(), nullptr, 10)));
    } else if (tokens[0] == "STN" && tokens.size() >= 4) {
        key.push_back(tokens[3]);
    }
    return key;
}

static std::string WithGraphMetadata(const std::string& text, const TraverseData& td) {
    if (td.graphNodes.empty() && td.graphGroups.empty()) return text;
    CommandDocument document;
    std::string error;
    if (!Project(text, document, error)) throw std::invalid_argument("Invalid generated graph metadata: " + error);
    document.groups = td.graphGroups;
    std::vector<bool> used(td.graphNodes.size(), false);
    std::unordered_set<std::string> reservedIds;
    for (const auto& node : td.graphNodes) reservedIds.insert(node.id);
    for (auto& node : document.commands) {
        const auto key = GraphCommandKey(node.text);
        bool matched = false;
        for (size_t i = 0; i < td.graphNodes.size(); ++i) {
            const auto& saved = td.graphNodes[i];
            if (used[i] || GraphCommandKey(saved.command) != key) continue;
            node.id = saved.id;
            node.x = saved.x;
            node.y = saved.y;
            node.groupId = saved.groupId;
            node.manualOrder = saved.manualOrder;
            used[i] = true;
            matched = true;
            break;
        }
        if (!matched) {
            size_t suffix = 1;
            while (reservedIds.count(node.id)) node.id = "command-" + std::to_string(suffix++);
            reservedIds.insert(node.id);
        }
    }
    return Serialize(document);
}

}  // namespace travcmd
