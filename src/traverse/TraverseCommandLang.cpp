#include "TraverseCommandLang.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
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
    if (!td.sourceComments.empty()) return true;
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

    std::sort(pieces.begin(), pieces.end(), [](const Piece& a, const Piece& b) {
        if (a.order != b.order) return a.order < b.order;
        if (a.sub != b.sub) return a.sub < b.sub;
        return a.seq < b.seq;
    });
    for (const Piece& piece : pieces) out << piece.text;
}

std::string Serialize(const TraverseData& td, const LsaWeights& weights) {
    std::ostringstream out;
    out << "// GoSurvey Traverse Command (GSTC) \xe2\x80\x94 edit and re-apply.\n";
    for (const std::string& option : td.importerSkippedInlineOptions)
        out << "// Inline option from STAR*NET (not applied): " << option << "\n";

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
        return AlignColumns(out.str());
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
    return AlignColumns(out.str());
}

bool Parse(const std::string& text, TraverseData& td, LsaWeights& weights, std::string& errorMsg) {
    TraverseData newTd;
    newTd.hasStartBearing = false;
    LsaWeights newWeights;
    bool sawStation = false;

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
            size_t quote = line.find('\'');
            const size_t tick = line.find('`');
            if (tick != std::string::npos && (quote == std::string::npos || tick < quote)) quote = tick;
            if (quote != std::string::npos) {
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

        if (directive == "IMPORT") {
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

    const bool hasNetwork = !newTd.networkAngles.empty() || !newTd.networkDists.empty() ||
                            !newTd.networkAzimuths.empty() || !newTd.networkElevDiffs.empty() ||
                            !newTd.networkZeniths.empty() || !newTd.networkDirections.empty();
    if (!sawStation && !hasNetwork) {
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
    td = std::move(newTd);
    weights = newWeights;
    errorMsg.clear();
    return true;
}

}  // namespace travcmd
