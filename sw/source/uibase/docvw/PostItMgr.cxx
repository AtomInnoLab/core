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

#include <config_wasm_strip.h>

#include <boost/property_tree/json_parser.hpp>

#include <PostItMgr.hxx>
#include <postithelper.hxx>

#include <AnnotationWin.hxx>
#include "frmsidebarwincontainer.hxx"
#include <accmap.hxx>

#include <SidebarWindowsConsts.hxx>
#include "AnchorOverlayObject.hxx"
#include "ShadowOverlayObject.hxx"

#include <utility>
#include <vcl/svapp.hxx>
#include <vcl/outdev.hxx>
#include <vcl/settings.hxx>

#include <chrdlgmodes.hxx>
#include <viewopt.hxx>
#include <view.hxx>
#include <docsh.hxx>
#include <wrtsh.hxx>
#include <doc.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentFieldsAccess.hxx>
#if defined(YRS)
#include <IDocumentState.hxx>
#endif
#include <docstyle.hxx>
#include <fldbas.hxx>
#include <fmtfld.hxx>
#include <docufld.hxx>
#include <edtwin.hxx>
#include <txtfld.hxx>
#include <txtannotationfld.hxx>
#include <rootfrm.hxx>
#include <SwRewriter.hxx>
#include <tools/color.hxx>
#include <unotools/datetime.hxx>

#include <swmodule.hxx>
#include <strings.hrc>
#include <cmdid.h>

#include <sfx2/request.hxx>
#include <sfx2/event.hxx>
#include <svl/srchitem.hxx>

#include <svl/languageoptions.hxx>
#include <svl/hint.hxx>

#include <svx/svdview.hxx>
#include <editeng/eeitem.hxx>
#include <editeng/langitem.hxx>
#include <editeng/outliner.hxx>
#include <editeng/outlobj.hxx>

#include <comphelper/lok.hxx>
#include <comphelper/string.hxx>
#include <officecfg/Office/Writer.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>

#include <annotsh.hxx>
#include <swabstdlg.hxx>
#include <pagefrm.hxx>
#include <officecfg/Office/Common.hxx>

#include <memory>

// distance between Anchor Y and initial note position
#define POSTIT_INITIAL_ANCHOR_DISTANCE      20
//distance between two postits
#define POSTIT_SPACE_BETWEEN                8
#define POSTIT_MINIMUMSIZE_WITH_META        60
#define POSTIT_SCROLL_SIDEBAR_HEIGHT        20

// if we layout more often we stop, this should never happen
#define MAX_LOOP_COUNT                      50

using namespace sw::sidebarwindows;
using namespace sw::annotation;

namespace {

    enum class CommentNotificationType { Add, Remove, Modify, Resolve, RedlinedDeletion };

    bool comp_pos(const std::unique_ptr<SwAnnotationItem>& a, const std::unique_ptr<SwAnnotationItem>& b)
    {
        // sort by anchor position
        SwPosition aPosAnchorA = a->GetAnchorPosition();
        SwPosition aPosAnchorB = b->GetAnchorPosition();

        bool aAnchorAInFooter = false;
        bool aAnchorBInFooter = false;

        // is the anchor placed in Footnote or the Footer?
        if( aPosAnchorA.GetNode().FindFootnoteStartNode() || aPosAnchorA.GetNode().FindFooterStartNode() )
            aAnchorAInFooter = true;
        if( aPosAnchorB.GetNode().FindFootnoteStartNode() || aPosAnchorB.GetNode().FindFooterStartNode() )
            aAnchorBInFooter = true;

        // fdo#34800
        // if AnchorA is in footnote, and AnchorB isn't
        // we do not want to change over the position
        if( aAnchorAInFooter && !aAnchorBInFooter )
            return false;
        // if aAnchorA is not placed in a footnote, and aAnchorB is
        // force a change over
        else if( !aAnchorAInFooter && aAnchorBInFooter )
            return true;
        // If neither or both are in the footer, compare the positions.
        // Since footnotes are in Inserts section of nodes array and footers
        // in Autotext section, all footnotes precede any footers so no need
        // to check that.
        else
            return aPosAnchorA < aPosAnchorB;
    }

    /// Emits LOK notification about one addition/removal/change of a comment
    void lcl_CommentNotification(const SwView* pView, const CommentNotificationType nType, const SwAnnotationItem* pItem, const sal_uInt32 nPostItId)
    {
        if (!comphelper::LibreOfficeKit::isActive())
            return;

        boost::property_tree::ptree aAnnotation;
        aAnnotation.put("action", (nType == CommentNotificationType::Add ? "Add" :
                                   (nType == CommentNotificationType::Remove ? "Remove" :
                                    (nType == CommentNotificationType::Modify ? "Modify" :
                                     (nType == CommentNotificationType::RedlinedDeletion ? "RedlinedDeletion" :
                                      (nType == CommentNotificationType::Resolve ? "Resolve" : "???"))))));

        aAnnotation.put("id", nPostItId);
        if (nType != CommentNotificationType::Remove && pItem != nullptr)
        {
            sw::annotation::SwAnnotationWin* pWin = pItem->mpPostIt.get();

            const SwPostItField* pField = pWin->GetPostItField();
            const SwRect& aRect = pWin->GetAnchorRect();
            tools::Rectangle aSVRect(aRect.Pos().getX(),
                                    aRect.Pos().getY(),
                                    aRect.Pos().getX() + aRect.SSize().Width(),
                                    aRect.Pos().getY() + aRect.SSize().Height());

            if (!pItem->maLayoutInfo.mPositionFromCommentAnchor)
            {
                // Comments on frames: anchor position is the corner position, not the whole frame.
                aSVRect.SetSize(Size(0, 0));
            }

            std::vector<OString> aRects;
            for (const basegfx::B2DRange& aRange : pWin->GetAnnotationTextRanges())
            {
                const SwRect rect(aRange.getMinX(), aRange.getMinY(), aRange.getWidth(), aRange.getHeight());
                aRects.push_back(rect.SVRect().toString());
            }
            const OString sRects = comphelper::string::join("; ", aRects);

            aAnnotation.put("id", pField->GetPostItId());
            aAnnotation.put("parentId", pField->GetParentPostItId());
            aAnnotation.put("author", pField->GetPar1().toUtf8().getStr());
            // Note, for just plain text we could use "text" populated by pField->GetPar2()
            aAnnotation.put("html", pWin->GetSimpleHtml());
            aAnnotation.put("resolved", pField->GetResolved() ? "true" : "false");
            aAnnotation.put("dateTime", utl::toISO8601(pField->GetDateTime().GetUNODateTime()));
            aAnnotation.put("anchorPos", aSVRect.toString());
            aAnnotation.put("textRange", sRects.getStr());
            aAnnotation.put("layoutStatus", pItem->mLayoutStatus);
        }
        if (nType == CommentNotificationType::Remove && comphelper::LibreOfficeKit::isActive())
        {
            // Redline author is basically the author which has made the modification rather than author of the comments
            // This is important to know who removed the comment
            aAnnotation.put("author", SwModule::get()->GetRedlineAuthor(SwModule::get()->GetRedlineAuthor()));
        }

        boost::property_tree::ptree aTree;
        aTree.add_child("comment", aAnnotation);
        std::stringstream aStream;
        boost::property_tree::write_json(aStream, aTree);
        std::string aPayload = aStream.str();

        if (pView)
        {
            pView->libreOfficeKitViewCallback(LOK_CALLBACK_COMMENT, OString(aPayload));
        }
    }

    class FilterFunctor
    {
    public:
        virtual bool operator()(const SwFormatField* pField) const = 0;
        virtual ~FilterFunctor() {}
    };

    class IsPostitField : public FilterFunctor
    {
    public:
        bool operator()(const SwFormatField* pField) const override
        {
            return pField->GetField()->GetTyp()->Which() == SwFieldIds::Postit;
        }
    };

    class IsPostitFieldWithAuthorOf : public FilterFunctor
    {
        OUString m_sAuthor;
    public:
        explicit IsPostitFieldWithAuthorOf(OUString aAuthor)
            : m_sAuthor(std::move(aAuthor))
        {
        }
        bool operator()(const SwFormatField* pField) const override
        {
            if (pField->GetField()->GetTyp()->Which() != SwFieldIds::Postit)
                return false;
            return static_cast<const SwPostItField*>(pField->GetField())->GetPar1() == m_sAuthor;
        }
    };

    class IsPostitFieldWithPostitId : public FilterFunctor
    {
        sal_uInt32 m_nPostItId;
    public:
        explicit IsPostitFieldWithPostitId(sal_uInt32 nPostItId)
            : m_nPostItId(nPostItId)
            {}

        bool operator()(const SwFormatField* pField) const override
        {
            if (pField->GetField()->GetTyp()->Which() != SwFieldIds::Postit)
                return false;
            return static_cast<const SwPostItField*>(pField->GetField())->GetPostItId() == m_nPostItId;
        }
    };

    class IsFieldNotDeleted : public FilterFunctor
    {
    private:
        IDocumentRedlineAccess const& m_rIDRA;
        FilterFunctor const& m_rNext;

    public:
        IsFieldNotDeleted(IDocumentRedlineAccess const& rIDRA,
                const FilterFunctor & rNext)
            : m_rIDRA(rIDRA)
            , m_rNext(rNext)
        {
        }
        bool operator()(const SwFormatField* pField) const override
        {
            if (!m_rNext(pField))
                return false;
            if (!pField->GetTextField())
                return false;
            return !sw::IsFieldDeletedInModel(m_rIDRA, *pField->GetTextField());
        }
    };

    //Manages the passed in vector by automatically removing entries if they are deleted
    //and automatically adding entries if they appear in the document and match the
    //functor.
    //
    //This will completely refill in the case of a "anonymous" NULL pField stating
    //rather unhelpfully that "something changed" so you may process the same
    //Fields more than once.
    class FieldDocWatchingStack : public SfxListener
    {
        std::vector<std::unique_ptr<SwAnnotationItem>>& m_aSidebarItems;
        std::vector<const SwFormatField*> m_aFormatFields;
        SwDocShell& m_rDocShell;
        FilterFunctor& m_rFilter;

        virtual void Notify(SfxBroadcaster&, const SfxHint& rHint) override
        {
            if ( rHint.GetId() != SfxHintId::SwFormatField )
                return;
            const SwFormatFieldHint* pHint = static_cast<const SwFormatFieldHint*>(&rHint);

            bool bAllInvalidated = false;
            if (pHint->Which() == SwFormatFieldHintWhich::REMOVED)
            {
                const SwFormatField* pField = pHint->GetField();
                bAllInvalidated = pField == nullptr;
                if (!bAllInvalidated && m_rFilter(pField))
                {
                    EndListening(const_cast<SwFormatField&>(*pField));
                    std::erase(m_aFormatFields, pField);
                }
            }
            else if (pHint->Which() == SwFormatFieldHintWhich::INSERTED)
            {
                const SwFormatField* pField = pHint->GetField();
                bAllInvalidated = pField == nullptr;
                if (!bAllInvalidated && m_rFilter(pField))
                {
                    StartListening(const_cast<SwFormatField&>(*pField));
                    m_aFormatFields.push_back(pField);
                }
            }

            if (bAllInvalidated)
                FillVector();

            return;
        }

    public:
        FieldDocWatchingStack(std::vector<std::unique_ptr<SwAnnotationItem>>& in, SwDocShell &rDocShell, FilterFunctor& rFilter)
            : m_aSidebarItems(in)
            , m_rDocShell(rDocShell)
            , m_rFilter(rFilter)
        {
            FillVector();
            StartListening(m_rDocShell);
        }
        void FillVector()
        {
            EndListeningToAllFields();
            m_aFormatFields.clear();
            m_aFormatFields.reserve(m_aSidebarItems.size());
            for (auto const& p : m_aSidebarItems)
            {
                const SwFormatField& rField = p->GetFormatField();
                if (!m_rFilter(&rField))
                    continue;
                StartListening(const_cast<SwFormatField&>(rField));
                m_aFormatFields.push_back(&rField);
            }
        }
        void EndListeningToAllFields()
        {
            for (auto const& pField : m_aFormatFields)
            {
                EndListening(const_cast<SwFormatField&>(*pField));
            }
        }
        virtual ~FieldDocWatchingStack() override
        {
            EndListeningToAllFields();
            EndListening(m_rDocShell);
        }
        const SwFormatField* pop()
        {
            if (m_aFormatFields.empty())
                return nullptr;
            const SwFormatField* p = m_aFormatFields.back();
            EndListening(const_cast<SwFormatField&>(*p));
            m_aFormatFields.pop_back();
            return p;
        }
    };

} // anonymous namespace

