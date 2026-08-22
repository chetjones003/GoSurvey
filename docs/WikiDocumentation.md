# Build a Complete End-User GitHub Wiki

You are responsible for creating a **comprehensive, professional, end-user documentation Wiki** for this CAD application.

This Wiki is **NOT developer documentation**.

The target audience is a person who has installed the application and wants to learn how to use it as a CAD/surveying application. Assume the reader does not know the source code, programming architecture, internal classes, APIs, or implementation details.

The Wiki should function as the application's **complete user manual and command reference**.

---

## 1. First: Thoroughly Analyze the Application

Before writing any documentation, inspect the repository extensively.

You must understand the application from the perspective of an end user.

Analyze:

* Every source file
* Every UI component
* Every toolbar
* Every menu
* Every command
* Every command-line command
* Every keyboard shortcut
* Every dialog
* Every settings/configuration screen
* Every panel
* Every context menu
* Every drawing tool
* Every editing tool
* Every selection mechanism
* Every snapping mechanism
* Every coordinate/measurement tool
* Every survey/COGO function
* Every import/export function
* Every file format
* Every layer/object/property system
* Every viewport/view/navigation function
* Every plotting/printing function
* Every undo/redo capability
* Every status indicator
* Every notification/error message that has user-facing meaning
* Every feature exposed through the UI
* Every feature exposed through commands
* Every configuration option
* Every shortcut
* Every workflow that can be inferred from the application's existing functionality

Also inspect:

* README files
* Existing documentation
* Example files
* Configuration files
* Tests when they reveal user-visible behavior
* Command registration
* Menu definitions
* Toolbar definitions
* UI labels
* Help text
* Error messages
* Tooltips
* Dialog labels
* Keyboard shortcut definitions
* User-facing strings

Do NOT assume that something exists merely because a typical CAD application would have it.

Document only functionality that actually exists in the application.

If functionality appears partially implemented, experimental, disabled, or incomplete, determine that from the code and document its current behavior accurately.

---

# 2. Documentation Philosophy

Write the Wiki as if you are documenting a mature professional CAD application.

The documentation should answer:

> "How do I use this application to accomplish my job?"

rather than:

> "How is this application implemented?"

Do NOT explain:

* Source code
* Classes
* Functions
* Interfaces
* Internal architecture
* Algorithms
* Database implementation
* Rendering implementation
* Internal event systems
* Internal application state
* Programming dependencies

unless an internal detail is absolutely necessary to explain observable user behavior.

Use user-facing terminology.

For example:

BAD:

> The SelectionManager maintains a collection of entity handles and dispatches selection events.

GOOD:

> **Selecting Objects**
>
> Click an object to select it. Hold `Ctrl` to add additional objects to the selection. Selected objects are highlighted so you can see which entities will be affected by the next editing command.

Gather screenshots of the application along the way to link and associate with the feature/topic/subject being covered

---

# 3. Treat the Wiki as a Complete User Manual

Create documentation that allows a new user to learn the application from beginning to advanced usage.

Organize the Wiki into logical sections.

At minimum, create the following pages/categories where applicable.

## Getting Started

Create pages covering:

* Welcome
* What the application does
* Starting the application
* Creating a new drawing/project
* Opening an existing drawing/project
* Saving
* Save As
* Closing a drawing
* Basic workspace orientation
* Understanding the interface
* First drawing
* First survey workflow
* First complete project workflow

---

# 4. User Interface Documentation

Document the entire UI.

Create a comprehensive page describing the application's interface.

Document every visible area, including where applicable:

* Title bar
* Menu bar
* Toolbars
* Ribbon
* Drawing area
* Command line
* Command prompt
* Properties panel
* Layers panel
* Tool palettes
* Status bar
* Coordinate display
* Snap controls
* Grid controls
* Navigation controls
* View controls
* Selection controls
* Dialogs
* Context menus
* Side panels
* Tabs
* Project browser
* Drawing browser
* Any other UI element

For every control, explain:

1. What it does
2. When to use it
3. How to activate it
4. What happens afterward
5. What options are available
6. Any keyboard shortcut
7. Any command-line equivalent
8. Any important limitations

---

# 5. CAD Commands

Create a **complete command reference**.

This is one of the most important sections of the Wiki.

Find every user-facing CAD command implemented in the application.

For every command, create documentation containing:

### Command Name

Include:

* Command name
* Aliases
* Menu location
* Toolbar location
* Keyboard shortcut
* Purpose

### Syntax

Show the command syntax where applicable.

Example:

```text
LINE
```

### What It Does

Explain the command in plain language.

### How to Use It

Give step-by-step instructions.

### Command Workflow

Explain every prompt the user encounters.

For example:

```text
Command: LINE

Specify first point:
Specify next point:
Specify next point:
```

Explain what the user can enter at each prompt.

