import { useState, type FormEvent } from "react";
import { ChevronRight, Maximize2, Terminal } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import type { CommandInput } from "./commandInput";

type CommandLineProps = {
  history: string[];
  prompt: { text: string; acceptsBlank: boolean; acceptsPoint: boolean } | null;
  onSubmit: (input: CommandInput) => void;
};

export default function CommandLine({ history, prompt, onSubmit }: CommandLineProps) {
  const [command, setCommand] = useState("");
  const submit = (event: FormEvent<HTMLFormElement>) => {
    event.preventDefault();
    const input = command.trim();
    if (!input) {
      if (prompt?.acceptsBlank) onSubmit({ type: "textSubmitted", text: "" });
      return;
    }
    onSubmit({ type: "textSubmitted", text: input });
    setCommand("");
  };

  return (
    <section aria-label="Command line" className="flex flex-col gap-2 border-t bg-background px-3 py-2">
      <header className="flex items-center gap-2 text-[11px] tracking-wider text-muted-foreground uppercase">
        <Terminal className="size-3.5" aria-hidden="true" />
        <span>Command line</span>
        <Badge variant="outline" className="text-[10px]">F2</Badge>
        <Button variant="ghost" size="icon" className="ml-auto size-6" aria-label="Expand command line" disabled>
          <Maximize2 />
        </Button>
      </header>
      <div aria-label="Command history" className="flex min-h-10 max-h-24 flex-col gap-1 overflow-auto font-mono text-xs text-muted-foreground">
        {history.map((entry, index) => (
          <p key={`${index}-${entry}`} className="flex items-center gap-2">
            <ChevronRight className="size-3 shrink-0" aria-hidden="true" />
            <span>{entry}</span>
          </p>
        ))}
      </div>
      <p aria-live="polite" className="text-xs text-muted-foreground">{prompt?.text ?? "Type a command or select linework"}</p>
      <form onSubmit={submit} className="flex items-center gap-2 rounded-md border px-3">
        <ChevronRight className="size-4 text-muted-foreground" aria-hidden="true" />
        <Input
          aria-label="Command"
          placeholder="Type a command..."
          className="h-9 border-0 bg-transparent px-0 font-mono shadow-none focus-visible:ring-0"
          value={command}
          onChange={(event) => setCommand(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Escape" && prompt !== null) {
              event.preventDefault();
              onSubmit({ type: "cancelled" });
              setCommand("");
            }
          }}
        />
        <span className="text-[11px] text-muted-foreground">Enter</span>
      </form>
    </section>
  );
}