SwPostItMgr::SwPostItMgr(SwView* pView)
    : mpView(pView)
    , mpWrtShell(mpView->GetDocShell()->GetWrtShell())
    , mpEditWin(&mpView->GetEditWin())
    , mnEventId(nullptr)
    , mbWaitingForCalcRects(false)
    , mpActivePostIt(nullptr)
    , mbLayout(false)
    , mbLayoutHeight(0)
    , mbLayouting(false)
    , mbReadOnly(mpView->GetDocShell()->IsReadOnly())
    , mbDeleteNote(true)
{
    if(!mpView->GetDrawView() )
        mpView->GetWrtShell().MakeDrawView();

    //make sure we get the colour yellow always, even if not the first one of comments or redlining
    SwModule::get()->GetRedlineAuthor();

    // collect all PostIts and redline comments that exist after loading the document
    // don't check for existence for any of them, don't focus them
    AddPostIts(false,false);
    /*  this code can be used once we want redline comments in the Sidebar
    AddRedlineComments(false,false);
    */
    // we want to receive stuff like SfxHintId::DocChanged
    StartListening(*mpView->GetDocShell());
    // listen to stylesheet pool to update on stylesheet rename,
    // as EditTextObject references styles by name.
    SfxStyleSheetBasePool* pStyleSheetPool = mpView->GetDocShell()->GetStyleSheetPool();
    if (pStyleSheetPool)
        StartListening(*static_cast<SwDocStyleSheetPool*>(pStyleSheetPool)->GetEEStyleSheetPool());
    if (!mvPostItFields.empty())
    {
        mbWaitingForCalcRects = true;
        mnEventId = Application::PostUserEvent( LINK( this, SwPostItMgr, CalcHdl) );
    }
}

SwPostItMgr::~SwPostItMgr()
{
    if ( mnEventId )
        Application::RemoveUserEvent( mnEventId );
    // forget about all our Sidebar windows
    RemoveSidebarWin();
    EndListeningAll();

    mPages.clear();
}

bool SwPostItMgr::CheckForRemovedPostIts()
{
    IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
    bool bRemoved = false;
    auto it = mvPostItFields.begin();
    while(it != mvPostItFields.end())
    {
        if (!(*it)->UseElement(*mpWrtShell->GetLayout(), rIDRA))
        {
            EndListening(const_cast<SfxBroadcaster&>(*(*it)->GetBroadcaster()));

            if((*it)->mpPostIt && (*it)->mpPostIt->GetPostItField())
                lcl_CommentNotification(mpView, CommentNotificationType::Remove, nullptr, (*it)->mpPostIt->GetPostItField()->GetPostItId());

            std::unique_ptr<SwAnnotationItem> p = std::move(*it);
            it = mvPostItFields.erase(it);
            if (GetActiveSidebarWin() == p->mpPostIt)
                SetActiveSidebarWin(nullptr);
            p->mpPostIt.disposeAndClear();

            if (comphelper::LibreOfficeKit::isActive() && !comphelper::LibreOfficeKit::isTiledAnnotations())
            {
                const SwPostItField* pPostItField = static_cast<const SwPostItField*>(p->GetFormatField().GetField());
                lcl_CommentNotification(mpView, CommentNotificationType::Remove, nullptr, pPostItField->GetPostItId());
            }

            bRemoved = true;
        }
        else
            ++it;
    }

    if ( !bRemoved )
        return false;

    // make sure that no deleted items remain in page lists
    // todo: only remove deleted ones?!
    if ( mvPostItFields.empty() )
    {
        PreparePageContainer();
        PrepareView();
    }
    else
    {
        // if postits are there make sure that page lists are not empty
        // otherwise sudden paints can cause pain (in BorderOverPageBorder)
        CalcRects();
    }

    return true;
}

SwAnnotationItem* SwPostItMgr::InsertItem(SfxBroadcaster* pItem, bool bCheckExistence, bool bFocus)
{
    if (bCheckExistence)
    {
        for (auto const& postItField : mvPostItFields)
        {
            if ( postItField->GetBroadcaster() == pItem )
                return nullptr;
        }
    }
    mbLayout = bFocus;

    SwAnnotationItem* pAnnotationItem = nullptr;
    if (auto pSwFormatField = dynamic_cast< SwFormatField *>( pItem ))
    {
        IsPostitField isPostitField;
        if (!isPostitField(pSwFormatField))
            return nullptr;
        mvPostItFields.push_back(std::make_unique<SwAnnotationItem>(*pSwFormatField, bFocus));
        pAnnotationItem = mvPostItFields.back().get();
    }
    assert(dynamic_cast< const SwFormatField *>( pItem ) && "Mgr::InsertItem: seems like new stuff was added");
    StartListening(*pItem);
    return pAnnotationItem;
}

sw::annotation::SwAnnotationWin* SwPostItMgr::GetRemovedAnnotationWin( const SfxBroadcaster* pBroadcast )
{
    auto i = std::find_if(mvPostItFields.begin(), mvPostItFields.end(),
        [&pBroadcast](const std::unique_ptr<SwAnnotationItem>& pField) { return pField->GetBroadcaster() == pBroadcast; });
    if (i != mvPostItFields.end())
    {
        return (*i)->mpPostIt;
    }
    return nullptr;
}

void SwPostItMgr::RemoveItem( SfxBroadcaster* pBroadcast )
{
    EndListening(*pBroadcast);
    auto i = std::find_if(mvPostItFields.begin(), mvPostItFields.end(),
        [&pBroadcast](const std::unique_ptr<SwAnnotationItem>& pField) { return pField->GetBroadcaster() == pBroadcast; });
    if (i != mvPostItFields.end())
    {
#if defined(YRS)
        mpView->GetDocShell()->GetDoc()->getIDocumentState().YrsRemoveComment(
            (*i)->GetAnchorPosition(),
            (*i)->mpPostIt->GetOutlinerView()->GetEditView().GetYrsCommentId());
#endif
        std::unique_ptr<SwAnnotationItem> p = std::move(*i);
        // tdf#120487 remove from list before dispose, so comment window
        // won't be recreated due to the entry still in the list if focus
        // transferring from the pPostIt triggers relayout of postits
        // tdf#133348 remove from list before calling SetActiveSidebarWin
        // so GetNextPostIt won't deal with mvPostItFields containing empty unique_ptr
        mvPostItFields.erase(i);
        if (GetActiveSidebarWin() == p->mpPostIt)
            SetActiveSidebarWin(nullptr);
        p->mpPostIt.disposeAndClear();
    }
    mbLayout = true;
    PrepareView();
}

void SwPostItMgr::Notify( SfxBroadcaster& rBC, const SfxHint& rHint )
{
    if (rHint.GetId() == SfxHintId::ThisIsAnSfxEventHint)
    {
        const SfxEventHint& rSfxEventHint = static_cast<const SfxEventHint&>(rHint);
        if (rSfxEventHint.GetEventId() == SfxEventHintId::SwEventLayoutFinished)
        {
            if ( !mbWaitingForCalcRects && !mvPostItFields.empty())
            {
                mbWaitingForCalcRects = true;
                mnEventId = Application::PostUserEvent( LINK( this, SwPostItMgr, CalcHdl) );
            }
        }
    }
    else if ( rHint.GetId() == SfxHintId::SwFormatField )
    {
        const SwFormatFieldHint * pFormatHint = static_cast<const SwFormatFieldHint*>(&rHint);
        SwFormatField* pField = const_cast <SwFormatField*>( pFormatHint->GetField() );
        switch ( pFormatHint->Which() )
        {
            case SwFormatFieldHintWhich::INSERTED :
            {
                if (!pField)
                {
                    AddPostIts();
                    break;
                }
                // get field to be inserted from hint
                if ( pField->IsFieldInDoc() )
                {
                    bool bEmpty = !HasNotes();
                    SwAnnotationItem* pItem = InsertItem( pField, true, false );

                    if (bEmpty && !mvPostItFields.empty())
                        PrepareView(true);

                    // True until the layout of this post it finishes
                    if (pItem)
                        pItem->mbPendingLayout = true;
                }
                else
                {
                    OSL_FAIL("Inserted field not in document!" );
                }
                break;
            }
            case SwFormatFieldHintWhich::REMOVED:
            case SwFormatFieldHintWhich::REDLINED_DELETION:
            {
                if (mbDeleteNote)
                {
                    if (!pField)
                    {
                        const bool bWasRemoved = CheckForRemovedPostIts();
                        // tdf#143643 ensure relayout on undo of insert comment
                        if (bWasRemoved)
                            mbLayout = true;
                        break;
                    }
                    this->Broadcast(rHint);
                    RemoveItem(pField);

                    // If LOK has disabled tiled annotations, emit annotation callbacks
                    if (comphelper::LibreOfficeKit::isActive() && !comphelper::LibreOfficeKit::isTiledAnnotations())
                    {
                        SwPostItField* pPostItField = static_cast<SwPostItField*>(pField->GetField());
                        auto type = pFormatHint->Which() == SwFormatFieldHintWhich::REMOVED ? CommentNotificationType::Remove: CommentNotificationType::RedlinedDeletion;
                        lcl_CommentNotification(mpView, type, nullptr, pPostItField->GetPostItId());
                    }
                }
                break;
            }
            case SwFormatFieldHintWhich::FOCUS:
            {
                if (pFormatHint->GetView()== mpView)
                    Focus(rBC);
                break;
            }
            case SwFormatFieldHintWhich::CHANGED:
            case SwFormatFieldHintWhich::RESOLVED:
            {
                SwFormatField* pFormatField = dynamic_cast<SwFormatField*>(&rBC);
                for (auto const& postItField : mvPostItFields)
                {
                    if ( pFormatField == postItField->GetBroadcaster() )
                    {
                        if (postItField->mpPostIt)
                        {
                            postItField->mpPostIt->SetPostItText();
                            mbLayout = true;
                            this->Forward(rBC, rHint);
                        }

                        // If LOK has disabled tiled annotations, emit annotation callbacks
                        if (comphelper::LibreOfficeKit::isActive() && !comphelper::LibreOfficeKit::isTiledAnnotations())
                        {
                            if(SwFormatFieldHintWhich::CHANGED == pFormatHint->Which())
                                lcl_CommentNotification(mpView, CommentNotificationType::Modify, postItField.get(), 0);
                            else
                                lcl_CommentNotification(mpView, CommentNotificationType::Resolve, postItField.get(), 0);
                        }
                        break;
                    }
                }
                break;
            }
        }
    }
    else if ( rHint.GetId() == SfxHintId::StyleSheetModifiedExtended )
    {
        const SfxStyleSheetModifiedHint * pStyleHint = static_cast<const SfxStyleSheetModifiedHint*>(&rHint);
        for (const auto& postItField : mvPostItFields)
        {
            auto pField = static_cast<SwPostItField*>(postItField->GetFormatField().GetField());
            pField->ChangeStyleSheetName(pStyleHint->GetOldName(), pStyleHint->GetStyleSheet());
        }
    }
    else
    {
        SfxHintId nId = rHint.GetId();
        switch ( nId )
        {
            case SfxHintId::ModeChanged:
            {
                if ( mbReadOnly != mpView->GetDocShell()->IsReadOnly() )
                {
                    mbReadOnly = !mbReadOnly;
                    SetReadOnlyState();
                    mbLayout = true;
                }
                break;
            }
            case SfxHintId::DocChanged:
            {
                if ( mpView->GetDocShell() == &rBC )
                {
                    if ( !mbWaitingForCalcRects && !mvPostItFields.empty())
                    {
                        mbWaitingForCalcRects = true;
                        mnEventId = Application::PostUserEvent( LINK( this, SwPostItMgr, CalcHdl) );
                    }
                }
                break;
            }
            case SfxHintId::LanguageChanged:
            {
                SetSpellChecking();
                break;
            }
            case SfxHintId::SwSplitNodeOperation:
            {
                // if we are in a SplitNode/Cut operation, do not delete note and then add again, as this will flicker
                mbDeleteNote = !mbDeleteNote;
                break;
            }
            case SfxHintId::Dying:
            {
                if ( mpView->GetDocShell() != &rBC )
                {
                    // field to be removed is the broadcaster
                    OSL_FAIL("Notification for removed SwFormatField was not sent!");
                    RemoveItem(&rBC);
                }
                break;
            }
            default: break;
        }
    }
}

void SwPostItMgr::Focus(const SfxBroadcaster& rBC)
{
    if (!mpWrtShell->GetViewOptions()->IsPostIts())
    {
        SfxRequest aRequest(mpView->GetViewFrame(), SID_TOGGLE_NOTES);
        mpView->ExecViewOptions(aRequest);
    }

    for (auto const& postItField : mvPostItFields)
    {
        // field to get the focus is the broadcaster
        if ( &rBC == postItField->GetBroadcaster() )
        {
            if (postItField->mpPostIt)
            {
                if (postItField->mpPostIt->IsResolved() &&
                        !mpWrtShell->GetViewOptions()->IsResolvedPostIts())
                {
                    SfxRequest aRequest(mpView->GetViewFrame(), SID_TOGGLE_RESOLVED_NOTES);
                    mpView->ExecViewOptions(aRequest);
                }
                postItField->mpPostIt->GrabFocus();
                MakeVisible(postItField->mpPostIt);
            }
            else
            {
                // when the layout algorithm starts, this postit is created and receives focus
                postItField->mbFocus = true;
            }
        }
    }
}

