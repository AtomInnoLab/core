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

#include <datauno.hxx>

#include <svl/hint.hxx>
#include <svl/numformat.hxx>
#include <svl/sharedstringpool.hxx>
#include <utility>
#include <vcl/svapp.hxx>
#include <unotools/charclass.hxx>
#include <osl/diagnose.h>

#include <com/sun/star/awt/XBitmap.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/util/SortField.hpp>
#include <com/sun/star/table/TableSortField.hpp>
#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/table/TableOrientation.hpp>
#include <com/sun/star/table/CellRangeAddress.hpp>
#include <com/sun/star/sheet/DataImportMode.hpp>
#include <com/sun/star/sheet/FilterFieldType.hpp>
#include <com/sun/star/sheet/FilterOperator2.hpp>
#include <com/sun/star/sheet/TableFilterField2.hpp>

#include <dapiuno.hxx>
#include <cellsuno.hxx>
#include <miscuno.hxx>
#include <targuno.hxx>
#include <rangeutl.hxx>
#include <dbdata.hxx>
#include <docsh.hxx>
#include <dbdocfun.hxx>
#include <unonames.hxx>
#include <globalnames.hxx>
#include <convuno.hxx>
#include <hints.hxx>
#include <attrib.hxx>
#include <dpshttab.hxx>
#include <queryentry.hxx>
#include <dputil.hxx>
#include <sortparam.hxx>
#include <dpobject.hxx>
#include <filterentries.hxx>

#include <comphelper/extract.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <svx/dataaccessdescriptor.hxx>

#include <memory>

using namespace com::sun::star;
using namespace css::sheet;

//  everything without Which-ID, map only for PropertySetInfo

static std::span<const SfxItemPropertyMapEntry> lcl_GetSubTotalPropertyMap()
{
    // some old property names are for 5.2 compatibility

    static const SfxItemPropertyMapEntry aSubTotalPropertyMap_Impl[] =
    {
        { SC_UNONAME_BINDFMT,  0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_CASE,     0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_ENABSORT, 0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_ENUSLIST, 0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_FORMATS,  0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_INSBRK,   0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_ISCASE,   0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_MAXFLD,   0,  cppu::UnoType<sal_Int32>::get(), beans::PropertyAttribute::READONLY, 0},
        { SC_UNONAME_SORTASC,  0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_ULIST,    0,  cppu::UnoType<bool>::get(),       0, 0},
        { SC_UNONAME_UINDEX,   0,  cppu::UnoType<sal_Int32>::get(), 0, 0},
        { SC_UNONAME_USINDEX,  0,  cppu::UnoType<sal_Int32>::get(), 0, 0},
    };
    return aSubTotalPropertyMap_Impl;
}

static std::span<const SfxItemPropertyMapEntry> lcl_GetFilterPropertyMap()
{
    static const SfxItemPropertyMapEntry aFilterPropertyMap_Impl[] =
    {
        { SC_UNONAME_CONTHDR,  0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_COPYOUT,  0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_ISCASE,   0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_MAXFLD,   0,  cppu::UnoType<sal_Int32>::get(),                beans::PropertyAttribute::READONLY, 0},
        { SC_UNONAME_ORIENT,   0,  cppu::UnoType<table::TableOrientation>::get(),  0, 0},
        { SC_UNONAME_OUTPOS,   0,  cppu::UnoType<table::CellAddress>::get(),       0, 0},
        { SC_UNONAME_SAVEOUT,  0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_SKIPDUP,  0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_USEREGEX, 0,  cppu::UnoType<bool>::get(),                      0, 0},
    };
    return aFilterPropertyMap_Impl;
}

static std::span<const SfxItemPropertyMapEntry> lcl_GetDBRangePropertyMap()
{
    static const SfxItemPropertyMapEntry aDBRangePropertyMap_Impl[] =
    {
        { SC_UNONAME_AUTOFLT,  0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_FLTCRT,   0,  cppu::UnoType<table::CellRangeAddress>::get(),  0, 0},
        { SC_UNONAME_FROMSELECT,0, cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_ISUSER,   0,  cppu::UnoType<bool>::get(),           beans::PropertyAttribute::READONLY, 0 },
        { SC_UNONAME_KEEPFORM, 0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNO_LINKDISPBIT,  0,  cppu::UnoType<awt::XBitmap>::get(), beans::PropertyAttribute::READONLY, 0 },
        { SC_UNO_LINKDISPNAME, 0,  cppu::UnoType<OUString>::get(), beans::PropertyAttribute::READONLY, 0 },
        { SC_UNONAME_MOVCELLS, 0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_REFPERIOD, 0, cppu::UnoType<sal_Int32>::get(),                0, 0},
        { SC_UNONAME_STRIPDAT, 0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_TOKENINDEX,0, cppu::UnoType<sal_Int32>::get(),     beans::PropertyAttribute::READONLY, 0 },
        { SC_UNONAME_USEFLTCRT,0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_TOTALSROW,0,  cppu::UnoType<bool>::get(),                      0, 0},
        { SC_UNONAME_CONTHDR  ,0,  cppu::UnoType<bool>::get(),                      0, 0},
    };
    return aDBRangePropertyMap_Impl;
}

SC_SIMPLE_SERVICE_INFO( ScConsolidationDescriptor, u"ScConsolidationDescriptor"_ustr, u"com.sun.star.sheet.ConsolidationDescriptor"_ustr )
SC_SIMPLE_SERVICE_INFO( ScDatabaseRangesObj, u"ScDatabaseRangesObj"_ustr, u"com.sun.star.sheet.DatabaseRanges"_ustr )
SC_SIMPLE_SERVICE_INFO( ScFilterDescriptorBase, u"ScFilterDescriptorBase"_ustr, u"com.sun.star.sheet.SheetFilterDescriptor"_ustr )
SC_SIMPLE_SERVICE_INFO( ScSubTotalDescriptorBase, u"ScSubTotalDescriptorBase"_ustr, u"com.sun.star.sheet.SubTotalDescriptor"_ustr )
SC_SIMPLE_SERVICE_INFO( ScSubTotalFieldObj, u"ScSubTotalFieldObj"_ustr, u"com.sun.star.sheet.SubTotalField"_ustr )

sheet::GeneralFunction  ScDataUnoConversion::SubTotalToGeneral( ScSubTotalFunc eSubTotal )
{
    sheet::GeneralFunction eGeneral;
    switch (eSubTotal)
    {
        case SUBTOTAL_FUNC_NONE: eGeneral = sheet::GeneralFunction_NONE;      break;
        case SUBTOTAL_FUNC_AVE:  eGeneral = sheet::GeneralFunction_AVERAGE;   break;
        case SUBTOTAL_FUNC_CNT:  eGeneral = sheet::GeneralFunction_COUNTNUMS; break;
        case SUBTOTAL_FUNC_CNT2: eGeneral = sheet::GeneralFunction_COUNT;     break;
        case SUBTOTAL_FUNC_MAX:  eGeneral = sheet::GeneralFunction_MAX;       break;
        case SUBTOTAL_FUNC_MIN:  eGeneral = sheet::GeneralFunction_MIN;       break;
        case SUBTOTAL_FUNC_PROD: eGeneral = sheet::GeneralFunction_PRODUCT;   break;
        case SUBTOTAL_FUNC_STD:  eGeneral = sheet::GeneralFunction_STDEV;     break;
        case SUBTOTAL_FUNC_STDP: eGeneral = sheet::GeneralFunction_STDEVP;    break;
        case SUBTOTAL_FUNC_SUM:  eGeneral = sheet::GeneralFunction_SUM;       break;
        case SUBTOTAL_FUNC_VAR:  eGeneral = sheet::GeneralFunction_VAR;       break;
        case SUBTOTAL_FUNC_VARP: eGeneral = sheet::GeneralFunction_VARP;      break;
        default:
            OSL_FAIL("SubTotalToGeneral: wrong enum");
            eGeneral = sheet::GeneralFunction_NONE;
            break;
    }
    return eGeneral;
}

void ScImportDescriptor::FillProperties( uno::Sequence<beans::PropertyValue>& rSeq, const ScImportParam& rParam )
{
    OSL_ENSURE( rSeq.getLength() == GetPropertyCount(), "wrong Count" );

    beans::PropertyValue* pArray = rSeq.getArray();

    sheet::DataImportMode eMode = sheet::DataImportMode_NONE;
    if ( rParam.bImport )
    {
        if ( rParam.bSql )
            eMode = sheet::DataImportMode_SQL;
        else if ( rParam.nType == ScDbQuery )
            eMode = sheet::DataImportMode_QUERY;
        else
            eMode = sheet::DataImportMode_TABLE;        // type always ScDbQuery or ScDbTable
    }

    svx::ODataAccessDescriptor aDescriptor;
    aDescriptor.setDataSource(rParam.aDBName);
    if (aDescriptor.has( svx::DataAccessDescriptorProperty::DataSource ))
    {
        pArray[0].Name = SC_UNONAME_DBNAME;
        pArray[0].Value <<= rParam.aDBName;
    }
    else if (aDescriptor.has( svx::DataAccessDescriptorProperty::ConnectionResource ))
    {
        pArray[0].Name = SC_UNONAME_CONRES;
        pArray[0].Value <<= rParam.aDBName;
    }

    pArray[1].Name = SC_UNONAME_SRCTYPE;
    pArray[1].Value <<= eMode;

    pArray[2].Name = SC_UNONAME_SRCOBJ;
    pArray[2].Value <<= rParam.aStatement;

    pArray[3].Name = SC_UNONAME_ISNATIVE;
    pArray[3].Value <<= rParam.bNative;
}

void ScImportDescriptor::FillImportParam( ScImportParam& rParam, const uno::Sequence<beans::PropertyValue>& rSeq )
{
    OUString aStrVal;
    for (const beans::PropertyValue& rProp : rSeq)
    {
        OUString aPropName(rProp.Name);

        if (aPropName == SC_UNONAME_ISNATIVE)
            rParam.bNative = ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        else if (aPropName == SC_UNONAME_DBNAME)
        {
            if ( rProp.Value >>= aStrVal )
                rParam.aDBName = aStrVal;
        }
        else if (aPropName == SC_UNONAME_CONRES)
        {
            if ( rProp.Value >>= aStrVal )
                rParam.aDBName = aStrVal;
        }
        else if (aPropName == SC_UNONAME_SRCOBJ)
        {
            if ( rProp.Value >>= aStrVal )
                rParam.aStatement = aStrVal;
        }
        else if (aPropName == SC_UNONAME_SRCTYPE)
        {
            //! test for correct enum type?
            sheet::DataImportMode eMode = static_cast<sheet::DataImportMode>(ScUnoHelpFunctions::GetEnumFromAny( rProp.Value ));
            switch (eMode)
            {
                case sheet::DataImportMode_NONE:
                    rParam.bImport = false;
                    break;
                case sheet::DataImportMode_SQL:
                    rParam.bImport = true;
                    rParam.bSql    = true;
                    break;
                case sheet::DataImportMode_TABLE:
                    rParam.bImport = true;
                    rParam.bSql    = false;
                    rParam.nType   = ScDbTable;
                    break;
                case sheet::DataImportMode_QUERY:
                    rParam.bImport = true;
                    rParam.bSql    = false;
                    rParam.nType   = ScDbQuery;
                    break;
                default:
                    OSL_FAIL("wrong mode");
                    rParam.bImport = false;
            }
        }
    }
}

