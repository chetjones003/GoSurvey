import { MousePointer2, Settings2, Slash } from "lucide-react";
import { Badge } from "@/components/ui/badge";

const general = [
  { label: "Object type", value: "—" },
  { label: "Layer", value: "—" },
  { label: "Color", value: "By layer" },
  { label: "Lineweight", value: "—" },
  { label: "Units", value: "—" },
];

export default function PropertiesPanel() {
  return (
    <aside aria-label="Properties" className="flex min-h-0 flex-col border-l">
      <header className="flex h-10 items-center gap-2 border-b px-3">
        <Settings2 className="size-4" aria-hidden="true" />
        <span className="text-sm font-medium">Properties</span>
        <Badge variant="secondary" className="ml-auto">0</Badge>
      </header>

      <div className="flex items-center gap-3 border-b px-3 py-4">
        <div className="flex size-9 items-center justify-center rounded-md bg-muted">
          <Slash className="size-4" aria-hidden="true" />
        </div>
        <div>
          <p className="text-sm font-medium">No selection</p>
          <p className="text-xs text-muted-foreground">Select an object to inspect</p>
        </div>
      </div>

      <section className="border-b px-3 py-3">
        <h2 className="mb-2 text-[11px] tracking-wider text-muted-foreground uppercase">General</h2>
        <dl className="flex flex-col gap-2 text-sm">
          {general.map((row) => (
            <div key={row.label} className="flex items-center justify-between">
              <dt className="text-muted-foreground">{row.label}</dt>
              <dd className="font-mono text-xs">{row.value}</dd>
            </div>
          ))}
        </dl>
      </section>

      <section className="flex flex-1 flex-col items-center justify-center gap-2 px-6 py-6 text-center">
        <MousePointer2 className="size-6 text-muted-foreground" aria-hidden="true" />
        <h2 className="text-[11px] tracking-wider text-muted-foreground uppercase">Geometry</h2>
        <p className="text-xs text-muted-foreground">Pick linework in the drawing.</p>
        <p className="text-xs text-muted-foreground">Shift + click adds to selection.</p>
      </section>
    </aside>
  );
}