### Input Methods

Document all supported ways of providing input, such as:

* Mouse selection
* Coordinates
* Relative coordinates
* Polar coordinates
* Distances
* Angles
* Object snaps
* Entity selection
* Keyboard input
* Command options

### Options

Document every command option.

Explain what each option does and when to use it.

### Examples

Provide realistic examples.

### Common Problems

Explain common mistakes and how to resolve them.

---

# 6. Command Reference Index

Create a master command index.

Organize commands into logical categories such as:

* Drawing
* Modify
* Annotation
* Layers
* Properties
* Measurement
* Geometry
* Surveying
* COGO
* Points
* Lines
* Curves
* Labels
* Views
* Selection
* Snapping
* Import
* Export
* File
* Settings
* Utilities

Only use categories that actually apply to the application.

Each command in the index should link to its detailed documentation.

---

# 7. Drawing Tools

Document every drawing tool in detail.

For each tool explain:

* What it creates
* How to start it
* Required inputs
* Available options
* Coordinate entry
* Object snapping
* Dynamic input
* Precision behavior
* How to finish the command
* How to cancel it
* How to continue drawing
* Examples

Include workflows for:

* Lines
* Polylines
* Arcs
* Circles
* Points
* Curves
* Text
* Dimensions
* Hatches
* Blocks
* Other geometry

Only document tools that actually exist.

---

# 8. Modify / Editing Tools

Document every editing operation.

For each command explain:

* Selecting objects
* Moving
* Copying
* Rotating
* Scaling
* Mirroring
* Trimming
* Extending
* Offsetting
* Joining
* Breaking
* Exploding
* Erasing
* Stretching
* Editing properties
* Editing geometry
* Any application-specific modification tools

Again, document only actual functionality.

---

# 9. Object Selection

Create a dedicated selection guide.

Document:

* Single selection
* Window selection
* Crossing selection
* Multiple selection
* Add/remove selection
* Selection filtering
* Selection cycling
* Preselection
* Selection grips
* Selection handles
* Selection highlighting
* Escape/cancel behavior
* Selection behavior during commands

Explain precisely how selection works in the application.

---

# 10. Object Snapping

Create a complete Object Snap guide.

Identify every supported snap type from the actual implementation.

For each snap type explain:

* Name
* Symbol/icon
* What it snaps to
* When it should be used
* How to activate it
* Whether it can be toggled
* Whether it works globally or per-command
* Any limitations

Include practical examples.

For a CAD/surveying application, pay particular attention to:

* Endpoint
* Midpoint
* Center
* Intersection
* Perpendicular
* Nearest
* Node/point
* Quadrant
* Tangent
* Other supported snap modes

Do not document unsupported snap modes.

---

# 11. Coordinate Input

Create a comprehensive coordinate-entry guide.

Document every supported coordinate-entry method.

Where supported, explain:

* Absolute coordinates
* Relative coordinates
* Polar coordinates
* Bearing/distance
* Distance/angle
* X/Y input
* Northing/Easting
* Elevation/Z
* 3D coordinates
* Dynamic input
* Coordinate tracking
* Coordinate display
* Units
* Precision

Use examples relevant to surveying and CAD.

---

# 12. Surveying / COGO

Because this is a surveying-oriented CAD application, give this section especially detailed treatment.

Document every surveying and COGO function actually implemented.

Potential categories include:

* Points
* Point numbering
* Northing
* Easting
* Elevation
* Descriptions
* Bearings
* Azimuths
* Distances
* Angles
* Inverse
* Traverse
* Traverse adjustment
* Coordinate geometry
* Point creation
* Point editing
* Point import
* Point export
* Bearing/distance calculations
* Area calculations
* Curve calculations
* Intersection calculations
* Offset calculations
* Stationing
* Elevations
* Survey annotations

Do not assume these exist. Discover them from the application.

For each surveying command provide practical examples using realistic survey data.

---

# 13. Layers

If layers exist, document them completely.

Explain:

* Creating layers
* Deleting layers
* Renaming layers
* Current layer
* Layer colors
* Layer visibility
* Layer locking
* Layer freezing
* Layer properties
* Assigning objects to layers
* Managing layers
* Layer-related commands

Explain why a user would use each feature.

---

# 14. Properties

Document the properties system.

Explain:

* How to view properties
* How to edit properties
* Which properties are available for each object type
* Geometry properties
* Position
* Rotation
* Layer
* Color
* Linetype
* Survey properties
* Any other user-editable properties

Create separate sections for different object types where necessary.

---

# 15. View and Navigation

Document every method of navigating the drawing.

Include:

* Zoom
* Zoom extents
* Zoom window
* Zoom previous
* Pan
* Mouse wheel
* Middle mouse navigation
* View controls
* Named views
* Viewports
* Model space
* Paper space/layouts
* 2D/3D views
* Orthographic views
* Isometric views
* Any camera controls