void ScSortDescriptor::FillProperties( uno::Sequence<beans::PropertyValue>& rSeq, const ScSortParam& rParam )
{
    OSL_ENSURE( rSeq.getLength() == GetPropertyCount(), "wrong count" );

    beans::PropertyValue* pArray = rSeq.getArray();

    //  gather Uno values together

    table::CellAddress aOutPos;
    aOutPos.Sheet  = rParam.nDestTab;
    aOutPos.Column = rParam.nDestCol;
    aOutPos.Row    = rParam.nDestRow;

    sal_uInt16 nSortCount = 0;
    while ( nSortCount < rParam.GetSortKeyCount() && rParam.maKeyState[nSortCount].bDoSort )
        ++nSortCount;

    uno::Sequence<table::TableSortField> aFields(nSortCount);
    if (nSortCount)
    {
        table::TableSortField* pFieldArray = aFields.getArray();
        for (sal_uInt16 i=0; i<nSortCount; i++)
        {
            pFieldArray[i].Field         = rParam.maKeyState[i].nField;
            pFieldArray[i].IsAscending   = rParam.maKeyState[i].bAscending;
            pFieldArray[i].FieldType     = table::TableSortFieldType_AUTOMATIC;     // always automatic
            pFieldArray[i].IsCaseSensitive = rParam.bCaseSens;
            pFieldArray[i].CollatorLocale = rParam.aCollatorLocale;
            pFieldArray[i].CollatorAlgorithm = rParam.aCollatorAlgorithm;
        }
    }

    //  fill the sequence

    pArray[0].Name = SC_UNONAME_ISSORTCOLUMNS;
    pArray[0].Value <<= !rParam.bByRow;

    pArray[1].Name = SC_UNONAME_CONTHDR;
    pArray[1].Value <<= rParam.bHasHeader;

    pArray[2].Name = SC_UNONAME_MAXFLD;
    pArray[2].Value <<= static_cast<sal_Int32>( rParam.GetSortKeyCount() );

    pArray[3].Name = SC_UNONAME_SORTFLD;
    pArray[3].Value <<= aFields;

    pArray[4].Name = SC_UNONAME_BINDFMT;
    pArray[4].Value <<= rParam.aDataAreaExtras.mbCellFormats;

    pArray[5].Name = SC_UNONAME_COPYOUT;
    pArray[5].Value <<= !rParam.bInplace;

    pArray[6].Name = SC_UNONAME_OUTPOS;
    pArray[6].Value <<= aOutPos;

    pArray[7].Name = SC_UNONAME_ISULIST;
    pArray[7].Value <<= rParam.bUserDef;

    pArray[8].Name = SC_UNONAME_UINDEX;
    pArray[8].Value <<= static_cast<sal_Int32>( rParam.nUserIndex );
}

void ScSortDescriptor::FillSortParam( ScSortParam& rParam, const uno::Sequence<beans::PropertyValue>& rSeq )
{
    sal_Int32 nSortSize = static_cast<sal_Int32>(rParam.GetSortKeyCount());

    for (const beans::PropertyValue& rProp : rSeq)
    {
        OUString aPropName(rProp.Name);

        if (aPropName == SC_UNONAME_ORIENT)
        {
            //! test for correct enum type?
            table::TableOrientation eOrient = static_cast<table::TableOrientation>(ScUnoHelpFunctions::GetEnumFromAny( rProp.Value ));
            rParam.bByRow = ( eOrient != table::TableOrientation_COLUMNS );
        }
        else if (aPropName == SC_UNONAME_ISSORTCOLUMNS)
        {
            rParam.bByRow = !::cppu::any2bool(rProp.Value);
        }
        else if (aPropName == SC_UNONAME_CONTHDR)
            rParam.bHasHeader = ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        else if (aPropName == SC_UNONAME_MAXFLD)
        {
            sal_Int32 nVal;
            if ( (rProp.Value >>= nVal) && nVal > nSortSize )
            {
                //! specify exceptions
                //! throw lang::IllegalArgumentException();
            }
        }
        else if (aPropName == SC_UNONAME_SORTFLD)
        {
            uno::Sequence<util::SortField> aSeq;
            uno::Sequence<table::TableSortField> aNewSeq;
            if ( rProp.Value >>= aSeq )
            {
                sal_Int32 nCount = aSeq.getLength();
                sal_Int32 i;
                if ( nCount > static_cast<sal_Int32>( rParam.GetSortKeyCount() ) )
                {
                    // tdf#105301 - increase the size of the sorting keys
                    nSortSize = nCount;
                    rParam.maKeyState.resize(nCount);
                }
                const util::SortField* pFieldArray = aSeq.getConstArray();
                for (i=0; i<nCount; i++)
                {
                    rParam.maKeyState[i].nField     = static_cast<SCCOLROW>( pFieldArray[i].Field );
                    rParam.maKeyState[i].bAscending = pFieldArray[i].SortAscending;

                    // FieldType is ignored
                    rParam.maKeyState[i].bDoSort = true;
                }
                for (i=nCount; i<nSortSize; i++)
                    rParam.maKeyState[i].bDoSort = false;
            }
            else if ( rProp.Value >>= aNewSeq )
            {
                sal_Int32 nCount = aNewSeq.getLength();
                sal_Int32 i;
                if ( nCount > nSortSize )
                {
                    nCount = nSortSize;
                    rParam.maKeyState.resize(nCount);
                }
                const table::TableSortField* pFieldArray = aNewSeq.getConstArray();
                for (i=0; i<nCount; i++)
                {
                    rParam.maKeyState[i].nField     = static_cast<SCCOLROW>( pFieldArray[i].Field );
                    rParam.maKeyState[i].bAscending = pFieldArray[i].IsAscending;

                    // only one is possible, sometime we should make it possible to have different for every entry
                    rParam.bCaseSens = pFieldArray[i].IsCaseSensitive;
                    rParam.aCollatorLocale = pFieldArray[i].CollatorLocale;
                    rParam.aCollatorAlgorithm = pFieldArray[i].CollatorAlgorithm;

                    // FieldType is ignored
                    rParam.maKeyState[i].bDoSort = true;
                }
                for (i=nCount; i<nSortSize; i++)
                    rParam.maKeyState[i].bDoSort = false;
            }
        }
        else if (aPropName == SC_UNONAME_ISCASE)
        {
            rParam.bCaseSens = ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        }
        else if (aPropName == SC_UNONAME_BINDFMT)
            rParam.aDataAreaExtras.mbCellFormats = ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        else if (aPropName == SC_UNONAME_COPYOUT)
            rParam.bInplace = !ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        else if (aPropName == SC_UNONAME_OUTPOS)
        {
            table::CellAddress aAddress;
            if ( rProp.Value >>= aAddress )
            {
                rParam.nDestTab = aAddress.Sheet;
                rParam.nDestCol = static_cast<SCCOL>(aAddress.Column);
                rParam.nDestRow = static_cast<SCROW>(aAddress.Row);
            }
        }
        else if (aPropName == SC_UNONAME_ISULIST)
            rParam.bUserDef = ScUnoHelpFunctions::GetBoolFromAny( rProp.Value );
        else if (aPropName == SC_UNONAME_UINDEX)
        {
            sal_Int32 nVal = 0;
            if ( rProp.Value >>= nVal )
                rParam.nUserIndex = static_cast<sal_uInt16>(nVal);
        }
        else if (aPropName == SC_UNONAME_COLLLOC)
        {
            rProp.Value >>= rParam.aCollatorLocale;
        }
        else if (aPropName == SC_UNONAME_COLLALG)
        {
            OUString sStr;
            if ( rProp.Value >>= sStr )
                rParam.aCollatorAlgorithm = sStr;
        }
    }
}

ScSubTotalFieldObj::ScSubTotalFieldObj( ScSubTotalDescriptorBase* pDesc, sal_uInt16 nP ) :
    xParent( pDesc ),
    nPos( nP )
{
    OSL_ENSURE(pDesc, "ScSubTotalFieldObj: Parent is 0");
}

ScSubTotalFieldObj::~ScSubTotalFieldObj()
{
}

// XSubTotalField

sal_Int32 SAL_CALL ScSubTotalFieldObj::getGroupColumn()
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    xParent->GetData(aParam);

    return aParam.aGroups[nPos].nField;
}

void SAL_CALL ScSubTotalFieldObj::setGroupColumn( sal_Int32 nGroupColumn )
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    xParent->GetData(aParam);

    aParam.aGroups[nPos].nField = static_cast<SCCOL>(nGroupColumn);

    xParent->PutData(aParam);
}

uno::Sequence<sheet::SubTotalColumn> SAL_CALL ScSubTotalFieldObj::getSubTotalColumns()
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    xParent->GetData(aParam);

    SCCOL nCount = aParam.aGroups[nPos].nSubTotals;
    uno::Sequence<sheet::SubTotalColumn> aSeq(nCount);
    sheet::SubTotalColumn* pAry = aSeq.getArray();
    for (SCCOL i=0; i<nCount; i++)
    {
        pAry[i].Column = aParam.aGroups[nPos].col(i);
        pAry[i].Function = ScDataUnoConversion::SubTotalToGeneral(aParam.aGroups[nPos].func(i));
    }
    return aSeq;
}

void SAL_CALL ScSubTotalFieldObj::setSubTotalColumns(
                            const uno::Sequence<sheet::SubTotalColumn>& aSubTotalColumns )
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    xParent->GetData(aParam);

    if (aSubTotalColumns.getLength() <= SCCOL_MAX)
        aParam.aGroups[nPos].SetSubtotals(aSubTotalColumns);
    //! otherwise exception or so? (too many columns)

    xParent->PutData(aParam);
}

ScSubTotalDescriptorBase::ScSubTotalDescriptorBase() :
    aPropSet( lcl_GetSubTotalPropertyMap() )
{
}

ScSubTotalDescriptorBase::~ScSubTotalDescriptorBase()
{
}

// XSubTotalDescriptor

rtl::Reference<ScSubTotalFieldObj> ScSubTotalDescriptorBase::GetObjectByIndex_Impl(sal_uInt16 nIndex)
{
    if ( nIndex < getCount() )
        return new ScSubTotalFieldObj( this, nIndex );
    return nullptr;
}

