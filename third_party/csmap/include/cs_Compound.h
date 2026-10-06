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

enum csHvDtCvtType
{
    hvDtcGeodetic3d,     /* Geodetic 3D transformation */
    hvDtcGeodetic2d,     /* Geodetic 2D transformation */
    hvDtcVertical,       /* Vertical transformation */
};

struct cs_HvDtcprm_
{
    char srcKeyName[24];        /* Key name of the source geodetic
                                   reference system (datum).  For error
                                   reporting purposes. */
    char srcVKeyName[24];       /* Key name of the source vertical
                                   reference system (geodetic or vertical datum).
                                   For error reporting purposes. */
    char trgKeyName[24];        /* Key name of the target geodetic
                                   reference system (datum).  For
                                   error reporting purposes. */
    char trgVKeyName[24];       /* Key name of the target vertical
                                   reference system (geodetic or vertical datum).
                                   For error reporting purposes. */
    short xfrmCount;            /* Number of xforms */
    struct {
        short type;             /* Transformation type, with value defined in enum csHvDtCvtType*/
        union
        {
            struct cs_Dtcprm_ *gxform;
            struct cs_VDtcprm_ *vxform;
        } dtcprm;
        int status;                /* The status each transformation returns*/
    } xforms[csPATH_MAXXFRM];      /* An array of transformation types and pointers
                                   to the parameters required by the various
                                   transformation techniques which are required to
                                   get from the source to the target system. */
    int status;                    /* The status of transformation construction.*/
};
