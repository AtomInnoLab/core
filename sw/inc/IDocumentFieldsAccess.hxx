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

#include <sal/config.h>

#include <rtl/ustring.hxx>
#include <sal/types.h>
#include "swtypes.hxx"
#include "nodeoffset.hxx"
#include "names.hxx"
#include <unordered_map>

class SwFieldTypes;
class SwFieldType;
class SfxPoolItem;
struct SwPosition;
class SwDocUpdateField;
class SwCalc;
class SwTextField;
class SwField;
class SwMsgPoolItem;
class DateTime;
class SetGetExpField;
class SwNode;
class SwTable;
enum class SwFieldIds : sal_uInt16;
class SwRootFrame;
class IDocumentRedlineAccess;

namespace com::sun::star::uno { class Any; }

 /** Document fields related interfaces
 */
 class IDocumentFieldsAccess
 {
 public:
    virtual const SwFieldTypes *GetFieldTypes() const = 0;

    virtual SwFieldType *InsertFieldType(const SwFieldType &) = 0;

    virtual SwFieldType *GetSysFieldType( const SwFieldIds eWhich ) const = 0;

    virtual SwFieldType* GetFieldType(SwFieldIds nResId, const OUString& rName, bool bDbFieldMatching) const = 0;

    // convenience methods
    SwFieldType* GetFieldType(SwFieldIds nResId, const UIName& rName, bool bDbFieldMatching) const
    { return GetFieldType(nResId, rName.toString(), bDbFieldMatching); }
    SwFieldType* GetFieldType(SwFieldIds nResId, const SwMarkName& rName, bool bDbFieldMatching) const
    { return GetFieldType(nResId, rName.toString(), bDbFieldMatching); }

    virtual void RemoveFieldType(size_t nField) = 0;

    virtual void UpdateFields(bool bCloseDB, bool bSetModified = true) = 0;

    virtual void InsDeletedFieldType(SwFieldType &) = 0;

    /**
       Puts a value into a field at a certain position.

       A missing field at the given position leads to a failure.

       @param rPosition        position of the field
       @param rVal             the value
       @param nMId

       @retval true            putting of value was successful
       @retval false           else
    */
    virtual void PutValueToField(const SwPosition & rPos, const css::uno::Any& rVal, sal_uInt16 nWhich) = 0;

    // Call update of expression fields. All expressions are re-evaluated.

    /** Updates a field.

        @param rDstFormatField field to update
        @param rSrcField field containing the new values
        @param pMsgHint
        @param bUpdateTableFields TRUE: update table fields, too.

        @retval true             update was successful
        @retval false            else
    */
    virtual bool UpdateField(SwTextField * rDstFormatField, SwField & rSrcField, bool bUpdateTableFields) = 0;

    virtual void UpdateRefFields() = 0;

    virtual void UpdateTableFields(const SwTable* pTable) = 0;

    virtual void UpdateExpFields(SwTextField* pField, bool bUpdateRefFields) = 0;

    virtual void UpdateUsrFields() = 0;

    virtual void UpdatePageFields(const SwTwips) = 0;

    virtual void LockExpFields() = 0;

    virtual void UnlockExpFields() = 0;

    virtual bool IsExpFieldsLocked() const = 0;

    virtual SwDocUpdateField& GetUpdateFields() const = 0;

    /*  @@@MAINTAINABILITY-HORROR@@@
        SwNode (see parameter pChk) is (?) part of the private
        data structure of SwDoc and should not be exposed
    */
    virtual bool SetFieldsDirty(bool b, const SwNode* pChk, SwNodeOffset nLen) = 0;

    virtual void SetFixFields(const DateTime* pNewDateTime) = 0;

    // In Calculator set all SetExpression fields that are valid up to the indicated position
    // (Node [ + css::ucb::Content]).
    // A generated list of all fields may be passed along too
    // (if the address != 0 and the pointer == 0 a new list will be returned).
    virtual void FieldsToCalc(SwCalc& rCalc, SwNodeOffset nLastNd, sal_Int32 nLastCnt) = 0;

    virtual void FieldsToCalc(SwCalc& rCalc, const SetGetExpField& rToThisField, SwRootFrame const* pLayout) = 0;

    virtual void FieldsToExpand(std::unordered_map<OUString,OUString> & rTable, const SetGetExpField& rToThisField, SwRootFrame const& rLayout) = 0;

    virtual bool IsNewFieldLst() const = 0;

    virtual void SetNewFieldLst( bool bFlag) = 0;

    virtual void InsDelFieldInFieldLst(bool bIns, const SwTextField& rField) = 0;

    virtual sal_Int32 GetRecordsPerDocument() const = 0;

protected:
    virtual ~IDocumentFieldsAccess() {};
 };

namespace sw {
bool IsFieldDeletedInModel(IDocumentRedlineAccess const& rIDRA,
        SwTextField const& rTextField);
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
