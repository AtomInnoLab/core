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
#include <sal/log.hxx>

#include <shutdownicon.hxx>
#include <sfx2/strings.hrc>
#include <sfx2/app.hxx>
#include <svtools/imagemgr.hxx>
#include <com/sun/star/task/InteractionHandler.hpp>
#include <com/sun/star/frame/Desktop.hpp>
#include <com/sun/star/frame/TerminationVetoException.hpp>
#include <com/sun/star/frame/XDispatchResultListener.hpp>
#include <com/sun/star/frame/XNotifyingDispatch.hpp>
#include <com/sun/star/frame/XFramesSupplier.hpp>
#include <com/sun/star/frame/XFrame.hpp>
#include <com/sun/star/util/URLTransformer.hpp>
#include <com/sun/star/util/XURLTransformer.hpp>
#include <com/sun/star/ui/dialogs/XFilePickerControlAccess.hpp>
#include <com/sun/star/ui/dialogs/XFilePicker3.hpp>
#include <com/sun/star/ui/dialogs/TemplateDescription.hpp>
#include <com/sun/star/ui/dialogs/ExtendedFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/CommonFilePickerElementIds.hpp>
#include <com/sun/star/ui/dialogs/ControlActions.hpp>
#include <com/sun/star/document/MacroExecMode.hpp>
#include <com/sun/star/document/UpdateDocMode.hpp>
#include <sfx2/filedlghelper.hxx>
#include <sfx2/docfilt.hxx>
#include <sfx2/fcontnr.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/propertyvalue.hxx>
#include <cppuhelper/implbase.hxx>
#include <cppuhelper/supportsservice.hxx>
#include <comphelper/extract.hxx>
#include <officecfg/Office/Common.hxx>
#include <tools/urlobj.hxx>
#include <tools/debug.hxx>
#include <osl/diagnose.h>
#include <osl/file.hxx>
#include <osl/module.hxx>
#include <rtl/ref.hxx>
#include <utility>
#include <vcl/svapp.hxx>

#include <sfx2/sfxresid.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::frame;
using namespace ::com::sun::star::container;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::util;
using namespace ::com::sun::star::ui::dialogs;
using namespace ::sfx2;

class SfxNotificationListener_Impl : public cppu::WeakImplHelper< XDispatchResultListener >
{
public:
    virtual void SAL_CALL dispatchFinished( const DispatchResultEvent& aEvent ) override;
    virtual void SAL_CALL disposing( const EventObject& aEvent ) override;
};

void SAL_CALL SfxNotificationListener_Impl::dispatchFinished( const DispatchResultEvent& )
{
    ShutdownIcon::LeaveModalMode();
}

void SAL_CALL SfxNotificationListener_Impl::disposing( const EventObject& )
{
}

OUString SAL_CALL ShutdownIcon::getImplementationName()
{
    return u"com.sun.star.comp.desktop.QuickstartWrapper"_ustr;
}

sal_Bool SAL_CALL ShutdownIcon::supportsService(OUString const & ServiceName)
{
    return cppu::supportsService(this, ServiceName);
}

css::uno::Sequence<OUString> SAL_CALL ShutdownIcon::getSupportedServiceNames()
{
    return { u"com.sun.star.office.Quickstart"_ustr };
}

bool ShutdownIcon::bModalMode = false;
rtl::Reference<ShutdownIcon> ShutdownIcon::pShutdownIcon;

void ShutdownIcon::initSystray()
{
    if (m_bInitialized)
        return;
    m_bInitialized = true;

#ifdef ENABLE_QUICKSTART_APPLET
#  ifdef _WIN32
    win32_init_sys_tray();
#  elif defined MACOSX
    aqua_init_systray();
#  endif // MACOSX
#endif // ENABLE_QUICKSTART_APPLET
}

void ShutdownIcon::deInitSystray()
{
    if (!m_bInitialized)
        return;

#ifdef ENABLE_QUICKSTART_APPLET
#  ifdef _WIN32
    win32_shutdown_sys_tray();
#  elif defined MACOSX
    aqua_shutdown_systray();
#  endif // MACOSX
#endif // ENABLE_QUICKSTART_APPLET

    m_bVeto = false;

    m_pFileDlg.reset();
    m_bInitialized = false;
}

