import { Toggle } from "@/components/ui/toggle";
import { Badge } from "@/components/ui/badge";

type StatusBarProps = {
  engineMessage: string;
  engineError: boolean;
};

const aids = [
  { label: "GRID", pressed: true },
  { label: "ORTHO", pressed: false },
  { label: "POLAR", pressed: false },
  { label: "OSNAP", pressed: true },
];

export default function StatusBar({ engineMessage, engineError }: StatusBarProps) {
  return (
    <footer className="flex h-9 items-center gap-4 border-t bg-background px-3 text-xs">
      {engineError ? (
        <p role="alert" className="text-red-400">
          {engineMessage}
        </p>
      ) : (
        <p className="flex items-center gap-2 text-muted-foreground">
          <span className="size-2 rounded-full bg-emerald-500" aria-hidden="true" />
          {engineMessage}
        </p>
      )}
      <span className="font-mono text-muted-foreground" aria-label="Cursor position">
        X — &nbsp; Y —
      </span>
      <Badge variant="outline" className="ml-auto text-[10px]">Model</Badge>
      <div role="group" aria-label="Drafting aids" className="flex items-center gap-1">
        {aids.map((aid) => (
          <Toggle key={aid.label} size="sm" defaultPressed={aid.pressed} disabled className="h-6 px-2 text-[11px]">
            {aid.label}
          </Toggle>
        ))}
      </div>
      <span className="font-mono text-muted-foreground">READY</span>
    </footer>
  );
}
