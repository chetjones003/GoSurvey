# State-plane zones and projections we ship

Inventory for issue #1409 (parent #1403, phase P41). It lists every coordinate system the current app
ships, read from the CS-MAP dictionaries that the Qt app installs (`third_party/csmap/dictionaries` on
`origin/beta`; CS-MAP trunk r3078, dictionary version 1502). The app's Coordinate System picker shows every
category and every code, so the shipped set is the full dictionary.

- **Full list:** [state-plane-zones.csv](state-plane-zones.csv): 11,669 coordinate systems in 245
  categories. Columns: category, code, description, projection (CS-MAP key), datum (ellipsoid key when
  there is no datum), unit.
- **Regenerate:** a small C program that calls `CS_altdr`, `CS_getCatName`, `CS_getItmName` and `CS_csdef`,
  the same calls as `src/survey/src/CoordinateSystems.cpp` on `origin/beta`, and prints the fields above.
  Link it with `csmap.lib` (x64 Release) and run it on the dictionary folder. The program is not committed.

## What the shipped set contains

| Group | Categories | Codes | Notes |
| --- | ---: | ---: | --- |
| USA state-plane (`USA, <State>`) | 50 | 4,173 | State categories. Datums: NAD27, NAD83, HARN/HPGN, NSRS and NATRF2022. Projections include TM, LM, LM1SP, HOM2022, LM-MNDOT, TM-WCCS and LM-WCCS. |
| USA Oregon OCRS (`USA, Oregon OCRS`) | 1 | 390 | Oregon Coordinate Reference System. Projections TM, LM1SP and HOM1XY. |
| USA other (`USA, Administration`, `Major Lakes`, `Gulf of Mexico`, `USGS-Albers`) | 4 | 78 | Not state-plane zones. |
| UTM (`UTM, <datum>`) | 6 | 715 | UTM on WGS84, WGS72, NAD27, NAD83, HPGN and the International ellipsoid. |
| Other countries and regions | 181 | 3,743 | Canada, Europe, UK (OSGB-GG951), China, Japan, Australia and others. |
| Geographic (`Lat Longs`) | 1 | 522 | Latitude/longitude systems, not zones. |
| Obsolete (`Obsolete Coordinate Systems`) | 1 | 2,044 | Shipped, flagged obsolete by the dictionary. |
| Test (`Test Only`) | 1 | 4 | Shipped, test only. |
| **Total** | **245** | **11,669** | |

The dictionary uses 51 projection keys and 51 unit names. The largest projection keys are TM (4,644
codes), UTM (1,771), LL (1,236), LM1SP (1,064) and LM (1,028).

## USA state-plane zones by state

One row per `USA, <State>` category, including `Oregon OCRS` (51 rows). Codes are the count in the CSV.
Datums and units are the union across each category's codes.

