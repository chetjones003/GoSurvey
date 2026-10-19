#pragma once
// REQ-431: the reading aids of the Qt least-squares adjustment window - a command editor with
// colours, line numbers and an inline error, and a plan preview with error ellipses. They only
// display; the traverse, the results and the files stay owned by AdjustmentWindow.
#include "traverse/TraverseCalc.hpp"
#include "traverse/TraverseLeastSquares.hpp"
#include <QColor>
#include <QPlainTextEdit>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>
#include <cstdint>

struct AdjustmentEditorPalette {
  QColor background, text, keyword, option, number, comment, fieldCode;
  QColor gutterBackground, gutterText, error;
  QColor planLeg, planPoint, planEllipse, planSide, planCheck;
};

/// Navy on the Dark appearance (REQ-081), white paper on the Light appearance.
AdjustmentEditorPalette AdjustmentPalette(bool dark);

enum class AdjustmentToken : std::uint8_t { Keyword, Option, Number, Comment, FieldCode };
struct AdjustmentSpan { int start = 0; int length = 0; AdjustmentToken kind = AdjustmentToken::Keyword; };

/// Colour classes of one command line. Words that are none of these keep the normal text colour
/// and are not listed. Mirrors the 0.7.4 editor: a whole-line `//` comment, `#` options, command
/// keywords, numbers, and everything from a word that starts with a quote or backtick.
QVector<AdjustmentSpan> ClassifyAdjustmentLine(const QString& line);

class AdjustmentGutter;
class QSyntaxHighlighter;

/// Command text box with a line-number gutter, colours, and one marked error line.
class AdjustmentCodeEdit final : public QPlainTextEdit {
public:
  explicit AdjustmentCodeEdit(QWidget* parent = nullptr);
  void SetPalette(const AdjustmentEditorPalette& palette);
  /// \p line is 1-based; 0 clears the mark.
  void SetError(int line, const QString& message);
  int ErrorLine() const { return m_ErrorLine; }
  const QString& ErrorMessage() const { return m_ErrorMessage; }
  int GutterWidth() const;
  int FirstVisibleLine() const;  ///< 1-based number of the first line the viewport shows
  QWidget* Gutter() const;
  const AdjustmentEditorPalette& Palette() const { return m_Palette; }
protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
private:
  friend class AdjustmentGutter;
  void PaintGutter(QPaintEvent* event);
  void UpdateGutterWidth();
  AdjustmentEditorPalette m_Palette;
  AdjustmentGutter* m_Gutter;
  QSyntaxHighlighter* m_Highlighter;
  int m_ErrorLine = 0;
  QString m_ErrorMessage;
};

/// Plan (north up) of the traverse as the text reads, with 1-sigma error ellipses once an
/// adjustment that matches the text is supplied.
class AdjustmentPlanView final : public QWidget {
public:
  explicit AdjustmentPlanView(QWidget* parent = nullptr);
  void SetPalette(const AdjustmentEditorPalette& palette);
  void SetUnitLabel(const QString& unit);
  /// \p adjustment is null when there is no current successful adjustment.
  void SetTraverse(const TraverseData& data, const LsaResult* adjustment);
  bool HasEnoughStations() const { return m_Stations.size() >= 2; }
  int StationCount() const { return static_cast<int>(m_Stations.size()); }
  int LegCount() const;
  int SideShotCount() const;
  bool HasClosingLeg() const;
  int EllipseCount() const;
  double Scale() const { return m_Scale; }
  QPointF Center() const { return m_Center; }
  void ZoomExtents();
  void ZoomAt(const QPointF& widgetPos, double wheelSteps);
  void PanBy(const QPointF& pixels);
  QPointF ToScreen(double easting, double northing) const;
  /// Ellipses are drawn larger when they would be too small to see; 1 means true size.
  double EllipseFactor() const;
  /// What hovering \p position says, or an empty string when it is not over a station.
  QString TooltipAt(const QPoint& position) const;
protected:
  void paintEvent(QPaintEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  bool event(QEvent* event) override;
private:
  struct Station {
    int id = 0;
    QPointF position;
    bool hasEllipse = false;
    double semiMajor = 0, semiMinor = 0, azimuthDeg = 0;
  };
  struct Segment { QPointF from, to; enum Kind : std::uint8_t { Leg, Closing, Side, Check } kind = Leg; QString label; };
  QPolygonF EllipseOutline(const Station& station, double factor) const;
  const Station* StationAt(const QPoint& position) const;
  AdjustmentEditorPalette m_Palette;
  QString m_Unit = "ft";
  std::vector<Station> m_Stations;
  std::vector<Segment> m_Segments;
  bool m_Initialised = false, m_HavePrevious = false, m_Panning = false;
  QPointF m_Center;
  double m_Scale = 1.0;
  QRectF m_Previous;
  QPoint m_LastMouse;
};
