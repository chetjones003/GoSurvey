/*
 * Copyright (c) 2022, Autodesk, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Autodesk, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Autodesk, Inc. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL Autodesk, Inc. OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdbool.h>

/* Status values used internally in the grid file interpolation system.
   Basically the same values as those used everywhere else in CS-MAP. */
#define csVGRIDI_ST_OK 0
#define csVGRIDI_ST_COVERAGE  1
#define csVGRIDI_ST_SYSTEM   -1

/* The following transformation "methods" are supported by the
   Vertical Transformation facility.  The methods are grouped by
   a method class; the method class distinguishing the type of
   parameters which are used by the method (i.e. which member
   of the parameter union of the cs_VerticalTransform_ structure
   applies to that method). */
#define cs_VDTCPRMTYP_MASK               0xF000
#define cs_VDTCPRMTYP_STANDALONE         0x1000
#define cs_VDTCPRMTYP_OFFSETANDSLOPE     0x2000
#define cs_VDTCPRMTYP_GRIDINTP           0x3000

/* Methods of the GRIDFILE parameter type are qualified by
    the file format of the grid file.  These are the currently
    supported grid file formats. */
#define cs_VDTCFRMT_NONE     0x00
#define cs_VDTCFRMT_EGM2008  0x01
#define cs_VDTCFRMT_GEOID    0x02
#define cs_VDTCFRMT_OSGM15   0x03

/* Standalone methods are (i.e. noparameters): */
#define cs_VDTCMTH_NULLX       (cs_VDTCPRMTYP_STANDALONE + 0x0001)

/* Vertical Methods Are: */
#define cs_VDTCMTH_VRTOFS      (cs_VDTCPRMTYP_OFFSETANDSLOPE + 0x0001)
#define cs_VDTCMTH_VRTOFSSLP   (cs_VDTCPRMTYP_OFFSETANDSLOPE + 0x0002)

/* For programming convenience: */
#define cs_VDTCMTH_NONE        (0x0000)

/* Grid File interpolation methods are: */
#define cs_VDTCMTH_GFILE       (cs_VDTCPRMTYP_GRIDINTP + cs_VDTCFRMT_NONE)
#define cs_VDTCMTH_EGM2008     (cs_VDTCPRMTYP_GRIDINTP + cs_VDTCFRMT_EGM2008)
#define cs_VDTCMTH_GEOID       (cs_VDTCPRMTYP_GRIDINTP + cs_VDTCFRMT_GEOID)

#define cs_VDTCDIR_NONE        0
#define cs_VDTCDIR_FWD         1
#define cs_VDTCDIR_INV         2
#define cs_VDTCDIR_ERR         999

#define cs_VDTCBRG_NONE        0
#define cs_VDTCBRG_BUILDING    1
#define cs_VDTCBRG_COMPLETE    2
#define cs_VDTCBRG_NOTUNIQUE   998
#define cs_VDTCBRG_ERROR       999

#define cs_VXIDX_NOXFRM    -1
#define cs_VXIDX_DUPXFRM   -2
#define cs_VXIDX_ERROR     -3

/* The following are essentially bits used to add specific properties to
   specific transformations and/or their variations.  Curently, we have
   only one such property: that is being reentrant.  Quite likely to be
   more before this code is sent to code heaven. */

#define cs_VXFRMFLG_NONE    0x0000UL
#define cs_VXFRMFLG_RNTRNT  0x0001UL				/* (1UL << 0) */

#define cs_VXFRMFLGS_NULLX       cs_VXFRMFLG_RNTRNT
#define cs_VXFRMFLGS_VRTOFS      cs_VXFRMFLG_RNTRNT
#define cs_VXFRMFLGS_VRTOFSSLP   cs_VXFRMFLG_RNTRNT
#define cs_VXFRMFLGS_GFILE       cs_VXFRMFLG_NONE           /* Depends upon the file format. */
#define cs_VXFRMFLGS_EGM2008     cs_VXFRMFLG_NONE           /* Need to examine buffer use */
#define cs_VXFRMFLGS_GEOID       cs_VXFRMFLG_NONE
#define cs_VXFRMFLGS_OSGM15      cs_VXFRMFLG_NONE


