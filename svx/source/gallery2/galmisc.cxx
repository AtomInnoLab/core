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

#include <sot/storage.hxx>
#include <unotools/streamwrap.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/string.hxx>
#include <ucbhelper/content.hxx>
#include <com/sun/star/ucb/ContentCreationException.hpp>
#include <tools/urlobj.hxx>
#include <vcl/graphicfilter.hxx>
#include <svl/itempool.hxx>
#include <sfx2/docfile.hxx>
#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <svx/svdpage.hxx>
#include <svx/dialmgr.hxx>
#include <svx/svdograf.hxx>
#include <svx/fmmodel.hxx>
#include <svx/unomodel.hxx>
#include "codec.hxx"
#include <svx/strings.hrc>
#include <svx/galtheme.hxx>
#include <svx/galmisc.hxx>
#include <osl/diagnose.h>
#include <com/sun/star/awt/XProgressMonitor.hpp>
#include <com/sun/star/ucb/TransferInfo.hpp>
#include <com/sun/star/ucb/NameClash.hpp>
#include <memory>

using namespace ::com::sun::star;

GalleryGraphicImportRet GalleryGraphicImport( const INetURLObject& rURL, Graphic& rGraphic,
                             OUString& rFilterName )
{
    GalleryGraphicImportRet  nRet = GalleryGraphicImportRet::IMPORT_NONE;
    SfxMedium   aMedium( rURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), StreamMode::READ );

    aMedium.Download();

    SvStream* pIStm = aMedium.GetInStream();

    if( pIStm )
    {
        GraphicFilter& rGraphicFilter = GraphicFilter::GetGraphicFilter();
        sal_uInt16              nFormat;

        if( !rGraphicFilter.ImportGraphic( rGraphic, rURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), *pIStm, GRFILTER_FORMAT_DONTKNOW, &nFormat ) )
        {
            rFilterName = rGraphicFilter.GetImportFormatName( nFormat );
            nRet = GalleryGraphicImportRet::IMPORT_FILE;
        }
    }

    return nRet;
}

bool GallerySvDrawImport( SvStream& rIStm, SdrModel& rModel )
{
    sal_uInt32  nVersion;
    bool        bRet = false;

    if( GalleryCodec::IsCoded( rIStm, nVersion ) )
    {
        SvMemoryStream  aMemStm( 65535, 65535 );
        GalleryCodec    aCodec( rIStm );

        aCodec.Read( aMemStm );
        aMemStm.Seek( 0 );

        if ( 2 == nVersion )
        {
            // recall to read as XML
            bRet = GallerySvDrawImport( aMemStm, rModel );
        }
    }
    else
    {
        // read as XML
        uno::Reference< io::XInputStream > xInputStream( new utl::OInputStreamWrapper( rIStm ) );

        rModel.GetItemPool().SetDefaultMetric( MapUnit::Map100thMM );
        uno::Reference< lang::XComponent > xComponent;

        bRet = SvxDrawingLayerImport( &rModel, xInputStream, xComponent, "com.sun.star.comp.Draw.XMLOasisImporter" );
        if( !bRet || (rModel.GetPageCount() == 0) )
        {
            rIStm.Seek(0);
            bRet = SvxDrawingLayerImport( &rModel, xInputStream, xComponent, "com.sun.star.comp.Draw.XMLImporter" );
        }

    }

    return bRet;
}

bool CreateIMapGraphic( const FmFormModel& rModel, Graphic& rGraphic, ImageMap& rImageMap )
{
    if (! rModel.GetPageCount() )
        return false;

    const SdrPage*      pPage = rModel.GetPage( 0 );
    const SdrObject*    pObj = pPage->GetObj( 0 );

    if ( pPage->GetObjCount() != 1 )
        return false;
    auto pGrafObj = dynamic_cast<const SdrGrafObj*>( pObj);
    if (!pGrafObj)
        return false;

    bool bRet = false;
    const sal_uInt16 nCount = pObj->GetUserDataCount();

    // Exist in the user data an IMap information?
    for ( sal_uInt16 i = 0; i < nCount; i++ )
    {
        const SdrObjUserData* pUserData = pObj->GetUserData( i );

        if ( ( pUserData->GetInventor() == SdrInventor::SgaImap ) && ( pUserData->GetId() == ID_IMAPINFO ) )
        {
            rGraphic = pGrafObj->GetGraphic();
            rImageMap = static_cast<const SgaIMapInfo*>( pUserData )->GetImageMap();
            bRet = true;
            break;
        }
    }

    return bRet;
}

