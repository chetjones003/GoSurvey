# MTEXT


![MTEXT command overview](wiki-img:commands/mtext.png)
**Command:** `MTEXT` — aliases: `MT`
**Category overview:** [[Annotation]]

**Ribbon:** Annotate → Mtext **Command:** `MTEXT`, `MT`

Multiline text inside a frame, edited **on the drawing** in a WYSIWYG editor.

```
MTEXT: First corner | ESC cancel
MTEXT: Opposite corner | ESC cancel
MTEXT: Edit in drawing box — Ctrl+Enter reformats | Save to place | Esc cancel
```

1. **Two corners** define the frame.
2. The rich-text editor opens **in place** on the drawing.
3. Type. **Enter** breaks a line; **Ctrl+Enter** reformats; **Save** places the text; **Esc**
   cancels.

**Double-click an existing MTEXT** to reopen the editor on it.

### The Text Formatting toolbar

Above the editing box:

| Control | What it does |
|---|---|
| **Font** | Font for the selection, or for what you type next |
| **Height** | Text height |
| **B / I / U** | Bold, italic, underline |
| **Colour** | Text colour |
| **Justification** | Top/Middle/Bottom × Left/Center/Right — nine positions |

> Select characters first to apply a font, colour, or B/I/U to **just them**. With no selection, the
> setting applies to what you type next.

Two optional rows are toggled from **Options ▸ Editor Settings**: **Show Options Row** and
**Show Ruler**.

### The Options menu

| Item | Shortcut | Notes |
|---|---|---|
| **Import Text…** | — | Inserts the contents of a text file at the caret |
| **Find and Replace…** | `Ctrl+R` | Within the MTEXT being edited |
| **Change Case ▸ UPPERCASE / lowercase** | — | Applies to the selection |
| **All CAPS** | — | Type new ASCII in all capitals |
| **Autocorrect cAPS Lock** | — | Fixes a word typed with Caps Lock inverted — `hELLO` becomes `Hello` |
| **Character Set ▸** | — | Choose the character set |
| **Remove Formatting ▸ From selected text / From the whole MTEXT** | — | Strips styling |
| **Editor Settings ▸ Show Options Row / Show Ruler** | — | Editor chrome |
| **Help** | `F1` | — |

### Items present but not functional

These appear on the Options menu and do nothing yet — the editor says so when you use them:

| Item | Message |
|---|---|
| **Insert Field…** (`Ctrl+F`) | *Fields are not supported yet* |
| **Background Mask…** | *Background masking is not supported yet* |
| **Paragraph…**, **Paragraph Alignment**, **Bullets and Lists**, **Columns**, **Combine Paragraphs** | *Paragraph and column properties are not stored yet* |

### Editing keys

| Key | Effect |
|---|---|
| `Enter` | New line |
| `Ctrl+Enter` | Reformat |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo **inside the text box** |
| `Ctrl+C` / `Ctrl+X` | Copy / cut **inside the text box** |
| `Esc` | Cancel the edit |

### Grips

A selected MTEXT shows grips you can drag to move it or resize its frame. `Esc` during a drag
cancels it.

---

---

## Related

- [[Annotation]]
- [[Command Reference]]