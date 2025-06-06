/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <config_features.h>

#include <interpre.hxx>

#include <sal/log.hxx>
#include <o3tl/safeint.hxx>
#include <rtl/math.hxx>
#include <sfx2/app.hxx>
#include <sfx2/objsh.hxx>
#include <basic/sbmeth.hxx>
#include <basic/sbmod.hxx>
#include <basic/sbstar.hxx>
#include <basic/sbx.hxx>
#include <basic/sbxobj.hxx>
#include <basic/sbuno.hxx>
#include <osl/thread.h>
#include <svl/numformat.hxx>
#include <svl/zforlist.hxx>
#include <svl/sharedstringpool.hxx>
#include <unotools/charclass.hxx>
#include <stdlib.h>
#include <string.h>

#include <com/sun/star/table/XCellRange.hpp>
#include <com/sun/star/script/XInvocation.hpp>
#include <com/sun/star/sheet/XSheetCellRange.hpp>

#include <global.hxx>
#include <dbdata.hxx>
#include <formulacell.hxx>
#include <callform.hxx>
#include <addincol.hxx>
#include <document.hxx>
#include <dociter.hxx>
#include <docsh.hxx>
#include <docoptio.hxx>
#include <scmatrix.hxx>
#include <adiasync.hxx>
#include <cellsuno.hxx>
#include <optuno.hxx>
#include <rangeseq.hxx>
#include <addinlis.hxx>
#include <jumpmatrix.hxx>
#include <parclass.hxx>
#include <externalrefmgr.hxx>
#include <formula/FormulaCompiler.hxx>
#include <macromgr.hxx>
#include <doubleref.hxx>
#include <queryparam.hxx>
#include <tokenarray.hxx>
#include <compiler.hxx>

#include <map>
#include <algorithm>
#include <basic/basmgr.hxx>
#include <vbahelper/vbaaccesshelper.hxx>
#include <memory>

using namespace com::sun::star;
using namespace formula;
using ::std::unique_ptr;

#define ADDIN_MAXSTRLEN 256

thread_local std::unique_ptr<ScTokenStack> ScInterpreter::pGlobalStack;
thread_local bool ScInterpreter::bGlobalStackInUse = false;

// document access functions

void ScInterpreter::ReplaceCell( ScAddress& rPos )
{
    size_t ListSize = mrDoc.m_TableOpList.size();
    for ( size_t i = 0; i < ListSize; ++i )
    {
        ScInterpreterTableOpParams *const pTOp = mrDoc.m_TableOpList[ i ];
        if ( rPos == pTOp->aOld1 )
        {
            rPos = pTOp->aNew1;
            return ;
        }
        else if ( rPos == pTOp->aOld2 )
        {
            rPos = pTOp->aNew2;
            return ;
        }
    }
}

bool ScInterpreter::IsTableOpInRange( const ScRange& rRange )
{
    if ( rRange.aStart == rRange.aEnd )
        return false;   // not considered to be a range in TableOp sense

    // we can't replace a single cell in a range
    size_t ListSize = mrDoc.m_TableOpList.size();
    for ( size_t i = 0; i < ListSize; ++i )
    {
        ScInterpreterTableOpParams *const pTOp = mrDoc.m_TableOpList[ i ];
        if ( rRange.Contains( pTOp->aOld1 ) )
            return true;
        if ( rRange.Contains( pTOp->aOld2 ) )
            return true;
    }
    return false;
}

sal_uInt32 ScInterpreter::GetCellNumberFormat( const ScAddress& rPos, const ScRefCellValue& rCell )
{
    sal_uInt32 nFormat;
    FormulaError nErr;
    if (rCell.isEmpty())
    {
        nFormat = mrDoc.GetNumberFormat( mrContext, rPos );
        nErr = FormulaError::NONE;
    }
    else
    {
        if (rCell.getType() == CELLTYPE_FORMULA)
            nErr = rCell.getFormula()->GetErrCode();
        else
            nErr = FormulaError::NONE;
        nFormat = mrDoc.GetNumberFormat( mrContext, rPos );
    }

    SetError(nErr);
    return nFormat;
}

/// Only ValueCell, formula cells already store the result rounded.
double ScInterpreter::GetValueCellValue( const ScAddress& rPos, double fOrig )
{
    if ( bCalcAsShown && fOrig != 0.0 )
    {
        sal_uInt32 nFormat = mrDoc.GetNumberFormat( mrContext, rPos );
        fOrig = mrDoc.RoundValueAsShown( fOrig, nFormat, &mrContext );
    }
    return fOrig;
}

FormulaError ScInterpreter::GetCellErrCode( const ScRefCellValue& rCell )
{
    return rCell.getType() == CELLTYPE_FORMULA ? rCell.getFormula()->GetErrCode() : FormulaError::NONE;
}

double ScInterpreter::ConvertStringToValue( const OUString& rStr )
{
    FormulaError nError = FormulaError::NONE;
    double fValue = ScGlobal::ConvertStringToValue( rStr, maCalcConfig, nError, mnStringNoValueError,
            mrContext, nCurFmtType);
    if (nError != FormulaError::NONE)
        SetError(nError);
    return fValue;
}

double ScInterpreter::ConvertStringToValue( const OUString& rStr, FormulaError& rError, SvNumFormatType& rCurFmtType )
{
    return ScGlobal::ConvertStringToValue( rStr, maCalcConfig, rError, mnStringNoValueError, mrContext, rCurFmtType);
}

double ScInterpreter::GetCellValue( const ScAddress& rPos, const ScRefCellValue& rCell )
{
    FormulaError nErr = nGlobalError;
    nGlobalError = FormulaError::NONE;
    double nVal = GetCellValueOrZero(rPos, rCell);
    // Propagate previous error, if any; nGlobalError==CellNoValue is not an
    // error here, preserve previous error or non-error.
    if (nErr != FormulaError::NONE || nGlobalError == FormulaError::CellNoValue)
        nGlobalError = nErr;
    return nVal;
}

double ScInterpreter::GetCellValueOrZero( const ScAddress& rPos, const ScRefCellValue& rCell )
{
    double fValue = 0.0;

    CellType eType = rCell.getType();
    switch (eType)
    {
        case CELLTYPE_FORMULA:
        {
            ScFormulaCell* pFCell = rCell.getFormula();
            FormulaError nErr = pFCell->GetErrCode();
            if( nErr == FormulaError::NONE )
            {
                if (pFCell->IsValue())
                {
                    fValue = pFCell->GetValue();
                    mrDoc.GetNumberFormatInfo( mrContext, nCurFmtType, nCurFmtIndex,
                        rPos );
                }
                else
                {
                    fValue = ConvertStringToValue(pFCell->GetString().getString());
                }
            }
            else
            {
                fValue = 0.0;
                SetError(nErr);
            }
        }
        break;
        case CELLTYPE_VALUE:
        {
            fValue = rCell.getDouble();
            nCurFmtIndex = mrDoc.GetNumberFormat( mrContext, rPos );
            nCurFmtType = mrContext.NFGetType(nCurFmtIndex);
            if ( bCalcAsShown && fValue != 0.0 )
                fValue = mrDoc.RoundValueAsShown( fValue, nCurFmtIndex, &mrContext );
        }
        break;
        case  CELLTYPE_STRING:
        case  CELLTYPE_EDIT:
        {
            // SUM(A1:A2) differs from A1+A2. No good. But people insist on
            // it ... #i5658#
            OUString aStr = rCell.getString(&mrDoc);
            fValue = ConvertStringToValue( aStr );
        }
        break;
        case CELLTYPE_NONE:
            fValue = 0.0;       // empty or broadcaster cell
        break;
    }

    return fValue;
}

void ScInterpreter::GetCellString( svl::SharedString& rStr, const ScRefCellValue& rCell )
{
    FormulaError nErr = FormulaError::NONE;

    switch (rCell.getType())
    {
        case CELLTYPE_STRING:
        case CELLTYPE_EDIT:
            rStr = rCell.getSharedString(&mrDoc, mrStrPool);
        break;
        case CELLTYPE_FORMULA:
        {
            ScFormulaCell* pFCell = rCell.getFormula();
            nErr = pFCell->GetErrCode();
            if (pFCell->IsValue())
            {
                rStr = GetStringFromDouble( pFCell->GetValue() );
            }
            else
                rStr = pFCell->GetString();
        }
        break;
        case CELLTYPE_VALUE:
        {
            rStr = GetStringFromDouble( rCell.getDouble() );
        }
        break;
        default:
            rStr = svl::SharedString::getEmptyString();
        break;
    }

    SetError(nErr);
}

bool ScInterpreter::CreateDoubleArr(SCCOL nCol1, SCROW nRow1, SCTAB nTab1,
                            SCCOL nCol2, SCROW nRow2, SCTAB nTab2, sal_uInt8* pCellArr)
{

    // Old Add-Ins are hard limited to sal_uInt16 values.
    static_assert(MAXCOLCOUNT <= SAL_MAX_UINT16 && MAXCOLCOUNT_JUMBO <= SAL_MAX_UINT16,
        "Add check for columns > SAL_MAX_UINT16!");
    if (nRow1 > SAL_MAX_UINT16 || nRow2 > SAL_MAX_UINT16)
        return false;

    sal_uInt16 nCount = 0;
    sal_uInt16* p = reinterpret_cast<sal_uInt16*>(pCellArr);
    *p++ = static_cast<sal_uInt16>(nCol1);
    *p++ = static_cast<sal_uInt16>(nRow1);
    *p++ = static_cast<sal_uInt16>(nTab1);
    *p++ = static_cast<sal_uInt16>(nCol2);
    *p++ = static_cast<sal_uInt16>(nRow2);
    *p++ = static_cast<sal_uInt16>(nTab2);
    sal_uInt16* pCount = p;
    *p++ = 0;
    sal_uInt16 nPos = 14;
    SCTAB nTab = nTab1;
    ScAddress aAdr;
    while (nTab <= nTab2)
    {
        aAdr.SetTab( nTab );
        SCROW nRow = nRow1;
        while (nRow <= nRow2)
        {
            aAdr.SetRow( nRow );
            SCCOL nCol = nCol1;
            while (nCol <= nCol2)
            {
                aAdr.SetCol( nCol );

                ScRefCellValue aCell(mrDoc, aAdr);
                if (!aCell.isEmpty())
                {
                    FormulaError  nErr = FormulaError::NONE;
                    double  nVal = 0.0;
                    bool    bOk = true;
                    switch (aCell.getType())
                    {
                        case CELLTYPE_VALUE :
                            nVal = GetValueCellValue(aAdr, aCell.getDouble());
                            break;
                        case CELLTYPE_FORMULA :
                            if (aCell.getFormula()->IsValue())
                            {
                                nErr = aCell.getFormula()->GetErrCode();
                                nVal = aCell.getFormula()->GetValue();
                            }
                            else
                                bOk = false;
                            break;
                        default :
                            bOk = false;
                            break;
                    }
                    if (bOk)
                    {
                        if ((nPos + (4 * sizeof(sal_uInt16)) + sizeof(double)) > MAXARRSIZE)
                            return false;
                        *p++ = static_cast<sal_uInt16>(nCol);
                        *p++ = static_cast<sal_uInt16>(nRow);
                        *p++ = static_cast<sal_uInt16>(nTab);
                        *p++ = static_cast<sal_uInt16>(nErr);
                        memcpy( p, &nVal, sizeof(double));
                        nPos += 8 + sizeof(double);
                        p = reinterpret_cast<sal_uInt16*>( pCellArr + nPos );
                        nCount++;
                    }
                }
                nCol++;
            }
            nRow++;
        }
        nTab++;
    }
    *pCount = nCount;
    return true;
}

bool ScInterpreter::CreateStringArr(SCCOL nCol1, SCROW nRow1, SCTAB nTab1,
                                    SCCOL nCol2, SCROW nRow2, SCTAB nTab2,
                                    sal_uInt8* pCellArr)
{

    // Old Add-Ins are hard limited to sal_uInt16 values.
    static_assert(MAXCOLCOUNT <= SAL_MAX_UINT16 && MAXCOLCOUNT_JUMBO <= SAL_MAX_UINT16,
        "Add check for columns > SAL_MAX_UINT16!");
    if (nRow1 > SAL_MAX_UINT16 || nRow2 > SAL_MAX_UINT16)
        return false;

    sal_uInt16 nCount = 0;
    sal_uInt16* p = reinterpret_cast<sal_uInt16*>(pCellArr);
    *p++ = static_cast<sal_uInt16>(nCol1);
    *p++ = static_cast<sal_uInt16>(nRow1);
    *p++ = static_cast<sal_uInt16>(nTab1);
    *p++ = static_cast<sal_uInt16>(nCol2);
    *p++ = static_cast<sal_uInt16>(nRow2);
    *p++ = static_cast<sal_uInt16>(nTab2);
    sal_uInt16* pCount = p;
    *p++ = 0;
    sal_uInt16 nPos = 14;
    SCTAB nTab = nTab1;
    while (nTab <= nTab2)
    {
        SCROW nRow = nRow1;
        while (nRow <= nRow2)
        {
            SCCOL nCol = nCol1;
            while (nCol <= nCol2)
            {
                ScRefCellValue aCell(mrDoc, ScAddress(nCol, nRow, nTab));
                if (!aCell.isEmpty())
                {
                    OUString  aStr;
                    FormulaError  nErr = FormulaError::NONE;
                    bool    bOk = true;
                    switch (aCell.getType())
                    {
                        case CELLTYPE_STRING:
                        case CELLTYPE_EDIT:
                            aStr = aCell.getString(&mrDoc);
                            break;
                        case CELLTYPE_FORMULA:
                            if (!aCell.getFormula()->IsValue())
                            {
                                nErr = aCell.getFormula()->GetErrCode();
                                aStr = aCell.getFormula()->GetString().getString();
                            }
                            else
                                bOk = false;
                            break;
                        default :
                            bOk = false;
                            break;
                    }
                    if (bOk)
                    {
                        OString aTmp(OUStringToOString(aStr,
                            osl_getThreadTextEncoding()));
                        // Old Add-Ins are limited to sal_uInt16 string
                        // lengths, and room for pad byte check.
                        if ( aTmp.getLength() > SAL_MAX_UINT16 - 2 )
                            return false;
                        // Append a 0-pad-byte if string length is odd
                        // MUST be sal_uInt16
                        sal_uInt16 nStrLen = static_cast<sal_uInt16>(aTmp.getLength());
                        sal_uInt16 nLen = ( nStrLen + 2 ) & ~1;

                        if ((static_cast<sal_uLong>(nPos) + (5 * sizeof(sal_uInt16)) + nLen) > MAXARRSIZE)
                            return false;
                        *p++ = static_cast<sal_uInt16>(nCol);
                        *p++ = static_cast<sal_uInt16>(nRow);
                        *p++ = static_cast<sal_uInt16>(nTab);
                        *p++ = static_cast<sal_uInt16>(nErr);
                        *p++ = nLen;
                        memcpy( p, aTmp.getStr(), nStrLen + 1);
                        nPos += 10 + nStrLen + 1;
                        sal_uInt8* q = pCellArr + nPos;
                        if( (nStrLen & 1) == 0 )
                        {
                            *q++ = 0;
                            nPos++;
                        }
                        p = reinterpret_cast<sal_uInt16*>( pCellArr + nPos );
                        nCount++;
                    }
                }
                nCol++;
            }
            nRow++;
        }
        nTab++;
    }
    *pCount = nCount;
    return true;
}

bool ScInterpreter::CreateCellArr(SCCOL nCol1, SCROW nRow1, SCTAB nTab1,
                                  SCCOL nCol2, SCROW nRow2, SCTAB nTab2,
                                  sal_uInt8* pCellArr)
{

    // Old Add-Ins are hard limited to sal_uInt16 values.
    static_assert(MAXCOLCOUNT <= SAL_MAX_UINT16 && MAXCOLCOUNT_JUMBO <= SAL_MAX_UINT16,
        "Add check for columns > SAL_MAX_UINT16!");
    if (nRow1 > SAL_MAX_UINT16 || nRow2 > SAL_MAX_UINT16)
        return false;

    sal_uInt16 nCount = 0;
    sal_uInt16* p = reinterpret_cast<sal_uInt16*>(pCellArr);
    *p++ = static_cast<sal_uInt16>(nCol1);
    *p++ = static_cast<sal_uInt16>(nRow1);
    *p++ = static_cast<sal_uInt16>(nTab1);
    *p++ = static_cast<sal_uInt16>(nCol2);
    *p++ = static_cast<sal_uInt16>(nRow2);
    *p++ = static_cast<sal_uInt16>(nTab2);
    sal_uInt16* pCount = p;
    *p++ = 0;
    sal_uInt16 nPos = 14;
    SCTAB nTab = nTab1;
    ScAddress aAdr;
    while (nTab <= nTab2)
    {
        aAdr.SetTab( nTab );
        SCROW nRow = nRow1;
        while (nRow <= nRow2)
        {
            aAdr.SetRow( nRow );
            SCCOL nCol = nCol1;
            while (nCol <= nCol2)
            {
                aAdr.SetCol( nCol );
                ScRefCellValue aCell(mrDoc, aAdr);
                if (!aCell.isEmpty())
                {
                    FormulaError  nErr = FormulaError::NONE;
                    sal_uInt16  nType = 0; // 0 = number; 1 = string
                    double  nVal = 0.0;
                    OUString  aStr;
                    bool    bOk = true;
                    switch (aCell.getType())
                    {
                        case CELLTYPE_STRING :
                        case CELLTYPE_EDIT :
                            aStr = aCell.getString(&mrDoc);
                            nType = 1;
                            break;
                        case CELLTYPE_VALUE :
                            nVal = GetValueCellValue(aAdr, aCell.getDouble());
                            break;
                        case CELLTYPE_FORMULA :
                            nErr = aCell.getFormula()->GetErrCode();
                            if (aCell.getFormula()->IsValue())
                                nVal = aCell.getFormula()->GetValue();
                            else
                                aStr = aCell.getFormula()->GetString().getString();
                            break;
                        default :
                            bOk = false;
                            break;
                    }
                    if (bOk)
                    {
                        if ((nPos + (5 * sizeof(sal_uInt16))) > MAXARRSIZE)
                            return false;
                        *p++ = static_cast<sal_uInt16>(nCol);
                        *p++ = static_cast<sal_uInt16>(nRow);
                        *p++ = static_cast<sal_uInt16>(nTab);
                        *p++ = static_cast<sal_uInt16>(nErr);
                        *p++ = nType;
                        nPos += 10;
                        if (nType == 0)
                        {
                            if ((nPos + sizeof(double)) > MAXARRSIZE)
                                return false;
                            memcpy( p, &nVal, sizeof(double));
                            nPos += sizeof(double);
                        }
                        else
                        {
                            OString aTmp(OUStringToOString(aStr,
                                osl_getThreadTextEncoding()));
                            // Old Add-Ins are limited to sal_uInt16 string
                            // lengths, and room for pad byte check.
                            if ( aTmp.getLength() > SAL_MAX_UINT16 - 2 )
                                return false;
                            // Append a 0-pad-byte if string length is odd
                            // MUST be sal_uInt16
                            sal_uInt16 nStrLen = static_cast<sal_uInt16>(aTmp.getLength());
                            sal_uInt16 nLen = ( nStrLen + 2 ) & ~1;
                            if ( (static_cast<sal_uLong>(nPos) + 2 + nLen) > MAXARRSIZE)
                                return false;
                            *p++ = nLen;
                            memcpy( p, aTmp.getStr(), nStrLen + 1);
                            nPos += 2 + nStrLen + 1;
                            sal_uInt8* q = pCellArr + nPos;
                            if( (nStrLen & 1) == 0 )
                            {
                                *q++ = 0;
                                nPos++;
                            }
                        }
                        nCount++;
                        p = reinterpret_cast<sal_uInt16*>( pCellArr + nPos );
                    }
                }
                nCol++;
            }
            nRow++;
        }
        nTab++;
    }
    *pCount = nCount;
    return true;
}

// Stack operations

// Also releases a TempToken if appropriate.

void ScInterpreter::PushWithoutError( const FormulaToken& r )
{
    if ( sp >= MAXSTACK )
        SetError( FormulaError::StackOverflow );
    else
    {
        r.IncRef();
        if( sp >= maxsp )
            maxsp = sp + 1;
        else
            pStack[ sp ]->DecRef();
        pStack[ sp ] = &r;
        ++sp;
    }
}

