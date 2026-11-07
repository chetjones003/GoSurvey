# TASK — Restore a visible terrain surface, viewport styles, and useful selected-object Properties

## Bug report (confirmed by user)
- **Title:** Showcase surface, viewport styles, and Properties panel don’t behave as expected.
- **Observed:** The mountain drawing shows survey points without a usable surface; changing Wireframe to Shaded has no visible effect; selecting objects beyond lines, circles, or text leaves Properties with little or no useful information.
- **Expected:** The mountain drawing contains a usable surface; rendering modes visibly change the model; Properties show useful object-specific data for selected objects.
- **Reproduction:** Open the showcase drawings, inspect the mountain drawing, change the View rendering mode, then select non-line/circle/text objects and inspect Properties.
- **Constraints:** Root cause first, no speculative code, preserve existing behavior. Automated tests/build only as authorized by developer/user instructions.

## Authority
- REQ-064: visual styles draw geometry differently (hidden depth, shaded triangle faces and lighting).
- REQ-068/069/070 and REQ-083/086: drawing-owned TIN surfaces can be built from imported survey points or source files, displayed with a surface style, and preserved through rebuild.
- REQ-057: 3D coordinates through model, IO and Properties.
- REQ-313: primitive solid objects and their 3D rendering.
- REQ-507: user-approved object-specific Properties for every selected entity kind.
- CON-06: smallest sufficient changes.

## Evidence and root cause
- The existing sample generator imported the 500 survey observations but wrote only DXF contours/markers; it never created a GoSurvey surface object. GoSurvey's DXF importer skips `3DFACE`, so the authored terrain faces were discarded.
- The primitive gallery contains wireframe DXF geometry, not GoSurvey B-rep solids. Consequently, there are no faces for Shaded to render or occlude. The renderer already has separate depth and shading paths for actual TINs and solids (REQ-064).
- `EditorProperties.cpp` renders useful geometry for lines, circles, annotations, feature lines, surfaces, splines, and alignments; many other selected entity kinds fall through after General and show a generic read-only message. Survey points only get a generic count note.
- Autodesk Civil 3D 2026 User Guide says the surface style controls points, TIN triangle lines, contours and other components, while AutoCAD's `VSCURRENT` distinguishes wireframe, hidden-line removal and shaded faces. Primary Autodesk sources reviewed: [surface style components](https://help.autodesk.com/cloudhelp/2027/ENU/Civil3D-UserGuide/files/GUID-110F476B-5DA5-4DE9-82CF-E7DB40F6D3E6.htm), [VSCURRENT](https://help.autodesk.com/view/ACD/2026/ENU/?caas=caas%2Fdocumentation%2FCIV3D%2F2014%2FENU%2FfilesACD%2FGUID-7990D01B-F913-497A-8623-631646AF135B-htm.html).
- Product evidence pass: repository/spec inspected; Autodesk official help reviewed; machine has Civil 3D 2026 Object Enabler and AutoCAD 2026/2027 `accoreconsole.exe`, but no full Civil 3D executable. No Civil 3D UI comparison can be run on this machine.

## Plan
1. Regenerate the showcase mountain from the 500 survey observations into a real GoSurvey TIN with a visible surface style; avoid point labels obscuring the terrain where existing controls allow it.
2. Regenerate primitive gallery shapes with GoSurvey solid primitives and the complex piping model with supported pipe runs. Confirm existing visual modes differ on these actual 3D objects; change renderer code only if the reproduction still fails.
3. Extend the Properties panel with concise type-specific geometry/domain summaries for currently unsupported selected entity kinds, while retaining existing editable fields and report paths.
4. Update the showcase README and this task record with verified contents and limitations.

## Verification plan
- Build the app in this worktree. Do not run/add automated tests unless asked by the user/developer. Open regenerated DWGs through GoSurvey and confirm the surface is a real TIN with nonzero triangles and Hidden/Shaded differ on actual faces.
- For the visible Properties UI change, capture the relevant surface and selection states and complete the required Civil 3D comparison-panel review if tools are available.

## Architectural boundary
- Reuse existing surface, primitive-solid, pipe-run, style and selection data models. No new rendering modes, object stores, or property-edit architectures.

## Product documentation review (pre-PR)

- `README.md`: reviewed; added a link and contents summary for the authored showcase.
- `PRIVACY.md`: reviewed; no privacy behavior or data handling changes, so no update needed.
- `resources/wiki/Properties.md`: reviewed and updated to describe the additional object-specific details.
- Public manual: reviewed `gosurveycad-site/docs/properties.html`, the Properties guide linked from gosurveycad.com. Updated it on `docs/properties-object-details`; submitted as [gosurveycad-site PR #4](https://github.com/chetjones003/gosurveycad-site/pull/4). The legacy GitHub Wiki URL redirects to the repository and `hasWikiEnabled=false`; the website manual source is the authoritative public guide.

## Implementation evidence

- Release build: `build.bat` succeeded.
- Showcase generation completed; generation output reports 500 surface points, 912 triangles, seven exported native solids, and four native pipe runs. The pipe catalog reports missing matching elbow fittings, so those runs have no catalog elbows.
- Website documentation change passed its Cloudflare Pages PR check; no review decision or comments were present when checked.
- Automated tests and visible UI screenshot review were not run.
