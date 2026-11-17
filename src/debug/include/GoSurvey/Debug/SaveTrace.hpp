#pragma once

/// Temporary Save-As breadcrumb log (%TEMP%\\gosurvey-save-trace.log + VS Output window).
/// Used while diagnosing the File → Save As crash (issue #167 follow-up).
void ClearSaveTrace();
void AppendSaveTrace(const char* step);
