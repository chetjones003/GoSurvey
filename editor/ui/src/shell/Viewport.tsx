import { Compass, Maximize, Minus, Plus } from "lucide-react";
import { Button } from "@/components/ui/button";

type ViewportProps = {
  name?: string;
  entityCount?: number;
};

export default function Viewport({ name, entityCount }: ViewportProps) {
  return (
    <section aria-label="Drawing" className="relative min-h-0 min-w-0 overflow-hidden bg-zinc-950">
      <svg className="absolute inset-0 size-full" role="img" aria-label="Empty drawing grid">
        <defs>
          <pattern id="grid" width="24" height="24" patternUnits="userSpaceOnUse">
            <path d="M 24 0 L 0 0 0 24" fill="none" className="stroke-zinc-800" strokeWidth="1" />
          </pattern>
        </defs>
        <rect width="100%" height="100%" fill="url(#grid)" />
      </svg>

      {name !== undefined && (
        <header className="absolute top-3 left-4 flex items-baseline gap-3">
          <h1 className="text-base font-semibold">{name}</h1>
          <p className="text-xs text-muted-foreground">{entityCount} entities</p>
        </header>
      )}
      <p className="absolute top-3 left-1/2 -translate-x-1/2 text-xs text-muted-foreground">
        [Top] [2D Wireframe]
      </p>

      <div className="absolute top-3 right-4 flex size-10 items-center justify-center rounded-full border text-muted-foreground" aria-hidden="true">
        <Compass className="size-5" />
      </div>
      <div className="absolute top-1/3 right-4 flex flex-col overflow-hidden rounded-md border bg-background">
        <Button variant="ghost" size="icon" aria-label="Zoom in" disabled>
          <Plus />
        </Button>
        <Button variant="ghost" size="icon" aria-label="Zoom out" disabled>
          <Minus />
        </Button>
        <Button variant="ghost" size="icon" aria-label="Zoom to extents" disabled>
          <Maximize />
        </Button>
      </div>

      <div className="absolute bottom-3 left-4 text-xs text-muted-foreground" aria-hidden="true">
        <span className="mr-3 text-red-400">X</span>
        <span className="text-emerald-400">Y</span>
      </div>
      <div className="absolute right-4 bottom-3 flex flex-col items-end text-[10px] text-muted-foreground" aria-hidden="true">
        <span className="mb-1 h-px w-24 bg-muted-foreground" />
        <span>50 ft</span>
      </div>
    </section>
  );
}
