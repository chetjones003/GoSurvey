import { chromium, expect } from "@playwright/test";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { spawn, execFileSync } from "node:child_process";
import { resolve } from "node:path";
const artifacts = resolve("../../build/artifacts/tauri-spike");
await mkdir(artifacts, { recursive: true });
const cssPath = resolve("src/style.css"),
  original = await readFile(cssPath, "utf8");
const child = spawn(
  "C:\\Windows\\System32\\cmd.exe",
  ["/d", "/s", "/c", "npm run desktop"],
  {
    cwd: process.cwd(),
    windowsHide: true,
    stdio: "pipe",
    env: {
      ...process.env,
      PATH: `${process.env.USERPROFILE}\\.cargo\\bin;${process.env.PATH}`,
      GOSURVEY_SPIKE_AUTOMATION: "1",
      WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS: "--remote-debugging-port=9228",
      WEBVIEW2_USER_DATA_FOLDER: resolve(artifacts, "webview-hmr-profile"),
    },
  },
);
let output = "",
  browser;
child.stdout.on("data", (chunk) => {
  output += chunk;
});
child.stderr.on("data", (chunk) => {
  output += chunk;
});
try {
  await expect
    .poll(
      async () => {
        try {
          return (await (await fetch("http://127.0.0.1:9228/json/list")).json())
            .length;
        } catch {
          return 0;
        }
      },
      { timeout: 120000, intervals: [250, 500, 1000] },
    )
    .toBeGreaterThan(0);
  browser = await chromium.connectOverCDP("http://127.0.0.1:9228");
  const page = browser.contexts()[0].pages()[0];
  await page.waitForURL(/:1420/, { timeout: 30000 });
  await expect(page.locator(".backend-badge")).toContainText("Rust engine");
  const input = page.getByRole("textbox", { name: "Command input" });
  for (const text of ["LINE", "0,0", "10,5", ""]) {
    await input.fill(text);
    await input.press("Enter");
  }
  const state = () =>
    page.evaluate(() =>
      window.__TAURI_INTERNALS__.invoke("dispatch", {
        event: { type: "snapshot" },
      }),
    );
  await expect.poll(async () => (await state()).lines.length).toBe(13);
  const before = await state(),
    compileCount = (output.match(/Compiling gosurvey-spike/g) ?? []).length;
  const marker = `hmr-${Date.now()}`,
    start = performance.now();
  await writeFile(
    cssPath,
    original + `\n:root { --spike-hmr-probe: ${marker}; }\n`,
  );
  await expect
    .poll(
      () =>
        page.evaluate(() =>
          getComputedStyle(document.documentElement)
            .getPropertyValue("--spike-hmr-probe")
            .trim(),
        ),
      { intervals: [20, 40, 80, 100] },
    )
    .toBe(marker);
  const cssUpdateObservedMs = Math.round(performance.now() - start);
  expect((await state()).lines).toEqual(before.lines);
  expect((output.match(/Compiling gosurvey-spike/g) ?? []).length).toBe(
    compileCount,
  );
  await page.reload();
  await expect(page.locator(".backend-badge")).toContainText("Rust engine");
  expect((await state()).lines).toEqual(before.lines);
  const result = {
    cssUpdateObservedMs,
    rustRecompiledForCss: false,
    geometrySurvivedHmr: true,
    geometrySurvivedFrontendReload: true,
    fixtureLines: 13,
    note: "One local warm CSS update; includes file write, CDP and polling overhead. Not a general performance guarantee.",
  };
  await writeFile(
    resolve(artifacts, "hmr-result.json"),
    JSON.stringify(result, null, 2),
  );
  console.log(JSON.stringify(result, null, 2));
} catch (error) {
  console.error(output);
  throw error;
} finally {
  await writeFile(cssPath, original);
  await writeFile(resolve(artifacts, "hmr-dev.log"), output);
  // Stop only the dev process tree this script created.
  try {
    execFileSync("taskkill", ["/PID", String(child.pid), "/T", "/F"], {
      windowsHide: true,
      stdio: "ignore",
    });
  } catch {
    /* Process may already have exited. */
  }
  if (browser) await browser.close().catch(() => {});
}
