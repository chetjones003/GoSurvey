import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { describe, expect, it, vi } from "vitest";

import CommandLine from "./CommandLine";

describe("CommandLine", () => {
  it("submits a blank Enter when the active command accepts it", async () => {
    const user = userEvent.setup();
    const onSubmit = vi.fn();
    render(
      <CommandLine
        history={[]}
        prompt={{ text: "Press Enter to accept", acceptsBlank: true, acceptsPoint: false }}
        onSubmit={onSubmit}
      />,
    );

    await user.click(screen.getByRole("textbox", { name: "Command" }));
    await user.keyboard("{Enter}");

    expect(onSubmit).toHaveBeenCalledWith({ type: "textSubmitted", text: "" });
  });
});
