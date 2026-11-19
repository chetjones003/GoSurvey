import { ChevronDown, Eye, FileText, Layers, PanelLeftClose } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";

const layers = [
  { name: "Boundary", color: "bg-zinc-200", active: true },
  { name: "Easements", color: "bg-amber-400", active: false },
  { name: "Structures", color: "bg-indigo-300", active: false },
  { name: "Draft", color: "bg-blue-500", active: false },
];

type ToolspaceProps = {
  entityCount?: number;
};

function Section({ title, count, children }: { title: string; count?: string; children: React.ReactNode }) {
  return (
    <section className="border-b py-3">
      <header className="mb-2 flex items-center gap-2 px-3 text-[11px] tracking-wider text-muted-foreground uppercase">
        <ChevronDown className="size-3" aria-hidden="true" />
        <span>{title}</span>
        {count !== undefined && <Badge variant="secondary" className="ml-auto">{count}</Badge>}
      </header>
      {children}
    </section>
  );
}

export default function Toolspace({ entityCount = 0 }: ToolspaceProps) {
  return (
    <aside aria-label="Toolspace" className="flex min-h-0 flex-col border-r">
      <header className="flex h-10 items-center justify-between border-b px-3">
        <span className="text-sm font-medium">Toolspace</span>
        <Button variant="ghost" size="icon" aria-label="Collapse toolspace" disabled>
          <PanelLeftClose />
        </Button>
      </header>
      <Tabs defaultValue="drawing" className="px-2 pt-2">
        <TabsList aria-label="Toolspace tabs" className="w-full">
          <TabsTrigger value="drawing" className="flex-1">Drawing</TabsTrigger>
          <TabsTrigger value="objects" className="flex-1">Objects</TabsTrigger>
        </TabsList>
      </Tabs>
      <div className="min-h-0 flex-1 overflow-auto">
        <div className="flex items-center gap-2 border-b px-3 py-3">
          <FileText className="size-4" aria-hidden="true" />
          <div>
            <p className="text-sm font-medium">Survey study</p>
            <p className="text-xs text-muted-foreground">In-memory drawing</p>
          </div>
        </div>
        <Section title="Layers" count={String(layers.length)}>
          <ul className="flex flex-col">
            {layers.map((layer) => (
              <li
                key={layer.name}
                className={`flex items-center gap-2 px-3 py-1.5 text-sm ${layer.active ? "bg-accent" : ""}`}
              >
                <span className={`size-2.5 rounded-sm ${layer.color}`} aria-hidden="true" />
                <span className="flex-1">{layer.name}</span>
                <Eye className="size-3.5 text-muted-foreground" aria-label={`${layer.name} visible`} />
              </li>
            ))}
          </ul>
        </Section>
        <Section title="Drawing summary">
          <dl className="grid grid-cols-[1fr_auto] gap-x-3 gap-y-1.5 px-3 text-sm">
            <dt className="text-muted-foreground">Line entities</dt>
            <dd className="font-mono tabular-nums">{entityCount}</dd>
            <dt className="text-muted-foreground">Selected</dt>
            <dd className="font-mono tabular-nums">0</dd>
          </dl>
        </Section>
        <Section title="Surfaces">
          <p className="flex items-center gap-2 px-3 text-sm text-muted-foreground">
            <Layers className="size-4" aria-hidden="true" />
            <span className="flex-1">Surfaces</span>
            <Badge variant="outline">Later</Badge>
          </p>
        </Section>
        <Section title="Blocks">
          <p className="flex items-center gap-2 px-3 text-sm text-muted-foreground">
            <span className="flex-1">Blocks</span>
            <Badge variant="outline">Later</Badge>
          </p>
        </Section>
      </div>
    </aside>
  );
}
