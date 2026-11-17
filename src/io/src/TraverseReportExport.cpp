#include <GoSurvey/IO/TraverseReportExport.hpp>
#include <GoSurvey/Pdf/PdfDocument.hpp>
#include "io/PlotFont.hpp"

#include <fpdf_edit.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;
namespace traverserpt {

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
    std::lock_guard<std::recursive_mutex> lock(pdfview::PdfiumMutex());
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