bool SwPostItMgr::CalcRects()
{
    if ( mnEventId )
    {
        // if CalcRects() was forced and an event is still pending: remove it
        // it is superfluous and also may cause reentrance problems if triggered while layouting
        Application::RemoveUserEvent( mnEventId );
        mnEventId = nullptr;
    }

    bool bChange = false;
    bool bRepair = false;
    PreparePageContainer();
    if ( !mvPostItFields.empty() )
    {
        IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
        for (auto const& pItem : mvPostItFields)
        {
            if (!pItem->UseElement(*mpWrtShell->GetLayout(), rIDRA))
            {
                OSL_FAIL("PostIt is not in doc or other wrong use");
                bRepair = true;
                continue;
            }
            const SwRect aOldAnchorRect( pItem->maLayoutInfo.mPosition );
            const SwPostItHelper::SwLayoutStatus eOldLayoutStatus = pItem->mLayoutStatus;
            const SwNodeOffset nOldStartNodeIdx( pItem->maLayoutInfo.mnStartNodeIdx );
            const sal_Int32 nOldStartContent( pItem->maLayoutInfo.mnStartContent );
            {
                // update layout information
                const SwTextAnnotationField* pTextAnnotationField =
                    dynamic_cast< const SwTextAnnotationField* >( pItem->GetFormatField().GetTextField() );
                const ::sw::mark::MarkBase* pAnnotationMark =
                    pTextAnnotationField != nullptr ? pTextAnnotationField->GetAnnotationMark() : nullptr;
                if ( pAnnotationMark != nullptr )
                {
                    pItem->mLayoutStatus =
                        SwPostItHelper::getLayoutInfos(
                            pItem->maLayoutInfo,
                            pItem->GetAnchorPosition(),
                            pAnnotationMark );
                }
                else
                {
                    pItem->mLayoutStatus =
                        SwPostItHelper::getLayoutInfos( pItem->maLayoutInfo, pItem->GetAnchorPosition() );
                }
            }
            bChange = bChange
                      || pItem->maLayoutInfo.mPosition != aOldAnchorRect
                      || pItem->mLayoutStatus != eOldLayoutStatus
                      || pItem->maLayoutInfo.mnStartNodeIdx != nOldStartNodeIdx
                      || pItem->maLayoutInfo.mnStartContent != nOldStartContent;
        }

        // show notes in right order in navigator
        //prevent Anchors during layout to overlap, e.g. when moving a frame
        if (mvPostItFields.size()>1 )
            std::stable_sort(mvPostItFields.begin(), mvPostItFields.end(), comp_pos);

        // sort the items into the right page vector, so layout can be done by page
        for (auto const& pItem : mvPostItFields)
        {
            if( SwPostItHelper::INVISIBLE == pItem->mLayoutStatus )
            {
                if (pItem->mpPostIt)
                    pItem->mpPostIt->HideNote();
                continue;
            }

            if( SwPostItHelper::HIDDEN == pItem->mLayoutStatus )
            {
                if (!mpWrtShell->GetViewOptions()->IsShowHiddenChar())
                {
                    if (pItem->mpPostIt)
                        pItem->mpPostIt->HideNote();
                    continue;
                }
            }

            const tools::ULong aPageNum = pItem->maLayoutInfo.mnPageNumber;
            if (aPageNum > mPages.size())
            {
                const tools::ULong nNumberOfPages = mPages.size();
                mPages.reserve(aPageNum);
                for (tools::ULong j=0; j<aPageNum - nNumberOfPages; ++j)
                    mPages.emplace_back( new SwPostItPageItem());
            }
            mPages[aPageNum-1]->mvSidebarItems.push_back(pItem.get());
            mPages[aPageNum-1]->mPageRect = pItem->maLayoutInfo.mPageFrame;
            mPages[aPageNum-1]->eSidebarPosition = pItem->maLayoutInfo.meSidebarPosition;
        }

        if (!bChange && mpWrtShell->getIDocumentSettingAccess().get(DocumentSettingId::BROWSE_MODE))
        {
            tools::Long nLayoutHeight = SwPostItHelper::getLayoutHeight( mpWrtShell->GetLayout() );
            if( nLayoutHeight > mbLayoutHeight )
            {
                if (mPages[0]->bScrollbar || HasScrollbars())
                    bChange = true;
            }
            else if( nLayoutHeight < mbLayoutHeight )
            {
                if (mPages[0]->bScrollbar || !BorderOverPageBorder(1))
                    bChange = true;
            }
        }
    }

    if ( bRepair )
        CheckForRemovedPostIts();

    mbLayoutHeight = SwPostItHelper::getLayoutHeight( mpWrtShell->GetLayout() );
    mbWaitingForCalcRects = false;
    return bChange;
}

bool SwPostItMgr::HasScrollbars() const
{
    for (auto const& postItField : mvPostItFields)
    {
        if (postItField->mbShow && postItField->mpPostIt && postItField->mpPostIt->HasScrollbar())
            return true;
    }
    return false;
}

void SwPostItMgr::PreparePageContainer()
{
    // we do not just delete the SwPostItPageItem, so offset/scrollbar is not lost
    tools::Long lPageSize = mpWrtShell->GetNumPages();
    tools::Long lContainerSize = mPages.size();

    if (lContainerSize < lPageSize)
    {
        mPages.reserve(lPageSize);
        for (tools::Long i=0; i<lPageSize - lContainerSize;i++)
            mPages.emplace_back( new SwPostItPageItem());
    }
    else if (lContainerSize > lPageSize)
    {
        for (int i=mPages.size()-1; i >= lPageSize;--i)
        {
            mPages.pop_back();
        }
    }
    // only clear the list, DO NOT delete the objects itself
    for (auto const& page : mPages)
    {
        page->mvSidebarItems.clear();
        if (mvPostItFields.empty())
            page->bScrollbar = false;
    }
}

VclPtr<SwAnnotationWin> SwPostItMgr::GetOrCreateAnnotationWindow(SwAnnotationItem& rItem, bool& rCreated)
{
    VclPtr<SwAnnotationWin> pPostIt = rItem.mpPostIt;
    if (!pPostIt)
    {
        pPostIt = rItem.GetSidebarWindow( mpView->GetEditWin(),
                                          *this );
        pPostIt->InitControls();
        pPostIt->SetReadonly(mbReadOnly);
        rItem.mpPostIt = pPostIt;
#if defined(YRS)
        SAL_INFO("sw.yrs", "YRS GetOrCreateAnnotationWindow " << rItem.mpPostIt);
#endif
        if (mpAnswer)
        {
            if (pPostIt->GetPostItField()->GetParentPostItId() != 0) //do we really have another note in front of this one
            {
                pPostIt->InitAnswer(*mpAnswer);
            }
            mpAnswer.reset();
        }

        rCreated = true;
    }
    return rItem.mpPostIt;
}

void SwPostItMgr::LayoutPostIts()
{
    const bool bLoKitActive = comphelper::LibreOfficeKit::isActive();
    const bool bTiledAnnotations = comphelper::LibreOfficeKit::isTiledAnnotations();
    const bool bShowNotes = ShowNotes();

    const bool bEnableMapMode = bLoKitActive && !mpEditWin->IsMapModeEnabled();
    if (bEnableMapMode)
        mpEditWin->EnableMapMode();

    std::set<VclPtr<SwAnnotationWin>> aCreatedPostIts;
    if ( !mvPostItFields.empty() && !mbWaitingForCalcRects )
    {
        mbLayouting = true;

        //loop over all pages and do the layout
        // - create SwPostIt if necessary
        // - place SwPostIts on their initial position
        // - calculate necessary height for all PostIts together
        bool bUpdate = false;
        for (std::unique_ptr<SwPostItPageItem>& pPage : mPages)
        {
            // only layout if there are notes on this page
            if (!pPage->mvSidebarItems.empty())
            {
                std::vector<SwAnnotationWin*> aVisiblePostItList;
                tools::ULong                  lNeededHeight = 0;

                for (auto const& pItem : pPage->mvSidebarItems)
                {
                    if (pItem->mbShow)
                    {
                        bool bCreated = false;
                        VclPtr<SwAnnotationWin> pPostIt = GetOrCreateAnnotationWindow(*pItem, bCreated);
                        if (bCreated)
                        {
                            // The annotation window was created for a previously existing, but not
                            // laid out comment.
                            aCreatedPostIts.insert(pPostIt);
                        }

                        pPostIt->SetChangeTracking(
                            pItem->mLayoutStatus,
                            GetColorAnchor(pItem->maLayoutInfo.mRedlineAuthor));
                        pPostIt->SetSidebarPosition(pPage->eSidebarPosition);

                        if (pPostIt->GetPostItField()->GetParentPostItId() != 0)
                            pPostIt->SetFollow(true);

                        tools::Long aPostItHeight = 0;
                        if (bShowNotes)
                        {
                            tools::Long mlPageBorder = 0;
                            tools::Long mlPageEnd = 0;

                            if (pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT )
                            {
                                // x value for notes positioning
                                mlPageBorder = mpEditWin->LogicToPixel( Point( pPage->mPageRect.Left(), 0)).X() - GetSidebarWidth(true);// - GetSidebarBorderWidth(true);
                                //bending point
                                mlPageEnd =
                                    mpWrtShell->getIDocumentSettingAccess().get(DocumentSettingId::BROWSE_MODE)
                                    ? pItem->maLayoutInfo.mPagePrtArea.Left()
                                    : pPage->mPageRect.Left() + 350;
                            }
                            else if (pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT )
                            {
                                // x value for notes positioning
                                mlPageBorder = mpEditWin->LogicToPixel( Point(pPage->mPageRect.Right(), 0)).X() + GetSidebarBorderWidth(true);
                                //bending point
                                mlPageEnd =
                                    mpWrtShell->getIDocumentSettingAccess().get(DocumentSettingId::BROWSE_MODE)
                                    ? pItem->maLayoutInfo.mPagePrtArea.Right() :
                                    pPage->mPageRect.Right() - 350;
                            }

                            tools::Long Y = mpEditWin->LogicToPixel( Point(0,pItem->maLayoutInfo.mPosition.Bottom())).Y();

                            aPostItHeight = ( pPostIt->GetPostItTextHeight() < pPostIt->GetMinimumSizeWithoutMeta()
                                              ? pPostIt->GetMinimumSizeWithoutMeta()
                                              : pPostIt->GetPostItTextHeight() )
                                            + pPostIt->GetMetaHeight();
                            pPostIt->SetPosSizePixelRect( mlPageBorder ,
                                                          Y - GetInitialAnchorDistance(),
                                                          GetSidebarWidth(true),
                                                          aPostItHeight,
                                                          mlPageEnd );
                        }

                        pPostIt->SetAnchorRect(pItem->maLayoutInfo.mPosition);

                        pPostIt->ChangeSidebarItem( *pItem );

                        if (pItem->mbFocus)
                        {
                            mbLayout = true;
                            pPostIt->GrabFocus();
                            pItem->mbFocus = false;
                        }
                        // only the visible postits are used for the final layout
                        aVisiblePostItList.push_back(pPostIt);
                        if (bShowNotes)
                            lNeededHeight += pPostIt->IsFollow() ? aPostItHeight : aPostItHeight+GetSpaceBetween();
                    }
                    else // we don't want to see it
                    {
                        VclPtr<SwAnnotationWin> pPostIt = pItem->mpPostIt;
                        if (pPostIt)
                            pPostIt->HideNote();
                    }
                    SwFormatField* pFormatField = &(pItem->GetFormatField());
                    SwFormatFieldHintWhich nWhich = SwFormatFieldHintWhich::INSERTED;
                    this->Broadcast(SwFormatFieldHint(pFormatField, nWhich, mpView));
                }

                if (!aVisiblePostItList.empty() && ShowNotes())
                {
                    bool bOldScrollbar = pPage->bScrollbar;
                    pPage->bScrollbar = LayoutByPage(aVisiblePostItList, pPage->mPageRect.SVRect(), lNeededHeight);
                    if (!pPage->bScrollbar)
                    {
                        pPage->lOffset = 0;
                    }
                    else if (sal_Int32 nScrollSize = GetScrollSize())
                    {
                        //when we changed our zoom level, the offset value can be too big, so let's check for the largest possible zoom value
                        tools::Long aAvailableHeight = mpEditWin->LogicToPixel(Size(0,pPage->mPageRect.Height())).Height() - 2 * GetSidebarScrollerHeight();
                        tools::Long lOffset = -1 * nScrollSize * (aVisiblePostItList.size() - aAvailableHeight / nScrollSize);
                        if (pPage->lOffset < lOffset)
                            pPage->lOffset = lOffset;
                    }
                    bUpdate = (bOldScrollbar != pPage->bScrollbar) || bUpdate;
                    const tools::Long aSidebarheight = pPage->bScrollbar ? mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height() : 0;
                    /*
                                       TODO
                                       - enlarge all notes till GetNextBorder(), as we resized to average value before
                                       */
                    //let's hide the ones which overlap the page
                    for (auto const& visiblePostIt : aVisiblePostItList)
                    {
                        if (pPage->lOffset != 0)
                            visiblePostIt->TranslateTopPosition(pPage->lOffset);

                        bool bBottom  = mpEditWin->PixelToLogic(Point(0,visiblePostIt->VirtualPos().Y()+visiblePostIt->VirtualSize().Height())).Y() <= (pPage->mPageRect.Bottom()-aSidebarheight);
                        bool bTop = mpEditWin->PixelToLogic(Point(0,visiblePostIt->VirtualPos().Y())).Y() >= (pPage->mPageRect.Top()+aSidebarheight);
                        if ( bBottom && bTop )
                        {
                            // When tiled rendering, make sure that only the
                            // view that has the comment focus emits callbacks,
                            // so the editing view jumps to the comment, but
                            // not the others.
                            bool bTiledPainting = comphelper::LibreOfficeKit::isTiledPainting();
                            if (!bTiledPainting)
                                // No focus -> disable callbacks.
                                comphelper::LibreOfficeKit::setTiledPainting(!visiblePostIt->HasChildPathFocus());
                            visiblePostIt->ShowNote();
                            if (!bTiledPainting)
                                comphelper::LibreOfficeKit::setTiledPainting(bTiledPainting);
                        }
                        else
                        {
                            if (mpEditWin->PixelToLogic(Point(0,visiblePostIt->VirtualPos().Y())).Y() < (pPage->mPageRect.Top()+aSidebarheight))
                            {
                                if ( pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT )
                                    visiblePostIt->ShowAnchorOnly(Point( pPage->mPageRect.Left(),
                                                                pPage->mPageRect.Top()));
                                else if ( pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT )
                                    visiblePostIt->ShowAnchorOnly(Point( pPage->mPageRect.Right(),
                                                                pPage->mPageRect.Top()));
                            }
                            else
                            {
                                if ( pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT )
                                    visiblePostIt->ShowAnchorOnly(Point(pPage->mPageRect.Left(),
                                                               pPage->mPageRect.Bottom()));
                                else if ( pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT )
                                    visiblePostIt->ShowAnchorOnly(Point(pPage->mPageRect.Right(),
                                                               pPage->mPageRect.Bottom()));
                            }
                            OSL_ENSURE(pPage->bScrollbar,"SwPostItMgr::LayoutByPage(): note overlaps, but bScrollbar is not true");
                        }
                    }
                }
                else
                {
                    for (auto const& visiblePostIt : aVisiblePostItList)
                    {
                        visiblePostIt->SetPosAndSize();
                    }

                    bool bOldScrollbar = pPage->bScrollbar;
                    pPage->bScrollbar = false;
                    bUpdate = (bOldScrollbar != pPage->bScrollbar) || bUpdate;
                }

                for (auto const& visiblePostIt : aVisiblePostItList)
                {
                    if (bLoKitActive && !bTiledAnnotations)
                    {
                        if (visiblePostIt->GetSidebarItem().mbPendingLayout && visiblePostIt->GetSidebarItem().mLayoutStatus != SwPostItHelper::DELETED)
                        {
                            // Notify about a just inserted comment.
                            aCreatedPostIts.insert(visiblePostIt);
                        }
                        else if (visiblePostIt->IsAnchorRectChanged())
                        {
                            lcl_CommentNotification(mpView, CommentNotificationType::Modify, &visiblePostIt->GetSidebarItem(), 0);
                            visiblePostIt->ResetAnchorRectChanged();
                        }
                    }

                    // Layout for this post it finished now
                    visiblePostIt->GetSidebarItem().mbPendingLayout = false;
                }
            }
            else
            {
                if (pPage->bScrollbar)
                    bUpdate = true;
                pPage->bScrollbar = false;
            }
        }

        if (!bShowNotes)
        {       // we do not want to see the notes anymore -> Options-Writer-View-Notes
            IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
            bool bRepair = false;
            for (auto const& postItField : mvPostItFields)
            {
                if (!postItField->UseElement(*mpWrtShell->GetLayout(), rIDRA))
                {
                    OSL_FAIL("PostIt is not in doc!");
                    bRepair = true;
                    continue;
                }

                if (postItField->mpPostIt)
                {
                    postItField->mpPostIt->HideNote();
                    if (postItField->mpPostIt->HasChildPathFocus())
                    {
                        SetActiveSidebarWin(nullptr);
                        postItField->mpPostIt->GrabFocusToDocument();
                    }
                }
            }

            if ( bRepair )
                CheckForRemovedPostIts();
        }

        // notes scrollbar is otherwise not drawn correctly for some cases
        // scrollbar area is enough
        if (bUpdate)
            mpEditWin->Invalidate(); /*This is a super expensive relayout and render of the entire page*/

        mbLayouting = false;
    }

    // Now that comments are laid out, notify about freshly laid out or just inserted comments.
    for (const auto& pPostIt : aCreatedPostIts)
    {
        lcl_CommentNotification(mpView, CommentNotificationType::Add, &pPostIt->GetSidebarItem(), 0);
    }

    if (bEnableMapMode)
        mpEditWin->EnableMapMode(false);
}

