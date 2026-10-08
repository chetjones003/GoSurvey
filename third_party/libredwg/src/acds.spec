/* -*- c -*- */
/*****************************************************************************/
/*  LibreDWG - free implementation of the DWG file format                    */
/*                                                                           */
/*  Copyright (C) 2020-2021 Free Software Foundation, Inc.                   */
/*                                                                           */
/*  This library is free software, licensed under the terms of the GNU       */
/*  General Public License as published by the Free Software Foundation,     */
/*  either version 3 of the License, or (at your option) any later version.  */
/*  You should have received a copy of the GNU General Public License        */
/*  along with this program.  If not, see <http://www.gnu.org/licenses/>.    */
/*****************************************************************************/

/*
 * acds.spec: AcDb:AcDsProtoype_1b datastorage section specification,
 *            containing new SAB ACIS content.
 * written by Reini Urban
 */

  #include "spec.h"

  static char _AcDs_Schema_Prop_types[] = {0, 0, 2, 1, 2, 4, 8, 1, 2, 4, 8, 4, 8, 0, 0, 0};

  DECODER {
    LOG_TRACE ("unknown_bits [%" PRIuSIZE " (%u,%d,%d) %" PRIuSIZE " TF]: ", dat->size * 8,
               0, 0, 0, dat->size);
    LOG_TRACE_TF (dat->chain, (int)dat->size);
    LOG_TRACE ("\n");
  }
  // header
  FIELD_RLx (file_signature, 0);
  FIELD_RL (file_header_size, 0);
  FIELD_RL (unknown_1, 0);  // always 2
  FIELD_RL (version, 0);    // always 2
  FIELD_RL (unknown_2, 0);  // always 0
  FIELD_RL (ds_version, 0); // datastorage revision
  FIELD_RL (segidx_offset, 0);
  FIELD_RL (segidx_unknown, 0);
  FIELD_RL (num_segidx, 0);
  FIELD_RL (schidx_segidx, 0);
  FIELD_RL (datidx_segidx, 0);
  FIELD_RL (search_segidx, 0);
  FIELD_RL (prvsav_segidx, 0);
  FIELD_RL (file_size, 0);
  dat->byte = _obj->segidx_offset;
#ifdef IS_DECODER
  if (dat->byte > dat->size)
    {
      LOG_ERROR ("Invalid segidx_offset");
      return DWG_ERR_VALUEOUTOFBOUNDS;
    }
  if ((size_t)(_obj->num_segidx * sizeof (Dwg_AcDs_SegmentIndex)) >
      (size_t)(dat->size - dat->byte))
    {
      LOG_ERROR ("Invalid num_segidx");
      return DWG_ERR_VALUEOUTOFBOUNDS;
    }
  _obj->segments = (Dwg_AcDs_Segment *)calloc (_obj->num_segidx,
                                               sizeof (Dwg_AcDs_Segment));
#endif
#ifndef IS_JSON
  if (FIELD_VALUE(num_segidx))
    {
      SUB_FIELD_RSx (segments[0],signature, 0); /* always 0xD5AC (ACD5 in the TF) */
      FIELD_TFF (segments[0].name, 6, 0);       /* always segidx */
      DECODER { _obj->segments[0].type = 0; }
      JSON { SUB_FIELD (segments[0],type, RC, 0); }
      SUB_FIELD (segments[0],segment_idx, RL, 0);
      SUB_FIELD (segments[0],is_blob01, RL, 0);
      SUB_FIELD (segments[0],segsize, RL, 0);
      SUB_FIELD (segments[0],unknown_2, RL, 0);
      SUB_FIELD (segments[0],ds_version, RL, 0); // datastorage revision
      SUB_FIELD (segments[0],unknown_3, RL, 0);
      SUB_FIELD (segments[0],data_algn_offset, RL, 0);
      SUB_FIELD (segments[0],objdata_algn_offset, RL, 0);
      FIELD_TFF (segments[0].padding, 8, 0); // always 8x 0x55
    }
#endif

  REPEAT (num_segidx, segidx, Dwg_AcDs_SegmentIndex)
  REPEAT_BLOCK
#ifdef IS_JSON
      KEY (index); VALUE_RL (rcount1, 0);
#endif
      SUB_FIELD_RLL (segidx[rcount1],offset, 0);
      SUB_FIELD_RL (segidx[rcount1],size, 0);
      DECODER { if (_obj->segidx[rcount1].offset) _obj->total_segments++; }
  END_REPEAT_BLOCK
