import { useCallback, useEffect, useState } from "react";
import { invoke } from "@tauri-apps/api/core";
import { Button } from "@ui-sdk/react/button";

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

  return (
    <main className="shell">
      <header className="status">
        {error ? (
          <>
            <p role="alert">{error}</p>
            <Button onClick={load}>Retry</Button>
          </>
        ) : (
          <p>{status ? "Engine ready" : "Starting engine"}</p>
        )}
      </header>
      <section className="drawing" aria-label="Drawing">
        {drawing && (
          <>
            <h1>{drawing.name}</h1>
            <p>{drawing.entityCount} entities</p>
          </>
        )}
      </section>
    </main>
  );
}
