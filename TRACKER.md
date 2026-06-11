# PROJECT ISSUE & FEATURE TRACKER

## BUGS

## FEATURES

### [FEAT-002] Traverse Editor
    - [ ]   Survey Traverse editor window that allows user
            to create legs of a traverse in different ways
                - such as with a backsight point, backsight reference angle,
                face 1/face2 measurments, forsight point/points,
                horizontal distance, horizontal angle (DMS or decimal), vertical angle (DMS or decimal,
                zenith or from backsight), slope distance. All of this should optional input data based\
                on what the user wants to provide and it will be up to the traverse engine to solve the
                traverse and let the user know what data to provide if data is insufficient.
    - [~] Survey Least Squares Adjustment window that lets the user review traverse leg residuals to detect
            blunders and edit accordingly.
            - Done (REQ-014..017): "Calculate Closure" opens a window showing the unadjusted closure
              beside a weighted least-squares adjustment (closed-loop), with a per-observation
              residuals tab and configurable a-priori standard errors. Raw F1/F2 measurements and
              per-leg statistics now display in the editor (REQ-010..012). See spec ADR-001/002.
            - Done (REQ-018): each leg expands inline to an editable observation-set editor —
              add/remove sets and edit the literal F1/F2 circle readings, slope distances, and
              zenith angles; the leg re-reduces from its sets via ReduceLegFromSets (ADR-003,
              backsight reading stored on the leg). "+ Add Leg" moved into the table as its last row.
            - Remaining: connecting (point-to-point) traverses.
    - [ ] Ability to import raw data formats such as Autodesk .fbk, Bently RWD,
            Carlson RW5, Microsurvey RW5, TDS RAW,
            TDS RW5 and traverse editor gets filled in automatically

### [FEAT-003] Level Loop Editor
    - [ ] same concept as traverse editor, allowing the user to process level loop data, whether single wire
            or three wire with a least sqaures adjustment editor

# COMPLETED
~~### [BUG-002] Fuzzy find menu has some functionality problems
    - using the up arrow closes the menu
    - using the down arrow and selecting the highlighted command does not run that command
    - Fixed: command input claims Up/Down via SetItemKeyOwner so keyboard-nav no longer steals
      focus while the list is open; highlighted command is persisted across the Enter frame
      (single-line InputText self-deactivates on Enter) so submit runs the highlighted entry.
~~

~~### [BUG-003] Focusing different panels shows a ugly dark blue
    - I don't want to visually see different panels gaining focus. no color change
    - Fixed: light theme TitleBgActive set equal to TitleBg, so a focused docked node's tab-bar
      strip no longer flips to the dark caption blue (ImGui fills it with TitleBgActive on focus).
~~

~~### [BUG-001] Object hovering not working properly when in state plane coordinate system.
    - Object hovering triggers on objects even if my cursor is not "visually" touching the object
    - Fixed: pick distance math now runs in double precision (float cancellation at state-plane
      magnitudes was quantizing distances to ~1 ft); hover/click tolerance uses the robust
      outlier-trimmed extent instead of the raw bbox; idle hover highlight uses a tight fixed
      3px aperture so the cursor must visually touch the stroke at any zoom.
~~

~~### [FEAT-001] Undo Redo System
    - [ ] Full undo redo with configureable history size settings window
    - [ ] UI Buttons at the top ribbon
    - [ ] CRTL+Z and CRTL+SHIFT+Z for undo redo
    - [ ] history log should be in %APPDATA%\GoSurvey\
    - [ ] update INNO script if necessary
~~