bool SwPostItMgr::BorderOverPageBorder(tools::ULong aPage) const
{
    if ( mPages[aPage-1]->mvSidebarItems.empty() )
    {
        OSL_FAIL("Notes SidePane painted but no rects and page lists calculated!");
        return false;
    }

    auto aItem = mPages[aPage-1]->mvSidebarItems.end();
    --aItem;
    OSL_ENSURE ((*aItem)->mpPostIt,"BorderOverPageBorder: NULL postIt, should never happen");
    if ((*aItem)->mpPostIt)
    {
        const tools::Long aSidebarheight = mPages[aPage-1]->bScrollbar ? mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height() : 0;
        const tools::Long aEndValue = mpEditWin->PixelToLogic(Point(0,(*aItem)->mpPostIt->GetPosPixel().Y()+(*aItem)->mpPostIt->GetSizePixel().Height())).Y();
        return aEndValue <= mPages[aPage-1]->mPageRect.Bottom()-aSidebarheight;
    }
    else
        return false;
}

void SwPostItMgr::DrawNotesForPage(OutputDevice *pOutDev, sal_uInt32 nPage)
{
    assert(nPage < mPages.size());
    if (nPage >= mPages.size())
        return;
    for (auto const& pItem : mPages[nPage]->mvSidebarItems)
    {
        SwAnnotationWin* pPostIt = pItem->mpPostIt;
        if (!pPostIt)
            continue;
        Point aPoint(mpEditWin->PixelToLogic(pPostIt->GetPosPixel()));
        pPostIt->DrawForPage(pOutDev, aPoint);
    }
}

void SwPostItMgr::PaintTile(OutputDevice& rRenderContext)
{
    for (const std::unique_ptr<SwAnnotationItem>& pItem : mvPostItFields)
    {
        SwAnnotationWin* pPostIt = pItem->mpPostIt;
        if (!pPostIt)
            continue;

        bool bEnableMapMode = !mpEditWin->IsMapModeEnabled();
        mpEditWin->EnableMapMode();
        rRenderContext.Push(vcl::PushFlags::MAPMODE);
        Point aOffset(mpEditWin->PixelToLogic(pPostIt->GetPosPixel()));
        MapMode aMapMode(rRenderContext.GetMapMode());
        aMapMode.SetOrigin(aMapMode.GetOrigin() + aOffset);
        rRenderContext.SetMapMode(aMapMode);
        Size aSize(rRenderContext.PixelToLogic(pPostIt->GetSizePixel()));
        tools::Rectangle aRectangle(Point(0, 0), aSize);

        pPostIt->PaintTile(rRenderContext, aRectangle);

        rRenderContext.Pop();
        if (bEnableMapMode)
            mpEditWin->EnableMapMode(false);
    }
}

void SwPostItMgr::Scroll(const tools::Long lScroll,const tools::ULong aPage)
{
    OSL_ENSURE((lScroll % GetScrollSize() )==0,"SwPostItMgr::Scroll: scrolling by wrong value");
    // do not scroll more than necessary up or down
    if ( ((mPages[aPage-1]->lOffset == 0) && (lScroll>0)) || ( BorderOverPageBorder(aPage) && (lScroll<0)) )
        return;

    const bool bOldUp = ArrowEnabled(KEY_PAGEUP,aPage);
    const bool bOldDown = ArrowEnabled(KEY_PAGEDOWN,aPage);
    const tools::Long aSidebarheight = mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height();
    for (auto const& item : mPages[aPage-1]->mvSidebarItems)
    {
        SwAnnotationWin* pPostIt = item->mpPostIt;
        // if this is an answer, we should take the normal position and not the real, slightly moved position
        pPostIt->SetVirtualPosSize(pPostIt->GetPosPixel(),pPostIt->GetSizePixel());
        pPostIt->TranslateTopPosition(lScroll);

        if (item->mbShow)
        {
            bool bBottom  = mpEditWin->PixelToLogic(Point(0,pPostIt->VirtualPos().Y()+pPostIt->VirtualSize().Height())).Y() <= (mPages[aPage-1]->mPageRect.Bottom()-aSidebarheight);
            bool bTop = mpEditWin->PixelToLogic(Point(0,pPostIt->VirtualPos().Y())).Y() >=   (mPages[aPage-1]->mPageRect.Top()+aSidebarheight);
            if ( bBottom && bTop)
            {
                    pPostIt->ShowNote();
            }
            else
            {
                if ( mpEditWin->PixelToLogic(Point(0,pPostIt->VirtualPos().Y())).Y() < (mPages[aPage-1]->mPageRect.Top()+aSidebarheight))
                {
                    if (mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT)
                        pPostIt->ShowAnchorOnly(Point(mPages[aPage-1]->mPageRect.Left(),mPages[aPage-1]->mPageRect.Top()));
                    else if (mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT)
                        pPostIt->ShowAnchorOnly(Point(mPages[aPage-1]->mPageRect.Right(),mPages[aPage-1]->mPageRect.Top()));
                }
                else
                {
                    if (mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT)
                        pPostIt->ShowAnchorOnly(Point(mPages[aPage-1]->mPageRect.Left(),mPages[aPage-1]->mPageRect.Bottom()));
                    else if (mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT)
                        pPostIt->ShowAnchorOnly(Point(mPages[aPage-1]->mPageRect.Right(),mPages[aPage-1]->mPageRect.Bottom()));
                }
            }
        }
    }
    mPages[aPage-1]->lOffset += lScroll;
    if ( (bOldUp != ArrowEnabled(KEY_PAGEUP,aPage)) ||(bOldDown != ArrowEnabled(KEY_PAGEDOWN,aPage)) )
    {
        mpEditWin->Invalidate(GetBottomScrollRect(aPage));
        mpEditWin->Invalidate(GetTopScrollRect(aPage));
    }
}

void SwPostItMgr::AutoScroll(const SwAnnotationWin* pPostIt,const tools::ULong aPage )
{
    // otherwise all notes are visible
    if (!mPages[aPage-1]->bScrollbar)
        return;

    const tools::Long aSidebarheight = mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height();
    const bool bBottom  = mpEditWin->PixelToLogic(Point(0,pPostIt->GetPosPixel().Y()+pPostIt->GetSizePixel().Height())).Y() <= (mPages[aPage-1]->mPageRect.Bottom()-aSidebarheight);
    const bool bTop = mpEditWin->PixelToLogic(Point(0,pPostIt->GetPosPixel().Y())).Y() >= (mPages[aPage-1]->mPageRect.Top()+aSidebarheight);
    if ( !(bBottom && bTop))
    {
        const tools::Long aDiff = bBottom ? mpEditWin->LogicToPixel(Point(0,mPages[aPage-1]->mPageRect.Top() + aSidebarheight)).Y() - pPostIt->GetPosPixel().Y() :
                                        mpEditWin->LogicToPixel(Point(0,mPages[aPage-1]->mPageRect.Bottom() - aSidebarheight)).Y() - (pPostIt->GetPosPixel().Y()+pPostIt->GetSizePixel().Height());
        // this just adds the missing value to get the next a* GetScrollSize() after aDiff
        // e.g aDiff= 61 POSTIT_SCROLL=50 --> lScroll = 100
        const auto nScrollSize = GetScrollSize();
        assert(nScrollSize);
        const tools::Long lScroll = bBottom ? (aDiff + ( nScrollSize - (aDiff % nScrollSize))) : (aDiff - (nScrollSize + (aDiff % nScrollSize)));
        Scroll(lScroll, aPage);
    }
}

void SwPostItMgr::MakeVisible(const SwAnnotationWin* pPostIt )
{
    tools::Long aPage = -1;
    // we don't know the page yet, let's find it ourselves
    std::vector<SwPostItPageItem*>::size_type n=0;
    for (auto const& page : mPages)
    {
        for (auto const& item : page->mvSidebarItems)
        {
            if (item->mpPostIt==pPostIt)
            {
                aPage = n+1;
                break;
            }
        }
        ++n;
    }
    if (aPage!=-1)
        AutoScroll(pPostIt,aPage);
    tools::Rectangle aNoteRect (Point(pPostIt->GetPosPixel().X(),pPostIt->GetPosPixel().Y()-5),pPostIt->GetSizePixel());
    if (!aNoteRect.IsEmpty())
        mpWrtShell->MakeVisible(SwRect(mpEditWin->PixelToLogic(aNoteRect)));
}

bool SwPostItMgr::ArrowEnabled(sal_uInt16 aDirection,tools::ULong aPage) const
{
    switch (aDirection)
    {
        case KEY_PAGEUP:
            {
                return (mPages[aPage-1]->lOffset != 0);
            }
        case KEY_PAGEDOWN:
            {
                return (!BorderOverPageBorder(aPage));
            }
        default: return false;
    }
}

Color SwPostItMgr::GetArrowColor(sal_uInt16 aDirection,tools::ULong aPage) const
{
    if (ArrowEnabled(aDirection,aPage))
    {
        if (Application::GetSettings().GetStyleSettings().GetHighContrastMode())
            return COL_WHITE;
        else
            return COL_NOTES_SIDEPANE_ARROW_ENABLED;
    }
    else
    {
        return COL_NOTES_SIDEPANE_ARROW_DISABLED;
    }
}

