import type { Camera, Line, Point } from "./types";
export function toScreen(
  p: Point,
  camera: Camera,
  width: number,
  height: number,
): Point {
  return {
    x: (p.x - camera.x) * camera.scale + width / 2,
    y: height / 2 - (p.y - camera.y) * camera.scale,
  };
}
export function toWorld(
  p: Point,
  camera: Camera,
  width: number,
  height: number,
): Point {
  return {
    x: (p.x - width / 2) / camera.scale + camera.x,
    y: (height / 2 - p.y) / camera.scale + camera.y,
  };
}
// Adapted from GoSurvey ZoomFraming.hpp's minimum one-unit extent.
// This spike leaves 20% for screen labels and navigation controls (native uses 8%).
export function fit(lines: Line[], width: number, height: number): Camera {
  if (!lines.length) return { x: 0, y: 0, scale: 4 };
  const points = lines.flatMap((l) => [l.a, l.b]);
  const minX = Math.min(...points.map((p) => p.x)),
    maxX = Math.max(...points.map((p) => p.x));
  const minY = Math.min(...points.map((p) => p.y)),
    maxY = Math.max(...points.map((p) => p.y));
  return {
    x: (minX + maxX) / 2,
    y: (minY + maxY) / 2,
    scale: Math.max(
      0.000001,
      Math.min(
        1e7,
        Math.min(
          (width * 0.8) / Math.max(1, maxX - minX),
          (height * 0.8) / Math.max(1, maxY - minY),
        ),
      ),
    ),
  };
}
export function zoomAt(
  camera: Camera,
  pixel: Point,
  factor: number,
  width: number,
  height: number,
): Camera {
  const before = toWorld(pixel, camera, width, height);
  const scale = Math.min(1e7, Math.max(0.000001, camera.scale * factor));
  return {
    x: before.x - (pixel.x - width / 2) / scale,
    y: before.y + (pixel.y - height / 2) / scale,
    scale,
  };
}
