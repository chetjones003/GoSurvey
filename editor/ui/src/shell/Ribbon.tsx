import { Copy, Eraser, Hand, Layers, Maximize, MousePointer2, Move, Redo2, Ruler, Slash, Undo2 } from "lucide-react";
import { Button } from "@/components/ui/button";
import { Input } from "@/components/ui/input";
import { Separator } from "@/components/ui/separator";
import { Tabs, TabsList, TabsTrigger } from "@/components/ui/tabs";

type RibbonButtonProps = {
  label: string;
  shortcut?: string;
  icon: React.ReactNode;
  large?: boolean;
};

function RibbonButton({ label, shortcut, icon, large = false }: RibbonButtonProps) {
  return (
    <Button
      variant="ghost"
      disabled
      className={large ? "h-auto flex-col gap-1 px-3 py-2" : "h-7 justify-start gap-2 px-2"}
    >
      {icon}
      <span>{label}</span>
      {shortcut && <kbd className="ml-1 text-[10px] text-muted-foreground">{shortcut}</kbd>}
    </Button>
  );
}

function RibbonGroup({ name, children }: { name: string; children: React.ReactNode }) {
  return (
    <section aria-label={name} className="flex h-full flex-col justify-between gap-1 px-2">
      <div className="flex items-start gap-1">{children}</div>
      <span className="text-center text-[10px] tracking-wider text-muted-foreground uppercase">{name}</span>
    </section>
  );
}

export default function Ribbon() {
  return (
    <div className="border-b">
      <Tabs defaultValue="drafting">
        <div className="flex items-center justify-between px-3">
          <TabsList variant="line" aria-label="Ribbon tabs">
            <TabsTrigger value="drafting">Drafting</TabsTrigger>
            <TabsTrigger value="survey">Survey</TabsTrigger>
            <TabsTrigger value="view">View</TabsTrigger>
          </TabsList>
          <div className="flex items-center gap-1 text-sm text-muted-foreground">
            <Button variant="ghost" size="icon" aria-label="Undo" disabled>
              <Undo2 />
            </Button>
            <Button variant="ghost" size="icon" aria-label="Redo" disabled>
              <Redo2 />
            </Button>
            <Separator orientation="vertical" className="mx-2 h-5" />
            <span>Drafting workspace</span>
          </div>
        </div>
      </Tabs>
      <div className="flex h-24 items-stretch gap-1 px-2 py-1">
        <RibbonGroup name="Selection">
          <RibbonButton label="Select" icon={<MousePointer2 />} large />
        </RibbonGroup>
        <Separator orientation="vertical" />
        <RibbonGroup name="Draw">
          <RibbonButton label="Draw line" icon={<Slash />} large />
          <div className="flex flex-col gap-1">
            <RibbonButton label="Rectangle" icon={<Layers />} />
            <RibbonButton label="Dimension" icon={<Ruler />} />
          </div>
        </RibbonGroup>
        <Separator orientation="vertical" />
        <RibbonGroup name="Modify">
          <div className="flex flex-col gap-1">
            <RibbonButton label="Move" shortcut="M" icon={<Move />} />
            <RibbonButton label="Copy" shortcut="CO" icon={<Copy />} />
          </div>
          <div className="flex flex-col gap-1">
            <RibbonButton label="Delete" shortcut="E" icon={<Eraser />} />
            <RibbonButton label="Undo" shortcut="U" icon={<Undo2 />} />
          </div>
        </RibbonGroup>
        <Separator orientation="vertical" />
        <RibbonGroup name="Layers">
          <div className="flex flex-col gap-1 px-1">
            <span className="text-xs text-muted-foreground">Current layer</span>
            <Input aria-label="Current layer" defaultValue="Boundary" className="h-7 w-44" disabled />
          </div>
        </RibbonGroup>
        <div className="ml-auto flex flex-col justify-center gap-1">
          <RibbonButton label="Pan" icon={<Hand />} />
          <RibbonButton label="Fit drawing" icon={<Maximize />} />
        </div>
      </div>
    </div>
  );
}
