/// Bring \ref AppCommandState::surfaceDisplayCache and \ref AppCommandState::surfaceDisplayGeometry
/// up to date — ADR-036 (e). Called once a frame, beside \ref TickSurfaceRebuilds.
///
/// Generates each style component a surface asks for — triangle edges, border, minor and major
/// contours — and leaves the buffer for a component that is switched off empty, so REQ-070's "a style
/// with triangles off and contours on draws only contours" is a property of what exists rather than
/// of what the renderer remembers to skip.
///
/// Regenerates an entry only when its staleness key `(triangulation pointer, resolved style)` has
/// moved, and **returns before allocating** when nothing has: at REQ-100's ~200k-triangle profile the
/// generation walks 600k edges, so an early-out placed after a `clear()` would still cost the frame
/// it was written to save (§11 invariant 7). Entries whose surface id no longer resolves are reaped
/// in the same pass, so an erased surface's geometry does not outlive it.
///
/// The key does **not** include the surface's definition, and that is the mechanism behind REQ-070's
/// central claim: changing a contour interval moves the style half of the key and nothing else, so
/// the triangulation is never rebuilt and the `shared_ptr` it hangs from is never even touched.
///
/// The batch assembly at the end is redone every call — it copies pointers and colours, never
/// vertices — because layer visibility and object isolation change with no surface geometry changing
/// at all.
void RefreshSurfaceDisplayGeometry(AppCommandState& st);

/// Border edges of surface \p surfaceIndex from the cache, or nullptr when it has none (never built,
/// or the cache has not caught up this frame). Six floats per segment.
/// Contour segments currently held in the display cache, across every surface (REQ-100 profile (c)).
///
/// For the BENCH record: profile (c) is defined as a CONTOURED surface, so "how many contour
/// segments were on screen" is part of what makes one run comparable with the next.
[[nodiscard]] int SurfaceDisplayContourSegs(const AppCommandState& st);

/// True when surface \p surfaceIndex asked for more contour levels than the display path generates,
/// with \p levelsAsked filled in — REQ-201, so "no contours" is never left to look like a defect.
///
/// The display pass runs once a frame with no command in flight and nowhere to log, so it records the
/// fact on the cache entry and the Surface Manager is what says it out loud.
[[nodiscard]] bool SurfaceContoursSuppressed(const AppCommandState& st, size_t surfaceIndex,
                                             int* levelsAsked);

[[nodiscard]] const std::vector<float>* SurfaceBorderEdges(const AppCommandState& st, size_t surfaceIndex);

/// Is surface \p surfaceIndex visible right now — built, on a layer that is on and not frozen, and
/// not isolated out (REQ-068, REQ-084 (d))?
///
/// **One rule, three readers.** Drawing (\ref AppendSurfaceEdgeLines), picking
/// (\ref PickClosestCadEntity) and the REQ-074 elevation readout each need it, and before this
/// existed the first two carried hand-copied versions that had already drifted apart: neither
/// consulted `hiddenEntityIds`, so an isolated-out surface stayed on screen. The point of a shared
/// predicate is that "invisible" and "unclickable" cannot disagree — which is exactly what REQ-084
/// (d) requires of every entity kind.
[[nodiscard]] bool SurfaceVisible(const AppCommandState& st, size_t surfaceIndex);


/// REQ-075: "a surface that is out of date or rebuilding is shown as such, and the state clears when
/// the rebuild lands." Shared by the Surface Manager and the Volume Dashboard (TASK-095) — both need
/// the identical current/stale/rebuilding classification for a surface, and it has no UI
/// dependency, so it lives here rather than in either panel.
enum class SurfaceState : std::uint8_t { Current, Stale, Rebuilding };

/// \p surfaceIndex's current/stale/rebuilding state. Both non-`Current` states are already knowable
/// without any new bookkeeping: a rebuild in flight is an entry in \c surfaceRebuildAsync, and
/// out-of-date is REQ-069's own dirty check (\c builtAtRevision vs \c cadGpuRevision).
[[nodiscard]] SurfaceState SurfaceRebuildStateOf(const AppCommandState& st, size_t surfaceIndex);