Explain practical workflows.

---

# 16. Grid, Ortho, Tracking, and Drafting Aids

Document every drafting aid.

Include where applicable:

* Grid
* Snap grid
* Ortho
* Polar tracking
* Object snap tracking
* Dynamic input
* Coordinate tracking
* Construction geometry
* Temporary tracking
* Constraints

Explain exactly how these features affect drawing.

---

# 17. Annotation

Document all annotation functionality.

Include:

* Text
* Multiline text
* Labels
* Dimensions
* Leaders
* Survey labels
* Point labels
* Bearing labels
* Distance labels
* Area labels
* Elevation labels
* Styles
* Text properties
* Annotation scaling

Only document implemented features.

---

# 18. Blocks / Symbols

If supported, document:

* Creating blocks
* Inserting blocks
* Editing blocks
* Block attributes
* Block properties
* Dynamic blocks
* Exploding blocks
* Updating blocks
* Block libraries
* Symbol workflows

Explain these from the user's perspective.

---

# 19. Import and Export

Document every supported file format.

For each format explain:

* What the format is used for
* How to import it
* How to export it
* Required options
* Coordinate systems
* Units
* Field mappings
* Common problems
* Example workflows

Pay particular attention to survey data formats and CAD exchange formats.

---

# 20. Files and Projects

Document:

* Creating files
* Opening files
* Saving
* Save As
* Recent files
* Backups
* Autosave
* Recovery
* File locations
* Project management
* Templates
* Any file-related settings

Only document actual behavior.

---

# 21. Settings and Preferences

Document every user-configurable setting.

For each setting explain:

* What it controls
* Default value
* Recommended value
* When to change it
* What effect it has

Organize settings into logical categories.

---

# 22. Keyboard Shortcuts

Create a complete keyboard shortcut reference.

Include:

* Shortcut
* Command/action
* Context
* Description

Separate:

* Global shortcuts
* CAD shortcuts
* Command-line shortcuts
* Navigation shortcuts
* Editing shortcuts

---

# 23. Command-Line Reference

If the application has a command console/command line, document it separately.

Explain:

* How to activate it
* How commands work
* Command history
* Prompt behavior
* Command cancellation
* Keyboard input
* Command options
* Coordinate entry
* Aliases

Provide examples.

---

# 24. Workflows

Create task-oriented tutorials.

Do not only document individual commands.

Create complete workflows such as:

* Creating a new drawing
* Setting up a project
* Creating survey points
* Importing survey data
* Drawing a boundary
* Inversing between points
* Creating a traverse
* Editing survey geometry
* Annotating a survey
* Creating a subdivision drawing
* Preparing a plan
* Exporting a drawing
* Printing/plotting
* Completing a typical survey project

Only create workflows supported by the application's actual functionality.

For every workflow:

1. State the goal.
2. List prerequisites.
3. Give numbered steps.
4. Explain what the user should see.
5. Explain important options.
6. Explain expected results.
7. Include troubleshooting.

---

# 25. Troubleshooting

Create a detailed troubleshooting section.

Document problems that can realistically occur based on the actual application.

Organize by categories:

* Installation
* Files
* Drawing
* Selection
* Snapping
* Coordinates
* Survey calculations
* Import
* Export
* Display
* Performance
* Commands
* Printing
* Other

Whenever possible use:

**Problem → Cause → Solution**

Do not invent errors.

---

# 26. FAQ

Create a practical FAQ based on the application's actual behavior.

Examples of the types of questions to answer:

* How do I start a drawing?
* How do I draw a line?
* How do I enter coordinates?
* How do I snap to an endpoint?
* How do I change layers?
* How do I undo a command?
* How do I import survey points?
* How do I calculate an inverse?
* How do I change units?
* How do I export my drawing?

Generate questions based on the actual features discovered in the code.

---

# 27. Documentation Quality Rules

Follow these rules throughout the Wiki.

### Never invent functionality

If you cannot find evidence that a feature exists, do not document it as available.

### Prefer actual application terminology

Use the exact names shown in:

* Menus
* Buttons
* Commands
* Dialogs
* Tooltips
* Status bar
* UI labels

### Explain actions, not implementation

The user should always know:

> What do I click/type?

> What happens?

> What should I expect?

> What do I do next?

### Be precise

Avoid vague statements such as:

> "Use the drawing tools to create geometry."

Instead:

> "Select **Line** from the Draw toolbar. At the `Specify first point:` prompt, click in the drawing area or enter a coordinate. At the `Specify next point:` prompt, select the endpoint of the second segment."

Only use examples that accurately reflect the application's behavior.

---

# 28. Cross-Link Everything

The Wiki should behave like a real documentation system.

Cross-link related topics.

For example:

