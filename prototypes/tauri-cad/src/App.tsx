import { useCallback, useEffect, useRef, useState } from "react";
import {
  Activity,
  ArrowDownRight,
  ArrowUpRight,
  Box,
  ChevronDown,
  ChevronRight,
  CircleHelp,
  Copy,
  Crosshair,
  Expand,
  Eye,
  EyeOff,
  File,
  Focus,
  Grid2X2,
  Hand,
  Layers,
  ListTree,
  LockKeyhole,
  Maximize2,
  MousePointer2,
  Move,
  PanelLeftClose,
  Redo2,
  Ruler,
  Settings2,
  Slash,
  Square,
  Terminal,
  Trash2,
  Triangle,
  Undo2,
  X,
} from "lucide-react";
import type { LucideIcon } from "lucide-react";
import { fit } from "./camera";
import { useEngine } from "./useEngine";
import {
  colorFor,
  format,
  layers,
  type Camera,
  type Drawing,
  type Point,
} from "./types";
import Viewport from "./Viewport";

function Tool({
  icon: Icon,
  label,
  shortcut,
  onClick,
  active,
  disabled,
  large,
}: {
  icon: LucideIcon;
  label: string;
  shortcut?: string;
  onClick?: () => void;
  active?: boolean;
  disabled?: boolean;
  large?: boolean;
}) {
  return (
    <button
      className={`btn btn-ghost tool ${active ? "active" : ""} ${large ? "tool-large" : ""}`}
      onClick={onClick}
      disabled={disabled}
      title={
        disabled
          ? `${label} · outside this spike`
          : `${label}${shortcut ? ` · ${shortcut}` : ""}`
      }
    >
      <Icon size={large ? 25 : 16} strokeWidth={1.55} />
      <span>{label}</span>
      {shortcut && !large && <kbd>{shortcut}</kbd>}
    </button>
  );
}
function Inspector({ drawing }: { drawing: Drawing | null }) {
  const selected =
    drawing?.lines.filter((l) => drawing.selected.includes(l.id)) ?? [];
  const line = selected.length === 1 ? selected[0] : null;
  const total = selected.reduce(
    (sum, l) => sum + Math.hypot(l.b.x - l.a.x, l.b.y - l.a.y),
    0,
  );
  return (
    <aside className="inspector">
      <div className="panel-heading">
        <span>
          <Settings2 size={14} />
          Properties
        </span>
        <span className="badge badge-ghost">{selected.length}</span>
      </div>
      <div className="inspector-title">
        <span className="entity-symbol">
          <Slash size={20} />
        </span>
        <div>
          <strong>
            {line
              ? `Line #${line.id}`
              : selected.length
                ? `${selected.length} lines`
                : "No selection"}
          </strong>
          <small>
            {selected.length
              ? "Model space geometry"
              : "Select an object to inspect"}
          </small>
        </div>
      </div>
      <div className="property-section">
        <h3>GENERAL</h3>
        <Property label="Object type" value={selected.length ? "Line" : "—"} />
        <Property
          label="Layer"
          value={
            line?.layer ??
            (selected.length ? "Multiple" : (drawing?.layer ?? "Boundary"))
          }
          dot={line ? colorFor(line.layer) : undefined}
        />
        <Property label="Color" value="By layer" />
        <Property label="Lineweight" value="Default" />
        <Property label="Units" value="Feet" />
      </div>
      <div className="property-section">
        <h3>GEOMETRY</h3>
        {line ? (
          <>
            <Property label="Start X" value={format(line.a.x)} />
            <Property label="Start Y" value={format(line.a.y)} />
            <Property label="End X" value={format(line.b.x)} />
            <Property label="End Y" value={format(line.b.y)} />
            <Property label="Length" value={`${format(total)} ft`} />
            <Property
              label="Angle"
              value={`${(((Math.atan2(line.b.y - line.a.y, line.b.x - line.a.x) * 180) / Math.PI + 360) % 360).toFixed(4)}°`}
            />
          </>
        ) : selected.length ? (
          <Property label="Total length" value={`${format(total)} ft`} />
        ) : (
          <div className="no-properties">
            <MousePointer2 size={22} />
            <p>
              Pick linework in the drawing.
              <br />
              Shift + click adds to selection.
            </p>
          </div>
        )}
      </div>
      <div className="inspector-footer">
        <LockKeyhole size={13} />
        <span>Geometry owned by Rust · f64</span>
      </div>
    </aside>
  );
}
function Property({
  label,
  value,
  dot,
}: {
  label: string;
  value: string;
  dot?: string;
}) {
  return (
    <div className="property">
      <span>{label}</span>
      <span>
        {dot && <i style={{ background: dot }} />}
        {value}
      </span>
    </div>
  );
}

