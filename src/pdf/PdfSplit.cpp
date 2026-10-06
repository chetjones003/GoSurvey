#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "PdfSplit.hpp"

#include "PdfDocument.hpp"

#include <fpdf_edit.h>
#include <fpdf_ppo.h>
#include <fpdf_save.h>
#include <fpdfview.h>

#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

namespace pdfview {

namespace {

std::string Trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
    ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
    --b;
  return s.substr(a, b - a);
}

// A whole token of digits -> value; false on anything else (sign, letters, empty, too long to fit).
bool ParseNumber(const std::string& s, long long& out) {
  if (s.empty() || s.size() > 9)
    return false;
  long long v = 0;
  for (char c : s) {
    if (c < '0' || c > '9')
      return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

struct FileWriter : FPDF_FILEWRITE {
  std::ofstream out;
  bool failed = false;
};

int WriteBlock(FPDF_FILEWRITE* self, const void* data, unsigned long size) {
  auto* w = static_cast<FileWriter*>(self);
  w->out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  if (!w->out)
    w->failed = true;
  return w->failed ? 0 : 1;
}

} // namespace

PageListResult ParsePageList(const std::string& text, int pageCount) {
  PageListResult r;
  if (Trim(text).empty()) {
    r.error = "type the pages to keep, for example 1-5, 9, 12-20";
    return r;
  }
  size_t start = 0;
  for (;;) {
    size_t comma = text.find(',', start);
    const bool last = comma == std::string::npos;
    if (last)
      comma = text.size();
    const std::string tok = Trim(text.substr(start, comma - start));
    start = comma + 1;
    if (tok.empty()) {
      r.pages.clear();
      r.error = "there is an empty entry between commas";
      return r;
    }
    const size_t dash = tok.find('-');
    long long a = 0, b = 0;
    bool ok;
    if (dash == std::string::npos) {
      ok = ParseNumber(tok, a);
      b = a;
    } else {
      ok = ParseNumber(Trim(tok.substr(0, dash)), a) && ParseNumber(Trim(tok.substr(dash + 1)), b);
    }
    if (!ok) {
      r.pages.clear();
      r.error = "\"" + tok + "\" is not a page number or a range such as 3-7";
      return r;
    }
    if (a < 1 || b < 1 || a > pageCount || b > pageCount) {
      r.pages.clear();
      r.error = "\"" + tok + "\" is outside this file, which has pages 1 to " + std::to_string(pageCount);
      return r;
    }
    if (b < a) {
      r.pages.clear();
      r.error = "the range \"" + tok + "\" runs backwards; write it as " + std::to_string(b) + "-" + std::to_string(a);
      return r;
    }
    for (long long p = a; p <= b; ++p)
      r.pages.push_back(static_cast<int>(p - 1));
    if (last)
      break;
  }
  return r;
}

std::string SplitPdf(const std::filesystem::path& source, const std::vector<int>& pages,
                     const std::filesystem::path& dest, std::atomic<int>* progress) {
  if (progress != nullptr)
    progress->store(0);
  if (pages.empty())
    return "no pages were chosen";
  std::error_code ec;
  if (source.lexically_normal() == dest.lexically_normal() ||
      (std::filesystem::exists(dest, ec) && std::filesystem::equivalent(source, dest, ec)))
    return "the new file must not replace the original; choose a different name";

  // The source is read into memory and closed again at once, so it is never held open or written.
  std::string bytes;
  {
    std::ifstream in(source, std::ios::binary);
    if (!in)
      return "cannot read the original file";
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  if (bytes.empty())
    return "the original file is empty";

  const std::filesystem::path tmp = dest.parent_path() / (dest.filename().string() + ".gssplit.tmp");
  std::string error;
  {
    std::lock_guard<std::recursive_mutex> lock(PdfiumMutex());
    FPDF_InitLibrary();
    FPDF_DOCUMENT src = FPDF_LoadMemDocument(bytes.data(), static_cast<int>(bytes.size()), nullptr);
    if (src == nullptr)
      return "the original file could not be read as a PDF";
    const int n = FPDF_GetPageCount(src);
    for (int p : pages)
      if (p < 0 || p >= n) {
        FPDF_CloseDocument(src);
        return "page " + std::to_string(p + 1) + " is not in the original file";
      }
    FPDF_DOCUMENT out = FPDF_CreateNewDocument();
    if (out == nullptr) {
      FPDF_CloseDocument(src);
      return "could not start the new PDF";
    }
    // One page at a time so a long split can report progress; each lands after the previous one.
    for (size_t i = 0; i < pages.size() && error.empty(); ++i) {
      const int idx = pages[i];
      if (!FPDF_ImportPagesByIndex(out, src, &idx, 1, static_cast<int>(i)))
        error = "page " + std::to_string(idx + 1) + " could not be copied";
      else if (progress != nullptr)
        progress->store(static_cast<int>(i) + 1);
    }
    if (error.empty()) {
      FileWriter w;
      w.version = 1;
      w.WriteBlock = &WriteBlock;
      w.out.open(tmp, std::ios::binary | std::ios::trunc);
      if (!w.out) {
        error = "cannot write beside the chosen file name";
      } else {
        const bool saved = FPDF_SaveAsCopy(out, &w, 0) != 0;
        w.out.close();
        if (!saved || w.failed || !w.out)
          error = "writing the new PDF failed (disk full or no permission?)";
      }
    }
    FPDF_CloseDocument(out);
    FPDF_CloseDocument(src);
  }
  if (!error.empty()) {
    std::filesystem::remove(tmp, ec);
    return error;
  }
  std::filesystem::rename(tmp, dest, ec); // replaces an existing dest on Windows and POSIX
  if (ec) {
    std::error_code ec2;
    std::filesystem::remove(tmp, ec2);
    return "could not put the new file in place: " + ec.message();
  }
  return {};
}

} // namespace pdfview
