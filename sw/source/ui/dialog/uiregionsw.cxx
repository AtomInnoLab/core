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

#include <hintids.hxx>
#include <regionsw.hxx>
#include <svl/urihelper.hxx>
#include <svl/PasswordHelper.hxx>
#include <vcl/svapp.hxx>
#include <vcl/weld.hxx>
#include <svl/stritem.hxx>
#include <svl/eitem.hxx>
#include <sfx2/passwd.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/request.hxx>
#include <sfx2/docfile.hxx>
#include <sfx2/linkmgr.hxx>
#include <sfx2/docinsert.hxx>
#include <sfx2/filedlghelper.hxx>
#include <editeng/sizeitem.hxx>
#include <svtools/htmlcfg.hxx>
#include <osl/diagnose.h>
#include <o3tl/string_view.hxx>
#include <comphelper/lok.hxx>

#include <uitool.hxx>
#include <IMark.hxx>
#include <section.hxx>
#include <docary.hxx>
#include <doc.hxx>
#include <wdocsh.hxx>
#include <view.hxx>
#include <wrtsh.hxx>
#include <column.hxx>
#include <fmtclbl.hxx>
#include <fmtfsize.hxx>
#include <frmatr.hxx>
#include <shellio.hxx>

#include <cmdid.h>
#include <strings.hrc>
#include <bitmaps.hlst>
#include <sfx2/bindings.hxx>
#include <sfx2/sfxdlg.hxx>
#include <sfx2/viewfrm.hxx>
#include <svx/dialogs.hrc>
#include <svx/flagsdef.hxx>
#include <memory>
#include <string_view>

using namespace ::com::sun::star;

namespace {

const OUString & BuildBitmap(bool bProtect, bool bHidden)
{
    if (bProtect)
        return bHidden ? RID_BMP_PROT_HIDE : RID_BMP_PROT_NO_HIDE;
    return bHidden ? RID_BMP_HIDE : RID_BMP_NO_HIDE;
}

OUString CollapseWhiteSpaces(std::u16string_view sName)
{
    const sal_Int32 nLen = sName.size();
    const sal_Unicode cRef = ' ';
    OUStringBuffer aBuf(nLen);
    for (sal_Int32 i = 0; i<nLen; )
    {
        const sal_Unicode cCur = sName[i++];
        aBuf.append(cCur);
        if (cCur!=cRef)
            continue;
        while (i<nLen && sName[i]==cRef)
            ++i;
    }
    return aBuf.makeStringAndClear();
}

}

static void   lcl_ReadSections( SfxMedium& rMedium, weld::ComboBox& rBox );

static void lcl_FillList( SwWrtShell& rSh, weld::ComboBox& rSubRegions, weld::ComboBox* pAvailNames, const SwSectionFormat* pNewFormat )
{
    if( !pNewFormat )
    {
        const size_t nCount = rSh.GetSectionFormatCount();
        for (size_t i = 0; i<nCount; i++)
        {
            SectionType eTmpType;
            const SwSectionFormat* pFormat = &rSh.GetSectionFormat(i);
            if( !pFormat->GetParent() &&
                    pFormat->IsInNodesArr() &&
                    (eTmpType = pFormat->GetSection()->GetType()) != SectionType::ToxContent
                    && SectionType::ToxHeader != eTmpType )
            {
                    const UIName sString(pFormat->GetSection()->GetSectionName());
                    if (pAvailNames)
                        pAvailNames->append_text(sString.toString());
                    rSubRegions.append_text(sString.toString());
                    lcl_FillList( rSh, rSubRegions, pAvailNames, pFormat );
            }
        }
    }
    else
    {
        SwSections aTmpArr;
        pNewFormat->GetChildSections(aTmpArr, SectionSort::Pos);
        if( !aTmpArr.empty() )
        {
            SectionType eTmpType;
            for( const auto pSect : aTmpArr )
            {
                const SwSectionFormat* pFormat = pSect->GetFormat();
                if( pFormat->IsInNodesArr()&&
                    (eTmpType = pFormat->GetSection()->GetType()) != SectionType::ToxContent
                    && SectionType::ToxHeader != eTmpType )
                {
                    const UIName sString(pFormat->GetSection()->GetSectionName());
                    if (pAvailNames)
                        pAvailNames->append_text(sString.toString());
                    rSubRegions.append_text(sString.toString());
                    lcl_FillList( rSh, rSubRegions, pAvailNames, pFormat );
                }
            }
        }
    }
}

static void lcl_FillSubRegionList( SwWrtShell& rSh, weld::ComboBox& rSubRegions, weld::ComboBox* pAvailNames )
{
    rSubRegions.clear();
    lcl_FillList( rSh, rSubRegions, pAvailNames, nullptr );
    IDocumentMarkAccess* const pMarkAccess = rSh.getIDocumentMarkAccess();
    for( auto ppMark = pMarkAccess->getBookmarksBegin();
        ppMark != pMarkAccess->getBookmarksEnd();
        ++ppMark)
    {
        const ::sw::mark::MarkBase* pBkmk = *ppMark;
        if( pBkmk->IsExpanded() )
            rSubRegions.append_text( pBkmk->GetName().toString() );
    }
}

// user data class for region information
class SectRepr
{
private:
    SwSectionData           m_SectionData;
    SwFormatCol                m_Col;
    std::unique_ptr<SvxBrushItem>            m_Brush;
    SwFormatFootnoteAtTextEnd        m_FootnoteNtAtEnd;
    SwFormatEndAtTextEnd        m_EndNtAtEnd;
    SwFormatNoBalancedColumns  m_Balance;
    std::shared_ptr<SvxFrameDirectionItem>   m_FrameDirItem;
    std::shared_ptr<SvxLRSpaceItem>          m_LRSpaceItem;
    const size_t            m_nArrPos;
    // shows, if maybe textcontent is in the region
    bool                    m_bContent  : 1;
    // for multiselection, mark at first, then work with TreeListBox!
    bool                    m_bSelected : 1;
    uno::Sequence<sal_Int8> m_TempPasswd;

public:
    SectRepr(size_t nPos, SwSection& rSect);

    SwSectionData &     GetSectionData()        { return m_SectionData; }
    SwFormatCol&               GetCol()            { return m_Col; }
    std::unique_ptr<SvxBrushItem>&   GetBackground()     { return m_Brush; }
    SwFormatFootnoteAtTextEnd&       GetFootnoteNtAtEnd()     { return m_FootnoteNtAtEnd; }
    SwFormatEndAtTextEnd&       GetEndNtAtEnd()     { return m_EndNtAtEnd; }
    SwFormatNoBalancedColumns& GetBalance()        { return m_Balance; }
    std::shared_ptr<SvxFrameDirectionItem>&  GetFrameDir()         { return m_FrameDirItem; }
    std::shared_ptr<SvxLRSpaceItem>&         GetLRSpace()        { return m_LRSpaceItem; }

    size_t              GetArrPos() const { return m_nArrPos; }
    OUString            GetFile() const;
    OUString            GetSubRegion() const;
    void                SetFile(std::u16string_view rFile);
    void                SetFilter(std::u16string_view rFilter);
    void                SetSubRegion(std::u16string_view rSubRegion);

    bool                IsContent() const { return m_bContent; }
    void                SetContent(bool const bValue) { m_bContent = bValue; }

    void                SetSelected() { m_bSelected = true; }
    bool                IsSelected() const { return m_bSelected; }

    uno::Sequence<sal_Int8> & GetTempPasswd() { return m_TempPasswd; }
    void SetTempPasswd(const uno::Sequence<sal_Int8> & rPasswd)
        { m_TempPasswd = rPasswd; }
};

SectRepr::SectRepr( size_t nPos, SwSection& rSect )
    : m_SectionData( rSect )
    , m_Brush(std::make_unique<SvxBrushItem>(RES_BACKGROUND))
    , m_FrameDirItem(std::make_shared<SvxFrameDirectionItem>(SvxFrameDirection::Environment, RES_FRAMEDIR))
    , m_LRSpaceItem(std::make_shared<SvxLRSpaceItem>(RES_LR_SPACE))
    , m_nArrPos(nPos)
    , m_bContent(m_SectionData.GetLinkFileName().isEmpty())
    , m_bSelected(false)
{
    SwSectionFormat *pFormat = rSect.GetFormat();
    if( pFormat )
    {
        m_Col = pFormat->GetCol();
        m_Brush = pFormat->makeBackgroundBrushItem();
        m_FootnoteNtAtEnd = pFormat->GetFootnoteAtTextEnd();
        m_EndNtAtEnd = pFormat->GetEndAtTextEnd();
        m_Balance.SetValue(pFormat->GetBalancedColumns().GetValue());
        m_FrameDirItem.reset(pFormat->GetFrameDir().Clone());
        m_LRSpaceItem.reset(pFormat->GetLRSpace().Clone());
    }
}

void SectRepr::SetFile( std::u16string_view rFile )
{
    OUString sNewFile( INetURLObject::decode( rFile,
                                           INetURLObject::DecodeMechanism::Unambiguous ));
    const OUString sOldFileName( m_SectionData.GetLinkFileName() );
    const std::u16string_view sSub( o3tl::getToken(sOldFileName, 2, sfx2::cTokenSeparator ) );

    if( !rFile.empty() || !sSub.empty() )
    {
        sNewFile += OUStringChar(sfx2::cTokenSeparator);
        if( !rFile.empty() ) // Filter only with FileName
            sNewFile += o3tl::getToken(sOldFileName, 1, sfx2::cTokenSeparator );

        sNewFile += OUStringChar(sfx2::cTokenSeparator) + sSub;
    }

    m_SectionData.SetLinkFileName( sNewFile );

    if( !rFile.empty() || !sSub.empty() )
    {
        m_SectionData.SetType( SectionType::FileLink );
    }
    else
    {
        m_SectionData.SetType( SectionType::Content );
    }
}

void SectRepr::SetFilter( std::u16string_view rFilter )
{
    OUString sNewFile;
    const OUString sOldFileName( m_SectionData.GetLinkFileName() );
    sal_Int32 nIdx{ 0 };
    const std::u16string_view sFile( o3tl::getToken(sOldFileName, 0, sfx2::cTokenSeparator, nIdx ) ); // token 0
    const std::u16string_view sSub( o3tl::getToken(sOldFileName, 1, sfx2::cTokenSeparator, nIdx ) );  // token 2

    if( !sFile.empty() )
        sNewFile = sFile + OUStringChar(sfx2::cTokenSeparator) +
                   rFilter + OUStringChar(sfx2::cTokenSeparator) + sSub;
    else if( !sSub.empty() )
        sNewFile = OUStringChar(sfx2::cTokenSeparator) + OUStringChar(sfx2::cTokenSeparator) + sSub;

    m_SectionData.SetLinkFileName( sNewFile );

    if( !sNewFile.isEmpty() )
    {
        m_SectionData.SetType( SectionType::FileLink );
    }
}

void SectRepr::SetSubRegion(std::u16string_view rSubRegion)
{
    OUString sNewFile;
    sal_Int32 n(0);
    const OUString sLinkFileName(m_SectionData.GetLinkFileName());
    const std::u16string_view sOldFileName( o3tl::getToken(sLinkFileName, 0, sfx2::cTokenSeparator, n ) );
    const std::u16string_view sFilter( o3tl::getToken(sLinkFileName, 0, sfx2::cTokenSeparator, n ) );

    if( !rSubRegion.empty() || !sOldFileName.empty() )
        sNewFile = sOldFileName + OUStringChar(sfx2::cTokenSeparator) +
                   sFilter + OUStringChar(sfx2::cTokenSeparator) + rSubRegion;

    m_SectionData.SetLinkFileName( sNewFile );

    if( !rSubRegion.empty() || !sOldFileName.empty() )
    {
        m_SectionData.SetType( SectionType::FileLink );
    }
    else
    {
        m_SectionData.SetType( SectionType::Content );
    }
}

