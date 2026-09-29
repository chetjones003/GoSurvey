/*
 * Copyright (c) 2008, Autodesk, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       documentation and/or other materials provided with the distribution.
 *       notice, this list of conditions and the following disclaimer in the
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

#pragma once

#ifndef csDEF_TO_ASC
#define csDEF_TO_ASC

bool CS_elDefToAsc (std:: ostream& oStrm,struct cs_Eldef_& elDef);
bool CS_dtDefToAsc (std:: ostream& oStrm,struct cs_Dtdef_& dtDef);
bool CS_csDefToAsc (std:: ostream& oStrm,struct cs_Csdef_& csDef);
bool CS_gxfDefToAsc (std:: ostream& oStrm,const cs_Gxdef_& gxDef);

// We define an array of this structure which carries an element for each line of
// data in a 'CoordSys.asc' data file.  This works as the type of each line is
// identified by the identifier assigned, and duplications within any individual
// definition are not allowed.  Note that the order in which these definitions
// appear is the order in which they will appear in the resulting .asc data
// file.
struct csAscTxtLine
{
    char Label [32];            // Type of line
    char Value [64];            // The value associated with the type
    char Comment [64];          // Optional comment to appear on the text line
    unsigned Id;                // A numeric ID used to find a specific entry
                                // in the array
    bool indent;                // 'true' says the line should be indented.
};

#endif			// csDEF_TO_ASC