OUString GetReducedString( const INetURLObject& rURL, sal_Int32 nMaxLen )
{
    OUString aReduced( rURL.GetMainURL( INetURLObject::DecodeMechanism::Unambiguous ) );

    aReduced = aReduced.copy(aReduced.lastIndexOf('/')+1);

    if( INetProtocol::PrivSoffice != rURL.GetProtocol() )
    {
        sal_Unicode     aDelimiter;
        const OUString  aPath( rURL.getFSysPath( FSysStyle::Detect, &aDelimiter ) );
        const OUString  aName( aReduced );

        if( aPath.getLength() > nMaxLen )
        {
            sal_Int32 nPathPrefixLen = nMaxLen - aName.getLength() - 4;

            if (nPathPrefixLen >= 0)
            {
                aReduced = OUString::Concat(aPath.subView(0, nPathPrefixLen)) + "..."
                    + OUStringChar(aDelimiter) + aName;
            }
            else
            {
                aReduced += "..." + OUStringChar(aDelimiter) + "..."
                    + aName.subView( aName.getLength() - (nMaxLen - 7) );
            }
        }
        else
            aReduced = aPath;
    }

    return aReduced;
}

OUString GetSvDrawStreamNameFromURL( const INetURLObject& rSvDrawObjURL )
{
    OUString aRet;

    if( rSvDrawObjURL.GetProtocol() == INetProtocol::PrivSoffice &&
        comphelper::string::getTokenCount(rSvDrawObjURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), '/') == 3 )
    {
        aRet = rSvDrawObjURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ).getToken( 2, '/' );
    }

    return aRet;
}

bool FileExists( const INetURLObject& rURL )
{
    bool bRet = false;

    if( rURL.GetProtocol() != INetProtocol::NotValid )
    {
        try
        {
            ::ucbhelper::Content        aCnt( rURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), uno::Reference< ucb::XCommandEnvironment >(), comphelper::getProcessComponentContext() );
            OUString    aTitle;

            aCnt.getPropertyValue(u"Title"_ustr) >>= aTitle;
            bRet = ( !aTitle.isEmpty() );
        }
        catch( const ucb::ContentCreationException& )
        {
        }
        catch( const uno::RuntimeException& )
        {
        }
        catch( const uno::Exception& )
        {
        }
    }

    return bRet;
}

bool CreateDir( const INetURLObject& rURL )
{
    bool bRet = FileExists( rURL );

    if( !bRet )
    {
        try
        {
            uno::Reference< ucb::XCommandEnvironment >  aCmdEnv;
            INetURLObject                           aParentURL( rURL );
            aParentURL.removeSegment();
            ::ucbhelper::Content                    aParent( aParentURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), aCmdEnv, comphelper::getProcessComponentContext() );
            uno::Sequence< OUString >               aProps{ u"Title"_ustr };
            uno::Sequence< uno::Any >               aValues{ uno::Any(rURL.GetLastName()) };

            ::ucbhelper::Content aContent( rURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), aCmdEnv, comphelper::getProcessComponentContext() );
            bRet = aParent.insertNewContent( u"application/vnd.sun.staroffice.fsys-folder"_ustr, aProps, aValues, aContent );
        }
        catch( const ucb::ContentCreationException& )
        {
        }
        catch( const uno::RuntimeException& )
        {
        }
        catch( const uno::Exception& )
        {
        }
    }

    return bRet;
}

bool CopyFile(  const INetURLObject& rSrcURL, const INetURLObject& rDstURL )
{
    bool bRet = false;

    try
    {
        ::ucbhelper::Content aDestPath( rDstURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), uno::Reference< ucb::XCommandEnvironment >(), comphelper::getProcessComponentContext() );

        aDestPath.executeCommand( u"transfer"_ustr,
                                  uno::Any( ucb::TransferInfo( false, rSrcURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                                                rDstURL.GetLastName(), ucb::NameClash::OVERWRITE ) ) );
        bRet = true;
    }
    catch( const ucb::ContentCreationException& )
    {
    }
    catch( const uno::RuntimeException& )
    {
    }
    catch( const uno::Exception& )
    {
    }

    return bRet;
}

