// `npm run release`: build the front end, embed it, and build and start the
// app as a release build.
import { spawnSync } from "node:child_process";

const isWindows = process.platform === "win32";

function run(command, args) {
  const result = spawnSync(command, args, { stdio: "inherit", shell: isWindows });
  if (result.status !== 0) process.exit(result.status ?? 1);
}

run("npm", ["--prefix", "editor/ui", "install"]);
run("npm", ["--prefix", "editor/ui", "run", "build"]);
run("cargo", ["run", "-p", "gs-editor", "--release", "--features", "tauri/custom-protocol"]);