/// The rollover readout for plan position (\p x, \p y) — one row per **visible surface covering it**
/// (REQ-089). \p out is cleared first; an empty result means "no surface here", which is the signal
/// to show nothing at all.
///
/// Covering is decided per triangle by \ref TinElevationAt, which is what makes REQ-089's "outside
/// the border, in a concave notch, or inside a hide-boundary void shows no readout" true by
/// construction rather than by three separate tests: there is simply no triangle in any of those
/// places, so the surface produces no row.
///
/// **Not cheap** — the query walks every triangle of every visible surface. REQ-089 makes calling it
/// once per cursor rest an acceptance condition for exactly that reason; see `util/hoverdwell.hpp`.
void BuildSurfaceHoverRows(const AppCommandState& st, double x, double y,
                           std::vector<SurfaceHoverRow>* out);

/// REQ-127: interpolated Z of the last covering visible surface at plan (x,y), or false if none.
[[nodiscard]] bool SurfaceSnapElevation(const AppCommandState& st, double x, double y, float* outZ);

/// Index of the surface named \p name (case-insensitive), or -1 (REQ-068).
///
/// For a name the **user** typed — a command argument, a panel selection. For a reference held
/// across time (an in-flight rebuild, a cross-object link), use \ref FindSurfaceIndexById: a name
/// can be changed, and an erased name can be taken by a different surface.
[[nodiscard]] int FindSurfaceIndex(const AppCommandState& st, const std::string& name);
/// REQ-497: the elevation reader for a built surface, in drawing-local coordinates; null when the surface is not built.
[[nodiscard]] std::unique_ptr<ISurfaceQuery> MakeSurfaceQueryForCommands(const AppCommandState& st, const CadSurface& surf);
/// REQ-512: atomically move selected block references to the sampled surface elevations.
bool MoveSelectedBlockRefsToSurface(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>& log);
/// REQ-508: select viewport triangle detail for one surface; records one undoable drawing change.
bool SetSurfaceDisplayLod(AppCommandState& st, size_t surfaceIndex, bool reduced);
/// REQ-511: add a readable model-space image attachment with stable identity and drawing placement.
bool AttachModelRasterImage(AppCommandState& st, const std::string& path, float x, float y,
                            float width, float height, float rotationRad, std::vector<std::string>& log);
/// REQ-511: associate a model-space image with a built TIN for clipped terrain draping.
bool SetModelRasterDrape(AppCommandState& st, size_t imageIndex, size_t surfaceIndex,
                         std::vector<std::string>& log);

/// Index of the surface whose stable entity id is \p id, or -1 when it does not resolve — erased, or
/// never assigned (REQ-076 / ADR-036 (a)). `id == 0` always returns -1: 0 means "unassigned", so
/// matching on it would resolve to whichever surface happened not to have been swept yet.
[[nodiscard]] int FindSurfaceIndexById(const AppCommandState& st, std::uint64_t id);

/// (Re)build \p surface's triangulation from its source point groups (REQ-068).
///
/// Replaces the shared pointer wholesale rather than writing through it — the condition the
/// architecture §11.5 sharing exemption rests on. Reports what it did and, on failure, leaves the
/// surface's previous triangulation untouched rather than half-replacing it (REQ-001).
///
/// Returns true when a surface was produced.
bool BuildSurfaceFromSources(AppCommandState& st, CadSurface& surface, std::vector<std::string>& log);
/// Refresh linked legend rows for surfaces using an edited style's analysis configuration.
void RefreshSurfaceLegendTablesForStyle(AppCommandState& st, const std::string& styleName);
/// Refresh linked entities and analysis results after a project-owned surface reference replaces its TIN.
void RefreshSurfaceDependentsAfterReplacement(AppCommandState& st, size_t surfaceIndex,
                                              std::vector<std::string>& log);
/// Refresh saved surface profiles after an alignment geometry or stationing edit.
void RefreshSurfaceProfilesForAlignment(AppCommandState& st, std::uint64_t alignmentId,
                                        std::vector<std::string>& log);