bool SwPostItMgr::LayoutByPage(std::vector<SwAnnotationWin*> &aVisiblePostItList, const tools::Rectangle& rBorder, tools::Long lNeededHeight)
{
    /*** General layout idea:***/
    //  - if we have space left, we always move the current one up,
    //    otherwise the next one down
    //  - first all notes are resized
    //  - then the real layout starts

    //rBorder is the page rect
    const tools::Rectangle aBorder         = mpEditWin->LogicToPixel(rBorder);
    tools::Long            lTopBorder      = aBorder.Top() + 5;
    tools::Long            lBottomBorder   = aBorder.Bottom() - 5;
    const tools::Long      lVisibleHeight  = lBottomBorder - lTopBorder; //aBorder.GetHeight() ;
    const size_t    nPostItListSize = aVisiblePostItList.size();
    tools::Long            lTranslatePos   = 0;
    bool            bScrollbars     = false;

    // do all necessary resizings
    if (nPostItListSize > 0 && lVisibleHeight < lNeededHeight)
    {
        // ok, now we have to really resize and adding scrollbars
        const tools::Long lAverageHeight = (lVisibleHeight - nPostItListSize*GetSpaceBetween()) / nPostItListSize;
        if (lAverageHeight<GetMinimumSizeWithMeta())
        {
            bScrollbars = true;
            lTopBorder += GetSidebarScrollerHeight() + 10;
            lBottomBorder -= (GetSidebarScrollerHeight() + 10);
            for (auto const& visiblePostIt : aVisiblePostItList)
                visiblePostIt->SetSize(Size(visiblePostIt->VirtualSize().getWidth(),visiblePostIt->GetMinimumSizeWithMeta()));
        }
        else
        {
            for (auto const& visiblePostIt : aVisiblePostItList)
            {
                if ( visiblePostIt->VirtualSize().getHeight() > lAverageHeight)
                    visiblePostIt->SetSize(Size(visiblePostIt->VirtualSize().getWidth(),lAverageHeight));
            }
        }
    }

    //start the real layout so nothing overlaps anymore
    if (aVisiblePostItList.size()>1)
    {
        int loop = 0;
        bool bDone = false;
        // if no window is moved anymore we are finished
        while (!bDone)
        {
            loop++;
            bDone = true;
            tools::Long lSpaceUsed = lTopBorder + GetSpaceBetween();
            for(auto i = aVisiblePostItList.begin(); i != aVisiblePostItList.end() ; ++i)
            {
                auto aNextPostIt = i;
                ++aNextPostIt;

                if (aNextPostIt != aVisiblePostItList.end())
                {
                    lTranslatePos = ( (*i)->VirtualPos().Y() + (*i)->VirtualSize().Height()) - (*aNextPostIt)->VirtualPos().Y();
                    if (lTranslatePos > 0) // note windows overlaps the next one
                    {
                        // we are not done yet, loop at least once more
                        bDone = false;
                        // if there is space left, move the current note up
                        // it could also happen that there is no space left for the first note due to a scrollbar
                        // then we also jump into, so we move the current one up and the next one down
                        if ( (lSpaceUsed <= (*i)->VirtualPos().Y()) || (i==aVisiblePostItList.begin()))
                        {
                            // we have space left, so let's move the current one up
                            if ( ((*i)->VirtualPos().Y()- lTranslatePos - GetSpaceBetween()) > lTopBorder)
                            {
                                if ((*aNextPostIt)->IsFollow())
                                    (*i)->TranslateTopPosition(-1*(lTranslatePos+ANCHORLINE_WIDTH));
                                else
                                    (*i)->TranslateTopPosition(-1*(lTranslatePos+GetSpaceBetween()));
                            }
                            else
                            {
                                tools::Long lMoveUp = (*i)->VirtualPos().Y() - lTopBorder;
                                (*i)->TranslateTopPosition(-1* lMoveUp);
                                if ((*aNextPostIt)->IsFollow())
                                    (*aNextPostIt)->TranslateTopPosition( (lTranslatePos+ANCHORLINE_WIDTH) - lMoveUp);
                                else
                                    (*aNextPostIt)->TranslateTopPosition( (lTranslatePos+GetSpaceBetween()) - lMoveUp);
                            }
                        }
                        else
                        {
                            // no space left, left move the next one down
                            if ((*aNextPostIt)->IsFollow())
                                (*aNextPostIt)->TranslateTopPosition(lTranslatePos+ANCHORLINE_WIDTH);
                            else
                                (*aNextPostIt)->TranslateTopPosition(lTranslatePos+GetSpaceBetween());
                        }
                    }
                    else
                    {
                        // the first one could overlap the topborder instead of a second note
                        if (i==aVisiblePostItList.begin())
                        {
                            tools::Long lMoveDown = lTopBorder - (*i)->VirtualPos().Y();
                            if (lMoveDown>0)
                            {
                                bDone = false;
                                (*i)->TranslateTopPosition( lMoveDown);
                            }
                        }
                    }
                    if ( (*aNextPostIt)->IsFollow() )
                        lSpaceUsed += (*i)->VirtualSize().Height() + ANCHORLINE_WIDTH;
                    else
                        lSpaceUsed += (*i)->VirtualSize().Height() + GetSpaceBetween();
                }
                else
                {
                    //(*i) is the last visible item
                    auto aPrevPostIt = i;
                    --aPrevPostIt;
                    lTranslatePos = ( (*aPrevPostIt)->VirtualPos().Y() + (*aPrevPostIt)->VirtualSize().Height() ) - (*i)->VirtualPos().Y();
                    if (lTranslatePos > 0)
                    {
                        bDone = false;
                        if ( ((*i)->VirtualPos().Y()+ (*i)->VirtualSize().Height()+lTranslatePos) < lBottomBorder)
                        {
                            if ( (*i)->IsFollow() )
                                (*i)->TranslateTopPosition(lTranslatePos+ANCHORLINE_WIDTH);
                            else
                                (*i)->TranslateTopPosition(lTranslatePos+GetSpaceBetween());
                        }
                        else
                        {
                            (*i)->TranslateTopPosition(lBottomBorder - ((*i)->VirtualPos().Y()+ (*i)->VirtualSize().Height()) );
                        }
                    }
                    else
                    {
                        // note does not overlap, but we might be over the lower border
                        // only do this if there are no scrollbars, otherwise notes are supposed to overlap the border
                        if (!bScrollbars && ((*i)->VirtualPos().Y()+ (*i)->VirtualSize().Height() > lBottomBorder) )
                        {
                            bDone = false;
                            (*i)->TranslateTopPosition(lBottomBorder - ((*i)->VirtualPos().Y()+ (*i)->VirtualSize().Height()));
                        }
                    }
                }
            }
            // security check so we don't loop forever
            if (loop>MAX_LOOP_COUNT)
            {
                OSL_FAIL("PostItMgr::Layout(): We are looping forever");
                break;
            }
        }
    }
    else
    {
        // only one left, make sure it is not hidden at the top or bottom
        auto i = aVisiblePostItList.begin();
        lTranslatePos = lTopBorder - (*i)->VirtualPos().Y();
        if (lTranslatePos>0)
        {
            (*i)->TranslateTopPosition(lTranslatePos+GetSpaceBetween());
        }
        lTranslatePos = lBottomBorder - ((*i)->VirtualPos().Y()+ (*i)->VirtualSize().Height());
        if (lTranslatePos<0)
        {
            (*i)->TranslateTopPosition(lTranslatePos);
        }
    }
    return bScrollbars;
 }

std::vector<SwFormatField*> SwPostItMgr::UpdatePostItsParentInfo()
{
    IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
    SwFieldType* pType = mpView->GetDocShell()->GetDoc()->getIDocumentFieldsAccess().GetFieldType(SwFieldIds::Postit, OUString(),false);
    std::vector<SwFormatField*> vFormatFields;
    pType->CollectPostIts(vFormatFields, rIDRA, mpWrtShell->GetLayout()->IsHideRedlines());

    for (std::vector<SwFormatField*>::iterator i = vFormatFields.begin(); i != vFormatFields.end(); i++)
    {
        SwPostItField *pChildPostIt = static_cast<SwPostItField*>((*i)->GetField());

        if (pChildPostIt->GetParentId() != 0 || !pChildPostIt->GetParentName().isEmpty())
        {
            for (std::vector<SwFormatField*>::iterator j = vFormatFields.begin(); j != vFormatFields.end(); j++)
            {
                SwPostItField *pParentPostIt = static_cast<SwPostItField*>((*j)->GetField());
                if (pChildPostIt->GetParentId() != 0 && pParentPostIt->GetParaId() == pChildPostIt->GetParentId())
                {
                    pChildPostIt->SetParentPostItId(pParentPostIt->GetPostItId());
                    pChildPostIt->SetParentName(pParentPostIt->GetName());
                }
                else if (!pParentPostIt->GetName().isEmpty() && pParentPostIt->GetName() == pChildPostIt->GetParentName())
                {
                    pChildPostIt->SetParentPostItId(pParentPostIt->GetPostItId());
                    pChildPostIt->SetParentName(pParentPostIt->GetName());
                }
            }
        }
    }
    return vFormatFields;
}


void SwPostItMgr::AddPostIts(const bool bCheckExistence, const bool bFocus)
{
    const bool bEmpty = mvPostItFields.empty();
    std::vector<SwFormatField*> vFormatFields = UpdatePostItsParentInfo();

    for(auto pFormatField : vFormatFields)
        InsertItem(pFormatField, bCheckExistence, bFocus);
    // if we just added the first one we have to update the view for centering
    if (bEmpty && !mvPostItFields.empty())
        PrepareView(true);
}

void SwPostItMgr::RemoveSidebarWin()
{
    for (auto& postItField : mvPostItFields)
    {
        EndListening( *const_cast<SfxBroadcaster*>(postItField->GetBroadcaster()) );
        postItField->mpPostIt.disposeAndClear();
        postItField.reset();
    }
    mvPostItFields.clear();

    // all postits removed, no items should be left in pages
    PreparePageContainer();
}

static bool ConfirmDeleteAll(const SwView& pView, const OUString& sText)
{
    const bool bAsk = officecfg::Office::Common::Misc::QueryDeleteAllComments::get();
    bool bConfirm = true;
    if (bAsk)
    {
        VclAbstractDialogFactory* pFact = VclAbstractDialogFactory::Create();
        auto pDlg
            = pFact->CreateQueryDialog(pView.GetFrameWeld(),
                                       SwResId(STR_QUERY_DELALLCOMMENTS_TITLE), sText, "", true);
        sal_Int32 nResult = pDlg->Execute();
        if (pDlg->ShowAgain() == false)
        {
            std::shared_ptr<comphelper::ConfigurationChanges> xChanges(
                comphelper::ConfigurationChanges::create());
            officecfg::Office::Common::Misc::QueryDeleteAllComments::set(false, xChanges);
            xChanges->commit();
        }
        bConfirm = (nResult == RET_YES);
        pDlg->disposeOnce();
    }
    return bConfirm;
}

// copy to new vector, otherwise RemoveItem would operate and delete stuff on mvPostItFields as well
// RemoveItem will clean up the core field and visible postit if necessary
// we cannot just delete everything as before, as postits could move into change tracking
void SwPostItMgr::Delete(const OUString& rAuthor)
{
    OUString sQuestion = SwResId(STR_QUERY_DELALLCOMMENTSAUTHOR_QUESTION);
    sQuestion = sQuestion.replaceAll("%AUTHOR", rAuthor);
    if (!ConfirmDeleteAll(mpWrtShell->GetView(), sQuestion))
        return;

    // tdf#136540 - prevent scrolling to cursor during deletion of annotations
    const bool bUnLockView = !mpWrtShell->IsViewLocked();
    mpWrtShell->LockView(true);

    mpWrtShell->StartAllAction();
    if (HasActiveSidebarWin() && (GetActiveSidebarWin()->GetAuthor() == rAuthor))
    {
        SetActiveSidebarWin(nullptr);
    }
    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_DELETE_AUTHOR_NOTES) + rAuthor);
    mpWrtShell->StartUndo( SwUndoId::DELETE, &aRewriter );

    IsPostitFieldWithAuthorOf aFilter(rAuthor);
    IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
    IsFieldNotDeleted aFilter2(rIDRA, aFilter);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter2);
    while (const SwFormatField* pField = aStack.pop())
    {
        if (mpWrtShell->GotoField(*pField))
            mpWrtShell->DelRight();
    }
    mpWrtShell->EndUndo();
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();

    // tdf#136540 - prevent scrolling to cursor during deletion of annotations
    if (bUnLockView)
        mpWrtShell->LockView(false);
}

