# CS-MAP — vendored (headers + prebuilt static lib + compiled dictionaries + US grids)

- Upstream: OSGeo MetaCRS CS-MAP, `https://svn.osgeo.org/metacrs/csmap/trunk/CsMapDev`
  (the GitHub mirrors are years stale; the Subversion trunk is the source of record).
- Pin: trunk **revision 3078** (dictionary `version.txt` = `1502`), retrieved 2026-09-29.
- REQ-358 / ADR-063 / D-2026-09-29-b / D-2026-09-29-d. Licence: BSD 3-Clause (Autodesk, Inc.,
  see `LICENSE`), compatible with the product's GPL-3.0-or-later (D-2026-08-29-g). The installer's
  licence page carries the notice (`installer/License.txt`).
- Only `src/geo/CoordinateSystems.cpp` includes these headers (ADR-063 (c)); CMake adds
  `include/` to that one source, not to any target's interface.

## What is here
- `include/*.h` — CS-MAP's public C headers (`cs_map.h` and what it includes).
- `lib/win-x64/csmap.lib` — MSVC x64 **Release**, `/MD /O2`, built with upstream's own
  `Source/Library.nmk` (`/DWIN32 /DNDEBUG /D_WINDOWS`, C++ files with `/D__CPP__ /EHsc`).
- `dictionaries/` — installer payload, staged by CMake to `build/resources/csmap/`:
  - the compiled dictionaries `*.CSD` (coordinate systems, datums, ellipsoids, geodetic
    transformations and paths, categories, vertical systems);
  - the **United States** grid files only (D-2026-09-29-d), all U.S. National Geodetic Survey,
    public domain, laid out as `GeodeticTransform.CSD` references them (`.\Usa\...`):
    `Usa/Nadcon` (NADCON 2, NAD27 ↔ NAD83), `Usa/Harn` (HARN/HPGN), `Usa/NSRS2007` and
    `Usa/NSRS2011` (horizontal `dsl*` grids + the NADCON 5 Hawaii PA11 grids), and `Usa/Vertcon`
    with `Vertcon.gdc` — CS-MAP's NADCON setup refuses to start without the VERTCON catalogue, even
    for a 2D shift.
  - NOT shipped: other countries' grids (their datum shifts fail with CS-MAP's message), geoid /
    height-only files (`Egm`, `Geoid*`, `GeoidHeight.gdc`, `WW15MGH.GRD`, NSRS `dsv*`).

## Debt
- `ninja-debug` links this Release `/MD` lib (no debug CS-MAP), as LibreDWG does; CS-MAP is plain C
  with a malloc/free API released through `CS_free`, so this works. Expect `LNK4099` on Debug.
- CS-MAP opens files through narrow (ANSI) paths of at most ~250 characters: an install folder the
  code page cannot represent makes the dictionary load fail (reported, not a crash).

## Rebuild the .lib and the dictionaries
1. Fetch trunk (no Subversion client needed; any `svn export` works too):
   `Source/`, `Include/`, `Dictionaries/` (skip `Egm/`, `Usa/Geoid*`, and other large vertical data).
2. In an MSVC x64 developer prompt, in `Source/`: `nmake -f Library.nmk VERSION=145` →
   `lib145/Release64/CsMap.LIB` → copy here as `lib/win-x64/csmap.lib`. Copy `Include/*.h`.
3. Build the dictionary compiler, in `Dictionaries/`:
   `cl /MD /O2 /I..\Include /DWIN32 /DNDEBUG /D_WINDOWS CS_Comp.c /link <CsMap.LIB>`.
4. Compile: `CS_Comp.exe /b <Dictionaries> <out>`. It checks the UK OSTN files exist in `<out>`,
   so copy `OSTN97.TXT` and `OSTN02.txt` there for the compile and delete them afterwards (the UK
   systems that need them are listed but fail with a message when used).
5. Copy `<out>/*.CSD`, `Vertcon.gdc` and the `Usa/` grids named above into `dictionaries/`.
6. Rebuild GoSurvey and run `GoSurveySnapTests "[req358]"` — it checks the category count, the
   Texas systems and an NGS datasheet point, so a dictionary change that moves any of them shows up.
