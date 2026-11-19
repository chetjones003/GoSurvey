import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import CommandLine from "./shell/CommandLine";
import PropertiesPanel from "./shell/PropertiesPanel";
import Ribbon from "./shell/Ribbon";
import StatusBar from "./shell/StatusBar";
import Toolspace from "./shell/Toolspace";
import TopBar from "./shell/TopBar";
import Viewport from "./shell/Viewport";

type EngineStatus = { state: "ready"; version: string };
type DrawingSnapshot = { name: string; entityCount: number };

export default function App() {
  const [status, setStatus] = useState<EngineStatus | null>(null);
  const [drawing, setDrawing] = useState<DrawingSnapshot | null>(null);
  const [error, setError] = useState("");

  const load = useCallback(async () => {
    try {
      const [nextStatus, nextDrawing] = await Promise.all([
        invoke<EngineStatus>("engine_status"),
        invoke<DrawingSnapshot>("drawing_snapshot"),
      ]);
      setStatus(nextStatus);
      setDrawing(nextDrawing);
      setError("");
    } catch (reason) {
      setStatus(null);
      setDrawing(null);
      setError(String(reason));
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const engineMessage = error || (status ? "Engine ready" : "Starting engine");

  return (
    <div className="grid h-full grid-rows-[auto_auto_minmax(0,1fr)_auto_auto] bg-background text-foreground">
      <TopBar />
      <Ribbon />
      <div className="grid min-h-0 grid-cols-[240px_minmax(0,1fr)_280px]">
        <Toolspace entityCount={drawing?.entityCount} />
        <Viewport name={drawing?.name} entityCount={drawing?.entityCount} />
        <PropertiesPanel />
      </div>
      <CommandLine />
      <StatusBar engineMessage={engineMessage} engineError={error !== ""} />
    </div>
  );
}
