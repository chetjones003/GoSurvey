/*
 * Copyright (c) 2008, Autodesk, Inc.
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

/*	Have we been here before? */
#ifndef __CS_EPSG_STUFF__H
#define __CS_EPSG_STUFF__H

// Ignore Spelling: wstring

#include <math.h>
#include <stack>
#include <list>

// The order in which these enumerators appear is important.  New versions must
// always map to a high numeric value than older versions.  The idea here is
// that the numeric value of the enumerators match the Version Number (as an
// integer) in the last record of the Version History table.  We use enumerators
// rather than just integers so that should it be necessary in future, we can
// make distinctions of versions in between major revision levels.  This is not
// necessary at the current time.  The ascending order MUST be preserved.
enum EpsgVersion {	epsgVerNone    =  0,
                    epsgVerSix     =  6,
                    epsgVerSeven   =  7,
                    epsgVerEight   =  8,
                    epsgVerNine    =  9,
                    epsgVerTen     = 10,
                    epsgVerEleven  = 11,
                    epsgVerUnknown = 99
};

// An enumeration of the tables in the EPSG Parameter Dataset.  All tables are
// enumerated even though some are not present in all versions of the parameter
// database.  Obviously, tables which were added in a specific version will
// not be present in a previous version.
enum EcsEpsgTable { epsgTblNone = 0,
                    epsgTblAlias,
                    epsgTblArea,					// deleted in EPSG10
                    epsgTblChange,
                    epsgTblConventionalRS,
                    epsgTblAxisName,
                    epsgTblAxis,
                    epsgTblReferenceSystem,
                    epsgTblCoordinateSystem,
                    epsgTblOperationMethod,
                    epsgTblParameterUsage,
                    epsgTblParameterValue,
                    epsgTblParameter,
                    epsgTblOperationPath,
                    epsgTblCoordinateOperation,
                    epsgTblDatum,
                    epsgTblDatumEnsemble,			// Added in EPSG 10
                    epsgTblDatumEnsembleMember,		// Added in EPSG 10
                    epsgTblDatumRealizationMethod,	// Added in EPSG 10
                    epsgTblDefiningOperation,		// Added in EPSG 10
                    epsgTblDeprecation,
                    epsgTblEllipsoid,
                    epsgTblExtent,					// Added in EPSG 10
                    epsgTblNamingSystem,
                    epsgTblPrimeMeridian,
                    epsgTblScope,					// Added in EPSG 10
                    epsgTblSupercession,
                    epsgTblUnitOfMeasure,
                    epsgTblUsage,					// Added in EPSG 10
                    epsgTblVersionHistory,
                    epsgTblUnknown
                  };


// An enumeration of the fields within the EPSG Parameter Dataset.  To the
// extent possible, fields in the EPSG tables which are the same as fields
// in other tables are given the same enumerator.  For example, the
// enumerator value epsgFldDatumCode is used to access the datum code
// value in all three (or is it four? now five?) tables in which this value
// appears.  This enumeration simply assigns a unique integer value to each
// type of field which is then used in the detailed EPSG table definition table
// defined below.  Note that not all fields are used in all versions.