OUString SectRepr::GetFile() const
{
    const OUString sLinkFile( m_SectionData.GetLinkFileName() );

    if( sLinkFile.isEmpty() )
    {
        return sLinkFile;
    }
    if (SectionType::DdeLink == m_SectionData.GetType())
    {
        sal_Int32 n = 0;
        return sLinkFile.replaceFirst( OUStringChar(sfx2::cTokenSeparator), " ", &n )
                        .replaceFirst( OUStringChar(sfx2::cTokenSeparator), " ", &n );
    }
    return INetURLObject::decode( o3tl::getToken(sLinkFile, 0, sfx2::cTokenSeparator ),
                                  INetURLObject::DecodeMechanism::Unambiguous );
}

OUString SectRepr::GetSubRegion() const
{
    const OUString sLinkFile( m_SectionData.GetLinkFileName() );
    if( !sLinkFile.isEmpty() )
        return sLinkFile.getToken( 2, sfx2::cTokenSeparator );
    return sLinkFile;
}

// dialog edit regions
SwEditRegionDlg::SwEditRegionDlg(weld::Window* pParent, SwWrtShell& rWrtSh)
    : SfxDialogController(pParent, u"modules/swriter/ui/editsectiondialog.ui"_ustr,
                          u"EditSectionDialog"_ustr)
    , m_bSubRegionsFilled(false)
    , m_rSh(rWrtSh)
    , m_bDontCheckPasswd(true)
    , m_xCurName(m_xBuilder->weld_entry(u"curname"_ustr))
    , m_xTree(m_xBuilder->weld_tree_view(u"tree"_ustr))
    , m_xFileCB(m_xBuilder->weld_check_button(u"link"_ustr))
    , m_xDDECB(m_xBuilder->weld_check_button(u"dde"_ustr))
    , m_xDDEFrame(m_xBuilder->weld_widget(u"ddedepend"_ustr))
    , m_xFileNameFT(m_xBuilder->weld_label(u"filenameft"_ustr))
    , m_xDDECommandFT(m_xBuilder->weld_label(u"ddeft"_ustr))
    , m_xFileNameED(m_xBuilder->weld_entry(u"filename"_ustr))
    , m_xFilePB(m_xBuilder->weld_button(u"file"_ustr))
    , m_xSubRegionFT(m_xBuilder->weld_label(u"sectionft"_ustr))
    , m_xSubRegionED(m_xBuilder->weld_combo_box(u"section"_ustr))
    , m_xProtectCB(m_xBuilder->weld_check_button(u"protect"_ustr))
    , m_xPasswdCB(m_xBuilder->weld_check_button(u"withpassword"_ustr))
    , m_xPasswdPB(m_xBuilder->weld_button(u"password"_ustr))
    , m_xHideCB(m_xBuilder->weld_check_button(u"hide"_ustr))
    , m_xConditionFT(m_xBuilder->weld_label(u"conditionft"_ustr))
    , m_xConditionED(new ConditionEdit(m_xBuilder->weld_entry(u"condition"_ustr)))
    , m_xEditInReadonlyCB(m_xBuilder->weld_check_button(u"editinro"_ustr))
    , m_xOK(m_xBuilder->weld_button(u"ok"_ustr))
    , m_xOptionsPB(m_xBuilder->weld_button(u"options"_ustr))
    , m_xDismiss(m_xBuilder->weld_button(u"remove"_ustr))
    , m_xHideFrame(m_xBuilder->weld_widget(u"hideframe"_ustr))
    , m_xLinkFrame(m_xBuilder->weld_frame(u"linkframe"_ustr))
{
    m_xTree->set_size_request(-1, m_xTree->get_height_rows(16));
    m_xFileCB->set_state(TRISTATE_FALSE);
    m_xSubRegionED->make_sorted();
    m_xProtectCB->set_state(TRISTATE_FALSE);
    m_xHideCB->set_state(TRISTATE_FALSE);
    // edit in readonly sections
    m_xEditInReadonlyCB->set_state(TRISTATE_FALSE);

    bool bWeb = dynamic_cast<SwWebDocShell*>( m_rSh.GetView().GetDocShell() ) != nullptr;

    m_xTree->connect_selection_changed(LINK(this, SwEditRegionDlg, GetFirstEntryHdl));
    m_xCurName->connect_changed(LINK(this, SwEditRegionDlg, NameEditHdl));
    m_xConditionED->connect_changed( LINK( this, SwEditRegionDlg, ConditionEditHdl));
    m_xOK->connect_clicked( LINK( this, SwEditRegionDlg, OkHdl));
    m_xPasswdCB->connect_toggled(LINK(this, SwEditRegionDlg, TogglePasswdHdl));
    m_xPasswdPB->connect_clicked(LINK(this, SwEditRegionDlg, ChangePasswdHdl));
    m_xHideCB->connect_toggled(LINK(this, SwEditRegionDlg, ChangeHideHdl));
    // edit in readonly sections
    m_xEditInReadonlyCB->connect_toggled(LINK(this, SwEditRegionDlg, ChangeEditInReadonlyHdl));

    m_xOptionsPB->connect_clicked(LINK(this, SwEditRegionDlg, OptionsHdl));
    m_xProtectCB->connect_toggled(LINK(this, SwEditRegionDlg, ChangeProtectHdl));
    m_xDismiss->connect_clicked( LINK( this, SwEditRegionDlg, ChangeDismissHdl));
    m_xFileCB->connect_toggled(LINK(this, SwEditRegionDlg, UseFileHdl));
    m_xFilePB->connect_clicked(LINK(this, SwEditRegionDlg, FileSearchHdl));
    m_xFileNameED->connect_changed(LINK(this, SwEditRegionDlg, FileNameEntryHdl));
    m_xSubRegionED->connect_changed(LINK(this, SwEditRegionDlg, FileNameComboBoxHdl));
    m_xSubRegionED->connect_popup_toggled(LINK(this, SwEditRegionDlg, SubRegionEventHdl));
    m_xSubRegionED->set_entry_completion(true, true);

    m_xTree->set_selection_mode(SelectionMode::Multiple);

    if (bWeb)
    {
        m_xDDECB->hide();
        m_xHideFrame->hide();
        m_xPasswdCB->hide();
    }

    m_xDDECB->connect_toggled(LINK(this, SwEditRegionDlg, DDEHdl));

    m_pCurrSect = m_rSh.GetCurrSection();
    RecurseList( nullptr, nullptr );

    // if the cursor is not in a region the first one will always be selected
    if (!m_xTree->get_selected(nullptr))
    {
        std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
        if (m_xTree->get_iter_first(*xIter))
        {
            m_xTree->select(*xIter);
            GetFirstEntryHdl(*m_xTree);
        }
    }

    m_xTree->show();
    m_bDontCheckPasswd = false;

    if(comphelper::LibreOfficeKit::isActive())
    {
        m_xLinkFrame->hide();
        m_xDDECB->hide();
        m_xDDECommandFT->hide();
        m_xFileNameFT->hide();
        m_xFileNameED->hide();
        m_xFilePB->hide();
    }
}

bool SwEditRegionDlg::CheckPasswd(weld::Toggleable* pBox)
{
    if (m_bDontCheckPasswd)
        return true;
    bool bRet = true;

    m_xTree->selected_foreach([this, &bRet](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        if (!pRepr->GetTempPasswd().hasElements()
            && pRepr->GetSectionData().GetPassword().hasElements())
        {
            SfxPasswordDialog aPasswdDlg(m_xDialog.get());
            bRet = false;
            if (aPasswdDlg.run())
            {
                const OUString sNewPasswd(aPasswdDlg.GetPassword());
                css::uno::Sequence <sal_Int8 > aNewPasswd;
                SvPasswordHelper::GetHashPassword( aNewPasswd, sNewPasswd );
                if (SvPasswordHelper::CompareHashPassword(
                        pRepr->GetSectionData().GetPassword(), sNewPasswd))
                {
                    pRepr->SetTempPasswd(aNewPasswd);
                    bRet = true;
                }
                else
                {
                    std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(m_xDialog.get(),
                                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                                  SwResId(STR_WRONG_PASSWORD)));
                    xInfoBox->run();
                }
            }
        }
        return false;
    });
    if (!bRet && pBox)
    {
        //reset old button state
        if (pBox->get_state() != TRISTATE_INDET)
            pBox->set_active(!pBox->get_active());
    }

    return bRet;
}

// recursively look for child-sections
void SwEditRegionDlg::RecurseList(const SwSectionFormat* pFormat, const weld::TreeIter* pEntry)
{
    std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
    if (!pFormat)
    {
        const size_t nCount=m_rSh.GetSectionFormatCount();
        for ( size_t n = 0; n < nCount; n++ )
        {
            SectionType eTmpType;
            if( !( pFormat = &m_rSh.GetSectionFormat(n))->GetParent() &&
                pFormat->IsInNodesArr() &&
                (eTmpType = pFormat->GetSection()->GetType()) != SectionType::ToxContent
                && SectionType::ToxHeader != eTmpType )
            {
                SwSection *pSect = pFormat->GetSection();
                SectRepr* pSectRepr = new SectRepr( n, *pSect );

                OUString sText(pSect->GetSectionName().toString());
                OUString sImage(BuildBitmap(pSect->IsProtect(),pSect->IsHidden()));
                OUString sId(weld::toId(pSectRepr));
                m_xTree->insert(nullptr, -1, &sText, &sId, nullptr, nullptr, false, xIter.get());
                m_xTree->set_image(*xIter, sImage);

                RecurseList(pFormat, xIter.get());
                if (m_xTree->iter_has_child(*xIter))
                    m_xTree->expand_row(*xIter);
                if (m_pCurrSect==pSect)
                {
                    m_xTree->select(*xIter);
                    m_xTree->scroll_to_row(*xIter);
                    GetFirstEntryHdl(*m_xTree);
                }
            }
        }
    }
    else
    {
        SwSections aTmpArr;
        pFormat->GetChildSections(aTmpArr, SectionSort::Pos);
        for( const auto pSect : aTmpArr )
        {
            SectionType eTmpType;
            pFormat = pSect->GetFormat();
            if( pFormat->IsInNodesArr() &&
                (eTmpType = pFormat->GetSection()->GetType()) != SectionType::ToxContent
                && SectionType::ToxHeader != eTmpType )
            {
                SectRepr* pSectRepr=new SectRepr(
                                FindArrPos( pSect->GetFormat() ), *pSect );

                OUString sText(pSect->GetSectionName().toString());
                OUString sImage = BuildBitmap(pSect->IsProtect(), pSect->IsHidden());
                OUString sId(weld::toId(pSectRepr));
                m_xTree->insert(pEntry, -1, &sText, &sId, nullptr, nullptr, false, xIter.get());
                m_xTree->set_image(*xIter, sImage);

                RecurseList(pSect->GetFormat(), xIter.get());
                if (m_xTree->iter_has_child(*xIter))
                    m_xTree->expand_row(*xIter);
                if (m_pCurrSect==pSect)
                {
                    m_xTree->select(*xIter);
                    m_xTree->scroll_to_row(*xIter);
                    GetFirstEntryHdl(*m_xTree);
                }
            }
        }
    }
}

size_t SwEditRegionDlg::FindArrPos(const SwSectionFormat* pFormat )
{
    const size_t nCount=m_rSh.GetSectionFormatCount();
    for ( size_t i = 0; i < nCount; i++ )
        if ( pFormat == &m_rSh.GetSectionFormat(i) )
            return i;

    OSL_FAIL("SectionFormat not on the list" );
    return SIZE_MAX;
}

SwEditRegionDlg::~SwEditRegionDlg( )
{
    std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
    if (m_xTree->get_iter_first(*xIter))
    {
        do
        {
            delete weld::fromId<SectRepr*>(m_xTree->get_id(*xIter));
        } while (m_xTree->iter_next(*xIter));
    }
}

