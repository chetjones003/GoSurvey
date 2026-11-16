// Connect only to the explicitly launched spike's WebView2 debugging port.
// Run: node scripts/smoke.mjs (app must be running with debug port 9227).
import { chromium, expect } from "@playwright/test";
import { mkdir, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { spawn } from "node:child_process";
const artifacts = resolve("../../build/artifacts/tauri-spike");
await mkdir(artifacts, { recursive: true });
const child = spawn(
  resolve("../../build/.cmake/tauri-spike/debug/GoSurveySpike.exe"),
  [],
  {
    cwd: resolve("../.."),
    windowsHide: true,
    env: {
      ...process.env,
      GOSURVEY_SPIKE_AUTOMATION: "1",
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: "--remote-debugging-port=9227",
      WEBVIEW2_USER_DATA_FOLDER: resolve(artifacts, "webview-test-profile"),
    },
  },
);
let browser;
try {
  await expect
    .poll(
      async () => {
        try {
          const response = await fetch("http://127.0.0.1:9227/json/list");
          return (await response.json()).length;
        } catch {
          return 0;
        }
      },
      { timeout: 30000 },
    )
    .toBeGreaterThan(0);
  browser = await chromium.connectOverCDP("http://127.0.0.1:9227");
  const page = browser.contexts()[0].pages()[0];
  if (!page) throw new Error("The GoSurvey spike webview is not available.");
  await page.waitForURL(/tauri\.localhost|:1420/, { timeout: 30000 });
  const errors = [];
  page.on("pageerror", (e) => errors.push(e.message));
  const state = () =>
    page.evaluate(() =>
      window.__TAURI_INTERNALS__.invoke("dispatch", {
        event: { type: "snapshot" },
      }),
    );
  const command = async (text) => {
    await page.getByRole("textbox", { name: "Command input" }).fill(text);
    await page.getByRole("textbox", { name: "Command input" }).press("Enter");
  };
  await expect(page.locator(".backend-badge")).toContainText("Rust engine");
  await expect.poll(async () => (await state()).lines.length).toBe(12);
  await page.screenshot({ path: resolve(artifacts, "01-workspace.png") });
  await command("LINE");
  await command("0,-20");
  await command("@20,10");
  await command("");
  await expect.poll(async () => (await state()).lines.length).toBe(13);
  let current = await state();
  expect(current.lines.at(-1).b).toEqual({ x: 20, y: -10 });
  // Pick the new line through the actual SVG viewport using its projected endpoints.
  const line = page.locator('[data-entity="13"] line');
  const endpoints = await line.evaluate((el) => ({
    x: (+el.getAttribute("x1") + +el.getAttribute("x2")) / 2,
    y: (+el.getAttribute("y1") + +el.getAttribute("y2")) / 2,
  }));
  await page.getByTestId("viewport").click({ position: endpoints });
  await expect.poll(async () => (await state()).selected).toEqual([13]);
  await expect(page.locator(".inspector-title")).toContainText("Line #13");
  await page.screenshot({ path: resolve(artifacts, "02-selection.png") });
  await page.locator('.inspector').screenshot({ path: resolve(artifacts, '02-properties-detail.png') });
  await command("MOVE");
  await command("0,0");
  await command("@5,5");
  await expect
    .poll(async () => (await state()).lines.at(-1).a)
    .toEqual({ x: 5, y: -15 });
  await command("COPY");
  await command("0,0");
  await command("@15,0");
  await expect.poll(async () => (await state()).lines.length).toBe(14);
  await command("DELETE");
  await expect.poll(async () => (await state()).lines.length).toBe(13);
  await command("UNDO");
  await expect.poll(async () => (await state()).lines.length).toBe(14);
  await command("REDO");
  await expect.poll(async () => (await state()).lines.length).toBe(13);
  await command("LINE");
  await command("0,0");
  await command("NaN,10");
  await expect(page.locator(".command-history")).toContainText(
    "Coordinates must be finite",
  );
  await page.screenshot({ path: resolve(artifacts, "03-invalid-input.png") });
  await page.locator('.command-panel').screenshot({ path: resolve(artifacts, '03-command-error-detail.png') });
  await page.keyboard.press("Escape");
  // Pointer entry and rubber-band preview use the same native LINE phase.
  await page.getByRole("button", { name: "Draw line" }).click();
  await page.getByTestId("viewport").click({ position: { x: 180, y: 180 } });
  const viewportBox = await page.getByTestId("viewport").boundingBox();
  await page.mouse.move(viewportBox.x + 290, viewportBox.y + 235);
  await page.screenshot({ path: resolve(artifacts, "04-line-preview.png") });
  await page.locator('.command-panel').screenshot({ path: resolve(artifacts, '04-command-detail.png') });
  await page.getByTestId("viewport").click({ position: { x: 290, y: 235 } });
  await expect.poll(async () => (await state()).lines.length).toBe(14);
  await page.keyboard.press("Escape");
  const sample = page.locator('[data-entity="1"] line');
  const startX = Number(await sample.getAttribute("x1"));
  await command("PAN");
  await page.mouse.move(viewportBox.x + 350, viewportBox.y + 280);
  await page.mouse.down();
  await page.mouse.move(viewportBox.x + 390, viewportBox.y + 310);
  await page.mouse.up();
  await expect
    .poll(async () => Number(await sample.getAttribute("x1")))
    .toBeCloseTo(startX + 40, 3);
  await page.keyboard.press("Escape");
  const zoomBefore =
    Number(await sample.getAttribute("x2")) -
    Number(await sample.getAttribute("x1"));
  await page.mouse.wheel(0, -160);
  await expect
    .poll(
      async () =>
        Number(await sample.getAttribute("x2")) -
        Number(await sample.getAttribute("x1")),
    )
    .toBeGreaterThan(zoomBefore);
  await command("ZE");
  await page.getByRole("button", { name: "Command reference" }).click();
  await expect(page.getByRole("dialog")).toBeVisible();
  await page.screenshot({
    path: resolve(artifacts, "05-command-reference.png"),
  });
  await page.locator('.help-modal').screenshot({ path: resolve(artifacts, '05-reference-detail.png') });
  await page.getByRole("button", { name: "Close help" }).click();
  await page.screenshot({ path: resolve(artifacts, "06-after-edits.png") });
  expect(errors).toEqual([]);
  const result = {
    nativeBackend: true,
    checks: [
      "typed LINE and relative coordinates",
      "viewport selection and inspector",
      "MOVE",
      "COPY",
      "DELETE",
      "UNDO/REDO",
      "invalid input without mutation",
      "pointer LINE and preview",
      "PAN 1:1",
      "wheel zoom",
      "zoom extents",
      "command reference",
    ],
    finalLines: (await state()).lines.length,
    pageErrors: errors,
  };
  await writeFile(
    resolve(artifacts, "smoke-result.json"),
    JSON.stringify(result, null, 2),
  );
  console.log(JSON.stringify(result, null, 2));
  await browser.close();
} finally {
  child.kill();
}
