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

#ifndef INCLUDED_SW_SOURCE_CORE_INC_FLYFRM_HXX
#define INCLUDED_SW_SOURCE_CORE_INC_FLYFRM_HXX

#include "layfrm.hxx"
#include <vector>
#include <frmfmt.hxx>
#include <anchoredobject.hxx>
#include <swdllapi.h>

class SwFormatAnchor;
class SwPageFrame;
class SwFormatFrameSize;
struct SwCursorMoveState;
class SwBorderAttrs;
class SwVirtFlyDrawObj;
class SwAttrSetChg;
namespace tools { class PolyPolygon; }
class SwFormat;
class SwViewShell;
class SwFEShell;
class SwWrtShell;
class SwFlyAtContentFrame;


/** search an anchor for paragraph bound frames starting from pOldAnch

    needed for dragging of objects bound to a paragraph for showing an anchor
    indicator as well as for changing the anchor.

    implemented in layout/flycnt.cxx
 */
const SwContentFrame *FindAnchor( const SwFrame *pOldAnch, const Point &rNew,
                              const bool bBody = false );

/** calculate rectangle in that the object can be moved or rather be resized */
bool CalcClipRect( const SdrObject *pSdrObj, SwRect &rRect, bool bMove = true );

enum class SwFlyFrameInvFlags : sal_uInt8
{
    NONE = 0x00,
    InvalidatePos = 0x01,
    InvalidateSize = 0x02,
    InvalidatePrt = 0x04,
    SetNotifyBack = 0x08,
    SetCompletePaint = 0x10,
    InvalidateBrowseWidth = 0x20,
    ClearContourCache = 0x40,
    UpdateObjInSortedList = 0x80,
};

namespace o3tl {
    template<> struct typed_flags<SwFlyFrameInvFlags> : is_typed_flags<SwFlyFrameInvFlags, 0x00ff> {};
}


/** general base class for all free-flowing frames

    #i26791# - inherit also from <SwAnchoredFlyFrame>
*/
class SAL_DLLPUBLIC_RTTI SwFlyFrame : public SwLayoutFrame, public SwAnchoredObject
{
    // is allowed to lock, implemented in frmtool.cxx
    friend void AppendObj(SwFrame *const pFrame, SwPageFrame *const pPage, SwFrameFormat *const pFormat, const SwFormatAnchor & rAnch);
    friend void Notify( SwFlyFrame *, SwPageFrame *pOld, const SwRect &rOld,
                        const SwRect* pOldPrt );

    void InitDrawObj(SwFrame&); // these to methods are called in the
    void FinitDrawObj();    // constructors

    void UpdateAttr_( const SfxPoolItem*, const SfxPoolItem*, SwFlyFrameInvFlags &,
                      SwAttrSetChg *pa = nullptr, SwAttrSetChg *pb = nullptr );
    void UpdateAttrForFormatChange( SwFormat* pOldFormat, SwFormat* pNewFormat, SwFlyFrameInvFlags & );

    using SwLayoutFrame::CalcRel;

protected:
    // Predecessor/Successor for chaining with text flow
    SwFlyFrame *m_pPrevLink, *m_pNextLink;
   static const SwFormatAnchor* GetAnchorFromPoolItem(const SfxPoolItem& rItem);
   static const SwFormatAnchor* GetAnchorFromPoolItem(const SwAttrSetChg& rItem);

private:
    // It must be possible to block Content-bound flys so that they will be not
    // formatted; in this case MakeAll() returns immediately. This is necessary
    // for page changes during formatting. In addition, it is needed during
    // the constructor call of the root object since otherwise the anchor will
    // be formatted before the root is anchored correctly to a shell and
    // because too much would be formatted as a result.
    bool m_bLocked :1;
    // true if the background of NotifyDTor needs to be notified at the end
    // of a MakeAll() call.
    bool m_bNotifyBack :1;

protected:
    // Pos, PrtArea or SSize have been invalidated - they will be evaluated
    // again immediately because they have to be valid _at all time_.
    // The invalidation is tracked here so that LayAction knows about it and
    // can handle it properly. Exceptions prove the rule.
    bool m_bInvalid :1;

    // true if the proposed height of an attribute is a minimal height
    // (this means that the frame can grow higher if needed)
    bool m_bMinHeight :1;
    // true if the fly frame could not format position/size based on its
    // attributes, e.g. because there was not enough space.
    bool m_bHeightClipped :1;
    bool m_bWidthClipped :1;
    // If true then call only the format after adjusting the width (CheckClip);
    // but the width will not be re-evaluated based on the attributes.
    bool m_bFormatHeightOnly :1;

