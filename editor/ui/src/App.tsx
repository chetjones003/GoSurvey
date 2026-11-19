import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import CommandLine from "./shell/CommandLine";
import { describeCommandInput, type CommandInput } from "./shell/commandInput";
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
type CommandPrompt = { text: string; acceptsBlank: boolean; acceptsPoint: boolean };
type CommandResult = { messages: string[]; prompt: CommandPrompt | null };

export default function App() {
  const [status, setStatus] = useState<EngineStatus | null>(null);
  const [drawing, setDrawing] = useState<DrawingSnapshot | null>(null);
  const [error, setError] = useState("");
  const [history, setHistory] = useState<string[]>(["GoSurvey Rust engine ready. Type HELP for commands."]);
  const [commandPrompt, setCommandPrompt] = useState<CommandPrompt | null>(null);

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

  const dispatchCommandInput = async (input: CommandInput) => {
    try {
      const result = await invoke<CommandResult>("execute_command", { input });
      setHistory((entries) => [...entries, ...result.messages]);
      setCommandPrompt(result.prompt);
      await load();
    } catch (reason) {
      setHistory((entries) => [...entries, `> ${describeCommandInput(input)}`, String(reason)]);
    }
  };

  const submitViewportPoint = (point: { x: number; y: number }) => {
    if (commandPrompt?.acceptsPoint) {
      void dispatchCommandInput({ type: "pointPicked", point });
    }
  };

  return (
    <div className="grid h-full grid-rows-[auto_auto_minmax(0,1fr)_auto_auto] bg-background text-foreground">
      <TopBar />
      <Ribbon />
      <div className="grid min-h-0 grid-cols-[240px_minmax(0,1fr)_280px]">
        <Toolspace entityCount={drawing?.entityCount} />
        <Viewport
          name={drawing?.name}
          entityCount={drawing?.entityCount}
          lines={drawing?.lines ?? []}
          acceptsPoint={commandPrompt?.acceptsPoint ?? false}
          onPoint={submitViewportPoint}
        />
        <PropertiesPanel />
      </div>
      <CommandLine history={history} prompt={commandPrompt} onSubmit={dispatchCommandInput} />
      <StatusBar engineMessage={engineMessage} engineError={error !== ""} />
    </div>
  );
}