/// Re-lay everything linked to surface p si, after that surface has been rebuilt (ADR-065 (b),
/// GitHub #150). Called from the only places a TIN is replaced - `SURFACEREBUILD` and the async
/// reap - so a link means the same thing whichever way the rebuild was driven.
///
/// A linked entity whose vertices are no longer over the new ground is LEFT WHERE IT IS and said
/// so: a surface shrinking under a draped line is not a reason to move or discard the line
/// (REQ-201). Its link is kept, so it re-drapes on its own once the surface covers it again.
void ReDrapeLinkedToSurface(AppCommandState& st, size_t si, std::vector<std::string>& log,
                            bool invalidateCache = true);

/// The name of the surface p e is draped on and follows, or empty when it follows none
/// (ADR-065 (d), GitHub #150) - never linked, baked since, or linked to a surface that has been
/// erased, which are the same thing to the user. The one place the link becomes something a
/// person reads, so the Properties panel and the `DRAPELINKS` report cannot disagree.
[[nodiscard]] std::string DrapedOnSurfaceName(const AppCommandState& st, const SelectedEntity& e);

/// Create a named surface from \p groupNames and build it. Returns the new surface's index, or -1
/// when the name is taken or the build produced nothing.
int CreateSurfaceFromPointGroups(AppCommandState& st, const std::string& name,
                                 const std::vector<std::string>& groupNames,
                                 std::vector<std::string>& log);

/// REQ-136: named TIN volume surface (comparison minus base). Returns the new index, or -1.
int CreateSurfaceFromVolumeParents(AppCommandState& st, const std::string& name, const std::string& baseName,
                                   const std::string& comparisonName, std::vector<std::string>& log);

/// If more than one surface draws with this surface's resolved style, copy that style to a unique
/// name and point only this surface at the copy. Named-style sharing (REQ-070) stays available via
/// SURFSTYLE ASSIGN. Returns true when a copy was made.
bool DetachSurfaceStyleIfShared(AppCommandState& st, size_t surfaceIndex, std::vector<std::string>* log);

/// Erase a surface by index, keeping its attribute array in step. Callers own the undo snapshot.
void EraseSurfaceAtIndex(AppCommandState& st, size_t index);

/// REQ-069's dynamic rebuild. Called once per frame (main.cpp, beside \ref EnsureEntityIds — both
/// are revision-gated per-frame maintenance run before anything can save, reference or render a
/// surface). Reaps any completed background rebuild — applying it if \c cadGpuRevision has not moved
/// since it was dispatched, discarding it otherwise (architecture §8 rule 4: undo or a further edit
/// while it ran) — then dispatches a fresh background rebuild for every surface whose
/// \c builtAtRevision is behind the current revision and does not already have one in flight, which
/// is what makes a single command that touches N points, or N surfaces, coalesce to at most one
/// rebuild per surface rather than N.
void TickSurfaceRebuilds(AppCommandState& st, std::vector<std::string>& log);

/// Advances the Volume Dashboard's own live recompute (REQ-073 amendment, TASK-095). Call every
/// frame, after \ref TickSurfaceRebuilds so a surface that finished rebuilding this frame is already
/// current when this checks it.
void TickVolumeDashboard(AppCommandState& st);

/// REQ-074: pick a point for its interpolated surface elevation; pick a second for the grade
/// between them. Reports every surface covering the pick, by name.
void StartSurfaceElevGradeCommand(AppCommandState& st, std::vector<std::string>& log);
void StartWaterDropCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartCatchmentCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfSwapEdgeCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfAddPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfDelPointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfMovePointCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartSurfDelLineCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);
void StartQuickProfileCommand(AppCommandState& st, const std::string& surfaceName, std::vector<std::string>& log);

/// REQ-069: designate one picked Line/Polyline as a breakline on the named surface, appended to its
/// definition by stable entity id. Refuses to start when \p surfaceName does not name an existing
/// surface, reported before the pick rather than after it (REQ-201).
void StartDesignateBreaklineCommand(AppCommandState& st, const std::string& surfaceName,
                                    std::vector<std::string>& log);
void StartDesignateContourCommand(AppCommandState& st, const std::string& surfaceName,
                                  std::vector<std::string>& log);

/// REQ-069: designate one picked CLOSED Polyline as a boundary ring of kind \p kind on the named
/// surface, applied in the order it was added. Same refuse-before-pick rule as above.
void StartDesignateBoundaryCommand(AppCommandState& st, const std::string& surfaceName, CadBoundaryKind kind,
                                   std::vector<std::string>& log);