bool KillFile( const INetURLObject& rURL )
{
    bool bRet = FileExists( rURL );

    if( bRet )
    {
        try
        {
            ::ucbhelper::Content aCnt( rURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), uno::Reference< ucb::XCommandEnvironment >(), comphelper::getProcessComponentContext() );
            aCnt.executeCommand( u"delete"_ustr, uno::Any( true ) );
        }
        catch( const ucb::ContentCreationException& )
        {
            bRet = false;
        }
        catch( const uno::RuntimeException& )
        {
            bRet = false;
        }
        catch( const uno::Exception& )
        {
            bRet = false;
        }
    }

    return bRet;
}


GalleryProgress::GalleryProgress( const GraphicFilter* pFilter )
{

    uno::Reference< lang::XMultiServiceFactory > xMgr( ::comphelper::getProcessServiceFactory() );

    uno::Reference< awt::XProgressMonitor > xMonitor( xMgr->createInstance( u"com.sun.star.awt.XProgressMonitor"_ustr ),
                                                      uno::UNO_QUERY );

    if ( !xMonitor.is() )
        return;

    mxProgressBar = xMonitor;

    OUString aProgressText;

    if( pFilter )
    {
        aProgressText = SvxResId(RID_SVXSTR_GALLERY_FILTER);
//          pFilter->SetUpdatePercentHdl( LINK( this, GalleryProgress, Update ) );     // sj: progress wasn't working up from SO7 at all
//                                                                                     // so I am removing this. The gallery progress should
//                                                                                     // be changed to use the XStatusIndicator instead of XProgressMonitor
    }
    else
        aProgressText = "Gallery";

    xMonitor->addText( u"Gallery"_ustr, aProgressText, false ) ;
    mxProgressBar->setRange( 0, GALLERY_PROGRESS_RANGE );
}

GalleryProgress::~GalleryProgress()
{
}

void GalleryProgress::Update( sal_Int32 nVal, sal_Int32 nMaxVal )
{
    if( mxProgressBar.is() && nMaxVal )
        mxProgressBar->setValue( std::min<sal_Int32>( static_cast<double>(nVal) / nMaxVal * GALLERY_PROGRESS_RANGE,
                                                      GALLERY_PROGRESS_RANGE ) );
}


GalleryTransferable::GalleryTransferable( GalleryTheme* pTheme, sal_uInt32 nObjectPos, bool bLazy ) :
    mpTheme( pTheme ),
    meObjectKind( pTheme ? mpTheme->GetObjectKind(nObjectPos) : SgaObjKind::NONE),
    mnObjectPos( nObjectPos )
{

    InitData( bLazy );
}

void GalleryTransferable::SelectObject(sal_uInt32 nObjectPos)
{
    if (nObjectPos == mnObjectPos)
        return;
    ClearFormats();
    mnObjectPos = nObjectPos;
    meObjectKind = mpTheme ? mpTheme->GetObjectKind(mnObjectPos) : SgaObjKind::NONE;
    ObjectReleased();
    InitData(true);
}

GalleryTransferable::~GalleryTransferable()
{
}

void GalleryTransferable::InitData( bool bLazy )
{
    switch( meObjectKind )
    {
        case SgaObjKind::SvDraw:
        {
            if( !bLazy )
            {
                if( !mpGraphicObject )
                {
                    Graphic aGraphic;

                    if (mpTheme && mpTheme->GetGraphic(mnObjectPos, aGraphic))
                        mpGraphicObject.reset(new GraphicObject( std::move(aGraphic) ));
                }

                if( !mxModelStream )
                {
                    mxModelStream = SotTempStream::Create( u""_ustr );
                    mxModelStream->SetBufferSize( 16348 );

                    if (!mpTheme || !mpTheme->GetModelStream(mnObjectPos, *mxModelStream))
                        mxModelStream.reset();
                    else
                        mxModelStream->Seek( 0 );
                }
            }
        }
        break;

        case SgaObjKind::Animation:
        case SgaObjKind::Bitmap:
        case SgaObjKind::Inet:
        case SgaObjKind::Sound:
        {
            if( !mpURL )
            {
                mpURL.reset(new INetURLObject);

                if (!mpTheme || !mpTheme->GetURL(mnObjectPos, *mpURL))
                {
                    mpURL.reset();
                }
            }

            if( ( SgaObjKind::Sound != meObjectKind ) && !mpGraphicObject )
            {
                Graphic aGraphic;

                if (mpTheme && mpTheme->GetGraphic(mnObjectPos, aGraphic))
                    mpGraphicObject.reset(new GraphicObject( std::move(aGraphic) ));
            }
        }
        break;

        default:
            OSL_FAIL( "GalleryTransferable::GalleryTransferable: invalid object type" );
        break;
    }
}