void SwEditRegionDlg::SelectSection(std::u16string_view rSectionName)
{
    std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
    if (!m_xTree->get_iter_first(*xIter))
        return;

    do
    {
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(*xIter));
        if (pRepr->GetSectionData().GetSectionName() == rSectionName)
        {
            m_xTree->unselect_all();
            m_xTree->select(*xIter);
            m_xTree->scroll_to_row(*xIter);
            GetFirstEntryHdl(*m_xTree);
            break;
        }
    } while (m_xTree->iter_next(*xIter));
}

// selected entry in TreeListBox is showed in Edit window in case of
// multiselection some controls are disabled
IMPL_LINK(SwEditRegionDlg, GetFirstEntryHdl, weld::TreeView&, rBox, void)
{
    m_bDontCheckPasswd = true;
    std::unique_ptr<weld::TreeIter> xIter(rBox.make_iterator());
    bool bEntry = rBox.get_selected(xIter.get());
    m_xHideCB->set_sensitive(true);
    // edit in readonly sections
    m_xEditInReadonlyCB->set_sensitive(true);

    m_xProtectCB->set_sensitive(true);
    m_xFileCB->set_sensitive(true);
    css::uno::Sequence <sal_Int8> aCurPasswd;
    if (1 < rBox.count_selected_rows())
    {
        m_xHideCB->set_state(TRISTATE_INDET);
        m_xProtectCB->set_state(TRISTATE_INDET);
        // edit in readonly sections
        m_xEditInReadonlyCB->set_state(TRISTATE_INDET);
        m_xFileCB->set_state(TRISTATE_INDET);

        bool bHiddenValid       = true;
        bool bProtectValid      = true;
        bool bConditionValid    = true;
        // edit in readonly sections
        bool bEditInReadonlyValid = true;
        bool bEditInReadonly    = true;

        bool bHidden            = true;
        bool bProtect           = true;
        OUString sCondition;
        bool bFirst             = true;
        bool bFileValid         = true;
        bool bFile              = true;
        bool bPasswdValid       = true;

        m_xTree->selected_foreach([&](weld::TreeIter& rEntry){
            SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
            SwSectionData const& rData( pRepr->GetSectionData() );
            if(bFirst)
            {
                sCondition      = rData.GetCondition();
                bHidden         = rData.IsHidden();
                bProtect        = rData.IsProtectFlag();
                // edit in readonly sections
                bEditInReadonly = rData.IsEditInReadonlyFlag();

                bFile           = (rData.GetType() != SectionType::Content);
                aCurPasswd      = rData.GetPassword();
            }
            else
            {
                if(sCondition != rData.GetCondition())
                    bConditionValid = false;
                bHiddenValid      = (bHidden == rData.IsHidden());
                bProtectValid     = (bProtect == rData.IsProtectFlag());
                // edit in readonly sections
                bEditInReadonlyValid =
                    (bEditInReadonly == rData.IsEditInReadonlyFlag());

                bFileValid        = (bFile ==
                    (rData.GetType() != SectionType::Content));
                bPasswdValid      = (aCurPasswd == rData.GetPassword());
            }
            bFirst = false;
            return false;
        });

        m_xHideCB->set_state(!bHiddenValid ? TRISTATE_INDET :
                    bHidden ? TRISTATE_TRUE : TRISTATE_FALSE);
        m_xProtectCB->set_state(!bProtectValid ? TRISTATE_INDET :
                    bProtect ? TRISTATE_TRUE : TRISTATE_FALSE);
        // edit in readonly sections
        m_xEditInReadonlyCB->set_state(!bEditInReadonlyValid ? TRISTATE_INDET :
                    bEditInReadonly ? TRISTATE_TRUE : TRISTATE_FALSE);

        m_xFileCB->set_state(!bFileValid ? TRISTATE_INDET :
                    bFile ? TRISTATE_TRUE : TRISTATE_FALSE);

        if (bConditionValid)
            m_xConditionED->set_text(sCondition);
        else
        {
            m_xConditionFT->set_sensitive(false);
            m_xConditionED->set_sensitive(false);
        }

        m_xCurName->set_sensitive(false);
        m_xDDECB->set_sensitive(false);
        m_xDDEFrame->set_sensitive(false);
        m_xOptionsPB->set_sensitive(false);
        bool bPasswdEnabled = m_xProtectCB->get_state() == TRISTATE_TRUE;
        m_xPasswdCB->set_sensitive(bPasswdEnabled);
        m_xPasswdPB->set_sensitive(bPasswdEnabled);
        if(!bPasswdValid)
        {
            rBox.get_selected(xIter.get());
            rBox.unselect_all();
            rBox.select(*xIter);
            GetFirstEntryHdl(rBox);
            return;
        }
        else
            m_xPasswdCB->set_active(aCurPasswd.hasElements());
    }
    else if (bEntry )
    {
        m_xCurName->set_sensitive(true);
        m_xOptionsPB->set_sensitive(true);
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(*xIter));
        SwSectionData const& rData( pRepr->GetSectionData() );
        m_xConditionED->set_text(rData.GetCondition());
        m_xHideCB->set_sensitive(true);
        m_xHideCB->set_state((rData.IsHidden()) ? TRISTATE_TRUE : TRISTATE_FALSE);
        bool bHide = TRISTATE_TRUE == m_xHideCB->get_state();
        m_xConditionED->set_sensitive(bHide);
        m_xConditionFT->set_sensitive(bHide);
        m_xPasswdCB->set_active(rData.GetPassword().hasElements());

        m_xOK->set_sensitive(true);
        m_xPasswdCB->set_sensitive(true);
        m_xCurName->set_text(rBox.get_text(*xIter));
        m_xCurName->set_sensitive(true);
        m_xDismiss->set_sensitive(true);
        const OUString aFile = pRepr->GetFile();
        const OUString sSub = pRepr->GetSubRegion();
        m_xSubRegionED->clear();
        m_xSubRegionED->append_text(u""_ustr); // put in a dummy entry, which is replaced when m_bSubRegionsFilled is set
        m_bSubRegionsFilled = false;
        if( !aFile.isEmpty() || !sSub.isEmpty() )
        {
            m_xFileCB->set_active(true);
            m_xFileNameED->set_text(aFile);
            m_xSubRegionED->set_entry_text(sSub);
            m_xDDECB->set_active(rData.GetType() == SectionType::DdeLink);
        }
        else
        {
            m_xFileCB->set_active(false);
            m_xFileNameED->set_text(aFile);
            m_xDDECB->set_sensitive(false);
            m_xDDECB->set_active(false);
        }
        UseFileHdl(*m_xFileCB);
        DDEHdl(*m_xDDECB);
        m_xProtectCB->set_state((rData.IsProtectFlag())
                ? TRISTATE_TRUE : TRISTATE_FALSE);
        m_xProtectCB->set_sensitive(true);

        // edit in readonly sections
        m_xEditInReadonlyCB->set_state((rData.IsEditInReadonlyFlag())
                ? TRISTATE_TRUE : TRISTATE_FALSE);
        m_xEditInReadonlyCB->set_sensitive(true);

        bool bPasswdEnabled = m_xProtectCB->get_active();
        m_xPasswdCB->set_sensitive(bPasswdEnabled);
        m_xPasswdPB->set_sensitive(bPasswdEnabled);
    }
    m_bDontCheckPasswd = false;
}

// in OkHdl the modified settings are being applied and reversed regions are deleted
IMPL_LINK_NOARG(SwEditRegionDlg, OkHdl, weld::Button&, void)
{
    // temp. Array because during changing of a region the position
    // inside of the "Core-Arrays" can be shifted:
    //  - at linked regions, when they have more SubRegions or get
    //    new ones.
    // StartUndo must certainly also happen not before the formats
    // are copied (ClearRedo!)

    const SwSectionFormats& rDocFormats = m_rSh.GetDoc()->GetSections();
    SwSectionFormats aOrigArray(rDocFormats);

    m_rSh.StartAllAction();
    m_rSh.StartUndo();
    m_rSh.ResetSelect( nullptr,false, ScrollSizeMode::ScrollSizeDefault );

    std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
    if (m_xTree->get_iter_first(*xIter))
    {
        do
        {
            SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(*xIter));
            SwSectionFormat* pFormat = aOrigArray[ pRepr->GetArrPos() ];
            if (!pRepr->GetSectionData().IsProtectFlag())
            {
                pRepr->GetSectionData().SetPassword(uno::Sequence<sal_Int8 >());
            }
            size_t nNewPos = rDocFormats.GetPos(pFormat);
            if ( SIZE_MAX != nNewPos )
            {
                SwAttrSet aSet(pFormat->GetAttrSet().CloneAsValue( false ));
                if( pFormat->GetCol() != pRepr->GetCol() )
                    aSet.Put( pRepr->GetCol() );

                std::unique_ptr<SvxBrushItem> aBrush(pFormat->makeBackgroundBrushItem(false));
                if( pRepr->GetBackground() && *aBrush != *pRepr->GetBackground() )
                    aSet.Put( *pRepr->GetBackground() );

                if( pFormat->GetFootnoteAtTextEnd(false) != pRepr->GetFootnoteNtAtEnd() )
                    aSet.Put( pRepr->GetFootnoteNtAtEnd() );

                if( pFormat->GetEndAtTextEnd(false) != pRepr->GetEndNtAtEnd() )
                    aSet.Put( pRepr->GetEndNtAtEnd() );

                if( pFormat->GetBalancedColumns() != pRepr->GetBalance() )
                    aSet.Put( pRepr->GetBalance() );

                if( pFormat->GetFrameDir() != *pRepr->GetFrameDir() )
                    aSet.Put( *pRepr->GetFrameDir() );

                if( pFormat->GetLRSpace() != *pRepr->GetLRSpace())
                    aSet.Put( *pRepr->GetLRSpace());

                m_rSh.UpdateSection( nNewPos, pRepr->GetSectionData(),
                                   aSet.Count() ? &aSet : nullptr );
            }
        } while (m_xTree->iter_next(*xIter));
    }

    for (SectReprs_t::reverse_iterator it = m_SectReprs.rbegin(), aEnd = m_SectReprs.rend(); it != aEnd; ++it)
    {
        assert(it->first == it->second->GetArrPos());
        SwSectionFormat* pFormat = aOrigArray[ it->second->GetArrPos() ];
        const size_t nNewPos = rDocFormats.GetPos( pFormat );
        if( SIZE_MAX != nNewPos )
            m_rSh.DelSectionFormat( nNewPos );
    }

    aOrigArray.clear();

    SwWrtShell& rSh = m_rSh;

    // response must be called ahead of EndAction's end,
    // otherwise ScrollError can occur.
    m_xDialog->response(RET_OK);

    // accessing 'this' after response isn't safe, as the callback might cause the dialog to be disposed
    rSh.EndUndo();
    rSh.EndAllAction();
}

// Toggle protect
IMPL_LINK(SwEditRegionDlg, ChangeProtectHdl, weld::Toggleable&, rButton, void)
{
    if (!CheckPasswd(&rButton))
        return;
    bool bCheck = TRISTATE_TRUE == rButton.get_state();
    m_xTree->selected_foreach([this, bCheck](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        pRepr->GetSectionData().SetProtectFlag(bCheck);
        OUString aImage = BuildBitmap(bCheck, TRISTATE_TRUE == m_xHideCB->get_state());
        m_xTree->set_image(rEntry, aImage);
        return false;
    });
    m_xPasswdCB->set_sensitive(bCheck);
    m_xPasswdPB->set_sensitive(bCheck);
}

