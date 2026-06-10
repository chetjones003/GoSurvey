# Coding Standards

> **Template.** The rules implementation must follow, and the rules reviews
> enforce. Standards exist to make code *readable, debuggable, and consistent* —
> not to enforce taste for its own sake. Where this project has a root
> `CLAUDE.md` or `.editorconfig`, those remain authoritative for the specifics;
> this document states the principles and the cross-language conventions.

---

## 1. Guiding principle

> The best code is easy to read, easy to debug, easy to modify, fast enough, and
> free of unnecessary abstraction. When in doubt, choose the simpler solution.

Readability beats cleverness. A junior engineer should be able to follow the
control flow without a debugger.

## 2. Simplicity first

Before introducing an interface, trait, template, generic, macro, design
pattern, dependency-injection seam, or "system," ask:

> Does the project need this **today**, with two or more real call sites?

If not, write the concrete version. Abstractions are earned by duplication that
already exists, never granted in anticipation of it.

```cpp
// Over-engineered — speculative tower of indirection
struct ICommandFactory { virtual ~ICommandFactory() = default; /*...*/ };
struct ICommandRegistry { /*...*/ };
struct ICommandProvider { /*...*/ };

// Right-sized — one concrete type that solves today's problem
class CommandManager {
public:
    void Register(std::string name, CommandFn fn);
    bool Execute(const std::string& line);
};
```

## 3. Ownership and lifetime

- Make ownership explicit and singular (see `architecture.md` §4).
- Borrowed pointers/references never free.
- Reach for shared ownership only with a recorded justification.

```cpp
// C++: owner holds unique_ptr; collaborators borrow raw/ref
class Editor {
    std::unique_ptr<Document> m_Document;   // owns
    Renderer* m_Renderer;                   // borrows — owned by App
};
```

```rust
// Rust: the type system already enforces this — lean into it
fn render(scene: &Scene, target: &mut Framebuffer) { /* borrows, owns nothing */ }
```

```zig
// Zig: allocator is explicit; pair every alloc with a defer
const buf = try allocator.alloc(Vertex, count);
defer allocator.free(buf);
```

```go
// Go: own the handle, give it an explicit Close
type File struct { f *os.File }
func (h *File) Close() error { return h.f.Close() }
```

## 4. Const / immutability

Prefer immutable data; make mutation visible.

- **C++:** use `const` aggressively — `const` params, `const` methods, `constexpr`
  constants. Pass large objects by `const&`.
- **Rust:** default `let` (immutable); `mut` is a deliberate, visible choice.
- **Zig:** prefer `const`; mark `var` only when you mutate.
- **Go:** no `const` for structs — minimize exported mutability; copy small
  values, pass pointers only when mutation or size demands it.

## 5. Naming conventions

Follow each language's idiomatic casing; do not import one language's style into
another.

| Element | C++ | Rust | Zig | Go |
|---------|-----|------|-----|----|
| Types | `PascalCase` | `PascalCase` | `PascalCase` | `PascalCase` (exported) |
| Functions | `PascalCase`* | `snake_case` | `camelCase` | `PascalCase`/`camelCase` by export |
| Variables | `camelCase` | `snake_case` | `snake_case` | `camelCase` |
| Members | `m_Member` | `snake_case` | `snake_case` | `camelCase` |
| Statics/globals | `s_Name` | `SCREAMING_SNAKE` (const) | `SCREAMING_SNAKE` | `PascalCase`/`camelCase` |
| Constants | `constexpr GridSize` | `const GRID_SIZE` | `const grid_size` | `GridSize` |

\* This project's C++ convention uses `PascalCase` functions and the `m_`/`s_`
prefixes per `CLAUDE.md`. Adjust this column to your house style and keep it
consistent — consistency matters more than the specific choice.

Naming substance (all languages):

- Names say *what* and *why*, not *how*. `closureError`, not `tmp2`.
- No Hungarian-type noise (`iCount`, `pszName`).
- Booleans read as predicates: `isClosed`, `hasError`.

## 6. Functions

- One job per function. If you need "and" to describe it, split it.
- Keep them short enough to see whole; deep nesting is a refactor signal —
  prefer early returns / guard clauses.
- Parameter order: inputs first, output/sink last; group related params into a
  small struct rather than passing six positional arguments.

## 7. Error handling

Match mechanism to failure kind, consistently (see `architecture.md` §9).

```cpp
// Programmer error: assert the invariant
ASSERT(vertexBuffer != nullptr);

// Recoverable: report, don't swallow
if (!file.Open(path)) {
    LOG_ERROR("Failed to open {}", path);
    return false;
}
```

```rust
let scene = load_scene(path)?;            // propagate recoverable errors
debug_assert!(!vertices.is_empty());      // invariant
```

```zig
const f = std.fs.cwd().openFile(path, .{}) catch |err| {
    log.err("open failed: {}", .{err});
    return err;
};
```

```go
data, err := os.ReadFile(path)
if err != nil {
    return fmt.Errorf("read %s: %w", path, err)   // wrap, don't discard
}
```

No empty error branches. Handle, return, or assert.

## 8. Comments

Code says *what*. Comments say *why*.

```cpp
// Bad
i++; // increment i

// Good
i++; // Skip the sentinel node stored at index 0.
```

- Comment non-obvious decisions, units, invariants, and gotchas.
- Delete commented-out code; that is what version control is for.
- Doc-comment public APIs (`///`, `//!`, `/** */`, Go doc comments) — the
  signature plus one line of intent.

## 9. Logging

- Log initialization, resource loading, warnings, and errors.
- Never log inside hot paths or tight loops.

```cpp
// Bad — floods the log and trashes the frame budget
for (auto& v : vertices) LOG_INFO("drawing vertex");
```

## 10. Performance discipline

- **Measure first.** No optimization without a profile that named the cost.
- Minimize allocations; pre-size containers (`vec.reserve(n)`,
  `make([]T, 0, n)`, `try list.ensureTotalCapacity(n)`).
- Pass large objects by reference/borrow, never by value into hot paths.
- Avoid virtual dispatch in rendering loops and other measured hot paths.
- Know your data's memory layout; favor contiguous storage for hot iteration.

```cpp
std::vector<Vertex> vertices;
vertices.reserve(expectedCount);   // one allocation, not N
```

## 11. Formatting and tooling

Let tools enforce mechanical style so reviews can focus on substance.

| Language | Formatter | Linter / analyzer |
|----------|-----------|-------------------|
| C++ | `clang-format` | `clang-tidy`, compiler `-Wall -Wextra` |
| Rust | `rustfmt` | `clippy` (deny warnings in CI) |
| Zig | `zig fmt` | compiler + tests |
| Go | `gofmt`/`goimports` | `go vet`, `staticcheck` |

- Warnings are errors in CI.
- Commit formatter config to the repo; never hand-format around it.

## 12. Tests

- Every `must` requirement has a happy-path **and** a failure-mode test.
- Assert numeric results against a tolerance, never exact float equality.
- Tests are deterministic and independent — no shared mutable global state, no
  ordering dependencies.
- A test result counts only after the test actually ran.

## 13. Review order

Reviewers (and self-review) check in this order; never block on a lower tier
while a higher one is open:

1. **Correctness** — does it meet the requirement and handle failure?
2. **Architecture** — layering, ownership, no unjustified abstraction?
3. **Performance** — any hot-path regression, backed by measurement?
4. **Style** — naming, const, comments, formatting.

> Readability and maintainability outrank cleverness at every tier.