void SwPostItMgr::Delete(sal_uInt32 nPostItId)
{
    mpWrtShell->StartAllAction();
    if (HasActiveSidebarWin() &&
        mpActivePostIt->GetPostItField()->GetPostItId() == nPostItId)
    {
        SetActiveSidebarWin(nullptr);
    }
    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_CONTENT_TYPE_SINGLE_POSTIT));
    mpWrtShell->StartUndo( SwUndoId::DELETE, &aRewriter );

    IsPostitFieldWithPostitId aFilter(nPostItId);
    IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
    IsFieldNotDeleted aFilter2(rIDRA, aFilter);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter2);
    const SwFormatField* pField = aStack.pop();
    if (pField && mpWrtShell->GotoField(*pField))
        mpWrtShell->DelRight();
    mpWrtShell->EndUndo();
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::DeleteCommentThread(sal_uInt32 nPostItId)
{
    mpWrtShell->StartAllAction();

    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_CONTENT_TYPE_SINGLE_POSTIT));

    // We have no undo ID at the moment.

    IsPostitFieldWithPostitId aFilter(nPostItId);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter);
    const SwFormatField* pField = aStack.pop();
    // pField now contains our AnnotationWin object
    if (pField) {
        SwAnnotationWin* pWin = GetSidebarWin(pField);
        pWin->DeleteThread();
    }
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::ToggleResolved(sal_uInt32 nPostItId)
{
    mpWrtShell->StartAllAction();

    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_CONTENT_TYPE_SINGLE_POSTIT));

    // We have no undo ID at the moment.

    IsPostitFieldWithPostitId aFilter(nPostItId);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter);
    const SwFormatField* pField = aStack.pop();
    // pField now contains our AnnotationWin object
    if (pField) {
        SwAnnotationWin* pWin = GetSidebarWin(pField);
        pWin->ToggleResolved();
    }

    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::ToggleResolvedForThread(sal_uInt32 nPostItId)
{
    mpWrtShell->StartAllAction();

    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_CONTENT_TYPE_SINGLE_POSTIT));

    // We have no undo ID at the moment.

    IsPostitFieldWithPostitId aFilter(nPostItId);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter);
    const SwFormatField* pField = aStack.pop();
    // pField now contains our AnnotationWin object
    if (pField) {
        SwAnnotationWin* pWin = GetSidebarWin(pField);
        pWin->ToggleResolvedForThread();
    }

    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}


