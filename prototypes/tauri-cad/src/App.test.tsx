import { fireEvent, render, screen, within } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { createAgent } from "@ui-sdk/react/agent";
import { beforeEach, expect, test, vi } from "vitest";
import type { Drawing } from "./types";

const engine = vi.hoisted(() => ({
  drawing: null as Drawing | null,
  send: vi.fn(),
}));

vi.mock("./useEngine", () => ({
  useEngine: () => ({ drawing: engine.drawing, error: null, send: engine.send }),
}));
vi.mock("./Viewport", () => ({ default: () => null }));

import App from "./App";

function sampleDrawing(overrides: Partial<Drawing> = {}): Drawing {
  return {
    lines: [{ id: 1, a: { x: 0, y: 0 }, b: { x: 10, y: 0 }, layer: "Boundary" }],
    selected: [],
    prompt: "Specify first point",
    mode: "select",
    anchor: null,
    log: ["Ready"],
    ortho: false,
    layer: "Boundary",
    can_undo: true,
    can_redo: false,
    ...overrides,
  };
}

function isShown(element: Element) {
  return element.closest("[hidden]") === null;
}

beforeEach(() => {
  engine.drawing = sampleDrawing();
  engine.send.mockReset();
});

test("ribbon tabs switch the ribbon", async () => {
  const user = userEvent.setup();
  render(<App />);
  expect(screen.getByRole("tab", { name: "Drafting" }).getAttribute("aria-selected")).toBe("true");

  await user.click(screen.getByRole("tab", { name: "Survey" }));

  expect(screen.getByRole("tab", { name: "Survey" }).getAttribute("aria-selected")).toBe("true");
  expect(screen.getByText("Survey tools are next.")).toBeTruthy();
});

test("sidebar tabs show the objects panel", async () => {
  const user = userEvent.setup();
  render(<App />);
  expect(isShown(screen.getByText("DRAWING SUMMARY"))).toBe(true);

  await user.click(screen.getByRole("tab", { name: "Objects" }));

  expect(screen.getByRole("tab", { name: "Objects" }).getAttribute("aria-selected")).toBe("true");
  expect(isShown(screen.getByText("Line #1"))).toBe(true);
  expect(screen.queryByText("DRAWING SUMMARY")).toBeNull();
});

test("choosing a layer sends it to the engine", async () => {
  const user = userEvent.setup();
  render(<App />);

  await user.click(screen.getByRole("combobox", { name: "Current layer" }));
  await user.click(screen.getByRole("option", { name: "Draft" }));

  expect(engine.send).toHaveBeenCalledWith({ type: "layer", layer: "Draft" });
});

test("hiding a layer keeps its label and sets pressed", async () => {
  const user = userEvent.setup();
  render(<App />);
  expect(screen.getByRole("button", { name: "Hide Easements" }).getAttribute("aria-pressed")).toBe("false");

  await user.click(screen.getByRole("button", { name: "Hide Easements" }));

  expect(screen.getByRole("button", { name: "Hide Easements" }).getAttribute("aria-pressed")).toBe("true");
});

test("the command reference dialog opens and closes with its buttons and Escape", async () => {
  const user = userEvent.setup();
  render(<App />);

  await user.click(screen.getByRole("button", { name: "Command reference" }));
  const dialog = screen.getByRole("dialog", { name: "Make a little linework." });

  await user.click(within(dialog).getByRole("button", { name: "Close help" }));
  expect(screen.queryByRole("dialog")).toBeNull();

  await user.click(screen.getByRole("button", { name: "Command reference" }));
  fireEvent.keyDown(screen.getByRole("dialog"), { key: "Escape" });
  expect(screen.queryByRole("dialog")).toBeNull();
});

test("typing a command and pressing Enter sends it as text", async () => {
  const user = userEvent.setup();
  render(<App />);

  await user.type(screen.getByRole("textbox", { name: "Command input" }), "LINE{enter}");

  expect(engine.send).toHaveBeenCalledWith({ type: "text", text: "LINE" });
});

test("Undo is disabled when the engine has nothing to undo", () => {
  engine.drawing = sampleDrawing({ can_undo: false });
  render(<App />);

  expect((screen.getByRole("button", { name: "Undo" }) as HTMLButtonElement).disabled).toBe(true);
});

test("grid toggle reflects and changes its pressed state", async () => {
  const user = userEvent.setup();
  render(<App />);
  expect(screen.getByRole("button", { name: "GRID" }).getAttribute("aria-pressed")).toBe("true");

  await user.click(screen.getByRole("button", { name: "GRID" }));

  expect(screen.getByRole("button", { name: "GRID" }).getAttribute("aria-pressed")).toBe("false");
});

test("an agent can find and use the main controls", async () => {
  const agent = createAgent();
  render(<App />);

  const components = agent
    .snapshot()
    .filter((entry) => !entry.offPage && entry.component !== "more");
  for (const entry of components) {
    expect(entry.label, `${entry.component} ${entry.ref} has no name`).toBeTruthy();
  }
  const labels = components.map((entry) => entry.label);
  for (const label of ["Current layer", "Command input", "Command reference", "Undo"]) {
    expect(labels, `${label} is missing from the snapshot`).toContain(label);
  }

  const result = await agent.act({
    steps: [{ ref: "Command input", action: "setValue", args: ["ZE"] }],
  });
  expect(result.ok, result.error?.message).toBe(true);
  expect((screen.getByRole("textbox", { name: "Command input" }) as HTMLInputElement).value).toBe("ZE");
});
