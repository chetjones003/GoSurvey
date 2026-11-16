export type Point = { x: number; y: number };
export type Line = { id: number; a: Point; b: Point; layer: string };
export type Drawing = {
  lines: Line[];
  selected: number[];
  prompt: string;
  mode: string;
  anchor: Point | null;
  log: string[];
  ortho: boolean;
  layer: string;
  can_undo: boolean;
  can_redo: boolean;
};
export type EngineEvent =
  | { type: "snapshot" | "cancel" | "undo" | "redo" }
  | { type: "text" | "start"; text: string }
  | { type: "point"; point: Point }
  | { type: "select"; id: number | null; additive: boolean }
  | { type: "ortho"; enabled: boolean }
  | { type: "layer"; layer: string };
export type Camera = { x: number; y: number; scale: number };
export const layers = [
  { name: "Boundary", color: "#c6cbd2", description: "Property linework" },
  { name: "Easements", color: "#c8a66a", description: "Access & utilities" },
  {
    name: "Structures",
    color: "#a5afc5",
    description: "Existing improvements",
  },
  { name: "Draft", color: "#79b5ed", description: "Working geometry" },
];
export const colorFor = (layer: string) =>
  layers.find((l) => l.name === layer)?.color ?? "#79b5ed";
export const format = (v: number) =>
  v.toLocaleString("en-US", {
    minimumFractionDigits: 3,
    maximumFractionDigits: 3,
  });
