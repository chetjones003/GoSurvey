# CLAUDE.md

## Overview

Core values:

* Simple code is preferred over clever code.
* Explicitness is preferred over magic.
* Minimize dependencies.
* Build systems should be understandable.
* Performance matters, but only after measurement.
* Architecture should emerge from requirements, not speculation.
* Prefer solving today's problem over inventing frameworks for future problems.
* Code should be easy to debug.
* Avoid unnecessary abstractions.

---

# General Rules

## Keep Things Simple

Before introducing:

* Interfaces
* Inheritance
* Templates
* Design patterns
* Dependency injection
* Generic systems

Ask:

> Does the project actually need this today?

Favor straightforward implementations until multiple concrete use cases justify abstraction.

---

## Avoid Premature Generalization

Do not build systems for hypothetical future requirements.

Bad:

```cpp
ICommandFactory
ICommandRegistry
ICommandProvider
ICommandExecutor
```

when:

```cpp
class CommandManager
```

solves the problem.

Create abstractions only after duplication appears.

---

## Prefer Composition Over Inheritance

Prefer:

```cpp
class Camera
{
private:
    Transform m_Transform;
};
```

instead of deep inheritance hierarchies.

Inheritance should be rare and justified.

---

## Explicit Ownership

Ownership must be obvious.

Prefer:

```cpp
std::unique_ptr<T>
```

over:

```cpp
std::shared_ptr<T>
```

Use raw pointers only when ownership is not transferred.

Example:

```cpp
Renderer* renderer;
```

means:

* Renderer is owned elsewhere.
* This class only references it.

Avoid shared ownership unless absolutely necessary.

---

## Const Correctness

Use const aggressively.

```cpp
void Draw(const Camera& camera);
```

Prefer immutable data whenever possible.

---

## Naming

### Types

Use PascalCase.

```cpp
class CommandManager;
struct Vertex;
enum class DrawMode;
```

### Functions

Use PascalCase.

```cpp
void SubmitLine();
void DrawGrid();
```

### Variables

Use camelCase.

```cpp
float zoomLevel;
uint32_t vertexCount;
```

### Member Variables

Prefix with `m_`.

```cpp
float m_Zoom;
Camera m_Camera;
```

### Static Variables

Prefix with `s_`.

```cpp
static RendererAPI* s_Instance;
```

### Constants

Use `constexpr`.

```cpp
constexpr float GridSize = 10.0f;
```

---

# Architecture

## Layered Design

Dependencies should flow downward.

Example:

```text
Application
 ├── Viewport
 ├── Commands
 ├── Renderer
 ├── Entities
 ├── IO
 └── Platform
```

Lower layers must never depend on higher layers.

Bad:

```text
Entities -> Editor
```

Good:

```text
Editor -> Entities
```

---

## Separate Systems Clearly

Each subsystem should have a single responsibility.

Examples:

### Renderer

Responsible for:

* GPU resources
* Draw calls
* Shaders
* Buffers

Not responsible for:

* UI
* Commands
* Business logic

---

### Commands

Responsible for:

* Parsing
* Execution
* Validation

Not responsible for:

* Rendering
* Window management

---

### UI

Responsible for:

* User interaction
* Displaying state

Not responsible for:

* Core application logic

---

## Data Flow

Prefer explicit data flow.

Good:

```cpp
renderer.Draw(scene);
```

Avoid hidden global state.

Bad:

```cpp
GlobalScene::Get().Draw();
```

---

# Error Handling

Prefer assertions for programmer errors.

```cpp
ASSERT(vertexBuffer != nullptr);
```

Prefer recoverable error handling for runtime failures.

```cpp
if (!file.Open())
{
    LOG_ERROR("Failed to open file");
    return false;
}
```

Avoid exceptions unless the project already relies on them heavily.

---

# Logging

Use logging for:

* Initialization
* Resource loading
* Errors
* Warnings

Do not log excessively in hot paths.

Bad:

```cpp
for (...)
{
    LOG_INFO("Drawing vertex");
}
```

---

# Performance Guidelines

## Measure First

Never optimize blindly.

Profile before making performance changes.

---

## Minimize Allocations

Prefer:

```cpp
std::vector<Vertex> vertices;
vertices.reserve(1000);
```

over repeated allocations.

---

## Pass Large Objects By Reference

Prefer:

```cpp
void Draw(const Mesh& mesh);
```

instead of:

```cpp
void Draw(Mesh mesh);
```

---

## Avoid Virtual Calls In Hot Paths

Virtual dispatch is acceptable for high-level systems.

Avoid it inside rendering loops and performance-critical code.

---

# Dependencies

Dependencies should be:

* Well known
* Actively maintained
* Solving a real problem

Before adding a dependency ask:

1. Can this be implemented simply?
2. Is the dependency worth the build complexity?
3. Will this increase compile times significantly?

---

# File Organization

Prefer:

```text
Renderer/
    Renderer.h
    Renderer.cpp

Command/
    CommandManager.h
    CommandManager.cpp

UI/
    ConsolePanel.h
    ConsolePanel.cpp
```

Avoid dumping unrelated code into large utility folders.

---

# Header Guidelines

Headers should contain:

* Public API
* Type declarations

Source files should contain:

* Implementation details

Minimize includes in headers.

Prefer forward declarations where practical.

---

# Build System

Build configuration should be:

* Reproducible
* Deterministic
* Easy to understand

Avoid complex build logic when simple solutions exist.

---

# Comments

Code should explain what it does.

Comments should explain why.

Bad:

```cpp
// Increment i
i++;
```

Good:

```cpp
// Skip sentinel node at index 0.
i++;
```

---

# Code Reviews

When reviewing code:

1. Verify correctness first.
2. Verify architecture second.
3. Verify performance third.
4. Verify style last.

Readability and maintainability are more important than clever solutions.

---

# Guiding Principle

The best code is:

* Easy to read.
* Easy to debug.
* Easy to modify.
* Fast enough.
* Free from unnecessary abstraction.

When in doubt, choose the simpler solution.