void ScInterpreter::Push( const FormulaToken& r )
{
    if ( sp >= MAXSTACK )
        SetError( FormulaError::StackOverflow );
    else
    {
        if (nGlobalError != FormulaError::NONE)
        {
            if (r.GetType() == svError)
                PushWithoutError( r);
            else
                PushTempTokenWithoutError( new FormulaErrorToken( nGlobalError));
        }
        else
            PushWithoutError( r);
    }
}

void ScInterpreter::PushTempToken( FormulaToken* p )
{
    if ( sp >= MAXSTACK )
    {
        SetError( FormulaError::StackOverflow );
        // p may be a dangling pointer hereafter!
        p->DeleteIfZeroRef();
    }
    else
    {
        if (nGlobalError != FormulaError::NONE)
        {
            if (p->GetType() == svError)
            {
                p->SetError( nGlobalError);
                PushTempTokenWithoutError( p);
            }
            else
            {
                // p may be a dangling pointer hereafter!
                p->DeleteIfZeroRef();
                PushTempTokenWithoutError( new FormulaErrorToken( nGlobalError));
            }
        }
        else
            PushTempTokenWithoutError( p);
    }
}

void ScInterpreter::PushTempTokenWithoutError( const FormulaToken* p )
{
    p->IncRef();
    if ( sp >= MAXSTACK )
    {
        SetError( FormulaError::StackOverflow );
        // p may be a dangling pointer hereafter!
        p->DecRef();
    }
    else
    {
        if( sp >= maxsp )
            maxsp = sp + 1;
        else
            pStack[ sp ]->DecRef();
        pStack[ sp ] = p;
        ++sp;
    }
}

void ScInterpreter::PushTokenRef( const formula::FormulaConstTokenRef& x )
{
    if ( sp >= MAXSTACK )
    {
        SetError( FormulaError::StackOverflow );
    }
    else
    {
        if (nGlobalError != FormulaError::NONE)
        {
            if (x->GetType() == svError && x->GetError() == nGlobalError)
                PushTempTokenWithoutError( x.get());
            else
                PushTempTokenWithoutError( new FormulaErrorToken( nGlobalError));
        }
        else
            PushTempTokenWithoutError( x.get());
    }
}

void ScInterpreter::PushCellResultToken( bool bDisplayEmptyAsString,
        const ScAddress & rAddress, SvNumFormatType * pRetTypeExpr, sal_uInt32 * pRetIndexExpr, bool bFinalResult )
{
    ScRefCellValue aCell(mrDoc, rAddress);
    if (aCell.hasEmptyValue())
    {
        bool bInherited = (aCell.getType() == CELLTYPE_FORMULA);
        if (pRetTypeExpr && pRetIndexExpr)
            mrDoc.GetNumberFormatInfo(mrContext, *pRetTypeExpr, *pRetIndexExpr, rAddress);
        PushTempToken( new ScEmptyCellToken( bInherited, bDisplayEmptyAsString));
        return;
    }

    FormulaError nErr = FormulaError::NONE;
    if (aCell.getType() == CELLTYPE_FORMULA)
        nErr = aCell.getFormula()->GetErrCode();

    if (nErr != FormulaError::NONE)
    {
        PushError( nErr);
        if (pRetTypeExpr)
            *pRetTypeExpr = SvNumFormatType::UNDEFINED;
        if (pRetIndexExpr)
            *pRetIndexExpr = 0;
    }
    else if (aCell.hasString())
    {
        svl::SharedString aRes;
        GetCellString( aRes, aCell);
        PushString( aRes);
        if (pRetTypeExpr)
            *pRetTypeExpr = SvNumFormatType::TEXT;
        if (pRetIndexExpr)
            *pRetIndexExpr = 0;
    }
    else
    {
        double fVal = GetCellValue(rAddress, aCell);
        if (bFinalResult)
        {
            TreatDoubleError( fVal);
            if (!IfErrorPushError())
                PushTempTokenWithoutError( CreateFormulaDoubleToken( fVal));
        }
        else
        {
            PushDouble( fVal);
        }
        if (pRetTypeExpr)
            *pRetTypeExpr = nCurFmtType;
        if (pRetIndexExpr)
            *pRetIndexExpr = nCurFmtIndex;
    }
}

// Simply throw away TOS.

void ScInterpreter::Pop()
{
    if( sp )
        sp--;
    else
        SetError(FormulaError::UnknownStackVariable);
}

// Simply throw away TOS and set error code, used with ocIsError et al.

void ScInterpreter::PopError()
{
    if( sp )
    {
        sp--;
        if (pStack[sp]->GetType() == svError)
            nGlobalError = pStack[sp]->GetError();
    }
    else
        SetError(FormulaError::UnknownStackVariable);
}

FormulaConstTokenRef ScInterpreter::PopToken()
{
    if (sp)
    {
        sp--;
        const FormulaToken* p = pStack[ sp ];
        if (p->GetType() == svError)
            nGlobalError = p->GetError();
        return p;
    }
    else
        SetError(FormulaError::UnknownStackVariable);
    return nullptr;
}

double ScInterpreter::PopDouble()
{
    nCurFmtType = SvNumFormatType::NUMBER;
    nCurFmtIndex = 0;
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svDouble:
                {
                    SvNumFormatType nType = static_cast<SvNumFormatType>(p->GetDoubleType());
                    if (nType != SvNumFormatType::ALL && nType != SvNumFormatType::UNDEFINED)
                        nCurFmtType = nType;
                    return p->GetDouble();
                }
            case svEmptyCell:
            case svMissing:
                return 0.0;
            default:
                SetError( FormulaError::IllegalArgument);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
    return 0.0;
}

const svl::SharedString & ScInterpreter::PopString()
{
    nCurFmtType = SvNumFormatType::TEXT;
    nCurFmtIndex = 0;
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svString:
            case svStringName:
                return p->GetString();
            case svEmptyCell:
            case svMissing:
                return svl::SharedString::getEmptyString();
            default:
                SetError( FormulaError::IllegalArgument);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);

    return svl::SharedString::getEmptyString();
}

void ScInterpreter::ValidateRef( const ScSingleRefData & rRef )
{
    SCCOL nCol;
    SCROW nRow;
    SCTAB nTab;
    SingleRefToVars( rRef, nCol, nRow, nTab);
}

void ScInterpreter::ValidateRef( const ScComplexRefData & rRef )
{
    ValidateRef( rRef.Ref1);
    ValidateRef( rRef.Ref2);
}

void ScInterpreter::ValidateRef( const ScRefList & rRefList )
{
    for (const auto& rRef : rRefList)
    {
        ValidateRef( rRef);
    }
}

void ScInterpreter::SingleRefToVars( const ScSingleRefData & rRef,
        SCCOL & rCol, SCROW & rRow, SCTAB & rTab )
{
    if ( rRef.IsColRel() )
        rCol = aPos.Col() + rRef.Col();
    else
        rCol = rRef.Col();

    if ( rRef.IsRowRel() )
        rRow = aPos.Row() + rRef.Row();
    else
        rRow = rRef.Row();

    if ( rRef.IsTabRel() )
        rTab = aPos.Tab() + rRef.Tab();
    else
        rTab = rRef.Tab();

    if( !mrDoc.ValidCol( rCol) || rRef.IsColDeleted() )
    {
        SetError( FormulaError::NoRef );
        rCol = 0;
    }
    if( !mrDoc.ValidRow( rRow) || rRef.IsRowDeleted() )
    {
        SetError( FormulaError::NoRef );
        rRow = 0;
    }
    if( !ValidTab( rTab, mrDoc.GetTableCount() - 1) || rRef.IsTabDeleted() )
    {
        SetError( FormulaError::NoRef );
        rTab = 0;
    }
}

void ScInterpreter::PopSingleRef(SCCOL& rCol, SCROW &rRow, SCTAB& rTab)
{
    ScAddress aAddr(rCol, rRow, rTab);
    PopSingleRef(aAddr);
    rCol = aAddr.Col();
    rRow = aAddr.Row();
    rTab = aAddr.Tab();
}

void ScInterpreter::PopSingleRef( ScAddress& rAdr )
{
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svSingleRef:
                {
                    const ScSingleRefData* pRefData = p->GetSingleRef();
                    if (pRefData->IsDeleted())
                    {
                        SetError( FormulaError::NoRef);
                        break;
                    }

                    SCCOL nCol;
                    SCROW nRow;
                    SCTAB nTab;
                    SingleRefToVars( *pRefData, nCol, nRow, nTab);
                    rAdr.Set( nCol, nRow, nTab );
                    if (!mrDoc.m_TableOpList.empty())
                        ReplaceCell( rAdr );
                }
                break;
            default:
                SetError( FormulaError::IllegalParameter);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
}

void ScInterpreter::DoubleRefToVars( const formula::FormulaToken* p,
        SCCOL& rCol1, SCROW &rRow1, SCTAB& rTab1,
        SCCOL& rCol2, SCROW &rRow2, SCTAB& rTab2 )
{
    const ScComplexRefData& rCRef = *p->GetDoubleRef();
    SingleRefToVars( rCRef.Ref1, rCol1, rRow1, rTab1);
    SingleRefToVars( rCRef.Ref2, rCol2, rRow2, rTab2);
    PutInOrder(rCol1, rCol2);
    PutInOrder(rRow1, rRow2);
    PutInOrder(rTab1, rTab2);
    if (!mrDoc.m_TableOpList.empty())
    {
        ScRange aRange( rCol1, rRow1, rTab1, rCol2, rRow2, rTab2 );
        if ( IsTableOpInRange( aRange ) )
            SetError( FormulaError::IllegalParameter );
    }
}