    bool m_bInCnt :1;        ///< RndStdIds::FLY_AS_CHAR, anchored as character
    bool m_bAtCnt :1;        ///< RndStdIds::FLY_AT_PARA, anchored at paragraph
                             ///< or RndStdIds::FLY_AT_CHAR
    bool m_bLayout :1;       ///< RndStdIds::FLY_AT_PAGE, RndStdIds::FLY_AT_FLY, at page or at frame
    bool m_bAutoPosition :1; ///< RndStdIds::FLY_AT_CHAR, anchored at character
    bool m_bDeleted :1;      ///< Anchored to a tracked deletion
    size_t m_nAuthor;        ///< Redline author index for colored crossing out

    friend class SwNoTextFrame; // is allowed to call NotifyBackground

    Point m_aContentPos;        // content area's position relatively to Frame
    bool m_bValidContentPos;

    virtual void Format( vcl::RenderContext* pRenderContext, const SwBorderAttrs *pAttrs = nullptr ) override;
    void MakePrtArea( const SwBorderAttrs &rAttrs );
    void MakeContentPos( const SwBorderAttrs &rAttrs );

    void Lock()         { m_bLocked = true; }
    void Unlock()       { m_bLocked = false; }

    Size CalcRel( const SwFormatFrameSize &rSz ) const;

    SwFlyFrame( SwFlyFrameFormat*, SwFrame*, SwFrame *pAnchor, bool bFollow = false );

    virtual void DestroyImpl() override;
    virtual ~SwFlyFrame() override;

    /** method to assure that anchored object is registered at the correct
        page frame
    */
    virtual void RegisterAtCorrectPage() override;

    virtual bool SetObjTop_( const SwTwips _nTop ) override;
    virtual bool SetObjLeft_( const SwTwips _nLeft ) override;

    virtual SwRect GetObjBoundRect() const override;
    virtual void SwClientNotify(const SwModify& rMod, const SfxHint& rHint) override;

    virtual const IDocumentDrawModelAccess& getIDocumentDrawModelAccess( ) override;

    SwTwips CalcContentHeight(const SwBorderAttrs *pAttrs, const SwTwips nMinHeight, const SwTwips nUL);

public:
    // #i26791#
    virtual void PaintSwFrame( vcl::RenderContext& rRenderContext, SwRect const&, PaintFrameMode mode = PAINT_ALL ) const override;
    virtual Size ChgSize( const Size& aNewSize ) override;
    virtual bool GetModelPositionForViewPoint( SwPosition *, Point&,
                              SwCursorMoveState* = nullptr, bool bTestBackground = false ) const override;

    virtual void CheckDirection( bool bVert ) override;
    virtual void Cut() override;
#ifdef DBG_UTIL
    virtual void Paste( SwFrame* pParent, SwFrame* pSibling = nullptr ) override;
#endif

    bool    IsResizeValid(const SwBorderAttrs *pAttrs, Size aTargetSize);
    SwTwips Shrink_( SwTwips, bool bTst );
    SwTwips Grow_(SwTwips, SwResizeLimitReason&, bool bTst);
    void    Invalidate_( SwPageFrame const *pPage = nullptr );

    bool FrameSizeChg( const SwFormatFrameSize & );

    SwFlyFrame *GetPrevLink() const { return m_pPrevLink; }
    SwFlyFrame *GetNextLink() const { return m_pNextLink; }

    static void ChainFrames( SwFlyFrame &rMaster, SwFlyFrame &rFollow );
    static void UnchainFrames( SwFlyFrame &rMaster, SwFlyFrame &rFollow );

    SwFlyFrame *FindChainNeighbour( SwFrameFormat const &rFormat, SwFrame *pAnch = nullptr );

    /// Is this fly allowed to split across pages? (Disabled by default.)
    bool IsFlySplitAllowed() const;

    // #i26791#
    const SwVirtFlyDrawObj* GetVirtDrawObj() const;
    SwVirtFlyDrawObj *GetVirtDrawObj();
    void NotifyDrawObj();

    void ChgRelPos( const Point &rAbsPos );
    bool IsInvalid() const { return m_bInvalid; }
    void Invalidate() const { const_cast<SwFlyFrame*>(this)->m_bInvalid = true; }
    void Validate() const { const_cast<SwFlyFrame*>(this)->m_bInvalid = false; }

