void StartPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log);
/// Start PDF page rasterization on a worker while the dialog remains responsive.
void StartPdfAttachBuild(AppCommandState& st, std::vector<std::string>& log);
/// Called from the viewport when the user clicks to place the PDF attachment.
void SubmitPdfAttachInsertPoint(AppCommandState& st, float wx, float wy, std::vector<std::string>& log);

/// Release draft cache and preview texture; resets command state to idle.
void CancelPdfAttachCommand(AppCommandState& st, std::vector<std::string>& log);
/// Convert snap-line geometry of the PDF underlay at \p pdfIndex into drawing entities on the current layer.
void VectorizePdfAttachmentLines(AppCommandState& st, int pdfIndex, std::vector<std::string>& log);