// The names assigned often include a reference to a table in abbreviated form.
// Fields which show up in multiple tables do not include a table name reference.
// As revisions move data fields from one table to another, some of these names
// which include a reference to a table will have duplicate names for a field
// with a specific purpose.
enum EcsEpsgField { epsgFldNone = 0,
                    epsgFldAction,
                    epsgFldAliasCode,
                    epsgFldAlias,
                    epsgFldAnchorEpoch,
                    epsgFldAreaCode,
                    epsgFldAreaName,
                    epsgFldAreaOfUse,
                    epsgFldAreaOfUseCode,
                    epsgFldAreaSouthBoundLat,
                    epsgFldAreaNorthBoundLat,
                    epsgFldAreaWestBoundLng,
                    epsgFldAreaEastBoundLng,
                    epsgFldBoundingPolygonFileName,
                    epsgFldChangeId,
                    epsgFldCmpdHorizCrsCode,
                    epsgFldCmpdVertCrsCode,
                    epsgFldCodeAreaOfUse,
                    epsgFldCodeNamingSystem,
                    epsgFldCodesAffected,
                    epsgFldComment,
                    epsgFldConventionalRsCode,
                    epsgFldConventionalRsName,
                    epsgFldConcatOperationCode,
                    epsgFldCoordAxisAbbreviation,
                    epsgFldCoordAxisCode,
                    epsgFldCoordAxisName,
                    epsgFldCoordAxisNameCode,
                    epsgFldCoordAxisOrientation,
                    epsgFldCoordOpAccuracy,
                    epsgFldCoordOpCode,
                    epsgFldCoordOpMethodCode,
                    epsgFldCoordOpMethodName,
                    epsgFldCoordOpName,
                    epsgFldCoordOpScope,
                    epsgFldCoordOpType,
                    epsgFldCoordOpVariant,
                    epsgFldCoordRefSysCode,
                    epsgFldCoordRefSysKind,
                    epsgFldCoordRefSysName,
                    epsgFldCoordSysCode,
                    epsgFldCoordSysName,
                    epsgFldCoordSysType,
                    epsgFldCoordTfmVersion,
                    epsgFldCrsScope,
                    epsgFldDataSource,
                    epsgFldDateClosed,
                    epsgFldDatumCode,
                    epsgFldDatumName,
                    epsgFldDatumScope,
                    epsgFldDatumType,
                    epsgFldDatumEnsembleCode,
                    epsgFldDatumSequence,
                    epsgFldEnsembleAccuracy,
                    epsgFldDeprecated,
                    epsgFldDeprecationDate,
                    epsgFldDeprecationId,
                    epsgFldDeprecationReason,
                    epsgFldDescription,
                    epsgFldDimension,
                    epsgFldEllipsoidCode,
                    epsgFldEllipsoidName,
                    epsgFldEllipsoidShape,
                    epsgFldExample,
                    epsgFldExtentCode,              // New Extent Table
                    epsgFldExtentName,              // New Extent Table
                    epsgFldExtentDescription,       // New Extent Table
                    epsgFldExtentEastBoundLon,      // New Extent Table
                    epsgFldExtentNorthBoundLat,     // New Extent Table
                    epsgFldExtentSouthBoundLat,     // New Extent Table
                    epsgFldExtentWestBoundLon,      // New Extent Table
                    epsgFldExtentVerticalMin,       // New Extent Table
                    epsgFldExtentVerticalMax,       // New Extent Table
                    epsgFldExtentVerticalCrsCode,   // New Extent Table
                    epsgFldExtentTemporalBegin,     // New Extent Table
                    epsgFldExtentTemporalEnd,       // New Extent Table
                    epsgFldFactorB,
                    epsgFldFactorC,
                    epsgFldFormula,
                    epsgFldFrameReferenceEpoch,     // New field in Datum table -- EPSG10
                    epsgFldGreenwichLongitude,
                    epsgFldInformationSource,
                    epsgFldInvFlattening,
                    epsgFldIsoA2Code,              // Moved from Area to Extent in EPSG-10
                    epsgFldIsoA3Code,              // Moved from Area to Extent in EPSG-10
                    epsgFldIsoNumericCode,         // Moved from Area to Extent in EPSG-10
                    epsgFldLeftLongitude,
                    epsgFldNamingSystemCode,
                    epsgFldNamingSystemName,
                    epsgFldNorthLatitude,
                    epsgFldOrder,
                    epsgFldObjectCode,
                    epsgFldObjectType,
                    epsgFldOpPathStep,
                    epsgFldOriginDescription,
                    epsgFldParameterCode,
                    epsgFldParameterName,
                    epsgFldParameterValue,
                    epsgFldParamSignReversal,
                    epsgFldParamValueFileRef,
                    epsgFldPrimeMeridianCode,
                    epsgFldPrimeMeridianName,
                    epsgFldProjectionConvCode,
                    epsgFldPublicationDate,         // New field in Datum table -- EPSG10
                    epsgFldRealizationEpoch,        // New field in Datum table -- EPSG10
                    epsgFldRealizationMethodCode,   // New field in Datum table -- EPSG10
                    epsgFldRealizationMethodName,   // New field in Datum table -- EPSG10
                    epsgFldRemarks,
                    epsgFldReplacedBy,
                    epsgFldReportDate,
                    epsgFldReporter,
                    epsgFldRequest,
                    epsgFldReverseOp,
                    epsgFldRevisionDate,
                    epsgFldRightLongitude,
                    epsgFldScopeCode,
                    epsgFldScope,
                    epsgFldSemiMajorAxis,
                    epsgFldSemiMinorAxis,
                    epsgFldShowCrs,
                    epsgFldShowOperation,
                    epsgFldSingleOperationCode,
                    epsgFldSortOrder,
                    epsgFldSourceCrsCode,
                    epsgFldSourceGeogCrsCode,
                    epsgFldSouthLatitude,
                    epsgFldSupercededBy,
                    epsgFldSupercedes,
                    epsgFldSupersessionId,
                    epsgFldSupersessionType,
                    epsgFldSupersessionYear,
                    epsgFldTablesAffected,
                    epsgFldTargetCrsCode,
                    epsgFldTargetUomCode,
                    epsgFldUnitOfMeasName,
                    epsgFldUnitOfMeasType,
                    epsgFldUomCode,
                    epsgFldUomCodeSourceCoordDiff,
                    epsgFldUomCodeTargetCoordDiff,
                    epsgFldUsageCode,
                    epsgFldVersionDate,
                    epsgFldVersionHistoryCode,
                    epsgFldVersionNumber,
                    epsgFldVersionRemarks,
                    epsgFldUnknown
                  };

