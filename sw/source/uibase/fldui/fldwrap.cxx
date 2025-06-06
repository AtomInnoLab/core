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

#include <cmdid.h>
#include <sfx2/basedlgs.hxx>
#include <docsh.hxx>
#include <fldwrap.hxx>

#include <swabstdlg.hxx>

SFX_IMPL_CHILDWINDOW_WITHID(SwFieldDlgWrapper, FN_INSERT_FIELD)

SwChildWinWrapper::SwChildWinWrapper(vcl::Window *pParentWindow, sal_uInt16 nId) :
        SfxChildWindow(pParentWindow, nId),
        m_aUpdateTimer("SwChildWinWrapper m_aUpdateTimer"),
        m_pDocSh(nullptr)
{
    // avoid flickering of buttons:
    m_aUpdateTimer.SetTimeout(200);
    m_aUpdateTimer.SetInvokeHandler(LINK(this, SwChildWinWrapper, UpdateHdl));
}

IMPL_LINK_NOARG(SwChildWinWrapper, UpdateHdl, Timer *, void)
{
    if (GetController())
        GetController()->Activate();    // update dialog
}

// newly initialise dialog after Doc switch
void SwChildWinWrapper::ReInitDlg()
{
    m_aUpdateTimer.Start();
}

SfxChildWinInfo SwFieldDlgWrapper::GetInfo() const
{
    SfxChildWinInfo aInfo = SfxChildWindow::GetInfo();
    return aInfo;
}

SwFieldDlgWrapper::SwFieldDlgWrapper( vcl::Window* _pParent, sal_uInt16 nId,
                                    SfxBindings* pB,
                                    SfxChildWinInfo*  )
    : SwChildWinWrapper( _pParent, nId )
{
    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    m_pDlgInterface = pFact->CreateSwFieldDlg(pB, this, _pParent->GetFrameWeld());
    SetController(m_pDlgInterface->GetController());
    m_pDlgInterface->StartExecuteAsync(nullptr);
}

void SwFieldDlgWrapper::ShowReferencePage()
{
    m_pDlgInterface->ShowReferencePage();
}

SFX_IMPL_CHILDWINDOW(SwFieldDataOnlyDlgWrapper, FN_INSERT_FIELD_DATA_ONLY)

SfxChildWinInfo SwFieldDataOnlyDlgWrapper::GetInfo() const
{
    SfxChildWinInfo aInfo = SfxChildWindow::GetInfo();
// prevent instantiation of dialog other than by calling
// the mail merge dialog
    aInfo.bVisible = false;
    return aInfo;
}

SwFieldDataOnlyDlgWrapper::SwFieldDataOnlyDlgWrapper( vcl::Window* _pParent, sal_uInt16 nId,
                                    SfxBindings* pB,
                                    SfxChildWinInfo* pInfo )
    : SwChildWinWrapper( _pParent, nId )
{
    SwAbstractDialogFactory* pFact = SwAbstractDialogFactory::Create();
    m_pDlgInterface = pFact->CreateSwFieldDlg(pB, this, _pParent->GetFrameWeld());

    SetController(m_pDlgInterface->GetController());
    m_pDlgInterface->ActivateDatabasePage();
    m_pDlgInterface->StartExecuteAsync(nullptr);
    m_pDlgInterface->Initialize( pInfo );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
