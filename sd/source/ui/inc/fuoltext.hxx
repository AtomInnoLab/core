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

#include "fupoor.hxx"

class SdDrawDocument;
class SfxRequest;

namespace sd
{

class SimpleOutlinerView;

/**
 * Functions class for shells that host only an Outliner e.g. NotesPanel
 *
 */
class FuSimpleOutlinerText : public FuPoor
{
public:
    virtual bool Command(const CommandEvent& rCEvt) override;

    virtual bool KeyInput(const KeyEvent& rKEvt) override;
    virtual bool MouseMove(const MouseEvent& rMEvt) override;
    virtual bool MouseButtonUp(const MouseEvent& rMEvt) override;
    virtual bool MouseButtonDown(const MouseEvent& rMEvt) override;

    virtual void DoCut() override;
    virtual void DoCopy(bool bMergeMasterPagesOnly = false ) override;
    virtual void DoPaste(bool bMergeMasterPagesOnly = false ) override;
    virtual void DoPasteUnformatted() override;

    /** Call this method when the text in the outliner (may) have changed.
        It will invalidate some slots of the view frame.
    */
    virtual void UpdateForKeyPress (const KeyEvent& rEvent);

protected:
    FuSimpleOutlinerText(
        ViewShell* pViewShell,
        ::sd::Window* pWin,
        ::sd::SimpleOutlinerView* pView,
        SdDrawDocument* pDoc,
        SfxRequest& rReq);

    ViewShell* pOutlineViewShell;
    SimpleOutlinerView* mpSimpleOutlinerView;
};

class FuOutlineText final : public FuSimpleOutlinerText
{
public:
    static rtl::Reference<FuPoor> Create( ViewShell* pViewSh, ::sd::Window* pWin, ::sd::SimpleOutlinerView* pView, SdDrawDocument* pDoc, SfxRequest& rReq );

    virtual bool KeyInput(const KeyEvent& rKEvt) override;
    /** Call this method when the text in the outliner (may) have changed.
        It will invalidate some slots of the view frame and update the
        preview in the slide sorter.
    */
    virtual void UpdateForKeyPress(const KeyEvent& rEvent) override;

private:
    FuOutlineText(
        ViewShell* pViewShell,
        ::sd::Window* pWin,
        ::sd::SimpleOutlinerView* pView,
        SdDrawDocument* pDoc,
        SfxRequest& rReq);
};

} // end of namespace sd

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