/* As of version 10, there are thirteen tables which have references to other
    tables which have a field/column of with the name "OBJECT_TABLE_NAME" 
    ("Object_Type" in the Access field label line).  When referencing records
    in these tables, the object type must be specified in order to be sure the
    reference is unambiguous.  This enumeration lists these tables and assigns
    each enumerator a either the equivalent table ID value or the value of
    'epsgTblNone'.  Thus, we have the enumerator values are assigned such that:

    1> The enumeration value can be used for validation purposes.
    2> The enumeration provides a user defined type which can be used in
       function and operator overloading.
    3> The table name table can be used to convert between names and enumerator
       values.
    4> Maintenance is enhanced as changes only require changing this
       enumeration.
*/
enum EcsEpsgObject {epsgObjectNone                 = EcsEpsgTable::epsgTblNone,
                    epsgObjAlias                   = EcsEpsgTable::epsgTblNone,
                    epsgObjArea                    = EcsEpsgTable::epsgTblUnknown,			// deleted in version 10
                    epsgObjChange                  = EcsEpsgTable::epsgTblNone,				// ID is Julian(?) date
                    epsgObjConventionalRS          = EcsEpsgTable::epsgTblNone,
                    epsgObjAxisName                = EcsEpsgTable::epsgTblAxisName,
                    epsgObjAxis                    = EcsEpsgTable::epsgTblNone,
                    epsgObjReferenceSystem         = EcsEpsgTable::epsgTblReferenceSystem,
                    epsgObjCoordinateSystem        = EcsEpsgTable::epsgTblCoordinateSystem,
                    epsgObjOperationMethod         = EcsEpsgTable::epsgTblOperationMethod,
                    epsgObjParameterUsage          = EcsEpsgTable::epsgTblNone,
                    epsgObjParameterValue          = EcsEpsgTable::epsgTblNone,
                    epsgObjParameter               = EcsEpsgTable::epsgTblParameter,
                    epsgObjOperationPath           = EcsEpsgTable::epsgTblNone,
                    epsgObjCoordinateOperation     = EcsEpsgTable::epsgTblCoordinateOperation,
                    epsgObjDatum                   = EcsEpsgTable::epsgTblDatum,
                    epsgObjDatumEnsemble           = EcsEpsgTable::epsgTblNone,
                    epsgObjDatumEnsembleMember     = EcsEpsgTable::epsgTblNone,
                    epsgObjDatumRealizationMethod  = EcsEpsgTable::epsgTblNone,
                    epsgObjDefiningOperation       = EcsEpsgTable::epsgTblNone,
                    epsgObjDeprecation             = EcsEpsgTable::epsgTblDeprecation,
                    epsgObjEllipsoid               = EcsEpsgTable::epsgTblEllipsoid,
                    epsgObjExtent                  = EcsEpsgTable::epsgTblExtent,
                    epsgObjNamingSystem            = EcsEpsgTable::epsgTblNamingSystem,
                    epsgObjPrimeMeridian           = EcsEpsgTable::epsgTblPrimeMeridian,
                    epsgObjScope                   = EcsEpsgTable::epsgTblScope,
                    epsgObjSupercession            = EcsEpsgTable::epsgTblNone,
                    epsgObjUnitOfMeasure           = EcsEpsgTable::epsgTblUnitOfMeasure,
                    epsgObjUsage                   = EcsEpsgTable::epsgTblNone,
                    epsgObjVersionHistory          = EcsEpsgTable::epsgTblNone,
                    epsgObjUnknown                 = EcsEpsgTable::epsgTblUnknown
};

// Note there is a difference between a CRS and  CSys
enum EcsCsysType { epsgCsysTypNone = 0,
                   epsgCsysTypAffine,
                   epsgCsysTypCartesian,
                   epsgCsysTypCylindrical,
                   epsgCsysTypEllipsoidal,
                   epsgCsysTypOrdinal,          // new Version 10
                   epsgCsysTypLinear,
                   epsgCsysTypParametric,
                   epsgCsysTypPolar,
                   epsgCsysTypSpherical,
                   epsgCsysTypTemporalCnt,
                   epsgCsysTypTemporalDtTime,
                   epsgCsysTypTemporalMeasure,
                   epsgCsysTypVertical,
                   epsgCsysTypUnknown
                 };

enum EcsCrsType { epsgCrsTypNone = 0,
                  epsgCrsTypCompund,
                  epsgCrsTypDerived,
                  epsgCrsTypEngineering,
                  epsgCrsTypGeocentric,
                  epsgCrsTypGeographic2D,
                  epsgCrsTypGeographic3D,
                  epsgCrsTypProjected,
                  epsgCrsTypVertical,
                  epsgCrsTypUnknown
                };

enum EcsOpType { epsgOpTypNone = 0,
                 epsgOpTypConversion,
                 epsgOpTypTransformation,
                 epsgOpTypConcatenated,
                 epsgOpTypPointMotion,          // new Version 10
                 epsgOpTypUnknown
               };

enum EcsDtmType { epsgDtmTypNone = 0,
                  epsgDtmTypEngineering,
                  epsgDtmTypGeodetic,
                  epsgDtmTypVertical,
                  epsgDtmTypEnsemble,           // new Version 10
                  epsgDtmTypDynamicGeodetic,
                  epsgDtmTypDynamicVertical,
                  epsgDtmTypUnknown
               };

enum EcsUomType { epsgUomTypNone = 0,
                  epsgUomTypLinear,
                  epsgUomTypAngular,
                  epsgUomTypScale,
                  epsgUomTypTime,               // new Version 10
                  epsgUomTypUnknown
               };

enum EcsOrientation { epsgOrntNone = 0,
                      epsgOrntForward,
                      epsgOrntGeoCtrX,
                      epsgOrntGeoCtrY,
                      epsgOrntGeoCtrZ,
                      epsgOrntEast,
                      epsgOrntWest,
                      epsgOrntNorth,
                      epsgOrntSouth,
                      epsgOrntUp,
                      epsgOrntDown,
                      epsgOrntUnknown
                    };

struct TcsEpsgTblMap
{
    EcsEpsgTable  TableId;
    short         FieldCount;
    EcsEpsgField  CodeKeyFieldId;
    wchar_t       TableName [64];
    EcsEpsgField  Sort1;
    EcsEpsgField  Sort2;
    EcsEpsgField  Sort3;
    EcsEpsgField  Sort4;
};

struct TcsEpsgFldMap
{
    EcsEpsgTable TableId;
    EcsEpsgField FieldId;
    short        FieldNbr;
    wchar_t      FieldName [64];
};

struct TcsEpsgCsysTypeMap
{
    EcsCsysType CsysType;
    wchar_t     CsysTypeName [64];
};

struct TcsEpsgCrsTypeMap
{
    EcsCrsType  CrsType;
    wchar_t     CrsTypeName [64];
};

struct TcsEpsgOpTypeMap
{
    EcsOpType  OpType;
    wchar_t    OpTypeName [64];
};

struct TcsEpsgDtmTypeMap
{
    EcsDtmType  DtmType;
    wchar_t     DtmTypeName [64];
};

struct TcsEpsgUomTypeMap
{
    EcsUomType  UomType;
    wchar_t     UomTypeName [64];
};