void SAL_CALL ScSubTotalDescriptorBase::clear()
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    GetData(aParam);

    for (auto& group : aParam.aGroups)
        group.bActive = false;

    //! notify the field objects???

    PutData(aParam);
}

void SAL_CALL ScSubTotalDescriptorBase::addNew(
                        const uno::Sequence<sheet::SubTotalColumn>& aSubTotalColumns,
                        sal_Int32 nGroupColumn )
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    GetData(aParam);

    sal_uInt16 nPos = 0;
    while (nPos < MAXSUBTOTAL && aParam.aGroups[nPos].bActive)
        ++nPos;

    if (nPos >= MAXSUBTOTAL || aSubTotalColumns.getLength() > SCCOL_MAX)
        // too many fields / columns
        throw uno::RuntimeException();      // no other exceptions specified
    auto& group = aParam.aGroups[nPos];

    group.bActive = true;
    group.nField = static_cast<SCCOL>(nGroupColumn);
    group.SetSubtotals(aSubTotalColumns);

    PutData(aParam);
}

//  flags/settings as properties

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScSubTotalDescriptorBase::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.SubTotalFieldsEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScSubTotalDescriptorBase::getCount()
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    GetData(aParam);

    sal_uInt16 nCount = 0;
    while ( nCount < MAXSUBTOTAL && aParam.aGroups[nCount].bActive )
        ++nCount;
    return nCount;
}

uno::Any SAL_CALL ScSubTotalDescriptorBase::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScSubTotalFieldObj> xField(GetObjectByIndex_Impl(static_cast<sal_uInt16>(nIndex)));
    if (!xField.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<sheet::XSubTotalField>(xField));
}

uno::Type SAL_CALL ScSubTotalDescriptorBase::getElementType()
{
    return cppu::UnoType<sheet::XSubTotalField>::get();
}

sal_Bool SAL_CALL ScSubTotalDescriptorBase::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScSubTotalDescriptorBase::getPropertySetInfo()
{
    SolarMutexGuard aGuard;
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( aPropSet.getPropertyMap() ));
    return aRef;
}

void SAL_CALL ScSubTotalDescriptorBase::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    GetData(aParam);

    // some old property names are for 5.2 compatibility

    if (aPropertyName == SC_UNONAME_CASE || aPropertyName == SC_UNONAME_ISCASE )
        aParam.bCaseSens = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_FORMATS || aPropertyName == SC_UNONAME_BINDFMT )
        aParam.bIncludePattern = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_ENABSORT )
        aParam.bDoSort = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_SORTASC )
        aParam.bAscending = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_INSBRK )
        aParam.bPagebreak = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_ULIST || aPropertyName == SC_UNONAME_ENUSLIST )
        aParam.bUserDef = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_UINDEX || aPropertyName == SC_UNONAME_USINDEX )
    {
        sal_Int32 nVal = 0;
        if ( aValue >>= nVal )
            aParam.nUserIndex = static_cast<sal_uInt16>(nVal);
    }
    else if (aPropertyName == SC_UNONAME_MAXFLD )
    {
        sal_Int32 nVal = 0;
        if ( (aValue >>= nVal) && nVal > sal::static_int_cast<sal_Int32>(MAXSUBTOTAL) )
        {
            throw lang::IllegalArgumentException();
        }
    }

    PutData(aParam);
}

uno::Any SAL_CALL ScSubTotalDescriptorBase::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    ScSubTotalParam aParam;
    GetData(aParam);

    uno::Any aRet;

    // some old property names are for 5.2 compatibility

    if (aPropertyName == SC_UNONAME_CASE || aPropertyName == SC_UNONAME_ISCASE )
        aRet <<= aParam.bCaseSens;
    else if (aPropertyName == SC_UNONAME_FORMATS || aPropertyName == SC_UNONAME_BINDFMT )
        aRet <<= aParam.bIncludePattern;
    else if (aPropertyName == SC_UNONAME_ENABSORT )
        aRet <<= aParam.bDoSort;
    else if (aPropertyName == SC_UNONAME_SORTASC )
        aRet <<= aParam.bAscending;
    else if (aPropertyName == SC_UNONAME_INSBRK )
        aRet <<= aParam.bPagebreak;
    else if (aPropertyName == SC_UNONAME_ULIST || aPropertyName == SC_UNONAME_ENUSLIST )
        aRet <<= aParam.bUserDef;
    else if (aPropertyName == SC_UNONAME_UINDEX || aPropertyName == SC_UNONAME_USINDEX )
        aRet <<= static_cast<sal_Int32>(aParam.nUserIndex);
    else if (aPropertyName == SC_UNONAME_MAXFLD )
        aRet <<= sal_Int32(MAXSUBTOTAL);

    return aRet;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScSubTotalDescriptorBase )

ScSubTotalDescriptor::ScSubTotalDescriptor()
{
}

ScSubTotalDescriptor::~ScSubTotalDescriptor()
{
}

void ScSubTotalDescriptor::GetData( ScSubTotalParam& rParam ) const
{
    rParam = aStoredParam;          // query for interface
}

void ScSubTotalDescriptor::PutData( const ScSubTotalParam& rParam )
{
    aStoredParam = rParam;          // set by the interface
}

void ScSubTotalDescriptor::SetParam( const ScSubTotalParam& rNew )
{
    aStoredParam = rNew;            // set from outside
}

ScRangeSubTotalDescriptor::ScRangeSubTotalDescriptor(ScDatabaseRangeObj* pPar) :
    mxParent(pPar)
{
}

ScRangeSubTotalDescriptor::~ScRangeSubTotalDescriptor()
{
}

void ScRangeSubTotalDescriptor::GetData( ScSubTotalParam& rParam ) const
{
    if (mxParent.is())
        mxParent->GetSubTotalParam( rParam );
}

void ScRangeSubTotalDescriptor::PutData( const ScSubTotalParam& rParam )
{
    if (mxParent.is())
        mxParent->SetSubTotalParam( rParam );
}

ScConsolidationDescriptor::ScConsolidationDescriptor()
{
}

ScConsolidationDescriptor::~ScConsolidationDescriptor()
{
}

void ScConsolidationDescriptor::SetParam( const ScConsolidateParam& rNew )
{
    aParam = rNew;
}

// XConsolidationDescriptor

sheet::GeneralFunction SAL_CALL ScConsolidationDescriptor::getFunction()
{
    SolarMutexGuard aGuard;
    return ScDataUnoConversion::SubTotalToGeneral(aParam.eFunction);
}

void SAL_CALL ScConsolidationDescriptor::setFunction( sheet::GeneralFunction nFunction )
{
    SolarMutexGuard aGuard;
    aParam.eFunction = ScDPUtil::toSubTotalFunc(static_cast<ScGeneralFunction>(nFunction));
}

uno::Sequence<table::CellRangeAddress> SAL_CALL ScConsolidationDescriptor::getSources()
{
    SolarMutexGuard aGuard;
    sal_uInt16 nCount = aParam.nDataAreaCount;
    if (!aParam.pDataAreas)
        nCount = 0;
    table::CellRangeAddress aRange;
    uno::Sequence<table::CellRangeAddress> aSeq(nCount);
    table::CellRangeAddress* pAry = aSeq.getArray();
    for (sal_uInt16 i=0; i<nCount; i++)
    {
        ScArea const & rArea = aParam.pDataAreas[i];
        aRange.Sheet        = rArea.nTab;
        aRange.StartColumn  = rArea.nColStart;
        aRange.StartRow     = rArea.nRowStart;
        aRange.EndColumn    = rArea.nColEnd;
        aRange.EndRow       = rArea.nRowEnd;
        pAry[i] = aRange;
    }
    return aSeq;
}

void SAL_CALL ScConsolidationDescriptor::setSources(
                    const uno::Sequence<table::CellRangeAddress>& aSources )
{
    SolarMutexGuard aGuard;
    sal_uInt16 nCount = static_cast<sal_uInt16>(aSources.getLength());
    if (nCount)
    {
        const table::CellRangeAddress* pAry = aSources.getConstArray();
        std::unique_ptr<ScArea[]> pNew(new ScArea[nCount]);
        sal_uInt16 i;
        for (i=0; i<nCount; i++)
            pNew[i] = ScArea( pAry[i].Sheet,
                    static_cast<SCCOL>(pAry[i].StartColumn), pAry[i].StartRow,
                    static_cast<SCCOL>(pAry[i].EndColumn),   pAry[i].EndRow );

        aParam.SetAreas( std::move(pNew), nCount );    // copy everything
    }
    else
        aParam.ClearDataAreas();
}

table::CellAddress SAL_CALL ScConsolidationDescriptor::getStartOutputPosition()
{
    SolarMutexGuard aGuard;
    table::CellAddress aPos;
    aPos.Column = aParam.nCol;
    aPos.Row    = aParam.nRow;
    aPos.Sheet  = aParam.nTab;
    return aPos;
}

void SAL_CALL ScConsolidationDescriptor::setStartOutputPosition(
                                const table::CellAddress& aStartOutputPosition )
{
    SolarMutexGuard aGuard;
    aParam.nCol = static_cast<SCCOL>(aStartOutputPosition.Column);
    aParam.nRow = static_cast<SCROW>(aStartOutputPosition.Row);
    aParam.nTab = aStartOutputPosition.Sheet;
}

sal_Bool SAL_CALL ScConsolidationDescriptor::getUseColumnHeaders()
{
    SolarMutexGuard aGuard;
    return aParam.bByCol;
}

void SAL_CALL ScConsolidationDescriptor::setUseColumnHeaders( sal_Bool bUseColumnHeaders )
{
    SolarMutexGuard aGuard;
    aParam.bByCol = bUseColumnHeaders;
}

sal_Bool SAL_CALL ScConsolidationDescriptor::getUseRowHeaders()
{
    SolarMutexGuard aGuard;
    return aParam.bByRow;
}

void SAL_CALL ScConsolidationDescriptor::setUseRowHeaders( sal_Bool bUseRowHeaders )
{
    SolarMutexGuard aGuard;
    aParam.bByRow = bUseRowHeaders;
}

sal_Bool SAL_CALL ScConsolidationDescriptor::getInsertLinks()
{
    SolarMutexGuard aGuard;
    return aParam.bReferenceData;
}

void SAL_CALL ScConsolidationDescriptor::setInsertLinks( sal_Bool bInsertLinks )
{
    SolarMutexGuard aGuard;
    aParam.bReferenceData = bInsertLinks;
}

ScFilterDescriptorBase::ScFilterDescriptorBase(ScDocShell* pDocShell) :
    aPropSet( lcl_GetFilterPropertyMap() ),
    pDocSh(pDocShell)
{
    if (pDocSh)
        pDocSh->GetDocument().AddUnoObject(*this);
}