/*
    The following defines define possible values of the bit mapped
    argument to the CS_vxchk function.  These options make it
    possible to use this function in a variety of different
    ways.  The Coordinate System compiler environment, where the
    datums and ellipsoids need to be checked against the dictionaries
    being generated (not necessarily the currently active one) is the
    primary reason for all of this.
*/

#define cs_VXCHK_DATUM	1	/* Turns on checking of the datum key
                               name against the currently active
                               datum dictionary. */
#define cs_VXCHK_REPORT 2	/* Instructs CS_vxchk to report all
                               errors to CS_erpt. */

/*
    The file format of the GEOID99, GEOID03, GEOID06, GEOID09, GEOID12
    and GEOID18 models are all the same. So we define a alias for
    csGeoid99GridFile_ to reuse it as a general Geoid object.
*/
#define csGeoidGridFile_ csGeoid99GridFile_


/******************************************************************************
*******************************************************************************
**                                                                           **
**                Analytical Vertical   Method   Structures                  **
**                                                                           **
*******************************************************************************
******************************************************************************/

/*
    The following structures carry definitions of vertical datums
    in various forms, suitable for the various transformation in
    use.
*/

/* Null Transformation
*/
struct csNullVx_
{
    double value;
};

/* Vertical Offset Transformation
*/
struct csVrtOfs_
{
    double offset;
};

/* Vertical Offset and Slope Transformation
*/
struct csVrtOfsSlp_
{
    double offset;
    double lngOfEvaluation;
    double latOfEvaluation;
    double inclinationInNorth;
    double inclinationInEast;
    struct cs_Datum_ interDatum;

    double rhoO;
    double nuO;
};

/******************************************************************************
*******************************************************************************
**                                                                           **
**           General  Vertical  Grid  Interpolation   File   Object          **
**                                                                           **
*******************************************************************************
******************************************************************************/

struct cs_VGridFile_
{
    short direction;
    unsigned char format;
    char filePath[MAXPATH];

    double density;

    double (*test)(void* gridFile, Const double srcLl[3]);
    int (*frwrd)(void* gridFile, Const double srcLl[3], double* trgHeight);
    int (*invrs)(void* gridFile, Const double srcLl[3], double* trgHeight);
    int (*inRange)(void* gridFile, int cnt, Const double pnts[][3]);
    int (*release)(void* gridFile);
    int (*destroy)(void* gridFile);

    union
    {
        void* verticalPtr;
        struct cs_Egm2008_* egm2008Ptr;
        struct csGeoidGridFile_* geoidPtr;
        struct cs_Ostn15_* ostn15Ptr;
    } fileObject;
};

struct csVGridi_
{
    short userDirection;
    short useBest;

    short fileCount;			/* Number of files */
    struct cs_VGridFile_* gridFiles[csGRIDI1_FILEMAX];
};

/******************************************************************************
*******************************************************************************
**                                                                           **
**                General   Vertical   Data   Structures                     **
**                                                                           **
*******************************************************************************
******************************************************************************/

/* cs_VerticalTransform_

This structure is essentially the binary form of a vertical transformation
definition.  A Vertical Transformation Dictionary is a collection of these
things sorted in order by name.

Essentially, the CS_vdtcsu function (DaTum Conversion Set Up) turns this
thing into a cs_VxXform which enables the actual mathemagics of the
desired operation.  Probably should have been called cs_Vxdef_ */

struct csVerticalXfromParmsFile_
{
    unsigned char fileFormat;
    unsigned char direction;
    char fileName[csGRIDI1_FLNMSZ];
};											/* 240 */

