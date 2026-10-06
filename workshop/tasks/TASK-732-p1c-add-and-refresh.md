# TASK-732-p1c - Add PDF to project + Refresh (REQ-379 cl. 5-6, D-2026-10-06-c/-d)

- Status: submitted
- Scope: Project Files Add PDF button; Toolspace Refresh button + "New files found" question; projfiles::TrackFile / FindUntracked; tests [req379][issue732].
- Assumption (recorded in D-2026-10-06-d): "new files" = .dwg .pdf .e57; hidden folders and project bookkeeping files never offered; no automatic scan.
- Verified: unit tests (98 assertions), p696-e2e 269/0. Not verified by a person: the buttons and the question window.