ScFilterDescriptorBase::~ScFilterDescriptorBase()
{
    SolarMutexGuard g;

    if (pDocSh)
        pDocSh->GetDocument().RemoveUnoObject(*this);
}

void ScFilterDescriptorBase::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocSh = nullptr;          // invalid
    }
}

// XSheetFilterDescriptor and XSheetFilterDescriptor2

uno::Sequence<sheet::TableFilterField> SAL_CALL ScFilterDescriptorBase::getFilterFields()
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    SCSIZE nEntries = aParam.GetEntryCount();   // allocated entries in Param
    SCSIZE nCount = 0;                          // active
    while ( nCount < nEntries &&
            aParam.GetEntry(nCount).bDoQuery )
        ++nCount;

    sheet::TableFilterField aField;
    uno::Sequence<sheet::TableFilterField> aSeq(static_cast<sal_Int32>(nCount));
    sheet::TableFilterField* pAry = aSeq.getArray();
    for (SCSIZE i=0; i<nCount; i++)
    {
        const ScQueryEntry& rEntry = aParam.GetEntry(i);
        if (rEntry.GetQueryItems().empty())
            continue;

        const ScQueryEntry::Item& rItem = rEntry.GetQueryItems().front();

        aField.Connection    = (rEntry.eConnect == SC_AND) ? sheet::FilterConnection_AND :
                                                             sheet::FilterConnection_OR;
        aField.Field         = rEntry.nField;
        aField.IsNumeric     = rItem.meType != ScQueryEntry::ByString;
        aField.StringValue   = rItem.maString.getString();
        aField.NumericValue  = rItem.mfVal;

        switch (rEntry.eOp)             // ScQueryOp
        {
            case SC_EQUAL:
                {
                    aField.Operator = sheet::FilterOperator_EQUAL;
                    if (rEntry.IsQueryByEmpty())
                    {
                        aField.Operator = sheet::FilterOperator_EMPTY;
                        aField.NumericValue = 0;
                    }
                    else if (rEntry.IsQueryByNonEmpty())
                    {
                        aField.Operator = sheet::FilterOperator_NOT_EMPTY;
                        aField.NumericValue = 0;
                    }
                }
                break;
            case SC_LESS:           aField.Operator = sheet::FilterOperator_LESS;             break;
            case SC_GREATER:        aField.Operator = sheet::FilterOperator_GREATER;          break;
            case SC_LESS_EQUAL:     aField.Operator = sheet::FilterOperator_LESS_EQUAL;   break;
            case SC_GREATER_EQUAL:  aField.Operator = sheet::FilterOperator_GREATER_EQUAL;  break;
            case SC_NOT_EQUAL:      aField.Operator = sheet::FilterOperator_NOT_EQUAL;    break;
            case SC_TOPVAL:         aField.Operator = sheet::FilterOperator_TOP_VALUES;   break;
            case SC_BOTVAL:         aField.Operator = sheet::FilterOperator_BOTTOM_VALUES;  break;
            case SC_TOPPERC:        aField.Operator = sheet::FilterOperator_TOP_PERCENT;      break;
            case SC_BOTPERC:        aField.Operator = sheet::FilterOperator_BOTTOM_PERCENT; break;
            default:
                OSL_FAIL("wrong filter enum");
                aField.Operator = sheet::FilterOperator_EMPTY;
        }
        pAry[i] = aField;
    }
    return aSeq;
}

namespace {

template<typename T>
void convertQueryEntryToUno(const ScQueryEntry& rEntry, T& rField)
{
    rField.Connection = (rEntry.eConnect == SC_AND) ? sheet::FilterConnection_AND : sheet::FilterConnection_OR;
    rField.Field = rEntry.nField;

    switch (rEntry.eOp)             // ScQueryOp
    {
    case SC_EQUAL:                  rField.Operator = sheet::FilterOperator2::EQUAL;                break;
    case SC_LESS:                   rField.Operator = sheet::FilterOperator2::LESS;                 break;
    case SC_GREATER:                rField.Operator = sheet::FilterOperator2::GREATER;              break;
    case SC_LESS_EQUAL:             rField.Operator = sheet::FilterOperator2::LESS_EQUAL;           break;
    case SC_GREATER_EQUAL:          rField.Operator = sheet::FilterOperator2::GREATER_EQUAL;        break;
    case SC_NOT_EQUAL:              rField.Operator = sheet::FilterOperator2::NOT_EQUAL;            break;
    case SC_TOPVAL:                 rField.Operator = sheet::FilterOperator2::TOP_VALUES;           break;
    case SC_BOTVAL:                 rField.Operator = sheet::FilterOperator2::BOTTOM_VALUES;        break;
    case SC_TOPPERC:                rField.Operator = sheet::FilterOperator2::TOP_PERCENT;          break;
    case SC_BOTPERC:                rField.Operator = sheet::FilterOperator2::BOTTOM_PERCENT;       break;
    case SC_CONTAINS:               rField.Operator = sheet::FilterOperator2::CONTAINS;             break;
    case SC_DOES_NOT_CONTAIN:       rField.Operator = sheet::FilterOperator2::DOES_NOT_CONTAIN;     break;
    case SC_BEGINS_WITH:            rField.Operator = sheet::FilterOperator2::BEGINS_WITH;          break;
    case SC_DOES_NOT_BEGIN_WITH:    rField.Operator = sheet::FilterOperator2::DOES_NOT_BEGIN_WITH;  break;
    case SC_ENDS_WITH:              rField.Operator = sheet::FilterOperator2::ENDS_WITH;            break;
    case SC_DOES_NOT_END_WITH:      rField.Operator = sheet::FilterOperator2::DOES_NOT_END_WITH;    break;
    default:
        OSL_FAIL("Unknown filter operator value.");
        rField.Operator = sheet::FilterOperator2::EMPTY;
    }
}

template<typename T>
void convertUnoToQueryEntry(const T& rField, ScQueryEntry& rEntry)
{
    rEntry.bDoQuery = true;
    rEntry.eConnect = (rField.Connection == sheet::FilterConnection_AND) ? SC_AND : SC_OR;
    rEntry.nField   = rField.Field;

    switch (rField.Operator)           // FilterOperator
    {
    case sheet::FilterOperator2::EQUAL:                 rEntry.eOp = SC_EQUAL;              break;
    case sheet::FilterOperator2::LESS:                  rEntry.eOp = SC_LESS;               break;
    case sheet::FilterOperator2::GREATER:               rEntry.eOp = SC_GREATER;            break;
    case sheet::FilterOperator2::LESS_EQUAL:            rEntry.eOp = SC_LESS_EQUAL;         break;
    case sheet::FilterOperator2::GREATER_EQUAL:         rEntry.eOp = SC_GREATER_EQUAL;      break;
    case sheet::FilterOperator2::NOT_EQUAL:             rEntry.eOp = SC_NOT_EQUAL;          break;
    case sheet::FilterOperator2::TOP_VALUES:            rEntry.eOp = SC_TOPVAL;             break;
    case sheet::FilterOperator2::BOTTOM_VALUES:         rEntry.eOp = SC_BOTVAL;             break;
    case sheet::FilterOperator2::TOP_PERCENT:           rEntry.eOp = SC_TOPPERC;            break;
    case sheet::FilterOperator2::BOTTOM_PERCENT:        rEntry.eOp = SC_BOTPERC;            break;
    case sheet::FilterOperator2::CONTAINS:              rEntry.eOp = SC_CONTAINS;           break;
    case sheet::FilterOperator2::DOES_NOT_CONTAIN:      rEntry.eOp = SC_DOES_NOT_CONTAIN;   break;
    case sheet::FilterOperator2::BEGINS_WITH:           rEntry.eOp = SC_BEGINS_WITH;        break;
    case sheet::FilterOperator2::DOES_NOT_BEGIN_WITH:   rEntry.eOp = SC_DOES_NOT_BEGIN_WITH;break;
    case sheet::FilterOperator2::ENDS_WITH:             rEntry.eOp = SC_ENDS_WITH;          break;
    case sheet::FilterOperator2::DOES_NOT_END_WITH:     rEntry.eOp = SC_DOES_NOT_END_WITH;  break;
    case sheet::FilterOperator2::EMPTY:
        rEntry.SetQueryByEmpty();
        break;
    case sheet::FilterOperator2::NOT_EMPTY:
        rEntry.SetQueryByNonEmpty();
        break;
    default:
        OSL_FAIL("Unknown filter operator type.");
        rEntry.eOp = SC_EQUAL;
    }
}

void fillQueryParam(
    ScQueryParam& rParam, ScDocument* pDoc,
    const uno::Sequence<sheet::TableFilterField2>& aFilterFields)
{
    size_t nCount = static_cast<size_t>(aFilterFields.getLength());
    rParam.Resize(nCount);

    const sheet::TableFilterField2* pAry = aFilterFields.getConstArray();
    svl::SharedStringPool& rPool = pDoc->GetSharedStringPool();
    for (size_t i = 0; i < nCount; ++i)
    {
        ScQueryEntry& rEntry = rParam.GetEntry(i);
        convertUnoToQueryEntry(pAry[i], rEntry);

        if (pAry[i].Operator != sheet::FilterOperator2::EMPTY && pAry[i].Operator != sheet::FilterOperator2::NOT_EMPTY)
        {
            ScQueryEntry::QueryItemsType& rItems = rEntry.GetQueryItems();
            rItems.resize(1);
            ScQueryEntry::Item& rItem = rItems.front();
            rItem.meType    = pAry[i].IsNumeric ? ScQueryEntry::ByValue : ScQueryEntry::ByString;
            rItem.mfVal     = pAry[i].NumericValue;
            rItem.maString = rPool.intern(pAry[i].StringValue);

            if (rItem.meType == ScQueryEntry::ByValue)
            {
                OUString aStr = pDoc->GetFormatTable()->GetInputLineString(rItem.mfVal, 0);
                rItem.maString = rPool.intern(aStr);
            }
        }
    }

    size_t nParamCount = rParam.GetEntryCount();    // if below eight Param isn't resized
    for (size_t i = nCount; i < nParamCount; ++i)
        rParam.GetEntry(i).bDoQuery = false;        // reset surplus fields
}

void fillQueryParam(
    ScQueryParam& rParam, ScDocument* pDoc,
    const uno::Sequence<sheet::TableFilterField3>& aFilterFields)
{
    size_t nCount = static_cast<size_t>(aFilterFields.getLength());
    rParam.Resize(nCount);

    svl::SharedStringPool& rPool = pDoc->GetSharedStringPool();
    const sheet::TableFilterField3* pAry = aFilterFields.getConstArray();
    for (size_t i = 0; i < nCount; ++i)
    {
        ScQueryEntry& rEntry = rParam.GetEntry(i);
        convertUnoToQueryEntry(pAry[i], rEntry);

        if (pAry[i].Operator != sheet::FilterOperator2::EMPTY && pAry[i].Operator != sheet::FilterOperator2::NOT_EMPTY)
        {
            ScQueryEntry::QueryItemsType& rItems = rEntry.GetQueryItems();
            rItems.clear();
            const uno::Sequence<sheet::FilterFieldValue>& rVals = pAry[i].Values;
            for (const auto& rVal : rVals)
            {
                ScQueryEntry::Item aItem;
                switch (rVal.FilterType)
                {
                    case FilterFieldType::NUMERIC:
                        aItem.meType = ScQueryEntry::ByValue;
                        break;
                    case FilterFieldType::STRING:
                        aItem.meType = ScQueryEntry::ByString;
                        break;
                    case FilterFieldType::DATE:
                        aItem.meType = ScQueryEntry::ByDate;
                        break;
                    case FilterFieldType::TEXT_COLOR:
                        aItem.meType = ScQueryEntry::ByTextColor;
                        break;
                    case FilterFieldType::BACKGROUND_COLOR:
                        aItem.meType = ScQueryEntry::ByBackgroundColor;
                        break;
                }
                aItem.mfVal    = rVal.NumericValue;
                aItem.maString = rPool.intern(rVal.StringValue);

                if (aItem.meType == ScQueryEntry::ByValue)
                {
                    OUString aStr = pDoc->GetFormatTable()->GetInputLineString(aItem.mfVal, 0);
                    aItem.maString = rPool.intern(aStr);
                }
                else if (aItem.meType == ScQueryEntry::ByTextColor
                         || aItem.meType == ScQueryEntry::ByBackgroundColor)
                {
                    aItem.maColor = Color(ColorTransparency, rVal.ColorValue);
                }

                // filter all dates starting with the given date filter YYYY or YYYY-MM and filter all datetimes
                // starting with the given datetime filter YYYY-MM-DD, YYYY-MM-DD HH, or YYYY-MM-DD HH:MM
                if( aItem.meType == ScQueryEntry::ByDate && aItem.maString.getLength() < 19 )
                {
                    ScFilterEntries aFilterEntries;
                    pDoc->GetFilterEntries(rEntry.nField, rParam.nRow1, rParam.nTab, aFilterEntries);
                    for( const auto& rFilter : aFilterEntries )
                    {
                        if( rFilter.GetString().startsWith(rVal.StringValue) )
                        {
                            aItem.maString = rPool.intern(rFilter.GetString());
                            rItems.push_back(aItem);
                        }
                    }
                }
                else
                {
                    rItems.push_back(aItem);
                }
            }
        }
    }

    size_t nParamCount = rParam.GetEntryCount();    // if below eight Param isn't resized
    for (size_t i = nCount; i < nParamCount; ++i)
        rParam.GetEntry(i).bDoQuery = false;        // reset surplus fields
}

}