ScDBRangeBase* ScInterpreter::PopDBDoubleRef()
{
    StackVar eType = GetStackType();
    switch (eType)
    {
        case svUnknown:
            SetError(FormulaError::UnknownStackVariable);
        break;
        case svError:
            PopError();
        break;
        case svDoubleRef:
        {
            SCCOL nCol1, nCol2;
            SCROW nRow1, nRow2;
            SCTAB nTab1, nTab2;
            PopDoubleRef(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
            if (nGlobalError != FormulaError::NONE)
                break;
            return new ScDBInternalRange(&mrDoc,
                ScRange(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2));
        }
        case svMatrix:
        case svExternalDoubleRef:
        {
            ScMatrixRef pMat;
            if (eType == svMatrix)
                pMat = PopMatrix();
            else
                PopExternalDoubleRef(pMat);
            if (nGlobalError != FormulaError::NONE)
                break;
            return new ScDBExternalRange(&mrDoc, std::move(pMat));
        }
        default:
            SetError( FormulaError::IllegalParameter);
    }

    return nullptr;
}

void ScInterpreter::PopDoubleRef(SCCOL& rCol1, SCROW &rRow1, SCTAB& rTab1,
                                 SCCOL& rCol2, SCROW &rRow2, SCTAB& rTab2)
{
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svDoubleRef:
                DoubleRefToVars( p, rCol1, rRow1, rTab1, rCol2, rRow2, rTab2);
                break;
            default:
                SetError( FormulaError::IllegalParameter);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
}

void ScInterpreter::DoubleRefToRange( const ScComplexRefData & rCRef,
        ScRange & rRange, bool bDontCheckForTableOp )
{
    SCCOL nCol;
    SCROW nRow;
    SCTAB nTab;
    SingleRefToVars( rCRef.Ref1, nCol, nRow, nTab);
    rRange.aStart.Set( nCol, nRow, nTab );
    SingleRefToVars( rCRef.Ref2, nCol, nRow, nTab);
    rRange.aEnd.Set( nCol, nRow, nTab );
    rRange.PutInOrder();
    if (!mrDoc.m_TableOpList.empty() && !bDontCheckForTableOp)
    {
        if ( IsTableOpInRange( rRange ) )
            SetError( FormulaError::IllegalParameter );
    }
}

void ScInterpreter::PopDoubleRef( ScRange & rRange, short & rParam, size_t & rRefInList )
{
    if (sp)
    {
        const formula::FormulaToken* pToken = pStack[ sp-1 ];
        switch (pToken->GetType())
        {
            case svError:
                nGlobalError = pToken->GetError();
                break;
            case svDoubleRef:
            {
                --sp;
                const ScComplexRefData* pRefData = pToken->GetDoubleRef();
                if (pRefData->IsDeleted())
                {
                    SetError( FormulaError::NoRef);
                    break;
                }
                DoubleRefToRange( *pRefData, rRange);
                break;
            }
            case svRefList:
                {
                    const ScRefList* pList = pToken->GetRefList();
                    if (rRefInList < pList->size())
                    {
                        DoubleRefToRange( (*pList)[rRefInList], rRange);
                        if (++rRefInList < pList->size())
                            ++rParam;
                        else
                        {
                            --sp;
                            rRefInList = 0;
                        }
                    }
                    else
                    {
                        --sp;
                        rRefInList = 0;
                        SetError( FormulaError::IllegalParameter);
                    }
                }
                break;
            default:
                SetError( FormulaError::IllegalParameter);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
}

void ScInterpreter::PopDoubleRef( ScRange& rRange, bool bDontCheckForTableOp )
{
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svDoubleRef:
                DoubleRefToRange( *p->GetDoubleRef(), rRange, bDontCheckForTableOp);
                break;
            default:
                SetError( FormulaError::IllegalParameter);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
}

const ScComplexRefData* ScInterpreter::GetStackDoubleRef(size_t rRefInList)
{
    if( sp )
    {
        const FormulaToken* p = pStack[ sp - 1 ];
        switch (p->GetType())
        {
            case svDoubleRef:
                return p->GetDoubleRef();
            case svRefList:
            {
                const ScRefList* pList = p->GetRefList();
                if (rRefInList < pList->size())
                    return &(*pList)[rRefInList];
                break;
            }
            default:
                break;
        }
    }
    return nullptr;
}

void ScInterpreter::PopExternalSingleRef(sal_uInt16& rFileId, OUString& rTabName, ScSingleRefData& rRef)
{
    if (!sp)
    {
        SetError(FormulaError::UnknownStackVariable);
        return;
    }

    --sp;
    const FormulaToken* p = pStack[sp];
    StackVar eType = p->GetType();

    if (eType == svError)
    {
        nGlobalError = p->GetError();
        return;
    }

    if (eType != svExternalSingleRef)
    {
        SetError( FormulaError::IllegalParameter);
        return;
    }

    rFileId = p->GetIndex();
    rTabName = p->GetString().getString();
    rRef = *p->GetSingleRef();
}

void ScInterpreter::PopExternalSingleRef(ScExternalRefCache::TokenRef& rToken, ScExternalRefCache::CellFormat* pFmt)
{
    sal_uInt16 nFileId;
    OUString aTabName;
    ScSingleRefData aData;
    PopExternalSingleRef(nFileId, aTabName, aData, rToken, pFmt);
}

void ScInterpreter::PopExternalSingleRef(
    sal_uInt16& rFileId, OUString& rTabName, ScSingleRefData& rRef,
    ScExternalRefCache::TokenRef& rToken, ScExternalRefCache::CellFormat* pFmt)
{
    PopExternalSingleRef(rFileId, rTabName, rRef);
    if (nGlobalError != FormulaError::NONE)
        return;

    ScExternalRefManager* pRefMgr = mrDoc.GetExternalRefManager();
    const OUString* pFile = pRefMgr->getExternalFileName(rFileId);
    if (!pFile)
    {
        SetError(FormulaError::NoName);
        return;
    }

    if (rRef.IsTabRel())
    {
        OSL_FAIL("ScCompiler::GetToken: external single reference must have an absolute table reference!");
        SetError(FormulaError::NoRef);
        return;
    }

    ScAddress aAddr = rRef.toAbs(mrDoc, aPos);
    ScExternalRefCache::CellFormat aFmt;
    ScExternalRefCache::TokenRef xNew = pRefMgr->getSingleRefToken(
        rFileId, rTabName, aAddr, &aPos, nullptr, &aFmt);

    if (!xNew)
    {
        SetError(FormulaError::NoRef);
        return;
    }

    if (xNew->GetType() == svError)
        SetError( xNew->GetError());

    rToken = std::move(xNew);
    if (pFmt)
        *pFmt = aFmt;
}

void ScInterpreter::PopExternalDoubleRef(sal_uInt16& rFileId, OUString& rTabName, ScComplexRefData& rRef)
{
    if (!sp)
    {
        SetError(FormulaError::UnknownStackVariable);
        return;
    }

    --sp;
    const FormulaToken* p = pStack[sp];
    StackVar eType = p->GetType();

    if (eType == svError)
    {
        nGlobalError = p->GetError();
        return;
    }

    if (eType != svExternalDoubleRef)
    {
        SetError( FormulaError::IllegalParameter);
        return;
    }

    rFileId = p->GetIndex();
    rTabName = p->GetString().getString();
    rRef = *p->GetDoubleRef();
}

void ScInterpreter::PopExternalDoubleRef(ScExternalRefCache::TokenArrayRef& rArray)
{
    sal_uInt16 nFileId;
    OUString aTabName;
    ScComplexRefData aData;
    PopExternalDoubleRef(nFileId, aTabName, aData);
    if (nGlobalError != FormulaError::NONE)
        return;

    GetExternalDoubleRef(nFileId, aTabName, aData, rArray);
    if (nGlobalError != FormulaError::NONE)
        return;
}

void ScInterpreter::PopExternalDoubleRef(ScMatrixRef& rMat)
{
    ScExternalRefCache::TokenArrayRef pArray;
    PopExternalDoubleRef(pArray);
    if (nGlobalError != FormulaError::NONE)
        return;

    // For now, we only support single range data for external
    // references, which means the array should only contain a
    // single matrix token.
    formula::FormulaToken* p = pArray->FirstToken();
    if (!p || p->GetType() != svMatrix)
        SetError( FormulaError::IllegalParameter);
    else
    {
        rMat = p->GetMatrix();
        if (!rMat)
            SetError( FormulaError::UnknownVariable);
    }
}

void ScInterpreter::GetExternalDoubleRef(
    sal_uInt16 nFileId, const OUString& rTabName, const ScComplexRefData& rData, ScExternalRefCache::TokenArrayRef& rArray)
{
    ScExternalRefManager* pRefMgr = mrDoc.GetExternalRefManager();
    const OUString* pFile = pRefMgr->getExternalFileName(nFileId);
    if (!pFile)
    {
        SetError(FormulaError::NoName);
        return;
    }
    if (rData.Ref1.IsTabRel() || rData.Ref2.IsTabRel())
    {
        OSL_FAIL("ScCompiler::GetToken: external double reference must have an absolute table reference!");
        SetError(FormulaError::NoRef);
        return;
    }

    ScComplexRefData aData(rData);
    ScRange aRange = aData.toAbs(mrDoc, aPos);
    if (!mrDoc.ValidColRow(aRange.aStart.Col(), aRange.aStart.Row()) || !mrDoc.ValidColRow(aRange.aEnd.Col(), aRange.aEnd.Row()))
    {
        SetError(FormulaError::NoRef);
        return;
    }

    ScExternalRefCache::TokenArrayRef pArray = pRefMgr->getDoubleRefTokens(
        nFileId, rTabName, aRange, &aPos);

    if (!pArray)
    {
        SetError(FormulaError::IllegalArgument);
        return;
    }

    formula::FormulaTokenArrayPlainIterator aIter(*pArray);
    formula::FormulaToken* pToken = aIter.First();
    assert(pToken);
    if (pToken->GetType() == svError)
    {
        SetError( pToken->GetError());
        return;
    }
    if (pToken->GetType() != svMatrix)
    {
        SetError(FormulaError::IllegalArgument);
        return;
    }

    if (aIter.Next())
    {
        // Can't handle more than one matrix per parameter.
        SetError( FormulaError::IllegalArgument);
        return;
    }

    rArray = std::move(pArray);
}

bool ScInterpreter::PopDoubleRefOrSingleRef( ScAddress& rAdr )
{
    switch ( GetStackType() )
    {
        case svDoubleRef :
        {
            ScRange aRange;
            PopDoubleRef( aRange, true );
            return DoubleRefToPosSingleRef( aRange, rAdr );
        }
        case svSingleRef :
        {
            PopSingleRef( rAdr );
            return true;
        }
        default:
            PopError();
            SetError( FormulaError::NoRef );
    }
    return false;
}

void ScInterpreter::PopDoubleRefPushMatrix()
{
    if ( GetStackType() == svDoubleRef )
    {
        ScMatrixRef pMat = GetMatrix();
        if ( pMat )
            PushMatrix( pMat );
        else
            PushIllegalParameter();
    }
    else
        SetError( FormulaError::NoRef );
}

void ScInterpreter::PopRefListPushMatrixOrRef()
{
    if ( GetStackType() == svRefList )
    {
        FormulaConstTokenRef xTok = pStack[sp-1];
        const std::vector<ScComplexRefData>* pv = xTok->GetRefList();
        if (pv)
        {
            const size_t nEntries = pv->size();
            if (nEntries == 1)
            {
                --sp;
                PushTempTokenWithoutError( new ScDoubleRefToken( mrDoc.GetSheetLimits(), (*pv)[0] ));
            }
            else if (bMatrixFormula)
            {
                // Only single cells can be stuffed into a column vector.
                // XXX NOTE: Excel doesn't do this but returns #VALUE! instead.
                // Though there's no compelling reason not to...
                for (const auto & rRef : *pv)
                {
                    if (rRef.Ref1 != rRef.Ref2)
                        return;
                }
                ScMatrixRef xMat = GetNewMat( 1, nEntries, true);   // init empty
                if (!xMat)
                    return;
                for (size_t i=0; i < nEntries; ++i)
                {
                    SCCOL nCol; SCROW nRow; SCTAB nTab;
                    SingleRefToVars( (*pv)[i].Ref1, nCol, nRow, nTab);
                    if (nGlobalError == FormulaError::NONE)
                    {
                        ScAddress aAdr( nCol, nRow, nTab);
                        ScRefCellValue aCell(mrDoc, aAdr);
                        if (aCell.hasError())
                            xMat->PutError( aCell.getFormula()->GetErrCode(), 0, i);
                        else if (aCell.hasEmptyValue())
                            xMat->PutEmpty( 0, i);
                        else if (aCell.hasString())
                            xMat->PutString( mrStrPool.intern( aCell.getString(&mrDoc)), 0, i);
                        else
                            xMat->PutDouble( aCell.getValue(), 0, i);
                    }
                    else
                    {
                        xMat->PutError( nGlobalError, 0, i);
                        nGlobalError = FormulaError::NONE;
                    }
                }
                --sp;
                PushMatrix( xMat);
            }
        }
        // else: keep token on stack, something will handle the error
    }
    else
        SetError( FormulaError::NoRef );
}

void ScInterpreter::ConvertMatrixJumpConditionToMatrix()
{
    StackVar eStackType = GetStackType();
    if (eStackType == svUnknown)
        return;     // can't do anything, some caller will catch that
    if (eStackType == svMatrix)
        return;     // already matrix, nothing to do

    if (eStackType != svDoubleRef && GetStackType(2) != svJumpMatrix)
        return;     // always convert svDoubleRef, others only in JumpMatrix context

    GetTokenMatrixMap();    // make sure it exists, create if not.
    ScMatrixRef pMat = GetMatrix();
    if ( pMat )
        PushMatrix( pMat );
    else
        PushIllegalParameter();
}

bool ScInterpreter::ConvertMatrixParameters()
{
    sal_uInt16 nParams = pCur->GetParamCount();
    SAL_WARN_IF( nParams > sp, "sc.core", "ConvertMatrixParameters: stack/param count mismatch:  eOp: "
            << static_cast<int>(pCur->GetOpCode()) << "  sp: " << sp << "  nParams: " << nParams);
    assert(nParams <= sp);
    SCSIZE nJumpCols = 0, nJumpRows = 0;
    for ( sal_uInt16 i=1; i <= nParams && i <= sp; ++i )
    {
        const FormulaToken* p = pStack[ sp - i ];
        if ( p->GetOpCode() != ocPush && p->GetOpCode() != ocMissing)
        {
            assert(!"ConvertMatrixParameters: not a push");
        }
        else
        {
            switch ( p->GetType() )
            {
                case svDouble:
                case svString:
                case svStringName:
                case svSingleRef:
                case svExternalSingleRef:
                case svMissing:
                case svError:
                case svEmptyCell:
                    // nothing to do
                break;
                case svMatrix:
                {
                    if ( ScParameterClassification::GetParameterType( pCur, nParams - i)
                            == formula::ParamClass::Value )
                    {   // only if single value expected
                        ScConstMatrixRef pMat = p->GetMatrix();
                        if ( !pMat )
                            SetError( FormulaError::UnknownVariable);
                        else
                        {
                            SCSIZE nCols, nRows;
                            pMat->GetDimensions( nCols, nRows);
                            if ( nJumpCols < nCols )
                                nJumpCols = nCols;
                            if ( nJumpRows < nRows )
                                nJumpRows = nRows;
                        }
                    }
                }
                break;
                case svDoubleRef:
                {
                    formula::ParamClass eType = ScParameterClassification::GetParameterType( pCur, nParams - i);
                    if ( eType != formula::ParamClass::Reference &&
                            eType != formula::ParamClass::ReferenceOrRefArray &&
                            eType != formula::ParamClass::ReferenceOrForceArray &&
                            // For scalar Value: convert to Array/JumpMatrix
                            // only if in array formula context, else (function
                            // has ForceArray or ReferenceOrForceArray
                            // parameter *somewhere else*) pick a normal
                            // position dependent implicit intersection later.
                            (eType != formula::ParamClass::Value || IsInArrayContext()))
                    {
                        SCCOL nCol1, nCol2;
                        SCROW nRow1, nRow2;
                        SCTAB nTab1, nTab2;
                        DoubleRefToVars( p, nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
                        // Make sure the map exists, created if not.
                        GetTokenMatrixMap();
                        ScMatrixRef pMat = CreateMatrixFromDoubleRef( p,
                                nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
                        if (pMat)
                        {
                            if ( eType == formula::ParamClass::Value )
                            {   // only if single value expected
                                if ( nJumpCols < o3tl::make_unsigned(nCol2 - nCol1 + 1) )
                                    nJumpCols = static_cast<SCSIZE>(nCol2 - nCol1 + 1);
                                if ( nJumpRows < o3tl::make_unsigned(nRow2 - nRow1 + 1) )
                                    nJumpRows = static_cast<SCSIZE>(nRow2 - nRow1 + 1);
                            }
                            formula::FormulaToken* pNew = new ScMatrixToken( std::move(pMat) );
                            pNew->IncRef();
                            pStack[ sp - i ] = pNew;
                            p->DecRef();    // p may be dead now!
                        }
                    }
                }
                break;
                case svExternalDoubleRef:
                {
                    formula::ParamClass eType = ScParameterClassification::GetParameterType( pCur, nParams - i);
                    if (eType == formula::ParamClass::Value || eType == formula::ParamClass::Array)
                    {
                        sal_uInt16 nFileId = p->GetIndex();
                        OUString aTabName = p->GetString().getString();
                        const ScComplexRefData& rRef = *p->GetDoubleRef();
                        ScExternalRefCache::TokenArrayRef pArray;
                        GetExternalDoubleRef(nFileId, aTabName, rRef, pArray);
                        if (nGlobalError != FormulaError::NONE || !pArray)
                            break;
                        formula::FormulaToken* pTemp = pArray->FirstToken();
                        if (!pTemp)
                            break;

                        ScMatrixRef pMat = pTemp->GetMatrix();
                        if (pMat)
                        {
                            if (eType == formula::ParamClass::Value)
                            {   // only if single value expected
                                SCSIZE nC, nR;
                                pMat->GetDimensions( nC, nR);
                                if (nJumpCols < nC)
                                    nJumpCols = nC;
                                if (nJumpRows < nR)
                                    nJumpRows = nR;
                            }
                            formula::FormulaToken* pNew = new ScMatrixToken( std::move(pMat) );
                            pNew->IncRef();
                            pStack[ sp - i ] = pNew;
                            p->DecRef();    // p may be dead now!
                        }
                    }
                }
                break;
                case svRefList:
                {
                    formula::ParamClass eType = ScParameterClassification::GetParameterType( pCur, nParams - i);
                    if ( eType != formula::ParamClass::Reference &&
                            eType != formula::ParamClass::ReferenceOrRefArray &&
                            eType != formula::ParamClass::ReferenceOrForceArray &&
                            eType != formula::ParamClass::ForceArray)
                    {
                        // can't convert to matrix
                        SetError( FormulaError::NoRef);
                    }
                    // else: the consuming function has to decide if and how to
                    // handle a reference list argument in array context.
                }
                break;
                default:
                    assert(!"ConvertMatrixParameters: unknown parameter type");
            }
        }
    }
    if( nJumpCols && nJumpRows )
    {
        short nPC = aCode.GetPC();
        short nStart = nPC - 1;     // restart on current code (-1)
        short nNext = nPC;          // next instruction after subroutine
        short nStop = nPC + 1;      // stop subroutine before reaching that
        FormulaConstTokenRef xNew;
        ScTokenMatrixMap::const_iterator aMapIter;
        if ((aMapIter = maTokenMatrixMap.find( pCur)) != maTokenMatrixMap.end())
            xNew = (*aMapIter).second;
        else
        {
            std::shared_ptr<ScJumpMatrix> pJumpMat;
            try
            {
                pJumpMat = std::make_shared<ScJumpMatrix>( pCur->GetOpCode(), nJumpCols, nJumpRows);
            }
            catch (const std::bad_alloc&)
            {
                SAL_WARN("sc.core", "std::bad_alloc in ScJumpMatrix ctor with " << nJumpCols << " columns and " << nJumpRows << " rows");
                return false;
            }
            pJumpMat->SetAllJumps( 1.0, nStart, nNext, nStop);
            // pop parameters and store in ScJumpMatrix, push in JumpMatrix()
            ScTokenVec aParams(nParams);
            for ( sal_uInt16 i=1; i <= nParams && sp > 0; ++i )
            {
                const FormulaToken* p = pStack[ --sp ];
                p->IncRef();
                // store in reverse order such that a push may simply iterate
                aParams[ nParams - i ] = p;
            }
            pJumpMat->SetJumpParameters( std::move(aParams) );
            xNew = new ScJumpMatrixToken( std::move(pJumpMat) );
            GetTokenMatrixMap().emplace(pCur, xNew);
        }
        PushTempTokenWithoutError( xNew.get());
        // set continuation point of path for main code line
        aCode.Jump( nNext, nNext);
        return true;
    }
    return false;
}

ScMatrixRef ScInterpreter::PopMatrix()
{
    if( sp )
    {
        --sp;
        const FormulaToken* p = pStack[ sp ];
        switch (p->GetType())
        {
            case svError:
                nGlobalError = p->GetError();
                break;
            case svMatrix:
                {
                    // ScMatrix itself maintains an im/mutable flag that should
                    // be obeyed where necessary... so we can return ScMatrixRef
                    // here instead of ScConstMatrixRef.
                    ScMatrix* pMat = const_cast<FormulaToken*>(p)->GetMatrix();
                    if ( pMat )
                        pMat->SetErrorInterpreter( this);
                    else
                        SetError( FormulaError::UnknownVariable);
                    return pMat;
                }
            default:
                SetError( FormulaError::IllegalParameter);
        }
    }
    else
        SetError( FormulaError::UnknownStackVariable);
    return nullptr;
}

sc::RangeMatrix ScInterpreter::PopRangeMatrix()
{
    sc::RangeMatrix aRet;
    if (sp)
    {
        switch (pStack[sp-1]->GetType())
        {
            case svMatrix:
            {
                --sp;
                const FormulaToken* p = pStack[sp];
                aRet.mpMat = const_cast<FormulaToken*>(p)->GetMatrix();
                if (aRet.mpMat)
                {
                    aRet.mpMat->SetErrorInterpreter(this);
                    if (p->GetByte() == MATRIX_TOKEN_HAS_RANGE)
                    {
                        const ScComplexRefData& rRef = *p->GetDoubleRef();
                        if (!rRef.Ref1.IsColRel() && !rRef.Ref1.IsRowRel() && !rRef.Ref2.IsColRel() && !rRef.Ref2.IsRowRel())
                        {
                            aRet.mnCol1 = rRef.Ref1.Col();
                            aRet.mnRow1 = rRef.Ref1.Row();
                            aRet.mnTab1 = rRef.Ref1.Tab();
                            aRet.mnCol2 = rRef.Ref2.Col();
                            aRet.mnRow2 = rRef.Ref2.Row();
                            aRet.mnTab2 = rRef.Ref2.Tab();
                        }
                    }
                }
                else
                    SetError( FormulaError::UnknownVariable);
            }
            break;
            default:
                aRet.mpMat = PopMatrix();
        }
    }
    return aRet;
}

void ScInterpreter::QueryMatrixType(const ScMatrixRef& xMat, SvNumFormatType& rRetTypeExpr, sal_uInt32& rRetIndexExpr)
{
    if (xMat)
    {
        SCSIZE nCols, nRows;
        xMat->GetDimensions(nCols, nRows);
        ScMatrixValue nMatVal = xMat->Get(0, 0);
        ScMatValType nMatValType = nMatVal.nType;
        if (ScMatrix::IsNonValueType( nMatValType))
        {
            if ( xMat->IsEmptyPath( 0, 0))
            {   // result of empty FALSE jump path
                FormulaTokenRef xRes = CreateFormulaDoubleToken( 0.0);
                PushTempToken( new ScMatrixFormulaCellToken(nCols, nRows, xMat, xRes.get()));
                rRetTypeExpr = SvNumFormatType::LOGICAL;
            }
            else if ( xMat->IsEmptyResult( 0, 0))
            {   // empty formula result
                FormulaTokenRef xRes = new ScEmptyCellToken( true, true);   // inherited, display empty
                PushTempToken( new ScMatrixFormulaCellToken(nCols, nRows, xMat, xRes.get()));
            }
            else if ( xMat->IsEmpty( 0, 0))
            {   // empty or empty cell
                FormulaTokenRef xRes = new ScEmptyCellToken( false, true);  // not inherited, display empty
                PushTempToken( new ScMatrixFormulaCellToken(nCols, nRows, xMat, xRes.get()));
            }
            else
            {
                FormulaTokenRef xRes = new FormulaStringToken( nMatVal.GetString() );
                PushTempToken( new ScMatrixFormulaCellToken(nCols, nRows, xMat, xRes.get()));
                rRetTypeExpr = SvNumFormatType::TEXT;
            }
        }
        else
        {
            FormulaError nErr = GetDoubleErrorValue( nMatVal.fVal);
            FormulaTokenRef xRes;
            if (nErr != FormulaError::NONE)
                xRes = new FormulaErrorToken( nErr);
            else
                xRes = CreateFormulaDoubleToken( nMatVal.fVal);
            PushTempToken( new ScMatrixFormulaCellToken(nCols, nRows, xMat, xRes.get()));
            if ( rRetTypeExpr != SvNumFormatType::LOGICAL )
                rRetTypeExpr = SvNumFormatType::NUMBER;
        }
        rRetIndexExpr = 0;
        xMat->SetErrorInterpreter( nullptr);
    }
    else
        SetError( FormulaError::UnknownStackVariable);
}

formula::FormulaToken* ScInterpreter::CreateFormulaDoubleToken( double fVal, SvNumFormatType nFmt )
{
    assert( mrContext.maTokens.size() == TOKEN_CACHE_SIZE );

    // Find a spare token
    for ( auto p : mrContext.maTokens )
    {
        if (p && p->GetRef() == 1)
        {
            p->SetDouble(fVal);
            p->SetDoubleType( static_cast<sal_Int16>(nFmt) );
            return p;
        }
    }

    // Allocate a new token
    auto p = new FormulaTypedDoubleToken( fVal, static_cast<sal_Int16>(nFmt) );
    p->SetRefCntPolicy(RefCntPolicy::UnsafeRef);
    if ( mrContext.maTokens[mrContext.mnTokenCachePos] )
        mrContext.maTokens[mrContext.mnTokenCachePos]->DecRef();
    mrContext.maTokens[mrContext.mnTokenCachePos] = p;
    p->IncRef();
    mrContext.mnTokenCachePos = (mrContext.mnTokenCachePos + 1) % TOKEN_CACHE_SIZE;
    return p;
}

formula::FormulaToken* ScInterpreter::CreateDoubleOrTypedToken( double fVal )
{
    // NumberFormat::NUMBER is the default untyped double.
    if (nFuncFmtType != SvNumFormatType::ALL && nFuncFmtType != SvNumFormatType::NUMBER &&
            nFuncFmtType != SvNumFormatType::UNDEFINED)
        return CreateFormulaDoubleToken( fVal, nFuncFmtType);
    else
        return CreateFormulaDoubleToken( fVal);
}

void ScInterpreter::PushDouble(double nVal)
{
    TreatDoubleError( nVal );
    if (!IfErrorPushError())
        PushTempTokenWithoutError( CreateDoubleOrTypedToken( nVal));
}

void ScInterpreter::PushInt(int nVal)
{
    if (!IfErrorPushError())
        PushTempTokenWithoutError( CreateDoubleOrTypedToken( nVal));
}

void ScInterpreter::PushStringBuffer( const sal_Unicode* pString )
{
    if ( pString )
    {
        svl::SharedString aSS = mrDoc.GetSharedStringPool().intern(OUString(pString));
        PushString(aSS);
    }
    else
        PushString(svl::SharedString::getEmptyString());
}

void ScInterpreter::PushString( const OUString& rStr )
{
    PushString(mrDoc.GetSharedStringPool().intern(rStr));
}

void ScInterpreter::PushString( const svl::SharedString& rString )
{
    if (!IfErrorPushError())
        PushTempTokenWithoutError( new FormulaStringToken( rString ) );
}

void ScInterpreter::PushSingleRef(SCCOL nCol, SCROW nRow, SCTAB nTab)
{
    if (!IfErrorPushError())
    {
        ScSingleRefData aRef;
        aRef.InitAddress(ScAddress(nCol,nRow,nTab));
        PushTempTokenWithoutError( new ScSingleRefToken( mrDoc.GetSheetLimits(), aRef ) );
    }
}

void ScInterpreter::PushDoubleRef(SCCOL nCol1, SCROW nRow1, SCTAB nTab1,
                                  SCCOL nCol2, SCROW nRow2, SCTAB nTab2)
{
    if (!IfErrorPushError())
    {
        ScComplexRefData aRef;
        aRef.InitRange(ScRange(nCol1,nRow1,nTab1,nCol2,nRow2,nTab2));
        PushTempTokenWithoutError( new ScDoubleRefToken( mrDoc.GetSheetLimits(), aRef ) );
    }
}

void ScInterpreter::PushExternalSingleRef(
    sal_uInt16 nFileId, const OUString& rTabName, SCCOL nCol, SCROW nRow, SCTAB nTab)
{
    if (!IfErrorPushError())
    {
        ScSingleRefData aRef;
        aRef.InitAddress(ScAddress(nCol,nRow,nTab));
        PushTempTokenWithoutError( new ScExternalSingleRefToken(nFileId,
                    mrDoc.GetSharedStringPool().intern( rTabName), aRef)) ;
    }
}

void ScInterpreter::PushExternalDoubleRef(
    sal_uInt16 nFileId, const OUString& rTabName,
    SCCOL nCol1, SCROW nRow1, SCTAB nTab1, SCCOL nCol2, SCROW nRow2, SCTAB nTab2)
{
    if (!IfErrorPushError())
    {
        ScComplexRefData aRef;
        aRef.InitRange(ScRange(nCol1,nRow1,nTab1,nCol2,nRow2,nTab2));
        PushTempTokenWithoutError( new ScExternalDoubleRefToken(nFileId,
                    mrDoc.GetSharedStringPool().intern( rTabName), aRef) );
    }
}

void ScInterpreter::PushSingleRef( const ScRefAddress& rRef )
{
    if (!IfErrorPushError())
    {
        ScSingleRefData aRef;
        aRef.InitFromRefAddress( mrDoc, rRef, aPos);
        PushTempTokenWithoutError( new ScSingleRefToken( mrDoc.GetSheetLimits(), aRef ) );
    }
}

void ScInterpreter::PushDoubleRef( const ScRefAddress& rRef1, const ScRefAddress& rRef2 )
{
    if (!IfErrorPushError())
    {
        ScComplexRefData aRef;
        aRef.InitFromRefAddresses( mrDoc, rRef1, rRef2, aPos);
        PushTempTokenWithoutError( new ScDoubleRefToken( mrDoc.GetSheetLimits(), aRef ) );
    }
}

void ScInterpreter::PushMatrix( const sc::RangeMatrix& rMat )
{
    if (!rMat.isRangeValid())
    {
        // Just push the matrix part only.
        PushMatrix(rMat.mpMat);
        return;
    }

    rMat.mpMat->SetErrorInterpreter(nullptr);
    nGlobalError = FormulaError::NONE;
    PushTempTokenWithoutError(new ScMatrixRangeToken(rMat));
}

void ScInterpreter::PushMatrix(const ScMatrixRef& pMat)
{
    pMat->SetErrorInterpreter( nullptr);
    // No   if (!IfErrorPushError())   because ScMatrix stores errors itself,
    // but with notifying ScInterpreter via nGlobalError, substituting it would
    // mean to inherit the error on all array elements in all following
    // operations.
    nGlobalError = FormulaError::NONE;
    PushTempTokenWithoutError( new ScMatrixToken( pMat ) );
}

void ScInterpreter::PushError( FormulaError nError )
{
    SetError( nError );     // only sets error if not already set
    PushTempTokenWithoutError( new FormulaErrorToken( nGlobalError));
}

void ScInterpreter::PushParameterExpected()
{
    PushError( FormulaError::ParameterExpected);
}

void ScInterpreter::PushIllegalParameter()
{
    PushError( FormulaError::IllegalParameter);
}

void ScInterpreter::PushIllegalArgument()
{
    PushError( FormulaError::IllegalArgument);
}

void ScInterpreter::PushNA()
{
    PushError( FormulaError::NotAvailable);
}

void ScInterpreter::PushNoValue()
{
    PushError( FormulaError::NoValue);
}

bool ScInterpreter::IsMissing() const
{
    return sp && pStack[sp - 1]->GetType() == svMissing;
}

StackVar ScInterpreter::GetRawStackType()
{
    if( sp )
    {
        return pStack[sp - 1]->GetType();
    }
    else
    {
        SetError(FormulaError::UnknownStackVariable);
        return svUnknown;
    }
}

StackVar ScInterpreter::GetStackType()
{
    switch (StackVar eRes = GetRawStackType())
    {
        case svMissing:
        case svEmptyCell:
            return svDouble; // default!
        default:
            return eRes;
    }
}

StackVar ScInterpreter::GetStackType( sal_uInt8 nParam )
{
    StackVar eRes;
    if( sp > nParam-1 )
    {
        eRes = pStack[sp - nParam]->GetType();
        if( eRes == svMissing || eRes == svEmptyCell )
            eRes = svDouble;    // default!
    }
    else
        eRes = svUnknown;
    return eRes;
}

void ScInterpreter::ReverseStack( sal_uInt8 nParamCount )
{
    //reverse order of parameter stack
    assert( sp >= nParamCount && " less stack elements than parameters");
    sal_uInt16 nStackParams = std::min<sal_uInt16>( sp, nParamCount);
    std::reverse( pStack+(sp-nStackParams), pStack+sp );
}

bool ScInterpreter::DoubleRefToPosSingleRef( const ScRange& rRange, ScAddress& rAdr )
{
    // Check for a singleton first - no implicit intersection for them.
    if( rRange.aStart == rRange.aEnd )
    {
        rAdr = rRange.aStart;
        return true;
    }

    bool bOk = false;

    if ( pJumpMatrix )
    {
        bOk = rRange.aStart.Tab() == rRange.aEnd.Tab();
        if ( !bOk )
            SetError( FormulaError::IllegalArgument);
        else
        {
            SCSIZE nC, nR;
            pJumpMatrix->GetPos( nC, nR);
            rAdr.SetCol( sal::static_int_cast<SCCOL>( rRange.aStart.Col() + nC ) );
            rAdr.SetRow( sal::static_int_cast<SCROW>( rRange.aStart.Row() + nR ) );
            rAdr.SetTab( rRange.aStart.Tab());
            bOk = rRange.aStart.Col() <= rAdr.Col() && rAdr.Col() <=
                rRange.aEnd.Col() && rRange.aStart.Row() <= rAdr.Row() &&
                rAdr.Row() <= rRange.aEnd.Row();
            if ( !bOk )
                SetError( FormulaError::NoValue);
        }
        return bOk;
    }

    bOk = ScCompiler::DoubleRefToPosSingleRefScalarCase(rRange, rAdr, aPos);

    if ( !bOk )
        SetError( FormulaError::NoValue );
    return bOk;
}

double ScInterpreter::GetDoubleFromMatrix(const ScMatrixRef& pMat)
{
    if (!pMat)
        return 0.0;

    if ( !pJumpMatrix )
    {
        double fVal = pMat->GetDoubleWithStringConversion( 0, 0);
        FormulaError nErr = GetDoubleErrorValue( fVal);
        if (nErr != FormulaError::NONE)
        {
            // Do not propagate the coded double error, but set nGlobalError in
            // case the matrix did not have an error interpreter set.
            SetError( nErr);
            fVal = 0.0;
        }
        return fVal;
    }

    SCSIZE nCols, nRows, nC, nR;
    pMat->GetDimensions( nCols, nRows);
    pJumpMatrix->GetPos( nC, nR);
    // Use vector replication for single row/column arrays.
    if ( (nC < nCols || nCols == 1) && (nR < nRows || nRows == 1) )
    {
        double fVal = pMat->GetDoubleWithStringConversion( nC, nR);
        FormulaError nErr = GetDoubleErrorValue( fVal);
        if (nErr != FormulaError::NONE)
        {
            // Do not propagate the coded double error, but set nGlobalError in
            // case the matrix did not have an error interpreter set.
            SetError( nErr);
            fVal = 0.0;
        }
        return fVal;
    }

    SetError( FormulaError::NoValue);
    return 0.0;
}

double ScInterpreter::GetDouble()
{
    double nVal;
    switch( GetRawStackType() )
    {
        case svDouble:
            nVal = PopDouble();
        break;
        case svString:
            nVal = ConvertStringToValue( PopString().getString());
        break;
        case svSingleRef:
        {
            ScAddress aAdr;
            PopSingleRef( aAdr );
            ScRefCellValue aCell(mrDoc, aAdr);
            nVal = GetCellValue(aAdr, aCell);
        }
        break;
        case svDoubleRef:
        {   // generate position dependent SingleRef
            ScRange aRange;
            PopDoubleRef( aRange );
            ScAddress aAdr;
            if ( nGlobalError == FormulaError::NONE && DoubleRefToPosSingleRef( aRange, aAdr ) )
            {
                ScRefCellValue aCell(mrDoc, aAdr);
                nVal = GetCellValue(aAdr, aCell);
            }
            else
                nVal = 0.0;
        }
        break;
        case svExternalSingleRef:
        {
            ScExternalRefCache::TokenRef pToken;
            PopExternalSingleRef(pToken);
            if (nGlobalError != FormulaError::NONE)
            {
                nVal = 0.0;
                break;
            }

            if (pToken->GetType() == svDouble || pToken->GetType() == svEmptyCell)
                nVal = pToken->GetDouble();
            else
                nVal = ConvertStringToValue( pToken->GetString().getString());
        }
        break;
        case svExternalDoubleRef:
        {
            ScMatrixRef pMat;
            PopExternalDoubleRef(pMat);
            if (nGlobalError != FormulaError::NONE)
            {
                nVal = 0.0;
                break;
            }

            nVal = GetDoubleFromMatrix(pMat);
        }
        break;
        case svMatrix:
        {
            ScMatrixRef pMat = PopMatrix();
            nVal = GetDoubleFromMatrix(pMat);
        }
        break;
        case svError:
            PopError();
            nVal = 0.0;
        break;
        case svEmptyCell:
        case svMissing:
            Pop();
            nVal = 0.0;
        break;
        default:
            PopError();
            SetError( FormulaError::IllegalParameter);
            nVal = 0.0;
    }
    if ( nFuncFmtType == nCurFmtType )
        nFuncFmtIndex = nCurFmtIndex;
    return nVal;
}

double ScInterpreter::GetDoubleWithDefault(double nDefault)
{
    if (!IsMissing())
        return GetDouble();
    Pop();
    return nDefault;
}

bool ScInterpreter::GetBoolWithDefault(bool bDefault)
{
    return GetDoubleWithDefault(bDefault ? 1.0 : 0.0) != 0.0;
}

template <typename Int>
    requires std::is_integral_v<Int>
Int ScInterpreter::double_to(double fVal)
{
    if (!std::isfinite(fVal))
    {
        SetError( GetDoubleErrorValue( fVal));
        return std::numeric_limits<Int>::max();
    }
    if (fVal > 0.0)
    {
        fVal = rtl::math::approxFloor( fVal);
        if (fVal > std::numeric_limits<Int>::max())
        {
            SetError( FormulaError::IllegalArgument);
            return std::numeric_limits<Int>::max();
        }
    }
    else if (fVal < 0.0)
    {
        fVal = rtl::math::approxCeil( fVal);
        if (fVal < std::numeric_limits<Int>::min())
        {
            SetError( FormulaError::IllegalArgument);
            return std::numeric_limits<Int>::max();
        }
    }
    return static_cast<Int>(fVal);
}

sal_Int32 ScInterpreter::double_to_int32(double fVal)
{
    return double_to<sal_Int32>(fVal);
}

sal_Int32 ScInterpreter::GetInt32()
{
    return double_to_int32(GetDouble());
}

sal_Int32 ScInterpreter::GetInt32WithDefault( sal_Int32 nDefault )
{
    return double_to_int32(GetDoubleWithDefault(nDefault));
}

sal_Int32 ScInterpreter::GetFloor32()
{
    double fVal = GetDouble();
    if (!std::isfinite(fVal))
    {
        SetError( GetDoubleErrorValue( fVal));
        return SAL_MAX_INT32;
    }
    fVal = rtl::math::approxFloor( fVal);
    if (fVal < SAL_MIN_INT32 || SAL_MAX_INT32 < fVal)
    {
        SetError( FormulaError::IllegalArgument);
        return SAL_MAX_INT32;
    }
    return static_cast<sal_Int32>(fVal);
}

sal_Int16 ScInterpreter::GetInt16()
{
    return double_to<sal_Int16>(GetDouble());
}

sal_Int16 ScInterpreter::GetInt16WithDefault(sal_Int16 nDefault)
{
    return double_to<sal_Int16>(GetDoubleWithDefault(nDefault));
}

sal_uInt32 ScInterpreter::GetUInt32()
{
    return double_to<sal_uInt32>(GetDouble());
}

bool ScInterpreter::GetDoubleOrString( double& rDouble, svl::SharedString& rString )
{
    bool bDouble = true;
    switch( GetRawStackType() )
    {
        case svDouble:
            rDouble = PopDouble();
        break;
        case svString:
            rString = PopString();
            bDouble = false;
        break;
        case svDoubleRef :
        case svSingleRef :
        {
            ScAddress aAdr;
            if (!PopDoubleRefOrSingleRef( aAdr))
            {
                rDouble = 0.0;
                return true;    // caller needs to check nGlobalError
            }
            ScRefCellValue aCell( mrDoc, aAdr);
            if (aCell.hasNumeric())
            {
                rDouble = GetCellValue( aAdr, aCell);
            }
            else
            {
                GetCellString( rString, aCell);
                bDouble = false;
            }
        }
        break;
        case svExternalSingleRef:
        case svExternalDoubleRef:
        case svMatrix:
        {
            ScMatValType nType = GetDoubleOrStringFromMatrix( rDouble, rString);
            bDouble = ScMatrix::IsValueType( nType);
        }
        break;
        case svError:
            PopError();
            rDouble = 0.0;
        break;
        case svEmptyCell:
        case svMissing:
            Pop();
            rDouble = 0.0;
        break;
        default:
            PopError();
            SetError( FormulaError::IllegalParameter);
            rDouble = 0.0;
    }
    if ( nFuncFmtType == nCurFmtType )
        nFuncFmtIndex = nCurFmtIndex;
    return bDouble;
}

svl::SharedString ScInterpreter::GetString()
{
    switch (GetRawStackType())
    {
        case svError:
            PopError();
            return svl::SharedString::getEmptyString();
        case svMissing:
        case svEmptyCell:
            Pop();
            return svl::SharedString::getEmptyString();
        case svDouble:
        {
            return GetStringFromDouble( PopDouble() );
        }
        case svString:
        case svStringName:
            return PopString();
        case svSingleRef:
        {
            ScAddress aAdr;
            PopSingleRef( aAdr );
            if (nGlobalError == FormulaError::NONE)
            {
                ScRefCellValue aCell(mrDoc, aAdr);
                svl::SharedString aSS;
                GetCellString(aSS, aCell);
                return aSS;
            }
            else
                return svl::SharedString::getEmptyString();
        }
        case svDoubleRef:
        {   // generate position dependent SingleRef
            ScRange aRange;
            PopDoubleRef( aRange );
            ScAddress aAdr;
            if ( nGlobalError == FormulaError::NONE && DoubleRefToPosSingleRef( aRange, aAdr ) )
            {
                ScRefCellValue aCell(mrDoc, aAdr);
                svl::SharedString aSS;
                GetCellString(aSS, aCell);
                return aSS;
            }
            else
                return svl::SharedString::getEmptyString();
        }
        case svExternalSingleRef:
        {
            ScExternalRefCache::TokenRef pToken;
            PopExternalSingleRef(pToken);
            if (nGlobalError != FormulaError::NONE)
                return svl::SharedString::getEmptyString();

            if (pToken->GetType() == svDouble)
            {
                return GetStringFromDouble( pToken->GetDouble() );
            }
            else // svString or svEmpty
                return pToken->GetString();
        }
        case svExternalDoubleRef:
        {
            ScMatrixRef pMat;
            PopExternalDoubleRef(pMat);
            return GetStringFromMatrix(pMat);
        }
        case svMatrix:
        {
            ScMatrixRef pMat = PopMatrix();
            return GetStringFromMatrix(pMat);
        }
        break;
        default:
            PopError();
            SetError( FormulaError::IllegalArgument);
    }
    return svl::SharedString::getEmptyString();
}

svl::SharedString ScInterpreter::GetStringFromMatrix(const ScMatrixRef& pMat)
{
    if ( !pMat )
        ;   // nothing
    else if ( !pJumpMatrix )
    {
        return pMat->GetString( mrContext, 0, 0);
    }
    else
    {
        SCSIZE nCols, nRows, nC, nR;
        pMat->GetDimensions( nCols, nRows);
        pJumpMatrix->GetPos( nC, nR);
        // Use vector replication for single row/column arrays.
        if ( (nC < nCols || nCols == 1) && (nR < nRows || nRows == 1) )
            return pMat->GetString( mrContext, nC, nR);

        SetError( FormulaError::NoValue);
    }
    return svl::SharedString::getEmptyString();
}

ScMatValType ScInterpreter::GetDoubleOrStringFromMatrix(
    double& rDouble, svl::SharedString& rString )
{

    rDouble = 0.0;
    rString = svl::SharedString::getEmptyString();
    ScMatValType nMatValType = ScMatValType::Empty;

    ScMatrixRef pMat;
    StackVar eType = GetStackType();
    if (eType == svExternalDoubleRef || eType == svExternalSingleRef || eType == svMatrix)
    {
        pMat = GetMatrix();
    }
    else
    {
        PopError();
        SetError( FormulaError::IllegalParameter);
        return nMatValType;
    }

    ScMatrixValue nMatVal;
    if (!pMat)
    {
        // nothing
    }
    else if (!pJumpMatrix)
    {
        nMatVal = pMat->Get(0, 0);
        nMatValType = nMatVal.nType;
    }
    else
    {
        SCSIZE nCols, nRows, nC, nR;
        pMat->GetDimensions( nCols, nRows);
        pJumpMatrix->GetPos( nC, nR);
        // Use vector replication for single row/column arrays.
        if ( (nC < nCols || nCols == 1) && (nR < nRows || nRows == 1) )
        {
            nMatVal = pMat->Get( nC, nR);
            nMatValType = nMatVal.nType;
        }
        else
            SetError( FormulaError::NoValue);
    }

    if (ScMatrix::IsValueType( nMatValType))
    {
        rDouble = nMatVal.fVal;
        FormulaError nError = nMatVal.GetError();
        if (nError != FormulaError::NONE)
            SetError( nError);
    }
    else
    {
        rString = nMatVal.GetString();
    }

    return nMatValType;
}

svl::SharedString ScInterpreter::GetStringFromDouble( double fVal )
{
    sal_uLong nIndex = mrContext.NFGetStandardFormat(
                        SvNumFormatType::NUMBER,
                        ScGlobal::eLnge);
    return mrStrPool.intern(mrContext.NFGetInputLineString(fVal, nIndex));
}

void ScInterpreter::ScDBGet()
{
    bool bMissingField = false;
    unique_ptr<ScDBQueryParamBase> pQueryParam( GetDBParams(bMissingField) );
    if (!pQueryParam)
    {
        // Failed to create query param.
        PushIllegalParameter();
        return;
    }

    pQueryParam->mbSkipString = false;
    ScDBQueryDataIterator aValIter(mrDoc, mrContext, std::move(pQueryParam));
    ScDBQueryDataIterator::Value aValue;
    if (!aValIter.GetFirst(aValue) || aValue.mnError != FormulaError::NONE)
    {
        // No match found.
        PushNoValue();
        return;
    }

    ScDBQueryDataIterator::Value aValNext;
    if (aValIter.GetNext(aValNext) && aValNext.mnError == FormulaError::NONE)
    {
        // There should be only one unique match.
        PushIllegalArgument();
        return;
    }

    if (aValue.mbIsNumber)
        PushDouble(aValue.mfValue);
    else
        PushString(aValue.maString);
}

void ScInterpreter::ScExternal()
{
    sal_uInt8 nParamCount = GetByte();
    OUString aUnoName;
    OUString aFuncName( pCur->GetExternal().toAsciiUpperCase());    // programmatic name
    LegacyFuncData* pLegacyFuncData = ScGlobal::GetLegacyFuncCollection()->findByName(aFuncName);
    if (pLegacyFuncData)
    {
        // Old binary non-UNO add-in function.
        // NOTE: parameter count is 1-based with the 0th "parameter" being the
        // return value, included in pLegacyFuncDatat->GetParamCount()
        if (nParamCount < MAXFUNCPARAM && nParamCount == pLegacyFuncData->GetParamCount() - 1)
        {
            ParamType   eParamType[MAXFUNCPARAM];
            void*       ppParam[MAXFUNCPARAM];
            double      nVal[MAXFUNCPARAM];
            char*       pStr[MAXFUNCPARAM];
            sal_uInt8*  pCellArr[MAXFUNCPARAM];
            short       i;

            for (i = 0; i < MAXFUNCPARAM; i++)
            {
                eParamType[i] = pLegacyFuncData->GetParamType(i);
                ppParam[i] = nullptr;
                nVal[i] = 0.0;
                pStr[i] = nullptr;
                pCellArr[i] = nullptr;
            }

            for (i = nParamCount; (i > 0) && (nGlobalError == FormulaError::NONE); i--)
            {
                if (IsMissing())
                {
                    // Old binary Add-In can't distinguish between missing
                    // omitted argument and 0 (or any other value). Force
                    // error.
                    SetError( FormulaError::ParameterExpected);
                    break;  // for
                }
                switch (eParamType[i])
                {
                    case ParamType::PTR_DOUBLE :
                        {
                            nVal[i-1] = GetDouble();
                            ppParam[i] = &nVal[i-1];
                        }
                        break;
                    case ParamType::PTR_STRING :
                        {
                            OString aStr(OUStringToOString(GetString().getString(),
                                osl_getThreadTextEncoding()));
                            if ( aStr.getLength() >= ADDIN_MAXSTRLEN )
                                SetError( FormulaError::StringOverflow );
                            else
                            {
                                pStr[i-1] = new char[ADDIN_MAXSTRLEN];
                                strncpy( pStr[i-1], aStr.getStr(), ADDIN_MAXSTRLEN );
                                pStr[i-1][ADDIN_MAXSTRLEN-1] = 0;
                                ppParam[i] = pStr[i-1];
                            }
                        }
                        break;
                    case ParamType::PTR_DOUBLE_ARR :
                        {
                            SCCOL nCol1;
                            SCROW nRow1;
                            SCTAB nTab1;
                            SCCOL nCol2;
                            SCROW nRow2;
                            SCTAB nTab2;
                            PopDoubleRef(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
                            pCellArr[i-1] = new sal_uInt8[MAXARRSIZE];
                            if (!CreateDoubleArr(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2, pCellArr[i-1]))
                                SetError(FormulaError::CodeOverflow);
                            else
                                ppParam[i] = pCellArr[i-1];
                        }
                        break;
                    case ParamType::PTR_STRING_ARR :
                        {
                            SCCOL nCol1;
                            SCROW nRow1;
                            SCTAB nTab1;
                            SCCOL nCol2;
                            SCROW nRow2;
                            SCTAB nTab2;
                            PopDoubleRef(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
                            pCellArr[i-1] = new sal_uInt8[MAXARRSIZE];
                            if (!CreateStringArr(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2, pCellArr[i-1]))
                                SetError(FormulaError::CodeOverflow);
                            else
                                ppParam[i] = pCellArr[i-1];
                        }
                        break;
                    case ParamType::PTR_CELL_ARR :
                        {
                            SCCOL nCol1;
                            SCROW nRow1;
                            SCTAB nTab1;
                            SCCOL nCol2;
                            SCROW nRow2;
                            SCTAB nTab2;
                            PopDoubleRef(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2);
                            pCellArr[i-1] = new sal_uInt8[MAXARRSIZE];
                            if (!CreateCellArr(nCol1, nRow1, nTab1, nCol2, nRow2, nTab2, pCellArr[i-1]))
                                SetError(FormulaError::CodeOverflow);
                            else
                                ppParam[i] = pCellArr[i-1];
                        }
                        break;
                    default :
                        SetError(FormulaError::IllegalParameter);
                        break;
                }
            }
            while ( i-- )
                Pop();      // In case of error (otherwise i==0) pop all parameters

            if (nGlobalError == FormulaError::NONE)
            {
                if ( pLegacyFuncData->GetAsyncType() == ParamType::NONE )
                {
                    switch ( eParamType[0] )
                    {
                        case ParamType::PTR_DOUBLE :
                        {
                            double nErg = 0.0;
                            ppParam[0] = &nErg;
                            pLegacyFuncData->Call(ppParam);
                            PushDouble(nErg);
                        }
                        break;
                        case ParamType::PTR_STRING :
                        {
                            std::unique_ptr<char[]> pcErg(new char[ADDIN_MAXSTRLEN]);
                            ppParam[0] = pcErg.get();
                            pLegacyFuncData->Call(ppParam);
                            OUString aUni( pcErg.get(), strlen(pcErg.get()), osl_getThreadTextEncoding() );
                            PushString( aUni );
                        }
                        break;
                        default:
                            PushError( FormulaError::UnknownState );
                    }
                }
                else
                {
                    // enable asyncs after loading
                    pArr->AddRecalcMode( ScRecalcMode::ONLOAD_LENIENT );
                    // assure identical handler with identical call?
                    double nErg = 0.0;
                    ppParam[0] = &nErg;
                    pLegacyFuncData->Call(ppParam);
                    sal_uLong nHandle = sal_uLong( nErg );
                    if ( nHandle >= 65536 )
                    {
                        ScAddInAsync* pAs = ScAddInAsync::Get( nHandle );
                        if ( !pAs )
                        {
                            pAs = new ScAddInAsync(nHandle, pLegacyFuncData, &mrDoc);
                            pMyFormulaCell->StartListening( *pAs );
                        }
                        else
                        {
                            pMyFormulaCell->StartListening( *pAs );
                            if ( !pAs->HasDocument( &mrDoc ) )
                                pAs->AddDocument( &mrDoc );
                        }
                        if ( pAs->IsValid() )
                        {
                            switch ( pAs->GetType() )
                            {
                                case ParamType::PTR_DOUBLE :
                                    PushDouble( pAs->GetValue() );
                                    break;
                                case ParamType::PTR_STRING :
                                    PushString( pAs->GetString() );
                                    break;
                                default:
                                    PushError( FormulaError::UnknownState );
                            }
                        }
                        else
                            PushNA();
                    }
                    else
                        PushNoValue();
                }
            }

            for (i = 0; i < MAXFUNCPARAM; i++)
            {
                delete[] pStr[i];
                delete[] pCellArr[i];
            }
        }
        else
        {
            while( nParamCount-- > 0)
                PopError();
            PushIllegalParameter();
        }
    }
    else if ( !( aUnoName = ScGlobal::GetAddInCollection()->FindFunction(aFuncName, false) ).isEmpty()  )
    {
        //  bLocalFirst=false in FindFunction, cFunc should be the stored
        //  internal name

        ScUnoAddInCall aCall( mrDoc, *ScGlobal::GetAddInCollection(), aUnoName, nParamCount );

        if ( !aCall.ValidParamCount() )
            SetError( FormulaError::IllegalParameter );

        if ( aCall.NeedsCaller() && GetError() == FormulaError::NONE )
        {
            ScDocShell* pShell = mrDoc.GetDocumentShell();
            if (pShell)
                aCall.SetCallerFromObjectShell( pShell );
            else
            {
                // use temporary model object (without document) to supply options
                aCall.SetCaller( static_cast<beans::XPropertySet*>(
                                    new ScDocOptionsObj( mrDoc.GetDocOptions() ) ) );
            }
        }

        short nPar = nParamCount;
        while ( nPar > 0 && GetError() == FormulaError::NONE )
        {
            --nPar;     // 0 .. (nParamCount-1)

            uno::Any aParam;
            if (IsMissing())
            {
                // Add-In has to explicitly handle an omitted empty missing
                // argument, do not default to anything like GetDouble() would
                // do (e.g. 0).
                Pop();
                aCall.SetParam( nPar, aParam );
                continue;   // while
            }

            StackVar nStackType = GetStackType();
            ScAddInArgumentType eType = aCall.GetArgType( nPar );
            switch (eType)
            {
                case SC_ADDINARG_INTEGER:
                    {
                        sal_Int32 nVal = GetInt32();
                        if (nGlobalError == FormulaError::NONE)
                            aParam <<= nVal;
                    }
                    break;

                case SC_ADDINARG_DOUBLE:
                    aParam <<= GetDouble();
                    break;

                case SC_ADDINARG_STRING:
                    aParam <<= GetString().getString();
                    break;

                case SC_ADDINARG_INTEGER_ARRAY:
                    switch( nStackType )
                    {
                        case svDouble:
                        case svString:
                        case svSingleRef:
                            {
                                sal_Int32 nVal = GetInt32();
                                if (nGlobalError == FormulaError::NONE)
                                {
                                    uno::Sequence<sal_Int32> aInner( &nVal, 1 );
                                    uno::Sequence< uno::Sequence<sal_Int32> > aOuter( &aInner, 1 );
                                    aParam <<= aOuter;
                                }
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                if (!ScRangeToSequence::FillLongArray( aParam, mrDoc, aRange ))
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svMatrix:
                            if (!ScRangeToSequence::FillLongArray( aParam, PopMatrix().get() ))
                                SetError(FormulaError::IllegalParameter);
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                case SC_ADDINARG_DOUBLE_ARRAY:
                    switch( nStackType )
                    {
                        case svDouble:
                        case svString:
                        case svSingleRef:
                            {
                                double fVal = GetDouble();
                                uno::Sequence<double> aInner( &fVal, 1 );
                                uno::Sequence< uno::Sequence<double> > aOuter( &aInner, 1 );
                                aParam <<= aOuter;
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                if (!ScRangeToSequence::FillDoubleArray( aParam, mrDoc, aRange ))
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svMatrix:
                            if (!ScRangeToSequence::FillDoubleArray( aParam, PopMatrix().get() ))
                                SetError(FormulaError::IllegalParameter);
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                case SC_ADDINARG_STRING_ARRAY:
                    switch( nStackType )
                    {
                        case svDouble:
                        case svString:
                        case svSingleRef:
                            {
                                OUString aString = GetString().getString();
                                uno::Sequence<OUString> aInner( &aString, 1 );
                                uno::Sequence< uno::Sequence<OUString> > aOuter( &aInner, 1 );
                                aParam <<= aOuter;
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                if (!ScRangeToSequence::FillStringArray( aParam, mrDoc, aRange ))
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svMatrix:
                            if (!ScRangeToSequence::FillStringArray( aParam, PopMatrix().get(), mrContext ))
                                SetError(FormulaError::IllegalParameter);
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                case SC_ADDINARG_MIXED_ARRAY:
                    switch( nStackType )
                    {
                        case svDouble:
                        case svString:
                        case svSingleRef:
                            {
                                uno::Any aElem;
                                if ( nStackType == svDouble )
                                    aElem <<= GetDouble();
                                else if ( nStackType == svString )
                                    aElem <<= GetString().getString();
                                else
                                {
                                    ScAddress aAdr;
                                    if ( PopDoubleRefOrSingleRef( aAdr ) )
                                    {
                                        ScRefCellValue aCell(mrDoc, aAdr);
                                        if (aCell.hasString())
                                        {
                                            svl::SharedString aStr;
                                            GetCellString(aStr, aCell);
                                            aElem <<= aStr.getString();
                                        }
                                        else
                                            aElem <<= GetCellValue(aAdr, aCell);
                                    }
                                }
                                uno::Sequence<uno::Any> aInner( &aElem, 1 );
                                uno::Sequence< uno::Sequence<uno::Any> > aOuter( &aInner, 1 );
                                aParam <<= aOuter;
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                if (!ScRangeToSequence::FillMixedArray( aParam, mrDoc, aRange ))
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svMatrix:
                            if (!ScRangeToSequence::FillMixedArray( aParam, PopMatrix().get() ))
                                SetError(FormulaError::IllegalParameter);
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                case SC_ADDINARG_VALUE_OR_ARRAY:
                    switch( nStackType )
                    {
                        case svDouble:
                            aParam <<= GetDouble();
                            break;
                        case svString:
                            aParam <<= GetString().getString();
                            break;
                        case svSingleRef:
                            {
                                ScAddress aAdr;
                                if ( PopDoubleRefOrSingleRef( aAdr ) )
                                {
                                    ScRefCellValue aCell(mrDoc, aAdr);
                                    if (aCell.hasString())
                                    {
                                        svl::SharedString aStr;
                                        GetCellString(aStr, aCell);
                                        aParam <<= aStr.getString();
                                    }
                                    else
                                        aParam <<= GetCellValue(aAdr, aCell);
                                }
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                if (!ScRangeToSequence::FillMixedArray( aParam, mrDoc, aRange ))
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svMatrix:
                            if (!ScRangeToSequence::FillMixedArray( aParam, PopMatrix().get() ))
                                SetError(FormulaError::IllegalParameter);
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                case SC_ADDINARG_CELLRANGE:
                    switch( nStackType )
                    {
                        case svSingleRef:
                            {
                                ScAddress aAdr;
                                PopSingleRef( aAdr );
                                ScRange aRange( aAdr );
                                uno::Reference<table::XCellRange> xObj =
                                        ScCellRangeObj::CreateRangeFromDoc( mrDoc, aRange );
                                if (xObj.is())
                                    aParam <<= xObj;
                                else
                                    SetError(FormulaError::IllegalParameter);
                            }
                            break;
                        case svDoubleRef:
                            {
                                ScRange aRange;
                                PopDoubleRef( aRange );
                                uno::Reference<table::XCellRange> xObj =
                                        ScCellRangeObj::CreateRangeFromDoc( mrDoc, aRange );
                                if (xObj.is())
                                {
                                    aParam <<= xObj;
                                }
                                else
                                {
                                    SetError(FormulaError::IllegalParameter);
                                }
                            }
                            break;
                        default:
                            PopError();
                            SetError(FormulaError::IllegalParameter);
                    }
                    break;

                default:
                    PopError();
                    SetError(FormulaError::IllegalParameter);
            }
            aCall.SetParam( nPar, aParam );
        }

        while (nPar-- > 0)
        {
            Pop();                  // in case of error, remove remaining args
        }
        if ( GetError() == FormulaError::NONE )
        {
            aCall.ExecuteCall();

            if ( aCall.HasVarRes() )                        // handle async functions
            {
                pArr->AddRecalcMode( ScRecalcMode::ONLOAD_LENIENT );
                uno::Reference<sheet::XVolatileResult> xRes = aCall.GetVarRes();
                ScAddInListener* pLis = ScAddInListener::Get( xRes );
                // In case there is no pMyFormulaCell, i.e. while interpreting
                // temporarily from within the Function Wizard, try to obtain a
                // valid result from an existing listener for that volatile, or
                // create a new and hope for an immediate result. If none
                // available that should lead to a void result and thus #N/A.
                bool bTemporaryListener = false;
                if ( !pLis )
                {
                    pLis = ScAddInListener::CreateListener( xRes, &mrDoc );
                    if (pMyFormulaCell)
                        pMyFormulaCell->StartListening( *pLis );
                    else
                        bTemporaryListener = true;
                }
                else if (pMyFormulaCell)
                {
                    pMyFormulaCell->StartListening( *pLis );
                    if ( !pLis->HasDocument( &mrDoc ) )
                    {
                        pLis->AddDocument( &mrDoc );
                    }
                }

                aCall.SetResult( pLis->GetResult() );       // use result from async

                if (bTemporaryListener)
                {
                    try
                    {
                        // EventObject can be any, not evaluated by
                        // ScAddInListener::disposing()
                        css::lang::EventObject aEvent;
                        pLis->disposing(aEvent);    // pLis is dead hereafter
                    }
                    catch (const uno::Exception&)
                    {
                    }
                }
            }

            if ( aCall.GetErrCode() != FormulaError::NONE )
            {
                PushError( aCall.GetErrCode() );
            }
            else if ( aCall.HasMatrix() )
            {
                PushMatrix( aCall.GetMatrix() );
            }
            else if ( aCall.HasString() )
            {
                PushString( aCall.GetString() );
            }
            else
            {
                PushDouble( aCall.GetValue() );
            }
        }
        else                // error...
            PushError( GetError());
    }
    else
    {
        while( nParamCount-- > 0)
        {
            PopError();
        }
        PushError( FormulaError::NoAddin );
    }
}

void ScInterpreter::ScMissing()
{
    if ( aCode.IsEndOfPath() )
        PushTempToken( new ScEmptyCellToken( false, false ) );
    else
        PushTempToken( new FormulaMissingToken );
}

#if HAVE_FEATURE_SCRIPTING

static uno::Any lcl_getSheetModule( const uno::Reference<table::XCellRange>& xCellRange, const ScDocument* pDok )
{
    uno::Reference< sheet::XSheetCellRange > xSheetRange( xCellRange, uno::UNO_QUERY_THROW );
    uno::Reference< beans::XPropertySet > xProps( xSheetRange->getSpreadsheet(), uno::UNO_QUERY_THROW );
    OUString sCodeName;
    xProps->getPropertyValue(u"CodeName"_ustr) >>= sCodeName;
    // #TODO #FIXME ideally we should 'throw' here if we don't get a valid parent, but... it is possible
    // to create a module ( and use 'Option VBASupport 1' ) for a calc document, in this scenario there
    // are *NO* special document module objects ( of course being able to switch between vba/non vba mode at
    // the document in the future could fix this, especially IF the switching of the vba mode takes care to
    // create the special document module objects if they don't exist.
    BasicManager* pBasMgr = pDok->GetDocumentShell()->GetBasicManager();

    uno::Reference< uno::XInterface > xIf;
    if ( pBasMgr && !pBasMgr->GetName().isEmpty() )
    {
        OUString sProj( u"Standard"_ustr );
        if ( !pDok->GetDocumentShell()->GetBasicManager()->GetName().isEmpty() )
        {
            sProj = pDok->GetDocumentShell()->GetBasicManager()->GetName();
        }
        StarBASIC* pBasic = pDok->GetDocumentShell()->GetBasicManager()->GetLib( sProj );
        if ( pBasic )
        {
            SbModule* pMod = pBasic->FindModule( sCodeName );
            if ( pMod )
            {
                xIf = pMod->GetUnoModule();
            }
        }
    }
    return uno::Any( xIf );
}

static bool lcl_setVBARange( const ScRange& aRange, const ScDocument& rDok, SbxVariable* pPar )
{
    bool bOk = false;
    try
    {
        uno::Reference< uno::XInterface > xVBARange;
        uno::Reference<table::XCellRange> xCellRange = ScCellRangeObj::CreateRangeFromDoc( rDok, aRange );
        uno::Sequence< uno::Any > aArgs{ lcl_getSheetModule( xCellRange, &rDok ),
                                         uno::Any(xCellRange) };
        xVBARange = ooo::vba::createVBAUnoAPIServiceWithArgs( rDok.GetDocumentShell(), "ooo.vba.excel.Range", aArgs );
        if ( xVBARange.is() )
        {
            SbxObjectRef aObj = GetSbUnoObject( u"A-Range"_ustr, uno::Any( xVBARange ) );
            SetSbUnoObjectDfltPropName( aObj.get() );
            bOk = pPar->PutObject( aObj.get() );
        }
    }
    catch( uno::Exception& )
    {
    }
    return bOk;
}

static bool lcl_isNumericResult( double& fVal, const SbxVariable* pVar )
{
    switch (pVar->GetType())
    {
        case SbxINTEGER:
        case SbxLONG:
        case SbxSINGLE:
        case SbxDOUBLE:
        case SbxCURRENCY:
        case SbxDATE:
        case SbxUSHORT:
        case SbxULONG:
        case SbxINT:
        case SbxUINT:
        case SbxSALINT64:
        case SbxSALUINT64:
        case SbxDECIMAL:
            fVal = pVar->GetDouble();
            return true;
        case SbxBOOL:
            fVal = (pVar->GetBool() ? 1.0 : 0.0);
            return true;
        default:
            ;   // nothing
    }
    return false;
}

#endif

void ScInterpreter::ScMacro()
{

#if !HAVE_FEATURE_SCRIPTING
    PushNoValue();      // without DocShell no CallBasic
    return;
#else
    SbxBase::ResetError();

    sal_uInt8 nParamCount = GetByte();
    OUString aMacro( pCur->GetExternal() );

    ScDocShell* pDocSh = mrDoc.GetDocumentShell();
    if ( !pDocSh )
    {
        PushNoValue();      // without DocShell no CallBasic
        return;
    }

    //  no security queue beforehand (just CheckMacroWarn), moved to  CallBasic

    //  If the  Dok was loaded during a Basic-Calls,
    //  is the  Sbx-object created(?)
//  pDocSh->GetSbxObject();

    //  search function with the name,
    //  then assemble  SfxObjectShell::CallBasic from aBasicStr, aMacroStr

    StarBASIC* pRoot;

    try
    {
        pRoot = pDocSh->GetBasic();
    }
    catch (...)
    {
        pRoot = nullptr;
    }

    SbxVariable* pVar = pRoot ? pRoot->Find(aMacro, SbxClassType::Method) : nullptr;
    if( !pVar || pVar->GetType() == SbxVOID )
    {
        PushError( FormulaError::NoMacro );
        return;
    }
    SbMethod* pMethod = dynamic_cast<SbMethod*>(pVar);
    if( !pMethod )
    {
        PushError( FormulaError::NoMacro );
        return;
    }

    bool bVolatileMacro = false;

    SbModule* pModule = pMethod->GetModule();
    bool bUseVBAObjects = pModule->IsVBASupport();
    SbxObject* pObject = pModule->GetParent();
    assert(pObject);
    OSL_ENSURE(dynamic_cast<const StarBASIC *>(pObject) != nullptr, "No Basic found!");
    OUString aMacroStr = pObject->GetName() + "." + pModule->GetName() + "." + pMethod->GetName();
    OUString aBasicStr;
    if (pRoot && bUseVBAObjects)
    {
        // just here to make sure the VBA objects when we run the macro during ODF import
        pRoot->getVBAGlobals();
    }
    if (pObject->GetParent())
    {
        aBasicStr = pObject->GetParent()->GetName();    // document BASIC
    }
    else
    {
        aBasicStr = SfxGetpApp()->GetName();            // application BASIC
    }
    //  assemble a parameter array

    SbxArrayRef refPar = new SbxArray;
    bool bOk = true;
    for( sal_uInt32 i = nParamCount; i && bOk ; i-- )
    {
        SbxVariable* pPar = refPar->Get(i);
        switch( GetStackType() )
        {
            case svDouble:
                pPar->PutDouble( GetDouble() );
            break;
            case svString:
                pPar->PutString( GetString().getString() );
            break;
            case svExternalSingleRef:
            {
                ScExternalRefCache::TokenRef pToken;
                PopExternalSingleRef(pToken);
                if (nGlobalError != FormulaError::NONE)
                    bOk = false;
                else
                {
                    if ( pToken->GetType() == svString )
                        pPar->PutString( pToken->GetString().getString() );
                    else if ( pToken->GetType() == svDouble )
                        pPar->PutDouble( pToken->GetDouble() );
                    else
                    {
                        SetError( FormulaError::IllegalArgument );
                        bOk = false;
                    }
                }
            }
            break;
            case svSingleRef:
            {
                ScAddress aAdr;
                PopSingleRef( aAdr );
                if ( bUseVBAObjects )
                {
                    ScRange aRange( aAdr );
                    bOk = lcl_setVBARange( aRange, mrDoc, pPar );
                }
                else
                {
                    bOk = SetSbxVariable( pPar, aAdr );
                }
            }
            break;
            case svDoubleRef:
            {
                SCCOL nCol1;
                SCROW nRow1;
                SCTAB nTab1;
                SCCOL nCol2;
                SCROW nRow2;
                SCTAB nTab2;
                PopDoubleRef( nCol1, nRow1, nTab1, nCol2, nRow2, nTab2 );
                if( nTab1 != nTab2 )
                {
                    SetError( FormulaError::IllegalParameter );
                    bOk = false;
                }
                else
                {
                    if ( bUseVBAObjects )
                    {
                        ScRange aRange( nCol1, nRow1, nTab1, nCol2, nRow2, nTab2 );
                        bOk = lcl_setVBARange( aRange, mrDoc, pPar );
                    }
                    else
                    {
                        SbxDimArrayRef refArray = new SbxDimArray;
                        refArray->AddDim(1, nRow2 - nRow1 + 1);
                        refArray->AddDim(1, nCol2 - nCol1 + 1);
                        ScAddress aAdr( nCol1, nRow1, nTab1 );
                        for( SCROW nRow = nRow1; bOk && nRow <= nRow2; nRow++ )
                        {
                            aAdr.SetRow( nRow );
                            sal_Int32 nIdx[ 2 ];
                            nIdx[ 0 ] = nRow-nRow1+1;
                            for( SCCOL nCol = nCol1; bOk && nCol <= nCol2; nCol++ )
                            {
                                aAdr.SetCol( nCol );
                                nIdx[ 1 ] = nCol-nCol1+1;
                                SbxVariable* p = refArray->Get(nIdx);
                                bOk = SetSbxVariable( p, aAdr );
                            }
                        }
                        pPar->PutObject( refArray.get() );
                    }
                }
            }
            break;
            case svExternalDoubleRef:
            case svMatrix:
            {
                ScMatrixRef pMat = GetMatrix();
                SCSIZE nC, nR;
                if (pMat && nGlobalError == FormulaError::NONE)
                {
                    pMat->GetDimensions(nC, nR);
                    SbxDimArrayRef refArray = new SbxDimArray;
                    refArray->AddDim(1, static_cast<sal_Int32>(nR));
                    refArray->AddDim(1, static_cast<sal_Int32>(nC));
                    for( SCSIZE nMatRow = 0; nMatRow < nR; nMatRow++ )
                    {
                        sal_Int32 nIdx[ 2 ];
                        nIdx[ 0 ] = static_cast<sal_Int32>(nMatRow+1);
                        for( SCSIZE nMatCol = 0; nMatCol < nC; nMatCol++ )
                        {
                            nIdx[ 1 ] = static_cast<sal_Int32>(nMatCol+1);
                            SbxVariable* p = refArray->Get(nIdx);
                            if (pMat->IsStringOrEmpty(nMatCol, nMatRow))
                            {
                                p->PutString( pMat->GetString(nMatCol, nMatRow).getString() );
                            }
                            else
                            {
                                p->PutDouble( pMat->GetDouble(nMatCol, nMatRow));
                            }
                        }
                    }
                    pPar->PutObject( refArray.get() );
                }
                else
                {
                    SetError( FormulaError::IllegalParameter );
                }
            }
            break;
            default:
                SetError( FormulaError::IllegalParameter );
                bOk = false;
        }
    }
    if( bOk )
    {
        mrDoc.LockTable( aPos.Tab() );
        SbxVariableRef refRes = new SbxVariable;
        mrDoc.IncMacroInterpretLevel();
        ErrCode eRet = pDocSh->CallBasic( aMacroStr, aBasicStr, refPar.get(), refRes.get() );
        mrDoc.DecMacroInterpretLevel();
        mrDoc.UnlockTable( aPos.Tab() );

        ScMacroManager* pMacroMgr = mrDoc.GetMacroManager();
        if (pMacroMgr)
        {
            bVolatileMacro = pMacroMgr->GetUserFuncVolatile( pMethod->GetName() );
            pMacroMgr->AddDependentCell(pModule->GetName(), pMyFormulaCell);
        }

        double fVal;
        SbxDataType eResType = refRes->GetType();
        if( SbxBase::GetError() )
        {
            SetError( FormulaError::NoValue);
        }
        if ( eRet != ERRCODE_NONE )
        {
            PushNoValue();
        }
        else if (lcl_isNumericResult( fVal, refRes.get()))
        {
            switch (eResType)
            {
                case SbxDATE:
                    nFuncFmtType = SvNumFormatType::DATE;
                break;
                case SbxBOOL:
                    nFuncFmtType = SvNumFormatType::LOGICAL;
                break;
                // Do not add SbxCURRENCY, we don't know which currency.
                default:
                    ;   // nothing
            }
            PushDouble( fVal );
        }
        else if ( eResType & SbxARRAY )
        {
            SbxBase* pElemObj = refRes->GetObject();
            SbxDimArray* pDimArray = dynamic_cast<SbxDimArray*>(pElemObj);
            sal_Int32 nDim = pDimArray ? pDimArray->GetDims() : 0;
            if ( 1 <= nDim && nDim <= 2 )
            {
                sal_Int32 nCs, nCe, nRs;
                SCSIZE nC, nR;
                SCCOL nColIdx;
                SCROW nRowIdx;
                if ( nDim == 1 )
                {   // array( cols )  one line, several columns
                    pDimArray->GetDim(1, nCs, nCe);
                    nC = static_cast<SCSIZE>(nCe - nCs + 1);
                    nRs = 0;
                    nR = 1;
                    nColIdx = 0;
                    nRowIdx = 1;
                }
                else
                {   // array( rows, cols )
                    sal_Int32 nRe;
                    pDimArray->GetDim(1, nRs, nRe);
                    nR = static_cast<SCSIZE>(nRe - nRs + 1);
                    pDimArray->GetDim(2, nCs, nCe);
                    nC = static_cast<SCSIZE>(nCe - nCs + 1);
                    nColIdx = 1;
                    nRowIdx = 0;
                }
                ScMatrixRef pMat = GetNewMat( nC, nR, /*bEmpty*/true);
                if ( pMat )
                {
                    SbxVariable* pV;
                    for ( SCSIZE j=0; j < nR; j++ )
                    {
                        sal_Int32 nIdx[ 2 ];
                        //  in one-dimensional array( cols )  nIdx[1]
                        // from SbxDimArray::Get is ignored
                        nIdx[ nRowIdx ] = nRs + static_cast<sal_Int32>(j);
                        for ( SCSIZE i=0; i < nC; i++ )
                        {
                            nIdx[ nColIdx ] = nCs + static_cast<sal_Int32>(i);
                            pV = pDimArray->Get(nIdx);
                            if ( lcl_isNumericResult( fVal, pV) )
                            {
                                pMat->PutDouble( fVal, i, j );
                            }
                            else
                            {
                                pMat->PutString(mrStrPool.intern(pV->GetOUString()), i, j);
                            }
                        }
                    }
                    PushMatrix( pMat );
                }
                else
                {
                    PushIllegalArgument();
                }
            }
            else
            {
                PushNoValue();
            }
        }
        else
        {
            PushString( refRes->GetOUString() );
        }
    }

    if (bVolatileMacro && meVolatileType == NOT_VOLATILE)
        meVolatileType = VOLATILE_MACRO;
#endif
}

#if HAVE_FEATURE_SCRIPTING

bool ScInterpreter::SetSbxVariable( SbxVariable* pVar, const ScAddress& rPos )
{
    bool bOk = true;
    ScRefCellValue aCell(mrDoc, rPos);
    if (!aCell.isEmpty())
    {
        FormulaError nErr;
        double nVal;
        switch (aCell.getType())
        {
            case CELLTYPE_VALUE :
                nVal = GetValueCellValue(rPos, aCell.getDouble());
                pVar->PutDouble( nVal );
            break;
            case CELLTYPE_STRING :
            case CELLTYPE_EDIT :
                pVar->PutString(aCell.getString(&mrDoc));
            break;
            case CELLTYPE_FORMULA :
                nErr = aCell.getFormula()->GetErrCode();
                if( nErr == FormulaError::NONE )
                {
                    if (aCell.getFormula()->IsValue())
                    {
                        nVal = aCell.getFormula()->GetValue();
                        pVar->PutDouble( nVal );
                    }
                    else
                        pVar->PutString(aCell.getFormula()->GetString().getString());
                }
                else
                {
                    SetError( nErr );
                    bOk = false;
                }
                break;
            default :
                pVar->PutEmpty();
        }
    }
    else
        pVar->PutEmpty();

    return bOk;
}

#endif

void ScInterpreter::ScTableOp()
{
    sal_uInt8 nParamCount = GetByte();
    if (nParamCount != 3 && nParamCount != 5)
    {
        PushIllegalParameter();
        return;
    }
    ScInterpreterTableOpParams aTableOp;
    if (nParamCount == 5)
    {
        PopSingleRef( aTableOp.aNew2 );
        PopSingleRef( aTableOp.aOld2 );
    }
    PopSingleRef( aTableOp.aNew1 );
    PopSingleRef( aTableOp.aOld1 );
    PopSingleRef( aTableOp.aFormulaPos );

    aTableOp.bValid = true;
    mrDoc.m_TableOpList.push_back(&aTableOp);
    mrDoc.IncInterpreterTableOpLevel();

    bool bReuseLastParams = (mrDoc.aLastTableOpParams == aTableOp);
    if ( bReuseLastParams )
    {
        aTableOp.aNotifiedFormulaPos = mrDoc.aLastTableOpParams.aNotifiedFormulaPos;
        aTableOp.bRefresh = true;
        for ( const auto& rPos : aTableOp.aNotifiedFormulaPos )
        {   // emulate broadcast and indirectly collect cell pointers
            ScRefCellValue aCell(mrDoc, rPos);
            if (aCell.getType() == CELLTYPE_FORMULA)
                aCell.getFormula()->SetTableOpDirty();
        }
    }
    else
    {   // broadcast and indirectly collect cell pointers and positions
        mrDoc.SetTableOpDirty( ScRange(aTableOp.aOld1) );
        if ( nParamCount == 5 )
            mrDoc.SetTableOpDirty( ScRange(aTableOp.aOld2) );
    }
    aTableOp.bCollectNotifications = false;

    ScRefCellValue aCell(mrDoc, aTableOp.aFormulaPos);
    if (aCell.getType() == CELLTYPE_FORMULA)
        aCell.getFormula()->SetDirtyVar();
    if (aCell.hasNumeric())
    {
        PushDouble(GetCellValue(aTableOp.aFormulaPos, aCell));
    }
    else
    {
        svl::SharedString aCellString;
        GetCellString(aCellString, aCell);
        PushString( aCellString );
    }

    auto const itr =
        ::std::find(mrDoc.m_TableOpList.begin(), mrDoc.m_TableOpList.end(), &aTableOp);
    if (itr != mrDoc.m_TableOpList.end())
    {
        mrDoc.m_TableOpList.erase(itr);
    }

    // set dirty again once more to be able to recalculate original
    for ( const auto& pCell : aTableOp.aNotifiedFormulaCells )
    {
        pCell->SetTableOpDirty();
    }

    // save these params for next incarnation
    if ( !bReuseLastParams )
        mrDoc.aLastTableOpParams = aTableOp;

    if (aCell.getType() == CELLTYPE_FORMULA)
    {
        aCell.getFormula()->SetDirtyVar();
        aCell.getFormula()->GetErrCode();     // recalculate original
    }

    // Reset all dirty flags so next incarnation does really collect all cell
    // pointers during notifications and not just non-dirty ones, which may
    // happen if a formula cell is used by more than one TableOp block.
    for ( const auto& pCell : aTableOp.aNotifiedFormulaCells )
    {
        pCell->ResetTableOpDirtyVar();
    }

    mrDoc.DecInterpreterTableOpLevel();
}

void ScInterpreter::ScDBArea()
{
    ScDBData* pDBData = mrDoc.GetDBCollection()->getNamedDBs().findByIndex(pCur->GetIndex());
    if (pDBData)
    {
        ScComplexRefData aRefData;
        aRefData.InitFlags();
        ScRange aRange;
        pDBData->GetArea(aRange);
        aRange.aEnd.SetTab(aRange.aStart.Tab());
        aRefData.SetRange(mrDoc.GetSheetLimits(), aRange, aPos);
        PushTempToken( new ScDoubleRefToken( mrDoc.GetSheetLimits(), aRefData ) );
    }
    else
        PushError( FormulaError::NoName);
}

void ScInterpreter::ScColRowNameAuto()
{
    ScComplexRefData aRefData( *pCur->GetDoubleRef() );
    ScRange aAbs = aRefData.toAbs(mrDoc, aPos);
    if (!mrDoc.ValidRange(aAbs))
    {
        PushError( FormulaError::NoRef );
        return;
    }

    SCCOL nStartCol;
    SCROW nStartRow;

    // maybe remember limit by using defined ColRowNameRange
    SCCOL nCol2 = aAbs.aEnd.Col();
    SCROW nRow2 = aAbs.aEnd.Row();
    // DataArea of the first cell
    nStartCol = aAbs.aStart.Col();
    nStartRow = aAbs.aStart.Row();
    aAbs.aEnd = aAbs.aStart; // Shrink to the top-left cell.

    {
        // Expand to the data area. Only modify the end position.
        SCCOL nDACol1 = aAbs.aStart.Col(), nDACol2 = aAbs.aEnd.Col();
        SCROW nDARow1 = aAbs.aStart.Row(), nDARow2 = aAbs.aEnd.Row();
        mrDoc.GetDataArea(aAbs.aStart.Tab(), nDACol1, nDARow1, nDACol2, nDARow2, true, false);
        aAbs.aEnd.SetCol(nDACol2);
        aAbs.aEnd.SetRow(nDARow2);
    }

    // corresponds with ScCompiler::GetToken
    if ( aRefData.Ref1.IsColRel() )
    {   // ColName
        aAbs.aEnd.SetCol(nStartCol);
        // maybe get previous limit by using defined ColRowNameRange
        if (aAbs.aEnd.Row() > nRow2)
            aAbs.aEnd.SetRow(nRow2);
        if ( aPos.Col() == nStartCol )
        {
            SCROW nMyRow = aPos.Row();
            if ( nStartRow <= nMyRow && nMyRow <= aAbs.aEnd.Row())
            {   //Formula in the same column and within the range
                if ( nMyRow == nStartRow )
                {   // take the rest under the name
                    nStartRow++;
                    if ( nStartRow > mrDoc.MaxRow() )
                        nStartRow = mrDoc.MaxRow();
                    aAbs.aStart.SetRow(nStartRow);
                }
                else
                {   // below the name to the formula cell
                    aAbs.aEnd.SetRow(nMyRow - 1);
                }
            }
        }
    }
    else
    {   // RowName
        aAbs.aEnd.SetRow(nStartRow);
        // maybe get previous limit by using defined ColRowNameRange
        if (aAbs.aEnd.Col() > nCol2)
            aAbs.aEnd.SetCol(nCol2);
        if ( aPos.Row() == nStartRow )
        {
            SCCOL nMyCol = aPos.Col();
            if (nStartCol <= nMyCol && nMyCol <= aAbs.aEnd.Col())
            {   //Formula in the same column and within the range
                if ( nMyCol == nStartCol )
                {    // take the rest under the name
                    nStartCol++;
                    if ( nStartCol > mrDoc.MaxCol() )
                        nStartCol = mrDoc.MaxCol();
                    aAbs.aStart.SetCol(nStartCol);
                }
                else
                {   // below the name to the formula cell
                    aAbs.aEnd.SetCol(nMyCol - 1);
                }
            }
        }
    }
    aRefData.SetRange(mrDoc.GetSheetLimits(), aAbs, aPos);
    PushTempToken( new ScDoubleRefToken( mrDoc.GetSheetLimits(), aRefData ) );
}

// --- internals ------------------------------------------------------------

void ScInterpreter::ScTTT()
{   // temporary test, testing functions etc.
    sal_uInt8 nParamCount = GetByte();
    // do something, count down nParamCount with Pops!

    // clean up Stack
    while ( nParamCount-- > 0)
        Pop();
    PushError(FormulaError::NoValue);
}

ScInterpreter::ScInterpreter( ScFormulaCell* pCell, ScDocument& rDoc, ScInterpreterContext& rContext,
        const ScAddress& rPos, ScTokenArray& r, bool bForGroupThreading )
    : aCode(r)
    , aPos(rPos)
    , pArr(&r)
    , mrContext(rContext)
    , mrDoc(rDoc)
    , mpLinkManager(rDoc.GetLinkManager())
    , mrStrPool(rDoc.GetSharedStringPool())
    , pJumpMatrix(nullptr)
    , pMyFormulaCell(pCell)
    , pCur(nullptr)
    , nGlobalError(FormulaError::NONE)
    , sp(0)
    , maxsp(0)
    , nFuncFmtIndex(0)
    , nCurFmtIndex(0)
    , nRetFmtIndex(0)
    , nFuncFmtType(SvNumFormatType::ALL)
    , nCurFmtType(SvNumFormatType::ALL)
    , nRetFmtType(SvNumFormatType::ALL)
    , mnStringNoValueError(FormulaError::NoValue)
    , mnSubTotalFlags(SubtotalFlags::NONE)
    , cPar(0)
    , bCalcAsShown(rDoc.GetDocOptions().IsCalcAsShown())
    , meVolatileType(r.IsRecalcModeAlways() ? VOLATILE : NOT_VOLATILE)
{
    MergeCalcConfig();

    if(pMyFormulaCell)
    {
        ScMatrixMode cMatFlag = pMyFormulaCell->GetMatrixFlag();
        bMatrixFormula = ( cMatFlag == ScMatrixMode::Formula );
    }
    else
        bMatrixFormula = false;

    // Let's not use the global stack while formula-group-threading.
    // as it complicates its life-cycle mgmt since for threading formula-groups,
    // ScInterpreter is preallocated (in main thread) for each worker thread.
    if (!bGlobalStackInUse && !bForGroupThreading)
    {
        bGlobalStackInUse = true;
        if (!pGlobalStack)
            pGlobalStack.reset(new ScTokenStack);
        pStackObj = pGlobalStack.get();
    }
    else
    {
        pStackObj = new ScTokenStack;
    }
    pStack = pStackObj->pPointer;
}

ScInterpreter::~ScInterpreter()
{
    if ( pStackObj == pGlobalStack.get() )
        bGlobalStackInUse = false;
    else
        delete pStackObj;
}

void ScInterpreter::Init( ScFormulaCell* pCell, const ScAddress& rPos, ScTokenArray& rTokArray )
{
    aCode.ReInit(rTokArray);
    aPos = rPos;
    pArr = &rTokArray;
    pJumpMatrix = nullptr;
    DropTokenCaches();
    pMyFormulaCell = pCell;
    pCur = nullptr;
    nGlobalError = FormulaError::NONE;
    sp = 0;
    maxsp = 0;
    nFuncFmtIndex = 0;
    nCurFmtIndex = 0;
    nRetFmtIndex = 0;
    nFuncFmtType = SvNumFormatType::ALL;
    nCurFmtType = SvNumFormatType::ALL;
    nRetFmtType = SvNumFormatType::ALL;
    mnStringNoValueError = FormulaError::NoValue;
    mnSubTotalFlags = SubtotalFlags::NONE;
    cPar = 0;
}

void ScInterpreter::DropTokenCaches()
{
    xResult = nullptr;
    maTokenMatrixMap.clear();
}

ScCalcConfig& ScInterpreter::GetOrCreateGlobalConfig()
{
    if (!mpGlobalConfig)
        mpGlobalConfig = new ScCalcConfig();
    return *mpGlobalConfig;
}

void ScInterpreter::SetGlobalConfig(const ScCalcConfig& rConfig)
{
    GetOrCreateGlobalConfig() = rConfig;
}

const ScCalcConfig& ScInterpreter::GetGlobalConfig()
{
    return GetOrCreateGlobalConfig();
}

void ScInterpreter::MergeCalcConfig()
{
    maCalcConfig = GetOrCreateGlobalConfig();
    maCalcConfig.MergeDocumentSpecific( mrDoc.GetCalcConfig());
}

void ScInterpreter::GlobalExit()
{
    OSL_ENSURE(!bGlobalStackInUse, "who is still using the TokenStack?");
    pGlobalStack.reset();
}

namespace {

double applyImplicitIntersection(const sc::RangeMatrix& rMat, const ScAddress& rPos)
{
    if (rMat.mnRow1 <= rPos.Row() && rPos.Row() <= rMat.mnRow2 && rMat.mnCol1 == rMat.mnCol2)
    {
        SCROW nOffset = rPos.Row() - rMat.mnRow1;
        return rMat.mpMat->GetDouble(0, nOffset);
    }

    if (rMat.mnCol1 <= rPos.Col() && rPos.Col() <= rMat.mnCol2 && rMat.mnRow1 == rMat.mnRow2)
    {
        SCROW nOffset = rPos.Col() - rMat.mnCol1;
        return rMat.mpMat->GetDouble(nOffset, 0);
    }

    return std::numeric_limits<double>::quiet_NaN();
}

// Test for Functions that evaluate an error code and directly set nGlobalError to 0
bool IsErrFunc(OpCode oc)
{
    switch (oc)
    {
        case ocCount :
        case ocCount2 :
        case ocErrorType :
        case ocIsEmpty :
        case ocIsErr :
        case ocIsError :
        case ocIsFormula :
        case ocIsLogical :
        case ocIsNA :
        case ocIsNonString :
        case ocIsRef :
        case ocIsString :
        case ocIsValue :
        case ocN :
        case ocType :
        case ocIfError :
        case ocIfNA :
        case ocErrorType_ODF :
        case ocAggregate:       // may ignore errors depending on option
        case ocIfs_MS:
        case ocSwitch_MS:
        case ocXLookup:
            return true;
        default:
            return false;
    }
}

} //namespace

StackVar ScInterpreter::Interpret()
{
    SvNumFormatType nRetTypeExpr = SvNumFormatType::UNDEFINED;
    sal_uInt32 nRetIndexExpr = 0;
    sal_uInt16 nErrorFunction = 0;
    sal_uInt16 nErrorFunctionCount = 0;
    std::vector<sal_uInt16> aErrorFunctionStack;
    sal_uInt16 nStackBase;

    nGlobalError = FormulaError::NONE;
    nStackBase = sp = maxsp = 0;
    nRetFmtType = SvNumFormatType::UNDEFINED;
    nFuncFmtType = SvNumFormatType::UNDEFINED;
    nFuncFmtIndex = nCurFmtIndex = nRetFmtIndex = 0;
    xResult = nullptr;
    pJumpMatrix = nullptr;
    mnSubTotalFlags = SubtotalFlags::NONE;
    ScTokenMatrixMap::const_iterator aTokenMatrixMapIter;

    // Once upon a time we used to have FP exceptions on, and there was a
    // Windows printer driver that kept switching off exceptions, so we had to
    // switch them back on again every time. Who knows if there isn't a driver
    // that keeps switching exceptions on, now that we run with exceptions off,
    // so reassure exceptions are really off.
    SAL_MATH_FPEXCEPTIONS_OFF();

    OpCode eOp = ocNone;
    aCode.Reset();
    for (;;)
    {
        pCur = aCode.Next();
        if (!pCur || (nGlobalError != FormulaError::NONE && nErrorFunction > nErrorFunctionCount) )
            break;
        eOp = pCur->GetOpCode();
        cPar = pCur->GetByte();
        if ( eOp == ocPush )
        {
            // RPN code push without error
            PushWithoutError( *pCur );
            nCurFmtType = SvNumFormatType::UNDEFINED;
        }
        else
        {
            const bool bIsOpCodeJumpCommand = FormulaCompiler::IsOpCodeJumpCommand(eOp);
            if (!bIsOpCodeJumpCommand &&
               ((aTokenMatrixMapIter = maTokenMatrixMap.find( pCur)) !=
                maTokenMatrixMap.end()) &&
               (*aTokenMatrixMapIter).second->GetType() != svJumpMatrix)
            {
                // Path already calculated, reuse result.
                const sal_uInt8 nParamCount = pCur->GetParamCount();
                if (sp >= nParamCount)
                    nStackBase = sp - nParamCount;
                else
                {
                    SAL_WARN("sc.core", "Stack anomaly with calculated path at "
                            << aPos.Tab() << "," << aPos.Col() << "," << aPos.Row()
                            << "  " << aPos.Format(
                                ScRefFlags::VALID | ScRefFlags::FORCE_DOC | ScRefFlags::TAB_3D, &mrDoc)
                            << "  eOp: " << static_cast<int>(eOp)
                            << "  params: " << static_cast<int>(nParamCount)
                            << "  nStackBase: " << nStackBase << "  sp: " << sp);
                    nStackBase = sp;
                    assert(!"underflow");
                }
                sp = nStackBase;
                PushTokenRef( (*aTokenMatrixMapIter).second);
            }
            else
            {
                // previous expression determines the current number format
                nCurFmtType = nRetTypeExpr;
                nCurFmtIndex = nRetIndexExpr;
                // default function's format, others are set if needed
                nFuncFmtType = SvNumFormatType::NUMBER;
                nFuncFmtIndex = 0;

                if (bIsOpCodeJumpCommand)
                    nStackBase = sp;        // don't mess around with the jumps
                else
                {
                    // Convert parameters to matrix if in array/matrix formula and
                    // parameters of function indicate doing so. Create JumpMatrix
                    // if necessary.
                    if ( MatrixParameterConversion() )
                    {
                        eOp = ocNone;       // JumpMatrix created
                        nStackBase = sp;
                    }
                    else
                    {
                        const sal_uInt8 nParamCount = pCur->GetParamCount();
                        if (sp >= nParamCount)
                            nStackBase = sp - nParamCount;
                        else
                        {
                            SAL_WARN("sc.core", "Stack anomaly at " << aPos.Tab() << "," << aPos.Col() << "," << aPos.Row()
                                    << "  " << aPos.Format(
                                        ScRefFlags::VALID | ScRefFlags::FORCE_DOC | ScRefFlags::TAB_3D, &mrDoc)
                                    << "  eOp: " << static_cast<int>(eOp)
                                    << "  params: " << static_cast<int>(nParamCount)
                                    << "  nStackBase: " << nStackBase << "  sp: " << sp);
                            nStackBase = sp;
                            assert(!"underflow");
                        }
                    }
                }

                switch( eOp )
                {
                    case ocSep:
                    case ocClose:           // pushed by the compiler
                    case ocMissing          : ScMissing();                  break;
                    case ocMacro            : ScMacro();                    break;
                    case ocDBArea           : ScDBArea();                   break;
                    case ocColRowNameAuto   : ScColRowNameAuto();           break;
                    case ocIf               : ScIfJump();                   break;
                    case ocIfError          : ScIfError( false );           break;
                    case ocIfNA             : ScIfError( true );            break;
                    case ocChoose           : ScChooseJump();               break;
                    case ocChooseCols       : ScChooseCols();               break;
                    case ocChooseRows       : ScChooseRows();               break;
                    case ocAdd              : ScAdd();                      break;
                    case ocSub              : ScSub();                      break;
                    case ocMul              : ScMul();                      break;
                    case ocDiv              : ScDiv();                      break;
                    case ocAmpersand        : ScAmpersand();                break;
                    case ocPow              : ScPow();                      break;
                    case ocEqual            : ScEqual();                    break;
                    case ocNotEqual         : ScNotEqual();                 break;
                    case ocLess             : ScLess();                     break;
                    case ocGreater          : ScGreater();                  break;
                    case ocLessEqual        : ScLessEqual();                break;
                    case ocGreaterEqual     : ScGreaterEqual();             break;
                    case ocAnd              : ScAnd();                      break;
                    case ocOr               : ScOr();                       break;
                    case ocXor              : ScXor();                      break;
                    case ocIntersect        : ScIntersect();                break;
                    case ocRange            : ScRangeFunc();                break;
                    case ocUnion            : ScUnionFunc();                break;
                    case ocNot              : ScNot();                      break;
                    case ocNegSub           :
                    case ocNeg              : ScNeg();                      break;
                    case ocPercentSign      : ScPercentSign();              break;
                    case ocPi               : ScPi();                       break;
                    case ocRandom           : ScRandom();                   break;
                    case ocRandArray        : ScRandArray();                break;
                    case ocRandomNV         : ScRandom();                   break;
                    case ocRandbetweenNV    : ScRandbetween();              break;
                    case ocFilter           : ScFilter();                   break;
                    case ocSort             : ScSort();                     break;
                    case ocSortBy           : ScSortBy();                   break;
                    case ocDrop             : ScDrop();                     break;
                    case ocExpand           : ScExpand();                   break;
                    case ocHStack           : ScHStack();                   break;
                    case ocVStack           : ScVStack();                   break;
                    case ocTake             : ScTake();                     break;
                    case ocTextAfter        : ScTextAfter();                break;
                    case ocTextBefore       : ScTextBefore();               break;
                    case ocTextSplit        : ScTextSplit();                break;
                    case ocToCol            : ScToCol();                    break;
                    case ocToRow            : ScToRow();                    break;
                    case ocUnique           : ScUnique();                   break;
                    case ocLet              : ScLet();                      break;
                    case ocWrapCols         : ScWrapCols();                 break;
                    case ocWrapRows         : ScWrapRows();                 break;
                    case ocTrue             : ScTrue();                     break;
                    case ocFalse            : ScFalse();                    break;
                    case ocGetActDate       : ScGetActDate();               break;
                    case ocGetActTime       : ScGetActTime();               break;
                    case ocNotAvail         : PushError( FormulaError::NotAvailable); break;
                    case ocDeg              : ScDeg();                      break;
                    case ocRad              : ScRad();                      break;
                    case ocSin              : ScSin();                      break;
                    case ocCos              : ScCos();                      break;
                    case ocTan              : ScTan();                      break;
                    case ocCot              : ScCot();                      break;
                    case ocArcSin           : ScArcSin();                   break;
                    case ocArcCos           : ScArcCos();                   break;
                    case ocArcTan           : ScArcTan();                   break;
                    case ocArcCot           : ScArcCot();                   break;
                    case ocSinHyp           : ScSinHyp();                   break;
                    case ocCosHyp           : ScCosHyp();                   break;
                    case ocTanHyp           : ScTanHyp();                   break;
                    case ocCotHyp           : ScCotHyp();                   break;
                    case ocArcSinHyp        : ScArcSinHyp();                break;
                    case ocArcCosHyp        : ScArcCosHyp();                break;
                    case ocArcTanHyp        : ScArcTanHyp();                break;
                    case ocArcCotHyp        : ScArcCotHyp();                break;
                    case ocCosecant         : ScCosecant();                 break;
                    case ocSecant           : ScSecant();                   break;
                    case ocCosecantHyp      : ScCosecantHyp();              break;
                    case ocSecantHyp        : ScSecantHyp();                break;
                    case ocExp              : ScExp();                      break;
                    case ocLn               : ScLn();                       break;
                    case ocLog10            : ScLog10();                    break;
                    case ocSqrt             : ScSqrt();                     break;
                    case ocFact             : ScFact();                     break;
                    case ocGetYear          : ScGetYear();                  break;
                    case ocGetMonth         : ScGetMonth();                 break;
                    case ocGetDay           : ScGetDay();                   break;
                    case ocGetDayOfWeek     : ScGetDayOfWeek();             break;
                    case ocWeek             : ScGetWeekOfYear();            break;
                    case ocIsoWeeknum       : ScGetIsoWeekOfYear();         break;
                    case ocWeeknumOOo       : ScWeeknumOOo();               break;
                    case ocEasterSunday     : ScEasterSunday();             break;
                    case ocNetWorkdays      : ScNetWorkdays( false);        break;
                    case ocNetWorkdays_MS   : ScNetWorkdays( true );        break;
                    case ocWorkday_MS       : ScWorkday_MS();               break;
                    case ocGetHour          : ScGetHour();                  break;
                    case ocGetMin           : ScGetMin();                   break;
                    case ocGetSec           : ScGetSec();                   break;
                    case ocPlusMinus        : ScPlusMinus();                break;
                    case ocAbs              : ScAbs();                      break;
                    case ocInt              : ScInt();                      break;
                    case ocEven             : ScEven();                     break;
                    case ocOdd              : ScOdd();                      break;
                    case ocPhi              : ScPhi();                      break;
                    case ocGauss            : ScGauss();                    break;
                    case ocStdNormDist      : ScStdNormDist();              break;
                    case ocStdNormDist_MS   : ScStdNormDist_MS();           break;
                    case ocFisher           : ScFisher();                   break;
                    case ocFisherInv        : ScFisherInv();                break;
                    case ocIsEmpty          : ScIsEmpty();                  break;
                    case ocIsString         : ScIsString();                 break;
                    case ocIsNonString      : ScIsNonString();              break;
                    case ocIsLogical        : ScIsLogical();                break;
                    case ocType             : ScType();                     break;
                    case ocCell             : ScCell();                     break;
                    case ocIsRef            : ScIsRef();                    break;
                    case ocIsValue          : ScIsValue();                  break;
                    case ocIsFormula        : ScIsFormula();                break;
                    case ocFormula          : ScFormula();                  break;
                    case ocIsNA             : ScIsNV();                     break;
                    case ocIsErr            : ScIsErr();                    break;
                    case ocIsError          : ScIsError();                  break;
                    case ocIsEven           : ScIsEven();                   break;
                    case ocIsOdd            : ScIsOdd();                    break;
                    case ocN                : ScN();                        break;
                    case ocGetDateValue     : ScGetDateValue();             break;
                    case ocGetTimeValue     : ScGetTimeValue();             break;
                    case ocCode             : ScCode();                     break;
                    case ocTrim             : ScTrim();                     break;
                    case ocUpper            : ScUpper();                    break;
                    case ocProper           : ScProper();                   break;
                    case ocLower            : ScLower();                    break;
                    case ocLen              : ScLen();                      break;
                    case ocT                : ScT();                        break;
                    case ocClean            : ScClean();                    break;
                    case ocValue            : ScValue();                    break;
                    case ocNumberValue      : ScNumberValue();              break;
                    case ocChar             : ScChar();                     break;
                    case ocArcTan2          : ScArcTan2();                  break;
                    case ocMod              : ScMod();                      break;
                    case ocPower            : ScPower();                    break;
                    case ocRound            : ScRound();                    break;
                    case ocRoundSig         : ScRoundSignificant();         break;
                    case ocRoundUp          : ScRoundUp();                  break;
                    case ocTrunc            :
                    case ocRoundDown        : ScRoundDown();                break;
                    case ocCeil             : ScCeil( true );               break;
                    case ocCeil_MS          : ScCeil_MS();                  break;
                    case ocCeil_Precise     :
                    case ocCeil_ISO         : ScCeil_Precise();             break;
                    case ocCeil_Math        : ScCeil( false );              break;
                    case ocFloor            : ScFloor( true );              break;
                    case ocFloor_MS         : ScFloor_MS();                 break;
                    case ocFloor_Precise    : ScFloor_Precise();            break;
                    case ocFloor_Math       : ScFloor( false );             break;
                    case ocSumProduct       : ScSumProduct();               break;
                    case ocSumSQ            : ScSumSQ();                    break;
                    case ocSumX2MY2         : ScSumX2MY2();                 break;
                    case ocSumX2DY2         : ScSumX2DY2();                 break;
                    case ocSumXMY2          : ScSumXMY2();                  break;
                    case ocRawSubtract      : ScRawSubtract();              break;
                    case ocLog              : ScLog();                      break;
                    case ocGCD              : ScGCD();                      break;
                    case ocLCM              : ScLCM();                      break;
                    case ocGetDate          : ScGetDate();                  break;
                    case ocGetTime          : ScGetTime();                  break;
                    case ocGetDiffDate      : ScGetDiffDate();              break;
                    case ocGetDiffDate360   : ScGetDiffDate360();           break;
                    case ocGetDateDif       : ScGetDateDif();               break;
                    case ocMin              : ScMin()       ;               break;
                    case ocMinA             : ScMin( true );                break;
                    case ocMax              : ScMax();                      break;
                    case ocMaxA             : ScMax( true );                break;
                    case ocSum              : ScSum();                      break;
                    case ocProduct          : ScProduct();                  break;
                    case ocNPV              : ScNPV();                      break;
                    case ocIRR              : ScIRR();                      break;
                    case ocMIRR             : ScMIRR();                     break;
                    case ocISPMT            : ScISPMT();                    break;
                    case ocAverage          : ScAverage()       ;           break;
                    case ocAverageA         : ScAverage( true );            break;
                    case ocCount            : ScCount();                    break;
                    case ocCount2           : ScCount2();                   break;
                    case ocVar              :
                    case ocVarS             : ScVar();                      break;
                    case ocVarA             : ScVar( true );                break;
                    case ocVarP             :
                    case ocVarP_MS          : ScVarP();                     break;
                    case ocVarPA            : ScVarP( true );               break;
                    case ocStDev            :
                    case ocStDevS           : ScStDev();                    break;
                    case ocStDevA           : ScStDev( true );              break;
                    case ocStDevP           :
                    case ocStDevP_MS        : ScStDevP();                   break;
                    case ocStDevPA          : ScStDevP( true );             break;
                    case ocPV               : ScPV();                       break;
                    case ocSYD              : ScSYD();                      break;
                    case ocDDB              : ScDDB();                      break;
                    case ocDB               : ScDB();                       break;
                    case ocVBD              : ScVDB();                      break;
                    case ocPDuration        : ScPDuration();                break;
                    case ocSLN              : ScSLN();                      break;
                    case ocPMT              : ScPMT();                      break;
                    case ocColumns          : ScColumns();                  break;
                    case ocRows             : ScRows();                     break;
                    case ocSheets           : ScSheets();                   break;
                    case ocColumn           : ScColumn();                   break;
                    case ocRow              : ScRow();                      break;
                    case ocSheet            : ScSheet();                    break;
                    case ocRRI              : ScRRI();                      break;
                    case ocFV               : ScFV();                       break;
                    case ocNper             : ScNper();                     break;
                    case ocRate             : ScRate();                     break;
                    case ocFilterXML        : ScFilterXML();                break;
                    case ocWebservice       : ScWebservice();               break;
                    case ocEncodeURL        : ScEncodeURL();                break;
                    case ocColor            : ScColor();                    break;
                    case ocErf_MS           : ScErf();                      break;
                    case ocErfc_MS          : ScErfc();                     break;
                    case ocIpmt             : ScIpmt();                     break;
                    case ocPpmt             : ScPpmt();                     break;
                    case ocCumIpmt          : ScCumIpmt();                  break;
                    case ocCumPrinc         : ScCumPrinc();                 break;
                    case ocEffect           : ScEffect();                   break;
                    case ocNominal          : ScNominal();                  break;
                    case ocSubTotal         : ScSubTotal();                 break;
                    case ocAggregate        : ScAggregate();                break;
                    case ocDBSum            : ScDBSum();                    break;
                    case ocDBCount          : ScDBCount();                  break;
                    case ocDBCount2         : ScDBCount2();                 break;
                    case ocDBAverage        : ScDBAverage();                break;
                    case ocDBGet            : ScDBGet();                    break;
                    case ocDBMax            : ScDBMax();                    break;
                    case ocDBMin            : ScDBMin();                    break;
                    case ocDBProduct        : ScDBProduct();                break;
                    case ocDBStdDev         : ScDBStdDev();                 break;
                    case ocDBStdDevP        : ScDBStdDevP();                break;
                    case ocDBVar            : ScDBVar();                    break;
                    case ocDBVarP           : ScDBVarP();                   break;
                    case ocIndirect         : ScIndirect();                 break;
                    case ocAddress          : ScAddressFunc();              break;
                    case ocMatch            : ScMatch();                    break;
                    case ocXMatch           : ScXMatch();                   break;
                    case ocCountEmptyCells  : ScCountEmptyCells();          break;
                    case ocCountIf          : ScCountIf();                  break;
                    case ocSumIf            : ScSumIf();                    break;
                    case ocAverageIf        : ScAverageIf();                break;
                    case ocSumIfs           : ScSumIfs();                   break;
                    case ocAverageIfs       : ScAverageIfs();               break;
                    case ocCountIfs         : ScCountIfs();                 break;
                    case ocLookup           : ScLookup();                   break;
                    case ocVLookup          : ScVLookup();                  break;
                    case ocXLookup          : ScXLookup();                  break;
                    case ocHLookup          : ScHLookup();                  break;
                    case ocIndex            : ScIndex();                    break;
                    case ocMultiArea        : ScMultiArea();                break;
                    case ocOffset           : ScOffset();                   break;
                    case ocAreas            : ScAreas();                    break;
                    case ocCurrency         : ScCurrency();                 break;
                    case ocReplace          : ScReplace();                  break;
                    case ocFixed            : ScFixed();                    break;
                    case ocFind             : ScFind();                     break;
                    case ocExact            : ScExact();                    break;
                    case ocLeft             : ScLeft();                     break;
                    case ocRight            : ScRight();                    break;
                    case ocSearch           : ScSearch();                   break;
                    case ocMid              : ScMid();                      break;
                    case ocText             : ScText();                     break;
                    case ocSubstitute       : ScSubstitute();               break;
                    case ocRegex            : ScRegex();                    break;
                    case ocRept             : ScRept();                     break;
                    case ocConcat           : ScConcat();                   break;
                    case ocConcat_MS        : ScConcat_MS();                break;
                    case ocTextJoin_MS      : ScTextJoin_MS();              break;
                    case ocIfs_MS           : ScIfs_MS();                   break;
                    case ocSwitch_MS        : ScSwitch_MS();                break;
                    case ocMinIfs_MS        : ScMinIfs_MS();                break;
                    case ocMaxIfs_MS        : ScMaxIfs_MS();                break;
                    case ocMatValue         : ScMatValue();                 break;
                    case ocMatrixUnit       : ScEMat();                     break;
                    case ocMatDet           : ScMatDet();                   break;
                    case ocMatInv           : ScMatInv();                   break;
                    case ocMatMult          : ScMatMult();                  break;
                    case ocMatSequence      : ScMatSequence();              break;
                    case ocMatTrans         : ScMatTrans();                 break;
                    case ocMatRef           : ScMatRef();                   break;
                    case ocB                : ScB();                        break;
                    case ocNormDist         : ScNormDist( 3 );              break;
                    case ocNormDist_MS      : ScNormDist( 4 );              break;
                    case ocExpDist          :
                    case ocExpDist_MS       : ScExpDist();                  break;
                    case ocBinomDist        :
                    case ocBinomDist_MS     : ScBinomDist();                break;
                    case ocPoissonDist      : ScPoissonDist( true );        break;
                    case ocPoissonDist_MS   : ScPoissonDist( false );       break;
                    case ocCombin           : ScCombin();                   break;
                    case ocCombinA          : ScCombinA();                  break;
                    case ocPermut           : ScPermut();                   break;
                    case ocPermutationA     : ScPermutationA();             break;
                    case ocHypGeomDist      : ScHypGeomDist( 4 );           break;
                    case ocHypGeomDist_MS   : ScHypGeomDist( 5 );           break;
                    case ocLogNormDist      : ScLogNormDist( 1 );           break;
                    case ocLogNormDist_MS   : ScLogNormDist( 4 );           break;
                    case ocTDist            : ScTDist();                    break;
                    case ocTDist_MS         : ScTDist_MS();                 break;
                    case ocTDist_RT         : ScTDist_T( 1 );               break;
                    case ocTDist_2T         : ScTDist_T( 2 );               break;
                    case ocFDist            :
                    case ocFDist_RT         : ScFDist();                    break;
                    case ocFDist_LT         : ScFDist_LT();                 break;
                    case ocChiDist          : ScChiDist( true );            break;
                    case ocChiDist_MS       : ScChiDist( false );           break;
                    case ocChiSqDist        : ScChiSqDist();                break;
                    case ocChiSqDist_MS     : ScChiSqDist_MS();             break;
                    case ocStandard         : ScStandard();                 break;
                    case ocAveDev           : ScAveDev();                   break;
                    case ocDevSq            : ScDevSq();                    break;
                    case ocKurt             : ScKurt();                     break;
                    case ocSkew             : ScSkew();                     break;
                    case ocSkewp            : ScSkewp();                    break;
                    case ocModalValue       : ScModalValue();               break;
                    case ocModalValue_MS    : ScModalValue_MS( true );      break;
                    case ocModalValue_Multi : ScModalValue_MS( false );     break;
                    case ocMedian           : ScMedian();                   break;
                    case ocGeoMean          : ScGeoMean();                  break;
                    case ocHarMean          : ScHarMean();                  break;
                    case ocWeibull          :
                    case ocWeibull_MS       : ScWeibull();                  break;
                    case ocBinomInv         :
                    case ocCritBinom        : ScCritBinom();                break;
                    case ocNegBinomVert     : ScNegBinomDist();             break;
                    case ocNegBinomDist_MS  : ScNegBinomDist_MS();          break;
                    case ocNoName           : ScNoName();                   break;
                    case ocBad              : ScBadName();                  break;
                    case ocZTest            :
                    case ocZTest_MS         : ScZTest();                    break;
                    case ocTTest            :
                    case ocTTest_MS         : ScTTest();                    break;
                    case ocFTest            :
                    case ocFTest_MS         : ScFTest();                    break;
                    case ocRank             :
                    case ocRank_Eq          : ScRank( false );              break;
                    case ocRank_Avg         : ScRank( true );               break;
                    case ocPercentile       :
                    case ocPercentile_Inc   : ScPercentile( true );         break;
                    case ocPercentile_Exc   : ScPercentile( false );        break;
                    case ocPercentrank      :
                    case ocPercentrank_Inc  : ScPercentrank( true );        break;
                    case ocPercentrank_Exc  : ScPercentrank( false );       break;
                    case ocLarge            : ScLarge();                    break;
                    case ocSmall            : ScSmall();                    break;
                    case ocFrequency        : ScFrequency();                break;
                    case ocQuartile         :
                    case ocQuartile_Inc     : ScQuartile( true );           break;
                    case ocQuartile_Exc     : ScQuartile( false );          break;
                    case ocNormInv          :
                    case ocNormInv_MS       : ScNormInv();                  break;
                    case ocSNormInv         :
                    case ocSNormInv_MS      : ScSNormInv();                 break;
                    case ocConfidence       :
                    case ocConfidence_N     : ScConfidence();               break;
                    case ocConfidence_T     : ScConfidenceT();              break;
                    case ocTrimMean         : ScTrimMean();                 break;
                    case ocProb             : ScProbability();              break;
                    case ocCorrel           : ScCorrel();                   break;
                    case ocCovar            :
                    case ocCovarianceP      : ScCovarianceP();              break;
                    case ocCovarianceS      : ScCovarianceS();              break;
                    case ocPearson          : ScPearson();                  break;
                    case ocRSQ              : ScRSQ();                      break;
                    case ocSTEYX            : ScSTEYX();                    break;
                    case ocSlope            : ScSlope();                    break;
                    case ocIntercept        : ScIntercept();                break;
                    case ocTrend            : ScTrend();                    break;
                    case ocGrowth           : ScGrowth();                   break;
                    case ocLinest           : ScLinest();                   break;
                    case ocLogest           : ScLogest();                   break;
                    case ocForecast_LIN     :
                    case ocForecast         : ScForecast();                   break;
                    case ocForecast_ETS_ADD : ScForecast_Ets( etsAdd );       break;
                    case ocForecast_ETS_SEA : ScForecast_Ets( etsSeason );    break;
                    case ocForecast_ETS_MUL : ScForecast_Ets( etsMult );      break;
                    case ocForecast_ETS_PIA : ScForecast_Ets( etsPIAdd );     break;
                    case ocForecast_ETS_PIM : ScForecast_Ets( etsPIMult );    break;
                    case ocForecast_ETS_STA : ScForecast_Ets( etsStatAdd );   break;
                    case ocForecast_ETS_STM : ScForecast_Ets( etsStatMult );  break;
                    case ocGammaLn          :
                    case ocGammaLn_MS       : ScLogGamma();                 break;
                    case ocGamma            : ScGamma();                    break;
                    case ocGammaDist        : ScGammaDist( true );          break;
                    case ocGammaDist_MS     : ScGammaDist( false );         break;
                    case ocGammaInv         :
                    case ocGammaInv_MS      : ScGammaInv();                 break;
                    case ocChiTest          :
                    case ocChiTest_MS       : ScChiTest();                  break;
                    case ocChiInv           :
                    case ocChiInv_MS        : ScChiInv();                   break;
                    case ocChiSqInv         :
                    case ocChiSqInv_MS      : ScChiSqInv();                 break;
                    case ocTInv             :
                    case ocTInv_2T          : ScTInv( 2 );                  break;
                    case ocTInv_MS          : ScTInv( 4 );                  break;
                    case ocFInv             :
                    case ocFInv_RT          : ScFInv();                     break;
                    case ocFInv_LT          : ScFInv_LT();                  break;
                    case ocLogInv           :
                    case ocLogInv_MS        : ScLogNormInv();               break;
                    case ocBetaDist         : ScBetaDist();                 break;
                    case ocBetaDist_MS      : ScBetaDist_MS();              break;
                    case ocBetaInv          :
                    case ocBetaInv_MS       : ScBetaInv();                  break;
                    case ocFourier          : ScFourier();                  break;
                    case ocExternal         : ScExternal();                 break;
                    case ocTableOp          : ScTableOp();                  break;
                    case ocStop :                                           break;
                    case ocErrorType        : ScErrorType();                break;
                    case ocErrorType_ODF    : ScErrorType_ODF();            break;
                    case ocCurrent          : ScCurrent();                  break;
                    case ocStyle            : ScStyle();                    break;
                    case ocDde              : ScDde();                      break;
                    case ocBase             : ScBase();                     break;
                    case ocDecimal          : ScDecimal();                  break;
                    case ocConvertOOo       : ScConvertOOo();               break;
                    case ocEuroConvert      : ScEuroConvert();              break;
                    case ocRoman            : ScRoman();                    break;
                    case ocArabic           : ScArabic();                   break;
                    case ocInfo             : ScInfo();                     break;
                    case ocHyperLink        : ScHyperLink();                break;
                    case ocBahtText         : ScBahtText();                 break;
                    case ocGetPivotData     : ScGetPivotData();             break;
                    case ocJis              : ScJis();                      break;
                    case ocAsc              : ScAsc();                      break;
                    case ocLenB             : ScLenB();                     break;
                    case ocRightB           : ScRightB();                   break;
                    case ocLeftB            : ScLeftB();                    break;
                    case ocMidB             : ScMidB();                     break;
                    case ocReplaceB         : ScReplaceB();                 break;
                    case ocFindB            : ScFindB();                    break;
                    case ocSearchB          : ScSearchB();                  break;
                    case ocUnicode          : ScUnicode();                  break;
                    case ocUnichar          : ScUnichar();                  break;
                    case ocBitAnd           : ScBitAnd();                   break;
                    case ocBitOr            : ScBitOr();                    break;
                    case ocBitXor           : ScBitXor();                   break;
                    case ocBitRshift        : ScBitRshift();                break;
                    case ocBitLshift        : ScBitLshift();                break;
                    case ocTTT              : ScTTT();                      break;
                    case ocDebugVar         : ScDebugVar();                 break;
                    case ocNone : nFuncFmtType = SvNumFormatType::UNDEFINED;    break;
                    default : PushError( FormulaError::UnknownOpCode);                 break;
                }

                // If the function pushed a subroutine as result, continue with
                // execution of the subroutine.
                if (sp > nStackBase && pStack[sp-1]->GetOpCode() == ocCall)
                {
                    Pop(); continue;
                }

                if (FormulaCompiler::IsOpCodeVolatile(eOp))
                    meVolatileType = VOLATILE;

                // Remember result matrix in case it could be reused.
                if (sp && GetStackType() == svMatrix)
                    maTokenMatrixMap.emplace(pCur, pStack[sp-1]);

                // outer function determines format of an expression
                if ( nFuncFmtType != SvNumFormatType::UNDEFINED )
                {
                    nRetTypeExpr = nFuncFmtType;
                    // Inherit the format index for currency, date or time formats.
                    switch (nFuncFmtType)
                    {
                        case SvNumFormatType::CURRENCY:
                        case SvNumFormatType::DATE:
                        case SvNumFormatType::TIME:
                        case SvNumFormatType::DATETIME:
                        case SvNumFormatType::DURATION:
                            nRetIndexExpr = nFuncFmtIndex;
                        break;
                        default:
                            nRetIndexExpr = 0;
                    }
                }
            }
        }

        // Need a clean stack environment for the JumpMatrix to work.
        if (nGlobalError != FormulaError::NONE && eOp != ocPush && sp > nStackBase + 1)
        {
            // Not all functions pop all parameters in case an error is
            // generated. Clean up stack. Assumes that every function pushes a
            // result, may be arbitrary in case of error.
            FormulaConstTokenRef xLocalResult = pStack[ sp - 1 ];
            while (sp > nStackBase)
                Pop();
            PushTokenRef( xLocalResult );
        }

        bool bGotResult;
        do
        {
            bGotResult = false;
            sal_uInt8 nLevel = 0;
            if ( GetStackType( ++nLevel ) == svJumpMatrix )
                ;   // nothing
            else if ( GetStackType( ++nLevel ) == svJumpMatrix )
                ;   // nothing
            else
                nLevel = 0;
            if ( nLevel == 1 || (nLevel == 2 && aCode.IsEndOfPath()) )
            {
                if (nLevel == 1)
                    aErrorFunctionStack.push_back( nErrorFunction);
                bGotResult = JumpMatrix( nLevel );
                if (aErrorFunctionStack.empty())
                    assert(!"ScInterpreter::Interpret - aErrorFunctionStack empty in JumpMatrix context");
                else
                {
                    nErrorFunction = aErrorFunctionStack.back();
                    if (bGotResult)
                        aErrorFunctionStack.pop_back();
                }
            }
            else
                pJumpMatrix = nullptr;
        } while ( bGotResult );

        if( IsErrFunc(eOp) )
            ++nErrorFunction;

        if ( nGlobalError != FormulaError::NONE )
        {
            if ( !nErrorFunctionCount )
            {   // count of errorcode functions in formula
                FormulaTokenArrayPlainIterator aIter(*pArr);
                for ( FormulaToken* t = aIter.FirstRPN(); t; t = aIter.NextRPN() )
                {
                    if ( IsErrFunc(t->GetOpCode()) )
                        ++nErrorFunctionCount;
                }
            }
            if ( nErrorFunction >= nErrorFunctionCount )
                ++nErrorFunction;   // that's it, error => terminate
            else if (nErrorFunctionCount && sp && GetStackType() == svError)
            {
                // Clear global error if we have an individual error result, so
                // an error evaluating function can receive multiple arguments
                // and not all evaluated arguments inheriting the error.
                // This is important for at least IFS() and SWITCH() as long as
                // they are classified as error evaluating functions and not
                // implemented as short-cutting jump code paths, but also for
                // more than one evaluated argument to AGGREGATE() or COUNT()
                // that may ignore errors.
                nGlobalError = FormulaError::NONE;
            }
        }
    }

    // End: obtain result

    bool bForcedResultType;
    switch (eOp)
    {
        case ocGetDateValue:
        case ocGetTimeValue:
            // Force final result of DATEVALUE and TIMEVALUE to number type,
            // which so far was date or time for calculations.
            nRetTypeExpr = nFuncFmtType = SvNumFormatType::NUMBER;
            nRetIndexExpr = nFuncFmtIndex = 0;
            bForcedResultType = true;
        break;
        default:
            bForcedResultType = false;
    }

    if (sp == 1)
    {
        pCur = pStack[ sp-1 ];
        if( pCur->GetOpCode() == ocPush )
        {
            // An svRefList can be resolved if it a) contains just one
            // reference, or b) in array context contains an array of single
            // cell references.
            if (pCur->GetType() == svRefList)
            {
                PopRefListPushMatrixOrRef();
                pCur = pStack[ sp-1 ];
            }
            switch( pCur->GetType() )
            {
                case svEmptyCell:
                    ;   // nothing
                break;
                case svError:
                    nGlobalError = pCur->GetError();
                break;
                case svDouble :
                    {
                        // If typed, pop token to obtain type information and
                        // push a plain untyped double so the result token to
                        // be transferred to the formula cell result does not
                        // unnecessarily duplicate the information.
                        if (pCur->GetDoubleType() != 0)
                        {
                            double fVal = PopDouble();
                            if (!bForcedResultType)
                            {
                                if (nCurFmtType != nFuncFmtType)
                                    nRetIndexExpr = 0;  // carry format index only for matching type
                                nRetTypeExpr = nFuncFmtType = nCurFmtType;
                            }
                            if (nRetTypeExpr == SvNumFormatType::DURATION)
                            {
                                // Round the duration in case a wall clock time
                                // display format is used instead of a duration
                                // format. To micro seconds which then catches
                                // the converted hh:mm:ss.9999997 cases.
                                if (fVal != 0.0)
                                {
                                    fVal *= 86400.0;
                                    fVal = rtl::math::round( fVal, 6);
                                    fVal /= 86400.0;
                                }
                            }
                            PushTempToken( CreateFormulaDoubleToken( fVal));
                        }
                        if ( nFuncFmtType == SvNumFormatType::UNDEFINED )
                        {
                            nRetTypeExpr = SvNumFormatType::NUMBER;
                            nRetIndexExpr = 0;
                        }
                    }
                break;
                case svString :
                    nRetTypeExpr = SvNumFormatType::TEXT;
                    nRetIndexExpr = 0;
                break;
                case svSingleRef :
                {
                    ScAddress aAdr;
                    PopSingleRef( aAdr );
                    if( nGlobalError == FormulaError::NONE)
                        PushCellResultToken( false, aAdr, &nRetTypeExpr, &nRetIndexExpr, true);
                }
                break;
                case svRefList :
                    PopError();     // maybe #REF! takes precedence over #VALUE!
                    PushError( FormulaError::NoValue);
                break;
                case svDoubleRef :
                {
                    if ( bMatrixFormula )
                    {   // create matrix for {=A1:A5}
                        PopDoubleRefPushMatrix();
                        ScMatrixRef xMat = PopMatrix();
                        QueryMatrixType(xMat, nRetTypeExpr, nRetIndexExpr);
                    }
                    else
                    {
                        ScRange aRange;
                        PopDoubleRef( aRange );
                        ScAddress aAdr;
                        if ( nGlobalError == FormulaError::NONE && DoubleRefToPosSingleRef( aRange, aAdr))
                            PushCellResultToken( false, aAdr, &nRetTypeExpr, &nRetIndexExpr, true);
                    }
                }
                break;
                case svExternalDoubleRef:
                {
                    ScMatrixRef xMat;
                    PopExternalDoubleRef(xMat);
                    QueryMatrixType(xMat, nRetTypeExpr, nRetIndexExpr);
                }
                break;
                case svMatrix :
                {
                    sc::RangeMatrix aMat = PopRangeMatrix();
                    if (aMat.isRangeValid())
                    {
                        // This matrix represents a range reference. Apply implicit intersection.
                        double fVal = applyImplicitIntersection(aMat, aPos);
                        if (std::isnan(fVal))
                            PushNoValue();
                        else
                            PushInt(fVal);
                    }
                    else
                        // This is a normal matrix.
                        QueryMatrixType(aMat.mpMat, nRetTypeExpr, nRetIndexExpr);
                }
                break;
                case svExternalSingleRef:
                {
                    FormulaTokenRef xToken;
                    ScExternalRefCache::CellFormat aFmt;
                    PopExternalSingleRef(xToken, &aFmt);
                    if (nGlobalError != FormulaError::NONE)
                        break;

                    PushTokenRef(xToken);

                    if (aFmt.mbIsSet)
                    {
                        nFuncFmtType = aFmt.mnType;
                        nFuncFmtIndex = aFmt.mnIndex;
                    }
                }
                break;
                default :
                    SetError( FormulaError::UnknownStackVariable);
            }
        }
        else
            SetError( FormulaError::UnknownStackVariable);
    }
    else if (sp > 1)
        SetError( FormulaError::OperatorExpected);
    else
        SetError( FormulaError::NoCode);

    if (bForcedResultType || nRetTypeExpr != SvNumFormatType::UNDEFINED)
    {
        nRetFmtType = nRetTypeExpr;
        nRetFmtIndex = nRetIndexExpr;
    }
    else if( nFuncFmtType != SvNumFormatType::UNDEFINED )
    {
        nRetFmtType = nFuncFmtType;
        nRetFmtIndex = nFuncFmtIndex;
    }
    else
        nRetFmtType = SvNumFormatType::NUMBER;

    if (nGlobalError != FormulaError::NONE && GetStackType() != svError )
        PushError( nGlobalError);

    // THE final result.
    xResult = PopToken();
    if (!xResult)
        xResult = new FormulaErrorToken( FormulaError::UnknownStackVariable);

    // release tokens in expression stack
    const FormulaToken** p = pStack;
    while( maxsp-- )
        (*p++)->DecRef();

    StackVar eType = xResult->GetType();
    if (eType == svMatrix)
        // Results are immutable in case they would be reused as input for new
        // interpreters.
        xResult->GetMatrix()->SetImmutable();
    return eType;
}

void ScInterpreter::AssertFormulaMatrix()
{
    bMatrixFormula = true;
}

const svl::SharedString & ScInterpreter::GetStringResult() const
{
    return xResult->GetString();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
