# TASK: Issue #993 â€” AutoCAD validity for R2010/R2018 DWG

## Authority

- Issue #993; accepted REQ-170 in `spec/requirements.md` (DWG save through R2018 and AutoCAD opens emitted entity sets without Recover).
- Constraints: identify and prove root cause before implementation; preserve R2000/R2004 output and R2010/R2013/R2018 version selection; no format redesign.

## Bug report

- Observed: AutoCAD rejects minimal R2010 and R2018 exports as invalid (status 434), while R2000 opens.
- Expected: each selected DWG version opens in AutoCAD without a Recover prompt.
- Repro: hidden `[.oracle768]` fixture writer emits level-0 files for each version; run them through `accoreconsole.exe`.

## Plan

1. Build beta in this task worktree and reproduce with the existing level-0 oracle fixtures and installed AutoCAD Core Console.
2. Inspect the encoded files and compare a passing R2000 file with failing newer versions; isolate the exact codec/header/section field causing rejection.
3. Write a minimal regression test that fails on current beta and proves the identified invariant.
4. Implement only the root-cause fix, preserving all selectable versions and R2000 behavior.
5. Re-run the focused tests, AutoCAD oracle at all five feature levels for R2000/R2010/R2018, and the required build/test verification.

## Verification plan

- `build.bat` Release.
- `GoSurveyTests.exe "[.oracle768]"` to create fixture files under `build/artifacts/oracle993/`.
- AutoCAD Core Console validation of level-0 R2000/R2010/R2018, then all fixture levels if the root cause is resolved.
- Relevant `[dwg][req170]` tests and CTest; full suite after the build.
- Self-review with build-project, testing, code-review, and architecture-review checklists.

## Root cause

The remaining failures are caused by these codec layout errors:

1. **R2010 CLASSES trailer.** The writer applies the legacy four-byte `0x0DCA`
   trailer to R2010/R2013. Native R2010 uses eight reserved zero bytes. A
   16-combination AutoCAD experiment isolates the trailer change as sufficient
   to make the level-0 R2010 export open and audit with zero errors. Header marker,
   section-size, and class hsize changes are unnecessary for that export.
