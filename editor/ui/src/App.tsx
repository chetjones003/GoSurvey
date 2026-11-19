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
type DrawingSnapshot = {
  name: string;
  entityCount: number;
  lines: { start: { x: number; y: number }; end: { x: number; y: number } }[];
};

export default function App() {
  const [status, setStatus] = useState<EngineStatus | null>(null);
  const [drawing, setDrawing] = useState<DrawingSnapshot | null>(null);
  const [error, setError] = useState("");
  const [history, setHistory] = useState<string[]>(["GoSurvey Rust engine ready. Type HELP for commands."]);

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

  const execute = async (command: string) => {
    try {
      const message = await invoke<string>("execute_command", { input: command });
      setHistory((entries) => [...entries, `> ${command}`, message]);
      await load();
    } catch (reason) {
      setHistory((entries) => [...entries, `> ${command}`, String(reason)]);
    }
  };

  return (
    <div className="grid h-full grid-rows-[auto_auto_minmax(0,1fr)_auto_auto] bg-background text-foreground">
      <TopBar />
      <Ribbon />
      <div className="grid min-h-0 grid-cols-[240px_minmax(0,1fr)_280px]">
        <Toolspace entityCount={drawing?.entityCount} />
        <Viewport name={drawing?.name} entityCount={drawing?.entityCount} lines={drawing?.lines ?? []} />
        <PropertiesPanel />
      </div>
      <CommandLine history={history} onSubmit={execute} />
      <StatusBar engineMessage={engineMessage} engineError={error !== ""} />
    </div>
  );
}
