# Qt may present Survey public models

- Status: Accepted
- Date: 2026-11-18

## Context

Editor views and forms consume Survey model types through public Survey headers. Treating these presentation adapters as forbidden consumers left stale Qt → Survey records in the dependency baseline, despite the includes using Survey’s public interface. Moving view presentation into Survey would give the domain system UI ownership; moving it wholesale into GoSurveyEditor would mix application workflows with the Qt adapter files.

## Decision

Qt may depend on Survey’s public model interfaces for editor-facing presentation and form binding. Survey continues to own survey calculations, model policy, and domain behavior. Commands and GoSurveyEditor continue to own command sequencing and product workflows. Qt may not include Survey private headers, and Survey or other engine systems may not depend on Qt.

## Consequences

The system map lists Survey as a Qt dependency, and the architecture ratchet enforces the public-header boundary. This exception does not permit Qt to own Survey rules or let Domain systems depend on platform backends.
