/* Shared native AcDbMLeader context: cluster geometry, content and attachments.
 * Embedded in MULTILEADER and its drawing-owned annotation-scale context objects. */
  FIELD_BL (ctx.num_leaders, 0);
  VALUEOUTOFBOUNDS (ctx.num_leaders, 5000) // MAX_LEADER_NUMBER
  DXF_OR_PRINT { VALUE_TFF ("LEADER{", 302); }
  REPEAT (ctx.num_leaders, ctx.leaders, Dwg_LEADER_Node)
  REPEAT_BLOCK
      #define lnode ctx.leaders[rcount1]
      SUB_FIELD_B (lnode, has_lastleaderlinepoint, 290);
      SUB_FIELD_B (lnode, has_dogleg, 291);
      /* DWG stores the vectors even when their validity flags are clear. */
#ifdef IS_DXF
      if (FIELD_VALUE (lnode.has_lastleaderlinepoint))
#endif
        SUB_FIELD_3BD (lnode, lastleaderlinepoint, 10);
#ifdef IS_DXF
      if (FIELD_VALUE (lnode.has_dogleg))
#endif
        SUB_FIELD_3BD (lnode, dogleg_vector, 11);
      SUB_FIELD_BL (lnode, num_breaks, 0);
      VALUEOUTOFBOUNDS (lnode.num_breaks, 5000)
      REPEAT2 (lnode.num_breaks, lnode.breaks, Dwg_LEADER_Break)
      REPEAT_BLOCK
          SUB_FIELD_3BD (lnode.breaks[rcount2], start, 11);
          SUB_FIELD_3BD (lnode.breaks[rcount2], end, 12);
          SET_PARENT (lnode.breaks[rcount2], (struct _dwg_LEADER_Line *)&_obj->lnode);
      END_REPEAT_BLOCK
      END_REPEAT (lnode.breaks);

      SUB_FIELD_BL (lnode, branch_index, 90);
      SUB_FIELD_BD (lnode, dogleg_length, 40);
      DXF_OR_PRINT { VALUE_TFF ("LEADER_LINE{", 304); }
      SUB_FIELD_BL (lnode, num_lines, 0);
      VALUEOUTOFBOUNDS (lnode.num_lines, 5000)
      REPEAT2 (lnode.num_lines, lnode.lines, Dwg_LEADER_Line)
      REPEAT_BLOCK
          #define lline lnode.lines[rcount2]
          SUB_FIELD_BL (lline, num_points, 0);
          FIELD_3DPOINT_VECTOR (lline.points, lline.num_points, 10);
          SUB_FIELD_BL (lline, num_breaks, 0);
          VALUEOUTOFBOUNDS (lline.num_breaks, 5000)
          REPEAT3 (lline.num_breaks, lline.breaks, Dwg_LEADER_Break)
          REPEAT_BLOCK
              SUB_FIELD_3BD (lline.breaks[rcount3], start, 11);
              SUB_FIELD_3BD (lline.breaks[rcount3], end, 12);
              SET_PARENT (lline.breaks[rcount3], &_obj->lline);
          END_REPEAT_BLOCK
          END_REPEAT (lline.breaks);
          SUB_FIELD_BL (lline, line_index, 91);

          SINCE (R_2010b)
            {
              SUB_FIELD_BS (lline, type, 170);
              SUB_FIELD_CMC (lline, color, 92);
              SUB_FIELD_HANDLE (lline, ltype, 5, 340);
              SUB_FIELD_BLd (lline, linewt, 171);
              SUB_FIELD_BD (lline, arrow_size, 40);
              SUB_FIELD_HANDLE (lline, arrow_handle, 5, 341);
              SUB_FIELD_BL (lline, flags, 93);
            }
            SET_PARENT (lline, &_obj->lnode);
            #undef lline
      END_REPEAT_BLOCK
      END_REPEAT (lnode.lines)
      SINCE (R_2010b)
        SUB_FIELD_BS (lnode, attach_dir, 271);
      DXF_OR_PRINT { VALUE_TFF ("}", 305); }
      SET_PARENT_OBJ (lnode);
  END_REPEAT_BLOCK
  END_REPEAT (ctx.leaders)

#ifndef IS_DXF
  MLEADER_CONTEXT_DATA_fields; // lgtm[cpp/use-after-free] codeql[cpp/use-after-free]
#endif
  DXF_OR_PRINT { VALUE_TFF ("}", 303); }
  SINCE (R_2010b)
    {
      FIELD_BS (ctx.text_top, 273);
      FIELD_BS (ctx.text_bottom, 272);
    }
  DXF_OR_PRINT { VALUE_TFF ("}", 301); }
  /* end CONTEXT_DATA */