uno::Sequence<sheet::TableFilterField2> SAL_CALL ScFilterDescriptorBase::getFilterFields2()
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    SCSIZE nEntries = aParam.GetEntryCount();   // allocated entries in Param
    SCSIZE nCount = 0;                          // active
    while ( nCount < nEntries &&
        aParam.GetEntry(nCount).bDoQuery )
        ++nCount;

    sheet::TableFilterField2 aField;
    uno::Sequence<sheet::TableFilterField2> aSeq(static_cast<sal_Int32>(nCount));
    sheet::TableFilterField2* pAry = aSeq.getArray();
    for (SCSIZE i=0; i<nCount; i++)
    {
        const ScQueryEntry& rEntry = aParam.GetEntry(i);
        convertQueryEntryToUno(rEntry, aField);

        bool bByEmpty = false;
        if (aField.Operator == sheet::FilterOperator2::EQUAL)
        {
            if (rEntry.IsQueryByEmpty())
            {
                aField.Operator = sheet::FilterOperator2::EMPTY;
                aField.NumericValue = 0;
                bByEmpty = true;
            }
            else if (rEntry.IsQueryByNonEmpty())
            {
                aField.Operator = sheet::FilterOperator2::NOT_EMPTY;
                aField.NumericValue = 0;
                bByEmpty = true;
            }
        }

        if (!bByEmpty && !rEntry.GetQueryItems().empty())
        {
            const ScQueryEntry::Item& rItem = rEntry.GetQueryItems().front();
            aField.IsNumeric     = rItem.meType != ScQueryEntry::ByString;
            aField.StringValue   = rItem.maString.getString();
            aField.NumericValue  = rItem.mfVal;
        }

        pAry[i] = aField;
    }
    return aSeq;
}

uno::Sequence<sheet::TableFilterField3> SAL_CALL ScFilterDescriptorBase::getFilterFields3()
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    SCSIZE nEntries = aParam.GetEntryCount();   // allocated entries in Param
    SCSIZE nCount = 0;                          // active
    while ( nCount < nEntries &&
        aParam.GetEntry(nCount).bDoQuery )
        ++nCount;

    sheet::TableFilterField3 aField;
    uno::Sequence<sheet::TableFilterField3> aSeq(static_cast<sal_Int32>(nCount));
    sheet::TableFilterField3* pAry = aSeq.getArray();
    for (SCSIZE i = 0; i < nCount; ++i)
    {
        const ScQueryEntry& rEntry = aParam.GetEntry(i);
        convertQueryEntryToUno(rEntry, aField);

        bool bByEmpty = false;
        if (aField.Operator == sheet::FilterOperator2::EQUAL)
        {
            if (rEntry.IsQueryByEmpty())
            {
                aField.Operator = sheet::FilterOperator2::EMPTY;
                aField.Values.realloc(1);
                aField.Values.getArray()[0].NumericValue = 0;
                bByEmpty = true;
            }
            else if (rEntry.IsQueryByNonEmpty())
            {
                aField.Operator = sheet::FilterOperator2::NOT_EMPTY;
                aField.Values.realloc(1);
                aField.Values.getArray()[0].NumericValue = 0;
                bByEmpty = true;
            }
        }

        if (!bByEmpty)
        {
            const ScQueryEntry::QueryItemsType& rItems = rEntry.GetQueryItems();
            size_t nItemCount = rItems.size();
            aField.Values.realloc(nItemCount);
            auto pValues = aField.Values.getArray();
            size_t j = 0;
            for (const auto& rItem : rItems)
            {
                pValues[j].IsNumeric = rItem.meType != ScQueryEntry::ByString;
                pValues[j].StringValue = rItem.maString.getString();
                pValues[j].NumericValue = rItem.mfVal;
                ++j;
            }
        }

        pAry[i] = aField;
    }
    return aSeq;
}

void SAL_CALL ScFilterDescriptorBase::setFilterFields(
                const uno::Sequence<sheet::TableFilterField>& aFilterFields )
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    SCSIZE nCount = static_cast<SCSIZE>(aFilterFields.getLength());
    aParam.Resize( nCount );

    ScDocument& rDoc = pDocSh->GetDocument();
    svl::SharedStringPool& rPool = rDoc.GetSharedStringPool();
    const sheet::TableFilterField* pAry = aFilterFields.getConstArray();
    SCSIZE i;
    for (i=0; i<nCount; i++)
    {
        ScQueryEntry& rEntry = aParam.GetEntry(i);
        ScQueryEntry::QueryItemsType& rItems = rEntry.GetQueryItems();
        rItems.resize(1);
        ScQueryEntry::Item& rItem = rItems.front();
        rEntry.bDoQuery = true;
        rEntry.eConnect = (pAry[i].Connection == sheet::FilterConnection_AND) ? SC_AND : SC_OR;
        rEntry.nField   = pAry[i].Field;
        rItem.meType    = pAry[i].IsNumeric ? ScQueryEntry::ByValue : ScQueryEntry::ByString;
        rItem.mfVal     = pAry[i].NumericValue;
        rItem.maString = rPool.intern(pAry[i].StringValue);

        if (rItem.meType != ScQueryEntry::ByString)
        {
            OUString aStr = rDoc.GetFormatTable()->GetInputLineString(rItem.mfVal, 0);
            rItem.maString = rPool.intern(aStr);
        }

        switch (pAry[i].Operator)           // FilterOperator
        {
            case sheet::FilterOperator_EQUAL:           rEntry.eOp = SC_EQUAL;       break;
            case sheet::FilterOperator_LESS:            rEntry.eOp = SC_LESS;            break;
            case sheet::FilterOperator_GREATER:         rEntry.eOp = SC_GREATER;         break;
            case sheet::FilterOperator_LESS_EQUAL:      rEntry.eOp = SC_LESS_EQUAL;  break;
            case sheet::FilterOperator_GREATER_EQUAL:   rEntry.eOp = SC_GREATER_EQUAL; break;
            case sheet::FilterOperator_NOT_EQUAL:       rEntry.eOp = SC_NOT_EQUAL;   break;
            case sheet::FilterOperator_TOP_VALUES:      rEntry.eOp = SC_TOPVAL;      break;
            case sheet::FilterOperator_BOTTOM_VALUES:   rEntry.eOp = SC_BOTVAL;      break;
            case sheet::FilterOperator_TOP_PERCENT:     rEntry.eOp = SC_TOPPERC;         break;
            case sheet::FilterOperator_BOTTOM_PERCENT:  rEntry.eOp = SC_BOTPERC;         break;
            case sheet::FilterOperator_EMPTY:
                rEntry.SetQueryByEmpty();
                break;
            case sheet::FilterOperator_NOT_EMPTY:
                rEntry.SetQueryByNonEmpty();
                break;
            default:
                OSL_FAIL("Wrong query enum");
                rEntry.eOp = SC_EQUAL;
        }
    }

    SCSIZE nParamCount = aParam.GetEntryCount();    // if below eight Param isn't resized
    for (i=nCount; i<nParamCount; i++)
        aParam.GetEntry(i).bDoQuery = false;        // reset surplus fields

    PutData(aParam);
}

void SAL_CALL ScFilterDescriptorBase::setFilterFields2(
    const uno::Sequence<sheet::TableFilterField2>& aFilterFields )
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);
    fillQueryParam(aParam, &pDocSh->GetDocument(), aFilterFields);
    PutData(aParam);
}

void SAL_CALL ScFilterDescriptorBase::setFilterFields3(
    const uno::Sequence<sheet::TableFilterField3>& aFilterFields )
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);
    fillQueryParam(aParam, &pDocSh->GetDocument(), aFilterFields);
    PutData(aParam);
}

// Rest are Properties

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScFilterDescriptorBase::getPropertySetInfo()
{
    SolarMutexGuard aGuard;
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( aPropSet.getPropertyMap() ));
    return aRef;
}

