import { ChevronRight, Maximize2, Terminal } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";

export default function CommandLine() {
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
      <div aria-label="Command history" className="flex min-h-10 flex-col gap-1 font-mono text-xs text-muted-foreground">
        <p className="flex items-center gap-2">
          <ChevronRight className="size-3" aria-hidden="true" />
          GoSurvey Rust engine ready. Type HELP for commands.
        </p>
      </div>
      <p className="text-xs text-muted-foreground">Type a command or select linework</p>
      <div className="flex items-center gap-2 rounded-md border px-3">
        <ChevronRight className="size-4 text-muted-foreground" aria-hidden="true" />
        <Input
          aria-label="Command"
          placeholder="Type a command..."
          className="h-9 border-0 bg-transparent px-0 font-mono shadow-none focus-visible:ring-0"
        />
        <span className="text-[11px] text-muted-foreground">Enter</span>
      </div>
    </section>
  );
}
