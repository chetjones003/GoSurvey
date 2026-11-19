import { render, screen, waitFor } from "@testing-library/react";
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
});