// Toggle hide
IMPL_LINK( SwEditRegionDlg, ChangeHideHdl, weld::Toggleable&, rButton, void)
{
    if (!CheckPasswd(&rButton))
        return;
    m_xTree->selected_foreach([this, &rButton](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        pRepr->GetSectionData().SetHidden(TRISTATE_TRUE == rButton.get_state());
        OUString aImage = BuildBitmap(TRISTATE_TRUE == m_xProtectCB->get_state(),
                                      TRISTATE_TRUE == rButton.get_state());
        m_xTree->set_image(rEntry, aImage);
        return false;
    });
    bool bHide = TRISTATE_TRUE == rButton.get_state();
    m_xConditionED->set_sensitive(bHide);
    m_xConditionFT->set_sensitive(bHide);
}

// Toggle edit in readonly
IMPL_LINK(SwEditRegionDlg, ChangeEditInReadonlyHdl, weld::Toggleable&, rButton, void)
{
    if (!CheckPasswd(&rButton))
        return;
    m_xTree->selected_foreach([this, &rButton](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        pRepr->GetSectionData().SetEditInReadonlyFlag(
                TRISTATE_TRUE == rButton.get_state());
        return false;
    });
}

// clear selected region
IMPL_LINK_NOARG(SwEditRegionDlg, ChangeDismissHdl, weld::Button&, void)
{
    if(!CheckPasswd())
        return;
    // at first mark all selected
    m_xTree->selected_foreach([this](weld::TreeIter& rEntry){
        SectRepr* const pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        pSectRepr->SetSelected();
        return false;
    });

    std::unique_ptr<weld::TreeIter> xEntry(m_xTree->make_iterator());
    bool bEntry(m_xTree->get_selected(xEntry.get()));
    // then delete
    while (bEntry)
    {
        SectRepr* const pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_id(*xEntry));
        std::unique_ptr<weld::TreeIter> xRemove;
        bool bRestart = false;
        if (pSectRepr->IsSelected())
        {
            m_SectReprs.insert(std::make_pair(pSectRepr->GetArrPos(),
                        std::unique_ptr<SectRepr>(pSectRepr)));
            if (m_xTree->iter_has_child(*xEntry))
            {
                std::unique_ptr<weld::TreeIter> xChild(m_xTree->make_iterator(xEntry.get()));
                (void)m_xTree->iter_children(*xChild);
                std::unique_ptr<weld::TreeIter> xParent(m_xTree->make_iterator(xEntry.get()));
                if (!m_xTree->iter_parent(*xParent))
                    xParent.reset();
                bool bChild = true;
                do
                {
                    // because of the repositioning we have to start at the beginning again
                    bRestart = true;
                    std::unique_ptr<weld::TreeIter> xMove(m_xTree->make_iterator(xChild.get()));
                    bChild = m_xTree->iter_next_sibling(*xChild);
                    m_xTree->move_subtree(*xMove, xParent.get(), m_xTree->get_iter_index_in_parent(*xEntry));
                } while (bChild);
            }
            xRemove = m_xTree->make_iterator(xEntry.get());
        }
        if (bRestart)
            bEntry = m_xTree->get_iter_first(*xEntry);
        else
            bEntry = m_xTree->iter_next(*xEntry);
        if (xRemove)
            m_xTree->remove(*xRemove);
    }

    if (m_xTree->get_selected(nullptr))
        return;

    m_xConditionFT->set_sensitive(false);
    m_xConditionED->set_sensitive(false);
    m_xDismiss->set_sensitive(false);
    m_xCurName->set_sensitive(false);
    m_xProtectCB->set_sensitive(false);
    m_xPasswdCB->set_sensitive(false);
    m_xHideCB->set_sensitive(false);
    // edit in readonly sections
    m_xEditInReadonlyCB->set_sensitive(false);
    m_xEditInReadonlyCB->set_state(TRISTATE_FALSE);
    m_xProtectCB->set_state(TRISTATE_FALSE);
    m_xPasswdCB->set_active(false);
    m_xHideCB->set_state(TRISTATE_FALSE);
    m_xFileCB->set_active(false);
    // otherwise the focus would be on HelpButton
    m_xOK->grab_focus();
    UseFileHdl(*m_xFileCB);
}

// link CheckBox to file?
IMPL_LINK(SwEditRegionDlg, UseFileHdl, weld::Toggleable&, rButton, void)
{
    if (!CheckPasswd(&rButton))
        return;
    bool bMulti = 1 < m_xTree->count_selected_rows();
    bool bFile = rButton.get_active();
    if (m_xTree->get_selected(nullptr))
    {
        m_xTree->selected_foreach([&](weld::TreeIter& rEntry){
            SectRepr* const pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
            bool bContent = pSectRepr->IsContent();
            if( rButton.get_active() && bContent && m_rSh.HasSelection() )
            {
                std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(m_xDialog.get(),
                                                               VclMessageType::Question, VclButtonsType::YesNo,
                                                               SwResId(STR_QUERY_CONNECT)));
                if (RET_NO == xQueryBox->run())
                    rButton.set_active( false );
            }
            if( bFile )
                pSectRepr->SetContent(false);
            else
            {
                pSectRepr->SetFile(u"");
                pSectRepr->SetSubRegion(std::u16string_view());
                pSectRepr->GetSectionData().SetLinkFilePassword(OUString());
            }
            return false;
        });
        m_xDDECB->set_sensitive(bFile && !bMulti);
        m_xDDEFrame->set_sensitive(bFile && !bMulti);
        if( bFile )
        {
            m_xProtectCB->set_state(TRISTATE_TRUE);
            m_xFileNameED->grab_focus();

        }
        else
        {
            m_xDDECB->set_active(false);
            m_xSubRegionED->set_entry_text(OUString());
        }
        DDEHdl(*m_xDDECB);
    }
    else
    {
        rButton.set_active(false);
        rButton.set_sensitive(false);
        m_xDDECB->set_active(false);
        m_xDDECB->set_sensitive(false);
        m_xDDEFrame->set_sensitive(false);
    }
}

// call dialog paste file
IMPL_LINK_NOARG(SwEditRegionDlg, FileSearchHdl, weld::Button&, void)
{
    if(!CheckPasswd())
        return;
    m_pDocInserter.reset(new ::sfx2::DocumentInserter(m_xDialog.get(), u"swriter"_ustr));
    m_pDocInserter->StartExecuteModal( LINK( this, SwEditRegionDlg, DlgClosedHdl ) );
}

IMPL_LINK_NOARG(SwEditRegionDlg, OptionsHdl, weld::Button&, void)
{
    if(!CheckPasswd())
        return;
    SectRepr* pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_selected_id());
    if (!pSectRepr)
        return;

    SfxItemSetFixed<
            RES_FRM_SIZE, RES_FRM_SIZE,
            RES_LR_SPACE, RES_LR_SPACE,
            RES_BACKGROUND, RES_BACKGROUND,
            RES_COL, RES_COL,
            RES_FTN_AT_TXTEND, RES_FRAMEDIR,
            XATTR_FILL_FIRST, XATTR_FILL_LAST,
            SID_ATTR_PAGE_SIZE, SID_ATTR_PAGE_SIZE>  aSet( m_rSh.GetView().GetPool() );

    aSet.Put( pSectRepr->GetCol() );
    aSet.Put( *pSectRepr->GetBackground() );
    aSet.Put( pSectRepr->GetFootnoteNtAtEnd() );
    aSet.Put( pSectRepr->GetEndNtAtEnd() );
    aSet.Put( pSectRepr->GetBalance() );
    aSet.Put( *pSectRepr->GetFrameDir() );
    aSet.Put( *pSectRepr->GetLRSpace() );

    const SwSectionFormats& rDocFormats = m_rSh.GetDoc()->GetSections();
    SwSectionFormats aOrigArray(rDocFormats);

    SwSectionFormat* pFormat = aOrigArray[pSectRepr->GetArrPos()];
    tools::Long nWidth = m_rSh.GetSectionWidth(*pFormat);
    aOrigArray.clear();
    if (!nWidth)
        nWidth = USHRT_MAX;

    aSet.Put(SwFormatFrameSize(SwFrameSize::Variable, nWidth));
    aSet.Put(SvxSizeItem(SID_ATTR_PAGE_SIZE, Size(nWidth, nWidth)));

    auto pDlg = std::make_shared<SwSectionPropertyTabDialog>(m_xDialog.get(), aSet, m_rSh);
    SfxTabDialogController::runAsync(pDlg, [pDlg, this](sal_Int32 nResult){
        if (nResult == RET_OK) {
            const SfxItemSet* pOutSet = pDlg->GetOutputItemSet();
            if( !(pOutSet && pOutSet->Count()) )
                return;

            const SwFormatCol* pColItem = pOutSet->GetItemIfSet(
                                    RES_COL, false );
            const SvxBrushItem* pBrushItem = pOutSet->GetItemIfSet(
                                    RES_BACKGROUND, false );
            const SwFormatFootnoteAtTextEnd* pFootnoteItem = pOutSet->GetItemIfSet(
                                    RES_FTN_AT_TXTEND, false );
            const SwFormatEndAtTextEnd* pEndItem = pOutSet->GetItemIfSet(
                                    RES_END_AT_TXTEND, false );
            const SwFormatNoBalancedColumns* pBalanceItem = pOutSet->GetItemIfSet(
                                    RES_COLUMNBALANCE, false );
            const SvxFrameDirectionItem* pFrameDirItem = pOutSet->GetItemIfSet(
                                    RES_FRAMEDIR, false );
            const SvxLRSpaceItem* pLRSpaceItem = pOutSet->GetItemIfSet(
                                    RES_LR_SPACE, false );

            if( !(pColItem ||
                  pBrushItem ||
                  pFootnoteItem ||
                  pEndItem ||
                  pBalanceItem ||
                  pFrameDirItem ||
                  pLRSpaceItem) )
                return;

            m_xTree->selected_foreach([&](weld::TreeIter& rEntry)
            {
                SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
                if (pColItem)
                    pRepr->GetCol() = *pColItem;
                if (pBrushItem)
                    pRepr->GetBackground().reset(pBrushItem->Clone());
                if (pFootnoteItem)
                    pRepr->GetFootnoteNtAtEnd() = *pFootnoteItem;
                if (pEndItem)
                    pRepr->GetEndNtAtEnd() = *pEndItem;
                if (pBalanceItem)
                    pRepr->GetBalance().SetValue(pBalanceItem->GetValue());
                if (pFrameDirItem)
                    pRepr->GetFrameDir()->SetValue(pFrameDirItem->GetValue());
                if (pLRSpaceItem)
                    pRepr->GetLRSpace().reset(pLRSpaceItem->Clone());
                return false;
            });
        }
    });

}

IMPL_LINK(SwEditRegionDlg, FileNameComboBoxHdl, weld::ComboBox&, rEdit, void)
{
    int nStartPos, nEndPos;
    rEdit.get_entry_selection_bounds(nStartPos, nEndPos);
    if (!CheckPasswd())
        return;
    rEdit.select_entry_region(nStartPos, nEndPos);
    SectRepr* pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_selected_id());
    pSectRepr->SetSubRegion( rEdit.get_active_text() );
}

// Applying of the filename or the linked region
IMPL_LINK(SwEditRegionDlg, FileNameEntryHdl, weld::Entry&, rEdit, void)
{
    int nStartPos, nEndPos;
    rEdit.get_selection_bounds(nStartPos, nEndPos);
    if (!CheckPasswd())
        return;
    rEdit.select_region(nStartPos, nEndPos);
    SectRepr* pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_selected_id());
    m_xSubRegionED->clear();
    m_xSubRegionED->append_text(u""_ustr); // put in a dummy entry, which is replaced when m_bSubRegionsFilled is set
    m_bSubRegionsFilled = false;
    if (m_xDDECB->get_active())
    {
        OUString sLink( CollapseWhiteSpaces(rEdit.get_text()) );
        sal_Int32 nPos = 0;
        sLink = sLink.replaceFirst( " ", OUStringChar(sfx2::cTokenSeparator), &nPos );
        if (nPos>=0)
        {
            sLink = sLink.replaceFirst( " ", OUStringChar(sfx2::cTokenSeparator), &nPos );
        }

        pSectRepr->GetSectionData().SetLinkFileName( sLink );
        pSectRepr->GetSectionData().SetType( SectionType::DdeLink );
    }
    else
    {
        OUString sTmp(rEdit.get_text());
        if(!sTmp.isEmpty())
        {
            SfxMedium* pMedium = m_rSh.GetView().GetDocShell()->GetMedium();
            INetURLObject aAbs;
            if( pMedium )
                aAbs = pMedium->GetURLObject();
            sTmp = URIHelper::SmartRel2Abs(
                aAbs, sTmp, URIHelper::GetMaybeFileHdl() );
        }
        pSectRepr->SetFile( sTmp );
        pSectRepr->GetSectionData().SetLinkFilePassword(OUString());
    }
}

