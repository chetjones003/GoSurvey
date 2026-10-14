#include "TraverseReport.hpp"

#include "TraverseCalc.hpp"
#include "../io/PlotFont.hpp"

#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <vector>

namespace fs = std::filesystem;

namespace traverserpt {

namespace {

std::string Line(char fillChar, int width) { return std::string(static_cast<size_t>(width), fillChar); }

// A station id column shows "-" rather than -1 for "this measurement kind has no such
// station" (e.g. a distance's At, or an angle's From when its backsight is the reference
// bearing rather than a station).
std::string IdOrDash(int id) { return id < 0 ? std::string("-") : std::to_string(id); }

template <typename T>
std::string Field(const char* label, T value, const char* unit = "") {
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

bool ExportReportTxt(const std::string& pathUtf8, const std::string& text, std::string& err) {
    std::ofstream os(fs::u8path(pathUtf8), std::ios::binary);
    if (!os) {
        err = "could not open file for writing";
        return false;
    }
    os << text;
    if (!os) {
        err = "write failed";
        return false;
    }
    return true;
}

namespace {
// RTF requires \par at line breaks and escaping of \, {, }.
std::string RtfEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char c : text) {
        if (c == '\n') {
            out += "\\par\n";
        } else if (c == '\\' || c == '{' || c == '}') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) >= 0x80) {
            // Non-ASCII: emit as a literal byte run is unsafe in RTF; drop to '?' rather than
            // mis-encode (report text here is ASCII in practice — numbers/labels/file paths).
            out += '?';
        } else {
            out += c;
        }
    }
    return out;
}
}  // namespace

bool ExportReportRtf(const std::string& pathUtf8, const std::string& text, std::string& err) {
    std::ofstream os(fs::u8path(pathUtf8), std::ios::binary);
    if (!os) {
        err = "could not open file for writing";
        return false;
    }
    os << "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fmodern Courier New;}}\\f0\\fs18\n";
    os << RtfEscape(text);
    os << "\n}";
    if (!os) {
        err = "write failed";
        return false;
    }
    return true;
}

namespace {
constexpr float kPtPerIn = 72.f;
struct PdfWriter {
    FPDF_FILEWRITE fw{};
    std::ofstream* os = nullptr;
};
int WriteBlockCb(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
    auto* w = reinterpret_cast<PdfWriter*>(self);
    w->os->write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    return w->os->good() ? 1 : 0;
}
std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}
}  // namespace

bool ExportReportPdf(const std::string& pathUtf8, const std::string& text, std::string& err) {
    FPDF_InitLibrary();  // idempotent; the app's own PDFium init may not have run in this process.
    FPDF_DOCUMENT doc = FPDF_CreateNewDocument();
    if (!doc) {
        err = "could not create PDF document";
        return false;
    }
    FPDF_FONT font = FPDFText_LoadStandardFont(doc, "Courier");
    if (!font) {
        FPDF_CloseDocument(doc);
        err = "could not load report font";
        return false;
    }

    constexpr float kPageWIn = 8.5f, kPageHIn = 11.f;
    constexpr float kMarginIn = 0.6f;
    constexpr float kFontPt = 8.5f;
    constexpr float kLineHIn = (kFontPt * 1.25f) / kPtPerIn;
    const int linesPerPage = static_cast<int>((kPageHIn - 2.f * kMarginIn) / kLineHIn);

    const std::vector<std::string> lines = SplitLines(text);
    int pageCount = 0;
    for (size_t start = 0; start < lines.size() || pageCount == 0; start += static_cast<size_t>(linesPerPage)) {
        FPDF_PAGE page = FPDFPage_New(doc, pageCount, kPageWIn * kPtPerIn, kPageHIn * kPtPerIn);
        ++pageCount;
        float y = kPageHIn - kMarginIn;
        for (size_t i = start; i < lines.size() && i < start + static_cast<size_t>(linesPerPage); ++i) {
            if (!lines[i].empty()) {
                FPDF_PAGEOBJECT obj = FPDFPageObj_CreateTextObj(doc, font, kFontPt);
                if (obj) {
                    std::vector<unsigned short> u16 = plotfont::Utf8ToUtf16(lines[i]);
                    FPDFText_SetText(obj, u16.data());
                    FPDFPageObj_SetFillColor(obj, 0, 0, 0, 255);
                    FS_MATRIX m{1.f, 0.f, 0.f, 1.f, kMarginIn * kPtPerIn, y * kPtPerIn};
                    FPDFPageObj_SetMatrix(obj, &m);
                    FPDFPage_InsertObject(page, obj);
                }
            }
            y -= kLineHIn;
        }
        FPDFPage_GenerateContent(page);
        FPDF_ClosePage(page);
        if (start + static_cast<size_t>(linesPerPage) >= lines.size()) break;
    }

    std::ofstream os(fs::u8path(pathUtf8), std::ios::binary);
    if (!os) {
        FPDFFont_Close(font);
        FPDF_CloseDocument(doc);
        err = "could not open file for writing";
        return false;
    }
    PdfWriter writer;
    writer.fw.version = 1;
    writer.fw.WriteBlock = &WriteBlockCb;
    writer.os = &os;
    const FPDF_BOOL ok = FPDF_SaveAsCopy(doc, &writer.fw, 0);
    FPDFFont_Close(font);
    FPDF_CloseDocument(doc);
    os.close();
    if (!ok) {
        err = "failed to write the PDF";
        return false;
    }
    return true;
}

}  // namespace traverserpt
