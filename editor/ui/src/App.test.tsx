import { fireEvent, render, screen, waitFor } from "@testing-library/react";
import { beforeEach, describe, expect, it, vi } from "vitest";

const invoke = vi.fn();
vi.mock("@tauri-apps/api/core", () => ({ invoke: (...args: unknown[]) => invoke(...args) }));

import App from "./App";

function answer(status: unknown, snapshot: unknown) {
  invoke.mockImplementation((command: string) =>
    Promise.resolve(command === "engine_status" ? status : snapshot),
  );
}

describe("App", () => {
  beforeEach(() => {
    invoke.mockReset();
  });

  it("shows the engine as ready and the drawing as empty", async () => {
    answer(
      { state: "ready", version: "0.1.0" },
      { name: "Untitled", entityCount: 0 },
    );

    render(<App />);

    expect(await screen.findByText("Engine ready")).toBeTruthy();
    expect(screen.getByText("Untitled")).toBeTruthy();
    expect(screen.getByText("0 entities")).toBeTruthy();
    expect(invoke).toHaveBeenCalledWith("engine_status");
    expect(invoke).toHaveBeenCalledWith("drawing_snapshot");
  });

  it("names the engine error and says to restart when the engine is unavailable", async () => {
    invoke.mockRejectedValue("Drawing engine unavailable. Restart GoSurvey.");

    render(<App />);

    expect(
      await screen.findByText("Drawing engine unavailable. Restart GoSurvey."),
    ).toBeTruthy();
    expect(screen.queryByRole("button", { name: "Retry" })).toBeNull();
  });

  it("names the drawing region for assistive technology", async () => {
    answer({ state: "ready", version: "0.1.0" }, { name: "Untitled", entityCount: 0 });

    render(<App />);

    await waitFor(() => expect(screen.getByText("Engine ready")).toBeTruthy());
    expect(screen.getByRole("region", { name: "Drawing" })).toBeTruthy();
  });

  it("draws and logs a line entered in the command line", async () => {
    let hasLine = false;
    invoke.mockImplementation((command: string) => {
      if (command === "engine_status") return Promise.resolve({ state: "ready", version: "0.1.0" });
      if (command === "execute_command") {
        hasLine = true;
        return Promise.resolve("Line created.");
      }
      return Promise.resolve(hasLine
        ? { name: "Untitled", entityCount: 1, lines: [{ start: { x: 0, y: 0 }, end: { x: 20, y: 10 } }] }
        : { name: "Untitled", entityCount: 0, lines: [] });
    });

    render(<App />);
    const input = await screen.findByRole("textbox", { name: "Command" });
    fireEvent.change(input, { target: { value: "LINE, 0,0, 20,10," } });
    fireEvent.submit(input.closest("form")!);

    expect(await screen.findByText("Line created.")).toBeTruthy();
    expect(await screen.findByText("1 entities")).toBeTruthy();
    expect(screen.getByRole("img", { name: "Drawing with linework" })).toBeTruthy();
    expect(invoke).toHaveBeenCalledWith("execute_command", { input: "LINE, 0,0, 20,10," });
  });
});