struct cs_VerticalTransform_
{
    char xfrmName [64];
    char srcDatum [cs_KEYNM_DEF];
    char trgDatum [cs_KEYNM_DEF];
    char group [24];
    char description [128];
    char source [64];
    char interDatum[cs_KEYNM_DEF];            /* interpolation datum */
    short methodCode;
    short epsgCode;
    short epsgVariation;
    short inverseSupported;
    short protect;
    double accuracy;
    double rangeMinLng;
    double rangeMaxLng;
    double rangeMinLat;
    double rangeMaxLat;
    double fill01;
    double fill02;
    double fill03;
    double fill04;
    /* The active member of the following union is a function of
       the method type. */
    union csVerticalXformParameters
    {
        struct csVerticalXformParmsOffsetAndSlope
        {
            double offset;
            double lngOfEvaluation;            /* The unit in the defination is degree*/
            double latOfEvaluation;            /* The unit in the defination is degree*/
            double inclinationInNorth;         /* The unit in the defination is arc-second*/
            double inclinationInEast;          /* The unit in the defination is arc-second*/

            char fill[12248]; /* 12288 - (5 * 8) = 12248 */
        } offsetAndSlopeParameters;
        struct csVerticalXformParmsGridFiles_
        {
            short fileReferenceCount;
            struct csVerticalXfromParmsFile_ fileNames[csGRIDI1_FILEMAX];
            char fill01[286];
        } fileParameters;				/* 240 * 50 + 2 = 12002 */
        struct csVerticalXfromParmsSize_
        {
            char unionSize [12288];
        } sizeDetermination;
    } parameters;
};
#define cs_BSWP_VXDEF_BASE          "64c24c24c24c128c64c24c5s9d"
#define cs_BSWP_VXDEF_OFFSETANDSLOP "ddddd12248c"
#define cs_BSWP_VXDEF_FILPRM        "s12000c"

/* DESIGN NOTE:  this object represents a vertical transformation which has
   been fully expanded and implemented.  Any and all extraneous information
   in the Vertical Transformation definition should end up here. */
struct cs_VxXform_
{
    struct cs_VerticalTransform_ vxDef;		/* a copy of the original definition. */

    short isNullXfrm;
    short userDirection;
    short methodCode;

    int (*frwrd)(void *vxXform,Const double* srcLl,double* trgHeight);
    int (*invrs)(void *vxXform, Const double* srcLl,double* trgHeight);
    int (*inRange)(void *vxXform,int cnt,Const double pnts [][3]);
    int (*isNull) (void *vxXform);
    int (*release)(void *vxXform);
    int (*destroy)(void *vxXform);

    /* The following union carries information specific to the
       various transformation types. */
    union
    {
        struct csNullVx_   nullvx;          /* Null transformation */
        struct csVrtOfs_   vrtOfs;          /* Vertical offset*/
        struct csVrtOfsSlp_ vrtOfsSlp;      /* Vertical offset and slope*/
        struct csVGridi_   vgridi;          /* Vertical grid*/
    } xforms;
};

struct cs_VxfrmTab_
{
    char key_nm [64];
    int (EXP_LVL9 * initialize)(struct cs_VxXform_*vxXform);
    int (EXP_LVL9 * isNull)    (void* vxXformParms);
    int (EXP_LVL9 * check)     (struct cs_VerticalTransform_*xfrmDef,
                                unsigned short method_code,
                                int err_list [],
                                int list_sz);
    unsigned short methodCode;
    ulong32_t methodFlags;
    ulong32_t epsgMethodCode;
    char descr [128];
};

struct cs_VGridFormatTab_
{
    char key_nm[64];
    int (EXP_LVL9* initialize)(struct cs_VGridFile_* vxFile);
    int (EXP_LVL9* check)     (struct csVerticalXfromParmsFile_* fileParms, Const char* dictDir,
        int err_list[],
        int list_sz);
    unsigned short formatCode;
    ulong32_t formatFlags;
    ulong32_t epsgMethodCode;
    char descr[128];
};

struct cs_VxIndex_
{
    char xfrmName[cs_KEYNM_DEF1];
    char srcDatum[cs_KEYNM_DEF];
    char trgDatum[cs_KEYNM_DEF];
    double accuracy;
    short inverseSupported;
    short methodCode;
};

