# PROJECT ISSUE & FEATURE TRACKER

## BUGS
- [x] **[BUG-001]** Rotation command will is not able to specify rotation from mouse click [COMPLETE]
    - After specifying a base point the command needs a rotation amount CW from north, R for reference, C for copy.
      I should be able to not only type in the degree but specify with the mouse.
    - This behavior also happens when typing R for reference, giving two reference points. I should be able to not only type in a
      rotation but specify with the mouse.
    - Not only specifying a point with the mouse but snaps should also work.

## FEATURES
- [ ] **[FEAT-001]** I want the UI to look much more professional.
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
        Success Green	#10B981

- [ ] **[FEAT-002]** Add more support for for hovering over entities
    - If I hover over a line, circle, text, survey point, etc...the entity should highlight very slightly
      to let me know i am over an entity.
    - And if I am hovering over an entity, click should select that entity. Other wise a box select is needed.
