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

#include <scitems.hxx>
#include <sfx2/bindings.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <unotools/charclass.hxx>
#include <osl/diagnose.h>

#include <dbfunc.hxx>
#include <docsh.hxx>
#include <attrib.hxx>
#include <sc.hrc>
#include <undodat.hxx>
#include <dbdata.hxx>
#include <globstr.hrc>
#include <scresid.hxx>
#include <global.hxx>
#include <dbdocfun.hxx>
#include <editable.hxx>
#include <queryentry.hxx>
#include <markdata.hxx>
#include <tabvwsh.hxx>
#include <sortparam.hxx>

ScDBFunc::ScDBFunc( vcl::Window* pParent, ScDocShell& rDocSh, ScTabViewShell* pViewShell ) :
    ScViewFunc( pParent, rDocSh, pViewShell )
{
}

ScDBFunc::~ScDBFunc()
{
}

//      auxiliary functions

void ScDBFunc::GotoDBArea( const OUString& rDBName )
{
    ScDocument& rDoc = GetViewData().GetDocument();
    ScDBCollection* pDBCol = rDoc.GetDBCollection();
    ScDBData* pData = pDBCol->getNamedDBs().findByUpperName(ScGlobal::getCharClass().uppercase(rDBName));
    if (!pData)
        return;

    SCTAB nTab = 0;
    SCCOL nStartCol = 0;
    SCROW nStartRow = 0;
    SCCOL nEndCol = 0;
    SCROW nEndRow = 0;

    pData->GetArea( nTab, nStartCol, nStartRow, nEndCol, nEndRow );
    SetTabNo( nTab );

    MoveCursorAbs( nStartCol, nStartRow, SC_FOLLOW_JUMP,
                   false, false );  // bShift,bControl
    DoneBlockMode();
    InitBlockMode( nStartCol, nStartRow, nTab );
    MarkCursor( nEndCol, nEndRow, nTab );
    SelectionChanged();
}

//  search current datarange for sort / filter

ScDBData* ScDBFunc::GetDBData( bool bMark, ScGetDBMode eMode, ScGetDBSelection eSel )
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    ScDBData* pData = nullptr;
    ScRange aRange;
    ScMarkType eMarkType = GetViewData().GetSimpleArea(aRange);
    if ( eMarkType == SC_MARK_SIMPLE || eMarkType == SC_MARK_SIMPLE_FILTERED )
    {
        bool bShrinkColumnsOnly = false;
        if (eSel == ScGetDBSelection::RowDown)
        {
            // Don't alter row range, additional rows may have been selected on
            // purpose to append data, or to have a fake header row.
            bShrinkColumnsOnly = true;
            // Select further rows only if only one row or a portion thereof is
            // selected.
            if (aRange.aStart.Row() != aRange.aEnd.Row())
            {
                // If an area is selected shrink that to the actual used
                // columns, don't draw filter buttons for empty columns.
                eSel = ScGetDBSelection::ShrinkToUsedData;
            }
            else if (aRange.aStart.Col() == aRange.aEnd.Col())
            {
                // One cell only, if it is not marked obtain entire used data
                // area.
                const ScMarkData& rMarkData = GetViewData().GetMarkData();
                if (!(rMarkData.IsMarked() || rMarkData.IsMultiMarked()))
                    eSel = ScGetDBSelection::Keep;
            }
        }
        switch (eSel)
        {
            case ScGetDBSelection::ShrinkToUsedData:
            case ScGetDBSelection::RowDown:
                {
                    // Shrink the selection to actual used area.
                    ScDocument& rDoc = pDocSh->GetDocument();
                    SCCOL nCol1 = aRange.aStart.Col(), nCol2 = aRange.aEnd.Col();
                    SCROW nRow1 = aRange.aStart.Row(), nRow2 = aRange.aEnd.Row();
                    bool bShrunk;
                    rDoc.ShrinkToUsedDataArea( bShrunk, aRange.aStart.Tab(),
                            nCol1, nRow1, nCol2, nRow2, bShrinkColumnsOnly);
                    if (bShrunk)
                    {
                        aRange.aStart.SetCol(nCol1);
                        aRange.aEnd.SetCol(nCol2);
                        aRange.aStart.SetRow(nRow1);
                        aRange.aEnd.SetRow(nRow2);
                    }
                }
                break;
            default:
                ;   // nothing
        }
        pData = pDocSh->GetDBData( aRange, eMode, eSel );
    }
    else if ( eMode != SC_DB_OLD )
        pData = pDocSh->GetDBData(
                    ScRange( GetViewData().GetCurX(), GetViewData().GetCurY(),
                             GetViewData().GetTabNo() ),
                    eMode, ScGetDBSelection::Keep );

    if (!pData)
        return nullptr;

    if (bMark)
    {
        ScRange aFound;
        pData->GetArea(aFound);
        MarkRange( aFound, false );
    }
    return pData;
}