IMPL_LINK(SwEditRegionDlg, DDEHdl, weld::Toggleable&, rButton, void)
{
    if (!CheckPasswd(&rButton))
        return;
    SectRepr* pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_selected_id());
    if (!pSectRepr)
        return;

    bool bFile = m_xFileCB->get_active();
    SwSectionData & rData( pSectRepr->GetSectionData() );
    bool bDDE = rButton.get_active();
    if(bDDE)
    {
        m_xFileNameFT->hide();
        m_xDDECommandFT->set_sensitive(true);
        m_xDDECommandFT->show();
        m_xSubRegionFT->hide();
        m_xSubRegionED->hide();
        if (SectionType::FileLink == rData.GetType())
        {
            pSectRepr->SetFile(u"");
            m_xFileNameED->set_text(OUString());
            rData.SetLinkFilePassword(OUString());
        }
        rData.SetType(SectionType::DdeLink);
    }
    else
    {
        m_xDDECommandFT->hide();
        m_xFileNameFT->set_sensitive(bFile);
        if(!comphelper::LibreOfficeKit::isActive())
            m_xFileNameFT->show();
        m_xSubRegionED->show();
        m_xSubRegionFT->show();
        m_xSubRegionED->set_sensitive(bFile);
        m_xSubRegionFT->set_sensitive(bFile);
        m_xSubRegionED->set_sensitive(bFile);
        if (SectionType::DdeLink == rData.GetType())
        {
            rData.SetType(SectionType::FileLink);
            pSectRepr->SetFile(u"");
            rData.SetLinkFilePassword(OUString());
            m_xFileNameED->set_text(OUString());
        }
    }
    m_xFilePB->set_sensitive(bFile && !bDDE);
}

void SwEditRegionDlg::ChangePasswd(bool bChange)
{
    if (!CheckPasswd())
    {
        if (!bChange)
            m_xPasswdCB->set_active(!m_xPasswdCB->get_active());
        return;
    }

    bool bSet = bChange ? bChange : m_xPasswdCB->get_active();

    m_xTree->selected_foreach([this, bChange, bSet](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        if(bSet)
        {
            if(!pRepr->GetTempPasswd().hasElements() || bChange)
            {
                SfxPasswordDialog aPasswdDlg(m_xDialog.get());
                aPasswdDlg.ShowExtras(SfxShowExtras::CONFIRM);
                if (RET_OK == aPasswdDlg.run())
                {
                    const OUString sNewPasswd(aPasswdDlg.GetPassword());
                    if (aPasswdDlg.GetConfirm() == sNewPasswd)
                    {
                        SvPasswordHelper::GetHashPassword( pRepr->GetTempPasswd(), sNewPasswd );
                    }
                    else
                    {
                        std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(m_xDialog.get(),
                                                                      VclMessageType::Info, VclButtonsType::Ok,
                                                                      SwResId(STR_WRONG_PASSWD_REPEAT)));
                        xInfoBox->run();
                        ChangePasswd(bChange);
                        return true;
                    }
                }
                else
                {
                    if(!bChange)
                        m_xPasswdCB->set_active(false);
                    return true;
                }
            }
            pRepr->GetSectionData().SetPassword(pRepr->GetTempPasswd());
        }
        else
        {
            pRepr->GetSectionData().SetPassword(uno::Sequence<sal_Int8 >());
        }
        return false;
    });
}

IMPL_LINK_NOARG(SwEditRegionDlg, TogglePasswdHdl, weld::Toggleable&, void)
{
    ChangePasswd(false);
}

IMPL_LINK_NOARG(SwEditRegionDlg, ChangePasswdHdl, weld::Button&, void)
{
    ChangePasswd(true);
}

// the current region name is being added to the TreeListBox immediately during
// editing, with empty string no Ok()
IMPL_LINK_NOARG(SwEditRegionDlg, NameEditHdl, weld::Entry&, void)
{
    if(!CheckPasswd())
        return;
    std::unique_ptr<weld::TreeIter> xIter(m_xTree->make_iterator());
    if (m_xTree->get_selected(xIter.get()))
    {
        const OUString aName = m_xCurName->get_text();
        m_xTree->set_text(*xIter, aName);
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(*xIter));
        pRepr->GetSectionData().SetSectionName(UIName(aName));

        m_xOK->set_sensitive(!aName.isEmpty());
    }
}

IMPL_LINK( SwEditRegionDlg, ConditionEditHdl, weld::Entry&, rEdit, void )
{
    int nStartPos, nEndPos;
    rEdit.get_selection_bounds(nStartPos, nEndPos);
    if(!CheckPasswd())
        return;
    rEdit.select_region(nStartPos, nEndPos);

    m_xTree->selected_foreach([this, &rEdit](weld::TreeIter& rEntry){
        SectRepr* pRepr = weld::fromId<SectRepr*>(m_xTree->get_id(rEntry));
        pRepr->GetSectionData().SetCondition(rEdit.get_text());
        return false;
    });
}

IMPL_LINK( SwEditRegionDlg, DlgClosedHdl, sfx2::FileDialogHelper *, _pFileDlg, void )
{
    OUString sFileName, sFilterName, sPassword;
    if ( _pFileDlg->GetError() == ERRCODE_NONE )
    {
        std::unique_ptr<SfxMedium> pMedium(m_pDocInserter->CreateMedium("sglobal"));
        if ( pMedium )
        {
            sFileName = pMedium->GetURLObject().GetMainURL( INetURLObject::DecodeMechanism::NONE );
            sFilterName = pMedium->GetFilter()->GetFilterName();
            if ( const SfxStringItem* pItem = pMedium->GetItemSet().GetItemIfSet( SID_PASSWORD, false ) )
                sPassword = pItem->GetValue();
            ::lcl_ReadSections(*pMedium, *m_xSubRegionED);
        }
    }

    SectRepr* pSectRepr = weld::fromId<SectRepr*>(m_xTree->get_selected_id());
    if (pSectRepr)
    {
        pSectRepr->SetFile( sFileName );
        pSectRepr->SetFilter( sFilterName );
        pSectRepr->GetSectionData().SetLinkFilePassword(sPassword);
        m_xFileNameED->set_text(pSectRepr->GetFile());
    }
}

IMPL_LINK_NOARG(SwEditRegionDlg, SubRegionEventHdl, weld::ComboBox&, void)
{
    if (m_bSubRegionsFilled)
        return;

    //if necessary fill the names bookmarks/sections/tables now

    OUString sFileName = m_xFileNameED->get_text();
    if(!sFileName.isEmpty())
    {
        SfxMedium* pMedium = m_rSh.GetView().GetDocShell()->GetMedium();
        INetURLObject aAbs;
        if( pMedium )
            aAbs = pMedium->GetURLObject();
        sFileName = URIHelper::SmartRel2Abs(
                aAbs, sFileName, URIHelper::GetMaybeFileHdl() );

        //load file and set the shell
        SfxMedium aMedium( sFileName, StreamMode::STD_READ );
        sFileName = aMedium.GetURLObject().GetMainURL( INetURLObject::DecodeMechanism::NONE );
        ::lcl_ReadSections(aMedium, *m_xSubRegionED);
    }
    else
        lcl_FillSubRegionList(m_rSh, *m_xSubRegionED, nullptr);
    m_bSubRegionsFilled = true;
}

// helper function - read section names from medium
static void lcl_ReadSections( SfxMedium& rMedium, weld::ComboBox& rBox )
{
    rBox.clear();
    uno::Reference < embed::XStorage > xStg;
    if( !(rMedium.IsStorage() && (xStg = rMedium.GetStorage()).is()) )
        return;

    std::vector<OUString> aArr;
    SotClipboardFormatId nFormat = SotStorage::GetFormatID( xStg );
    if ( nFormat == SotClipboardFormatId::STARWRITER_60 || nFormat == SotClipboardFormatId::STARWRITERGLOB_60 ||
        nFormat == SotClipboardFormatId::STARWRITER_8 || nFormat == SotClipboardFormatId::STARWRITERGLOB_8)
        SwGetReaderXML()->GetSectionList( rMedium, aArr );

    for (auto const& it : aArr)
    {
        rBox.append_text(it);
    }
}

SwInsertSectionTabDialog::SwInsertSectionTabDialog(
            weld::Window* pParent, const SfxItemSet& rSet, SwWrtShell& rSh)
    : SfxTabDialogController(pParent, u"modules/swriter/ui/insertsectiondialog.ui"_ustr,
                             u"InsertSectionDialog"_ustr,&rSet)
    , m_rWrtSh(rSh)
{
    SfxAbstractDialogFactory* pFact = SfxAbstractDialogFactory::Create();
    AddTabPage(u"section"_ustr, SwInsertSectionTabPage::Create, nullptr);
    AddTabPage(u"columns"_ustr,   SwColumnPage::Create, nullptr);
    AddTabPage(u"background"_ustr, pFact->GetTabPageCreatorFunc(RID_SVXPAGE_BKG), nullptr);
    AddTabPage(u"notes"_ustr, SwSectionFootnoteEndTabPage::Create, nullptr);
    AddTabPage(u"indents"_ustr, SwSectionIndentTabPage::Create, nullptr);

    tools::Long nHtmlMode = SvxHtmlOptions::GetExportMode();

    bool bWeb = dynamic_cast<SwWebDocShell*>( rSh.GetView().GetDocShell()  ) != nullptr ;
    if(bWeb)
    {
        RemoveTabPage(u"notes"_ustr);
        RemoveTabPage(u"indents"_ustr);
        if( HTML_CFG_NS40 != nHtmlMode && HTML_CFG_WRITER != nHtmlMode)
            RemoveTabPage(u"columns"_ustr);
    }
    SetCurPageId(u"section"_ustr);
}

SwInsertSectionTabDialog::~SwInsertSectionTabDialog()
{
}

void SwInsertSectionTabDialog::PageCreated(const OUString& rId, SfxTabPage &rPage)
{
    if (rId == "section")
        static_cast<SwInsertSectionTabPage&>(rPage).SetWrtShell(m_rWrtSh);
    else if (rId == "background")
    {
        SfxAllItemSet aSet(*(GetInputSetImpl()->GetPool()));
        aSet.Put (SfxUInt32Item(SID_FLAG_TYPE, static_cast<sal_uInt32>(SvxBackgroundTabFlags::SHOW_SELECTOR)));
        rPage.PageCreated(aSet);
    }
    else if (rId == "columns")
    {
        const SwFormatFrameSize& rSize = GetInputSetImpl()->Get(RES_FRM_SIZE);
        static_cast<SwColumnPage&>(rPage).SetPageWidth(rSize.GetWidth());
        static_cast<SwColumnPage&>(rPage).ShowBalance(true);
        static_cast<SwColumnPage&>(rPage).SetInSection(true);
    }
    else if (rId == "indents")
        static_cast<SwSectionIndentTabPage&>(rPage).SetWrtShell(m_rWrtSh);
}

void SwInsertSectionTabDialog::SetSectionData(SwSectionData const& rSect)
{
    m_pSectionData.reset( new SwSectionData(rSect) );
}

