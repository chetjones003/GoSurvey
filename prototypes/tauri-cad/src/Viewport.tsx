import { useEffect, useRef, useState } from "react";
import { Compass, Expand, Minus, Plus, MousePointer2 } from "lucide-react";
import {
  colorFor,
  type Camera,
  type Drawing,
  type EngineEvent,
  type Point,
} from "./types";
import { toScreen, toWorld, zoomAt } from "./camera";

type Props = {
  drawing: Drawing | null;
  camera: Camera;
  setCamera: React.Dispatch<React.SetStateAction<Camera>>;
  send: (e: EngineEvent) => Promise<void>;
  pan: boolean;
  snap: boolean;
  grid: boolean;
  hidden: string[];
  fitView: () => void;
  setSize: (size: { width: number; height: number }) => void;
  onCursor: (point: Point) => void;
  focusCommand: () => void;
  cancel: () => void;
};
export default function Viewport({
  drawing,
  camera,
  setCamera,
  send,
  pan,
  snap,
  grid,
  hidden,
  fitView,
  setSize,
  onCursor,
  focusCommand,
  cancel,
}: Props) {
  const root = useRef<HTMLDivElement>(null);
  const [size, resize] = useState({ width: 800, height: 600 });
  const [cursor, setCursor] = useState<Point | null>(null);
  const [snapped, setSnapped] = useState(false);
  const drag = useRef<{ point: Point; camera: Camera } | null>(null);
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) => {
      const next = {
        width: entry.contentRect.width,
        height: entry.contentRect.height,
      };
      resize(next);
      setSize(next);
    });
    if (root.current) observer.observe(root.current);
    return () => observer.disconnect();
  }, [setSize]);
  const visible = drawing?.lines.filter((l) => !hidden.includes(l.layer)) ?? [];
  const screen = (p: Point) => toScreen(p, camera, size.width, size.height);
  const local = (e: { clientX: number; clientY: number }) => {
    const rect = root.current!.getBoundingClientRect();
    return { x: e.clientX - rect.left, y: e.clientY - rect.top };
  };
  const resolve = (pixel: Point) => {
    let world = toWorld(pixel, camera, size.width, size.height),
      hit = false,
      best = 11;
    if (snap)
      for (const line of visible)
        for (const point of [line.a, line.b]) {
          const p = screen(point),
            distance = Math.hypot(p.x - pixel.x, p.y - pixel.y);
          if (distance < best) {
            best = distance;
            world = point;
            hit = true;
          }
        }
    if (drawing?.ortho && drawing.anchor) {
      const a = drawing.anchor;
      if (Math.abs(world.x - a.x) >= Math.abs(world.y - a.y))
        world = { x: world.x, y: a.y };
      else world = { x: a.x, y: world.y };
    }
    return { world, hit };
  };
  function pick(pixel: Point, additive: boolean) {
    let id: number | null = null,
      best = 9;
    for (const line of visible) {
      const a = screen(line.a),
        b = screen(line.b),
        dx = b.x - a.x,
        dy = b.y - a.y;
      const t = Math.max(
        0,
        Math.min(
          1,
          ((pixel.x - a.x) * dx + (pixel.y - a.y) * dy) /
            (dx * dx + dy * dy || 1),
        ),
      );
      const distance = Math.hypot(
        pixel.x - a.x - t * dx,
        pixel.y - a.y - t * dy,
      );
      if (distance < best) {
        best = distance;
        id = line.id;
      }
    }
    void send({ type: "select", id, additive });
  }
  useEffect(() => {
    const element = root.current;
    const wheel = (event: WheelEvent) => {
      event.preventDefault();
      const pixel = local(event);
      setCamera((c) =>
        zoomAt(
          c,
          pixel,
          Math.exp(-event.deltaY * 0.0015),
          size.width,
          size.height,
        ),
      );
    };
    element?.addEventListener("wheel", wheel, { passive: false });
    return () => element?.removeEventListener("wheel", wheel);
  }, [size, setCamera]);
  const gridWorld = 10 ** Math.floor(Math.log10(75 / camera.scale));
  const spacing = gridWorld * camera.scale;
  const origin = screen({ x: 0, y: 0 });
  const cp = cursor ? screen(cursor) : null;
  const anchor = drawing?.anchor ? screen(drawing.anchor) : null;
  const isSelecting =
    !drawing?.anchor &&
    (drawing?.mode === "select" ||
      drawing?.prompt.startsWith("Select objects"));
  return (
    <div
      className={`viewport ${pan ? "panning" : ""}`}
      ref={root}
      data-testid="viewport"
      onContextMenu={(e) => {
        e.preventDefault();
        cancel();
      }}
      onPointerDown={(e) => {
        if (e.button === 1 || (e.button === 0 && pan)) {
          e.preventDefault();
          drag.current = { point: local(e), camera };
          e.currentTarget.setPointerCapture(e.pointerId);
          return;
        }
        if (e.button !== 0) return;
        const pixel = local(e);
        if (isSelecting)
          pick(
            pixel,
            e.shiftKey || !!drawing?.prompt.startsWith("Select objects"),
          );
        else void send({ type: "point", point: resolve(pixel).world });
        focusCommand();
      }}
      onPointerMove={(e) => {
        const pixel = local(e);
        if (drag.current) {
          const d = drag.current;
          setCamera({
            ...d.camera,
            x: d.camera.x - (pixel.x - d.point.x) / d.camera.scale,
            y: d.camera.y + (pixel.y - d.point.y) / d.camera.scale,
          });
          return;
        }
        const { world, hit } = resolve(pixel);
        setCursor(world);
        setSnapped(hit);
        onCursor(world);
      }}
      onPointerUp={(e) => {
        drag.current = null;
        if (e.currentTarget.hasPointerCapture(e.pointerId))
          e.currentTarget.releasePointerCapture(e.pointerId);
      }}
      onPointerCancel={() => {
        drag.current = null;
      }}
      onPointerLeave={() => {
        if (!drag.current) setCursor(null);
      }}
      onDoubleClick={(e) => {
        if (e.button === 1) fitView();
      }}
    >
      <svg
        width="100%"
        height="100%"
        aria-label="Interactive survey drawing"
        role="img"
      >
        <defs>
          <pattern
            id="minor-grid"
            width={spacing}
            height={spacing}
            patternUnits="userSpaceOnUse"
            x={origin.x % spacing}
            y={origin.y % spacing}
          >
            <path
              d={`M ${spacing} 0 L 0 0 0 ${spacing}`}
              fill="none"
              stroke="#2b2d31"
              strokeWidth="0.6"
            />
          </pattern>
          <pattern
            id="major-grid"
            width={spacing * 5}
            height={spacing * 5}
            patternUnits="userSpaceOnUse"
            x={origin.x % (spacing * 5)}
            y={origin.y % (spacing * 5)}
          >
            <rect
              width={spacing * 5}
              height={spacing * 5}
              fill="url(#minor-grid)"
            />
            <path
              d={`M ${spacing * 5} 0 L 0 0 0 ${spacing * 5}`}
              fill="none"
              stroke="#373a3f"
              strokeWidth="0.65"
            />
          </pattern>
        </defs>
        {grid && <rect width="100%" height="100%" fill="url(#major-grid)" />}
        {visible.map((line) => {
          const a = screen(line.a),
            b = screen(line.b),
            selected = drawing!.selected.includes(line.id);
          return (
            <g key={line.id} data-entity={line.id}>
              <line
                x1={a.x}
                y1={a.y}
                x2={b.x}
                y2={b.y}
                stroke={selected ? "#d7f7ff" : colorFor(line.layer)}
                strokeWidth={
                  selected ? 2.4 : line.layer === "Boundary" ? 1.8 : 1.2
                }
                strokeDasharray={line.layer === "Easements" ? "9 6" : undefined}
              />
              {line.layer === "Boundary" && !selected && (
                <>
                  <circle
                    cx={a.x}
                    cy={a.y}
                    r="2.5"
                    fill="#111b24"
                    stroke="#bac2cb"
                  />
                  <text
                    x={(a.x + b.x) / 2 + 9}
                    y={(a.y + b.y) / 2 - 9}
                    fill="#9fa8b2"
                    fontSize="10"
                    fontFamily="Consolas,monospace"
                  >
                    {Math.hypot(
                      line.b.x - line.a.x,
                      line.b.y - line.a.y,
                    ).toFixed(2)}
                    ′
                  </text>
                </>
              )}
              {selected &&
                [a, b].map((p, i) => (
                  <rect
                    key={i}
                    x={p.x - 3}
                    y={p.y - 3}
                    width="6"
                    height="6"
                    fill="#111b24"
                    stroke="#8ee5ff"
                  />
                ))}
            </g>
          );
        })}
        {anchor && cp && (
          <g>
            <line
              x1={anchor.x}
              y1={anchor.y}
              x2={cp.x}
              y2={cp.y}
              stroke="#d3d8df"
              strokeWidth="1.3"
              strokeDasharray="5 4"
            />
            <rect
              x={cp.x + 18}
              y={cp.y + 15}
              width="112"
              height="25"
              rx="3"
              fill="#19252e"
              stroke="#43515b"
            />
            <text
              x={cp.x + 25}
              y={cp.y + 32}
              fill="#e4eaf0"
              fontSize="11"
              fontFamily="monospace"
            >
              {Math.hypot(
                cursor!.x - drawing!.anchor!.x,
                cursor!.y - drawing!.anchor!.y,
              ).toFixed(3)}{" "}
              ft
            </text>
          </g>
        )}
        {cp && !pan && (
          <g pointerEvents="none" opacity="0.85">
            <path
              d={`M ${cp.x - 22} ${cp.y} H ${cp.x - 5} M ${cp.x + 5} ${cp.y} H ${cp.x + 22} M ${cp.x} ${cp.y - 22} V ${cp.y - 5} M ${cp.x} ${cp.y + 5} V ${cp.y + 22}`}
              stroke="#dce8eb"
              strokeWidth="0.8"
            />
            {snapped ? (
              <rect
                x={cp.x - 5}
                y={cp.y - 5}
                width="10"
                height="10"
                stroke="#bbcbdc"
                fill="none"
                strokeWidth="1.5"
              />
            ) : (
              <rect
                x={cp.x - 3}
                y={cp.y - 3}
                width="6"
                height="6"
                stroke="#dce8eb"
                fill="none"
                strokeWidth="0.7"
              />
            )}
          </g>
        )}
        <g transform={`translate(37 ${size.height - 30})`}>
          <path d="M0 0 H40 M0 0 V-40" stroke="#92979e" fill="none" />
          <path d="M40 0 l-5 -3 v6 Z" fill="#d98d81" />
          <path d="M0 -40 l-3 5 h6 Z" fill="#93c9a9" />
          <text x="45" y="4" fill="#d98d81" fontSize="10">
            X
          </text>
          <text x="-3" y="-47" fill="#93c9a9" fontSize="10">
            Y
          </text>
          <rect
            x="-3"
            y="-3"
            width="6"
            height="6"
            fill="#17212b"
            stroke="#8aa4b2"
          />
        </g>
      </svg>
      <div className="viewport-caption">
        <span className="view-label">[ Top ]</span>
        <span>[ 2D Wireframe ]</span>
      </div>
      <div className="north">
        <Compass size={29} strokeWidth={1} />
        <span>N</span>
      </div>
      <div
        className="viewport-controls"
        onPointerDown={(e) => e.stopPropagation()}
      >
        <button
          className="btn btn-ghost btn-square btn-sm"
          title="Zoom in"
          aria-label="Zoom in"
          onClick={() =>
            setCamera((c) =>
              zoomAt(
                c,
                { x: size.width / 2, y: size.height / 2 },
                1.3,
                size.width,
                size.height,
              ),
            )
          }
        >
          <Plus size={16} />
        </button>
        <button
          className="btn btn-ghost btn-square btn-sm"
          title="Zoom out"
          aria-label="Zoom out"
          onClick={() =>
            setCamera((c) =>
              zoomAt(
                c,
                { x: size.width / 2, y: size.height / 2 },
                1 / 1.3,
                size.width,
                size.height,
              ),
            )
          }
        >
          <Minus size={16} />
        </button>
        <span className="separator" />
        <button
          className="btn btn-ghost btn-square btn-sm"
          title="Zoom extents · ZE"
          aria-label="Zoom extents"
          onClick={fitView}
        >
          <Expand size={16} />
        </button>
      </div>
      {!drawing?.lines.length && (
        <div className="empty-view">
          <MousePointer2 size={28} />
          <b>Your next survey starts here.</b>
          <span>Type LINE or choose Draw line to begin.</span>
        </div>
      )}
      <div className="scale-label">
        <span style={{ width: spacing * 5 }} />
        <span>{gridWorld * 5} ft</span>
      </div>
    </div>
  );
}