function Toolspace({
  drawing,
  hidden,
  toggleLayer,
  setLayer,
  selectedTab,
  setTab,
}: {
  drawing: Drawing | null;
  hidden: string[];
  toggleLayer: (name: string) => void;
  setLayer: (name: string) => void;
  selectedTab: string;
  setTab: (tab: string) => void;
}) {
  return (
    <aside className="toolspace">
      <div className="panel-heading">
        <span>
          <ListTree size={15} />
          Toolspace
        </span>
        <PanelLeftClose size={14} />
      </div>
      <div className="tabs tabs-border sidebar-tabs" role="tablist">
        {["Drawing", "Objects"].map((tab) => (
          <button
            role="tab"
            aria-selected={selectedTab === tab}
            key={tab}
            className={`tab ${selectedTab === tab ? "tab-active" : ""}`}
            onClick={() => setTab(tab)}
          >
            {tab}
          </button>
        ))}
      </div>
      {selectedTab === "Drawing" ? (
        <>
          <div className="drawing-tree">
            <span>
              <ChevronDown size={13} />
              <File size={15} />
              <b>Survey study</b>
            </span>
            <small>In-memory drawing</small>
          </div>
          <div className="tree-group">
            <div className="tree-caption">
              <span>
                <ChevronDown size={12} />
                <Layers size={14} />
                LAYERS
              </span>
              <span className="tiny-count">4</span>
            </div>
            {layers.map((layer) => (
              <div
                className={`layer-row ${drawing?.layer === layer.name ? "current" : ""}`}
                key={layer.name}
              >
                <button
                  className="layer-name"
                  onClick={() => setLayer(layer.name)}
                  title={`Draw on ${layer.name}`}
                >
                  <span
                    className="layer-swatch"
                    style={{ background: layer.color }}
                  />
                  <span>{layer.name}</span>
                  {drawing?.layer === layer.name && (
                    <span className="current-dot" />
                  )}
                </button>
                <button
                  className="visibility"
                  title={`${hidden.includes(layer.name) ? "Show" : "Hide"} ${layer.name}`}
                  aria-label={`Toggle ${layer.name} visibility`}
                  onClick={() => toggleLayer(layer.name)}
                >
                  {hidden.includes(layer.name) ? (
                    <EyeOff size={13} />
                  ) : (
                    <Eye size={13} />
                  )}
                </button>
              </div>
            ))}
          </div>
          <div className="tree-group">
            <div className="tree-caption">
              <span>
                <ChevronDown size={12} />
                <Crosshair size={14} />
                DRAWING SUMMARY
              </span>
            </div>
            <div className="summary-row">
              <span>Line entities</span>
              <b>{drawing?.lines.length ?? 0}</b>
            </div>
            <div className="summary-row">
              <span>Selected</span>
              <b>{drawing?.selected.length ?? 0}</b>
            </div>
            <div className="summary-row">
              <span>Drawing units</span>
              <b>Feet</b>
            </div>
          </div>
          <div className="tree-group upcoming">
            <div className="tree-caption">
              <span>
                <ChevronRight size={12} />
                <Triangle size={14} />
                SURFACES
              </span>
              <span className="badge badge-ghost">Later</span>
            </div>
            <div className="tree-caption">
              <span>
                <ChevronRight size={12} />
                <Box size={14} />
                BLOCKS
              </span>
              <span className="badge badge-ghost">Later</span>
            </div>
          </div>
        </>
      ) : (
        <div className="object-list">
          {drawing?.lines.map((line) => (
            <div
              key={line.id}
              className={
                drawing.selected.includes(line.id) ? "selected-object" : ""
              }
            >
              <Slash size={13} color={colorFor(line.layer)} />
              <span>Line #{line.id}</span>
              <small>
                {Math.hypot(line.b.x - line.a.x, line.b.y - line.a.y).toFixed(
                  2,
                )}{" "}
                ft
              </small>
            </div>
          ))}
        </div>
      )}
      <div className="workspace-note">
        <span className="note-icon">
          <Activity size={15} />
        </span>
        <strong>A new foundation.</strong>
        <p>
          React on the surface.
          <br />
          Rust at the core.
        </p>
        <span className="badge badge-outline">EXPERIMENTAL SPIKE</span>
      </div>
    </aside>
  );
}

