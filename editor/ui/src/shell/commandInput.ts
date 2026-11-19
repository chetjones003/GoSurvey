export type CommandInput =
  | { type: "textSubmitted"; text: string }
  | { type: "pointPicked"; point: { x: number; y: number } }
  | { type: "cancelled" };

export function describeCommandInput(input: CommandInput): string {
  switch (input.type) {
    case "textSubmitted":
      return input.text;
    case "pointPicked":
      return `${input.point.x},${input.point.y}`;
    case "cancelled":
      return "Esc";
  }
}
