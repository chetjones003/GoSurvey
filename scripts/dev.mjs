// `npm run dev`: Vite dev server for the front end, then the app in a debug
// build that loads it. Ctrl+C stops both.
import { spawn, spawnSync } from "node:child_process";
import { setTimeout as delay } from "node:timers/promises";

const DEV_URL = "http://127.0.0.1:1420";
const isWindows = process.platform === "win32";

function run(command, args) {
  const result = spawnSync(command, args, { stdio: "inherit", shell: isWindows });
  if (result.status !== 0) process.exit(result.status ?? 1);
}

function stopProcess(child) {
  if (!child || child.exitCode !== null || child.pid === undefined) return;
  if (isWindows) {
    // npm starts Vite through a shell, so kill the whole tree.
    spawnSync("taskkill", ["/pid", String(child.pid), "/T", "/F"], { stdio: "ignore" });
  } else {
    child.kill("SIGTERM");
  }
}

async function waitForVite() {
  for (let attempt = 0; attempt < 120; attempt++) {
    try {
      const response = await fetch(DEV_URL);
      if (response.ok) return;
    } catch {
      // Not listening yet.
    }
    await delay(500);
  }
  throw new Error(`The Vite dev server did not start at ${DEV_URL}.`);
}

run("npm", ["--prefix", "editor/ui", "install"]);

const vite = spawn("npm", ["--prefix", "editor/ui", "run", "dev"], {
  stdio: "inherit",
  shell: isWindows,
});
let app;

function shutdown(code) {
  stopProcess(app);
  stopProcess(vite);
  process.exit(code);
}

process.on("SIGINT", () => shutdown(130));
process.on("SIGTERM", () => shutdown(143));

vite.on("exit", (code) => {
  if (!app) {
    console.error("The Vite dev server stopped before the app started.");
    shutdown(code ?? 1);
  }
});

try {
  await waitForVite();
} catch (error) {
  console.error(error.message);
  shutdown(1);
}

app = spawn("cargo", ["run", "-p", "gs-editor"], { stdio: "inherit" });
app.on("exit", (code) => shutdown(code ?? 0));
