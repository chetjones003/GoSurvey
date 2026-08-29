# TASK-141 — Civil 3D Home + Feature Line chrome (D-2026-08-29-a)

- Type:    feature
- Status:  done
- Opened:  2026-08-29
- Owner:   Workshop

## 1. Authority
- Goal:         Civil 3D-shaped Home / Feature Line UI (user screenshots)
- Requirements: REQ-154 (from-objects), REQ-087/088 (entity + elev editor), REQ-084 (NYI leftovers)
- Constraints:  D-2026-08-28-f (no Object Viewer); D-2026-08-28-d/k/l (chrome, no invented objects)
- Acceptance:   D-2026-08-29-a — Home panels; Feature Line flyout; contextual Feature Line tab;
                Create Feature Lines dialog submits FEATURELINESFROMOBJECTS
- Owning subsystem: UI (`CadUi.cpp`, `CadUi_FeatureLineElev.cpp`); session flags on `AppCommandState`

## 2. Scope
- In scope: Home restyle, Feature Line dropdown, contextual tab, create dialog
- Out of scope: alignment/corridor/stepped offset; labels; Drive; weed points; Object Viewer
- Smallest change: session-only contextual tab (same as TIN / SURVEY Point)

## 3. Architectural boundary check
- [x] No — no new entity types, no prefs slot for the contextual tab

## 6. Plan
- Home panels + Feature Line popup; contextual tab; Create Feature Lines dialog; prefs clamp

## 8. Implementation log
- 2026-08-29 implemented D-2026-08-29-a chrome