export default function App() {
  const { drawing, error, send } = useEngine();
  const [camera, setCamera] = useState<Camera>({ x: 8, y: 12, scale: 3.8 });
  const [size, setSize] = useState({ width: 800, height: 600 });
  const [cursor, setCursor] = useState<Point>({ x: 0, y: 0 });
  const [pan, setPan] = useState(false),
    [snap, setSnap] = useState(true),
    [grid, setGrid] = useState(true);
  const [hidden, setHidden] = useState<string[]>([]),
    [tab, setTab] = useState("Drafting"),
    [sidebarTab, setSidebarTab] = useState("Drawing");
  const [command, setCommand] = useState(""),
    [zoomPrompt, setZoomPrompt] = useState(false);
  const [historyExpanded, setHistoryExpanded] = useState(false),
    [help, setHelp] = useState(false);
  const input = useRef<HTMLInputElement>(null),
    history = useRef<HTMLDivElement>(null);
  const commandHistory = useRef<string[]>([]),
    historyIndex = useRef(0),
    initialFit = useRef(false);
  const focusCommand = useCallback(
    () => input.current?.focus({ preventScroll: true }),
    [],
  );
  const fitView = useCallback(() => {
    setCamera(
      fit(
        drawing?.lines.filter((l) => !hidden.includes(l.layer)) ?? [],
        size.width,
        size.height,
      ),
    );
  }, [drawing?.lines, hidden, size]);
  useEffect(() => {
    if (drawing && size.width > 0 && !initialFit.current) {
      initialFit.current = true;
      fitView();
    }
  }, [drawing, fitView, size.width]);
  useEffect(() => {
    if (history.current)
      history.current.scrollTop = history.current.scrollHeight;
  }, [drawing?.log.length, historyExpanded]);
  const cancel = useCallback(() => {
    setPan(false);
    setZoomPrompt(false);
    setCommand("");
    void send({ type: "cancel" });
  }, [send]);
  function activate(text: string) {
    startTool(text);
  }
  // Tool activation cancels the pending phase while preserving native preselection.
  function startTool(text: string) {
    setPan(false);
    setZoomPrompt(false);
    void send({ type: "start", text });
    focusCommand();
  }
  function submit(text = command) {
    const upper = text.trim().toUpperCase();
    if (text.trim()) {
      commandHistory.current.push(text);
      historyIndex.current = commandHistory.current.length;
    }
    setCommand("");
    if (pan && !upper) {
      setPan(false);
      return;
    }
    if (upper === "PAN" || upper === "P") {
      void send({ type: "cancel" });
      setPan(true);
      setZoomPrompt(false);
      return;
    }
    if (
      upper === "ZE" ||
      upper === "ZOOM EXTENTS" ||
      (zoomPrompt && (upper === "E" || upper === "EXTENTS"))
    ) {
      fitView();
      setZoomPrompt(false);
      return;
    }
    if (upper === "Z" || upper === "ZOOM") {
      setZoomPrompt(true);
      return;
    }
    if (zoomPrompt) {
      const factor = Number(text);
      if (Number.isFinite(factor) && factor > 0) {
        setCamera((c) => ({
          ...c,
          scale: Math.min(1e7, Math.max(0.000001, c.scale * factor)),
        }));
        setZoomPrompt(false);
      }
      return;
    }
    if (upper === "ESC" || upper === "CANCEL") {
      cancel();
      return;
    }
    setPan(false);
    void send({ type: "text", text });
  }
  useEffect(() => {
    function key(event: KeyboardEvent) {
      const target = event.target as HTMLElement;
      const editing =
        target.tagName === "INPUT" ||
        target.tagName === "TEXTAREA" ||
        target.isContentEditable;
      if (event.key === "Escape") {
        event.preventDefault();
        setHelp(false);
        cancel();
        return;
      }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "z") {
        if (!editing || !command) {
          event.preventDefault();
          void send({ type: event.shiftKey ? "redo" : "undo" });
        }
        return;
      }
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "y") {
        event.preventDefault();
        void send({ type: "redo" });
        return;
      }
      if (event.key === "F8") {
        event.preventDefault();
        void send({ type: "ortho", enabled: !drawing?.ortho });
        return;
      }
      if (event.key === "F3") {
        event.preventDefault();
        setSnap((v) => !v);
        return;
      }
      if (event.key === "F2") {
        event.preventDefault();
        setHistoryExpanded((v) => !v);
        return;
      }
      if (editing || help) return;
      if (event.key === "Delete") {
        event.preventDefault();
        startTool("DELETE");
        return;
      }
      if (event.key === "Enter") {
        event.preventDefault();
        submit("");
        focusCommand();
        return;
      }
      if (
        event.key.length === 1 &&
        !event.ctrlKey &&
        !event.metaKey &&
        !event.altKey
      ) {
        event.preventDefault();
        setCommand((v) => v + event.key);
        focusCommand();
      }
    }
    window.addEventListener("keydown", key);
    return () => window.removeEventListener("keydown", key);
  });
  const selectedCount = drawing?.selected.length ?? 0;
  const active = pan ? "pan" : (drawing?.mode ?? "select");
  const prompt = pan
    ? "PAN · Drag to move view · Enter or Esc to finish"
    : zoomPrompt
      ? "ZOOM · Enter scale factor, or E for extents"
      : (drawing?.prompt ?? "Connecting to Rust engine…");
  return (
    <div className="app-shell">
      <header className="titlebar">
        <div className="brand">
          <span className="brand-mark">
            <Triangle size={20} strokeWidth={1.8} />
          </span>
          <b>
            GoSurvey<span className="brand-period">.</span>
          </b>
          <span className="spike-tag">LABS</span>
        </div>
        <div className="document-location">
          <span>Workspace</span>
          <ChevronRight size={12} />
          <span>Survey study</span>
          <span className="unsaved-dot" title="In-memory prototype" />
        </div>
        <div className="title-actions">
          <span className={`backend-badge ${error ? "disconnected" : ""}`}>
            <i />
            {error ? "Disconnected" : drawing ? "Rust engine" : "Connecting"}
          </span>
          <button
            className="btn btn-ghost btn-square btn-sm"
            aria-label="Command reference"
            onClick={() => setHelp(true)}
          >
            <CircleHelp size={16} />
          </button>
        </div>
      </header>
      <nav className="menubar">
        <div className="ribbon-tabs">
          {["Drafting", "Survey", "View"].map((name) => (
            <button
              key={name}
              className={name === tab ? "selected" : ""}
              onClick={() => setTab(name)}
            >
              {name}
            </button>
          ))}
        </div>
        <div className="quick-actions">
          <button
            className="btn btn-ghost btn-square btn-xs"
            aria-label="Undo"
            title="Undo · Ctrl+Z"
            disabled={!drawing?.can_undo}
            onClick={() => void send({ type: "undo" })}
          >
            <Undo2 size={15} />
          </button>
          <button
            className="btn btn-ghost btn-square btn-xs"
            aria-label="Redo"
            title="Redo · Ctrl+Y"
            disabled={!drawing?.can_redo}
            onClick={() => void send({ type: "redo" })}
          >
            <Redo2 size={15} />
          </button>
          <span className="vertical-rule" />
          <span className="muted text-xs">Drafting workspace</span>
          <ChevronDown size={12} />
        </div>
      </nav>
      <section className="ribbon">
        {tab === "Drafting" ? (
          <>
            <div className="ribbon-group">
              <div className="ribbon-tools">
                <Tool
                  icon={MousePointer2}
                  label="Select"
                  large
                  active={active === "select"}
                  onClick={cancel}
                />
              </div>
              <span className="group-label">SELECTION</span>
            </div>
            <div className="ribbon-group">
              <div className="ribbon-tools">
                <Tool
                  icon={Slash}
                  label="Draw line"
                  shortcut="L"
                  large
                  active={active === "line"}
                  onClick={() => activate("LINE")}
                />
                <div className="stack">
                  <Tool icon={Square} label="Rectangle" disabled />
                  <Tool icon={Ruler} label="Dimension" disabled />
                </div>
              </div>
              <span className="group-label">DRAW</span>
            </div>
            <div className="ribbon-group">
              <div className="ribbon-tools">
                <div className="stack">
                  <Tool
                    icon={Move}
                    label="Move"
                    shortcut="M"
                    active={active === "move"}
                    onClick={() => startTool("MOVE")}
                  />
                  <Tool
                    icon={Copy}
                    label="Copy"
                    shortcut="CO"
                    active={active === "copy"}
                    onClick={() => startTool("COPY")}
                  />
                </div>
                <div className="stack">
                  <Tool
                    icon={Trash2}
                    label="Delete"
                    shortcut="E"
                    disabled={!selectedCount}
                    onClick={() => startTool("DELETE")}
                  />
                  <Tool
                    icon={Undo2}
                    label="Undo"
                    shortcut="U"
                    disabled={!drawing?.can_undo}
                    onClick={() => void send({ type: "undo" })}
                  />
                </div>
              </div>
              <span className="group-label">MODIFY</span>
            </div>
            <div className="ribbon-group layer-tools">
              <div className="ribbon-tools">
                <span className="layer-tool-icon">
                  <Layers size={25} strokeWidth={1.4} />
                </span>
                <div>
                  <label htmlFor="active-layer">Current layer</label>
                  <select
                    id="active-layer"
                    className="select select-bordered select-xs"
                    value={drawing?.layer ?? "Boundary"}
                    onChange={(e) =>
                      void send({ type: "layer", layer: e.target.value })
                    }
                  >
                    {layers.map((l) => (
                      <option key={l.name}>{l.name}</option>
                    ))}
                  </select>
                </div>
              </div>
              <span className="group-label">LAYERS</span>
            </div>
          </>
        ) : tab === "Survey" ? (
          <div className="survey-ribbon">
            <Crosshair size={30} />
            <div>
              <b>Survey tools are next.</b>
              <p>
                This spike proves drafting commands. COGO, points, surfaces and
                file exchange remain outside its scope.
              </p>
            </div>
            <button
              className="btn btn-sm btn-outline"
              onClick={() => setTab("Drafting")}
            >
              Back to drafting
            </button>
          </div>
        ) : (
          <>
            <div className="ribbon-group">
              <div className="ribbon-tools">
                <Tool
                  icon={Hand}
                  label="Pan"
                  large
                  active={pan}
                  onClick={() => submit("PAN")}
                />
                <Tool icon={Expand} label="Extents" large onClick={fitView} />
              </div>
              <span className="group-label">NAVIGATE</span>
            </div>
            <div className="ribbon-group">
              <div className="ribbon-tools">
                <Tool
                  icon={Grid2X2}
                  label="Grid"
                  large
                  active={grid}
                  onClick={() => setGrid((v) => !v)}
                />
                <Tool
                  icon={Focus}
                  label="Object snap"
                  large
                  active={snap}
                  onClick={() => setSnap((v) => !v)}
                />
              </div>
              <span className="group-label">DRAFTING AIDS</span>
            </div>
          </>
        )}
        <div className="ribbon-right">
          <Tool
            icon={Hand}
            label="Pan"
            active={pan}
            onClick={() => submit("PAN")}
          />
          <Tool icon={Expand} label="Fit drawing" onClick={fitView} />
        </div>
      </section>
      <div className="document-tabs">
        <div className="document-tab">
          <File size={13} />
          <span>Survey study</span>
          <span className="tab-dot" />
        </div>
        <span className="memory-label">In memory · sample linework</span>
        <span className="document-spacer" />
        <span className="view-type">MODEL SPACE</span>
      </div>
      <main className="workspace">
        <Toolspace
          drawing={drawing}
          hidden={hidden}
          toggleLayer={(name) =>
            setHidden((v) =>
              v.includes(name) ? v.filter((n) => n !== name) : [...v, name],
            )
          }
          setLayer={(layer) => void send({ type: "layer", layer })}
          selectedTab={sidebarTab}
          setTab={setSidebarTab}
        />
        <div className="drawing-column">
          <div className="drawing-area">
            <Viewport
              drawing={drawing}
              camera={camera}
              setCamera={setCamera}
              send={send}
              pan={pan}
              snap={snap}
              grid={grid}
              hidden={hidden}
              fitView={fitView}
              setSize={setSize}
              onCursor={setCursor}
              focusCommand={focusCommand}
              cancel={cancel}
            />
            <div className="drawing-toolbar">
              <span>
                <span className="live-dot" />
                {pan
                  ? "Pan"
                  : active === "select"
                    ? "Ready to draft"
                    : `${active.toUpperCase()} active`}
              </span>
              <span>
                {selectedCount
                  ? `${selectedCount} selected`
                  : "Shift + click to multiselect"}
              </span>
            </div>
          </div>
          <section
            className={`command-panel ${historyExpanded ? "expanded" : ""}`}
          >
            <button
              className="command-heading"
              onClick={() => setHistoryExpanded((v) => !v)}
            >
              <span>
                <Terminal size={13} />
                COMMAND LINE<span className="keyboard-hint">F2</span>
              </span>
              <Maximize2 size={12} />
            </button>
            <div className="command-history" ref={history} aria-live="polite">
              {drawing?.log.map((line, index) => (
                <div
                  className={line.startsWith("Error:") ? "error-log" : ""}
                  key={index}
                >
                  <span className="log-chevron">›</span>
                  {line}
                </div>
              ))}
            </div>
            <div className="command-prompt">{prompt}</div>
            <form
              className="command-entry"
              onSubmit={(e) => {
                e.preventDefault();
                submit();
              }}
            >
              <ChevronRight size={17} />
              <input
                autoFocus
                ref={input}
                aria-label="Command input"
                placeholder="Type a command…"
                autoComplete="off"
                spellCheck={false}
                value={command}
                onChange={(e) => setCommand(e.target.value)}
                onKeyDown={(e) => {
                  if (e.key === "ArrowUp" || e.key === "ArrowDown") {
                    e.preventDefault();
                    historyIndex.current = Math.max(
                      0,
                      Math.min(
                        commandHistory.current.length,
                        historyIndex.current + (e.key === "ArrowUp" ? -1 : 1),
                      ),
                    );
                    setCommand(
                      commandHistory.current[historyIndex.current] ?? "",
                    );
                  }
                }}
              />
              <span className="entry-hint">
                Enter <ArrowDownRight size={11} />
              </span>
            </form>
          </section>
        </div>
        <Inspector drawing={drawing} />
      </main>
      <footer className="statusbar">
        <div className="model-tab">
          <span className="live-dot" />
          Model
        </div>
        <div className="coordinates">
          <Crosshair size={12} />
          <span>X {format(cursor.x)}</span>
          <span>Y {format(cursor.y)}</span>
          <span className="unit-label">ft</span>
        </div>
        <span className="status-spacer" />
        <button
          className={grid ? "enabled" : ""}
          title="Show grid"
          onClick={() => setGrid((v) => !v)}
        >
          <Grid2X2 size={12} />
          GRID
        </button>
        <button
          className={drawing?.ortho ? "enabled" : ""}
          title="ORTHO · F8"
          onClick={() => void send({ type: "ortho", enabled: !drawing?.ortho })}
        >
          <ArrowUpRight size={12} />
          ORTHO
        </button>
        <button
          className={snap ? "enabled" : ""}
          title="Endpoint object snap · F3"
          onClick={() => setSnap((v) => !v)}
        >
          <Focus size={12} />
          OSNAP
        </button>
        <span className="status-divider" />
        <span className="precision-label">f64</span>
        <span className="ready-label">{drawing ? "READY" : "WAITING"}</span>
      </footer>
      {error && (
        <div className="alert connection-error" role="alert">
          <Activity size={18} />
          <div>
            <b>Native engine unavailable</b>
            <p>{error}</p>
          </div>
        </div>
      )}
      {help && (
        <div
          className="modal modal-open"
          role="dialog"
          aria-modal="true"
          aria-label="Command reference"
        >
          <div className="modal-box help-modal">
            <button
              className="btn btn-sm btn-circle btn-ghost absolute right-3 top-3"
              aria-label="Close help"
              onClick={() => setHelp(false)}
            >
              <X size={16} />
            </button>
            <span className="eyebrow">GOSURVEY LABS</span>
            <h2>Make a little linework.</h2>
            <p>
              Use the ribbon, type a command, or combine both. All committed
              geometry lives in Rust.
            </p>
            <div className="help-commands">
              {[
                [
                  "L / LINE",
                  "Pick or type X,Y, then the next point. Enter ends.",
                ],
                ["M / MOVE", "Select lines, Enter, then base and destination."],
                [
                  "CO / COPY",
                  "Select lines, Enter, then base and destination.",
                ],
                ["E / DELETE", "Remove the current selection."],
                ["U / UNDO", "Undo an edit. REDO brings it back."],
                [
                  "P / PAN",
                  "Left-drag to pan. Enter / Esc / right click exits.",
                ],
                [
                  "Z / ZOOM",
                  "Enter a scale factor, or E for extents. ZE fits directly.",
                ],
                ["@10,5", "Relative coordinates from the current base point."],
              ].map(([key, description]) => (
                <div key={key}>
                  <kbd>{key}</kbd>
                  <span>{description}</span>
                </div>
              ))}
            </div>
            <p className="help-footnote">
              Middle drag pans at any time. Wheel zooms at the cursor. Shift +
              click adds to selection. F3 toggles endpoint snap; F8 toggles
              ORTHO. Nothing is saved to disk in this spike.
            </p>
            <button
              className="btn btn-primary"
              onClick={() => {
                setHelp(false);
                focusCommand();
              }}
            >
              Start drafting <ArrowUpRight size={15} />
            </button>
          </div>
          <button
            className="modal-backdrop"
            aria-label="Dismiss help"
            onClick={() => setHelp(false)}
          />
        </div>
      )}
    </div>
  );
}