struct csVdtmBridgeXfrm_
{
    struct cs_VxIndex_* xfrmPtr;
    short direction;
};

/* A csVdtmBridge_ object is used to automatically construct a path from a source
   vertical datum to a target vertical datum from the Vertical Transformation
   dictionary.

   We start out with just the source and target datum names, and the index
   values set to -1.  The -1 indicates that there is no coreesponding entry
   in the bridge.
*/

struct csVdtmBridge_
{
    short srcIndex;                     /* pointer to the last transformation
                                           added to the bridge from the source
                                           end.  -1 means none as yet. */
    char srcDatumName[cs_KEYNM_DEF];	/* The name of the datum at which the
                                           bridge starts. */
    struct csVdtmBridgeXfrm_ bridgeXfrms[csPATH_MAXXFRM];
                                        /* An ordered array of vertical transformation
                                           index entries defining (when complete) the
                                           Path from the start of the bridge to the end
                                           of the bridge.  Important to have the
                                           direction of each transformation in the
                                           path. */
    char trgDatumName[cs_KEYNM_DEF];	/* The name of the datum at which the
                                           bridge ends (i.e. completes). */
    short trgIndex;						/* pointer to the last transformation
                                           added to the bridge from the traget
                                           end.  csPath_MAXXFRM (or greater)
                                           means none as yet. */

};

/* DESIGN NOTE: this object represents a vertical path which has been fully
   expanded and implemented.  Any and all extraneous information in the
   Vertical Path definition should end up here. */
struct cs_VDtcprm_
{
    char srcKeyName [24];		/* Key name of the source vertical
                                   reference system (datum).  For error
                                   reporting purposes. */
    char trgKeyName [24];		/* Key name of the target vertical
                                   reference system (datum).  For
                                   error reporting purposes. */
    short block_err;			/* Carries the block error reporting code:
                                    cs_DTCFLG_BLK_F,
                                    cs_DTCFLG_BLK_W,
                                    cs_DTCFLG_BLK_1,
                                    cs_DTCFLG_BLK_I
                                */
    short xfrmCount;			/* Number of xforms */
    struct cs_VxXform_* vxforms [csPATH_MAXXFRM];
                                /* An array of transformation types and pointers
                                   to the parameters required by the various
                                   transformation techniques which are required to
                                   get from the source to the target system.*/
};

/*
    The following casts are used to eliminate warnings from
    ANSI compilers.

    Note, the defines do not include the surrounding
    parenthesis so they look something like a cast in
    the code.
*/
#if _RUN_TIME >= _rt_UNIXPCC
#	define cs_VTEST_CAST double(*)(void *,double *)
#	define cs_FRWRD_CAST int(*)(void *,double *,double *)
#	define cs_INVRS_CAST int(*)(void *,double *,double *)
#else
#	define cs_VTEST_CAST double(*)(void *,Const double *)
#	define cs_FRWRD_CAST int(*)(void *,Const double *,double *)
#	define cs_INVRS_CAST int(*)(void *,Const double *,double *)
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct cs_VxIndex_* EXP_LVL5 CS_getVxIndexPtr(void);
unsigned EXP_LVL5 CS_getVxIndexCount();
Const struct cs_VxIndex_* EXP_LVL5 CS_getVxIndexEntry(unsigned index);
void EXP_LVL5 CS_releaseVxIndex(void);
int EXP_LVL5 CS_locateVxByName(Const char* xfrmName);
int EXP_LVL5 CS_locateVxByDatum(unsigned startAt, Const char* srcDtmName, Const char* trgDtmName);
int EXP_LVL5 CS_locateVxByDatum2(int* direction, Const char* srcDtmName, Const char* trgDtmName);
void EXP_LVL9 CSgenerateVxIndex(void);
int EXP_LVL9 CSgetVxsByCounterpartGeodeticDatum(Const char* vdtmName, bool isSrc, struct cs_VxIndex_* vx[], unsigned maxVxCount);

