# Store block properties tables in GoSurvey drawing data

**Status: accepted.** A block properties table belongs to its GoSurvey block definition and is saved in GoSurvey's drawing data; DWG export does not write an AutoCAD native block-table object and reports how many definitions contain tables. This preserves GoSurvey save/load round trips while making the interoperability limit explicit: AutoCAD cannot use the table, and re-saving the drawing there can discard it. Revisit this decision when verified native block-table read/write support exists.
