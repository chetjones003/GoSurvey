#include "GoSurvey/Survey/TraverseReport.hpp"

#include "GoSurvey/Survey/TraverseCalc.hpp"


#include <cstdio>
#include <sstream>
#include <type_traits>
#include <vector>


namespace traverserpt {

namespace {

std::string Line(char fillChar, int width) { return std::string(static_cast<size_t>(width), fillChar); }

// A station id column shows "-" rather than -1 for "this measurement kind has no such
// station" (e.g. a distance's At, or an angle's From when its backsight is the reference
// bearing rather than a station).
std::string IdOrDash(int id) { return id < 0 ? std::string("-") : std::to_string(id); }

template <typename T>
std::string Field(const char* label, const T& value, const char* unit = "") {
    char buf[160];
    if constexpr (std::is_same_v<T, std::string>) {
        std::snprintf(buf, sizeof(buf), "  %-28s: %s%s\n", label, value.c_str(), unit);
    } else if constexpr (std::is_same_v<T, bool>) {
        std::snprintf(buf, sizeof(buf), "  %-28s: %s\n", label, value ? "Yes" : "No");
    } else if constexpr (std::is_integral_v<T>) {
        std::snprintf(buf, sizeof(buf), "  %-28s: %d%s\n", label, static_cast<int>(value), unit);
    } else {
        std::snprintf(buf, sizeof(buf), "  %-28s: %g%s\n", label, static_cast<double>(value), unit);
    }
    return buf;
}

}  // namespace

std::string BuildReportText(const TraverseData& td, const LsaWeights& w, const LsaResult& result,
                            const std::string& projectName, const std::string& unit) {
    std::ostringstream out;
    out << Line('=', 70) << "\n";
    out << "  Summary of Files Used and Option Settings\n";
    out << Line('=', 70) << "\n\n";

    out << "Project Name: " << (projectName.empty() ? "(untitled)" : projectName) << "\n";
    out << "Data File List:\n";
    if (td.importedFiles.empty()) {
        out << "  (none imported)\n";
    } else {
        for (size_t i = 0; i < td.importedFiles.size(); ++i)
            out << "  " << (i + 1) << ". " << td.importedFiles[i] << "\n";
    }
    out << "\n";

    out << "Project Option Settings\n" << Line('-', 40) << "\n";
    out << Field("Type of Adjustment", std::string(w.adjustmentIs3D ? "3D" : "2D"));
    out << "  " << std::string(28, ' ') << "  (" << (result.usedGpsVectors ? "elevation solved" : "elevation not solved")
        << " this run)\n";
    out << Field("Project Units", unit.empty() ? std::string("ft") : unit);
    out << Field("Convergence Limit", w.convergenceLimit);
    out << Field("Max Iterations", w.maxIterations);
    out << Field("Coefficient of Refraction", w.refractionCoefficient);
    out << "\n";

    out << "Instrument Standard Error Settings\n" << Line('-', 40) << "\n";
    out << Field("Distances (Constant)", w.sigmaDistConstFt, " ft");
    out << Field("Distances (PPM)", w.sigmaDistPpm);
    out << Field("Angles", w.sigmaAngleSec, " sec");
    out << Field("Azimuths & Bearings", w.sigmaAzimuthSec, " sec");
    out << Field("Zeniths", w.sigmaZenithSec, " sec");
    out << Field("Elevation Differences (Constant)", w.sigmaVertConstFt, " ft");
    out << Field("Elevation Differences (PPM)", w.sigmaVertPpm);
    out << Field("Differential Levels", w.sigmaLevelFt, " ft");
    out << Field("Centering Error Instrument", w.centeringInstrumentFt, " ft");
    out << Field("Centering Error Target", w.centeringTargetFt, " ft");
    out << Field("Centering Error Vertical", w.centeringVerticalFt, " ft");
    out << "\n";

    out << Line('=', 70) << "\n";
    out << "  Adjustment Results\n";
    out << Line('=', 70) << "\n\n";
    if (!result.ok) {
        out << "Adjustment did not succeed: " << result.message << "\n";
        return out.str();
    }
    out << "Stations (unknowns): " << result.unknowns << "   Observations: " << result.observations
        << "   Redundancy: " << result.redundancy << "   Iterations: " << result.iterations << "\n";
    out << "Standard error of unit weight: " << result.refStdDev << "\n\n";

    out << "Measurement Residuals\n" << Line('-', 70) << "\n";
    char hdr[160];
    std::snprintf(hdr, sizeof(hdr), "  %-10s %6s %6s %6s %12s %12s %10s %10s %s\n", "Kind", "At", "From", "To",
                 "Adjusted", "Residual", "StdError", "StdResid", "");
    out << hdr;
    for (const LsaMeasResidual& row : result.measResiduals) {
        char line[200];
        std::snprintf(line, sizeof(line), "  %-10s %6s %6s %6s %12.5f %12.5f %10.4f %10.2f %s\n",
                     LsaMeasKindName(row.kind), IdOrDash(row.atStationId).c_str(),
                     IdOrDash(row.fromStationId).c_str(), IdOrDash(row.toStationId).c_str(), row.adjusted,
                     row.residual, row.stdError, row.stdResidual, row.flagged ? "*" : "");
        out << line;
    }
    out << "\n";

    if (!result.kindSummaries.empty()) {
        out << "Summary by Measurement Kind\n" << Line('-', 50) << "\n";
        for (const LsaKindSummary& s : result.kindSummaries) {
            char line[160];
            std::snprintf(line, sizeof(line), "  %-10s  count=%-4d  sum-sq-std-resid=%-10.3f  error-factor=%.4f\n",
                         LsaMeasKindName(s.kind), s.count, s.sumSqStdResidual, s.errorFactor);
            out << line;
        }
        out << "\n";
    }

    if (result.chiSquare.evaluated) {
        const LsaChiSquare& cs = result.chiSquare;
        out << "Chi-Square Test (5%, two-tailed)\n" << Line('-', 50) << "\n";
        char line[200];
        std::snprintf(line, sizeof(line),
                     "  error factor=%.4f  bounds=[%.4f, %.4f]  %s\n", cs.errorFactor, cs.lowerFactor,
                     cs.upperFactor, cs.passed ? "PASS" : (cs.exceededUpper ? "FAIL (exceeds upper bound)"
                                                                           : "FAIL (below lower bound)"));
        out << line;
    }

    if (!result.stationIds.empty()) {
        out << "\n";
        out << "Adjusted Coordinates\n" << Line('-', 50) << "\n";
        char hdr2[80];
        std::snprintf(hdr2, sizeof(hdr2), "  %-10s %14s %14s %14s\n", "Station", "Easting", "Northing",
                     result.usedGpsVectors ? "Elevation" : "");
        out << hdr2;
        for (size_t i = 0; i < result.stationIds.size(); ++i) {
            char line2[120];
            if (result.usedGpsVectors) {
                std::snprintf(line2, sizeof(line2), "  %-10d %14.5f %14.5f %14.5f\n", result.stationIds[i],
                             result.adjEasting[i], result.adjNorthing[i], result.adjElevation[i]);
            } else {
                std::snprintf(line2, sizeof(line2), "  %-10d %14.5f %14.5f\n", result.stationIds[i],
                             result.adjEasting[i], result.adjNorthing[i]);
            }
            out << line2;
        }
    }

    return out.str();
}

}  // namespace traverserpt
