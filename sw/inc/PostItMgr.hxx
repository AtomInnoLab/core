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
#include "swdllapi.h"

#include <cstddef>
#include <memory>
#include <vector>
#include <rtl/ustring.hxx>
#include <sal/log.hxx>
#include <tools/link.hxx>
#include "swrect.hxx"
#include <unotools/configitem.hxx>
#include <com/sun/star/uno/Any.hxx>
#include "SidebarWindowsTypes.hxx"
#include <editeng/outlobj.hxx>
#include <svl/lstner.hxx>
#include <vcl/vclptr.hxx>

class OutputDevice;
class SwWrtShell;
class SwView;
class SwPostItField;
class SwFormatField;
class SfxBroadcaster;
class SfxHint;
class SwEditWin;
class Color;
class SfxItemSet;
class SvxSearchItem;
namespace sw::annotation { class SwAnnotationWin; }
namespace sw::sidebarwindows { class SwFrameSidebarWinContainer; }
class SwAnnotationItem;
class SwFrame;
namespace vcl { class Window; }
struct ImplSVEvent;
class OutlinerParaObject;
namespace i18nutil { struct SearchOptions2; }

#define COL_NOTES_SIDEPANE_ARROW_ENABLED    Color(0,0,0)
#define COL_NOTES_SIDEPANE_ARROW_DISABLED   Color(172,168,153)

struct SwPostItPageItem
{
    bool bScrollbar;
    sw::sidebarwindows::SidebarPosition eSidebarPosition;
    tools::Long lOffset;
    SwRect mPageRect;
    std::vector<SwAnnotationItem*> mvSidebarItems;
    SwPostItPageItem(): bScrollbar(false), eSidebarPosition( sw::sidebarwindows::SidebarPosition::NONE ), lOffset(0)
    {
    }
};

struct FieldShadowState
{
    const SwPostItField* mpShadowField;
    bool bCursor;
    bool bMouse;
    FieldShadowState(): mpShadowField(nullptr),bCursor(false),bMouse(false)
    {
    }
};