struct TcsEpsgOrntTypeMap
{
    EcsOrientation  OrntType;
    wchar_t         OrntTypeName [64];
};

// The following functions simply provide access to the tables defined above.
// They are independent of the TcsEPsgDatSetV6 object.  However, it is unlikely
// that the results produced by these functions have much value outside the
// environment of a TcsEpsgDataSetV6 object.  So, they could. and some day
// probably will be, moved to reside within the namespace of the
// TcsEPsgDataSetV6 object.
const wchar_t* GetEpsgTableName (EcsEpsgTable tblId);
EcsEpsgTable GetEpsgTableId (const wchar_t* tableName);
EcsEpsgField GetEpsgCodeFieldId (EcsEpsgTable tableId);
short GetEpsgCodeFieldNbr (EcsEpsgTable tableId);
short GetEpsgFieldNumber (EcsEpsgTable tableId,EcsEpsgField fieldId);

EcsEpsgObject GetEpsgObject (const wchar_t* espgObjTypName);
const wchar_t* GetEpsgObjName (EcsEpsgObject epsgObjectId);

EcsCsysType GetEpsgCsysType (const wchar_t* csysTypeName);
EcsCrsType GetEpsgCrsType (const wchar_t* crsTypeName);
EcsOpType GetEpsgOpType (const wchar_t* opTypeName);
EcsDtmType GetEpsgDtmType (const wchar_t* dtmTypeName);
EcsUomType GetEpsgUomType (const wchar_t* uomTypeName);
//newPage//
//============================================================================
// EPSG Code Variable
//
// This object is used to represent an EPSG code.  All code specific to EPSG
// code values is handled within this object.
//
// Look to TcsEPsgSupport.cpp for the implementation of this object.
//
class TcsEpsgCode
{
    //=========================================================================
    // Static Constants, Variables, and Member Functions
    static const unsigned long InvalidValue;        // Zero, for now!
    static const unsigned long MaxValue;            // 32767, for now!
public:
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
    TcsEpsgCode (void);
    TcsEpsgCode (unsigned long epsgCode);
    TcsEpsgCode (unsigned int epsgCode);
    TcsEpsgCode (const wchar_t* epsgCode);
    TcsEpsgCode (const std::wstring& epsgCode);
    TcsEpsgCode (const TcsEpsgCode& source);
    ~TcsEpsgCode (void);
    TcsEpsgCode& operator= (const TcsEpsgCode& rhs);
    TcsEpsgCode& operator= (unsigned long epsgCode);
    //=========================================================================
    // Operator Overrides
    bool operator< (unsigned long epsgCode) const;
    bool operator< (const std::wstring& epsgCode) const;
    bool operator== (unsigned long epsgCode) const;
    bool operator== (unsigned int epsgCode) const;
    bool operator== (const std::wstring& epsgCode) const;
    bool operator> (unsigned long epsgCode) const;
    bool operator> (const std::wstring& epsgCode) const;
    operator unsigned long () const {return EpsgCode; };
    TcsEpsgCode operator++ (void);
    TcsEpsgCode operator++ (int dummy);
    TcsEpsgCode& operator+= (unsigned long rhs);
    TcsEpsgCode& operator-= (unsigned long rhs);
    TcsEpsgCode& operator+= (int rhs);
    TcsEpsgCode& operator-= (int rhs);
    TcsEpsgCode operator-- (void);
    TcsEpsgCode operator-- (int dummy);
    TcsEpsgCode operator+ (unsigned long rhs) const;
    TcsEpsgCode operator+ (int rhs) const;
    TcsEpsgCode operator- (unsigned long rhs) const;
    TcsEpsgCode operator- (int rhs) const;
    //=========================================================================
    // Public Named Functions
    bool IsValid (void) const;
    bool IsNotValid (void) const;
    std::wstring AsWstring (void) const;
    std::string AsString (void) const;
    bool AsString (wchar_t* result,size_t resultSize) const;
    bool AsString (char* result,size_t resultSize) const;
    void Invalidate (void) {EpsgCode = InvalidValue; };
protected:
    //=========================================================================
    // Protected Support Functions
    unsigned long StrToEpsgCode (const wchar_t* epsgCodeStr) const;
    unsigned long StrToEpsgCode (const char* epsgCodeStr) const;
private:
    //=========================================================================
    // Private Support Functions
    //=========================================================================
    // Private Data Members
    unsigned long EpsgCode;
};
//newPage//
// Table for mapping EPSG Coordinate Operation Method codes to CS_MAP
// Operation codes (for the binary definitions) and key names (for the
// ASCII dictionary definitions.
//
// This is not a solid one to one relationship, so the tables are used in the
// easy cases; some intriguing code handles the remainder.  Occasionally,
// manual intervention is required.
struct TcsEpsgMethMap
{
    TcsEpsgCode  EpsgMethCode;
    char         EpsgMethName [128];	// A descriptive name, EPSG in some cases
    unsigned     CsMapMethodCode;
    char         CsMapKeyName [64];
};
// Table for mapping EPSG Operation Codes to a list of parameter values
// by EPSG parameter code.  While mathematically the same, the format differs
// slightly so using the EPSG Parameter Usage Table directly would be very
// complex.  This cannot be used for every method type, but for a lot of them.
struct TcsEpsgParmMap
{
    TcsEpsgCode  EpsgMethCode;
    TcsEpsgCode  DeltaX;
    TcsEpsgCode  DeltaY;
    TcsEpsgCode  DeltaZ;
    TcsEpsgCode  RotateX;
    TcsEpsgCode  RotateY;
    TcsEpsgCode  RotateZ;
    TcsEpsgCode  ScaleDiff;
    TcsEpsgCode  TranslateX;
    TcsEpsgCode  TranslateY;
    TcsEpsgCode  TranslateZ;

