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

#ifndef INCLUDED_SW_SOURCE_CORE_INC_UNDOCORE_HXX
#define INCLUDED_SW_SOURCE_CORE_INC_UNDOCORE_HXX

#include <undobj.hxx>
#include <calbck.hxx>
#include <rtl/ustring.hxx>
#include <redline.hxx>
#include <numrule.hxx>

#include <memory>
#include <vector>

class SfxItemSet;
class SwFormatColl;
class SwFormatAnchor;
class SdrMarkList;
class SwUndoDelete;

namespace sw {
    class UndoManager;
    class IShellCursorSupplier;
}

class SwRedlineSaveData final : public SwUndRng, public SwRedlineData, private SwUndoSaveSection
{
public:
    SwRedlineSaveData(
        SwComparePosition eCmpPos,
        const SwPosition& rSttPos,
        const SwPosition& rEndPos,
        SwRangeRedline& rRedl,
        bool bCopyNext );

    ~SwRedlineSaveData();

    void RedlineToDoc( SwPaM const & rPam );

    const SwNodeIndex* GetMvSttIdx() const
    {
        return SwUndoSaveSection::GetMvSttIdx();
    }

#if OSL_DEBUG_LEVEL > 0
    sal_uInt16 m_nRedlineCount;
    bool m_bRedlineCountDontCheck;
    bool m_bRedlineMoved;
#endif
};

class SwRedlineSaveDatas {
private:
    std::vector<std::unique_ptr<SwRedlineSaveData>> m_Data;

public:
    SwRedlineSaveDatas() : m_Data() {}

    void clear() { m_Data.clear(); }
    bool empty() const { return m_Data.empty(); }
    size_t size() const { return m_Data.size(); }
    void push_back(std::unique_ptr<SwRedlineSaveData> pNew) { m_Data.push_back(std::move(pNew)); }
    const SwRedlineSaveData& operator[](size_t const nIdx) const { return *m_Data[ nIdx ]; }
    SwRedlineSaveData& operator[](size_t const nIdx) { return *m_Data[ nIdx ]; }
#if OSL_DEBUG_LEVEL > 0
    void SetRedlineCountDontCheck(bool bCheck) { m_Data[0]->m_bRedlineCountDontCheck=bCheck; }
#endif
    void dumpAsXml(xmlTextWriterPtr pWriter) const;
};

namespace sw {
class UndoRedoContext final
    : public SfxUndoContext
{
public:
    UndoRedoContext(SwDoc & rDoc, IShellCursorSupplier & rCursorSupplier)
        : m_rDoc(rDoc)
        , m_rCursorSupplier(rCursorSupplier)
        , m_pSelFormat(nullptr)
        , m_pMarkList(nullptr)
    { }

    SwDoc & GetDoc() const { return m_rDoc; }

    IShellCursorSupplier & GetCursorSupplier() { return m_rCursorSupplier; }

    void SetSelections(SwFrameFormat *const pSelFormat, SdrMarkList *const pMarkList)
    {
        m_pSelFormat = pSelFormat;
        m_pMarkList = pMarkList;
    }
    void GetSelections(SwFrameFormat *& o_rpSelFormat, SdrMarkList *& o_rpMarkList)
    {
        o_rpSelFormat = m_pSelFormat;
        o_rpMarkList = m_pMarkList;
    }

    void SetUndoOffset(size_t nUndoOffset) { m_nUndoOffset = nUndoOffset; }

    size_t GetUndoOffset() override { return m_nUndoOffset; }

private:
    SwDoc & m_rDoc;
    IShellCursorSupplier & m_rCursorSupplier;
    SwFrameFormat * m_pSelFormat;
    SdrMarkList * m_pMarkList;
    size_t m_nUndoOffset = 0;
};

class RepeatContext final
    : public SfxRepeatTarget
{
public:
    RepeatContext(SwDoc & rDoc, SwPaM & rPaM)
        : m_rDoc(rDoc)
        , m_pCurrentPaM(& rPaM)
        , m_bDeleteRepeated(false)
    { }

    SwDoc & GetDoc() const { return m_rDoc; }

    SwPaM & GetRepeatPaM()
    {
        return *m_pCurrentPaM;
    }

private:
    friend class ::sw::UndoManager;
    friend class ::SwUndoDelete;

    SwDoc & m_rDoc;
    SwPaM * m_pCurrentPaM;
    bool m_bDeleteRepeated; /// has a delete action been repeated?
};

} // namespace sw

class SwUndoFormatColl final : public SwUndo, private SwUndRng
{
    UIName maFormatName;
    std::unique_ptr<SwHistory> mpHistory;
    // for correct <ReDo(..)> and <Repeat(..)>
    // boolean, which indicates that the attributes are reset at the nodes
    // before the format has been applied.
    const bool mbReset;
    // boolean, which indicates that the list attributes had been reset at
    // the nodes before the format has been applied.
    const bool mbResetListAttrs;

