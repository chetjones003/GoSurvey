import { useCallback, useEffect, useRef, useState } from "react";
import { invoke, isTauri } from "@tauri-apps/api/core";
import type { Drawing, EngineEvent } from "./types";
export function useEngine() {
  const [drawing, setDrawing] = useState<Drawing | null>(null);
  const [error, setError] = useState("");
  const queue = useRef<Promise<void>>(Promise.resolve());
  const send = useCallback((event: EngineEvent) => {
    queue.current = queue.current.then(async () => {
      try {
        if (!isTauri())
          throw new Error(
            "Open this spike with npm run desktop to connect the Rust engine.",
          );
        setDrawing(await invoke<Drawing>("dispatch", { event }));
        setError("");
      } catch (reason) {
        setError(String(reason));
      }
    });
    return queue.current;
  }, []);
  useEffect(() => {
    void send({ type: "snapshot" });
  }, [send]);
  // A frontend remount/HMR reload asks the living native owner for its current document.
  return { drawing, error, send };
}