#ifdef IS_JSON
  END_REPEAT (segidx) // still needed, free later
#endif

  _REPEAT_NF (_obj->num_segidx, segments, Dwg_AcDs_Segment, 1)
  REPEAT_BLOCK
      if (_obj->segidx[rcount1].offset)
        {
#ifdef IS_JSON
          KEY (index); VALUE_RL (rcount1, 0);
#endif
#ifdef IS_DECODER
          if (_obj->segidx[rcount1].offset >= dat->size)
            {
              LOG_ERROR ("Invalid AcDs.segments[%d] offset: " FORMAT_RLL "\n", rcount1,
                         _obj->segidx[rcount1].offset);
              _obj->segidx[rcount1].offset = 0;
              continue;
            }
          else
            {
              LOG_TRACE ("\nsegments[%d] offset: " FORMAT_RLL "\n", rcount1,
                         _obj->segidx[rcount1].offset);
              dat->byte = _obj->segidx[rcount1].offset;
            }
#endif
        }
      else
        {
#ifdef IS_JSON
          ENDHASH;
#endif
          continue;
        }

      SUB_FIELD_RSx (segments[rcount1],signature, 0); /* always 0xD5AC (ACD5 in the TF) */
      DECODER {
        if (_obj->segments[rcount1].signature != 0xD5AC)
          {
            LOG_ERROR ("Invalid AcDs.segments[%d].signature %x != 0xd5ac", rcount1,
                       _obj->segments[rcount1].signature);
            error |= DWG_ERR_SECTIONNOTFOUND;
            //continue;
          }
      }
      /* segidx, datidx, _data_, schidx, schdat, search, blob01, prvsav, freesp */
      FIELD_TFF (segments[rcount1].name, 6, 0);
      DECODER {
        _obj->segments[rcount1].name[6] = '\0';
        _obj->segments[rcount1].type = -1;
        if (strEQc ((char*)_obj->segments[rcount1].name, "segidx"))
          _obj->segments[rcount1].type = 0;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "datidx"))
          _obj->segments[rcount1].type = 1;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "_data_"))
          _obj->segments[rcount1].type = 2;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "schidx"))
          _obj->segments[rcount1].type = 3;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "schdat"))
          _obj->segments[rcount1].type = 4;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "search"))
          _obj->segments[rcount1].type = 5;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "blob01"))
          _obj->segments[rcount1].type = 6;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "prvsav")) // ODA undocumented
          _obj->segments[rcount1].type = 7;
        else if (strEQc ((char*)_obj->segments[rcount1].name, "freesp")) // ODA undocumented
          _obj->segments[rcount1].type = 8;
        else
          {
            LOG_ERROR ("Invalid AcDs.segments[%d].name %s", rcount1,
                       (char *)_obj->segments[rcount1].name);
            error |= DWG_ERR_SECTIONNOTFOUND;
            continue;
          }
      }
      JSON {
        SUB_FIELD (segments[rcount1],type, RC, 0);
      }
      SUB_FIELD (segments[rcount1],segment_idx, RL, 0);
      SUB_FIELD (segments[rcount1],is_blob01, RL, 0);
      SUB_FIELD (segments[rcount1],segsize, RL, 0);
      SUB_FIELD (segments[rcount1],unknown_2, RL, 0);
      SUB_FIELD (segments[rcount1],ds_version, RL, 0); // datastorage revision
      SUB_FIELD (segments[rcount1],unknown_3, RL, 0);
      SUB_FIELD (segments[rcount1],data_algn_offset, RL, 0);
      SUB_FIELD (segments[rcount1],objdata_algn_offset, RL, 0);
      FIELD_TFF (segments[rcount1].padding, 8, 0); // always 8x 0x55

      switch (_obj->segments[rcount1].type) {
      case 0: // hmm, more than 1 segidx?
#if 0
        REPEAT2 (num_segidx, segidx, Dwg_AcDs_SegmentIndex)
        REPEAT_BLOCK
# ifdef IS_JSON
            KEY (index); VALUE_RL (rcount2, 0);
# endif
            SUB_FIELD_RLL (segidx[rcount2],offset, 0);
            SUB_FIELD_RL (segidx[rcount2],size, 0);
        END_REPEAT_BLOCK
        END_REPEAT (segidx)
#endif
        break;
      case 1:
        if (_obj->datidx_segidx != rcount1)
          LOG_WARN ("Possibly wrong datidx_segidx %d for %d", _obj->datidx_segidx, rcount1);
        SUB_FIELD_RL (datidx,num_entries, 0);
        SUB_FIELD_RL (datidx,di_unknown, 0);
        REPEAT2 (datidx.num_entries, datidx.entries, Dwg_AcDs_DataIndex_Entry)
        REPEAT_BLOCK
#ifdef IS_JSON
            KEY (index); VALUE_RL (rcount2, 0);
#endif
            SUB_FIELD_RL (datidx.entries[rcount2],segidx, 0);
            SUB_FIELD_RL (datidx.entries[rcount2],offset, 0);
            SUB_FIELD_RL (datidx.entries[rcount2],schidx, 0);
        END_REPEAT_BLOCK
        END_REPEAT (datidx.entries)
        break;
      case 3:
        if (_obj->schidx_segidx != rcount1)
          LOG_WARN ("Possibly wrong schidx_segidx %d for %d", _obj->schidx_segidx, rcount1);
        SUB_FIELD_RL (schidx,num_props, 0); // or RLL
        SUB_FIELD_RL (schidx,si_unknown_1, 0);
        REPEAT2 (schidx.num_props, schidx.props, Dwg_AcDs_SchemaIndex_Prop)
        REPEAT_BLOCK
            SUB_FIELD_RL (schidx.props[rcount2],index, 0);
            SUB_FIELD_RL (schidx.props[rcount2],segidx, 0);
            SUB_FIELD_RL (schidx.props[rcount2],offset, 0);
        END_REPEAT_BLOCK
        END_REPEAT (schidx.props)
        SUB_FIELD_RLL (schidx,si_tag, 0); /* 0x0af10c */
        SUB_FIELD_RL (schidx,num_prop_entries, 0); // or RLL
        SUB_FIELD_RL (schidx,si_unknown_2, 0);
        REPEAT2 (schidx.num_prop_entries, schidx.prop_entries, Dwg_AcDs_SchemaIndex_Prop)
        REPEAT_BLOCK
            SUB_FIELD_RL (schidx.prop_entries[rcount2],index, 0);
            SUB_FIELD_RL (schidx.prop_entries[rcount2],segidx, 0);
            SUB_FIELD_RL (schidx.prop_entries[rcount2],offset, 0);
        END_REPEAT_BLOCK
        END_REPEAT (schidx.prop_entries)
        break;
      case 4:
        DEBUG_HERE
        DECODER { _obj->schdat.num_uprops = 1; } // FIXME
        REPEAT2 (schdat.num_uprops, schdat.uprops, Dwg_AcDs_SchemaData_UProp)
        REPEAT_BLOCK
          SUB_FIELD_RL (schdat.uprops[rcount2], size, 0);
          SUB_FIELD_RL (schdat.uprops[rcount2], flags, 0);
        END_REPEAT_BLOCK
        END_REPEAT (schdat.uprops)
        DEBUG_HERE
        REPEAT2 (schdat.num_schemas, schdat.schemas, Dwg_AcDs_Schema)
        REPEAT_BLOCK
            SUB_FIELD_RS (schdat.schemas[rcount2],num_index, 0);
            SUB_FIELD_VECTOR (schdat.schemas[rcount2],index, RLL, num_index, 0);
            SUB_FIELD_RS (schdat.schemas[rcount2],num_props, 0);
            REPEAT2 (schdat.schemas[rcount2].num_props, schdat.schemas[rcount2].props, Dwg_AcDs_Schema_Prop)
#define prop schdat.schemas[rcount2].props[rcount3]
            REPEAT_BLOCK
                SUB_FIELD_RL (prop,flags, 91); // 1, 2, 8
                SUB_FIELD_RL (prop,namidx, 2);
                if (!(_obj->prop.flags & 2))
                {
                  SUB_FIELD_RL (prop,type, 280);
                  if (_obj->prop.type  == 0xe)
                    {
                      SUB_FIELD_RL (prop,type_size, 0)
                    }
                  else
                    _obj->prop.type_size = _AcDs_Schema_Prop_types[_obj->prop.type];
                }
                if (_obj->prop.flags == 1)
                  {
                    SUB_FIELD_RL (prop,unknown_1, 0)
                  }
                else if (_obj->prop.flags == 8)
                  {
                    SUB_FIELD_RL (prop,unknown_2, 0)
                  }
                SUB_FIELD_RS (prop,num_values, 0);
                if (_obj->prop.type_size)
                  {
                    SUB_FIELD_VECTOR_TYPESIZE (prop, values, num_values,
                                               _obj->prop.type_size, 0);
                  }
            END_REPEAT_BLOCK
#undef prop
            END_REPEAT (schdat.schemas[rcount2].props)
        END_REPEAT_BLOCK
        END_REPEAT (schdat.schemas)
        LOG_WARN ("AcDs %s segment yet unhandled", _obj->segments[rcount1].name);
        break;
      case 5: // search
        if (_obj->search_segidx != rcount1)
          LOG_WARN ("Possibly wrong search_segidx %d for %d", _obj->search_segidx, rcount1);
        SUB_FIELD_RL (search,num_search, 0);
        REPEAT2 (search.num_search, search.search, Dwg_AcDs_Search_Data)
        REPEAT_BLOCK
          SUB_FIELD_RL (search.search[rcount2],schema_namidx, 0);
          SUB_FIELD_RL (search.search[rcount2],num_sortedidx, 0);
          SUB_FIELD_VECTOR (search.search[rcount2],sortedidx, RLLd, num_sortedidx, 0);
          SUB_FIELD_RL (search.search[rcount2],num_ididxs, 0);
          SUB_FIELD_RL (search.search[rcount2],unknown, 0);
          REPEAT3 (search.search[rcount2].num_ididxs, search.search[rcount2].ididxs, Dwg_AcDs_Search_IdIdxs)
          REPEAT_BLOCK
            SUB_FIELD_RL (search.search[rcount2].ididxs[rcount3],num_ididx, 0);
            REPEAT4 (search.search[rcount2].ididxs[rcount3].num_ididx, search.search[rcount2].ididxs[rcount3].ididx, Dwg_AcDs_Search_IdIdx)
            REPEAT_BLOCK
              SUB_FIELD_RLL (search.search[rcount2].ididxs[rcount3].ididx[rcount4],handle, 0);
              SUB_FIELD_RL (search.search[rcount2].ididxs[rcount3].ididx[rcount4],num_ididx, 0);
              SUB_FIELD_VECTOR (search.search[rcount2].ididxs[rcount3].ididx[rcount4],ididx, RLL, num_ididx, 0);
            END_REPEAT_BLOCK
            END_REPEAT (search.search[rcount2].ididxs[rcount3].ididx)
          END_REPEAT_BLOCK
          END_REPEAT (search.search[rcount2].ididxs)
        END_REPEAT_BLOCK
        END_REPEAT (search.search)
        break;
      case 7: // prvsav
        if (_obj->prvsav_segidx != rcount1)
          LOG_WARN ("Possibly wrong prvsav_segidx %d for %d", _obj->prvsav_segidx, rcount1);
        // fallthru
        //break;
      case 8: // freesp
        //break;
      case 2: // _data_
        //break;
      case 6: // blob01
        //break;
      default:
#ifndef IS_FREE
        LOG_WARN ("AcDs %s segment yet unhandled", _obj->segments[rcount1].name);
#endif
        break;
      }
  END_REPEAT_BLOCK
  END_REPEAT (segments)
