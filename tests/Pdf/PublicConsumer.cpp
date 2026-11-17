#include <GoSurvey/Pdf/Pdf.hpp>

#if __has_include(<commands/CadCommands.hpp>) || __has_include(<font/ShxFont.hpp>)
#error Pdf consumers must not inherit the application's private source root
#endif

int main() {
  pdfview::SnapIndex index;
  pdfview::SnapIndex::Pt point;
  if (index.Nearest(0.f, 0.f, 1.f, point)) return 1;

  // Pull in the annotation implementation, its PDFium import and real font provider.
  float width = 0.f, height = 0.f;
  pdfview::MeasureText("Arial", "Public Pdf consumer", 12.f, width, height);
  return width > 0.f && height > 0.f ? 0 : 2;
}