void SAL_CALL ScFilterDescriptorBase::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    if (aPropertyName == SC_UNONAME_CONTHDR)
        aParam.bHasHeader = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_COPYOUT)
        aParam.bInplace = !(ScUnoHelpFunctions::GetBoolFromAny( aValue ));
    else if (aPropertyName == SC_UNONAME_ISCASE)
        aParam.bCaseSens = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_MAXFLD)
    {
        // silently ignored
    }
    else if (aPropertyName == SC_UNONAME_ORIENT)
    {
        //! test for correct enum type?
        table::TableOrientation eOrient = static_cast<table::TableOrientation>(ScUnoHelpFunctions::GetEnumFromAny( aValue ));
        aParam.bByRow = ( eOrient != table::TableOrientation_COLUMNS );
    }
    else if (aPropertyName == SC_UNONAME_OUTPOS)
    {
        table::CellAddress aAddress;
        if ( aValue >>= aAddress )
        {
            aParam.nDestTab = aAddress.Sheet;
            aParam.nDestCol = static_cast<SCCOL>(aAddress.Column);
            aParam.nDestRow = static_cast<SCROW>(aAddress.Row);
        }
    }
    else if (aPropertyName == SC_UNONAME_SAVEOUT)
        aParam.bDestPers = ScUnoHelpFunctions::GetBoolFromAny( aValue );
    else if (aPropertyName == SC_UNONAME_SKIPDUP)
        aParam.bDuplicate = !(ScUnoHelpFunctions::GetBoolFromAny( aValue ));
    else if (aPropertyName == SC_UNONAME_USEREGEX)
        aParam.eSearchType = ScUnoHelpFunctions::GetBoolFromAny( aValue ) ? utl::SearchParam::SearchType::Regexp :
            utl::SearchParam::SearchType::Normal;

    PutData(aParam);
}

uno::Any SAL_CALL ScFilterDescriptorBase::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    ScQueryParam aParam;
    GetData(aParam);

    uno::Any aRet;

    if (aPropertyName == SC_UNONAME_CONTHDR )
        aRet <<= aParam.bHasHeader;
    else if (aPropertyName == SC_UNONAME_COPYOUT )
        aRet <<= !(aParam.bInplace);
    else if (aPropertyName == SC_UNONAME_ISCASE )
        aRet <<= aParam.bCaseSens;
    else if (aPropertyName == SC_UNONAME_MAXFLD )
        aRet <<= static_cast<sal_Int32>(aParam.GetEntryCount());
    else if (aPropertyName == SC_UNONAME_ORIENT )
    {
        table::TableOrientation eOrient = aParam.bByRow ? table::TableOrientation_ROWS :
                                                          table::TableOrientation_COLUMNS;
        aRet <<= eOrient;
    }
    else if (aPropertyName == SC_UNONAME_OUTPOS )
    {
        table::CellAddress aOutPos;
        aOutPos.Sheet  = aParam.nDestTab;
        aOutPos.Column = aParam.nDestCol;
        aOutPos.Row    = aParam.nDestRow;
        aRet <<= aOutPos;
    }
    else if (aPropertyName == SC_UNONAME_SAVEOUT )
        aRet <<= aParam.bDestPers;
    else if (aPropertyName == SC_UNONAME_SKIPDUP )
        aRet <<= !(aParam.bDuplicate);
    else if (aPropertyName == SC_UNONAME_USEREGEX )
        aRet <<= (aParam.eSearchType == utl::SearchParam::SearchType::Regexp);

    return aRet;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScFilterDescriptorBase )

ScFilterDescriptor::ScFilterDescriptor(ScDocShell* pDocShell)
    :
    ScFilterDescriptorBase(pDocShell)
{
}

ScFilterDescriptor::~ScFilterDescriptor()
{
}

void ScFilterDescriptor::GetData( ScQueryParam& rParam ) const
{
    rParam = aStoredParam;          // query for interface
}

void ScFilterDescriptor::PutData( const ScQueryParam& rParam )
{
    aStoredParam = rParam;          // set by the interface
}

void ScFilterDescriptor::SetParam( const ScQueryParam& rNew )
{
    aStoredParam = rNew;            // set from outside
}

ScRangeFilterDescriptor::ScRangeFilterDescriptor(ScDocShell* pDocShell, ScDatabaseRangeObj* pPar) :
    ScFilterDescriptorBase(pDocShell),
    mxParent(pPar)
{
}

ScRangeFilterDescriptor::~ScRangeFilterDescriptor()
{
}

void ScRangeFilterDescriptor::GetData( ScQueryParam& rParam ) const
{
    if (mxParent.is())
        mxParent->GetQueryParam( rParam );
}

void ScRangeFilterDescriptor::PutData( const ScQueryParam& rParam )
{
    if (mxParent.is())
        mxParent->SetQueryParam( rParam );
}

ScDataPilotFilterDescriptor::ScDataPilotFilterDescriptor(ScDocShell* pDocShell, ScDataPilotDescriptorBase* pPar) :
    ScFilterDescriptorBase(pDocShell),
    mxParent(pPar)
{
}

ScDataPilotFilterDescriptor::~ScDataPilotFilterDescriptor()
{
}

void ScDataPilotFilterDescriptor::GetData( ScQueryParam& rParam ) const
{
    if (mxParent.is())
    {
        ScDPObject* pDPObj = mxParent->GetDPObject();
        if (pDPObj && pDPObj->IsSheetData())
            rParam = pDPObj->GetSheetDesc()->GetQueryParam();
    }
}

void ScDataPilotFilterDescriptor::PutData( const ScQueryParam& rParam )
{
    if (!mxParent.is())
        return;

    ScDPObject* pDPObj = mxParent->GetDPObject();
    if (pDPObj)
    {
        ScSheetSourceDesc aSheetDesc(&mxParent->GetDocShell()->GetDocument());
        if (pDPObj->IsSheetData())
            aSheetDesc = *pDPObj->GetSheetDesc();
        aSheetDesc.SetQueryParam(rParam);
        pDPObj->SetSheetDesc(aSheetDesc);
        mxParent->SetDPObject(pDPObj);
    }
}

ScDatabaseRangeObj::ScDatabaseRangeObj(ScDocShell* pDocSh, OUString aNm) :
    pDocShell( pDocSh ),
    aName(std::move( aNm )),
    aPropSet( lcl_GetDBRangePropertyMap() ),
    bIsUnnamed(false),
    aTab( 0 )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScDatabaseRangeObj::ScDatabaseRangeObj(ScDocShell* pDocSh, const SCTAB nTab) :
    pDocShell( pDocSh ),
    aName(STR_DB_LOCAL_NONAME),
    aPropSet( lcl_GetDBRangePropertyMap() ),
    bIsUnnamed(true),
    aTab( nTab )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScDatabaseRangeObj::~ScDatabaseRangeObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScDatabaseRangeObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{

    if ( rHint.GetId() == SfxHintId::Dying )
        pDocShell = nullptr;
    else if ( rHint.GetId() == SfxHintId::ScDBRangeRefreshed )
    {
        auto pRefreshHint = static_cast<const ScDBRangeRefreshedHint*>(&rHint);
        ScDBData* pDBData = GetDBData_Impl();
        ScImportParam aParam;
        pDBData->GetImportParam(aParam);
        if (aParam == pRefreshHint->GetImportParam())
            Refreshed_Impl();
    }
}

// Help functions

ScDBData* ScDatabaseRangeObj::GetDBData_Impl() const
{
    ScDBData* pRet = nullptr;
    if (pDocShell)
    {
        if (bIsUnnamed)
        {
            pRet = pDocShell->GetDocument().GetAnonymousDBData(aTab);
        }
        else
        {
            ScDBCollection* pNames = pDocShell->GetDocument().GetDBCollection();
            if (pNames)
            {
                ScDBData* p = pNames->getNamedDBs().findByUpperName(ScGlobal::getCharClass().uppercase(aName));
                if (p)
                    pRet = p;
            }
        }
    }
    return pRet;
}

// XNamed

OUString SAL_CALL ScDatabaseRangeObj::getName()
{
    SolarMutexGuard aGuard;
    return aName;
}

void SAL_CALL ScDatabaseRangeObj::setName( const OUString& aNewName )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        ScDBDocFunc aFunc(*pDocShell);
        bool bOk = aFunc.RenameDBRange( aName, aNewName );
        if (bOk)
            aName = aNewName;
    }
}

// XDatabaseRange

table::CellRangeAddress SAL_CALL ScDatabaseRangeObj::getDataArea()
{
    SolarMutexGuard aGuard;
    table::CellRangeAddress aAddress;
    ScDBData* pData = GetDBData_Impl();
    if (pData)
    {
        ScRange aRange;
        pData->GetArea(aRange);
        aAddress.Sheet       = aRange.aStart.Tab();
        aAddress.StartColumn = aRange.aStart.Col();
        aAddress.StartRow    = aRange.aStart.Row();
        aAddress.EndColumn   = aRange.aEnd.Col();
        aAddress.EndRow      = aRange.aEnd.Row();
    }
    return aAddress;
}

void SAL_CALL ScDatabaseRangeObj::setDataArea( const table::CellRangeAddress& aDataArea )
{
    SolarMutexGuard aGuard;
    ScDBData* pData = GetDBData_Impl();
    if ( pDocShell && pData )
    {
        ScDBData aNewData( *pData );
        //! MoveTo ???
        aNewData.SetArea( aDataArea.Sheet, static_cast<SCCOL>(aDataArea.StartColumn), static_cast<SCROW>(aDataArea.StartRow),
                                           static_cast<SCCOL>(aDataArea.EndColumn), static_cast<SCROW>(aDataArea.EndRow) );
        ScDBDocFunc aFunc(*pDocShell);
        aFunc.ModifyDBData(aNewData);
    }
}

uno::Sequence<beans::PropertyValue> SAL_CALL ScDatabaseRangeObj::getSortDescriptor()
{
    SolarMutexGuard aGuard;
    ScSortParam aParam;
    const ScDBData* pData = GetDBData_Impl();
    if (pData)
    {
        pData->GetSortParam(aParam);

        //  SortDescriptor contains the counted fields inside the area
        ScRange aDBRange;
        pData->GetArea(aDBRange);
        SCCOLROW nFieldStart = aParam.bByRow ? static_cast<SCCOLROW>(aDBRange.aStart.Col()) : static_cast<SCCOLROW>(aDBRange.aStart.Row());
        for (sal_uInt16 i=0; i<aParam.GetSortKeyCount(); i++)
            if ( aParam.maKeyState[i].bDoSort && aParam.maKeyState[i].nField >= nFieldStart )
                aParam.maKeyState[i].nField -= nFieldStart;
    }

    uno::Sequence<beans::PropertyValue> aSeq( ScSortDescriptor::GetPropertyCount() );
    ScSortDescriptor::FillProperties( aSeq, aParam );
    return aSeq;
}