ScDBData* ScDBFunc::GetAnonymousDBData()
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    ScRange aRange;
    ScMarkType eMarkType = GetViewData().GetSimpleArea(aRange);
    if (eMarkType != SC_MARK_SIMPLE && eMarkType != SC_MARK_SIMPLE_FILTERED)
        return nullptr;

    // Expand to used data area if not explicitly marked.
    const ScMarkData& rMarkData = GetViewData().GetMarkData();
    if (!rMarkData.IsMarked() && !rMarkData.IsMultiMarked())
    {
        SCCOL nCol1 = aRange.aStart.Col();
        SCCOL nCol2 = aRange.aEnd.Col();
        SCROW nRow1 = aRange.aStart.Row();
        SCROW nRow2 = aRange.aEnd.Row();
        pDocSh->GetDocument().GetDataArea(aRange.aStart.Tab(), nCol1, nRow1, nCol2, nRow2, false, false);
        aRange.aStart.SetCol(nCol1);
        aRange.aStart.SetRow(nRow1);
        aRange.aEnd.SetCol(nCol2);
        aRange.aEnd.SetRow(nRow2);
    }

    return pDocSh->GetAnonymousDBData(aRange);
}

//      main functions

// Sort

void ScDBFunc::UISort( const ScSortParam& rSortParam )
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    ScDocument& rDoc = pDocSh->GetDocument();
    SCTAB nTab = GetViewData().GetTabNo();
    ScDBData* pDBData = rDoc.GetDBAtArea( nTab, rSortParam.nCol1, rSortParam.nRow1,
                                                    rSortParam.nCol2, rSortParam.nRow2 );
    if (!pDBData)
    {
        OSL_FAIL( "Sort: no DBData" );
        return;
    }

    ScSubTotalParam aSubTotalParam;
    pDBData->GetSubTotalParam( aSubTotalParam );
    if (aSubTotalParam.aGroups[0].bActive && !aSubTotalParam.bRemoveOnly)
    {
        //  repeat subtotals, with new sortorder

        DoSubTotals( aSubTotalParam, true/*bRecord*/, &rSortParam );
    }
    else
    {
        Sort( rSortParam );        // just sort
    }
}

void ScDBFunc::Sort( const ScSortParam& rSortParam, bool bRecord, bool bPaint )
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    SCTAB nTab = GetViewData().GetTabNo();
    ScDBDocFunc aDBDocFunc( *pDocSh );
    bool bSuccess = aDBDocFunc.Sort( nTab, rSortParam, bRecord, bPaint, false );
    if ( bSuccess && !rSortParam.bInplace )
    {
        //  mark target
        ScRange aDestRange( rSortParam.nDestCol, rSortParam.nDestRow, rSortParam.nDestTab,
                            rSortParam.nDestCol + rSortParam.nCol2 - rSortParam.nCol1,
                            rSortParam.nDestRow + rSortParam.nRow2 - rSortParam.nRow1,
                            rSortParam.nDestTab );
        MarkRange( aDestRange );
    }

    ResetAutoSpellForContentChange();
}

//  filters

void ScDBFunc::Query( const ScQueryParam& rQueryParam, const ScRange* pAdvSource, bool bRecord )
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    SCTAB nTab = GetViewData().GetTabNo();
    ScDBDocFunc aDBDocFunc( *pDocSh );
    bool bSuccess = aDBDocFunc.Query( nTab, rQueryParam, pAdvSource, bRecord, false );

    if (!bSuccess)
        return;

    bool bCopy = !rQueryParam.bInplace;
    if (bCopy)
    {
        //  mark target range (data base range has been set up if applicable)
        ScDocument& rDoc = pDocSh->GetDocument();
        ScDBData* pDestData = rDoc.GetDBAtCursor(
                                        rQueryParam.nDestCol, rQueryParam.nDestRow,
                                        rQueryParam.nDestTab, ScDBDataPortion::TOP_LEFT );
        if (pDestData)
        {
            ScRange aDestRange;
            pDestData->GetArea(aDestRange);
            MarkRange( aDestRange );
        }
    }

    if (!bCopy)
    {
        ScTabViewShell::notifyAllViewsSheetGeomInvalidation(
            GetViewData().GetViewShell(),
            false /* bColumns */, true /* bRows */,
            false /* bSizes*/, true /* bHidden */, true /* bFiltered */,
            false /* bGroups */, nTab);
        UpdateScrollBars(ROW_HEADER);
        SelectionChanged();     // for attribute states (filtered rows are ignored)
    }

    GetViewData().GetBindings().Invalidate( SID_UNFILTER );
}