static bool UseSystemFileDialog()
{
    return !Application::IsHeadlessModeEnabled() && officecfg::Office::Common::Misc::UseSystemFileDialog::get();
}

ShutdownIcon::ShutdownIcon( css::uno::Reference< XComponentContext > xContext ) :
    m_bVeto ( false ),
    m_bListenForTermination ( false ),
    m_bSystemDialogs(UseSystemFileDialog()),
    m_xContext(std::move( xContext )),
    m_bInitialized( false )
{
}

ShutdownIcon::~ShutdownIcon()
{
    deInitSystray();
}


void ShutdownIcon::OpenURL( const OUString& aURL, const OUString& rTarget, const Sequence< PropertyValue >& aArgs )
{
    if ( !getInstance() || !getInstance()->m_xDesktop.is() )
        return;

    css::uno::Reference < XDispatchProvider > xDispatchProvider = getInstance()->m_xDesktop;
    if ( !xDispatchProvider.is() )
        return;

    css::util::URL aDispatchURL;
    aDispatchURL.Complete = aURL;

    css::uno::Reference< util::XURLTransformer > xURLTransformer( util::URLTransformer::create( ::comphelper::getProcessComponentContext() ) );
    try
    {
        css::uno::Reference< css::frame::XDispatch > xDispatch;

        xURLTransformer->parseStrict( aDispatchURL );
        xDispatch = xDispatchProvider->queryDispatch( aDispatchURL, rTarget, 0 );
        if ( xDispatch.is() )
            xDispatch->dispatch( aDispatchURL, aArgs );
    }
    catch ( css::uno::RuntimeException& )
    {
        throw;
    }
    catch ( css::uno::Exception& )
    {
    }
}


void ShutdownIcon::FileOpen()
{
    if ( getInstance() && getInstance()->m_xDesktop.is() )
    {
        ::SolarMutexGuard aGuard;
        EnterModalMode();
        getInstance()->StartFileDialog();
    }
}


void ShutdownIcon::FromTemplate()
{
    ShutdownIcon::FromCommand( ".uno:NewDoc" );
}

void ShutdownIcon::FromCommand( const OUString& rCommand )
{
    if ( !getInstance() || !getInstance()->m_xDesktop.is() )
        return;

    css::uno::Reference < css::frame::XFramesSupplier > xDesktop = getInstance()->m_xDesktop;
    css::uno::Reference < css::frame::XFrame > xFrame( xDesktop->getActiveFrame() );
    if ( !xFrame.is() )
        xFrame = xDesktop;

    URL aTargetURL;
    aTargetURL.Complete = rCommand;
    css::uno::Reference< util::XURLTransformer > xTrans( util::URLTransformer::create( ::comphelper::getProcessComponentContext() ) );
    xTrans->parseStrict( aTargetURL );

    css::uno::Reference < css::frame::XDispatchProvider > xProv( xFrame, UNO_QUERY );
    css::uno::Reference < css::frame::XDispatch > xDisp;
    if ( xProv.is() )
    {
        xDisp = xProv->queryDispatch( aTargetURL, u"_self"_ustr, 0 );
    }
    if ( !xDisp.is() )
        return;

    Sequence<PropertyValue> aArgs { comphelper::makePropertyValue(u"Referer"_ustr, u"private:user"_ustr) };
    css::uno::Reference< css::frame::XNotifyingDispatch > xNotifier(xDisp, UNO_QUERY);
    if (xNotifier.is())
    {
        EnterModalMode();
        xNotifier->dispatchWithNotification(aTargetURL, aArgs, new SfxNotificationListener_Impl);
    }
    else
        xDisp->dispatch( aTargetURL, aArgs );
}

OUString ShutdownIcon::GetUrlDescription( std::u16string_view aUrl )
{
    return SvFileInformationManager::GetDescription( INetURLObject( aUrl ) );
}

void ShutdownIcon::StartFileDialog()
{
    ::SolarMutexGuard aGuard;

    bool bDirty = m_bSystemDialogs != UseSystemFileDialog();

    if ( m_pFileDlg && bDirty )
    {
        // Destroy instance as changing the system file dialog setting
        // forces us to create a new FileDialogHelper instance!
        m_pFileDlg.reset();
    }

    if ( !m_pFileDlg )
        m_pFileDlg.reset( new FileDialogHelper(
                ui::dialogs::TemplateDescription::FILEOPEN_READONLY_VERSION,
                FileDialogFlags::MultiSelection, OUString(), SfxFilterFlags::NONE, SfxFilterFlags::NONE, nullptr ) );
    m_pFileDlg->StartExecuteModal( LINK( this, ShutdownIcon, DialogClosedHdl_Impl ) );
}