| State | Codes | Projections | Datums | Units |
| --- | ---: | --- | --- | --- |
| Alabama | 48 | TM | HARN/AL, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Alaska | 153 | HOM1XY, HOM2022, LL, LM, LM1SP, MSTERO, TM | NAD27, NAD27-AK, NAD83, NATRF2022 | DEGREE, FOOT, IFoot, METER, Meter |
| Arizona | 168 | HOM2022, LM1SP, TM | HARN/AZ, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Arkansas | 58 | LM, LM1SP, PLYCN | HARN/AR, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter, MILE |
| California | 93 | AE, HOM2022, LM, LM1SP, TM | HARN/CA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Colorado | 107 | HOM2022, LM, LM1SP, RSKEWC, TM | HARN/CO, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Connecticut | 13 | LM, LM1SP | HARN/NE, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Delaware | 13 | TM | HARN/MD, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Florida | 50 | AE, HOM2022, LL, LM, LM1SP, TM | CANAVRL, HARN/FL, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | DEGREE, FOOT, IFoot, METER, Meter |
| Georgia | 29 | LM, TM | HARN/GA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Hawaii | 81 | AE, HOM2022, LL, LM1SP, TM, UTM | [INTNL], HARN/HI, HPGN, NAD27, NAD27HII, NAD83, OLDHI, PATRF2022 | DEGREE, FOOT, Foot, IFoot, METER, Meter, MILE |
| Idaho | 54 | HOM2022, LM1SP, TM | HARN/MT, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Illinois | 93 | LM, LM1SP, TM | HARN/IL, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Indiana | 322 | TM | HARN/IN, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Iowa | 66 | LM, LM1SP, TM | HARN/IA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, Foot, IFoot, METER, Meter |
| Kansas | 66 | LM, LM1SP, TM | HARN/KS, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Kentucky | 48 | HOM2022, LM, LM1SP | HARN/KY, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, Foot, IFoot, METER, Meter |
| Louisiana | 35 | HOM2022, LM, LM1SP | HARN/LA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Maine | 45 | HOM2022, TM | HARN/ME, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Maryland | 13 | LM, LM1SP | HARN/MD, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Massachusetts | 24 | LM, LM1SP | HARN/NE, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Michigan | 89 | HOM1XY, HOM2022, LM, LM-MICH, LM1SP, TM | HARN/MI, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, Foot, IFOOT, IFoot, METER, Meter |
| Minnesota | 381 | LM, LM-MNDOT, LM1SP, TM, TM-MNDOT | HARN/MN, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Mississippi | 29 | TM | HARN/MS, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Missouri | 89 | LM1SP, TM | HARN/MO, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Montana | 74 | HOM2022, LM, LM1SP, TM | HARN/MT, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Nebraska | 64 | LM, LM1SP, TM | HARN/NB, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Nevada | 53 | HOM2022, LM1SP, TM | HARN/NV, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| New Hampshire | 13 | TM | HARN/NE, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| New Jersey | 13 | HOM2022, TM | HARN/NJ, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| New Mexico | 45 | HOM2022, TM | HARN/NM, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| New York | 54 | HOM2022, LM, LM1SP, TM | HARN/NY, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| North Carolina | 13 | LM, LM1SP | HARN/NC, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| North Dakota | 62 | LM, LM1SP, TM | HARN/ND, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Ohio | 201 | LM, LM1SP, OSTERO, TM | HARN/OH, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Oklahoma | 66 | HOM2022, LM, LM1SP, TM | HARN/OK, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Oregon | 118 | HOM2022, LM, LM1SP, TM | HARN/WO, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Oregon OCRS | 390 | HOM1XY, LM1SP, TM | HARN/WO, HPGN, NAD83, NSRS07, NSRS11 | IFOOT, METER |
| Pennsylvania | 32 | HOM2022, LM, LM1SP, TM | HARN/PA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Rhode Island | 13 | TM | HARN/NE, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| South Carolina | 18 | LM, LM1SP | HARN/SC, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| South Dakota | 75 | LM, LM1SP, TM | HARN/SD, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Tennessee | 13 | LM, LM1SP | HARN/TN, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Texas | 179 | AE, HOM2022, LM, LM1SP, TM | HARN/TX, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Utah | 230 | HOM2022, LM, LM1SP, TM | HARN/UT, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFOOT, IFoot, METER, Meter |
| Vermont | 13 | TM | HARN/NE, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Virginia | 28 | LM, LM1SP | HARN/VA, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Washington | 28 | LM, LM1SP | HARN/WO, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| West Virginia | 24 | HOM2022, LM | HARN/WV, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |
| Wisconsin | 519 | LM, LM-WCCS, LM1SP, TM, TM-WCCS | HARN/WI, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, Foot, IFOOT, IFoot, METER, Meter |
| Wyoming | 58 | LM1SP, TM | HARN/WY, HPGN, NAD27, NAD83, NATRF2022, NSRS07, NSRS11 | FOOT, IFoot, METER, Meter |

## Decisions for the port (P41)

1. **Scope.** The issue's "zones we ship" can mean the 4,563 USA codes (state categories plus Oregon OCRS)
   only, or the whole dictionary. The current app ships and lists all 11,669. The port needs an explicit
   choice of which families to carry first. This document does not make that choice.
2. **Granularity.** P41 is "one zone per slice". Read literally, one ticket per code is 11,669 tickets, so
   the sensible grain is one ticket per projection family or per state. This is a choice for the owner.
3. **Obsolete and test categories** are shipped today. Whether the port carries them is a scope decision.

## Not done in this change

- No GitHub issues were created. The acceptance line "adding one ticket per zone" is an outward-facing
  step, so it waits for the owner's choice of granularity (decision 2 above).
- No projection code was ported. The inventory is the input to P41.

## Docs review

- README.md, PRIVACY.md: no change. Neither names a zone or a projection.
- GLOSSARY.md: no change. "State-plane coordinates" already defined.
- ARCHITECTURE.md: no change.
- resources/wiki and the GitHub Wiki: not in this branch; no change.
