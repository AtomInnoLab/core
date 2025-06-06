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
#pragma once

#include "address.hxx"
#include "calcmacros.hxx"
#include "dpitemdata.hxx"
#include "dpnumgroupinfo.hxx"
#include "scdllapi.h"
#include "types.hxx"

#include <mdds/flat_segment_tree.hpp>
#include <tools/long.hxx>
#include <o3tl/sorted_vector.hxx>

#include <memory>
#include <unordered_set>
#include <vector>

struct ScQueryParam;
class ScDPObject;
class ScDocument;
struct ScInterpreterContext;

enum class SvNumFormatType : sal_Int16;

/**
 * This class represents the cached data part of the datapilot cache table
 * implementation.
 */
class ScDPCache
{
public:
    typedef std::unordered_set<OUString> StringSetType;
    typedef mdds::flat_segment_tree<SCROW, bool> EmptyRowsType;
    typedef std::vector<ScDPItemData> ScDPItemDataVec;
    typedef o3tl::sorted_vector<ScDPObject*> ScDPObjectSet;
    typedef std::vector<SCROW> IndexArrayType;

    struct GroupItems
    {
        ScDPItemDataVec maItems;
        ScDPNumGroupInfo maInfo;
        sal_Int32 mnGroupType;

        GroupItems();
        GroupItems(const GroupItems&) = delete;
        const GroupItems& operator=(const GroupItems&) = delete;
        GroupItems(const ScDPNumGroupInfo& rInfo, sal_Int32 nGroupType);
    };

    struct Field
    {
        /**
         * Optional items for grouped field.
         */
        std::unique_ptr<GroupItems> mpGroup;

        /**
         * Unique values in the field, stored in ascending order.
         */
        ScDPItemDataVec maItems;

        /**
         * Original source data represented as indices to the unique value
         * list.  The order of the data is as they appear in the original
         * data source.
         */
        IndexArrayType maData;

        sal_uInt32 mnNumFormat;

        Field();
        Field(const Field&) = delete;
        const Field& operator=(const Field&) = delete;
    };

    /**
     * Interface for connecting to database source.  Column index is 0-based.
     */
    class DBConnector
    {
    public:
        virtual tools::Long getColumnCount() const = 0;
        virtual OUString getColumnLabel(tools::Long nCol) const = 0;
        virtual bool first() = 0;
        virtual bool next() = 0;
        virtual void finish() = 0;
        virtual void getValue(tools::Long nCol, ScDPItemData& rData, SvNumFormatType& rNumType) const = 0;
        virtual ~DBConnector() {}
    };

private:

    ScDocument& mrDoc;
    SCCOL mnColumnCount;

    /**
     * All pivot table objects that references this cache.
     */
    mutable ScDPObjectSet maRefObjects;

    typedef std::vector< std::unique_ptr<Field> > FieldsType;
    typedef std::vector< std::unique_ptr<GroupItems> > GroupFieldsType;

    FieldsType maFields;
    GroupFieldsType maGroupFields;
    std::vector<StringSetType> maStringPools; // one for each field.

    std::vector<OUString> maLabelNames; // Stores dimension names and the data layout dimension name at position 0.
    EmptyRowsType maEmptyRows;
    SCROW mnDataSize;
    SCROW mnRowCount;

    bool mbDisposing;

public:
    rtl_uString* InternString( size_t nDim, const OUString& rStr );
    void AddReference(ScDPObject* pObj) const;
    void RemoveReference(ScDPObject* pObj) const;
    SC_DLLPUBLIC const ScDPObjectSet& GetAllReferences() const;

    SCROW GetIdByItemData(tools::Long nDim, const ScDPItemData& rItem) const;

    static sal_uInt32 GetLocaleIndependentFormat( const ScInterpreterContext& rContext, sal_uInt32 nNumFormat );
    static OUString GetLocaleIndependentFormattedNumberString( double fValue );
    static OUString GetLocaleIndependentFormattedString( double fValue, const ScInterpreterContext& rContext, sal_uInt32 nNumFormat );
    SC_DLLPUBLIC OUString GetFormattedString(tools::Long nDim, const ScDPItemData& rItem, bool bLocaleIndependent) const;
    ScInterpreterContext& GetInterpreterContext() const;

    tools::Long AppendGroupField();
    void ResetGroupItems(tools::Long nDim, const ScDPNumGroupInfo& rNumInfo, sal_Int32 nGroupType);
    SCROW SetGroupItem(tools::Long nDim, const ScDPItemData& rData);
    SC_DLLPUBLIC void GetGroupDimMemberIds(tools::Long nDim, std::vector<SCROW>& rIds) const;
    void ClearGroupFields();
    void ClearAllFields();
    SC_DLLPUBLIC const ScDPNumGroupInfo* GetNumGroupInfo(tools::Long nDim) const;

    /**
     * Return a group type identifier.  The values correspond with
     * css::sheet::DataPilotFieldGroupBy constant values.
     *
     * @param nDim 0-based dimension index.
     *
     * @return group type identifier, or 0 on failure.
     */
    SC_DLLPUBLIC sal_Int32 GetGroupType(tools::Long nDim) const;

    SC_DLLPUBLIC SCCOL GetDimensionIndex(std::u16string_view sName) const;
    sal_uInt32 GetNumberFormat( tools::Long nDim ) const;
    SC_DLLPUBLIC bool IsDateDimension( tools::Long nDim ) const ;
    tools::Long GetDimMemberCount(tools::Long nDim) const;

    SC_DLLPUBLIC const IndexArrayType* GetFieldIndexArray( size_t nDim ) const;
    SC_DLLPUBLIC const ScDPItemDataVec& GetDimMemberValues( SCCOL nDim ) const;
    void InitFromDoc(ScDocument& rDoc, const ScRange& rRange);
    bool InitFromDataBase(DBConnector& rDB);

    /**
     * Row count is the number of records plus any trailing empty rows in case
     * the source data is sheet and contains trailing empty rows.
     */
    SCROW GetRowCount() const;

    /**
     * Data size is the number of records without any trailing empty rows for
     * sheet source data.  For any other source type, this should equal the
     * row count.
     */
    SC_DLLPUBLIC SCROW GetDataSize() const;
    SCROW GetItemDataId( sal_uInt16 nDim, SCROW nRow, bool bRepeatIfEmpty ) const;
    SC_DLLPUBLIC const OUString & GetDimensionName(std::vector<OUString>::size_type nDim) const;
    bool IsRowEmpty(SCROW nRow) const;
    bool ValidQuery(SCROW nRow, const ScQueryParam& rQueryParam) const;

    ScDocument& GetDoc() const;
    tools::Long GetColumnCount() const;

    SC_DLLPUBLIC const ScDPItemData* GetItemDataById( tools::Long nDim, SCROW nId ) const;

    SC_DLLPUBLIC size_t GetFieldCount() const;
    SC_DLLPUBLIC size_t GetGroupFieldCount() const;

    ScDPCache(const ScDPCache&) = delete;
    const ScDPCache& operator=(const ScDPCache&) = delete;
    ScDPCache(ScDocument& rDoc);
    ~ScDPCache();

#if DUMP_PIVOT_TABLE
    void Dump() const;
#endif

private:
    void PostInit();
    void Clear();
    const GroupItems* GetGroupItems(tools::Long nDim) const;
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