2. **R2010+ MULTILEADER common tail.** `dwg2.spec` incorrectly gates the block-label
   list and the text-direction/alignment/justification/scale fields with the
   R2007-only arrowhead list. A native AutoCAD R2018 leader stores those common
   fields after `is_annotative` and before `attach_dir`. Parsing them there gives
   zero block labels, negative-text-direction 0, IPE alignment 0, justification 1,
   scale 1, then attachment values 0/9/9 and extended-text flag 1, ending exactly
   at the string stream. The old decoder instead reads attachment values 0/32/4786
   and leaves nine unexplained bits before the string stream. A regression using
   nondefault common-tail fields fails before the correction (10 failed checks,
   including the constructor's zero default global scale).
3. **MULTILEADER node vectors.** The DWG record contains the connection-point and
   dogleg vectors even when their validity flags are clear. The codec incorrectly
   omits them in that case. Correcting the vectors alone or the common tail alone
   still fails AutoCAD; correcting both makes the full R2010/R2018 level-4 fixture
   open and audit with zero errors. DXF keeps its existing conditional groups.

The level-1+ oracle also identifies TEXT entities with zero width factor; AutoCAD
AUDIT repairs each to 1. The TEXT constructor leaves this required positive field
at calloc's zero value. This is independent of the container rejection. The constructor now initializes
width to 1, and MULTILEADER global scale likewise starts at 1.

### Ruled-out diagnostic changes

- System-page and encrypted file-header checksums already match in rejected and
  accepted files; they are not changed.
- Preview capacity, AppInfo/AppInfoHistory, header markers, physical ordering,
  class hsize, and section-size experiments were not retained.
- Literal-only compression, native leader class metadata, and node-flag changes
  do not resolve the level-4 rejection.
- An unconditional leader-node vector change was insufficient on its own. It is
  retained together with the proven common-tail correction after the combined
  AutoCAD test passed; diagnostic node-flag/geometry changes were removed.
- Initial class/style-only probes accidentally exercised the exporter's legacy
  LEADER fallback. Repeating them while suppressing that fallback proves class
  declarations and MLEADERSTYLE alone open successfully; the rejection follows
  the native MULTILEADER record.

## Historical investigation log (before the final fix)

- Built the beta-based worktree with `build.bat` successfully during the investigation. The build
  emitted a non-blocking `vswhere.exe` not recognized message from the environment setup.
- Reproduced R2010 `ErrorStatus=53` with AutoCAD 2025 Core Console. R2000 and R2018 controls open;
  the accepted R2010 control audits with zero errors.
- Tested preview capacity, header markers, AppInfo/AppInfoHistory, pre-2018 section ordering, and
  object-stream trailing bits in disposable diagnostic fixtures. None resolved the R2010 rejection.
  Those temporary source changes have been removed.
- Isolated test of the page-header-size hypothesis: restoring beta source and changing only
  `page_hdr[3]` from the physical page extent to decompressed `content_size` still produced
  `ErrorStatus=53` in AutoCAD. This hypothesis is falsified as a sufficient fix; the source is
  restored to beta.
- Isolated Preview-capacity test: changing only the R2007â€“R2010 Preview capacity from `0x4a000`
  to the native-control value `0x9800` reduced the R2010 oracle file from about 310 KB to 46 KB,
  but AutoCAD still returned `ErrorStatus=53`. The capacity mismatch is not a sufficient fix.
- Isolated R2010 file-header marker test: emitting the accepted control's marker values
  (`maint_version=255`, `app_dwg_version=33`, `app_maint_version=255`, `maint_rel_version=226`)
  changed the generated header to match the control, but AutoCAD still returned `ErrorStatus=53`.
  The source was restored to beta after the experiment.
- Header CRC comparison: LibreDWG recalculates each tested R2010 file's encrypted file-header
  CRC to the stored value successfully, including the rejected GoSurvey output and a rejected
  LibreDWG rewrite of the accepted AutoCAD control. The remaining CRC failure is therefore not
  this file-header CRC.
- Diagnostic test code used for the comparisons was removed; there are no source changes to the
  writer or tests.
- `build.bat release` passed. Regenerated all five levels for R2000/R2010/R2018 with the hidden
  oracle writer; fixture test passed (17 assertions). The regenerated R2010 level-0 file is still
  rejected. AutoCAD 2025 Core Console is installed at
  `C:\Program Files\Autodesk\AutoCAD 2025\accoreconsole.exe`.
- Remaining work before closure: identify the exact invalid R2010 container/header/section invariant,
  add a regression test for it, implement and validate the fix, and run the full oracle levels/build/
  test plan above. Issue #993 remains open until AutoCAD accepts and audits R2010 output.

## Final implementation

- `third_party/libredwg/src/encode.c`: use the eight-byte reserved CLASSES trailer
  for R2010+; leave R2004's legacy trailer and the existing R2018 framing intact.
- `third_party/libredwg/src/dwg2.spec`: correct the shared MULTILEADER encoder and
  decoder layout for the node vectors and common tail.
- `third_party/libredwg/src/dwg_api.c`: initialize TEXT width and MULTILEADER scale
  to their valid unit defaults.
- `tests/LibreDwgTests.cpp`: decompress CLASSES and inspect its sentinel/trailer
  directly, independently of the reader's ignored trailer. Covers R2004/R2010/
  R2013/R2018; fails beta on the R2010 four-byte trailer (20 bytes after the
  sentinel start instead of 24).
- `tests/LibreDwgCadTests.cpp`: verify TEXT defaults and nondefault MULTILEADER
  common-tail fields, attachment fields, and stored node vectors with clear flags.
- `third_party/libredwg/VENDORED.md`: record both vendor corrections.
- All temporary environment switches, native-control copying, traces, and
  diagnostic tests were removed. Diagnostic artifacts remain ignored under
  `build/artifacts/oracle993/`.

## Final verification evidence

- Windows MSVC Release build: PASS (`build-final-verified.log`).
- Focused `[issue993]`: PASS, 110 assertions in 3 cases (`regression-final.log`).
  The regressions were observed failing before their respective corrections.
- Oracle fixture writer: PASS, 17 assertions (`fixtures-final.log`).
- AutoCAD **2025** Core Console, ordinary `/i` open followed by AUDIT (no Recover):
  **all ten R2010/R2018 fixtures, levels 0–4, report 0 errors and 0 fixes**.
  Includes paper layouts, viewports, hatch, MULTILEADER, and table content.
- R2000 regression oracle: all five fixtures open. Levels 0–3 audit clean;
  level 4 retains an existing legacy LEADER annotation warning
  (`annotation id Bad fcf id`, annotation disabled). This warning was present
  before the fix and is outside #993's R2010/R2018 rejection.
- Oracle summary: `build/artifacts/oracle993/verified-oracle-results.txt`;
  individual logs: `verified-L<level>-R<version>.log`.
- Full CTest execution: in progress; final result will be appended below.
- Supplemental investigation found R2004/R2013 Core Console rejections as well;
  those were not the versions reported by #993 and remain separate compatibility
  work. No claim is made that the entire REQ-170 version matrix is resolved.

## Product documentation review

- `README.md`: reviewed; no update needed for this codec correctness fix.
- `PRIVACY.md`: reviewed; no data handling change.
- `resources/wiki/`: reviewed the DWG exchange/FAQ/UI descriptions; no new option
  or workflow is introduced by this fix. Existing converter-era descriptions are
  stale independently of this task.
- Public GoSurvey manual wiki: cloned read-only to the artifact directory and
  reviewed the same DWG exchange/FAQ/UI descriptions. The same existing stale
  converter descriptions are outside this binary-format correction; no external
  publication was made.

## Scope and architecture review

The fix remains in the existing vendored codec. There is no new dependency,
converter requirement, version downgrade, file-format redesign, UI surface,
thread, global state, or architectural boundary change. Regression tests use
owned buffers and the existing in-process decompressor. Runtime work adds only
required serialized fields and scalar default assignments; no hot rendering or
compute path changes and no performance claim is made.