void SwPostItMgr::Delete()
{
    if (!ConfirmDeleteAll(mpWrtShell->GetView(), SwResId(STR_QUERY_DELALLCOMMENTS_QUESTION)))
        return;

    mpWrtShell->StartAllAction();
    SetActiveSidebarWin(nullptr);
    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_DELETE_ALL_NOTES) );
    mpWrtShell->StartUndo( SwUndoId::DELETE, &aRewriter );

    IsPostitField aFilter;
    IDocumentRedlineAccess const& rIDRA(mpWrtShell->getIDocumentRedlineAccess());
    IsFieldNotDeleted aFilter2(rIDRA, aFilter);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(),
        aFilter2);
    while (const SwFormatField* pField = aStack.pop())
    {
        if (mpWrtShell->GotoField(*pField))
            mpWrtShell->DelRight();
    }

    mpWrtShell->EndUndo();
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::PromoteToRoot(sal_uInt32 nPostItId)
{
    mpWrtShell->StartAllAction();

    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_CONTENT_TYPE_SINGLE_POSTIT));

    // We have no undo ID at the moment.

    IsPostitFieldWithPostitId aFilter(nPostItId);
    FieldDocWatchingStack aStack(mvPostItFields, *mpView->GetDocShell(), aFilter);
    const SwFormatField* pField = aStack.pop();
    // pField now contains our AnnotationWin object
    if (pField)
    {
        SwAnnotationWin* pWin = GetSidebarWin(pField);
        pWin->SetAsRoot();
    }
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::MoveSubthreadToRoot(const sw::annotation::SwAnnotationWin* pNewRoot)
{
    std::vector<std::unique_ptr<SwAnnotationItem>>::iterator first, middle, last;
    first = std::find_if(mvPostItFields.begin(), mvPostItFields.end(),
                         [&pNewRoot](const std::unique_ptr<SwAnnotationItem>& pField) {
                             return pField->mpPostIt == pNewRoot;
                         });
    if (first == mvPostItFields.end())
        return;
    std::set<int> aPostItIds;
    aPostItIds.insert(pNewRoot->GetPostItField()->GetPostItId());
    middle = first + 1;
    while (middle != mvPostItFields.end()
           && aPostItIds.contains((*middle)->mpPostIt->GetPostItField()->GetParentPostItId()))
    {
        aPostItIds.insert((*middle)->mpPostIt->GetPostItField()->GetPostItId());
        ++middle;
    }
    if (middle == mvPostItFields.end())
        return;
    last = middle;
    while (last != mvPostItFields.end()
           && (*last)->mpPostIt->GetPostItField()->GetParentPostItId() != 0)
        ++last;
    if (last == middle)
        return;
    std::rotate(first, middle, last);
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::ExecuteFormatAllDialog(SwView& rView)
{
    if (mvPostItFields.empty())
        return;
    sw::annotation::SwAnnotationWin *pOrigActiveWin = GetActiveSidebarWin();
    sw::annotation::SwAnnotationWin *pWin = pOrigActiveWin;
    if (!pWin)
    {
        for (auto const& postItField : mvPostItFields)
        {
            pWin = postItField->mpPostIt;
            if (pWin)
                break;
        }
    }
    if (!pWin)
        return;
    SetActiveSidebarWin(pWin);
    OutlinerView* pOLV = pWin->GetOutlinerView();
    SfxItemSet aEditAttr(pOLV->GetAttribs());
    SfxItemPool* pPool(SwAnnotationShell::GetAnnotationPool(rView));
    auto xDlgAttr = std::make_shared<SfxItemSetFixed<XATTR_FILLSTYLE, XATTR_FILLCOLOR, EE_ITEMS_START, EE_ITEMS_END>>(*pPool);
    xDlgAttr->Put(aEditAttr);
    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    VclPtr<SfxAbstractTabDialog> pDlg(pFact->CreateSwCharDlg(rView.GetFrameWeld(), rView, *xDlgAttr, SwCharDlgMode::Ann));
    pDlg->StartExecuteAsync(
        [this, pDlg, xDlgAttr=std::move(xDlgAttr), pOrigActiveWin] (sal_Int32 nResult)->void
        {
            if (nResult == RET_OK)
            {
                auto aNewAttr = *xDlgAttr;
                aNewAttr.Put(*pDlg->GetOutputItemSet());
                FormatAll(aNewAttr);
            }
            pDlg->disposeOnce();
            SetActiveSidebarWin(pOrigActiveWin);
        }
    );
}

void SwPostItMgr::FormatAll(const SfxItemSet &rNewAttr)
{
    mpWrtShell->StartAllAction();
    SwRewriter aRewriter;
    aRewriter.AddRule(UndoArg1, SwResId(STR_FORMAT_ALL_NOTES) );
    mpWrtShell->StartUndo( SwUndoId::INSATTR, &aRewriter );

    for (auto const& postItField : mvPostItFields)
    {
        if (!postItField->mpPostIt)
            continue;
        OutlinerView* pOLV = postItField->mpPostIt->GetOutlinerView();
        //save old selection
        ESelection aOrigSel(pOLV->GetSelection());
        //select all
        Outliner *pOutliner = pOLV->GetOutliner();
        if (pOutliner)
        {
            sal_Int32 nParaCount = pOutliner->GetParagraphCount();
            if (nParaCount > 0)
                pOLV->SelectRange(0, nParaCount);
        }
        //set new char properties
        pOLV->SetAttribs(rNewAttr);
        //restore old selection
        pOLV->SetSelection(aOrigSel);
        // tdf#91596 store updated formatting in SwField
        postItField->mpPostIt->UpdateData();
    }

    mpWrtShell->EndUndo();
    PrepareView();
    mpWrtShell->EndAllAction();
    mbLayout = true;
    CalcRects();
    LayoutPostIts();
}

void SwPostItMgr::Hide( std::u16string_view rAuthor )
{
    for (auto const& postItField : mvPostItFields)
    {
        if ( postItField->mpPostIt && (postItField->mpPostIt->GetAuthor() == rAuthor) )
        {
            postItField->mbShow  = false;
            postItField->mpPostIt->HideNote();
        }
    }

    LayoutPostIts();
}

void SwPostItMgr::Hide()
{
    for (auto const& postItField : mvPostItFields)
    {
        postItField->mbShow = false;
        if (postItField->mpPostIt)
            postItField->mpPostIt->HideNote();
    }
}

SwAnnotationWin* SwPostItMgr::GetSidebarWin( const SfxBroadcaster* pBroadcaster) const
{
    for (auto const& postItField : mvPostItFields)
    {
        if ( postItField->GetBroadcaster() == pBroadcaster)
            return postItField->mpPostIt;
    }
    return nullptr;
}

sw::annotation::SwAnnotationWin* SwPostItMgr::GetAnnotationWin(const SwPostItField* pField) const
{
    for (auto const& postItField : mvPostItFields)
    {
        if ( postItField->GetFormatField().GetField() == pField )
            return postItField->mpPostIt.get();
    }
    return nullptr;
}

sw::annotation::SwAnnotationWin* SwPostItMgr::GetAnnotationWin(const sal_uInt32 nPostItId) const
{
    for (auto const& postItField : mvPostItFields)
    {
        if ( static_cast<const SwPostItField*>(postItField->GetFormatField().GetField())->GetPostItId() == nPostItId )
            return postItField->mpPostIt.get();
    }
    return nullptr;
}

SwPostItField* SwPostItMgr::GetLatestPostItField()
{
    return static_cast<SwPostItField*>(mvPostItFields.back()->GetFormatField().GetField());
}

sw::annotation::SwAnnotationWin* SwPostItMgr::GetOrCreateAnnotationWindowForLatestPostItField()
{
    return GetOrCreateAnnotationWindow(*mvPostItFields.back(), o3tl::temporary(bool()));
}

SwAnnotationWin* SwPostItMgr::GetNextPostIt( sal_uInt16 aDirection,
                                          SwAnnotationWin* aPostIt )
{
    if (mvPostItFields.size()>1)
    {
        auto i = std::find_if(mvPostItFields.begin(), mvPostItFields.end(),
            [&aPostIt](const std::unique_ptr<SwAnnotationItem>& pField) { return pField->mpPostIt == aPostIt; });
        if (i == mvPostItFields.end())
            return nullptr;

        auto iNextPostIt = i;
        if (aDirection == KEY_PAGEUP)
        {
            if ( iNextPostIt == mvPostItFields.begin() )
            {
                return nullptr;
            }
            --iNextPostIt;
        }
        else
        {
            ++iNextPostIt;
            if ( iNextPostIt == mvPostItFields.end() )
            {
                return nullptr;
            }
        }
        // let's quit, we are back at the beginning
        if ( (*iNextPostIt)->mpPostIt == aPostIt)
            return nullptr;
        return (*iNextPostIt)->mpPostIt;
    }
    else
        return nullptr;
}

tools::Long SwPostItMgr::GetNextBorder()
{
    for (auto const& pPage : mPages)
    {
        for(auto b = pPage->mvSidebarItems.begin(); b!= pPage->mvSidebarItems.end(); ++b)
        {
            if ((*b)->mpPostIt == mpActivePostIt)
            {
                auto aNext = b;
                ++aNext;
                bool bFollow = (aNext != pPage->mvSidebarItems.end()) && (*aNext)->mpPostIt->IsFollow();
                if ( pPage->bScrollbar || bFollow )
                {
                    return -1;
                }
                else
                {
                    //if this is the last item, return the bottom border otherwise the next item
                    if (aNext == pPage->mvSidebarItems.end())
                        return mpEditWin->LogicToPixel(Point(0,pPage->mPageRect.Bottom())).Y() - GetSpaceBetween();
                    else
                        return (*aNext)->mpPostIt->GetPosPixel().Y() - GetSpaceBetween();
                }
            }
        }
    }

    OSL_FAIL("SwPostItMgr::GetNextBorder(): We have to find a next border here");
    return -1;
}

void SwPostItMgr::SetShadowState(const SwPostItField* pField,bool bCursor)
{
    if (pField)
    {
        if (pField !=mShadowState.mpShadowField)
        {
            if (mShadowState.mpShadowField)
            {
                // reset old one if still alive
                // TODO: does not work properly if mouse and cursor was set
                sw::annotation::SwAnnotationWin* pOldPostIt =
                                    GetAnnotationWin(mShadowState.mpShadowField);
                if (pOldPostIt && pOldPostIt->Shadow() && (pOldPostIt->Shadow()->GetShadowState() != SS_EDIT))
                    pOldPostIt->SetViewState(ViewState::NORMAL);
            }
            //set new one, if it is not currently edited
            sw::annotation::SwAnnotationWin* pNewPostIt = GetAnnotationWin(pField);
            if (pNewPostIt && pNewPostIt->Shadow() && (pNewPostIt->Shadow()->GetShadowState() != SS_EDIT))
            {
                pNewPostIt->SetViewState(ViewState::VIEW);
                //remember our new field
                mShadowState.mpShadowField = pField;
                mShadowState.bCursor = false;
                mShadowState.bMouse = false;
            }
        }
        if (bCursor)
            mShadowState.bCursor = true;
        else
            mShadowState.bMouse = true;
    }
    else
    {
        if (mShadowState.mpShadowField)
        {
            if (bCursor)
                mShadowState.bCursor = false;
            else
                mShadowState.bMouse = false;
            if (!mShadowState.bCursor && !mShadowState.bMouse)
            {
                // reset old one if still alive
                sw::annotation::SwAnnotationWin* pOldPostIt = GetAnnotationWin(mShadowState.mpShadowField);
                if (pOldPostIt && pOldPostIt->Shadow() && (pOldPostIt->Shadow()->GetShadowState() != SS_EDIT))
                {
                    pOldPostIt->SetViewState(ViewState::NORMAL);
                    mShadowState.mpShadowField = nullptr;
                }
            }
        }
    }
}

void SwPostItMgr::PrepareView(bool bIgnoreCount)
{
    if (!HasNotes() || bIgnoreCount)
    {
        mpWrtShell->StartAllAction();
        SwRootFrame* pLayout = mpWrtShell->GetLayout();
        if ( pLayout )
            SwPostItHelper::setSidebarChanged( pLayout,
                mpWrtShell->getIDocumentSettingAccess().get( DocumentSettingId::BROWSE_MODE ) );
        mpWrtShell->EndAllAction();
    }
}

bool SwPostItMgr::ShowScrollbar(const tools::ULong aPage) const
{
    if (mPages.size() > aPage-1)
        return (mPages[aPage-1]->bScrollbar && !mbWaitingForCalcRects);
    else
        return false;
}

bool SwPostItMgr::IsHit(const Point& aPointPixel)
{
    if (!HasNotes() || !ShowNotes())
        return false;

    const Point aPoint = mpEditWin->PixelToLogic(aPointPixel);
    tools::Rectangle aRect(GetSidebarRect(aPoint));
    if (!aRect.Contains(aPoint))
        return false;

    // we hit the note's sidebar
    // let's now test for the arrow area
    SwRect aPageFrame;
    const tools::ULong nPageNum
        = SwPostItHelper::getPageInfo(aPageFrame, mpWrtShell->GetLayout(), aPoint);
    if (!nPageNum)
        return false;
    if (mPages[nPageNum - 1]->bScrollbar)
        return ScrollbarHit(nPageNum, aPoint);
    return false;
}

vcl::Window* SwPostItMgr::IsHitSidebarWindow(const Point& rPointLogic)
{
    vcl::Window* pRet = nullptr;

    if (HasNotes() && ShowNotes())
    {
        bool bEnableMapMode = !mpEditWin->IsMapModeEnabled();
        if (bEnableMapMode)
            mpEditWin->EnableMapMode();

        for (const std::unique_ptr<SwAnnotationItem>& pItem : mvPostItFields)
        {
            SwAnnotationWin* pPostIt = pItem->mpPostIt;
            if (!pPostIt)
                continue;

            if (pPostIt->IsHitWindow(rPointLogic))
            {
                pRet = pPostIt;
                break;
            }
        }

        if (bEnableMapMode)
            mpEditWin->EnableMapMode(false);
    }

    return pRet;
}

tools::Rectangle SwPostItMgr::GetSidebarRect(const Point& rPointLogic)
{
    const SwRootFrame* pLayout = mpWrtShell->GetLayout();
    SwRect aPageFrame;
    const tools::ULong nPageNum = SwPostItHelper::getPageInfo(aPageFrame, pLayout, rPointLogic);
    if (!nPageNum)
        return tools::Rectangle();

    return GetSidebarPos(rPointLogic) == sw::sidebarwindows::SidebarPosition::LEFT
               ? tools::Rectangle(
                     Point(aPageFrame.Left() - GetSidebarWidth() - GetSidebarBorderWidth(),
                           aPageFrame.Top()),
                     Size(GetSidebarWidth(), aPageFrame.Height()))
               : tools::Rectangle(
                     Point(aPageFrame.Right() + GetSidebarBorderWidth(), aPageFrame.Top()),
                     Size(GetSidebarWidth(), aPageFrame.Height()));
}

bool SwPostItMgr::IsHitSidebarDragArea(const Point& rPointPx)
{
    if (!HasNotes() || !ShowNotes())
        return false;

    const Point aPointLogic = mpEditWin->PixelToLogic(rPointPx);
    sw::sidebarwindows::SidebarPosition eSidebarPosition = GetSidebarPos(aPointLogic);
    if (eSidebarPosition == sw::sidebarwindows::SidebarPosition::NONE)
        return false;

    tools::Rectangle aDragArea(GetSidebarRect(aPointLogic));
    aDragArea.SetTop(aPointLogic.Y());
    if (eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT)
        aDragArea.SetPos(Point(aDragArea.Right() - 50, aDragArea.Top()));
    else
        aDragArea.SetPos(Point(aDragArea.Left() - 50, aDragArea.Top()));

    Size aS(aDragArea.GetSize());
    aS.setWidth(100);
    aDragArea.SetSize(aS);
    return aDragArea.Contains(aPointLogic);
}

tools::Rectangle SwPostItMgr::GetBottomScrollRect(const tools::ULong aPage) const
{
    SwRect aPageRect = mPages[aPage-1]->mPageRect;
    Point aPointBottom = mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT
                         ? Point(aPageRect.Left() - GetSidebarWidth() - GetSidebarBorderWidth() + mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Bottom()- mpEditWin->PixelToLogic(Size(0,2+GetSidebarScrollerHeight())).Height())
                         : Point(aPageRect.Right() + GetSidebarBorderWidth() + mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Bottom()- mpEditWin->PixelToLogic(Size(0,2+GetSidebarScrollerHeight())).Height());
    Size aSize(GetSidebarWidth() - mpEditWin->PixelToLogic(Size(4,0)).Width(), mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height()) ;
    return tools::Rectangle(aPointBottom,aSize);
}

tools::Rectangle SwPostItMgr::GetTopScrollRect(const tools::ULong aPage) const
{
    SwRect aPageRect = mPages[aPage-1]->mPageRect;
    Point aPointTop = mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT
                      ? Point(aPageRect.Left() - GetSidebarWidth() -GetSidebarBorderWidth()+ mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Top() + mpEditWin->PixelToLogic(Size(0,2)).Height())
                      : Point(aPageRect.Right() + GetSidebarBorderWidth() + mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Top() + mpEditWin->PixelToLogic(Size(0,2)).Height());
    Size aSize(GetSidebarWidth() - mpEditWin->PixelToLogic(Size(4,0)).Width(), mpEditWin->PixelToLogic(Size(0,GetSidebarScrollerHeight())).Height()) ;
    return tools::Rectangle(aPointTop,aSize);
}

//IMPORTANT: if you change the rects here, also change SwPageFrame::PaintNotesSidebar()
bool SwPostItMgr::ScrollbarHit(const tools::ULong aPage,const Point &aPoint)
{
    SwRect aPageRect = mPages[aPage-1]->mPageRect;
    Point aPointBottom = mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT
                         ? Point(aPageRect.Left() - GetSidebarWidth()-GetSidebarBorderWidth() + mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Bottom()- mpEditWin->PixelToLogic(Size(0,2+GetSidebarScrollerHeight())).Height())
                         : Point(aPageRect.Right() + GetSidebarBorderWidth()+ mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Bottom()- mpEditWin->PixelToLogic(Size(0,2+GetSidebarScrollerHeight())).Height());

    Point aPointTop = mPages[aPage-1]->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT
                      ? Point(aPageRect.Left() - GetSidebarWidth()-GetSidebarBorderWidth()+ mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Top() + mpEditWin->PixelToLogic(Size(0,2)).Height())
                      : Point(aPageRect.Right()+GetSidebarBorderWidth()+ mpEditWin->PixelToLogic(Size(2,0)).Width(),aPageRect.Top() + mpEditWin->PixelToLogic(Size(0,2)).Height());

    tools::Rectangle aRectBottom(GetBottomScrollRect(aPage));
    tools::Rectangle aRectTop(GetTopScrollRect(aPage));

    if (aRectBottom.Contains(aPoint))
    {
        if (aPoint.X() < tools::Long((aPointBottom.X() + GetSidebarWidth()/3)))
            Scroll( GetScrollSize(),aPage);
        else
            Scroll( -1*GetScrollSize(), aPage);
        return true;
    }
    else if (aRectTop.Contains(aPoint))
    {
        if (aPoint.X() < tools::Long((aPointTop.X() + GetSidebarWidth()/3*2)))
            Scroll(GetScrollSize(), aPage);
        else
            Scroll(-1*GetScrollSize(), aPage);
        return true;
    }
    return false;
}

void SwPostItMgr::CorrectPositions()
{
    if ( mbWaitingForCalcRects || mbLayouting || mvPostItFields.empty() )
        return;

    // find first valid note
    SwAnnotationWin *pFirstPostIt = nullptr;
    for (auto const& postItField : mvPostItFields)
    {
        pFirstPostIt = postItField->mpPostIt;
        if (pFirstPostIt)
            break;
    }

    //if we have not found a valid note, forget about it and leave
    if (!pFirstPostIt)
        return;

    // yeah, I know,    if this is a left page it could be wrong, but finding the page and the note is probably not even faster than just doing it
    // check, if anchor overlay object exists.
    const tools::Long aAnchorX = pFirstPostIt->Anchor()
                          ? mpEditWin->LogicToPixel( Point(static_cast<tools::Long>(pFirstPostIt->Anchor()->GetSixthPosition().getX()),0)).X()
                          : 0;
    const tools::Long aAnchorY = pFirstPostIt->Anchor()
                          ? mpEditWin->LogicToPixel( Point(0,static_cast<tools::Long>(pFirstPostIt->Anchor()->GetSixthPosition().getY()))).Y() + 1
                          : 0;
    if (Point(aAnchorX,aAnchorY) == pFirstPostIt->GetPosPixel())
        return;

    tools::Long aAnchorPosX = 0;
    tools::Long aAnchorPosY = 0;
    for (const std::unique_ptr<SwPostItPageItem>& pPage : mPages)
    {
        for (auto const& item : pPage->mvSidebarItems)
        {
            // check, if anchor overlay object exists.
            if ( item->mbShow && item->mpPostIt && item->mpPostIt->Anchor() )
            {
                aAnchorPosX = pPage->eSidebarPosition == sw::sidebarwindows::SidebarPosition::LEFT
                    ? mpEditWin->LogicToPixel( Point(static_cast<tools::Long>(item->mpPostIt->Anchor()->GetSeventhPosition().getX()),0)).X()
                    : mpEditWin->LogicToPixel( Point(static_cast<tools::Long>(item->mpPostIt->Anchor()->GetSixthPosition().getX()),0)).X();
                aAnchorPosY = mpEditWin->LogicToPixel( Point(0,static_cast<tools::Long>(item->mpPostIt->Anchor()->GetSixthPosition().getY()))).Y() + 1;
                item->mpPostIt->SetPosPixel(Point(aAnchorPosX,aAnchorPosY));
            }
        }
    }
}

bool SwPostItMgr::ShowNotes() const
{
    // we only want to see notes if Options - Writer - View - Notes is ticked
    return mpWrtShell->GetViewOptions()->IsPostIts();
}

bool SwPostItMgr::HasNotes() const
{
    return !mvPostItFields.empty();
}

void SwPostItMgr::SetSidebarWidth(const Point& rPointLogic)
{
    tools::Rectangle nSidebarRect = GetSidebarRect(rPointLogic);
    if (nSidebarRect.IsEmpty())
        return;

    sw::sidebarwindows::SidebarPosition eSidebarPosition = GetSidebarPos(rPointLogic);
    if (eSidebarPosition == sw::sidebarwindows::SidebarPosition::NONE)
        return;

    // Calculate the width to be applied in logic units
    tools::Long nLogicWidth;
    if (eSidebarPosition == sw::sidebarwindows::SidebarPosition::RIGHT)
        nLogicWidth = rPointLogic.X() - nSidebarRect.Left();
    else
        nLogicWidth = nSidebarRect.Right() - rPointLogic.X();

    // The zoom level is conveniently used as reference to define the minimum width
    const sal_uInt16 nZoom = mpWrtShell->GetViewOptions()->GetZoom();
    double nFactor = static_cast<double>(mpEditWin->LogicToPixel(Point(nLogicWidth, 0)).X())
                     / static_cast<double>(nZoom);
    // The width may vary from 1x to 8x the zoom factor
    nFactor = std::clamp(nFactor, 1.0, 8.0);
    std::shared_ptr<comphelper::ConfigurationChanges> xChanges(
        comphelper::ConfigurationChanges::create());
    officecfg::Office::Writer::Notes::DisplayWidthFactor::set(nFactor, xChanges);
    xChanges->commit();

    // tdf#159146 After resizing the sidebar the layout and the ruler needs to be updated
    mpWrtShell->InvalidateLayout(true);
    mpView->GetHRuler().Invalidate();
    mpView->InvalidateRulerPos();

    LayoutPostIts();
}

tools::ULong SwPostItMgr::GetSidebarWidth(bool bPx) const
{
    bool bEnableMapMode = !mpWrtShell->GetOut()->IsMapModeEnabled();
    sal_uInt16 nZoom = mpWrtShell->GetViewOptions()->GetZoom();
    if (comphelper::LibreOfficeKit::isActive() && !bEnableMapMode)
    {
        // The output device is the tile and contains the real wanted scale factor.
        double fScaleX = double(mpWrtShell->GetOut()->GetMapMode().GetScaleX());
        nZoom = fScaleX * 100;
    }
    tools::ULong aWidth = static_cast<tools::ULong>(
        nZoom * officecfg::Office::Writer::Notes::DisplayWidthFactor::get());

    if (bPx)
        return aWidth;
    else
    {
        if (bEnableMapMode)
            // The output device is the window.
            mpWrtShell->GetOut()->EnableMapMode();
        tools::Long nRet = mpWrtShell->GetOut()->PixelToLogic(Size(aWidth, 0)).Width();
        if (bEnableMapMode)
            mpWrtShell->GetOut()->EnableMapMode(false);
        return nRet;
    }
}

tools::ULong SwPostItMgr::GetSidebarBorderWidth(bool bPx) const
{
    if (bPx)
        return 2;
    else
        return mpWrtShell->GetOut()->PixelToLogic(Size(2,0)).Width();
}

Color SwPostItMgr::GetColorDark(std::size_t aAuthorIndex)
{
    Color aColor = GetColorAnchor(aAuthorIndex);
    svtools::ColorConfig aColorConfig;
    const Color aBgColor(aColorConfig.GetColorValue(svtools::DOCCOLOR).nColor);
    if (aBgColor.IsDark())
        aColor.DecreaseLuminance(80);
    else
        aColor.IncreaseLuminance(150);
    return aColor;
}

Color SwPostItMgr::GetColorLight(std::size_t aAuthorIndex)
{
    Color aColor = GetColorAnchor(aAuthorIndex);
    svtools::ColorConfig aColorConfig;
    const Color aBgColor(aColorConfig.GetColorValue(svtools::DOCCOLOR).nColor);
    if (aBgColor.IsDark())
        aColor.DecreaseLuminance(130);
    else
        aColor.IncreaseLuminance(200);
    return aColor;
}

Color SwPostItMgr::GetColorAnchor(std::size_t aAuthorIndex)
{
    if (!Application::GetSettings().GetStyleSettings().GetHighContrastMode())
    {
        svtools::ColorConfig aColorConfig;
        switch (aAuthorIndex % 9)
        {
            case 0: return aColorConfig.GetColorValue(svtools::AUTHOR1).nColor;
            case 1: return aColorConfig.GetColorValue(svtools::AUTHOR2).nColor;
            case 2: return aColorConfig.GetColorValue(svtools::AUTHOR3).nColor;
            case 3: return aColorConfig.GetColorValue(svtools::AUTHOR4).nColor;
            case 4: return aColorConfig.GetColorValue(svtools::AUTHOR5).nColor;
            case 5: return aColorConfig.GetColorValue(svtools::AUTHOR6).nColor;
            case 6: return aColorConfig.GetColorValue(svtools::AUTHOR7).nColor;
            case 7: return aColorConfig.GetColorValue(svtools::AUTHOR8).nColor;
            case 8: return aColorConfig.GetColorValue(svtools::AUTHOR9).nColor;
        }
    }

    return COL_WHITE;
}

void SwPostItMgr::SetActiveSidebarWin( SwAnnotationWin* p)
{
    if ( p == mpActivePostIt )
        return;

    // we need the temp variable so we can set mpActivePostIt before we call DeactivatePostIt
    // therefore we get a new layout in DOCCHANGED when switching from postit to document,
    // otherwise, GetActivePostIt() would still hold our old postit
    SwAnnotationWin* pActive = mpActivePostIt;
    mpActivePostIt = p;
    if (pActive)
    {
        pActive->DeactivatePostIt();
        mShadowState.mpShadowField = nullptr;
    }
    if (mpActivePostIt)
    {
        mpActivePostIt->GotoPos();
        mpView->AttrChangedNotify(nullptr);
        mpActivePostIt->ActivatePostIt();
    }
}

IMPL_LINK_NOARG( SwPostItMgr, CalcHdl, void*, void )
{
    mnEventId = nullptr;
    if ( mbLayouting )
    {
        OSL_FAIL("Reentrance problem in Layout Manager!");
        mbWaitingForCalcRects = false;
        return;
    }

    // do not change order, even if it would seem so in the first place, we need the calcrects always
    if (CalcRects() || mbLayout)
    {
        mbLayout = false;
        LayoutPostIts();
    }
}

void SwPostItMgr::Rescale()
{
    for (auto const& postItField : mvPostItFields)
        if ( postItField->mpPostIt )
            postItField->mpPostIt->Rescale();
}

sal_Int32 SwPostItMgr::GetInitialAnchorDistance() const
{
    const Fraction& f( mpEditWin->GetMapMode().GetScaleY() );
    return sal_Int32(POSTIT_INITIAL_ANCHOR_DISTANCE * f);
}

sal_Int32 SwPostItMgr::GetSpaceBetween() const
{
    const Fraction& f( mpEditWin->GetMapMode().GetScaleY() );
    return sal_Int32(POSTIT_SPACE_BETWEEN * f);
}

sal_Int32 SwPostItMgr::GetScrollSize() const
{
    const Fraction& f( mpEditWin->GetMapMode().GetScaleY() );
    return sal_Int32((POSTIT_SPACE_BETWEEN + POSTIT_MINIMUMSIZE_WITH_META) * f);
}

sal_Int32 SwPostItMgr::GetMinimumSizeWithMeta() const
{
    const Fraction& f( mpEditWin->GetMapMode().GetScaleY() );
    return sal_Int32(POSTIT_MINIMUMSIZE_WITH_META * f);
}

sal_Int32 SwPostItMgr::GetSidebarScrollerHeight() const
{
    const Fraction& f( mpEditWin->GetMapMode().GetScaleY() );
    return sal_Int32(POSTIT_SCROLL_SIDEBAR_HEIGHT * f);
}

void SwPostItMgr::SetSpellChecking()
{
    for (auto const& postItField : mvPostItFields)
        if ( postItField->mpPostIt )
            postItField->mpPostIt->SetSpellChecking();
}

void SwPostItMgr::SetReadOnlyState()
{
    for (auto const& postItField : mvPostItFields)
        if ( postItField->mpPostIt )
            postItField->mpPostIt->SetReadonly( mbReadOnly );
}

void SwPostItMgr::CheckMetaText()
{
    for (auto const& postItField : mvPostItFields)
        if ( postItField->mpPostIt )
            postItField->mpPostIt->CheckMetaText();
}

void SwPostItMgr::UpdateColors()
{
    for (auto const& postItField : mvPostItFields)
        if ( postItField->mpPostIt )
        {
            postItField->mpPostIt->UpdateColors();
            postItField->mpPostIt->Invalidate();
        }
}

sal_uInt16 SwPostItMgr::Replace(SvxSearchItem const * pItem)
{
    SwAnnotationWin* pWin = GetActiveSidebarWin();
    sal_uInt16 aResult = pWin->GetOutlinerView()->StartSearchAndReplace( *pItem );
    if (!aResult)
        SetActiveSidebarWin(nullptr);
    return aResult;
}

sal_uInt16 SwPostItMgr::FinishSearchReplace(const i18nutil::SearchOptions2& rSearchOptions, bool bSrchForward)
{
    SwAnnotationWin* pWin = GetActiveSidebarWin();
    SvxSearchItem aItem(SID_SEARCH_ITEM );
    aItem.SetSearchOptions(rSearchOptions);
    aItem.SetBackward(!bSrchForward);
    sal_uInt16 aResult = pWin->GetOutlinerView()->StartSearchAndReplace( aItem );
    if (!aResult)
        SetActiveSidebarWin(nullptr);
    return aResult;
}

sal_uInt16 SwPostItMgr::SearchReplace(const SwFormatField &pField, const i18nutil::SearchOptions2& rSearchOptions, bool bSrchForward)
{
    sal_uInt16 aResult = 0;
    SwAnnotationWin* pWin = GetSidebarWin(&pField);
    if (pWin)
    {
        ESelection aOldSelection = pWin->GetOutlinerView()->GetSelection();
        if (bSrchForward)
            pWin->GetOutlinerView()->SetSelection(ESelection(0, 0));
        else
            pWin->GetOutlinerView()->SetSelection(ESelection::AtEnd());
        SvxSearchItem aItem(SID_SEARCH_ITEM );
        aItem.SetSearchOptions(rSearchOptions);
        aItem.SetBackward(!bSrchForward);
        aResult = pWin->GetOutlinerView()->StartSearchAndReplace( aItem );
        if (!aResult)
            pWin->GetOutlinerView()->SetSelection(aOldSelection);
        else
        {
            SetActiveSidebarWin(pWin);
            MakeVisible(pWin);
        }
    }
    return aResult;
}

void SwPostItMgr::AssureStdModeAtShell()
{
    mpWrtShell->AssureStdMode();
}

bool SwPostItMgr::HasActiveSidebarWin() const
{
    return mpActivePostIt != nullptr;
}

bool SwPostItMgr::HasActiveAnnotationWin() const
{
    return HasActiveSidebarWin() &&
           mpActivePostIt != nullptr;
}

void SwPostItMgr::GrabFocusOnActiveSidebarWin()
{
    if ( HasActiveSidebarWin() )
    {
        mpActivePostIt->GrabFocus();
    }
}

void SwPostItMgr::UpdateDataOnActiveSidebarWin()
{
    if ( HasActiveSidebarWin() )
    {
        mpActivePostIt->UpdateData();
    }
}

void SwPostItMgr::DeleteActiveSidebarWin()
{
    if ( HasActiveSidebarWin() )
    {
        mpActivePostIt->Delete();
    }
}

void SwPostItMgr::HideActiveSidebarWin()
{
    if ( HasActiveSidebarWin() )
    {
        mpActivePostIt->Hide();
    }
}

void SwPostItMgr::ToggleInsModeOnActiveSidebarWin()
{
    if ( HasActiveSidebarWin() )
    {
        mpActivePostIt->ToggleInsMode();
    }
}

#if !ENABLE_WASM_STRIP_ACCESSIBILITY
void SwPostItMgr::ConnectSidebarWinToFrame( const SwFrame& rFrame,
                                          const SwFormatField& rFormatField,
                                          SwAnnotationWin& rSidebarWin )
{
    if ( mpFrameSidebarWinContainer == nullptr )
    {
        mpFrameSidebarWinContainer.reset(new SwFrameSidebarWinContainer());
    }

    const bool bInserted = mpFrameSidebarWinContainer->insert( rFrame, rFormatField, rSidebarWin );
    if ( bInserted &&
         mpWrtShell->GetAccessibleMap() )
    {
        mpWrtShell->GetAccessibleMap()->InvalidatePosOrSize( nullptr, nullptr, &rSidebarWin, SwRect() );
    }
}

void SwPostItMgr::DisconnectSidebarWinFromFrame( const SwFrame& rFrame,
                                               SwAnnotationWin& rSidebarWin )
{
    if ( mpFrameSidebarWinContainer != nullptr )
    {
        const bool bRemoved = mpFrameSidebarWinContainer->remove( rFrame, rSidebarWin );
        if ( bRemoved &&
             mpWrtShell->GetAccessibleMap() )
        {
            mpWrtShell->GetAccessibleMap()->A11yDispose( nullptr, nullptr, &rSidebarWin );
        }
    }
}
#endif // ENABLE_WASM_STRIP_ACCESSIBILITY

bool SwPostItMgr::HasFrameConnectedSidebarWins( const SwFrame& rFrame )
{
    bool bRet( false );

    if ( mpFrameSidebarWinContainer != nullptr )
    {
        bRet = !mpFrameSidebarWinContainer->empty( rFrame );
    }

    return bRet;
}

vcl::Window* SwPostItMgr::GetSidebarWinForFrameByIndex( const SwFrame& rFrame,
                                                 const sal_Int32 nIndex )
{
    vcl::Window* pSidebarWin( nullptr );

    if ( mpFrameSidebarWinContainer != nullptr )
    {
        pSidebarWin = mpFrameSidebarWinContainer->get( rFrame, nIndex );
    }

    return pSidebarWin;
}

std::vector<vcl::Window*> SwPostItMgr::GetAllSidebarWinForFrame(const SwFrame& rFrame)
{
    if ( mpFrameSidebarWinContainer != nullptr )
        return mpFrameSidebarWinContainer->getAll(rFrame);

    return {};
}

void SwPostItMgr::ShowHideResolvedNotes(bool visible) {
    for (auto const& pPage : mPages)
    {
        for(auto b = pPage->mvSidebarItems.begin(); b!= pPage->mvSidebarItems.end(); ++b)
        {
            if ((*b)->mpPostIt->IsResolved())
            {
                (*b)->mpPostIt->SetResolved(true);
                (*b)->mpPostIt->GetSidebarItem().mbShow = visible;
            }
        }
    }
    LayoutPostIts();
}

void SwPostItMgr::UpdateResolvedStatus(const sw::annotation::SwAnnotationWin* topNote) {
    // Given the topmost note as an argument, scans over all notes and sets the
    // 'resolved' state of each descendant of the top notes to the resolved state
    // of the top note.
    bool resolved = topNote->IsResolved();
    for (auto const& pPage : mPages)
    {
        for(auto b = pPage->mvSidebarItems.begin(); b!= pPage->mvSidebarItems.end(); ++b)
        {
            if((*b)->mpPostIt->GetTopReplyNote() == topNote) {
               (*b)->mpPostIt->SetResolved(resolved);
            }
        }
    }
}

sw::sidebarwindows::SidebarPosition SwPostItMgr::GetSidebarPos(const Point& rPointLogic)
{
    if (const SwRootFrame* pLayout = mpWrtShell->GetLayout())
    {
        const SwPageFrame* pPageFrame = pLayout->GetPageAtPos(rPointLogic, nullptr, true);
        if (pPageFrame)
            return pPageFrame->SidebarPosition();
    }
    return sw::sidebarwindows::SidebarPosition::NONE;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