	/* Time  Dependent stuff, RPC == Rate of Change */
    TcsEpsgCode  RocDeltaX;
    TcsEpsgCode  RocDeltaY;
    TcsEpsgCode  RocDeltaZ;
    TcsEpsgCode  RocRotateX;
    TcsEpsgCode  RocRotateY;
    TcsEpsgCode  RocRotateZ;
    TcsEpsgCode  RocScaleDiff;
};

//newPage//
//============================================================================
// EPSG Table Specialization
//
// An object which encapsulates all CSV file functionality, adding a few special
// features particular to EPSG tables:
//
// 1> Maintains a binary unsigned long index on the first field of each table.
//    The default Index feature of the TcsCsvFileBase object is std::wstring
//    based and not as useful as the unsigned long based index used in this
//    object.
// 2> Provides for setting a current record, and accessing fields in that record.
//    Use of this feature is discouraged, as a multi-thread safe API is now
//    available.
// 3> Provides getting field data in the specific forms useful for dealing
//    with EPSG type data.
//
// Currently, there are several tables which are sorted by the EPSG code
// value.  In this case, generating an Index for the table is superfluous,
// a simple binary search (i.e. lower_bound) would work just as well.
// Eliminating this redundancy remains a "TODO" item.
//
class TcsEpsgTable : public TcsCsvFileBase
{
public:
    //=========================================================================
    // Static Constants, Variables, and Member Functions
    static const wchar_t LogicalTrue  [6];
    static const wchar_t LogicalFalse [6];
    static bool IsLogicalTrue (const wchar_t* logicalValue);
    static bool IsLogicalFalse (const wchar_t* logicalValue);
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
    TcsEpsgTable (const TcsEpsgTblMap& tblMap,const wchar_t* databaseFldr);
    TcsEpsgTable (const TcsEpsgTable& source);
    virtual ~TcsEpsgTable (void);
    TcsEpsgTable& operator= (const TcsEpsgTable& rhs);
    //=========================================================================
    // Operator Overrides
    //=========================================================================
    // Public Named Functions
    bool IsOk (void) const {return Ok; };
    EcsEpsgTable GetTableId (void) const {return TableId; };
    const TcsCsvStatus& GetStatus (void) const {return CsvStatus; };
    unsigned LocateRecordByEpsgCode (const TcsEpsgCode& epsgCode) const;
    unsigned LocateRecordByObject (const EcsEpsgObject& objectId,const TcsEpsgCode& epsgCode) const;
    bool EpsgLocateCode (TcsEpsgCode& epsgCode,EcsEpsgField fieldId,const wchar_t* fldValue) const;

    // The following functions locate the record which meets the criteria
    // provided, and return its record number.  In this case, the first data
    // record is record number zero as there are no label records in this
    // environment.  (There may be labels, but they are not considered a
    // record.)
    unsigned EpsgLocateFirst (EcsEpsgField fieldId,const wchar_t* fldValue,bool honorCase = false) const;
    unsigned EpsgLocateFirst (EcsEpsgField fieldId,const TcsEpsgCode& epsgCode) const;
    unsigned EpsgLocatepsgFldSourceCrsCodeeFirst (EcsEpsgField fieldId,const TcsEpsgCode& epsgCode) const;
    unsigned EpsgLocateNext (unsigned startAfter,EcsEpsgField fieldId,const wchar_t* fldValue,
                                                                      bool honorCase = false) const;
    unsigned EpsgLocateNext (unsigned startAfter,EcsEpsgField fieldId,const TcsEpsgCode& epsgCode) const;

    // The following apply to the record identified by the epsgCode parameter,
    // or the record number parameter, as the case may be; they do not affect
    // the current record setting.  More importantly, they will work on a
    // "const" object and are multi-thread safe.
    bool IsDeprecated (const TcsEpsgCode& epsgCode) const;
    bool IsDeprecated (unsigned recNbr) const;

    bool GetField (std::wstring& result,const TcsEpsgCode& epsgCode,short fieldNbr) const;
    bool GetAsLong (long& result,const TcsEpsgCode& epsgCode,short fieldNbr) const;
    bool GetAsULong (unsigned long& result,const TcsEpsgCode& epsgCode,short fieldNbr) const;
    bool GetAsEpsgCode (TcsEpsgCode& result,const TcsEpsgCode& epsgCode,short fieldNbr) const;
    bool GetAsReal (double& result,const TcsEpsgCode& epsgCode,short fieldNbr) const;

