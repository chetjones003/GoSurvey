# GRADING

**Command:** `GRADING` — aliases: `GRD`
**Category overview:** [[Point Groups and Surfaces]]

Grades side slopes from a feature line out to a target, giving the daylight line.

### Workflow

1. **Baseline**: select ONE feature line. Draw one with [[Commands/featureline]] first.
2. **Options**, then **Enter** to grade:
   - `C` / `F`: cut and fill slopes, as run:rise (`3:1`), a bare run (`3`), or a percent.
   - `S`: choose the target surface.
   - `T`: target type: `Surface`, `Distance`, `Absolute` elevation, or `Relative` grade percent.
   - `Group <name>[, Add/Remove/List]`: manage the grading group of selected results.

Select an existing grading result before running `GRADING` to edit it. **Esc** cancels.

See also [[Commands/gradinggroup]] and [[Commands/gradinginfill]].

---

## Related

- [[Point Groups and Surfaces]]
- [[Command Reference]]
