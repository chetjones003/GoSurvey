# QUICKPROFILE


![QUICKPROFILE command overview](wiki-img:commands/quickprofile.png)
**Command:** `QUICKPROFILE` — aliases: `QPROF`
**Category overview:** [[Point Groups and Surfaces]]

Quick profile along two points: QUICKPROFILE <surface>[, <x1>, <y1>, <x2>, <y2>]

See [[Point Groups and Surfaces]] for related tools and workflows.

---

## Related

- [[Point Groups and Surfaces]]
- [[Command Reference]]

## Along an alignment

`QUICKPROFILE <surface>, <alignment>` samples the surface along the alignment from its start to its end, one foot apart,
and shows the same profile graph as the two-point form. Points off the surface are left out of the graph, and the
command says how many were on it. The alignment itself has no elevation: this reads the ground from the surface (REQ-497)
and nothing is stored on the alignment.