short SwInsertSectionTabDialog::Ok()
{
    short nRet = SfxTabDialogController::Ok();
    OSL_ENSURE(m_pSectionData, "SwInsertSectionTabDialog: no SectionData?");
    const SfxItemSet* pOutputItemSet = GetOutputItemSet();
    m_rWrtSh.InsertSection(*m_pSectionData, pOutputItemSet);
    SfxViewFrame& rViewFrame = m_rWrtSh.GetView().GetViewFrame();
    uno::Reference< frame::XDispatchRecorder > xRecorder =
            rViewFrame.GetBindings().GetRecorder();
    if ( xRecorder.is() )
    {
        SfxRequest aRequest(rViewFrame, FN_INSERT_REGION);
        if(const SwFormatCol* pCol = pOutputItemSet->GetItemIfSet(RES_COL, false))
        {
            aRequest.AppendItem(SfxUInt16Item(SID_ATTR_COLUMNS,
                pCol->GetColumns().size()));
        }
        aRequest.AppendItem(SfxStringItem( FN_PARAM_REGION_NAME,
                    m_pSectionData->GetSectionName().toString()));
        aRequest.AppendItem(SfxStringItem( FN_PARAM_REGION_CONDITION,
                    m_pSectionData->GetCondition()));
        aRequest.AppendItem(SfxBoolItem( FN_PARAM_REGION_HIDDEN,
                    m_pSectionData->IsHidden()));
        aRequest.AppendItem(SfxBoolItem( FN_PARAM_REGION_PROTECT,
                    m_pSectionData->IsProtectFlag()));
        // edit in readonly sections
        aRequest.AppendItem(SfxBoolItem( FN_PARAM_REGION_EDIT_IN_READONLY,
                    m_pSectionData->IsEditInReadonlyFlag()));

        const OUString sLinkFileName( m_pSectionData->GetLinkFileName() );
        sal_Int32 n = 0;
        aRequest.AppendItem(SfxStringItem( FN_PARAM_1, sLinkFileName.getToken( 0, sfx2::cTokenSeparator, n )));
        aRequest.AppendItem(SfxStringItem( FN_PARAM_2, sLinkFileName.getToken( 0, sfx2::cTokenSeparator, n )));
        aRequest.AppendItem(SfxStringItem( FN_PARAM_3, sLinkFileName.getToken( 0, sfx2::cTokenSeparator, n )));
        aRequest.Done();
    }
    return nRet;
}

SwInsertSectionTabPage::SwInsertSectionTabPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet &rAttrSet)
    : SfxTabPage(pPage, pController, u"modules/swriter/ui/sectionpage.ui"_ustr, u"SectionPage"_ustr, &rAttrSet)
    , m_pWrtSh(nullptr)
    , m_xCurName(m_xBuilder->weld_entry_tree_view(u"sectionnames"_ustr, u"sectionnames-entry"_ustr,
                                                  u"sectionnames-list"_ustr))
    , m_xFileCB(m_xBuilder->weld_check_button(u"link"_ustr))
    , m_xDDECB(m_xBuilder->weld_check_button(u"dde"_ustr))
    , m_xDDECommandFT(m_xBuilder->weld_label(u"ddelabel"_ustr))
    , m_xFileNameFT(m_xBuilder->weld_label(u"filelabel"_ustr))
    , m_xFileNameED(m_xBuilder->weld_entry(u"filename"_ustr))
    , m_xFilePB(m_xBuilder->weld_button(u"selectfile"_ustr))
    , m_xSubRegionFT(m_xBuilder->weld_label(u"sectionlabel"_ustr))
    , m_xSubRegionED(m_xBuilder->weld_combo_box(u"sectionname"_ustr))
    , m_xProtectCB(m_xBuilder->weld_check_button(u"protect"_ustr))
    , m_xPasswdCB(m_xBuilder->weld_check_button(u"withpassword"_ustr))
    , m_xPasswdPB(m_xBuilder->weld_button(u"selectpassword"_ustr))
    , m_xHideCB(m_xBuilder->weld_check_button(u"hide"_ustr))
    , m_xConditionFT(m_xBuilder->weld_label(u"condlabel"_ustr))
    , m_xConditionED(new ConditionEdit(m_xBuilder->weld_entry(u"withcond"_ustr)))
    // edit in readonly sections
    , m_xEditInReadonlyCB(m_xBuilder->weld_check_button(u"editable"_ustr))
{
    m_xCurName->make_sorted();
    m_xCurName->set_height_request_by_rows(12);
    m_xSubRegionED->make_sorted();

    m_xProtectCB->connect_toggled( LINK( this, SwInsertSectionTabPage, ChangeProtectHdl));
    m_xPasswdCB->connect_toggled( LINK( this, SwInsertSectionTabPage, TogglePasswdHdl));
    m_xPasswdPB->connect_clicked( LINK( this, SwInsertSectionTabPage, ChangePasswdHdl));
    m_xHideCB->connect_toggled( LINK( this, SwInsertSectionTabPage, ChangeHideHdl));
    m_xFileCB->connect_toggled( LINK( this, SwInsertSectionTabPage, UseFileHdl ));
    m_xFilePB->connect_clicked( LINK( this, SwInsertSectionTabPage, FileSearchHdl ));
    m_xCurName->connect_changed( LINK( this, SwInsertSectionTabPage, NameEditHdl));
    m_xDDECB->connect_toggled( LINK( this, SwInsertSectionTabPage, DDEHdl ));
    ChangeProtectHdl(*m_xProtectCB);
    m_xSubRegionED->set_entry_completion(true, true);

    // Hide Link section. In general it makes no sense to insert a file from the jail,
    // because it does not contain any usable files (documents).
    if(comphelper::LibreOfficeKit::isActive())
    {
        m_xBuilder->weld_label(u"label1"_ustr)->hide(); // Link
        m_xFileCB->hide();
        m_xDDECB->hide();
        m_xDDECommandFT->hide();
        m_xFileNameFT->hide();
        m_xFileNameED->hide();
        m_xFilePB->hide();
        m_xSubRegionFT->hide();
        m_xSubRegionED->hide();
    }
}

SwInsertSectionTabPage::~SwInsertSectionTabPage()
{
}

void    SwInsertSectionTabPage::SetWrtShell(SwWrtShell& rSh)
{
    m_pWrtSh = &rSh;

    bool bWeb = dynamic_cast<SwWebDocShell*>( m_pWrtSh->GetView().GetDocShell() )!= nullptr;
    if(bWeb)
    {
        m_xHideCB->hide();
        m_xConditionED->hide();
        m_xConditionFT->hide();
        m_xDDECB->hide();
        m_xDDECommandFT->hide();
    }

    lcl_FillSubRegionList(*m_pWrtSh, *m_xSubRegionED, m_xCurName.get());

    SwSectionData *const pSectionData =
        static_cast<SwInsertSectionTabDialog*>(GetDialogController())
            ->GetSectionData();
    if (pSectionData) // something set?
    {
        const OUString sSectionName(pSectionData->GetSectionName().toString());
        m_xCurName->set_entry_text(rSh.GetUniqueSectionName(&sSectionName));
        m_xProtectCB->set_active( pSectionData->IsProtectFlag() );
        ChangeProtectHdl(*m_xProtectCB);
        m_sFileName = pSectionData->GetLinkFileName();
        m_sFilePasswd = pSectionData->GetLinkFilePassword();
        m_xFileCB->set_active( !m_sFileName.isEmpty() );
        m_xFileNameED->set_text( m_sFileName );
        UseFileHdl(*m_xFileCB);
    }
    else
    {
        m_xCurName->set_entry_text(rSh.GetUniqueSectionName());
    }
}

bool SwInsertSectionTabPage::FillItemSet( SfxItemSet* )
{
    SwSectionData aSection(SectionType::Content, UIName(m_xCurName->get_active_text()));
    aSection.SetCondition(m_xConditionED->get_text());
    bool bProtected = m_xProtectCB->get_active();
    aSection.SetProtectFlag(bProtected);
    aSection.SetHidden(m_xHideCB->get_active());
    // edit in readonly sections
    aSection.SetEditInReadonlyFlag(m_xEditInReadonlyCB->get_active());

    if(bProtected)
    {
        aSection.SetPassword(m_aNewPasswd);
    }
    const OUString sFileName = m_xFileNameED->get_text();
    const OUString sSubRegion = m_xSubRegionED->get_active_text();
    bool bDDe = m_xDDECB->get_active();
    if (m_xFileCB->get_active() && (!sFileName.isEmpty() || !sSubRegion.isEmpty() || bDDe))
    {
        OUString aLinkFile;
        if( bDDe )
        {
            aLinkFile = CollapseWhiteSpaces(sFileName);
            sal_Int32 nPos = 0;
            aLinkFile = aLinkFile.replaceFirst( " ", OUStringChar(sfx2::cTokenSeparator), &nPos );
            if (nPos>=0)
            {
                aLinkFile = aLinkFile.replaceFirst( " ", OUStringChar(sfx2::cTokenSeparator), &nPos );
            }
        }
        else
        {
            if(!sFileName.isEmpty())
            {
                SfxMedium* pMedium = m_pWrtSh->GetView().GetDocShell()->GetMedium();
                INetURLObject aAbs;
                if( pMedium )
                    aAbs = pMedium->GetURLObject();
                aLinkFile = URIHelper::SmartRel2Abs(
                    aAbs, sFileName, URIHelper::GetMaybeFileHdl() );
                aSection.SetLinkFilePassword( m_sFilePasswd );
            }

            aLinkFile += OUStringChar(sfx2::cTokenSeparator) + m_sFilterName
                      +  OUStringChar(sfx2::cTokenSeparator) + sSubRegion;
        }

        aSection.SetLinkFileName(aLinkFile);
        if (!aLinkFile.isEmpty())
        {
            aSection.SetType( m_xDDECB->get_active() ?
                                    SectionType::DdeLink :
                                        SectionType::FileLink);
        }
    }
    static_cast<SwInsertSectionTabDialog*>(GetDialogController())->SetSectionData(aSection);
    return true;
}

void SwInsertSectionTabPage::Reset( const SfxItemSet* )
{
}

std::unique_ptr<SfxTabPage> SwInsertSectionTabPage::Create(weld::Container* pPage, weld::DialogController* pController,
                                                  const SfxItemSet* rAttrSet)
{
    return std::make_unique<SwInsertSectionTabPage>(pPage, pController, *rAttrSet);
}

IMPL_LINK(SwInsertSectionTabPage, ChangeHideHdl, weld::Toggleable&, rBox, void)
{
    bool bHide = rBox.get_active();
    m_xConditionED->set_sensitive(bHide);
    m_xConditionFT->set_sensitive(bHide);
}

IMPL_LINK(SwInsertSectionTabPage, ChangeProtectHdl, weld::Toggleable&, rBox, void)
{
    bool bCheck = rBox.get_active();
    m_xPasswdCB->set_sensitive(bCheck);
    m_xPasswdPB->set_sensitive(bCheck);
}

void SwInsertSectionTabPage::ChangePasswd(bool bChange)
{
    bool bSet = bChange ? bChange : m_xPasswdCB->get_active();
    if (bSet)
    {
        if(!m_aNewPasswd.hasElements() || bChange)
        {
            SfxPasswordDialog aPasswdDlg(GetFrameWeld());
            aPasswdDlg.ShowExtras(SfxShowExtras::CONFIRM);
            if (RET_OK == aPasswdDlg.run())
            {
                const OUString sNewPasswd(aPasswdDlg.GetPassword());
                if (aPasswdDlg.GetConfirm() == sNewPasswd)
                {
                    SvPasswordHelper::GetHashPassword( m_aNewPasswd, sNewPasswd );
                }
                else
                {
                    std::unique_ptr<weld::MessageDialog> xInfoBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                                  VclMessageType::Info, VclButtonsType::Ok,
                                                                  SwResId(STR_WRONG_PASSWD_REPEAT)));
                    xInfoBox->run();
                }
            }
            else if(!bChange)
                m_xPasswdCB->set_active(false);
        }
    }
    else
        m_aNewPasswd.realloc(0);
}