//  autofilter-buttons show / hide

void ScDBFunc::ToggleAutoFilter()
{
    ScViewData& rViewData = GetViewData();
    ScDocShell* pDocSh = rViewData.GetDocShell();

    ScQueryParam    aParam;
    ScDocument&     rDoc    = rViewData.GetDocument();
    ScDBData*       pDBData = GetDBData(false, SC_DB_AUTOFILTER, ScGetDBSelection::RowDown);

    pDBData->SetByRow( true );              //! undo, retrieve beforehand ??
    pDBData->GetQueryParam( aParam );

    SCCOL  nCol;
    SCROW  nRow = aParam.nRow1;
    SCTAB  nTab = rViewData.GetTabNo();
    ScMF   nFlag;
    bool   bHasAuto = true;
    bool   bHeader  = pDBData->HasHeader();

    //!     instead retrieve from DB-range?

    for (nCol=aParam.nCol1; nCol<=aParam.nCol2 && bHasAuto; nCol++)
    {
        nFlag = rDoc.GetAttr( nCol, nRow, nTab, ATTR_MERGE_FLAG )->GetValue();

        if ( !(nFlag & ScMF::Auto) )
            bHasAuto = false;
    }

    if (bHasAuto)                               // remove
    {
        //  hide filter buttons

        for (nCol=aParam.nCol1; nCol<=aParam.nCol2; nCol++)
        {
            nFlag = rDoc.GetAttr( nCol, nRow, nTab, ATTR_MERGE_FLAG )->GetValue();
            rDoc.ApplyAttr( nCol, nRow, nTab, ScMergeFlagAttr( nFlag & ~ScMF::Auto ) );
            aParam.RemoveAllEntriesByField(nCol);
        }

        // use a list action for the AutoFilter buttons (ScUndoAutoFilter) and the filter operation

        OUString aUndo = ScResId( STR_UNDO_QUERY );
        pDocSh->GetUndoManager()->EnterListAction( aUndo, aUndo, 0, rViewData.GetViewShell()->GetViewShellId() );

        ScRange aRange;
        pDBData->GetArea( aRange );
        pDocSh->GetUndoManager()->AddUndoAction(
            std::make_unique<ScUndoAutoFilter>( pDocSh, aRange, pDBData->GetName(), false ) );

        pDBData->SetAutoFilter(false);

        aParam.bDuplicate = true;
        Query( aParam, nullptr, true );

        pDocSh->GetUndoManager()->LeaveListAction();

        ScDBFunc::ModifiedAutoFilter(pDocSh);
    }
    else                                    // show filter buttons
    {
        if ( !rDoc.IsBlockEmpty( aParam.nCol1, aParam.nRow1,
                                 aParam.nCol2, aParam.nRow2, nTab ) )
        {
            if (!bHeader)
            {
                std::shared_ptr<weld::MessageDialog> xBox(Application::CreateMessageDialog(rViewData.GetDialogParent(),
                                                                                           VclMessageType::Question,
                                                                                           VclButtonsType::YesNo,
                                                                                           // header from first row?
                                                                                           ScResId(STR_MSSG_MAKEAUTOFILTER_0)));
                xBox->set_title(ScResId(STR_MSSG_DOSUBTOTALS_0)); // "StarCalc"
                xBox->set_default_response(RET_YES);
                xBox->SetInstallLOKNotifierHdl(LINK(this, ScDBFunc, InstallLOKNotifierHdl));
                xBox->runAsync(xBox, [pDocSh, &rViewData, pDBData, nRow, nTab, aParam] (sal_Int32 nResult) {
                    if (nResult == RET_YES)
                    {
                        pDBData->SetHeader( true );     //! Undo ??
                    }

                    ApplyAutoFilter(pDocSh, rViewData, pDBData, nRow, nTab, aParam);
                });
            }
            else
                ApplyAutoFilter(pDocSh, rViewData, pDBData, nRow, nTab, aParam);
        }
        else
        {
            std::shared_ptr<weld::MessageDialog> xErrorBox(Application::CreateMessageDialog(rViewData.GetDialogParent(),
                                                           VclMessageType::Warning, VclButtonsType::Ok,
                                                           ScResId(STR_ERR_AUTOFILTER)));
            xErrorBox->SetInstallLOKNotifierHdl(LINK(this, ScDBFunc, InstallLOKNotifierHdl));
            xErrorBox->runAsync(xErrorBox, [] (sal_Int32) {});
        }
    }
}

IMPL_STATIC_LINK_NOARG(ScDBFunc, InstallLOKNotifierHdl, void*, vcl::ILibreOfficeKitNotifier*)
{
    return GetpApp();
}

