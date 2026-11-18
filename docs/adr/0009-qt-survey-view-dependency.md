# Qt may present public domain models

- Status: Accepted
- Date: 2026-11-18

## Context

Editor views and forms consume Survey and Terrain model types through their public headers. Treating these presentation adapters as forbidden consumers left stale Qt → domain records in the dependency baseline, despite the includes using public system interfaces. Moving view presentation into domain systems would give those systems UI ownership; moving it wholesale into GoSurveyEditor would mix application workflows with the Qt adapter files.

## Decision

Qt may depend on public Survey and Terrain model interfaces for editor-facing presentation and form binding. These systems continue to own their calculations, model policy, and domain behavior. Commands and GoSurveyEditor continue to own command sequencing and product workflows. Qt may not include private domain headers, and domain or other engine systems may not depend on Qt.

## Consequences

The system map lists Survey and Terrain as Qt dependencies. The architecture ratchet enforces the public-header boundary. This exception does not permit Qt to own domain rules or let domain systems depend on platform backends.
