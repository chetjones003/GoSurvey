import { test } from "node:test";
import assert from "node:assert/strict";
import { toWorld, toScreen, zoomAt, fit } from "../src/camera.ts";
test("world/screen round trip preserves state-plane coordinates", () => {
  const camera = { x: 2000000.1, y: 500000.03, scale: 37 };
  const point = { x: 2000010.35, y: 500000.53 };
  const actual = toWorld(toScreen(point, camera, 1100, 700), camera, 1100, 700);
  assert.ok(Math.abs(actual.x - point.x) < 1e-9);
  assert.ok(Math.abs(actual.y - point.y) < 1e-9);
});
test("wheel zoom keeps the world point underneath the cursor fixed", () => {
  const camera = { x: 90, y: -35, scale: 4 },
    pixel = { x: 173, y: 441 };
  const before = toWorld(pixel, camera, 1000, 700);
  const after = toWorld(
    pixel,
    zoomAt(camera, pixel, 1.37, 1000, 700),
    1000,
    700,
  );
  assert.ok(Math.abs(before.x - after.x) < 1e-10);
  assert.ok(Math.abs(before.y - after.y) < 1e-10);
});
test("extents handles empty, flat and state-plane drawings with finite positive cameras", () => {
  for (const lines of [
    [],
    [
      {
        id: 1,
        a: { x: 2e6, y: 5e5 },
        b: { x: 2e6 + 10, y: 5e5 },
        layer: "Draft",
      },
    ],
  ]) {
    const camera = fit(lines, 800, 500);
    assert.ok(
      Number.isFinite(camera.x) &&
        Number.isFinite(camera.y) &&
        camera.scale > 0,
    );
    for (const line of lines)
      for (const point of [line.a, line.b]) {
        const screen = toScreen(point, camera, 800, 500);
        assert.ok(
          screen.x > 0 && screen.x < 800 && screen.y > 0 && screen.y < 500,
        );
      }
  }
});