IMPL_LINK( ShutdownIcon, DialogClosedHdl_Impl, FileDialogHelper*, /*unused*/, void )
{
    DBG_ASSERT( m_pFileDlg, "ShutdownIcon, DialogClosedHdl_Impl(): no file dialog" );

    // use constructor for filling up filters automatically!
    if ( ERRCODE_NONE == m_pFileDlg->GetError() )
    {
        css::uno::Reference< XFilePicker3 >    xPicker = m_pFileDlg->GetFilePicker();

        try
        {

            if ( xPicker.is() )
            {

                css::uno::Reference < XFilePickerControlAccess > xPickerControls ( xPicker, UNO_QUERY );

                Sequence< OUString >        sFiles = xPicker->getSelectedFiles();
                int                         nFiles = sFiles.getLength();

                css::uno::Reference < css::task::XInteractionHandler2 > xInteraction(
                    task::InteractionHandler::createWithParent(::comphelper::getProcessComponentContext(), nullptr) );

                int                         nArgs=3;
                Sequence< PropertyValue >   aArgs{
                    comphelper::makePropertyValue(u"InteractionHandler"_ustr, xInteraction),
                    comphelper::makePropertyValue(u"MacroExecutionMode"_ustr, sal_Int16(css::document::MacroExecMode::USE_CONFIG)),
                    comphelper::makePropertyValue(u"UpdateDocMode"_ustr, sal_Int16(css::document::UpdateDocMode::ACCORDING_TO_CONFIG))
                };

                // use the filedlghelper to get the current filter name,
                // because it removes the extensions before you get the filter name.
                OUString aFilterName( m_pFileDlg->GetCurrentFilter() );

                if ( xPickerControls.is() )
                {

                    // Set readonly flag

                    bool    bReadOnly = false;


                    xPickerControls->getValue( ExtendedFilePickerElementIds::CHECKBOX_READONLY, 0 ) >>= bReadOnly;

                    // Only set property if readonly is set to TRUE

                    if ( bReadOnly )
                    {
                        aArgs.realloc( ++nArgs );
                        auto pArgs = aArgs.getArray();
                        pArgs[nArgs-1].Name  = "ReadOnly";
                        pArgs[nArgs-1].Value <<= bReadOnly;
                    }

                    // Get version string

                    sal_Int32   iVersion = -1;

                    xPickerControls->getValue( ExtendedFilePickerElementIds::LISTBOX_VERSION, ControlActions::GET_SELECTED_ITEM_INDEX ) >>= iVersion;

                    if ( iVersion >= 0 )
                    {
                        sal_Int16   uVersion = static_cast<sal_Int16>(iVersion);

                        aArgs.realloc( ++nArgs );
                        auto pArgs = aArgs.getArray();
                        pArgs[nArgs-1].Name  = "Version";
                        pArgs[nArgs-1].Value <<= uVersion;
                    }

                    // Retrieve the current filter

                    if ( aFilterName.isEmpty() )
                        xPickerControls->getValue( CommonFilePickerElementIds::LISTBOX_FILTER, ControlActions::GET_SELECTED_ITEM ) >>= aFilterName;

                }


                // Convert UI filter name to internal filter name

                if ( !aFilterName.isEmpty() )
                {
                    std::shared_ptr<const SfxFilter> pFilter = SfxGetpApp()->GetFilterMatcher().GetFilter4UIName( aFilterName, SfxFilterFlags::NONE, SfxFilterFlags::NOTINFILEDLG );

                    if ( pFilter )
                    {
                        aFilterName = pFilter->GetFilterName();

                        if ( !aFilterName.isEmpty() )
                        {
                            aArgs.realloc( ++nArgs );
                            auto pArgs = aArgs.getArray();
                            pArgs[nArgs-1].Name  = "FilterName";
                            pArgs[nArgs-1].Value <<= aFilterName;
                        }
                    }
                }

                if ( 1 == nFiles )
                    OpenURL( sFiles[0], u"_default"_ustr, aArgs );
                else
                {
                    OUString    aBaseDirURL = sFiles[0];
                    if ( !aBaseDirURL.isEmpty() && !aBaseDirURL.endsWith("/") )
                        aBaseDirURL += "/";

                    int iFiles;
                    for ( iFiles = 1; iFiles < nFiles; iFiles++ )
                    {
                        OUString aURL = aBaseDirURL + sFiles[iFiles];
                        OpenURL( aURL, u"_default"_ustr, aArgs );
                    }
                }
            }
        }
        catch ( ... )
        {
        }
    }

#ifdef _WIN32
    // Destroy dialog to prevent problems with custom controls
    // This fix is dependent on the dialog settings. Destroying the dialog here will
    // crash the non-native dialog implementation! Therefore make this dependent on
    // the settings.
    if (UseSystemFileDialog())
    {
        m_pFileDlg.reset();
    }
#endif

    LeaveModalMode();
}


