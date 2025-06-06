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

#include <sal/config.h>

#include <o3tl/test_info.hxx>
#include <unx/salframe.h>
#include <unx/salinst.h>
#include <dndhelper.hxx>
#include <vcl/sysdata.hxx>

#include "X11_clipboard.hxx"
#include <com/sun/star/lang/IllegalArgumentException.hpp>

using namespace cppu;
using namespace com::sun::star::uno;
using namespace com::sun::star::lang;
using namespace com::sun::star::datatransfer::clipboard;
using namespace x11;

namespace
{
void InitializeDnD(const css::uno::Reference<css::lang::XInitialization>& xDnD,
                   const X11SalFrame* pFrame)
{
    ::Window aShellWindow = pFrame ? pFrame->GetShellWindow() : 0;
    if (aShellWindow && xDnD)
        xDnD->initialize({ css::uno::Any(Application::GetDisplayConnection()),
                           css::uno::Any(static_cast<sal_uInt64>(aShellWindow)) });
}
}

Sequence< OUString > x11::X11Clipboard_getSupportedServiceNames()
{
    return { u"com.sun.star.datatransfer.clipboard.SystemClipboard"_ustr };
}

Sequence< OUString > x11::Xdnd_getSupportedServiceNames()
{
    return { u"com.sun.star.datatransfer.dnd.X11DragSource"_ustr };
}

Sequence< OUString > x11::Xdnd_dropTarget_getSupportedServiceNames()
{
    return { u"com.sun.star.datatransfer.dnd.X11DropTarget"_ustr };
}

css::uno::Reference<css::datatransfer::clipboard::XClipboard>
X11SalInstance::CreateClipboard(const Sequence<Any>& arguments)
{
    if ( o3tl::IsRunningUnitTest() || o3tl::IsRunningUITest() )
        return SalInstance::CreateClipboard( arguments );

    SelectionManager& rManager = SelectionManager::get();
    css::uno::Sequence<css::uno::Any> mgrArgs{ css::uno::Any(Application::GetDisplayConnection()) };
    rManager.initialize(mgrArgs);

    OUString sel;
    if (!arguments.hasElements()) {
        sel = "CLIPBOARD";
    } else if (arguments.getLength() != 1 || !(arguments[0] >>= sel)) {
        throw css::lang::IllegalArgumentException(
            u"bad X11SalInstance::CreateClipboard arguments"_ustr,
            css::uno::Reference<css::uno::XInterface>(), -1);
    }
    Atom nSelection = rManager.getAtom(sel);

    std::unordered_map< Atom, css::uno::Reference< XClipboard > >::iterator it = m_aInstances.find( nSelection );
    if( it != m_aInstances.end() )
        return it->second;

    css::uno::Reference<css::datatransfer::clipboard::XClipboard> pClipboard = X11Clipboard::create( rManager, nSelection );
    m_aInstances[ nSelection ] = pClipboard;

    return pClipboard;
}

css::uno::Reference<css::datatransfer::dnd::XDragSource>
X11SalInstance::ImplCreateDragSource(const SystemEnvData* pSysEnv)
{
    rtl::Reference<SelectionManagerHolder> xSelectionManagerHolder = new SelectionManagerHolder();
    InitializeDnD(xSelectionManagerHolder, static_cast<X11SalFrame*>(pSysEnv->pSalFrame));
    return xSelectionManagerHolder;
}

css::uno::Reference<css::datatransfer::dnd::XDropTarget>
X11SalInstance::ImplCreateDropTarget(const SystemEnvData* pSysEnv)
{
    rtl::Reference<DropTarget> xDropTarget = new DropTarget();
    InitializeDnD(xDropTarget, static_cast<X11SalFrame*>(pSysEnv->pSalFrame));
    return xDropTarget;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