IMPL_LINK_NOARG(SwInsertSectionTabPage, TogglePasswdHdl, weld::Toggleable&, void)
{
    ChangePasswd(false);
}

IMPL_LINK_NOARG(SwInsertSectionTabPage, ChangePasswdHdl, weld::Button&, void)
{
    ChangePasswd(true);
}


IMPL_LINK_NOARG(SwInsertSectionTabPage, NameEditHdl, weld::ComboBox&, void)
{
    const OUString aName = m_xCurName->get_active_text();
    GetDialogController()->GetOKButton().set_sensitive(!aName.isEmpty() &&
            m_xCurName->find_text(aName) == -1);
}

IMPL_LINK(SwInsertSectionTabPage, UseFileHdl, weld::Toggleable&, rButton, void)
{
    if (rButton.get_active())
    {
        if (m_pWrtSh->HasSelection())
        {
            std::unique_ptr<weld::MessageDialog> xQueryBox(Application::CreateMessageDialog(GetFrameWeld(),
                                                           VclMessageType::Question, VclButtonsType::YesNo,
                                                           SwResId(STR_QUERY_CONNECT)));
            if (RET_NO == xQueryBox->run())
                rButton.set_active(false);
        }
    }

    bool bFile = rButton.get_active();
    m_xFileNameFT->set_sensitive(bFile);
    m_xFileNameED->set_sensitive(bFile);
    m_xFilePB->set_sensitive(bFile);
    m_xSubRegionFT->set_sensitive(bFile);
    m_xSubRegionED->set_sensitive(bFile);
    m_xDDECommandFT->set_sensitive(bFile);
    m_xDDECB->set_sensitive(bFile);
    if (bFile)
    {
        m_xFileNameED->grab_focus();
        m_xProtectCB->set_active(true);
        ChangeProtectHdl(*m_xProtectCB);
    }
    else
    {
        m_xDDECB->set_active(false);
        DDEHdl(*m_xDDECB);
    }
}

IMPL_LINK_NOARG(SwInsertSectionTabPage, FileSearchHdl, weld::Button&, void)
{
    m_pDocInserter.reset(new ::sfx2::DocumentInserter(GetFrameWeld(), u"swriter"_ustr));
    m_pDocInserter->StartExecuteModal( LINK( this, SwInsertSectionTabPage, DlgClosedHdl ) );
}

IMPL_LINK( SwInsertSectionTabPage, DDEHdl, weld::Toggleable&, rButton, void )
{
    bool bDDE = rButton.get_active();
    bool bFile = m_xFileCB->get_active();
    m_xFilePB->set_sensitive(!bDDE && bFile);
    if (bDDE)
    {
        m_xFileNameFT->hide();
        m_xDDECommandFT->set_sensitive(bDDE);
        m_xDDECommandFT->show();
        m_xSubRegionFT->hide();
        m_xSubRegionED->hide();
        m_xFileNameED->set_accessible_name(m_xDDECommandFT->get_label());
    }
    else
    {
        m_xDDECommandFT->hide();
        m_xFileNameFT->set_sensitive(bFile);
        if(!comphelper::LibreOfficeKit::isActive())
            m_xFileNameFT->show();
        m_xSubRegionFT->show();
        m_xSubRegionED->show();
        m_xSubRegionED->set_sensitive(bFile);
        m_xFileNameED->set_accessible_name(m_xFileNameFT->get_label());
    }
}

IMPL_LINK( SwInsertSectionTabPage, DlgClosedHdl, sfx2::FileDialogHelper *, _pFileDlg, void )
{
    if ( _pFileDlg->GetError() == ERRCODE_NONE )
    {
        std::unique_ptr<SfxMedium> pMedium(m_pDocInserter->CreateMedium("sglobal"));
        if ( pMedium )
        {
            m_sFileName = pMedium->GetURLObject().GetMainURL( INetURLObject::DecodeMechanism::NONE );
            m_sFilterName = pMedium->GetFilter()->GetFilterName();
            if ( const SfxStringItem* pItem = pMedium->GetItemSet().GetItemIfSet( SID_PASSWORD, false ) )
                m_sFilePasswd = pItem->GetValue();
            m_xFileNameED->set_text( INetURLObject::decode(
                m_sFileName, INetURLObject::DecodeMechanism::Unambiguous ) );
            ::lcl_ReadSections(*pMedium, *m_xSubRegionED);
        }
    }
    else
    {
        m_sFilterName.clear();
        m_sFilePasswd.clear();
    }
}

SwSectionFootnoteEndTabPage::SwSectionFootnoteEndTabPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet &rAttrSet)
    : SfxTabPage(pPage, pController, u"modules/swriter/ui/footnotesendnotestabpage.ui"_ustr, u"FootnotesEndnotesTabPage"_ustr, &rAttrSet)
    , m_xFootnoteNtAtTextEndCB(m_xBuilder->weld_check_button(u"ftnntattextend"_ustr))
    , m_xFootnoteNtNumCB(m_xBuilder->weld_check_button(u"ftnntnum"_ustr))
    , m_xFootnoteOffsetLbl(m_xBuilder->weld_label(u"ftnoffset_label"_ustr))
    , m_xFootnoteOffsetField(m_xBuilder->weld_spin_button(u"ftnoffset"_ustr))
    , m_xFootnoteNtNumFormatCB(m_xBuilder->weld_check_button(u"ftnntnumfmt"_ustr))
    , m_xFootnotePrefixFT(m_xBuilder->weld_label(u"ftnprefix_label"_ustr))
    , m_xFootnotePrefixED(m_xBuilder->weld_entry(u"ftnprefix"_ustr))
    , m_xFootnoteNumViewBox(new SwNumberingTypeListBox(m_xBuilder->weld_combo_box(u"ftnnumviewbox"_ustr)))
    , m_xFootnoteSuffixFT(m_xBuilder->weld_label(u"ftnsuffix_label"_ustr))
    , m_xFootnoteSuffixED(m_xBuilder->weld_entry(u"ftnsuffix"_ustr))
    , m_xEndNtAtTextEndCB(m_xBuilder->weld_check_button(u"endntattextend"_ustr))
    , m_xEndNtNumCB(m_xBuilder->weld_check_button(u"endntnum"_ustr))
    , m_xEndOffsetLbl(m_xBuilder->weld_label(u"endoffset_label"_ustr))
    , m_xEndOffsetField(m_xBuilder->weld_spin_button(u"endoffset"_ustr))
    , m_xEndNtNumFormatCB(m_xBuilder->weld_check_button(u"endntnumfmt"_ustr))
    , m_xEndPrefixFT(m_xBuilder->weld_label(u"endprefix_label"_ustr))
    , m_xEndPrefixED(m_xBuilder->weld_entry(u"endprefix"_ustr))
    , m_xEndNumViewBox(new SwNumberingTypeListBox(m_xBuilder->weld_combo_box(u"endnumviewbox"_ustr)))
    , m_xEndSuffixFT(m_xBuilder->weld_label(u"endsuffix_label"_ustr))
    , m_xEndSuffixED(m_xBuilder->weld_entry(u"endsuffix"_ustr))
{
    m_xFootnoteNumViewBox->Reload(SwInsertNumTypes::Extended);
    m_xEndNumViewBox->Reload(SwInsertNumTypes::Extended);

    Link<weld::Toggleable&,void> aLk( LINK( this, SwSectionFootnoteEndTabPage, FootEndHdl));
    m_xFootnoteNtAtTextEndCB->connect_toggled( aLk );
    m_xFootnoteNtNumCB->connect_toggled( aLk );
    m_xEndNtAtTextEndCB->connect_toggled( aLk );
    m_xEndNtNumCB->connect_toggled( aLk );
    m_xFootnoteNtNumFormatCB->connect_toggled( aLk );
    m_xEndNtNumFormatCB->connect_toggled( aLk );
}

SwSectionFootnoteEndTabPage::~SwSectionFootnoteEndTabPage()
{
}

bool SwSectionFootnoteEndTabPage::FillItemSet( SfxItemSet* rSet )
{
    SwFormatFootnoteAtTextEnd aFootnote( m_xFootnoteNtAtTextEndCB->get_active()
                            ? ( m_xFootnoteNtNumCB->get_active()
                                ? ( m_xFootnoteNtNumFormatCB->get_active()
                                    ? FTNEND_ATTXTEND_OWNNUMANDFMT
                                    : FTNEND_ATTXTEND_OWNNUMSEQ )
                                : FTNEND_ATTXTEND )
                            : FTNEND_ATPGORDOCEND );

    switch( aFootnote.GetValue() )
    {
    case FTNEND_ATTXTEND_OWNNUMANDFMT:
        aFootnote.SetNumType( m_xFootnoteNumViewBox->GetSelectedNumberingType() );
        aFootnote.SetPrefix( m_xFootnotePrefixED->get_text().replaceAll("\\t", "\t") ); // fdo#65666
        aFootnote.SetSuffix( m_xFootnoteSuffixED->get_text().replaceAll("\\t", "\t") );
        [[fallthrough]];

    case FTNEND_ATTXTEND_OWNNUMSEQ:
        aFootnote.SetOffset( static_cast< sal_uInt16 >( m_xFootnoteOffsetField->get_value()-1 ) );
        break;
    default: break;
    }

    SwFormatEndAtTextEnd aEnd( m_xEndNtAtTextEndCB->get_active()
                            ? ( m_xEndNtNumCB->get_active()
                                ? ( m_xEndNtNumFormatCB->get_active()
                                    ? FTNEND_ATTXTEND_OWNNUMANDFMT
                                    : FTNEND_ATTXTEND_OWNNUMSEQ )
                                : FTNEND_ATTXTEND )
                            : FTNEND_ATPGORDOCEND );

    switch( aEnd.GetValue() )
    {
    case FTNEND_ATTXTEND_OWNNUMANDFMT:
        aEnd.SetNumType( m_xEndNumViewBox->GetSelectedNumberingType() );
        aEnd.SetPrefix( m_xEndPrefixED->get_text().replaceAll("\\t", "\t") );
        aEnd.SetSuffix( m_xEndSuffixED->get_text().replaceAll("\\t", "\t") );
        [[fallthrough]];

    case FTNEND_ATTXTEND_OWNNUMSEQ:
        aEnd.SetOffset( static_cast< sal_uInt16 >( m_xEndOffsetField->get_value()-1 ) );
        break;
    default: break;
    }

    rSet->Put( aFootnote );
    rSet->Put( aEnd );

    return true;
}