void ShutdownIcon::addTerminateListener()
{
    ShutdownIcon* pInst = getInstance();
    if ( ! pInst)
        return;

    if (pInst->m_bListenForTermination)
        return;

    css::uno::Reference< XDesktop2 > xDesktop = pInst->m_xDesktop;
    if ( ! xDesktop.is())
        return;

    xDesktop->addTerminateListener( pInst );
    pInst->m_bListenForTermination = true;
}


void ShutdownIcon::terminateDesktop()
{
    ShutdownIcon* pInst = getInstance();
    if ( ! pInst)
        return;

    css::uno::Reference< XDesktop2 > xDesktop = pInst->m_xDesktop;
    if ( ! xDesktop.is())
        return;

    // always remove ourselves as listener
    pInst->m_bListenForTermination = true;
    xDesktop->removeTerminateListener( pInst );

    // terminate desktop only if no tasks exist
    css::uno::Reference< XIndexAccess > xTasks = xDesktop->getFrames();
    if( xTasks.is() && xTasks->getCount() < 1 )
        Application::Quit();

    // remove the instance pointer
    ShutdownIcon::pShutdownIcon = nullptr;
}


ShutdownIcon* ShutdownIcon::getInstance()
{
    OSL_ASSERT( pShutdownIcon );
    return pShutdownIcon.get();
}


ShutdownIcon* ShutdownIcon::createInstance()
{
    if (pShutdownIcon)
        return pShutdownIcon.get();

    try {
        rtl::Reference<ShutdownIcon> pIcon(new ShutdownIcon( comphelper::getProcessComponentContext() ));
        pIcon->init ();
        pShutdownIcon = std::move(pIcon);
    } catch (...) {
    }

    return pShutdownIcon.get();
}

void ShutdownIcon::init()
{
    css::uno::Reference < XDesktop2 > xDesktop = Desktop::create( m_xContext );
    std::unique_lock aGuard(m_aMutex);
    m_xDesktop = std::move(xDesktop);
}

void ShutdownIcon::disposing(std::unique_lock<std::mutex>&)
{
    m_xContext.clear();
    m_xDesktop.clear();

    deInitSystray();
}

// XEventListener
void SAL_CALL ShutdownIcon::disposing( const css::lang::EventObject& )
{
}

// XTerminateListener
void SAL_CALL ShutdownIcon::queryTermination( const css::lang::EventObject& )
{
    SAL_INFO("sfx.appl", "ShutdownIcon::queryTermination: veto is " << m_bVeto);
    std::unique_lock  aGuard( m_aMutex );

    if ( m_bVeto )
        throw css::frame::TerminationVetoException();
}


void SAL_CALL ShutdownIcon::notifyTermination( const css::lang::EventObject& )
{
}


void SAL_CALL ShutdownIcon::initialize( const css::uno::Sequence< css::uno::Any>& aArguments )
{
    std::unique_lock aGuard( m_aMutex );

    // third argument only sets veto, everything else will be ignored!
    if (aArguments.getLength() > 2)
    {
        bool bVeto = ::cppu::any2bool(aArguments[2]);
        m_bVeto = bVeto;
        return;
    }

    if ( aArguments.getLength() > 0 )
    {
        if ( !ShutdownIcon::pShutdownIcon )
        {
            try
            {
                bool bQuickstart = ::cppu::any2bool( aArguments[0] );
                if( !bQuickstart && !GetAutostart() )
                    return;
                aGuard.unlock();
                init ();
                aGuard.lock();
                if ( !m_xDesktop.is() )
                    return;

                /* Create a sub-classed instance - foo */
                ShutdownIcon::pShutdownIcon = this;
                initSystray();
            }
            catch(const css::lang::IllegalArgumentException&)
            {
            }
        }
    }
    if ( aArguments.getLength() > 1 )
    {
            bool bAutostart = ::cppu::any2bool( aArguments[1] );
            if (bAutostart && !GetAutostart())
                SetAutostart( true );
            if (!bAutostart && GetAutostart())
                SetAutostart( false );
    }

}