class SAL_DLLPUBLIC_RTTI SwPostItMgr final : public SfxListener,
                                             public SfxBroadcaster
{
    private:
        SwView*                         mpView;
        SwWrtShell*                     mpWrtShell;
        VclPtr<SwEditWin>               mpEditWin;
        std::vector<std::unique_ptr<SwAnnotationItem>>  mvPostItFields;
        std::vector<std::unique_ptr<SwPostItPageItem>>  mPages;
        ImplSVEvent *                   mnEventId;
        bool                            mbWaitingForCalcRects;
        VclPtr<sw::annotation::SwAnnotationWin> mpActivePostIt;
        bool                            mbLayout;
        tools::Long                            mbLayoutHeight;
        bool                            mbLayouting;
        bool                            mbReadOnly;
        bool                            mbDeleteNote;
        FieldShadowState                mShadowState;
        std::optional<OutlinerParaObject> mpAnswer;
        OUString                        maAnswerText;

        // data structure to collect the <SwAnnotationWin> instances for certain <SwFrame> instances.
        std::unique_ptr<sw::sidebarwindows::SwFrameSidebarWinContainer> mpFrameSidebarWinContainer;

        void            AddPostIts(bool bCheckExistence = true,bool bFocus = true);
        void            RemoveSidebarWin();
        void            PreparePageContainer();
        void            Scroll(const tools::Long lScroll,const tools::ULong aPage );
        void            AutoScroll(const sw::annotation::SwAnnotationWin* pPostIt,const tools::ULong aPage );
        bool            ScrollbarHit(const tools::ULong aPage,const Point &aPoint);
        bool            LayoutByPage( std::vector<sw::annotation::SwAnnotationWin*> &aVisiblePostItList,
                                      const tools::Rectangle& rBorder,
                                      tools::Long lNeededHeight);
        // return true if a postit was found to have been removed
        bool            CheckForRemovedPostIts();
        bool            ArrowEnabled(sal_uInt16 aDirection,tools::ULong aPage) const;
        bool            BorderOverPageBorder(tools::ULong aPage) const;
        bool            HasScrollbars() const;
        void            Focus(const SfxBroadcaster& rBC);

        sal_Int32       GetInitialAnchorDistance() const;
        sal_Int32       GetScrollSize() const;
        sal_Int32       GetSpaceBetween() const;
        void            SetReadOnlyState();
        DECL_DLLPRIVATE_LINK( CalcHdl, void*, void);

        sw::annotation::SwAnnotationWin* GetSidebarWin(const SfxBroadcaster* pBroadcaster) const;

        SwAnnotationItem*  InsertItem( SfxBroadcaster* pItem, bool bCheckExistence, bool bFocus);
        void            RemoveItem( SfxBroadcaster* pBroadcast );

#if defined(YRS)
    public:
#endif
        VclPtr<sw::annotation::SwAnnotationWin> GetOrCreateAnnotationWindow(SwAnnotationItem& rItem, bool& rCreated);

    public:
        SwPostItMgr(SwView* aDoc);
        virtual ~SwPostItMgr() override;

        typedef std::vector< std::unique_ptr<SwAnnotationItem> >::const_iterator const_iterator;
        const_iterator begin()  const { return mvPostItFields.begin(); }
        const_iterator end()    const { return mvPostItFields.end();  }
        const std::vector<std::unique_ptr<SwAnnotationItem>>& GetPostItFields() { return mvPostItFields; }

        void Notify( SfxBroadcaster& rBC, const SfxHint& rHint ) override;

        std::vector<SwFormatField*> UpdatePostItsParentInfo();
        void LayoutPostIts();
        bool CalcRects();

        void MakeVisible( const sw::annotation::SwAnnotationWin* pPostIt);

        bool ShowScrollbar(const tools::ULong aPage) const;
        bool HasNotes() const ;
        bool ShowNotes() const;
        void SetSidebarWidth(const Point& rPointLogic);
        tools::Rectangle GetSidebarRect(const Point& rPointLogic);
        tools::ULong GetSidebarWidth(bool bPx = false) const;
        tools::ULong GetSidebarBorderWidth(bool bPx = false) const;

        void PrepareView(bool bIgnoreCount = false);

        void CorrectPositions();

        void SetLayout() { mbLayout = true; };
        void Delete(const OUString& aAuthor);
        void Delete(sal_uInt32 nPostItId);
        void Delete();
        void DeleteCommentThread(sal_uInt32 nPostItId);
        void ToggleResolved(sal_uInt32 nPostItId);
        void ToggleResolvedForThread(sal_uInt32 nPostItId);
        void PromoteToRoot(sal_uInt32 nPostItId);
        void MoveSubthreadToRoot(const sw::annotation::SwAnnotationWin* pNewRoot);

        sw::annotation::SwAnnotationWin* GetRemovedAnnotationWin(const SfxBroadcaster* pBroadcast);

        void ExecuteFormatAllDialog(SwView& rView);
        void FormatAll(const SfxItemSet &rNewAttr);

        void Hide( std::u16string_view rAuthor );
        void Hide();
        void UpdateResolvedStatus(const sw::annotation::SwAnnotationWin* topNote);
        void ShowHideResolvedNotes(bool visible);

        void Rescale();

        tools::Rectangle GetBottomScrollRect(const tools::ULong aPage) const;
        tools::Rectangle GetTopScrollRect(const tools::ULong aPage) const;

        bool IsHit(const Point &aPointPixel);
        /// Get the matching window that is responsible for handling mouse events of rPointLogic, if any.
        vcl::Window* IsHitSidebarWindow(const Point& rPointLogic);
        bool IsHitSidebarDragArea(const Point& rPointLogic);
        Color GetArrowColor(sal_uInt16 aDirection, tools::ULong aPage) const;

        sw::annotation::SwAnnotationWin* GetAnnotationWin(const SwPostItField* pField) const;
        sw::annotation::SwAnnotationWin* GetAnnotationWin(const sal_uInt32 nPostItId) const;

        sw::annotation::SwAnnotationWin* GetNextPostIt( sal_uInt16 aDirection,
                                                        sw::annotation::SwAnnotationWin* aPostIt);
        SwPostItField* GetLatestPostItField();
        sw::annotation::SwAnnotationWin* GetOrCreateAnnotationWindowForLatestPostItField();

        tools::Long GetNextBorder();

        sw::annotation::SwAnnotationWin* GetActiveSidebarWin() { return mpActivePostIt; }
        void SetActiveSidebarWin( sw::annotation::SwAnnotationWin* p);
        SW_DLLPUBLIC bool HasActiveSidebarWin() const;
        bool HasActiveAnnotationWin() const;
        void GrabFocusOnActiveSidebarWin();
        SW_DLLPUBLIC void UpdateDataOnActiveSidebarWin();
        void DeleteActiveSidebarWin();
        void HideActiveSidebarWin();
        void ToggleInsModeOnActiveSidebarWin();

        sal_Int32 GetMinimumSizeWithMeta() const;
        sal_Int32 GetSidebarScrollerHeight() const;

        void SetShadowState(const SwPostItField* pField,bool bCursor = true);

        void SetSpellChecking();

        static Color           GetColorDark(std::size_t aAuthorIndex);
        static Color           GetColorLight(std::size_t aAuthorIndex);
        static Color           GetColorAnchor(std::size_t aAuthorIndex);

        void                RegisterAnswer(const OutlinerParaObject& rAnswer) { mpAnswer = rAnswer; }
        OutlinerParaObject* IsAnswer() { return mpAnswer ? &*mpAnswer : nullptr; }
        void                RegisterAnswerText(const OUString& rAnswerText) { maAnswerText = rAnswerText; }
        const OUString&     GetAnswerText() const { return maAnswerText; }
        void CheckMetaText();
        void UpdateColors();

        sal_uInt16 Replace(SvxSearchItem const * pItem);
        sal_uInt16 SearchReplace(const SwFormatField &pField, const i18nutil::SearchOptions2& rSearchOptions,bool bSrchForward);
        sal_uInt16 FinishSearchReplace(const i18nutil::SearchOptions2& rSearchOptions,bool bSrchForward);

        void AssureStdModeAtShell();

        void ConnectSidebarWinToFrame( const SwFrame& rFrame,
                                     const SwFormatField& rFormatField,
                                     sw::annotation::SwAnnotationWin& rSidebarWin );
        void DisconnectSidebarWinFromFrame( const SwFrame& rFrame,
                                          sw::annotation::SwAnnotationWin& rSidebarWin );
        bool HasFrameConnectedSidebarWins( const SwFrame& rFrame );
        vcl::Window* GetSidebarWinForFrameByIndex( const SwFrame& rFrame,
                                            const sal_Int32 nIndex );
        std::vector<vcl::Window*> GetAllSidebarWinForFrame(const SwFrame& rFrame);

        void DrawNotesForPage(OutputDevice *pOutDev, sal_uInt32 nPage);
        void PaintTile(OutputDevice& rRenderContext);

        sw::sidebarwindows::SidebarPosition GetSidebarPos(const Point& rPointLogic);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
