#pragma once

#include <QImage>

#include <map>
#include <string>

struct AppCommandState;
class ViewportRenderer;

/// REQ-350 (e) / ADR-062 — each library part's rendered picture, by block name. The renderer draws
/// the picture; the palette (a separate top-level window, so a separate GL context) only shows it.
using PartThumbnailImages = std::map<std::string, QImage>;

/// Serves the thumbnails the Pipe Fittings palette asked for (`AppCommandState::pipeFittingThumbRequests`)
/// and drops the ones marked stale. Must run inside the viewport's paint, after the scene pass, where
/// binding another framebuffer is safe. Returns true when \p images gained or lost an entry.
bool ServicePipeFittingThumbnails(AppCommandState& cmd, ViewportRenderer& renderer, PartThumbnailImages& images);