void ShutdownIcon::EnterModalMode()
{
    bModalMode = true;
}


void ShutdownIcon::LeaveModalMode()
{
    bModalMode = false;
}

#ifdef _WIN32
// defined in shutdowniconw32.cxx
#elif defined MACOSX
// defined in shutdowniconaqua.cxx
#else
bool ShutdownIcon::IsQuickstarterInstalled()
{
    return false;
}
#endif


#ifdef ENABLE_QUICKSTART_APPLET
#ifdef _WIN32
OUString ShutdownIcon::getShortcutName()
{
    return GetAutostartFolderNameW32() + "\\" + SfxResId(STR_QUICKSTART_LNKNAME) + ".lnk";
}
#endif // _WIN32
#endif

bool ShutdownIcon::GetAutostart( )
{
#if defined MACOSX
    return true;
#elif defined ENABLE_QUICKSTART_APPLET
    bool bRet = false;
    OUString aShortcut( getShortcutName() );
    OUString aShortcutUrl;
    osl::File::getFileURLFromSystemPath( aShortcut, aShortcutUrl );
    osl::File f( aShortcutUrl );
    osl::File::RC error = f.open( osl_File_OpenFlag_Read );
    if( error == osl::File::E_None )
    {
        f.close();
        bRet = true;
    }
    return bRet;
#else // ENABLE_QUICKSTART_APPLET
    return false;
#endif
}

void ShutdownIcon::SetAutostart( bool bActivate )
{
#ifdef ENABLE_QUICKSTART_APPLET
#ifndef MACOSX
    OUString aShortcut( getShortcutName() );
#endif

    if( bActivate && IsQuickstarterInstalled() )
    {
#ifdef _WIN32
        EnableAutostartW32( aShortcut );
#endif
    }
    else
    {
#ifndef MACOSX
        OUString aShortcutUrl;
        ::osl::File::getFileURLFromSystemPath( aShortcut, aShortcutUrl );
        ::osl::File::remove( aShortcutUrl );
#endif
    }
#else
    (void)bActivate; // unused variable
#endif // ENABLE_QUICKSTART_APPLET
}

const ::sal_Int32 PROPHANDLE_TERMINATEVETOSTATE = 0;

// XFastPropertySet
void SAL_CALL ShutdownIcon::setFastPropertyValue(       ::sal_Int32                  nHandle,
                                                  const css::uno::Any& aValue )
{
    switch(nHandle)
    {
        case PROPHANDLE_TERMINATEVETOSTATE :
             {
                // use new value in case it's a valid information only
                bool bState( false );
                if (! (aValue >>= bState))
                    return;

                m_bVeto = bState;
                if (m_bVeto && ! m_bListenForTermination)
                    addTerminateListener();
             }
             break;

        default :
            throw css::beans::UnknownPropertyException(OUString::number(nHandle));
    }
}

// XFastPropertySet
css::uno::Any SAL_CALL ShutdownIcon::getFastPropertyValue( ::sal_Int32 nHandle )
{
    css::uno::Any aValue;
    switch(nHandle)
    {
        case PROPHANDLE_TERMINATEVETOSTATE :
             {
                bool bState   = (m_bListenForTermination && m_bVeto);
                aValue <<= bState;
             }
             break;

        default :
            throw css::beans::UnknownPropertyException(OUString::number(nHandle));
    }

    return aValue;
}

extern "C" SAL_DLLPUBLIC_EXPORT css::uno::XInterface *
com_sun_star_comp_desktop_QuickstartWrapper_get_implementation(
    css::uno::XComponentContext *context,
    css::uno::Sequence<css::uno::Any> const &)
{
    return cppu::acquire(new ShutdownIcon(context));
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
