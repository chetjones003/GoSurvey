import { fireEvent, render, screen } from "@testing-library/react";
import { describe, expect, it, vi } from "vitest";

import Viewport from "./Viewport";

describe("Viewport", () => {
  it("converts a click into a drawing point when the command asks for one", () => {
    const onPoint = vi.fn();
    render(<Viewport lines={[]} acceptsPoint onPoint={onPoint} />);
    const svg = screen.getByRole("img", { name: "Empty drawing grid" });
    const point = {
      x: 0,
      y: 0,
      matrixTransform: vi.fn(() => ({ x: 23, y: -11 })),
    };
    Object.defineProperty(svg, "getScreenCTM", {
      value: () => ({ inverse: () => ({}) }),
    });
    Object.defineProperty(svg, "createSVGPoint", { value: () => point });

    fireEvent.click(svg, { clientX: 300, clientY: 250 });

    expect(point.matrixTransform).toHaveBeenCalled();
    expect(onPoint).toHaveBeenCalledWith({ x: 23, y: 11 });
  });

  it("does not turn viewport clicks into command input when no point is requested", () => {
    const onPoint = vi.fn();
    render(<Viewport lines={[]} onPoint={onPoint} />);

    fireEvent.click(screen.getByRole("img", { name: "Empty drawing grid" }));

    expect(onPoint).not.toHaveBeenCalled();
  });
});
