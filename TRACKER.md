# PROJECT ISSUE & FEATURE TRACKER

## BUGS

## FEATURES

# COMPLETED

~~- [x] **[BUG-001]** Rotation command will is not able to specify rotation from mouse click [COMPLETE]
    - After specifying a base point the command needs a rotation amount CW from north, R for reference,
      C for copy. I should be able to not only type in the degree but specify with the mouse.
    - This behavior also happens when typing R for reference, giving two reference points. I should 
      be able to not only type in a rotation but specify with the mouse.
    - Not only specifying a point with the mouse but snaps should also work.~~

~~- [x] **[BUG-003]** Fix Some UI Elements [COMPLETE]
    - Menu dropdowns need some padding on the left and right sides of the text
    - Properties tab is still not focused correctly over the Reports tab
    - Settings window needs scroll bars where scrolling is possible
    - Save Layout window also needs padding
        - save layout window should also have a dropdown of existing layouts and a button 
          to create a new layout and then type the name~~

~~- [X] **[FEAT-001]** I want the UI to look much more professional. [COMPLETED]
    - Core UI Colors
        Purpose                 Color           Hex
        Workspace Background	Rich Black	    #0D0F12
        Secondary Background	Dark Charcoal	#171A1F
        Panel Background	    Graphite	    #20252C
        Raised Surface	        Slate	        #2A313B
        Border	                Steel Gray	    #3A434F
        Separator Lines	        Mid Gray	    #4B5664
    - Command Line / Console
        Purpose	            Hex
        Console Background	#111418	
        Prompt Color	    #22C55E	
        Command Text	    #E5E7EB	
        Error Text	        #EF4444	
        Warning Text	    #F59E0B	
        Success Text	    #10B981	
    - These are the colors used sparingly throughout the UI:
        Accent	        Hex
        Primary Blue	#3B82F6
        Secondary Cyan	#06B6D4
        Survey Orange	#F97316
        Warning Amber	#F59E0B
        Success Green	#10B981~~

~~- [X] **[BUG-002]** Survey Point Labels do not work correctly [COMPLETED]
    - When selecting a survey point, if I press delete the label is deleted first then I
      have to press delete again to delete the point.
    - The point and the label should be "linked" together in a sense. I can move the label independently 
      of the point yes, but if i move the point then the label should move with it the same. And this 
      goes for deleting. If i delete the point the label goes. I cannot select the label without 
      selecting the point. The are "linked".
    - Moving the point label does not lock position because if i regen or zoom the label goes back
      to default position.
    - Also labels just don't "feel" good to work with. Maybe try to clean this up.
    - I should also be able to change a points color, label's color, even the different styles of 
      labels, for example, An id can be one color, description
      another, and elevation another.
    - I also need settings for more label styles, like northing, easting.
    - If a point label gets a certain "distance" away from the point the i need a leader
      pointing back to it's associated point. A leader is a line with an arrow
~~

~~- [x] **[FEAT-002]** Add more support for hovering over entities [COMPLETE]
    - If I hover over a line, circle, text, survey point, etc...the entity should highlight very slightly
      to let me know i am over an entity.
    - And if I am hovering over an entity, click should select that entity. Other wise a box select is needed.~~

~~- [x] **[FEAT-003]** Settings menu right click options [COMPLETE]
    - I want in the User Preferences tab to have a Right Click Options section that has the following:
        - Default Mode:
            - if no objects are selected, right click means:
                - dropdown list
                    - Repeat last command
                    - Shortcut menu
        - Edit Mode
            - If one or more objects are selected, right click means:
                - dropdown list
                    - Repeat last command
                    - Shortcut menu
        - Command Mode
            - If a command is in progress, right click means:
                - dropdown list
                    - ENTER
                    - Shortcut menu: always enabled
                    - Shortcut menu: enabled when options are present~~

~~- [x] **[FEAT-004]** Selection cycling [COMPLETE]
    - I want a way if i have multiple items selected i can cycle through what is selected one
      at a time by bringing up a window with the entities selected to toggle on or off the selection.~~

~~- [x] **[FEAT-005]** Quick Select [COMPLETE]
    - I want a QUICKSELECT (Alias QS) command that brings up a window that will allow me to choose
      to apply to entire drawing or select an area in the drawing. specify object type, specify properties
      to select, operator (=, <, >, select all), value.~~

~~- [x] **[FEAT-006]** Grip Sizes and Multiple Grips [COMPLETE]
    - grips should be blue
    - I want a way in the settings menu to change the size of grips
    - If i have multiple objects selected the grips should should show for all objects
    - also cursor should snap to grips~~