    void DoSetFormatColl(SwDoc & rDoc, SwPaM const & rPaM);

public:
    SwUndoFormatColl( const SwPaM&, const SwFormatColl*,
                   const bool bReset,
                   const bool bResetListAttrs );
    virtual ~SwUndoFormatColl() override;

    virtual void UndoImpl( ::sw::UndoRedoContext & ) override;
    virtual void RedoImpl( ::sw::UndoRedoContext & ) override;
    virtual void RepeatImpl( ::sw::RepeatContext & ) override;

    /**
       Returns the rewriter for this undo object.

       The rewriter contains one rule:

           $1 -> <name of format collection>

       <name of format collection> is the name of the format
       collection that is applied by the action recorded by this undo
       object.

       @return the rewriter for this undo object
    */
    virtual SwRewriter GetRewriter() const override;

    SwHistory* GetHistory() { return mpHistory.get(); }

};

class SwUndoSetFlyFormat final : public SwUndo, public SwClient
{
    SwFrameFormat* m_pFrameFormat;                  // saved FlyFormat
    const UIName m_DerivedFromFormatName;
    const UIName m_NewFormatName;
    std::optional<SfxItemSet> m_oItemSet;               // the re-/ set attributes
    SwNodeOffset m_nOldNode, m_nNewNode;
    sal_Int32 m_nOldContent, m_nNewContent;
    RndStdIds m_nOldAnchorType, m_nNewAnchorType;
    bool m_bAnchorChanged;

    void PutAttr( sal_uInt16 nWhich, const SfxPoolItem* pItem );
    void SwClientNotify(const SwModify&, const SfxHint&) override;
    void GetAnchor( SwFormatAnchor& rAnhor, SwNodeOffset nNode, sal_Int32 nContent );

public:
    SwUndoSetFlyFormat( SwFrameFormat& rFlyFormat, const SwFrameFormat& rNewFrameFormat );
    virtual ~SwUndoSetFlyFormat() override;

    virtual void UndoImpl( ::sw::UndoRedoContext & ) override;
    virtual void RedoImpl( ::sw::UndoRedoContext & ) override;

    virtual SwRewriter GetRewriter() const override;
};

class SwUndoOutlineLeftRight final : public SwUndo, private SwUndRng
{
    short m_nOffset;

public:
    SwUndoOutlineLeftRight( const SwPaM& rPam, short nOffset );

    virtual void UndoImpl( ::sw::UndoRedoContext & ) override;
    virtual void RedoImpl( ::sw::UndoRedoContext & ) override;
    virtual void RepeatImpl( ::sw::RepeatContext & ) override;
};

class SwUndoOutlineEdit final : public SwUndo, private SwUndRng
{
    SwNumRule m_aNewNumRule;
    SwNumRule m_aOldNumRule;

public:
    SwUndoOutlineEdit(const SwNumRule& rOldRule, const SwNumRule& rNewRule, const SwDoc& rDoc);

    virtual void UndoImpl(::sw::UndoRedoContext&) override;
    virtual void RedoImpl(::sw::UndoRedoContext&) override;
    virtual void RepeatImpl(::sw::RepeatContext&) override;
};

const int nUndoStringLength = 20;

/**
   Shortens a string to a maximum length.

   @param rStr      the string to be shortened
   @param nLength   the maximum length for rStr
   @param aFillStr  string to replace cut out characters with

   If rStr has less than nLength characters it will be returned unaltered.

   If rStr has more than nLength characters the following algorithm
   generates the shortened string:

       frontLength = (nLength - length(aFillStr)) / 2
       rearLength = nLength - length(aFillStr) - frontLength
       shortenedString = concat(<first frontLength characters of rStr,
                                aFillStr,
                                <last rearLength characters of rStr>)

   Preconditions:
      - nLength - length(aFillStr) >= 2

   @return the shortened string
 */
OUString
ShortenString(const OUString & rStr, sal_Int32 nLength, std::u16string_view aFillStr);
/**
   Denotes special characters in a string.

   The rStr is split into parts containing special characters and
   parts not containing special characters. In a part containing
   special characters all characters are equal. These parts are
   maximal.

   @param aStr     the string to denote in
   @param bQuoted  add quotation marks to the text

   The resulting string is generated by concatenating the found
   parts. The parts without special characters are surrounded by
   "'". The parts containing special characters are denoted as "n x",
   where n is the length of the part and x is the representation of
   the special character (i. e. "tab(s)").

   @return the denoted string
*/
OUString DenoteSpecialCharacters(std::u16string_view aStr, bool bQuoted = true);

#endif // INCLUDED_SW_SOURCE_CORE_INC_UNDOCORE_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