int EXP_LVL9 CSvdtcsuPhaseOne(struct csVdtmBridge_* bridgePtr, struct cs_VDtcprm_* dtcPtr);

Const char* EXP_LVL9 CSvdtcGetInterDtm(struct cs_VDtcprm_* dtcPtr);
struct cs_VDtcprm_* EXP_LVL9 CSvdtcsuByVx(short direction, int blk_erf, struct cs_VxIndex_* vxIdxPtr);

struct cs_VxXform_ EXP_LVL5 *CS_vxloc (Const char* vxDefName,short userDirection);
struct cs_VxXform_ EXP_LVL5 *CS_vxloc1 (Const struct cs_VerticalTransform_*vxXform,short userDirection);

int EXP_LVL1 CS_vxFrwrd (struct cs_VxXform_*xform,Const double srcLl [3], double* tarHeight);
int EXP_LVL1 CS_vxInvrs (struct cs_VxXform_*xform,Const double srcLl [3], double* tarHeight);
int EXP_LVL1 CS_vxIsNull  (struct cs_VxXform_*xform);
void EXP_LVL1 CS_vxDisable (struct cs_VxXform_*xform);
int EXP_LVL1 CS_vxchk(Const struct cs_VerticalTransform_* vxXform, unsigned short vxChkFlg, int err_list[], int list_sz);

struct csVdtmBridge_* CSnewVdtmBridge(Const char* src_dt, Const char* dst_dt);
Const char* CSvdtmBridgeGetSourceDtm(struct csVdtmBridge_* thisPtr);
Const char* CSvdtmBridgeGetTargetDtm(struct csVdtmBridge_* thisPtr);

int CSvdtmBridgeIsComplete(struct csVdtmBridge_* thisPtr);
int CSvdtmBridgeIsFull(Const struct csVdtmBridge_* thisPtr);
int CSvdtmBridgeAddSrcTransformation(struct csVdtmBridge_* thisPtr, Const struct cs_VxIndex_* xfrmPtr,
    short direction);

