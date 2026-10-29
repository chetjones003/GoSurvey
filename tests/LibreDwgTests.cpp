// REQ-170 increment 1: LibreDWG is compiled into the test binary and can write/read R2004.

#include "DwgIo.hpp"
#include "LibreDwg.hpp"

#if defined(__cplusplus) && !defined(restrict)
#define restrict
#endif
extern "C" {
#include <dwg.h>
#include "../third_party/libredwg/src/bits.h"
#include "../third_party/libredwg/src/decode.h"
}

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" unsigned int loglevel;
extern "C" uint32_t dwg_section_page_checksum(uint32_t seed, Bit_Chain* dat, int32_t size, bool skip_checksum);
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace {

struct ScratchDir {
  std::filesystem::path path;
  explicit ScratchDir(const char* tag) {
    static int counter = 0;
    path = std::filesystem::temp_directory_path() /
           ("gosurvey-libredwg-" + std::string(tag) + "-" + std::to_string(++counter));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    std::filesystem::create_directories(path, ec);
  }
  ~ScratchDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  ScratchDir(const ScratchDir&) = delete;
  ScratchDir& operator=(const ScratchDir&) = delete;
};

}  // namespace

TEST_CASE("LibreDwgPackageVersion is the pinned 0.14 release", "[dwg][libredwg]") {
  const std::string v = LibreDwgPackageVersion();
  REQUIRE_FALSE(v.empty());
  REQUIRE(v.find("0.14") != std::string::npos);
}

TEST_CASE("LibreDwgWriteMinimalR2000 refuses an empty path", "[dwg][libredwg]") {
  REQUIRE_FALSE(LibreDwgWriteMinimalR2000(nullptr));
  REQUIRE_FALSE(LibreDwgWriteMinimalR2000(""));
}

TEST_CASE("LibreDwgReadVersionName refuses a missing file", "[dwg][libredwg]") {
  REQUIRE(LibreDwgReadVersionName("Z:/gosurvey-no-such-file.dwg").empty());
}

TEST_CASE("LibreDWG UMC encoding uses canonical unsigned continuation bytes",
          "[dwg][libredwg][bits]") {
  const struct Row {
    BITCODE_UMC value;
    std::array<unsigned char, 10> bytes;
    std::size_t length;
  } rows[] = {
      {0x3f, {0x3f}, 1},
      {0x40, {0x40}, 1},
      {0x41, {0x41}, 1},
      {0x7f, {0x7f}, 1},
      {0x80, {0x80, 0x01}, 2},
      {0x4000, {0x80, 0x80, 0x01}, 3},
      {0xffffffffffffffffULL, {0xff, 0xff, 0xff, 0xff, 0xff,
                    0xff, 0xff, 0xff, 0xff, 0x01}, 10},
  };
  for (const Row& row : rows) {
    std::array<unsigned char, 10> storage{};
    Bit_Chain chain{};
    chain.chain = storage.data();
    chain.size = storage.size();
    bit_write_UMC(&chain, row.value);
    REQUIRE(chain.byte == row.length);
    REQUIRE(std::equal(row.bytes.begin(), row.bytes.begin() + row.length,
                       storage.begin()));
    chain.byte = 0;
    REQUIRE(bit_read_UMC(&chain) == row.value);
    REQUIRE(chain.byte == row.length);
  }
}

TEST_CASE("LibreDWG TU encoders preserve section and object string conventions",
          "[dwg][libredwg][bits]") {
  std::array<BITCODE_RS, 3> text = {'1', ':', '1'};
  std::array<unsigned char, 16> storage{};
  Bit_Chain chain{};
  chain.chain = storage.data();
  chain.size = storage.size();
  bit_write_TU(&chain, text.data());
  REQUIRE(bit_position(&chain) == 74);
  chain.byte = 0;
  chain.bit = 0;
  REQUIRE(bit_read_BS(&chain) == 4);

  storage.fill(0);
  chain.byte = 0;
  chain.bit = 0;
  bit_write_TU_no_null(&chain, text.data());
  REQUIRE(bit_position(&chain) == 58);

  chain.byte = 0;
  chain.bit = 0;
  BITCODE_TU decoded = bit_read_TU(&chain);
  REQUIRE(decoded);
  REQUIRE(decoded[0] == '1');
  REQUIRE(decoded[1] == ':');
  REQUIRE(decoded[2] == '1');
  REQUIRE(decoded[3] == 0);
  free(decoded);
}

TEST_CASE("LibreDwg writes R2000 that the in-process reader and DwgProbe both see",
          "[dwg][libredwg]") {
  ScratchDir dir("r2000");
  const auto dwgPath = dir.path / "line.dwg";
  const std::string pathUtf8 = dwgPath.string();

  REQUIRE(LibreDwgWriteMinimalR2000(pathUtf8.c_str()));
  REQUIRE(std::filesystem::file_size(dwgPath) > 6);

  std::ifstream in(dwgPath, std::ios::binary);
  char magic[7] = {};
  in.read(magic, 6);
  REQUIRE(in.gcount() == 6);
  const std::string tag(magic, 6);
  REQUIRE(tag == "AC1015");
  REQUIRE(DwgVersionName(pathUtf8.c_str()) == "AutoCAD 2000");

  REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == "r2000");
}