void ScDBFunc::ApplyAutoFilter(ScDocShell* pDocSh, ScViewData& rViewData, ScDBData* pDBData,
                               SCROW nRow, SCTAB nTab, const ScQueryParam& aParam)
{
    ScDocument& rDoc = rViewData.GetDocument();
    ScRange aRange;
    pDBData->GetArea(aRange);
    pDocSh->GetUndoManager()->AddUndoAction(
        std::make_unique<ScUndoAutoFilter>(pDocSh, aRange, pDBData->GetName(), true));

    pDBData->SetAutoFilter(true);

    for (SCCOL nCol=aParam.nCol1; nCol<=aParam.nCol2; nCol++)
    {
        ScMF nFlag = rDoc.GetAttr(nCol, nRow, nTab, ATTR_MERGE_FLAG)->GetValue();
        rDoc.ApplyAttr(nCol, nRow, nTab, ScMergeFlagAttr(nFlag | ScMF::Auto));
    }
    pDocSh->PostPaint(ScRange(aParam.nCol1, nRow, nTab, aParam.nCol2, nRow, nTab),
                        PaintPartFlags::Grid);

    ScDBFunc::ModifiedAutoFilter(pDocSh);
}

void ScDBFunc::ModifiedAutoFilter(ScDocShell* pDocSh)
{
    ScDocShellModificator aModificator(*pDocSh);
    aModificator.SetDocumentModified();

    if (SfxBindings* pBindings = pDocSh->GetViewBindings())
    {
        pBindings->Invalidate(SID_AUTO_FILTER);
        pBindings->Invalidate(SID_AUTOFILTER_HIDE);
    }
}

//      just hide, no data change

void ScDBFunc::HideAutoFilter()
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    ScDocShellModificator aModificator( *pDocSh );

    ScDocument& rDoc = pDocSh->GetDocument();

    ScDBData* pDBData = GetDBData( false );

    SCTAB nTab;
    SCCOL nCol1, nCol2;
    SCROW nRow1, nRow2;
    pDBData->GetArea(nTab, nCol1, nRow1, nCol2, nRow2);

    for (SCCOL nCol=nCol1; nCol<=nCol2; nCol++)
    {
        ScMF nFlag = rDoc.GetAttr( nCol, nRow1, nTab, ATTR_MERGE_FLAG )->GetValue();
        rDoc.ApplyAttr( nCol, nRow1, nTab, ScMergeFlagAttr( nFlag & ~ScMF::Auto ) );
    }

    ScRange aRange;
    pDBData->GetArea( aRange );
    pDocSh->GetUndoManager()->AddUndoAction(
        std::make_unique<ScUndoAutoFilter>( pDocSh, aRange, pDBData->GetName(), false ) );

    pDBData->SetAutoFilter(false);

    pDocSh->PostPaint(ScRange(nCol1, nRow1, nTab, nCol2, nRow1, nTab), PaintPartFlags::Grid );
    aModificator.SetDocumentModified();

    SfxBindings& rBindings = GetViewData().GetBindings();
    rBindings.Invalidate( SID_AUTO_FILTER );
    rBindings.Invalidate( SID_AUTOFILTER_HIDE );
}

void ScDBFunc::ClearAutoFilter()
{
    ScDocShell* pDocSh = GetViewData().GetDocShell();
    ScDocument& rDoc = pDocSh->GetDocument();

    SCCOL nCol = GetViewData().GetCurX();
    SCROW nRow = GetViewData().GetCurY();
    SCTAB nTab = GetViewData().GetTabNo();

    ScDBData* pDBData = rDoc.GetDBAtCursor(nCol, nRow, nTab, ScDBDataPortion::AREA);
    if (!pDBData)
        return;

    ScQueryParam aParam;
    pDBData->GetQueryParam(aParam);

    aParam.RemoveAllEntriesByField(nCol);
    aParam.eSearchType = utl::SearchParam::SearchType::Normal;
    aParam.bCaseSens = false;
    aParam.bDuplicate = true;
    aParam.bInplace = true;

    Query(aParam, nullptr, true);

    SfxBindings& rBindings = GetViewData().GetBindings();
    rBindings.Invalidate( SID_CLEAR_AUTO_FILTER );
}

//      Re-Import

bool ScDBFunc::ImportData( const ScImportParam& rParam )
{
    ScDocument& rDoc = GetViewData().GetDocument();
    ScEditableTester aTester( rDoc, GetViewData().GetTabNo(), rParam.nCol1,rParam.nRow1,
                                                            rParam.nCol2,rParam.nRow2 );
    if ( !aTester.IsEditable() )
    {
        ErrorMessage(aTester.GetMessageId());
        return false;
    }

    ScDBDocFunc aDBDocFunc( *GetViewData().GetDocShell() );
    return aDBDocFunc.DoImport( GetViewData().GetTabNo(), rParam, nullptr );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