#ifdef IS_FREE
  END_REPEAT (segidx)
#endif

#ifdef IS_DECODER
  // Bind each AcDs ACIS/ASM record to its 3DSOLID through the index, never by
  // the order blobs happen to appear in the section (GoSurvey issue #786: 27 of
  // 89 solids in a Plant 3D drawing got a neighbour's geometry that way).
  //   datidx entry -> slot at segment + 48 + entry.offset:
  //                   RL slot_size, RL unknown, RLL handle, RL local_offset
  //   record       -> segment + objdata_algn_offset * 16 + local_offset:
  //                   RL length, then length bytes ("ASM BinaryFile..." or
  //                   "ACIS BinaryFile..."; other schemas hold e.g. PNG previews)
  DECODER {
    unsigned int num_acis_sab_data = 0;
    const unsigned int wanted = dwg->num_acis_sab_hdl;
    const char end_asm[] = "\016\003End\016\002of\016\003ASM\r\004data";
    const char end_acis[] = "\016\003End\016\002of\016\004ACIS\r\004data";
    BITCODE_RL k;
    for (k = 0; k < _obj->datidx.num_entries; k++)
      {
        const Dwg_AcDs_DataIndex_Entry *ent = &_obj->datidx.entries[k];
        size_t segoff, segend, slot, rec, len;
        BITCODE_RLL handle;
        BITCODE_RL local;
        const char *data, *end_mark, *e;
        size_t end_len;
        Dwg_Object *o;
        Dwg_Entity_3DSOLID *sol;
        char *acis_sab_data;
        const BITCODE_B acis_empty = 0;
        const BITCODE_BS version = 2;
        if (ent->segidx >= _obj->num_segidx || !_obj->segments
            || _obj->segments[ent->segidx].type != 2
            || !_obj->segidx[ent->segidx].offset)
          continue;
        segoff = (size_t)_obj->segidx[ent->segidx].offset;
        segend = segoff + _obj->segidx[ent->segidx].size;
        if (segend > dat->size)
          segend = dat->size;
        slot = segoff + 48 + ent->offset;
        if (slot + 20 > segend)
          continue;
        handle = (BITCODE_RLL)dat->chain[slot + 8]
                 | ((BITCODE_RLL)dat->chain[slot + 9] << 8)
                 | ((BITCODE_RLL)dat->chain[slot + 10] << 16)
                 | ((BITCODE_RLL)dat->chain[slot + 11] << 24)
                 | ((BITCODE_RLL)dat->chain[slot + 12] << 32)
                 | ((BITCODE_RLL)dat->chain[slot + 13] << 40)
                 | ((BITCODE_RLL)dat->chain[slot + 14] << 48)
                 | ((BITCODE_RLL)dat->chain[slot + 15] << 56);
        local = (BITCODE_RL)dat->chain[slot + 16]
                | ((BITCODE_RL)dat->chain[slot + 17] << 8)
                | ((BITCODE_RL)dat->chain[slot + 18] << 16)
                | ((BITCODE_RL)dat->chain[slot + 19] << 24);
        rec = segoff
              + (size_t)_obj->segments[ent->segidx].objdata_algn_offset * 16
              + local;
        if (rec + 4 > segend)
          continue;
        len = (size_t)dat->chain[rec] | ((size_t)dat->chain[rec + 1] << 8)
              | ((size_t)dat->chain[rec + 2] << 16)
              | ((size_t)dat->chain[rec + 3] << 24);
        if (len > segend - rec - 4)
          continue;
        data = (const char *)&dat->chain[rec + 4];
        if (len >= 14 && !memcmp (data, "ASM BinaryFile", 14))
          {
            end_mark = end_asm;
            end_len = sizeof (end_asm) - 1;
          }
        else if (len >= 15 && !memcmp (data, "ACIS BinaryFile", 15))
          {
            end_mark = end_acis;
            end_len = sizeof (end_acis) - 1;
          }
        else
          continue; // another schema's record (e.g. a preview image)
        e = (const char *)memmem (data, len, end_mark, end_len);
        if (!e)
          {
            LOG_WARN ("AcDs record for handle " FORMAT_RLLx
                      " has no End-of-data marker", handle);
            continue;
          }
        len = (size_t)(e - data) + end_len;
        o = dwg_resolve_handle (dwg, handle);
        if (!o || !dwg_obj_is_3dsolid (o))
          {
            LOG_WARN ("AcDs SAB record handle " FORMAT_RLLx " is not a 3DSOLID",
                      handle);
            continue;
          }
        sol = o->tio.entity->tio._3DSOLID;
        if (sol->acis_data && !sol->acis_empty)
          continue; // already has inline data
        acis_sab_data = (char *)malloc (len);
        if (!acis_sab_data)
          return DWG_ERR_OUTOFMEM;
        memcpy (acis_sab_data, data, len);
        num_acis_sab_data++;
        dwg_dynapi_entity_set_value (sol, o->name, "acis_data", &acis_sab_data, 0);
        dwg_dynapi_entity_set_value (sol, o->name, "sab_size", &len, 0);
        dwg_dynapi_entity_set_value (sol, o->name, "version", &version, 0);
        dwg_dynapi_entity_set_value (sol, o->name, "acis_empty", &acis_empty, 0);
        LOG_TRACE ("%s.acis_data = %" PRIuSIZE " for handle " FORMAT_RLLx "\n",
                   o->name, len, handle);
      }
    if (wanted == num_acis_sab_data)
      LOG_TRACE ("Matching number of %u 3DSOLID entities and AcDs SAB data\n",
                 wanted);
    else
      LOG_WARN ("Attached %u AcDs SAB records for %u 3DSOLID handles\n",
                num_acis_sab_data, wanted);
    while (dwg->num_acis_sab_hdl > 0)
      free (SHIFT_HV (dwg, num_acis_sab_hdl, acis_sab_hdl));
  }
#endif
