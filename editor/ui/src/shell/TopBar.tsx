import { CircleHelp, Triangle } from "lucide-react";
import { Badge } from "@/components/ui/badge";
import { Button } from "@/components/ui/button";

export default function TopBar() {
  return (
    <header className="flex h-12 items-center justify-between border-b px-4">
      <div className="flex items-center gap-3">
        <Triangle className="size-5" aria-hidden="true" />
        <span className="text-base font-semibold">GoSurvey.</span>
        <Badge variant="outline" className="text-[10px] tracking-wider">
          LABS
        </Badge>
        <nav aria-label="Breadcrumb" className="ml-4 flex items-center gap-2 text-sm text-muted-foreground">
          <span>Workspace</span>
          <span aria-hidden="true">›</span>
          <span className="text-foreground">Survey study</span>
        </nav>
      </div>
      <div className="flex items-center gap-3 text-sm text-muted-foreground">
        <span className="flex items-center gap-2">
          <span className="size-2 rounded-full bg-emerald-500" aria-hidden="true" />
          Rust engine
        </span>
        <Button variant="ghost" size="icon" aria-label="Help" disabled>
          <CircleHelp />
        </Button>
      </div>
    </header>
  );
}