```text
LINE
  ↓
Coordinate Input
  ↓
Object Snaps
  ↓
Drafting Aids
```

A command page should link to relevant concepts.

A workflow should link to the commands used in that workflow.

A troubleshooting article should link back to the relevant feature documentation.

---

# 29. Screenshots and Visual Documentation

Where practical, identify places where screenshots would significantly improve the documentation.

If the application can be run in the environment, use it to understand the UI.

For important UI pages and workflows, document where screenshots should be included.

Do not fabricate screenshots.

If you have the ability to generate screenshots from the running application, use them where appropriate.

---

# 30. Wiki Structure

Create a logical Wiki navigation structure.

At minimum, create a Home page that serves as the documentation landing page.

The Home page should contain links to:

* Getting Started
* User Interface
* Commands
* Drawing
* Editing
* Selection & Snapping
* Coordinates
* Surveying / COGO
* Layers
* Annotation
* Blocks
* Import / Export
* Views & Navigation
* Settings
* Keyboard Shortcuts
* Workflows
* Troubleshooting
* FAQ

Adapt this structure to the actual application.

Do not create empty pages simply because they are listed above.

---

# 31. Command Coverage Audit

After creating the documentation, perform a second pass through the source code.

Look specifically for:

* Command registration
* Command aliases
* Menu commands
* Toolbar actions
* Keyboard shortcuts
* UI buttons
* Dialog actions
* Context menu actions
* User-facing features

Build an internal checklist of every discovered user-facing command and feature.

Verify that every one has documentation.

If you find undocumented functionality, document it.

---

# 32. Feature Coverage Audit

Perform another audit specifically for functionality.

Verify that documentation exists for:

* Every drawing tool
* Every editing tool
* Every selection method
* Every snap mode
* Every navigation method
* Every import format
* Every export format
* Every annotation type
* Every layer feature
* Every property
* Every setting
* Every shortcut
* Every survey/COGO feature
* Every user-facing dialog
* Every major workflow

Do not stop after generating the initial pages.

---

# 33. Accuracy Audit

After the Wiki is complete, review it as though you were a new user.

For every procedure ask:

1. Can a user actually perform this action?
2. Is the command name correct?
3. Is the UI location correct?
4. Are the prompts correct?
5. Are the options correct?
6. Are the examples valid?
7. Does the described result actually occur?
8. Did documentation accidentally describe an unimplemented feature?
9. Is there anything the user would need to know that is missing?

Correct any problems you find.

---

# 34. Keep Developer Documentation Out

This Wiki is specifically for **end users**.

Do NOT put pages such as:

* Architecture Internals
* Code Structure
* Developer Setup
* Build Instructions
* API Internals
* Database Schema
* Rendering Architecture
* Event Bus
* Internal Services
* Class Reference
* Development Dependencies

Those belong in developer documentation elsewhere.

If a concept has both a user-facing and developer-facing explanation, include only the user-facing explanation here.

---

# 35. Writing Style

Write like professional CAD software documentation.

Use:

* Clear headings
* Short paragraphs
* Numbered procedures
* Tables where useful
* Command examples
* Keyboard shortcuts
* Notes
* Warnings
* Tips
* Cross-links
* Practical examples

Avoid:

* Marketing language
* Excessive verbosity that doesn't help the user
* Programming terminology
* Speculation
* Unsupported claims

The documentation should be detailed enough that a user can learn the application without needing to ask the developer how a feature works.

---

# 36. Important: Do Not Stop at a Skeleton

Do NOT simply create:

```text
Home
Commands
Drawing
Editing
Settings
FAQ
```

with a few paragraphs in each.

The objective is a **complete reference manual**.

Every actual user-facing feature should be represented somewhere in the documentation.

If the application has 100 commands/features, the Wiki should reflect all 100.

---

# 37. GitHub Wiki Implementation

Create the documentation in the appropriate GitHub Wiki repository for this project.

Determine the correct Wiki repository and branch/workflow from the GitHub repository.

If the Wiki is not enabled, determine whether it can be enabled using the available GitHub tooling.

Use Git to create and manage the Wiki pages where appropriate.

Do not modify the application's source code merely to create the documentation.

Keep the Wiki documentation separate from the application's production source unless documentation files already belong in the repository.

---

# 38. Final Deliverable

When finished, report:

1. The Wiki structure you created.
2. The number of pages created.
3. The major feature categories documented.
4. Any functionality you discovered that was undocumented before.
5. Any functionality that appeared incomplete or ambiguous.
6. Any features you could not document because their behavior could not be determined.
7. Any screenshots that would still be useful to add.
8. Any recommendations for improving the application's user experience or documentation.

Most importantly:

**Do not claim the Wiki is complete until you have performed the command coverage, feature coverage, and accuracy audits described above.**

The goal is to produce a **professional, comprehensive, end-user CAD manual**, not a high-level project README.