    bool IsMinHeight()  const { return m_bMinHeight; }
    bool IsLocked()     const { return m_bLocked; }
    bool IsAutoPos()    const { return m_bAutoPosition; }
    bool IsFlyInContentFrame() const { return m_bInCnt; }
    bool IsFlyFreeFrame() const { return m_bAtCnt || m_bLayout; }
    bool IsFlyLayFrame() const { return m_bLayout; }
    bool IsFlyAtContentFrame() const { return m_bAtCnt; }
    bool IsDeleted() const { return m_bDeleted; }
    void SetDeleted(bool bDeleted) { m_bDeleted = bDeleted; }
    void SetAuthor( size_t nAuthor ) { m_nAuthor = nAuthor; }
    size_t GetAuthor() const { return m_nAuthor; }

    bool IsNotifyBack() const { return m_bNotifyBack; }
    void SetNotifyBack()      { m_bNotifyBack = true; }
    void ResetNotifyBack()    { m_bNotifyBack = false; }

    bool IsClipped()        const   { return m_bHeightClipped || m_bWidthClipped; }
    bool IsHeightClipped()  const   { return m_bHeightClipped; }

    bool IsLowerOf( const SwLayoutFrame* pUpper ) const;
    bool IsUpperOf( const SwFlyFrame& _rLower ) const
    {
        return _rLower.IsLowerOf( this );
    }

    SwFrame *FindLastLower();

    // #i13147# - add parameter <_bForPaint> to avoid load of
    // the graphic during paint. Default value: false
    bool GetContour( tools::PolyPolygon&   rContour,
                     const bool _bForPaint = false ) const;

    // Paint on this shell (consider Preview, print flag, etc. recursively)?
    static bool IsPaint(SdrObject *pObj, const SwViewShell& rSh);

    /** SwFlyFrame::IsBackgroundTransparent

        determines if background of fly frame has to be drawn transparently

        definition found in /core/layout/paintfrm.cxx

        @return true, if background color is transparent or an existing background
        graphic is transparent.
    */
    bool IsBackgroundTransparent() const;

    void Chain( SwFrame* _pAnchor );
    void Unchain();
    void InsertCnt();
    void DeleteCnt();
    void InsertColumns();

    // #i26791# - pure virtual methods of base class <SwAnchoredObject>
    virtual void MakeObjPos() override;
    virtual void InvalidateObjPos() override;
    virtual void RegisterAtPage(SwPageFrame&) override;

    virtual SwFrameFormat* GetFrameFormat() override;
    virtual const SwFrameFormat* GetFrameFormat() const override;

    virtual SwRect GetObjRect() const override;

    /** method to determine if a format on the Writer fly frame is possible

        #i28701#
        refine 'IsFormatPossible'-conditions of method
        <SwAnchoredObject::IsFormatPossible()> by:
        format isn't possible, if Writer fly frame is locked resp. col-locked.
    */
    virtual bool IsFormatPossible() const override;
    static void GetAnchoredObjects( std::vector<SwAnchoredObject*>&, const SwFormat& rFormat );

    // overwriting "SwFrameFormat *SwLayoutFrame::GetFormat" to provide the correct derived return type.
    // (This is in order to skip on the otherwise necessary casting of the result to
    // 'SwFlyFrameFormat *' after calls to this function. The casting is now done in this function.)
    virtual const SwFlyFrameFormat *GetFormat() const override;
    SW_DLLPUBLIC virtual SwFlyFrameFormat *GetFormat() override;

    virtual void dumpAsXml(xmlTextWriterPtr writer = nullptr) const override;

    virtual void Calc(vcl::RenderContext* pRenderContext) const override;

    const Point& ContentPos() const { return m_aContentPos; }
    Point& ContentPos() { return m_aContentPos; }

    void InvalidateContentPos();

    void SelectionHasChanged(SwFEShell* pShell);
    SW_DLLPUBLIC bool IsShowUnfloatButton(SwWrtShell* pWrtSh) const;

    // For testing only (see uiwriter)
    SW_DLLPUBLIC void ActiveUnfloatButton(SwWrtShell* pWrtSh);

    virtual const SwFlyFrame* DynCastFlyFrame() const override;
    virtual SwFlyFrame* DynCastFlyFrame() override;

    SW_DLLPUBLIC SwFlyAtContentFrame* DynCastFlyAtContentFrame();

private:
    void UpdateUnfloatButton(SwWrtShell* pWrtSh, bool bShow) const;
    void PaintDecorators() const;
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