    bool GetField (std::wstring& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsLong (long& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsULong (unsigned long& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsEpsgCode (TcsEpsgCode& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsReal (double& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsChars (char *result,unsigned rsltSize,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;
    bool GetAsBool (bool& result,const TcsEpsgCode& epsgCode,EcsEpsgField fieldId) const;

    // The following apply to the record identified by the recordNbr parameter,
    // they do not affect the current record setting.
    bool GetField (std::wstring& result,unsigned int recNbr,EcsEpsgField fieldId) const;
    bool GetAsLong (long& result,unsigned recNbr,EcsEpsgField fieldId) const;
    bool GetAsULong (unsigned long& result,unsigned recNbr,EcsEpsgField fieldId) const;
    bool GetAsEpsgCode (TcsEpsgCode& result,unsigned recNbr,EcsEpsgField fieldId) const;
    bool GetAsReal (double& result,unsigned recNbr,EcsEpsgField fieldId) const;

    // The following provides access to the derived TcsCsvFileBase::GetField
    // function without the caller having to have knowledge of the
    // TcsCsvStatus object.
    bool GetField (std::wstring& result,unsigned recNbr,short fieldNbr) const;

    // This structure is used to provide the csvStatus parameter required by
    // the TcsCsvFileBase base object.  The contents are highly dependent
    // on the context and last operation.  Probably should be a protected
    // function.
    TcsCsvStatus& GetCsvStatus (void);
    const TcsCsvStatus& GetCsvStatus (void) const;
private:
    //=========================================================================
    // Private Support Functions
    bool PrepareCsvFile (void);
    bool BuildEpsgIndex (short fldNbr,TcsCsvStatus& csvStatus);
    //=========================================================================
    // Private Data Members
    bool Ok;								// true = construction succeeded
    bool Sorted;							// Sorted in designated order
    bool Indexed;							// true = indexed by EPSG code
    EcsEpsgTable TableId;					// The ID of this table.
    EcsEpsgField CodeKeyField;				// ID of EPSG unique identifying code
    TcsCsvSortFunctor SortFunctor;			// Specifies sort order for this table
    std::map<TcsEpsgCode,unsigned> CodeIndex;
    TcsCsvStatus CsvStatus;					// Status of CSV operations
};
//newPage//
//=============================================================================
// TcsEpsgDataSetV6  -  An EPSG dataset based on the version 6 model.
//
// An image of the dataset in .csv form is expected to reside in the directory
// provided to the constructor.  This object is intended to be a read only
// object but nothing specific was done to preclude changing the underlying
// tables or writing changes back to the .csv files.  There are just no member
// functions at this time to support such operation.
//
// EPSG is up to version 11 now (January 2024).  At version 10, major changes
// were implemented.  Essentially:
//  * the Area Table has been removed
//  * Several new tables have bee added
//
// The object has been updated to work with all known versions.  Accessing
// features present in the newer versions when an older version is present
// produces error conditions.  With regard to the changes made regarding the
// ARea table, requests based on the Area table are properly mapped to the
// newer Extent table.
//
// NOTE:  The ReleaseLevel member carries the complete Version description for
// reporting purposes.  The Version number member is used to determine which
// version of the database schema is active.
//
class TcsEpsgDataSetV6
{
public:
    //=========================================================================
    // Static Constants, Variables, and Member Functions
    static short GetFldNbr (EcsEpsgTable tableId,EcsEpsgField fieldId);
    static short GetFldName (std::wstring& fieldName,EcsEpsgTable tableId,EcsEpsgField fieldId);
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
    TcsEpsgDataSetV6 (const wchar_t* databaseFolder,const wchar_t* revLevel = 0);
    TcsEpsgDataSetV6 (const TcsEpsgDataSetV6& source);
    virtual ~TcsEpsgDataSetV6 (void);
    TcsEpsgDataSetV6& operator= (const TcsEpsgDataSetV6& rhs);
    //=========================================================================
    // Operator Overrides
    //=========================================================================
    // Public Named Functions
    // Implementation of these functions can be found in csEpsgStuff.cpp
    //=========================================================================
    //		Basic Support Functions
    bool IsOk (void);
    bool IsOk (void) const;
    TcsEpsgTable* GetTablePtr (EcsEpsgTable tableId);
    const TcsEpsgTable* GetTablePtr (EcsEpsgTable tableId) const;
    unsigned GetRecordCount (EcsEpsgTable tableId) const;
    bool ConvertUnits (double& result,TcsEpsgCode trgUomCode,double value,
                                                             TcsEpsgCode srcUomCode) const;
    const wchar_t* GetRevisionLevel (void) const;
    std::wstring GetFailMessage (void) const;
    //=========================================================================
    // Some general access functions:
    // Step through an EPSG table one record at a time.
    bool GetFieldByIndex (std::wstring& fieldData,EcsEpsgTable tableId,EcsEpsgField fieldId,
                                                                       unsigned recNbr) const;
    bool GetCodeByIndex (TcsEpsgCode& epsgCode,EcsEpsgTable tableId,EcsEpsgField fieldId,
                                                                    unsigned recNbr) const;
    // Get a field from a table which is indexed by an EPSG code.
    bool GetFieldByCode (std::wstring& fieldData,EcsEpsgTable tableId,EcsEpsgField fieldId,
                                                                      const TcsEpsgCode& epsgCode) const;
    bool GetFieldByCode (TcsEpsgCode& result,EcsEpsgTable tableId,EcsEpsgField fieldId,
                                                                  const TcsEpsgCode& epsgCode) const;
    bool GetFieldByCode (double& result,EcsEpsgTable tableId,EcsEpsgField fieldId,
                                                             const TcsEpsgCode& epsgCode) const;
    // Is a record in table indexed by an EPSG code deprecated?
    bool IsDeprecated (EcsEpsgTable tableId,const TcsEpsgCode& epsgCode) const;

    //=========================================================================
    // These functions will work on a constant object and, therefore, are
    // multi-thread safe.
    bool LocateGeographicBase (TcsEpsgCode& geographicBase,EcsCrsType crsType,
                                                           const TcsEpsgCode& datumCode) const;
    TcsEpsgCode LocateOperation (EcsOpType crsType,const TcsEpsgCode& sourceCode,
                                                   const TcsEpsgCode& targetCode,
                                                   long variant = 1L) const;
    bool GetParameterFileName (std::wstring& parameterFileName,const TcsEpsgCode& opCode,
                                                                const TcsEpsgCode& opMethCode,
                                                               const TcsEpsgCode& prmCode) const;
    bool GetParameterValue (double& parameterValue,const TcsEpsgCode& opCode,
                                                   const TcsEpsgCode& opMethCode,
                                                   const TcsEpsgCode& prmCode,
                                                   const TcsEpsgCode& trgUomCode) const;
    bool GetCsMapEllipsoid (struct cs_Eldef_& ellipsoid,const TcsEpsgCode& epsgCode) const;
    bool GetCsMapDatum (struct cs_Dtdef_& datum,struct cs_Eldef_& ellipsoid,const TcsEpsgCode& epsgDtmCode,
                                                                            unsigned variant = 0U) const;
    bool GetCsMapCoordsys (struct cs_Csdef_& coordsys,struct cs_Dtdef_& datum,struct cs_Eldef_& ellipsoid,
                                                                              const TcsEpsgCode& crsEpsgCode) const;
    bool GetCsMapGxdef (struct cs_Gxdef_& gxXform,const TcsEpsgCode& oprEpsgCode) const;
    short DetermineCsMapDatumMethod (const TcsEpsgCode& operationCode,bool& coordFrame) const;
    bool GetCoordsysQuad (short& quad,TcsEpsgCode& horzUom,TcsEpsgCode& vertUom,
                                                           const TcsEpsgCode& crsEpsgCode) const;
    EcsCrsType GetCrsType (const TcsEpsgCode& crsCode) const;
    EcsUomType GetUomFactor (double& uomFactor,const TcsEpsgCode& uomCode) const;
protected:
    //=========================================================================
    // Protected Support Functions
    //
    // These functions provide support for the public member functions of this
    // object.  Implementation of these members can be found in
    // csEpsgSupport.cpp  Selecting these members to be protected is quite
    // /arbitrary.
    bool GetUomToDegrees (double& toDegrees,const TcsEpsgCode& uomCode) const;
    bool GetUomToMeters (double& toMeters,const TcsEpsgCode& uomCode) const;
    bool GetUomToUnity (double& toUnity,const TcsEpsgCode& uomCode) const;
    bool ConvertUnits (double& value,const TcsEpsgCode& trgUomCode,const TcsEpsgCode& srcUomCode) const;
    bool FieldToReal (double& result,const TcsEpsgCode& trgUomCode,const wchar_t* fldData,const TcsEpsgCode& srcUomCode) const;
    bool FieldToDegrees (double& result,const wchar_t* field,const TcsEpsgCode& uomCode) const;
    bool GetReferenceDatum (TcsEpsgCode& dtmEpsgCode,const TcsEpsgCode& crsEpsgCode) const;
    bool GetPrimeMeridian (double& primeMeridian,const TcsEpsgCode& crsEpsgCode) const;
    bool IsPrMerRotation (bool& isPrMerRot,const TcsEpsgCode& oprtnCode) const;
    unsigned LocateParameterValue (const TcsEpsgCode& opCode,const TcsEpsgCode& opMethCode,
                                                             const TcsEpsgCode& prmCode) const;
    bool AddDatumParameterValues (struct cs_Dtdef_& datum,const TcsEpsgCode& operationCode) const;
    bool DetermineRevisionLevel (void);
    bool GetAreaOfUse (double &minLng,
                       double& maxLng,
                       double& minLat,
                       double& maxLat,
                       EcsEpsgObject objType,
                       TcsEpsgCode objCode) const;
private:
    //=========================================================================
    // Private Support Functions
    // These are private as they are called by GetCsMapCoordsys only after all
    // the stuff common to both types is done.  Calling by any other function
    // is likely to cause some real problems.
    bool GeographicCoordsys (struct cs_Csdef_& coordsys,const TcsEpsgCode& crsEpsgCode,
                                                        const TcsEpsgCode& horzUomCode) const;
    bool ProjectedCoordsys (struct cs_Csdef_& coordsys,const TcsEpsgCode& crsEpsgCode,
                                                       const TcsEpsgCode& horzUomCode) const;
    //=========================================================================
    // Private Data Members
    bool Ok;
    EpsgVersion Version;					// Version of the current object content
    std::wstring RevisionLevel;				// The revision level of the EPSG data in this object
    std::wstring DatabaseFolder;			// The folder in which the .CSV files reside.
    std::wstring FailMessage;
    std::map<EcsEpsgTable,TcsEpsgTable*> EpsgTables;
                                            // The individual tables of the EPSG dataset.
};
//newPage//
//=============================================================================
// The following objects were developed in response to the issue of datums and
// geodetic transformations within EPSG.  Model the EPSG structure in a form
// which is somewhat rational and can be understood, used, and maintained by
// us mere mortals.  Implementation of these objects can be found in
// cs_EpsgSupport.cpp.
//
// Within EPSG, a conversion or transformation (usually transformation) may
// have several applicable variations.  Some more precise than others, but
// none the less, all have a certain degree of validity.  Especially with
// respect to datum transformations, there may be several variations which
// will get you from one datum to another.
//
// To complicate matters further, a variant maybe a concatenated operation.
// That is, consist of two or more single operations which must be performed
// in a specific order.  Generally, concatenated operations are defined in the
// Coordinate_Operation Path table.  Often, one or more of these concatenated
// operations are: 1> the null transformation, or 2> a longitude translation
// (i.e. application of a prime meridian).  In either of these two cases,
// they can usually be ignored in the CS-MAP environment.
//
// TcsOpSingle is a definition of a single operation. TcsSingleOp represents
//		an entry in the operation table which is NOT a concatenated entry.
// TcsOpVariant is a std::list of TcsOpSingle objects, often only one.
//		TcsOpVariant will contain more than one TcsOpSingle in the case
//		of a variant which is a concatenated operation.  Each phase of the
//		concatenated operation is represented by its own TcsOpSingle.
// TcsOpsVariants is a std::vector of TcsVariant objects, which is usually
//		sorted by the Accuracy member of the TcsOpVariant object.
//
struct TcsSingleOp
{
    //=========================================================================
    // Static Constants, Variables, and Member Functions
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
public:
    TcsSingleOp (const	TcsEpsgDataSetV6& epsgDB,const TcsEpsgCode& operationCode);
    TcsSingleOp (const TcsSingleOp& source);
    ~TcsSingleOp (void);
    TcsSingleOp& operator= (const TcsSingleOp& rhs);
    //=========================================================================
    // Operator Overrides
    //=========================================================================
    // Public Named Member Functions
    bool IsValid (void) const {return OperationCode.IsValid (); }
    bool IsNull (void) const {return Null; }
    bool IsLngXlation (void) const {return LngXlation; }
    EcsOpType GetType (void) const {return Type; }
    TcsEpsgCode GetSourceCRS (void) const {return SourceCRS; }
    TcsEpsgCode GetTargetCRS (void) const {return TargetCRS; }
    TcsEpsgCode GetOperationCode (void) const {return OperationCode; }
    TcsEpsgCode GetMethodCode (void) const {return OperationMethod; }
    double GetAccuracy (void) const {return Accuracy; }
    std::wstring GetOpName (void) const {return OpName; }
    const wchar_t* GetOpNamePtr (void) const {return OpName.c_str (); }
    //=========================================================================
    // Private Support Member Functions
    bool Classify (const TcsEpsgDataSetV6& epsgDB,const TcsEpsgCode& oprtnCode);
    //=========================================================================
    // Private Data Members
    bool Null;					// true says Null transformation
    bool LngXlation;			// true says Longitude Translation
    EcsOpType Type;				// Conversion or Transformation.
    TcsEpsgCode SourceCRS;		// Somewhat redundant, but convenient
    TcsEpsgCode TargetCRS;		// Somewhat redundant, but convenient
    TcsEpsgCode OperationCode;	// EPSG Operation code of the non-concatenated operation
    TcsEpsgCode OperationMethod;// EPSG Operation Method code for this operation
    double Accuracy;			// Accuracy of this operation per EPSG database
    std::wstring OpName;		// Name of the operation, usually for report generation
};
// TcsOpVariant represents an entry in the operation table which MAY BE
// concatenated.  Typically, it is not concatenated, and the Singles container
// will only carry a single element.  As CsMap does not support concatenated
// operations, it is at this level that an equivalent Datum Definition can
// be generated.  We use a list as the std::list object does not require a
// default constructor, and all such objects should be valid representations
// of a TcsSingleOp.
class TcsOpVariant
{
    //=========================================================================
    // Static Constants, Variables, and Member Functions
public:
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
    TcsOpVariant (const TcsEpsgDataSetV6& epsgDB,const TcsEpsgCode& operationCode);
    TcsOpVariant (const TcsOpVariant& source);
    ~TcsOpVariant (void);
    TcsOpVariant& operator= (const TcsOpVariant& rhs);
    //=========================================================================
    // Public Named Member Functions
    bool IsValid (void) const;
    unsigned GetOperationCount (void) const;
    EcsOpType GetType (void) const {return Type; }
    unsigned GetVariantNbr (void) const {return VariantNbr; }
    double GetAccuracy (void) const;
    const wchar_t* GetVariantNamePtr (void) const {return VariantName.c_str (); }
    TcsEpsgCode GetOpCodeForCsMap (void) const;
private:
    EcsOpType Type;						// May be Concatenated.
    TcsEpsgCode EpsgOpCode;				// OpCode of the primary operation.
    unsigned VariantNbr;				// EPSG variant number.  There is
                                        // no requirement about how variants
                                        // are numbered, only that they should be
                                        // (but aren't always) unique.
    std::wstring Version;				// Version from EPSG operation table
    double Accuracy;					// Accuracy from the EPSG Operation table.
    std::wstring VariantName;			// Operation name, use in report generation.
    std::list<TcsSingleOp> Singles;		// container of the individual components of
                                        // a concatenated operation.   Order is the
                                        // order in which the operations are to be
                                        // performed, per EPSG Path table.
};
// TcsOpVariants is a collection of TcsOpVariant's.  Quite often, there is
// only one TcsOpVariant; but the exceptional cases need to be handled.
class TcsOpVariants
{
    //=========================================================================
    // Static Constants, Variables, and Member Functions
public:
    //=========================================================================
    // Construction  /  Destruction  /  Assignment
    TcsOpVariants (const TcsEpsgDataSetV6& epsgDB,const TcsEpsgCode& sourceCRS,
                                                  const TcsEpsgCode& targetCRS);
    TcsOpVariants (const TcsOpVariants& source);
    ~TcsOpVariants (void);
    TcsOpVariants& operator= (const TcsOpVariants& rhs);
    //=========================================================================
    // Public Named Member Functions
    unsigned GetVariantCount (void) const {return static_cast<unsigned>(Variants.size ()); }
    TcsEpsgCode GetSourceCRS (void) const {return SourceCrs; }
    TcsEpsgCode GetTargetCRS (void) const {return TargetCrs; }
    bool GetVariant (TcsOpVariant& variant,unsigned index) const;
    const TcsOpVariant* GetVariantPtr (unsigned index) const;
    TcsEpsgCode GetOprtnCodeByVariant (unsigned variant) const;
    TcsEpsgCode GetBestOprtnCode (const TcsEpsgDataSetV6& epsgDB) const;
private:
    TcsEpsgCode SourceCrs;				// EPSG Code of the source CRS
    TcsEpsgCode TargetCrs;				// EPSG code of the target CRS
    std::vector<TcsOpVariant> Variants;	// Collection of variants.  Usually
                                        // only one, but there can be as many
                                        // as 100.
};

#endif				//  __CS_EPSG_STUFF__H