// Issue #600: LibreDWG 0.14+ must encode a from-scratch R2004 document (one LINE) and read it back.
TEST_CASE("LibreDwg writes R2004 that the in-process reader sees (issue #600)",
          "[dwg][libredwg][issue600]") {
  ScratchDir dir("r2004");
  const auto dwgPath = dir.path / "line-r2004.dwg";
  const std::string pathUtf8 = dwgPath.string();

  REQUIRE(LibreDwgWriteMinimalR2004(pathUtf8.c_str()));
  REQUIRE(std::filesystem::file_size(dwgPath) > 6);
  std::ifstream in(dwgPath, std::ios::binary);
  char magic[7] = {};
  in.read(magic, 6);
  REQUIRE(in.gcount() == 6);
  REQUIRE(std::string(magic, 6) == "AC1018");
  REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == "r2004");
}

TEST_CASE("LibreDwg writes R2010/R2013/R2018 minimal LINE files (R2018 export path)",
          "[dwg][libredwg][issue600]") {
  const struct Row {
    int libVer;
    const char* acTag;
    const char* readName;
  } rows[] = {
      {R_2010, "AC1024", "r2010"},
      {R_2013, "AC1027", "r2013"},
      {R_2018, "AC1032", "r2018"},
  };
  for (const Row& row : rows) {
    ScratchDir dir(row.acTag);
    const std::string pathUtf8 = (dir.path / "line.dwg").string();
    REQUIRE(LibreDwgWriteMinimalAtVersion(row.libVer, pathUtf8.c_str()));
    std::ifstream in(pathUtf8, std::ios::binary);
    char magic[7] = {};
    in.read(magic, 6);
    REQUIRE(std::string(magic, 6) == row.acTag);
    REQUIRE(LibreDwgReadVersionName(pathUtf8.c_str()) == row.readName);
  }
}

// REQ-170 / issue #993: LibreDWG's reader skips the CLASSES trailer, so an
// ordinary round trip cannot catch the R2010 file AutoCAD rejects with status 53.
TEST_CASE("DWG class sections terminate with the target version trailer",
          "[dwg][libredwg][req170][issue993]") {
  for (const auto version : {R_2004, R_2010, R_2013, R_2018}) {
    CAPTURE(version);
    const std::string scratchTag = "class-trailer-" + std::to_string(version);
    ScratchDir dir(scratchTag.c_str());
    const auto path = dir.path / "line.dwg";
    REQUIRE(LibreDwgWriteMinimalAtVersion(version, path.string().c_str()));
    const auto freeDrawing = [](Dwg_Data* drawing) {
      dwg_free(drawing);
      delete drawing;
    };
    std::unique_ptr<Dwg_Data, decltype(freeDrawing)> drawing(
        new Dwg_Data{}, freeDrawing);
    REQUIRE(dwg_read_file(path.string().c_str(), drawing.get()) < DWG_ERR_CRITICAL);
    const Dwg_Section_Info* classes = nullptr;
    for (unsigned i = 0; i < drawing->header.section_infohdr.num_desc; ++i) {
      const auto& info = drawing->header.section_info[i];
      if (info.fixedtype == SECTION_CLASSES) classes = &info;
    }
    REQUIRE(classes != nullptr);
    REQUIRE(classes->num_sections == 1);
    REQUIRE(classes->sections != nullptr);
    REQUIRE(classes->sections[0] != nullptr);
    REQUIRE(classes->compressed == 2);
    REQUIRE(classes->size > 24);
    REQUIRE(classes->size <= classes->max_decomp_size);
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    const auto address = classes->sections[0]->address;
    REQUIRE(address + 32 <= bytes.size());
    uint32_t header[8];
    std::memcpy(header, bytes.data() + address, sizeof(header));
    for (auto& word : header) word ^= 0x4164536b ^ static_cast<uint32_t>(address);
    REQUIRE(address + 32 + header[2] <= bytes.size());
    std::vector<unsigned char> decoded(classes->max_decomp_size);
    Bit_Chain src{}, dst{};
    src.chain = bytes.data();
    src.byte = address + 32;
    src.size = src.byte + header[2];
    dst.chain = decoded.data();
    dst.size = decoded.size();
    REQUIRE(decompress_R2004_section(&src, &dst) == 0);
    decoded.resize(static_cast<size_t>(classes->size));
    const size_t trailerSize = version >= R_2010 ? 8 : 4;
    // Locate the fixed end sentinel independently of the size/CRC fields.
    const std::array<unsigned char, 16> endSentinel = {
        0x72, 0x5e, 0x3b, 0x47, 0x3b, 0x56, 0x07, 0x3a,
        0x3f, 0x23, 0x0b, 0xa0, 0x18, 0x30, 0x49, 0x75};
    const auto end = std::search(decoded.begin(), decoded.end(),
                                 endSentinel.begin(), endSentinel.end());
    REQUIRE(end != decoded.end());
    REQUIRE(static_cast<size_t>(decoded.end() - end) == endSentinel.size() + trailerSize);
    const auto trailer = end + endSentinel.size();
    if (version >= R_2010) {
      CHECK(std::all_of(trailer, decoded.end(), [](unsigned char b) { return b == 0; }));
    } else {
      CHECK(std::vector<unsigned char>(trailer, decoded.end()) ==
            std::vector<unsigned char>{0xca, 0x0d, 0, 0});
    }
  }
}