void ScDatabaseRangeObj::GetQueryParam(ScQueryParam& rQueryParam) const
{
    const ScDBData* pData = GetDBData_Impl();
    if (!pData)
        return;

    pData->GetQueryParam(rQueryParam);

    //  FilterDescriptor contains the counted fields inside the area
    ScRange aDBRange;
    pData->GetArea(aDBRange);
    SCCOLROW nFieldStart = rQueryParam.bByRow ? static_cast<SCCOLROW>(aDBRange.aStart.Col()) : static_cast<SCCOLROW>(aDBRange.aStart.Row());
    SCSIZE nCount = rQueryParam.GetEntryCount();
    for (SCSIZE i=0; i<nCount; i++)
    {
        ScQueryEntry& rEntry = rQueryParam.GetEntry(i);
        if (rEntry.bDoQuery && rEntry.nField >= nFieldStart)
            rEntry.nField -= nFieldStart;
    }
}

void ScDatabaseRangeObj::SetQueryParam(const ScQueryParam& rQueryParam)
{
    const ScDBData* pData = GetDBData_Impl();
    if (!pData)
        return;

    //  FilterDescriptor contains the counted fields inside the area
    ScQueryParam aParam(rQueryParam);
    ScRange aDBRange;
    pData->GetArea(aDBRange);
    SCCOLROW nFieldStart = aParam.bByRow ? static_cast<SCCOLROW>(aDBRange.aStart.Col()) : static_cast<SCCOLROW>(aDBRange.aStart.Row());

    SCSIZE nCount = aParam.GetEntryCount();
    for (SCSIZE i=0; i<nCount; i++)
    {
           ScQueryEntry& rEntry = aParam.GetEntry(i);
           if (rEntry.bDoQuery)
                   rEntry.nField += nFieldStart;
    }

    ScDBData aNewData( *pData );
    aNewData.SetQueryParam(aParam);
    aNewData.SetHeader(aParam.bHasHeader);      // not in ScDBData::SetQueryParam
    aNewData.SetTotals(aParam.bHasTotals);      // not in ScDBData::SetQueryParam
    ScDBDocFunc aFunc(*pDocShell);
    aFunc.ModifyDBData(aNewData);
}

uno::Reference<sheet::XSheetFilterDescriptor> SAL_CALL ScDatabaseRangeObj::getFilterDescriptor()
{
    SolarMutexGuard aGuard;
    return new ScRangeFilterDescriptor(pDocShell, this);
}

void ScDatabaseRangeObj::GetSubTotalParam(ScSubTotalParam& rSubTotalParam) const
{
    const ScDBData* pData = GetDBData_Impl();
    if (!pData)
        return;

    pData->GetSubTotalParam(rSubTotalParam);

    //  FilterDescriptor contains the counted fields inside the area
    ScRange aDBRange;
    pData->GetArea(aDBRange);
    SCCOL nFieldStart = aDBRange.aStart.Col();
    for (auto& group : rSubTotalParam.aGroups)
    {
        if (group.bActive)
        {
            if (group.nField >= nFieldStart)
                group.nField -= nFieldStart;
            for (SCCOL j = 0; j < group.nSubTotals; j++)
                if (group.col(j) >= nFieldStart)
                    group.col(j) -= nFieldStart;
        }
    }
}

void ScDatabaseRangeObj::SetSubTotalParam(const ScSubTotalParam& rSubTotalParam)
{
    const ScDBData* pData = GetDBData_Impl();
    if (!pData)
        return;

    //  FilterDescriptor contains the counted fields inside the area
    ScSubTotalParam aParam(rSubTotalParam);
    ScRange aDBRange;
    pData->GetArea(aDBRange);
    SCCOL nFieldStart = aDBRange.aStart.Col();
    for (auto& group : aParam.aGroups)
    {
        if (group.bActive)
        {
            group.nField += nFieldStart;
            for (SCCOL j = 0; j < group.nSubTotals; j++)
                group.col(j) += nFieldStart;
        }
    }

    ScDBData aNewData( *pData );
    aNewData.SetSubTotalParam(aParam);
    ScDBDocFunc aFunc(*pDocShell);
    aFunc.ModifyDBData(aNewData);
}

uno::Reference<sheet::XSubTotalDescriptor> SAL_CALL ScDatabaseRangeObj::getSubTotalDescriptor()
{
    SolarMutexGuard aGuard;
    return new ScRangeSubTotalDescriptor(this);
}

uno::Sequence<beans::PropertyValue> SAL_CALL ScDatabaseRangeObj::getImportDescriptor()
{
    SolarMutexGuard aGuard;
    ScImportParam aParam;
    const ScDBData* pData = GetDBData_Impl();
    if (pData)
        pData->GetImportParam(aParam);

    uno::Sequence<beans::PropertyValue> aSeq( ScImportDescriptor::GetPropertyCount() );
    ScImportDescriptor::FillProperties( aSeq, aParam );
    return aSeq;
}

// XRefreshable

void SAL_CALL ScDatabaseRangeObj::refresh()
{
    SolarMutexGuard aGuard;
    ScDBData* pData = GetDBData_Impl();
    if ( !(pDocShell && pData) )
        return;

    ScDBDocFunc aFunc(*pDocShell);

    // repeat import?
    bool bContinue = true;
    ScImportParam aImportParam;
    pData->GetImportParam( aImportParam );
    if (aImportParam.bImport && !pData->HasImportSelection())
    {
        SCTAB nTab;
        SCCOL nDummyCol;
        SCROW nDummyRow;
        pData->GetArea( nTab, nDummyCol,nDummyRow,nDummyCol,nDummyRow );
        bContinue = aFunc.DoImport( nTab, aImportParam, nullptr );   //! Api-Flag as parameter
    }

    // if no error then internal operations (sort, query, subtotal)
    if (bContinue)
        aFunc.RepeatDB( pData->GetName(), true, bIsUnnamed, aTab );
}

void SAL_CALL ScDatabaseRangeObj::addRefreshListener(
                                const uno::Reference<util::XRefreshListener >& xListener )
{
    SolarMutexGuard aGuard;
    aRefreshListeners.emplace_back( xListener );

    //  hold one additional ref to keep this object alive as long as there are listeners
    if ( aRefreshListeners.size() == 1 )
        acquire();
}

void SAL_CALL ScDatabaseRangeObj::removeRefreshListener(
                                const uno::Reference<util::XRefreshListener >& xListener )
{
    SolarMutexGuard aGuard;
    sal_uInt16 nCount = aRefreshListeners.size();
    for ( sal_uInt16 n=nCount; n--; )
    {
        uno::Reference<util::XRefreshListener>& rObj = aRefreshListeners[n];
        if ( rObj == xListener )
        {
            aRefreshListeners.erase( aRefreshListeners.begin() + n );
            if ( aRefreshListeners.empty() )
                release();                          // release ref for listeners
            break;
        }
    }
}

void ScDatabaseRangeObj::Refreshed_Impl()
{
    lang::EventObject aEvent;
    aEvent.Source = getXWeak();
    for (uno::Reference<util::XRefreshListener> & xRefreshListener : aRefreshListeners)
        xRefreshListener->refreshed( aEvent );
}

// XCellRangeSource

uno::Reference<table::XCellRange> SAL_CALL ScDatabaseRangeObj::getReferredCells()
{
    SolarMutexGuard aGuard;
    ScDBData* pData = GetDBData_Impl();
    if ( pData )
    {
        //! static function to create ScCellObj/ScCellRange on ScCellRangeObj ???
        ScRange aRange;

        pData->GetArea(aRange);
        if ( aRange.aStart == aRange.aEnd )
            return new ScCellObj( pDocShell, aRange.aStart );
        else
            return new ScCellRangeObj( pDocShell, aRange );
    }
    return nullptr;
}

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScDatabaseRangeObj::getPropertySetInfo()
{
    SolarMutexGuard aGuard;
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( aPropSet.getPropertyMap() ));
    return aRef;
}