void GalleryTransferable::AddSupportedFormats()
{
    if( SgaObjKind::SvDraw == meObjectKind )
    {
        AddFormat( SotClipboardFormatId::DRAWING );
        AddFormat( SotClipboardFormatId::SVXB );
        AddFormat( SotClipboardFormatId::GDIMETAFILE );
        AddFormat( SotClipboardFormatId::BITMAP );
    }
    else
    {
        if( mpURL )
            AddFormat( SotClipboardFormatId::SIMPLE_FILE );

        if( mpGraphicObject )
        {
            AddFormat( SotClipboardFormatId::SVXB );

            if( mpGraphicObject->GetType() == GraphicType::GdiMetafile )
            {
                AddFormat( SotClipboardFormatId::GDIMETAFILE );
                AddFormat( SotClipboardFormatId::BITMAP );
            }
            else
            {
                AddFormat( SotClipboardFormatId::BITMAP );
                AddFormat( SotClipboardFormatId::GDIMETAFILE );
            }
        }
    }
}

bool GalleryTransferable::GetData( const datatransfer::DataFlavor& rFlavor, const OUString& /*rDestDoc*/ )
{
    SotClipboardFormatId nFormat = SotExchange::GetFormat( rFlavor );
    bool        bRet = false;

    InitData( false );

    if( ( SotClipboardFormatId::DRAWING == nFormat ) && ( SgaObjKind::SvDraw == meObjectKind ) )
    {
        bRet = ( mxModelStream && SetObject( mxModelStream.get(), 0, rFlavor ) );
    }
    else if( ( SotClipboardFormatId::SIMPLE_FILE == nFormat ) && mpURL )
    {
        bRet = SetString( mpURL->GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
    }
    else if( ( SotClipboardFormatId::SVXB == nFormat ) && mpGraphicObject )
    {
        bRet = SetGraphic( mpGraphicObject->GetGraphic() );
    }
    else if( ( SotClipboardFormatId::GDIMETAFILE == nFormat ) && mpGraphicObject )
    {
        bRet = SetGDIMetaFile( mpGraphicObject->GetGraphic().GetGDIMetaFile() );
    }
    else if( ( SotClipboardFormatId::BITMAP == nFormat || SotClipboardFormatId::PNG == nFormat)
        && mpGraphicObject )
    {
        bRet = SetBitmapEx( mpGraphicObject->GetGraphic().GetBitmapEx(), rFlavor );
    }

    return bRet;
}

bool GalleryTransferable::WriteObject( SvStream& rOStm, void* pUserObject,
                                           sal_uInt32, const datatransfer::DataFlavor& )
{
    bool bRet = false;

    if( pUserObject )
    {
        rOStm.WriteStream( *static_cast< SotStorageStream* >( pUserObject ) );
        bRet = ( rOStm.GetError() == ERRCODE_NONE );
    }

    return bRet;
}

void GalleryTransferable::DragFinished( sal_Int8 nDropAction )
{
    if (mpTheme)
    {
        mpTheme->SetDragging( false );
        mpTheme->SetDragPos( 0 );
    }
    if ( nDropAction )
    {
        vcl::Window *pFocusWindow = Application::GetFocusWindow();
        if ( pFocusWindow )
            pFocusWindow->GrabFocusToDocument();
    }
}

void GalleryTransferable::ObjectReleased()
{
    mxModelStream.reset();
    mpGraphicObject.reset();
    mpURL.reset();
}

bool GalleryTransferable::StartDrag()
{
    INetURLObject aURL;
    if (mpTheme && mpTheme->GetURL(mnObjectPos, aURL) && aURL.GetProtocol() != INetProtocol::NotValid)
    {
        mpTheme->SetDragging( true );
        mpTheme->SetDragPos( mnObjectPos );
        return false;
    }
    return true;
}

INetURLObject ImplGetURL(const GalleryObject* pObject)
{
    INetURLObject aURL;

    if (pObject)
        aURL = *pObject->m_oStorageUrl;

    return aURL;
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
