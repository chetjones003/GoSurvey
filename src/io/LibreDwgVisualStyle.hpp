#pragma once

#include "CadEntities.hpp"

struct _dwg_struct;

/// Caches VISUALSTYLE handles during R2007+ DWG export (issue #624 / REQ-371).
struct DwgExportVisualStyleContext {
  bool enabled = false;
};

void DwgExportVisualStyleContextInit(DwgExportVisualStyleContext* ctx, bool r2007OrNewer);

/// Read paper-space VIEWPORT visual style (opaque `Dwg_Entity_VIEWPORT*`).
[[nodiscard]] VisualStyle DwgImportVisualStyleFromViewport(_dwg_struct* dwg, const void* viewportEntity);

/// Attach VISUALSTYLE handle to exported paper VIEWPORT (`Dwg_Entity_VIEWPORT*`).
void DwgExportSetPaperViewportVisualStyle(_dwg_struct* dwg, DwgExportVisualStyleContext* ctx,
                                          void* viewportEntity, VisualStyle style);