void SAL_CALL ScDatabaseRangeObj::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;
    ScDBData* pData = GetDBData_Impl();
    if ( !(pDocShell && pData) )
        return;

    ScDBData aNewData( *pData );
    bool bDo = true;

    if ( aPropertyName == SC_UNONAME_KEEPFORM )
        aNewData.SetKeepFmt( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    else if ( aPropertyName == SC_UNONAME_MOVCELLS )
        aNewData.SetDoSize( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    else if ( aPropertyName == SC_UNONAME_STRIPDAT )
        aNewData.SetStripData( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    else if (aPropertyName == SC_UNONAME_AUTOFLT )
    {
        bool bAutoFilter(ScUnoHelpFunctions::GetBoolFromAny( aValue ));
        aNewData.SetAutoFilter(bAutoFilter);
        ScRange aRange;
        aNewData.GetArea(aRange);
        ScDocument& rDoc = pDocShell->GetDocument();
        if (bAutoFilter)
            rDoc.ApplyFlagsTab( aRange.aStart.Col(), aRange.aStart.Row(),
                                 aRange.aEnd.Col(), aRange.aStart.Row(),
                                 aRange.aStart.Tab(), ScMF::Auto );
        else if (!bAutoFilter)
            rDoc.RemoveFlagsTab(aRange.aStart.Col(), aRange.aStart.Row(),
                                 aRange.aEnd.Col(), aRange.aStart.Row(),
                                 aRange.aStart.Tab(), ScMF::Auto );
        ScRange aPaintRange(aRange.aStart, aRange.aEnd);
        aPaintRange.aEnd.SetRow(aPaintRange.aStart.Row());
        pDocShell->PostPaint(aPaintRange, PaintPartFlags::Grid);
    }
    else if (aPropertyName == SC_UNONAME_USEFLTCRT )
    {
        if (ScUnoHelpFunctions::GetBoolFromAny( aValue ))
        {
            // only here to set bIsAdvanced in ScDBData
            ScRange aRange;
            (void)aNewData.GetAdvancedQuerySource(aRange);
            aNewData.SetAdvancedQuerySource(&aRange);
        }
        else
            aNewData.SetAdvancedQuerySource(nullptr);
    }
    else if (aPropertyName == SC_UNONAME_FLTCRT )
    {
        table::CellRangeAddress aRange;
        if (aValue >>= aRange)
        {
            ScRange aCoreRange;
            ScUnoConversion::FillScRange(aCoreRange, aRange);

            aNewData.SetAdvancedQuerySource(&aCoreRange);
        }
    }
    else if (aPropertyName == SC_UNONAME_FROMSELECT )
    {
        aNewData.SetImportSelection(::cppu::any2bool(aValue));
    }
    else if (aPropertyName == SC_UNONAME_REFPERIOD )
    {
        sal_Int32 nRefresh = 0;
        if (aValue >>= nRefresh)
        {
            ScDocument& rDoc = pDocShell->GetDocument();
            aNewData.SetRefreshDelay(nRefresh);
            if (rDoc.GetDBCollection())
            {
                aNewData.SetRefreshHandler( rDoc.GetDBCollection()->GetRefreshHandler() );
                aNewData.SetRefreshControl( &rDoc.GetRefreshTimerControlAddress() );
            }
        }
    }
    else if (aPropertyName == SC_UNONAME_CONRES )
    {
    }
    else if ( aPropertyName == SC_UNONAME_TOTALSROW )
        aNewData.SetTotals( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    else if ( aPropertyName == SC_UNONAME_CONTHDR )
        aNewData.SetHeader( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    else
        bDo = false;

    if (bDo)
    {
        ScDBDocFunc aFunc(*pDocShell);
        aFunc.ModifyDBData(aNewData);
    }
}

uno::Any SAL_CALL ScDatabaseRangeObj::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    uno::Any aRet;
    ScDBData* pData = GetDBData_Impl();
    if ( pData )
    {
        if ( aPropertyName == SC_UNONAME_KEEPFORM )
            aRet <<= pData->IsKeepFmt();
        else if ( aPropertyName == SC_UNONAME_MOVCELLS )
            aRet <<= pData->IsDoSize();
        else if ( aPropertyName == SC_UNONAME_STRIPDAT )
            aRet <<= pData->IsStripData();
        else if ( aPropertyName == SC_UNONAME_ISUSER )
        {
            //  all database ranges except "unnamed" are user defined
            aRet <<= (pData->GetName() != STR_DB_LOCAL_NONAME);
        }
        else if ( aPropertyName == SC_UNO_LINKDISPBIT )
        {
            //  no target bitmaps for individual entries (would be all equal)
            // ScLinkTargetTypeObj::SetLinkTargetBitmap( aRet, SC_LINKTARGETTYPE_DBAREA );
        }
        else if ( aPropertyName == SC_UNO_LINKDISPNAME )
            aRet <<= aName;
        else if (aPropertyName == SC_UNONAME_AUTOFLT )
        {
            bool bAutoFilter(GetDBData_Impl()->HasAutoFilter());

            aRet <<= bAutoFilter;
        }
        else if (aPropertyName == SC_UNONAME_USEFLTCRT )
        {
            ScRange aRange;
            bool bIsAdvancedSource(GetDBData_Impl()->GetAdvancedQuerySource(aRange));

            aRet <<= bIsAdvancedSource;
        }
        else if (aPropertyName == SC_UNONAME_FLTCRT )
        {
            table::CellRangeAddress aRange;
            ScRange aCoreRange;
            if (GetDBData_Impl()->GetAdvancedQuerySource(aCoreRange))
                ScUnoConversion::FillApiRange(aRange, aCoreRange);

            aRet <<= aRange;
        }
        else if (aPropertyName == SC_UNONAME_FROMSELECT )
        {
            aRet <<= GetDBData_Impl()->HasImportSelection();
        }
        else if (aPropertyName == SC_UNONAME_REFPERIOD )
        {
            sal_Int32 nRefresh(GetDBData_Impl()->GetRefreshDelaySeconds());
            aRet <<= nRefresh;
        }
        else if (aPropertyName == SC_UNONAME_CONRES )
        {
        }
        else if (aPropertyName == SC_UNONAME_TOKENINDEX )
        {
            // get index for use in formula tokens (read-only)
            aRet <<= static_cast<sal_Int32>(GetDBData_Impl()->GetIndex());
        }
        else if (aPropertyName == SC_UNONAME_TOTALSROW )
        {
            bool bTotals(GetDBData_Impl()->HasTotals());

            aRet <<= bTotals;
        }
        else if (aPropertyName == SC_UNONAME_CONTHDR )
        {
            bool bHeader(GetDBData_Impl()->HasHeader());

            aRet <<= bHeader;
        }
    }
    return aRet;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScDatabaseRangeObj )

// XServiceInfo
OUString SAL_CALL ScDatabaseRangeObj::getImplementationName()
{
    return u"ScDatabaseRangeObj"_ustr;
}

sal_Bool SAL_CALL ScDatabaseRangeObj::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence<OUString> SAL_CALL ScDatabaseRangeObj::getSupportedServiceNames()
{
    return {u"com.sun.star.sheet.DatabaseRange"_ustr,
            SCLINKTARGET_SERVICE};
}

ScDatabaseRangesObj::ScDatabaseRangesObj(ScDocShell* pDocSh) :
    pDocShell( pDocSh )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScDatabaseRangesObj::~ScDatabaseRangesObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScDatabaseRangesObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    //  reference update does not matter here

    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;
    }
}

// XDatabaseRanges

rtl::Reference<ScDatabaseRangeObj> ScDatabaseRangesObj::GetObjectByIndex_Impl(size_t nIndex)
{
    if (!pDocShell)
        return nullptr;

    ScDBCollection* pNames = pDocShell->GetDocument().GetDBCollection();
    if (!pNames)
        return nullptr;

    const ScDBCollection::NamedDBs& rDBs = pNames->getNamedDBs();
    if (rDBs.empty() || nIndex >= rDBs.size())
        return nullptr;

    ScDBCollection::NamedDBs::const_iterator itr = rDBs.begin();
    ::std::advance(itr, nIndex); // boundary check is done above.
    return new ScDatabaseRangeObj(pDocShell, (*itr)->GetName());
}

rtl::Reference<ScDatabaseRangeObj> ScDatabaseRangesObj::GetObjectByName_Impl(const OUString& aName)
{
    if ( pDocShell && hasByName(aName) )
    {
        return new ScDatabaseRangeObj( pDocShell, aName );
    }
    return nullptr;
}

void SAL_CALL ScDatabaseRangesObj::addNewByName( const OUString& aName,
                                        const table::CellRangeAddress& aRange )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        ScDBDocFunc aFunc(*pDocShell);

        ScRange aNameRange( static_cast<SCCOL>(aRange.StartColumn), static_cast<SCROW>(aRange.StartRow), aRange.Sheet,
                            static_cast<SCCOL>(aRange.EndColumn),   static_cast<SCROW>(aRange.EndRow),   aRange.Sheet );
        bDone = aFunc.AddDBRange( aName, aNameRange );
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

void SAL_CALL ScDatabaseRangesObj::removeByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        ScDBDocFunc aFunc(*pDocShell);
        bDone = aFunc.DeleteDBRange( aName );
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScDatabaseRangesObj::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.DatabaseRangesEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScDatabaseRangesObj::getCount()
{
    SolarMutexGuard aGuard;

    //! need to omit "unnamed"?

    if (pDocShell)
    {
        ScDBCollection* pNames = pDocShell->GetDocument().GetDBCollection();
        if (pNames)
            return static_cast<sal_Int32>(pNames->getNamedDBs().size());
    }
    return 0;
}

uno::Any SAL_CALL ScDatabaseRangesObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    if (nIndex < 0)
        throw lang::IndexOutOfBoundsException();

    rtl::Reference<ScDatabaseRangeObj> xRange(GetObjectByIndex_Impl(static_cast<size_t>(nIndex)));
    if (!xRange.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<sheet::XDatabaseRange>(xRange));
}

uno::Type SAL_CALL ScDatabaseRangesObj::getElementType()
{
    return cppu::UnoType<sheet::XDatabaseRange>::get();
}

sal_Bool SAL_CALL ScDatabaseRangesObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

// XNameAccess

uno::Any SAL_CALL ScDatabaseRangesObj::getByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScDatabaseRangeObj> xRange(GetObjectByName_Impl(aName));
    if (!xRange.is())
        throw container::NoSuchElementException();

    return uno::Any(uno::Reference<sheet::XDatabaseRange>(xRange));
}

uno::Sequence<OUString> SAL_CALL ScDatabaseRangesObj::getElementNames()
{
    SolarMutexGuard aGuard;

    //! need to omit "unnamed"?

    if (pDocShell)
    {
        ScDBCollection* pNames = pDocShell->GetDocument().GetDBCollection();
        if (pNames)
        {
            const ScDBCollection::NamedDBs& rDBs = pNames->getNamedDBs();
            uno::Sequence<OUString> aSeq(rDBs.size());
            auto aSeqRange = asNonConstRange(aSeq);
            size_t i = 0;
            for (const auto& rDB : rDBs)
            {
                aSeqRange[i] = rDB->GetName();
                ++i;
            }

            return aSeq;
        }
    }
    return {};
}

sal_Bool SAL_CALL ScDatabaseRangesObj::hasByName( const OUString& aName )
{
    SolarMutexGuard aGuard;

    //! need to omit "unnamed"?

    if (pDocShell)
    {
        ScDBCollection* pNames = pDocShell->GetDocument().GetDBCollection();
        if (pNames)
            return pNames->getNamedDBs().findByUpperName(ScGlobal::getCharClass().uppercase(aName)) != nullptr;
    }
    return false;
}

ScUnnamedDatabaseRangesObj::ScUnnamedDatabaseRangesObj(ScDocShell* pDocSh) :
    pDocShell( pDocSh )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScUnnamedDatabaseRangesObj::~ScUnnamedDatabaseRangesObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScUnnamedDatabaseRangesObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    //  reference update does not matter here

    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;
    }
}

// XUnnamedDatabaseRanges

void ScUnnamedDatabaseRangesObj::setByTable( const table::CellRangeAddress& aRange )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        if ( pDocShell->GetDocument().GetTableCount() <= aRange.Sheet )
            throw lang::IndexOutOfBoundsException();

        ScDBDocFunc aFunc(*pDocShell);
        ScRange aUnnamedRange( static_cast<SCCOL>(aRange.StartColumn), static_cast<SCROW>(aRange.StartRow), aRange.Sheet,
                            static_cast<SCCOL>(aRange.EndColumn),   static_cast<SCROW>(aRange.EndRow),   aRange.Sheet );
        bDone = aFunc.AddDBRange( STR_DB_LOCAL_NONAME, aUnnamedRange );
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

uno::Any ScUnnamedDatabaseRangesObj::getByTable( sal_Int32 nTab )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
        throw uno::RuntimeException();

    if ( pDocShell->GetDocument().GetTableCount() <= nTab )
        throw lang::IndexOutOfBoundsException();
    uno::Reference<sheet::XDatabaseRange> xRange(
        new ScDatabaseRangeObj(pDocShell, static_cast<SCTAB>(nTab)));
    if (!xRange.is())
        throw container::NoSuchElementException();

    return uno::Any(xRange);
}

sal_Bool ScUnnamedDatabaseRangesObj::hasByTable( sal_Int32 nTab )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        if (pDocShell->GetDocument().GetTableCount() <= nTab)
            throw lang::IndexOutOfBoundsException();
        if (pDocShell->GetDocument().GetAnonymousDBData(static_cast<SCTAB>(nTab)))
            return true;
        return false;
    }
    else
        return false;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
