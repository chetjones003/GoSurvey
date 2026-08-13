#include "DwgMeshConvert.hpp"

#include "DwgIo.hpp"
#include "ProcessRun.hpp"
#include "util/stlimport.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace dwgmesh {
namespace {

namespace fs = std::filesystem;

/// Forward slashes: AutoLISP string literals treat a backslash as an escape, so a Windows path
/// pasted in raw turns "C:\temp" into a tab. Forward slashes are accepted everywhere in AutoCAD.
std::string ForLisp(const fs::path& p) {
  std::string s = p.u8string();
  for (char& c : s)
    if (c == '\\')
      c = '/';
  return s;
}

fs::path MakeWorkDir() {
  std::random_device rd;
  char name[64];
  std::snprintf(name, sizeof(name), "gosurvey_dwg3d_%08x", rd());
  fs::path d = fs::temp_directory_path() / name;
  std::error_code ec;
  fs::create_directories(d, ec);
  return d;
}

} // namespace

bool ConversionAvailable(std::string* whyNotOut) {
  const DwgConverter& c = FindDwgConverter();
  if (!c.available()) {
    if (whyNotOut)
      *whyNotOut = "no DWG converter found. Install AutoCAD, or set GOSURVEY_DWG_CONVERTER to an "
                   "accoreconsole.exe.";
    return false;
  }
  if (c.kind != DwgConverterKind::AutoCadCore) {
    if (whyNotOut)
      *whyNotOut = c.displayName +
                   " can translate DWG to DXF but cannot tessellate 3D solids, which is what a 3D "
                   "model import needs. An installed AutoCAD (accoreconsole) can.";
    return false;
  }
  return true;
}

ConvertResult ConvertDwgToMesh(const std::string& dwgPathUtf8, const modelimport::Options& imp,
                               const ConvertOptions& conv) {
  ConvertResult out;
  std::string whyNot;
  if (!ConversionAvailable(&whyNot)) {
    out.error = whyNot;
    return out;
  }
  const DwgConverter& converter = FindDwgConverter();
  out.converterName = converter.displayName;

  std::error_code ec;
  if (!fs::exists(fs::u8path(dwgPathUtf8), ec)) {
    out.error = "drawing not found: " + dwgPathUtf8;
    return out;
  }

  const fs::path work = MakeWorkDir();
  if (work.empty()) {
    out.error = "could not create a temporary working directory.";
    return out;
  }
  struct Cleanup {
    fs::path dir;
    ~Cleanup() {
      std::error_code e;
      fs::remove_all(dir, e);
    }
  } cleanup{work};

  // Work on a COPY. AutoCAD holds an open drawing with a share-deny lock, and the far more
  // important reason: the conversion EXPLODES the model, and that must never touch the user's file.
  const fs::path copy = work / "model.dwg";
  {
    std::ifstream in(fs::u8path(dwgPathUtf8), std::ios::binary);
    if (!in) {
      out.error = "could not read the drawing (is it open with an exclusive lock?): " + dwgPathUtf8;
      return out;
    }
    std::ofstream cp(copy, std::ios::binary);
    if (!cp) {
      out.error = "could not write a temporary copy of the drawing.";
      return out;
    }
    cp << in.rdbuf();
  }

  const fs::path stlPath = work / "model.stl";
  const fs::path lspPath = work / "conv.lsp";
  const fs::path scrPath = work / "conv.scr";

  {
    // EXPLODE is repeated: a Plant 3D object can yield a block reference whose contents hold the
    // solids, so a single pass does not reach them all. Four is empirically past the fixed point on
    // the reference model and costs nothing when there is nothing left to explode.
    std::ofstream l(lspPath, std::ios::binary);
    l << "(defun say (s) (princ (strcat \"\\n@@\" s)) (princ))\n"
      << "(defun c:GSCONV ( / ss n)\n"
      << "  (setvar \"QAFLAGS\" 1)\n"
      << "  (setvar \"FACETRES\" " << conv.facetRes << ")\n"
      << "  (setq n 0)\n"
      << "  (while (< n 4)\n"
      << "    (setq ss (ssget \"_X\" '((0 . \"ACPP*,INSERT\"))))\n"
      << "    (if ss (command \"_.EXPLODE\" ss \"\"))\n"
      << "    (setq n (1+ n)))\n"
      << "  (setq ss (ssget \"_X\" '((0 . \"3DSOLID\"))))\n"
      << "  (if ss\n"
      << "    (progn\n"
      << "      (say (strcat \"SOLIDS \" (itoa (sslength ss))))\n"
      << "      (command \"_.STLOUT\" ss \"\" \"_Y\" \"" << ForLisp(stlPath) << "\"))\n"
      << "    (say \"SOLIDS 0\"))\n"
      << "  (say \"END\")\n"
      << "  (princ))\n";
  }
  {
    // SECURELOAD 0 is required or AutoCAD silently refuses to load a LISP file from a temp path —
    // silently being the operative word: the script then runs an undefined command and "succeeds".
    std::ofstream s(scrPath, std::ios::binary);
    s << "FILEDIA\n0\nSECURELOAD\n0\n(load \"" << ForLisp(lspPath) << "\")\nGSCONV\nQUIT\nY\n";
  }

  int exitCode = 0;
  const std::vector<std::string> args = {"/i", copy.u8string(), "/s", scrPath.u8string()};
  if (!RunProcessAndWait(converter.exePath, args, work.u8string(), conv.timeoutMs, &exitCode)) {
    out.error = "the converter did not finish within " + std::to_string(conv.timeoutMs / 1000) +
                " s. A very large model may need longer.";
    return out;
  }

  if (!fs::exists(stlPath, ec)) {
    // The most common real cause, and worth naming rather than reporting a bare failure: a drawing
    // whose 3D content is all custom objects that this AutoCAD's enablers cannot explode.
    out.error = "the converter produced no geometry. The drawing may contain no 3D solids, or this "
                "AutoCAD may lack the object enabler for its content (" + out.converterName + ").";
    return out;
  }

  out.model = stl::ImportStlFile(stlPath.u8string(), imp);
  if (!out.model.ok) {
    out.error = "converted geometry could not be read — " + out.model.error;
    return out;
  }
  out.solidCount = out.model.triangleCount();
  out.ok = true;
  return out;
}

} // namespace dwgmesh