int         EXP_LVL9      CSnullVxD(struct csNullVx_* nullx);
int         EXP_LVL9      CSnullVxF(struct csNullVx_* nullx, Const double srcLl[3], double* tarHeight);
int         EXP_LVL9      CSnullVxI(struct csNullVx_* nullx, Const double srcLl[3], double* tarHeight);
int         EXP_LVL9      CSnullVxL(struct csNullVx_* nullx, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSnullVxN(struct csNullVx_* nullx);
int         EXP_LVL9      CSnullVxQ(struct cs_VerticalTransform_* vxDef, unsigned short xfrmCode, int err_list[], int list_sz);
int         EXP_LVL9      CSnullVxR(struct csNullVx_* nullx);
int         EXP_LVL9      CSnullVxS(struct cs_VxXform_* vxXfrm);

int         EXP_LVL9      CSvrtofsD(struct csVrtOfs_* vrtOfs);
int         EXP_LVL9      CSvrtofsF(struct csVrtOfs_* vrtOfs, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvrtofsI(struct csVrtOfs_* vrtOfs, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvrtofsL(struct csVrtOfs_* vrtOfs, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSvrtofsN(struct csVrtOfs_* vrtOfs);
int         EXP_LVL9      CSvrtofsQ(struct cs_VerticalTransform_* vxDef, unsigned short xfrmCode, int err_list[], int list_sz);
int         EXP_LVL9      CSvrtofsR(struct csVrtOfs_* vrtOfs);
int         EXP_LVL9      CSvrtofsS(struct cs_VxXform_* vxXfrm);

int         EXP_LVL9      CSvgridiD(struct csVGridi_* gridi);
int         EXP_LVL9      CSvgridiF(struct csVGridi_* gridi, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvgridiI(struct csVGridi_* gridi, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvgridiL(struct csVGridi_* gridi, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSvgridiN(struct csVGridi_* gridi);
int         EXP_LVL9      CSvgridiQ(struct cs_VerticalTransform_* vxDef, unsigned short xfrmCode, int err_list[], int list_sz);
int         EXP_LVL9      CSvgridiR(struct csVGridi_* gridi);
int         EXP_LVL9      CSvgridiS(struct cs_VxXform_* gridi);
int         EXP_LVL9      CSvgridiT(struct csVGridi_* gridi, Const double srcLl[3], short direction);

int         EXP_LVL9      CSegm2008D(struct cs_Egm2008_* egm2008);
int         EXP_LVL9      CSegm2008F(struct cs_Egm2008_* egm2008, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSegm2008I(struct cs_Egm2008_* egm2008, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSegm2008L(struct cs_Egm2008_* egm2008, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSegm2008N(struct cs_Egm2008_* egm2008);
int         EXP_LVL9      CSegm2008Q(struct csVerticalXfromParmsFile_* csVerticalXfromParmsFile_, Const char* dictDir, int err_list[], int list_sz);
int         EXP_LVL9      CSegm2008R(struct cs_Egm2008_* egm2008);
int         EXP_LVL9      CSegm2008S(struct cs_VGridFile_* gridFile);
double      EXP_LVL9      CSegm2008T(struct cs_Egm2008_* egm2008, Const double srcLl[3]);

int         EXP_LVL9      CSvrtofsslpD(struct csVrtOfsSlp_* vrtOfsSlp);
int         EXP_LVL9      CSvrtofsslpF(struct csVrtOfsSlp_* vrtOfsSlp, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvrtofsslpI(struct csVrtOfsSlp_* vrtOfsSlp, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSvrtofsslpL(struct csVrtOfsSlp_* vrtOfsSlp, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSvrtofsslpN(struct csVrtOfsSlp_* vrtOfsSlp);
int         EXP_LVL9      CSvrtofsslpQ(struct cs_VerticalTransform_* vxDef, unsigned short xfrmCode, int err_list[], int list_sz);
int         EXP_LVL9      CSvrtofsslpR(struct csVrtOfsSlp_* vrtOfsSlp);
int         EXP_LVL9      CSvrtofsslpS(struct cs_VxXform_* vxXfrm);

int         EXP_LVL9      CSgeoidD(struct csGeoidGridFile_* geoid);
int         EXP_LVL9      CSgeoidF(struct csGeoidGridFile_* geoid, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSgeoidI(struct csGeoidGridFile_* geoid, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSgeoidL(struct csGeoidGridFile_* geoid, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSgeoidN(struct csGeoidGridFile_* geoid);
int         EXP_LVL9      CSgeoidQ(struct csVerticalXfromParmsFile_* csVerticalXfromParmsFile_, Const char* dictDir, int err_list[], int list_sz);
int         EXP_LVL9      CSgeoidR(struct csGeoidGridFile_* geoid);
int         EXP_LVL9      CSgeoidS(struct cs_VGridFile_* gridFile);
double      EXP_LVL9      CSgeoidT(struct csGeoidGridFile_* geoid, Const double srcLl[3]);

int         EXP_LVL9      CSosgm15D(struct cs_Ostn15_* ostn15);
int         EXP_LVL9      CSosgm15F(struct cs_Ostn15_* ostn15, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSosgm15I(struct cs_Ostn15_* ostn15, Const double srcLl[3], double* trgHeight);
int         EXP_LVL9      CSosgm15L(struct cs_Ostn15_* ostn15, int cnt, Const double pnts[][3]);
int         EXP_LVL9      CSosgm15N(struct cs_Ostn15_* ostn15);
int         EXP_LVL9      CSosgm15Q(struct csVerticalXfromParmsFile_* csVerticalXfromParmsFile_, Const char* dictDir, int err_list[], int list_sz);
int         EXP_LVL9      CSosgm15R(struct cs_Ostn15_* ostn15);
int         EXP_LVL9      CSosgm15S(struct cs_VGridFile_* gridFile);
double      EXP_LVL9      CSosgm15T(struct cs_Ostn15_* ostn15, Const double srcLl[3]);

#ifdef __cplusplus
}
#endif