void SwSectionFootnoteEndTabPage::ResetState( bool bFootnote,
                                         const SwFormatFootnoteEndAtTextEnd& rAttr )
{
    weld::CheckButton *pNtAtTextEndCB, *pNtNumCB, *pNtNumFormatCB;
    weld::Label *pPrefixFT, *pSuffixFT;
    weld::Entry *pPrefixED, *pSuffixED;
    SwNumberingTypeListBox *pNumViewBox;
    weld::Label *pOffsetText;
    weld::SpinButton *pOffsetField;

    if( bFootnote )
    {
        pNtAtTextEndCB = m_xFootnoteNtAtTextEndCB.get();
        pNtNumCB = m_xFootnoteNtNumCB.get();
        pNtNumFormatCB = m_xFootnoteNtNumFormatCB.get();
        pPrefixFT = m_xFootnotePrefixFT.get();
        pPrefixED = m_xFootnotePrefixED.get();
        pSuffixFT = m_xFootnoteSuffixFT.get();
        pSuffixED = m_xFootnoteSuffixED.get();
        pNumViewBox = m_xFootnoteNumViewBox.get();
        pOffsetText = m_xFootnoteOffsetLbl.get();
        pOffsetField = m_xFootnoteOffsetField.get();
    }
    else
    {
        pNtAtTextEndCB = m_xEndNtAtTextEndCB.get();
        pNtNumCB = m_xEndNtNumCB.get();
        pNtNumFormatCB = m_xEndNtNumFormatCB.get();
        pPrefixFT = m_xEndPrefixFT.get();
        pPrefixED = m_xEndPrefixED.get();
        pSuffixFT = m_xEndSuffixFT.get();
        pSuffixED = m_xEndSuffixED.get();
        pNumViewBox = m_xEndNumViewBox.get();
        pOffsetText = m_xEndOffsetLbl.get();
        pOffsetField = m_xEndOffsetField.get();
    }

    const sal_uInt16 eState = rAttr.GetValue();
    switch( eState )
    {
    case FTNEND_ATTXTEND_OWNNUMANDFMT:
        pNtNumFormatCB->set_state( TRISTATE_TRUE );
        [[fallthrough]];

    case FTNEND_ATTXTEND_OWNNUMSEQ:
        pNtNumCB->set_state( TRISTATE_TRUE );
        [[fallthrough]];

    case FTNEND_ATTXTEND:
        pNtAtTextEndCB->set_state( TRISTATE_TRUE );
        // no break;
    }

    pNumViewBox->SelectNumberingType( rAttr.GetNumType() );
    pOffsetField->set_value( rAttr.GetOffset() + 1 );
    pPrefixED->set_text( rAttr.GetPrefix().replaceAll("\t", "\\t") );
    pSuffixED->set_text( rAttr.GetSuffix().replaceAll("\t", "\\t") );

    switch( eState )
    {
    case FTNEND_ATPGORDOCEND:
        pNtNumCB->set_sensitive( false );
        [[fallthrough]];

    case FTNEND_ATTXTEND:
        pNtNumFormatCB->set_sensitive( false );
        pOffsetField->set_sensitive( false );
        pOffsetText->set_sensitive( false );
        [[fallthrough]];

    case FTNEND_ATTXTEND_OWNNUMSEQ:
        pNumViewBox->set_sensitive( false );
        pPrefixFT->set_sensitive( false );
        pPrefixED->set_sensitive( false );
        pSuffixFT->set_sensitive( false );
        pSuffixED->set_sensitive( false );
        // no break;
    }
}

void SwSectionFootnoteEndTabPage::Reset( const SfxItemSet* rSet )
{
    ResetState( true, rSet->Get( RES_FTN_AT_TXTEND, false ));
    ResetState( false, rSet->Get( RES_END_AT_TXTEND, false ));
}

std::unique_ptr<SfxTabPage> SwSectionFootnoteEndTabPage::Create( weld::Container* pPage, weld::DialogController* pController,
                                                   const SfxItemSet* rAttrSet)
{
    return std::make_unique<SwSectionFootnoteEndTabPage>(pPage, pController, *rAttrSet);
}

IMPL_LINK( SwSectionFootnoteEndTabPage, FootEndHdl, weld::Toggleable&, rBox, void )
{
    bool bFoot = m_xFootnoteNtAtTextEndCB.get() == &rBox || m_xFootnoteNtNumCB.get() == &rBox ||
                    m_xFootnoteNtNumFormatCB.get() == &rBox ;

    weld::CheckButton *pNumBox, *pNumFormatBox, *pEndBox;
    SwNumberingTypeListBox* pNumViewBox;
    weld::Label *pOffsetText;
    weld::SpinButton *pOffsetField;
    weld::Label *pPrefixFT, *pSuffixFT;
    weld::Entry *pPrefixED, *pSuffixED;

    if( bFoot )
    {
        pEndBox = m_xFootnoteNtAtTextEndCB.get();
        pNumBox = m_xFootnoteNtNumCB.get();
        pNumFormatBox = m_xFootnoteNtNumFormatCB.get();
        pNumViewBox = m_xFootnoteNumViewBox.get();
        pOffsetText = m_xFootnoteOffsetLbl.get();
        pOffsetField = m_xFootnoteOffsetField.get();
        pPrefixFT = m_xFootnotePrefixFT.get();
        pSuffixFT = m_xFootnoteSuffixFT.get();
        pPrefixED = m_xFootnotePrefixED.get();
        pSuffixED = m_xFootnoteSuffixED.get();
    }
    else
    {
        pEndBox = m_xEndNtAtTextEndCB.get();
        pNumBox = m_xEndNtNumCB.get();
        pNumFormatBox = m_xEndNtNumFormatCB.get();
        pNumViewBox = m_xEndNumViewBox.get();
        pOffsetText = m_xEndOffsetLbl.get();
        pOffsetField = m_xEndOffsetField.get();
        pPrefixFT = m_xEndPrefixFT.get();
        pSuffixFT = m_xEndSuffixFT.get();
        pPrefixED = m_xEndPrefixED.get();
        pSuffixED = m_xEndSuffixED.get();
    }

    bool bEnableAtEnd = TRISTATE_TRUE == pEndBox->get_state();
    bool bEnableNum = bEnableAtEnd && TRISTATE_TRUE == pNumBox->get_state();
    bool bEnableNumFormat = bEnableNum && TRISTATE_TRUE == pNumFormatBox->get_state();

    pNumBox->set_sensitive( bEnableAtEnd );
    pOffsetText->set_sensitive( bEnableNum );
    pOffsetField->set_sensitive( bEnableNum );
    pNumFormatBox->set_sensitive( bEnableNum );
    pNumViewBox->set_sensitive( bEnableNumFormat );
    pPrefixED->set_sensitive( bEnableNumFormat );
    pSuffixED->set_sensitive( bEnableNumFormat );
    pPrefixFT->set_sensitive( bEnableNumFormat );
    pSuffixFT->set_sensitive( bEnableNumFormat );
}

SwSectionPropertyTabDialog::SwSectionPropertyTabDialog(
    weld::Window* pParent, const SfxItemSet& rSet, SwWrtShell& rSh)
    : SfxTabDialogController(pParent, u"modules/swriter/ui/formatsectiondialog.ui"_ustr,
                             u"FormatSectionDialog"_ustr, &rSet)
    , m_rWrtSh(rSh)
{
    SfxAbstractDialogFactory* pFact = SfxAbstractDialogFactory::Create();
    AddTabPage(u"columns"_ustr,   SwColumnPage::Create, nullptr);
    AddTabPage(u"background"_ustr, pFact->GetTabPageCreatorFunc(RID_SVXPAGE_BKG), nullptr);
    AddTabPage(u"notes"_ustr, SwSectionFootnoteEndTabPage::Create, nullptr);
    AddTabPage(u"indents"_ustr, SwSectionIndentTabPage::Create, nullptr);

    tools::Long nHtmlMode = SvxHtmlOptions::GetExportMode();
    bool bWeb = dynamic_cast<SwWebDocShell*>( rSh.GetView().GetDocShell()  ) != nullptr ;
    if(bWeb)
    {
        RemoveTabPage(u"notes"_ustr);
        RemoveTabPage(u"indents"_ustr);
        if( HTML_CFG_NS40 != nHtmlMode && HTML_CFG_WRITER != nHtmlMode)
            RemoveTabPage(u"columns"_ustr);
    }
}

SwSectionPropertyTabDialog::~SwSectionPropertyTabDialog()
{
}

void SwSectionPropertyTabDialog::PageCreated(const OUString& rId, SfxTabPage &rPage)
{
    if (rId == "background")
    {
        SfxAllItemSet aSet(*(GetInputSetImpl()->GetPool()));
        aSet.Put (SfxUInt32Item(SID_FLAG_TYPE, static_cast<sal_uInt32>(SvxBackgroundTabFlags::SHOW_SELECTOR)));
        rPage.PageCreated(aSet);
    }
    else if (rId == "columns")
    {
        static_cast<SwColumnPage&>(rPage).ShowBalance(true);
        static_cast<SwColumnPage&>(rPage).SetInSection(true);
    }
    else if (rId == "indents")
        static_cast<SwSectionIndentTabPage&>(rPage).SetWrtShell(m_rWrtSh);
}

SwSectionIndentTabPage::SwSectionIndentTabPage(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet &rAttrSet)
    : SfxTabPage(pPage, pController, u"modules/swriter/ui/indentpage.ui"_ustr, u"IndentPage"_ustr, &rAttrSet)
    , m_xBeforeMF(m_xBuilder->weld_metric_spin_button(u"before"_ustr, FieldUnit::CM))
    , m_xAfterMF(m_xBuilder->weld_metric_spin_button(u"after"_ustr, FieldUnit::CM))
    , m_xPreviewWin(new weld::CustomWeld(*m_xBuilder, u"preview"_ustr, m_aPreviewWin))
{
    Link<weld::MetricSpinButton&,void> aLk = LINK(this, SwSectionIndentTabPage, IndentModifyHdl);
    m_xBeforeMF->connect_value_changed(aLk);
    m_xAfterMF->connect_value_changed(aLk);
}

SwSectionIndentTabPage::~SwSectionIndentTabPage()
{
}

bool SwSectionIndentTabPage::FillItemSet(SfxItemSet* rSet)
{
    if (m_xBeforeMF->get_value_changed_from_saved() || m_xAfterMF->get_value_changed_from_saved())
    {
        SvxLRSpaceItem aLRSpace(
            SvxIndentValue::twips(
                m_xBeforeMF->denormalize(m_xBeforeMF->get_value(FieldUnit::TWIP))),
            SvxIndentValue::twips(m_xAfterMF->denormalize(m_xAfterMF->get_value(FieldUnit::TWIP))),
            SvxIndentValue::zero(), RES_LR_SPACE);
        rSet->Put(aLRSpace);
    }
    return true;
}

void SwSectionIndentTabPage::Reset( const SfxItemSet* rSet)
{
    //this page doesn't show up in HTML mode
    FieldUnit aMetric = ::GetDfltMetric(false);
    SetFieldUnit(*m_xBeforeMF, aMetric);
    SetFieldUnit(*m_xAfterMF , aMetric);

    SfxItemState eItemState = rSet->GetItemState( RES_LR_SPACE );
    if ( eItemState >= SfxItemState::DEFAULT )
    {
        const SvxLRSpaceItem& rSpace =
            rSet->Get( RES_LR_SPACE );

        m_xBeforeMF->set_value(m_xBeforeMF->normalize(rSpace.ResolveLeft({})), FieldUnit::TWIP);
        m_xAfterMF->set_value(m_xAfterMF->normalize(rSpace.ResolveRight({})), FieldUnit::TWIP);
    }
    else
    {
        m_xBeforeMF->set_text(u""_ustr);
        m_xAfterMF->set_text(u""_ustr);
    }
    m_xBeforeMF->save_value();
    m_xAfterMF->save_value();
    IndentModifyHdl(*m_xBeforeMF);
}

std::unique_ptr<SfxTabPage> SwSectionIndentTabPage::Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rAttrSet)
{
    return std::make_unique<SwSectionIndentTabPage>(pPage, pController, *rAttrSet);
}

void SwSectionIndentTabPage::SetWrtShell(SwWrtShell const & rSh)
{
    //set sensible values at the preview
    m_aPreviewWin.SetAdjust(SvxAdjust::Block);
    m_aPreviewWin.SetLastLine(SvxAdjust::Block);
    const SwRect& rPageRect = rSh.GetAnyCurRect( CurRectType::Page );
    Size aPageSize(rPageRect.Width(), rPageRect.Height());
    m_aPreviewWin.SetSize(aPageSize);
}

IMPL_LINK_NOARG(SwSectionIndentTabPage, IndentModifyHdl, weld::MetricSpinButton&, void)
{
    m_aPreviewWin.SetLeftMargin(m_xBeforeMF->denormalize(m_xBeforeMF->get_value(FieldUnit::TWIP)));
    m_aPreviewWin.SetRightMargin(m_xAfterMF->denormalize(m_xAfterMF->get_value(FieldUnit::TWIP)));
    m_aPreviewWin.Invalidate();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
