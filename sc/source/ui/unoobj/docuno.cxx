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

#include <config_feature_opencl.h>

#include <scitems.hxx>

#include <comphelper/dispatchcommand.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/sequence.hxx>
#include <editeng/brushitem.hxx>
#include <editeng/editview.hxx>
#include <editeng/memberids.h>
#include <editeng/outliner.hxx>
#include <editeng/lrspitem.hxx>
#include <editeng/ulspitem.hxx>
#include <editeng/sizeitem.hxx>
#include <o3tl/any.hxx>
#include <o3tl/safeint.hxx>
#include <svx/fmview.hxx>
#include <svx/svditer.hxx>
#include <svx/svdpage.hxx>
#include <svx/svxids.hrc>

#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <officecfg/Office/Common.hxx>
#include <officecfg/Office/Calc.hxx>
#include <svl/numuno.hxx>
#include <svl/hint.hxx>
#include <unotools/moduleoptions.hxx>
#include <sfx2/bindings.hxx>
#include <sfx2/dispatch.hxx>
#include <sfx2/viewfrm.hxx>
#include <svx/unopage.hxx>
#include <vcl/pdfextoutdevdata.hxx>
#include <vcl/print.hxx>
#include <vcl/svapp.hxx>
#include <tools/json_writer.hxx>
#include <tools/multisel.hxx>
#include <tools/UnitConversion.hxx>
#include <toolkit/awt/vclxdevice.hxx>

#include <float.h>

#include <com/sun/star/beans/PropertyAttribute.hpp>
#include <com/sun/star/util/Date.hpp>
#include <com/sun/star/util/XTheme.hpp>
#include <com/sun/star/sheet/XNamedRanges.hpp>
#include <com/sun/star/sheet/XLabelRanges.hpp>
#include <com/sun/star/sheet/XSelectedSheetsSupplier.hpp>
#include <com/sun/star/sheet/XUnnamedDatabaseRanges.hpp>
#include <com/sun/star/i18n/XForbiddenCharacters.hpp>
#include <com/sun/star/script/XLibraryContainer.hpp>
#include <com/sun/star/lang/XInitialization.hpp>
#include <com/sun/star/lang/ServiceNotRegisteredException.hpp>
#include <com/sun/star/document/XDocumentEventBroadcaster.hpp>
#include <com/sun/star/script/XInvocation.hpp>
#include <com/sun/star/script/vba/XVBAEventProcessor.hpp>
#include <com/sun/star/beans/XFastPropertySet.hpp>
#include <comphelper/indexedpropertyvalues.hxx>
#include <comphelper/lok.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/profilezone.hxx>
#include <comphelper/servicehelper.hxx>
#include <comphelper/string.hxx>
#include <cppuhelper/queryinterface.hxx>
#include <cppuhelper/supportsservice.hxx>
#if HAVE_FEATURE_OPENCL
#include <opencl/platforminfo.hxx>
#endif
#include <sfx2/lokhelper.hxx>
#include <sfx2/lokcomponenthelpers.hxx>
#include <sfx2/LokControlHandler.hxx>
#include <docmodel/uno/UnoTheme.hxx>
#include <docmodel/theme/Theme.hxx>

#include <cellsuno.hxx>
#include <columnspanset.hxx>
#include <convuno.hxx>
#include <datauno.hxx>
#include <docfunc.hxx>
#include <docoptio.hxx>
#include <docsh.hxx>
#include <docuno.hxx>
#include <drwlayer.hxx>
#include <forbiuno.hxx>
#include <formulagroup.hxx>
#include <gridwin.hxx>
#include <hints.hxx>
#include <inputhdl.hxx>
#include <inputopt.hxx>
#include <interpre.hxx>
#include <linkuno.hxx>
#include <markdata.hxx>
#include <miscuno.hxx>
#include <nameuno.hxx>
#include <notesuno.hxx>
#include <optuno.hxx>
#include <pfuncache.hxx>
#include <postit.hxx>
#include <printfun.hxx>
#include <rangeutl.hxx>
#include <scmod.hxx>
#include <scresid.hxx>
#include <servuno.hxx>
#include <shapeuno.hxx>
#include <sheetevents.hxx>
#include <styleuno.hxx>
#include <tabvwsh.hxx>
#include <targuno.hxx>
#include <unonames.hxx>
#include <ViewSettingsSequenceDefines.hxx>
#include <editsh.hxx>
#include <drawsh.hxx>
#include <drtxtob.hxx>
#include <transobj.hxx>
#include <chgtrack.hxx>
#include <table.hxx>
#include <appoptio.hxx>
#include <formulaopt.hxx>
#include <output.hxx>
#include <stlpool.hxx>

#include <strings.hrc>

using namespace com::sun::star;

// #i111553# provides the name of the VBA constant for this document type (e.g. 'ThisExcelDoc' for Calc)
constexpr OUString SC_UNO_VBAGLOBNAME = u"VBAGlobalConstantName"_ustr;

//  no Which-ID here, map only for PropertySetInfo

//! rename this, those are no longer only options
static std::span<const SfxItemPropertyMapEntry> lcl_GetDocOptPropertyMap()
{
    static const SfxItemPropertyMapEntry aDocOptPropertyMap_Impl[] =
    {
        { SC_UNO_APPLYFMDES,              0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_AREALINKS,               0, cppu::UnoType<sheet::XAreaLinks>::get(),               0, 0},
        { SC_UNO_AUTOCONTFOC,             0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_BASICLIBRARIES,          0, cppu::UnoType<script::XLibraryContainer>::get(),     beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_DIALOGLIBRARIES,         0, cppu::UnoType<script::XLibraryContainer>::get(),     beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_VBAGLOBNAME,             0, cppu::UnoType<OUString>::get(),                  beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_CALCASSHOWN,             PROP_UNO_CALCASSHOWN, cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNONAME_CLOCAL,              0, cppu::UnoType<lang::Locale>::get(),                                    0, 0},
        { SC_UNO_CJK_CLOCAL,              0, cppu::UnoType<lang::Locale>::get(),                                    0, 0},
        { SC_UNO_CTL_CLOCAL,              0, cppu::UnoType<lang::Locale>::get(),                                    0, 0},
        { SC_UNO_COLLABELRNG,             0, cppu::UnoType<sheet::XLabelRanges>::get(),             0, 0},
        { SC_UNO_DDELINKS,                0, cppu::UnoType<container::XNameAccess>::get(),          0, 0},
        { SC_UNO_DEFTABSTOP,              PROP_UNO_DEFTABSTOP, cppu::UnoType<sal_Int16>::get(),                     0, 0},
        { SC_UNO_EXTERNALDOCLINKS,        0, cppu::UnoType<sheet::XExternalDocLinks>::get(),        0, 0},
        { SC_UNO_FORBIDDEN,               0, cppu::UnoType<i18n::XForbiddenCharacters>::get(),      beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_HASDRAWPAGES,            0, cppu::UnoType<bool>::get(),                                             beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_IGNORECASE,              PROP_UNO_IGNORECASE, cppu::UnoType<bool>::get(),                           0, 0},
        { SC_UNO_ITERENABLED,             PROP_UNO_ITERENABLED, cppu::UnoType<bool>::get(),                          0, 0},
        { SC_UNO_ITERCOUNT,               PROP_UNO_ITERCOUNT, cppu::UnoType<sal_Int32>::get(),                      0, 0},
        { SC_UNO_ITEREPSILON,             PROP_UNO_ITEREPSILON, cppu::UnoType<double>::get(),                       0, 0},
        { SC_UNO_LOOKUPLABELS,            PROP_UNO_LOOKUPLABELS, cppu::UnoType<bool>::get(),                         0, 0},
        { SC_UNO_MATCHWHOLE,              PROP_UNO_MATCHWHOLE, cppu::UnoType<bool>::get(),                           0, 0},
        { SC_UNO_NAMEDRANGES,             0, cppu::UnoType<sheet::XNamedRanges>::get(),             0, 0},
        { SC_UNO_THEME,                   0, cppu::UnoType<util::XTheme>::get(), 0,  0},
        { SC_UNO_DATABASERNG,             0, cppu::UnoType<sheet::XDatabaseRanges>::get(),          0, 0},
        { SC_UNO_NULLDATE,                PROP_UNO_NULLDATE, cppu::UnoType<util::Date>::get(),                      0, 0},
        { SC_UNO_ROWLABELRNG,             0, cppu::UnoType<sheet::XLabelRanges>::get(),             0, 0},
        { SC_UNO_SHEETLINKS,              0, cppu::UnoType<container::XNameAccess>::get(),          0, 0},
        { SC_UNO_SPELLONLINE,             0, cppu::UnoType<bool>::get(),                            0, 0},
        { SC_UNO_STANDARDDEC,             PROP_UNO_STANDARDDEC, cppu::UnoType<sal_Int16>::get(),                    0, 0},
        { SC_UNO_REGEXENABLED,            PROP_UNO_REGEXENABLED, cppu::UnoType<bool>::get(),                         0, 0},
        { SC_UNO_WILDCARDSENABLED,        PROP_UNO_WILDCARDSENABLED, cppu::UnoType<bool>::get(),                         0, 0},
        { SC_UNO_RUNTIMEUID,              0, cppu::UnoType<OUString>::get(),                  beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_HASVALIDSIGNATURES,      0, cppu::UnoType<bool>::get(),                                             beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_ALLOWLINKUPDATE,         0, cppu::UnoType<bool>::get(),                                             beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_ISLOADED,                0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_ISUNDOENABLED,           0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_RECORDCHANGES,           0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_ISRECORDCHANGESPROTECTED,0, cppu::UnoType<bool>::get(),            beans::PropertyAttribute::READONLY, 0},
        { SC_UNO_ISADJUSTHEIGHTENABLED,   0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_ISEXECUTELINKENABLED,    0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_ISCHANGEREADONLYENABLED, 0, cppu::UnoType<bool>::get(),                                             0, 0},
        { SC_UNO_REFERENCEDEVICE,         0, cppu::UnoType<awt::XDevice>::get(),                    beans::PropertyAttribute::READONLY, 0},
        {u"BuildId"_ustr,                      0, ::cppu::UnoType<OUString>::get(),                0, 0},
        { SC_UNO_CODENAME,                0, cppu::UnoType<OUString>::get(),                  0, 0},
        { SC_UNO_INTEROPGRABBAG,          0, cppu::UnoType<uno::Sequence< beans::PropertyValue >>::get(), 0, 0},
    };
    return aDocOptPropertyMap_Impl;
}

//! StandardDecimals as property and from NumberFormatter ????????

static std::span<const SfxItemPropertyMapEntry> lcl_GetColumnsPropertyMap()
{
    static const SfxItemPropertyMapEntry aColumnsPropertyMap_Impl[] =
    {
        { SC_UNONAME_MANPAGE,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_NEWPAGE,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_CELLVIS,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_OWIDTH,   0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_CELLWID,  0,  cppu::UnoType<sal_Int32>::get(),    0, 0 },
    };
    return aColumnsPropertyMap_Impl;
}

static std::span<const SfxItemPropertyMapEntry> lcl_GetRowsPropertyMap()
{
    static const SfxItemPropertyMapEntry aRowsPropertyMap_Impl[] =
    {
        { SC_UNONAME_CELLHGT,  0,  cppu::UnoType<sal_Int32>::get(),    0, 0 },
        { SC_UNONAME_CELLFILT, 0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_OHEIGHT,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_MANPAGE,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_NEWPAGE,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_CELLVIS,  0,  cppu::UnoType<bool>::get(),          0, 0 },
        { SC_UNONAME_CELLBACK, ATTR_BACKGROUND, ::cppu::UnoType<sal_Int32>::get(), 0, MID_BACK_COLOR },
        { SC_UNONAME_CELLTRAN, ATTR_BACKGROUND, cppu::UnoType<bool>::get(), 0, MID_GRAPHIC_TRANSPARENT },
        // not sorted, not used with SfxItemPropertyMapEntry::GetByName
    };
    return aRowsPropertyMap_Impl;
}

constexpr OUString SCMODELOBJ_SERVICE = u"com.sun.star.sheet.SpreadsheetDocument"_ustr;
constexpr OUString SCDOCSETTINGS_SERVICE = u"com.sun.star.sheet.SpreadsheetDocumentSettings"_ustr;
constexpr OUString SCDOC_SERVICE = u"com.sun.star.document.OfficeDocument"_ustr;

SC_SIMPLE_SERVICE_INFO( ScAnnotationsObj, u"ScAnnotationsObj"_ustr, u"com.sun.star.sheet.CellAnnotations"_ustr )
SC_SIMPLE_SERVICE_INFO( ScDrawPagesObj, u"ScDrawPagesObj"_ustr, u"com.sun.star.drawing.DrawPages"_ustr )
SC_SIMPLE_SERVICE_INFO( ScScenariosObj, u"ScScenariosObj"_ustr, u"com.sun.star.sheet.Scenarios"_ustr )
SC_SIMPLE_SERVICE_INFO( ScSpreadsheetSettingsObj, u"ScSpreadsheetSettingsObj"_ustr, u"com.sun.star.sheet.SpreadsheetDocumentSettings"_ustr )
SC_SIMPLE_SERVICE_INFO( ScTableColumnsObj, u"ScTableColumnsObj"_ustr, u"com.sun.star.table.TableColumns"_ustr )
SC_SIMPLE_SERVICE_INFO( ScTableRowsObj, u"ScTableRowsObj"_ustr, u"com.sun.star.table.TableRows"_ustr )
SC_SIMPLE_SERVICE_INFO( ScTableSheetsObj, u"ScTableSheetsObj"_ustr, u"com.sun.star.sheet.Spreadsheets"_ustr )

class ScPrintUIOptions : public vcl::PrinterOptionsHelper
{
public:
    ScPrintUIOptions();
    void SetDefaults();
};

ScPrintUIOptions::ScPrintUIOptions()
{
    const ScPrintOptions& rPrintOpt = ScModule::get()->GetPrintOptions();
    sal_Int32 nContent = rPrintOpt.GetAllSheets() ? 0 : 1;
    bool bSuppress = rPrintOpt.GetSkipEmpty();

    sal_Int32 nNumProps= 10, nIdx = 0;

    m_aUIProperties.resize(nNumProps);

    // load the writer PrinterOptions into the custom tab
    m_aUIProperties[nIdx].Name = "OptionsUIFile";
    m_aUIProperties[nIdx++].Value <<= u"modules/scalc/ui/printeroptions.ui"_ustr;

    // create Section for spreadsheet (results in an extra tab page in dialog)
    SvtModuleOptions aOpt;
    OUString aAppGroupname( ScResId( SCSTR_PRINTOPT_PRODNAME ) );
    aAppGroupname = aAppGroupname.replaceFirst( "%s", aOpt.GetModuleName( SvtModuleOptions::EModule::CALC ) );
    m_aUIProperties[nIdx++].Value = setGroupControlOpt(u"tabcontrol-page2"_ustr, aAppGroupname, OUString());

    // show subgroup for pages
    m_aUIProperties[nIdx++].Value = setSubgroupControlOpt(u"pages"_ustr, ScResId( SCSTR_PRINTOPT_PAGES ), OUString());

    // create a bool option for empty pages
    m_aUIProperties[nIdx++].Value = setBoolControlOpt(u"suppressemptypages"_ustr, ScResId( SCSTR_PRINTOPT_SUPPRESSEMPTY ),
                                                  u".HelpID:vcl:PrintDialog:IsSuppressEmptyPages:CheckBox"_ustr,
                                                  u"IsSuppressEmptyPages"_ustr,
                                                  bSuppress);
    // show Subgroup for print content
    vcl::PrinterOptionsHelper::UIControlOptions aPrintRangeOpt;
    aPrintRangeOpt.maGroupHint = "PrintRange";
    m_aUIProperties[nIdx++].Value = setSubgroupControlOpt(u"printrange"_ustr, ScResId( SCSTR_PRINTOPT_PAGES ),
                                                      OUString(),
                                                      aPrintRangeOpt);

    // create a choice for the content to create
    uno::Sequence< OUString > aChoices{
        ScResId( SCSTR_PRINTOPT_ALLSHEETS ),
        ScResId( SCSTR_PRINTOPT_SELECTEDSHEETS ),
        ScResId( SCSTR_PRINTOPT_SELECTEDCELLS )};
    uno::Sequence< OUString > aHelpIds{
        u".HelpID:vcl:PrintDialog:PrintContent:ListBox"_ustr};
    m_aUIProperties[nIdx++].Value = setChoiceListControlOpt( u"printextrabox"_ustr, OUString(),
                                                    aHelpIds, u"PrintContent"_ustr,
                                                    aChoices, nContent );

    // show Subgroup for print range
    aPrintRangeOpt.mbInternalOnly = true;
    m_aUIProperties[nIdx++].Value = setSubgroupControlOpt(u"fromwhich"_ustr, ScResId( SCSTR_PRINTOPT_FROMWHICH ),
                                                      OUString(),
                                                      aPrintRangeOpt);

    // create a choice for the range to print
    OUString aPrintRangeName( u"PrintRange"_ustr );
    aChoices = { ScResId( SCSTR_PRINTOPT_PRINTALLPAGES ), ScResId( SCSTR_PRINTOPT_PRINTPAGES ) };
    aHelpIds = { u".HelpID:vcl:PrintDialog:PrintRange:RadioButton:0"_ustr,
                 u".HelpID:vcl:PrintDialog:PrintRange:RadioButton:1"_ustr };
    uno::Sequence< OUString > aWidgetIds{ u"rbAllPages"_ustr, u"rbRangePages"_ustr };
    m_aUIProperties[nIdx++].Value = setChoiceRadiosControlOpt(aWidgetIds, OUString(),
                                                    aHelpIds,
                                                    aPrintRangeName,
                                                    aChoices,
                                                    0 );

    // create an Edit dependent on "Pages" selected
    vcl::PrinterOptionsHelper::UIControlOptions aPageRangeOpt( aPrintRangeName, 1, true );
    m_aUIProperties[nIdx++].Value = setEditControlOpt(u"pagerange"_ustr, OUString(),
                                                      u".HelpID:vcl:PrintDialog:PageRange:Edit"_ustr,
                                                      u"PageRange"_ustr, OUString(), aPageRangeOpt);

    vcl::PrinterOptionsHelper::UIControlOptions aEvenOddOpt(aPrintRangeName, 0, true);
    m_aUIProperties[ nIdx++ ].Value = setChoiceListControlOpt(u"evenoddbox"_ustr,
                                                           OUString(),
                                                           uno::Sequence<OUString>(),
                                                           u"EvenOdd"_ustr,
                                                           uno::Sequence<OUString>(),
                                                           0,
                                                           uno::Sequence< sal_Bool >(),
                                                           aEvenOddOpt);

    assert(nIdx == nNumProps);
}

void ScPrintUIOptions::SetDefaults()
{
    // re-initialize the default values from print options

    const ScPrintOptions& rPrintOpt = ScModule::get()->GetPrintOptions();
    sal_Int32 nContent = rPrintOpt.GetAllSheets() ? 0 : 1;
    bool bSuppress = rPrintOpt.GetSkipEmpty();

    for (beans::PropertyValue & rPropValue : m_aUIProperties)
    {
        uno::Sequence<beans::PropertyValue> aUIProp;
        if ( rPropValue.Value >>= aUIProp )
        {
            for (auto& rProp : asNonConstRange(aUIProp))
            {
                OUString aName = rProp.Name;
                if ( aName == "Property" )
                {
                    beans::PropertyValue aPropertyValue;
                    if ( rProp.Value >>= aPropertyValue )
                    {
                        if ( aPropertyValue.Name == "PrintContent" )
                        {
                            aPropertyValue.Value <<= nContent;
                            rProp.Value <<= aPropertyValue;
                        }
                        else if ( aPropertyValue.Name == "IsSuppressEmptyPages" )
                        {
                            aPropertyValue.Value <<= bSuppress;
                            rProp.Value <<= aPropertyValue;
                        }
                    }
                }
            }
            rPropValue.Value <<= aUIProp;
        }
    }
}

void ScModelObj::CreateAndSet(ScDocShell* pDocSh)
{
    if (pDocSh)
        pDocSh->SetBaseModel( new ScModelObj(pDocSh) );
}

SdrModel& ScModelObj::getSdrModelFromUnoModel() const
{
    ScDocument& rDoc(pDocShell->GetDocument());

    if(!rDoc.GetDrawLayer())
    {
        rDoc.InitDrawLayer();
    }

    return *rDoc.GetDrawLayer(); // TTTT should be reference
}

ScModelObj::ScModelObj( ScDocShell* pDocSh ) :
    SfxBaseModel( pDocSh ),
    aPropSet( lcl_GetDocOptPropertyMap() ),
    pDocShell( pDocSh ),
    maChangesListeners( m_aMutex )
{
    // pDocShell may be NULL if this is the base of a ScDocOptionsObj
    if ( pDocShell )
    {
        pDocShell->GetDocument().AddUnoObject(*this);      // SfxModel is derived from SfxListener
    }
}

ScModelObj::~ScModelObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);

    if (xNumberAgg.is())
        xNumberAgg->setDelegator(uno::Reference<uno::XInterface>());

    pPrintFuncCache.reset();
    pPrinterOptions.reset();
}

uno::Reference< uno::XAggregation> const & ScModelObj::GetFormatter()
{
    // pDocShell may be NULL if this is the base of a ScDocOptionsObj
    if ( !xNumberAgg.is() && pDocShell )
    {
        // setDelegator changes RefCount, so we'd better hold the reference ourselves
        // (directly in m_refCount, so we don't delete ourselves with release())
        osl_atomic_increment( &m_refCount );
        // we need a reference to SvNumberFormatsSupplierObj during queryInterface,
        // otherwise it'll be deleted
        uno::Reference<util::XNumberFormatsSupplier> xFormatter(
            new SvNumberFormatsSupplierObj(pDocShell->GetDocument().GetThreadedContext().GetFormatTable() ));
        {
            xNumberAgg.set(uno::Reference<uno::XAggregation>( xFormatter, uno::UNO_QUERY ));
            // extra block to force deletion of the temporary before setDelegator
        }

        // during setDelegator no additional reference should exist
        xFormatter = nullptr;

        if (xNumberAgg.is())
            xNumberAgg->setDelegator( getXWeak() );
        osl_atomic_decrement( &m_refCount );
    } // if ( !xNumberAgg.is() )
    return xNumberAgg;
}

ScDocument* ScModelObj::GetDocument() const
{
    if (pDocShell)
        return &pDocShell->GetDocument();
    return nullptr;
}

SfxObjectShell* ScModelObj::GetEmbeddedObject() const
{
    return pDocShell;
}

void ScModelObj::UpdateAllRowHeights()
{
    if (pDocShell)
        pDocShell->UpdateAllRowHeights();
}

void ScModelObj::BeforeXMLLoading()
{
    if (pDocShell)
        pDocShell->BeforeXMLLoading();
}

void ScModelObj::AfterXMLLoading()
{
    if (pDocShell)
        pDocShell->AfterXMLLoading(true);
}

ScSheetSaveData* ScModelObj::GetSheetSaveData()
{
    if (pDocShell)
        return pDocShell->GetSheetSaveData();
    return nullptr;
}

ScFormatSaveData* ScModelObj::GetFormatSaveData()
{
    if (pDocShell)
        return pDocShell->GetFormatSaveData();
    return nullptr;
}

void ScModelObj::RepaintRange( const ScRange& rRange )
{
    if (pDocShell)
        pDocShell->PostPaint( rRange, PaintPartFlags::Grid );
}

void ScModelObj::RepaintRange( const ScRangeList& rRange )
{
    if (pDocShell)
        pDocShell->PostPaint(rRange, PaintPartFlags::Grid, SC_PF_TESTMERGE);
}

static OString getTabViewRenderState(const ScTabViewShell& rTabViewShell)
{
    OStringBuffer aState;
    const ScViewRenderingOptions& rViewRenderingOptions = rTabViewShell.GetViewRenderingData();

    if (rTabViewShell.IsAutoSpell())
        aState.append('S');
    if (rViewRenderingOptions.GetDocColor() == svtools::ColorConfig::GetDefaultColor(svtools::DOCCOLOR, 1))
        aState.append('D');

    aState.append(';');

    OString aThemeName = OUStringToOString(rViewRenderingOptions.GetColorSchemeName(), RTL_TEXTENCODING_UTF8);
    aState.append(aThemeName);

    return aState.makeStringAndClear();
}

static ScViewData* lcl_getViewMatchingDocZoomTab(const Fraction& rZoomX,
                                              const Fraction& rZoomY,
                                              const SCTAB nTab,
                                              const ViewShellDocId& rDocId,
                                              std::string_view rViewRenderState)
{
    constexpr size_t nMaxIter = 5;
    size_t nIter = 0;
    for (SfxViewShell* pViewShell = SfxViewShell::GetFirst();
            pViewShell && nIter < nMaxIter;
            (pViewShell = SfxViewShell::GetNext(*pViewShell)), ++nIter)
    {
        if (pViewShell->GetDocId() != rDocId)
            continue;

        ScTabViewShell* pTabViewShell = dynamic_cast<ScTabViewShell*>(pViewShell);
        if (!pTabViewShell)
            continue;

        ScViewData& rData = pTabViewShell->GetViewData();
        if (rData.GetTabNo() == nTab && rData.GetZoomX() == rZoomX && rData.GetZoomY() == rZoomY &&
            getTabViewRenderState(*pTabViewShell) == rViewRenderState)
        {
            return &rData;
        }
    }

    return nullptr;
}

void ScModelObj::paintTile( VirtualDevice& rDevice,
                            int nOutputWidth, int nOutputHeight,
                            int nTilePosX, int nTilePosY,
                            tools::Long nTileWidth, tools::Long nTileHeight )
{
    ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false);

    // FIXME: Can this happen? What should we do?
    if (!pViewShell)
        return;

    ScViewData* pActiveViewData = &pViewShell->GetViewData();
    Fraction aFracX(o3tl::toTwips(nOutputWidth, o3tl::Length::px), nTileWidth);
    Fraction aFracY(o3tl::toTwips(nOutputHeight, o3tl::Length::px), nTileHeight);

    // Try to find a view that matches the tile-zoom requested by iterating over
    // first few shells. This is to avoid switching of zooms in ScGridWindow::PaintTile
    // and hence avoid grid-offset recomputation on all shapes which is not cheap.
    ScViewData* pViewData = lcl_getViewMatchingDocZoomTab(aFracX, aFracY,
            pActiveViewData->GetTabNo(), pViewShell->GetDocId(),
            getTabViewRenderState(*pViewShell));
    if (!pViewData)
        pViewData = pActiveViewData;

    ScGridWindow* pGridWindow = pViewData->GetActiveWin();

    // update the size of the area we are painting
    // FIXME we want to use only the minimal necessary size, like the
    // following; but for the moment there is too many problems with that and
    // interaction with editeng used for the cell editing
    //Size aTileSize(nOutputWidth, nOutputHeight);
    //if (pGridWindow->GetOutputSizePixel() != aTileSize)
    //    pGridWindow->SetOutputSizePixel(Size(nOutputWidth, nOutputHeight));
    // so instead for now, set the viewport size to document size

    // Fetch the document size and the tiled rendering area together,
    // because the tiled rendering area is not cheap to compute, and we want
    // to pass it down to ScGridWindow::PaintFile to avoid computing twice.
    SCCOL nTiledRenderingAreaEndCol = 0;
    SCROW nTiledRenderingAreaEndRow = 0;
    Size aDocSize = getDocumentSize(nTiledRenderingAreaEndCol, nTiledRenderingAreaEndRow);

    pGridWindow->SetOutputSizePixel(Size(aDocSize.Width() * pViewData->GetPPTX(), aDocSize.Height() * pViewData->GetPPTY()));

    pGridWindow->PaintTile( rDevice, nOutputWidth, nOutputHeight,
                            nTilePosX, nTilePosY, nTileWidth, nTileHeight,
                            nTiledRenderingAreaEndCol, nTiledRenderingAreaEndRow );

    // Draw Form controls
    ScDrawLayer* pDrawLayer = pDocShell->GetDocument().GetDrawLayer();
    SdrPage* pPage = pDrawLayer->GetPage(sal_uInt16(pViewData->GetTabNo()));
    SdrView* pDrawView = pViewData->GetViewShell()->GetScDrawView();
    tools::Rectangle aTileRect(Point(nTilePosX, nTilePosY), Size(nTileWidth, nTileHeight));
    Size aOutputSize(nOutputWidth, nOutputHeight);
    LokControlHandler::paintControlTile(pPage, pDrawView, *pGridWindow, rDevice, aOutputSize, aTileRect);
}

void ScModelObj::setPart( int nPart, bool /*bAllowChangeFocus*/ )
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScTabView* pTabView = pViewData->GetView();
    if (!pTabView)
        return;

    if (SdrView* pDrawView = pViewData->GetViewShell()->GetScDrawView())
        pDrawView->SetNegativeX(comphelper::LibreOfficeKit::isActive() &&
            pViewData->GetDocument().IsLayoutRTL(nPart));

    pTabView->SelectTabPage(nPart + 1);
}

int ScModelObj::getParts()
{
    ScDocument& rDoc = pDocShell->GetDocument();
    return rDoc.GetTableCount();
}

int ScModelObj::getPart()
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    return pViewData ? pViewData->GetViewShell()->getPart() : 0;
}

OUString ScModelObj::getPartInfo( int nPart )
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return OUString();

    const bool bIsVisible = pViewData->GetDocument().IsVisible(nPart);
    const bool bIsProtected = pViewData->GetDocument().IsTabProtected(nPart);
    //FIXME: Implement IsSelected().
    const bool bIsSelected = false; //pViewData->GetDocument()->IsSelected(nPart);
    const bool bIsRTLLayout = pViewData->GetDocument().IsLayoutRTL(nPart);

    ::tools::JsonWriter jsonWriter;
    jsonWriter.put("visible", static_cast<unsigned int>(bIsVisible));
    jsonWriter.put("rtllayout", static_cast<unsigned int>(bIsRTLLayout));
    jsonWriter.put("protected", static_cast<unsigned int>(bIsProtected));
    jsonWriter.put("selected", static_cast<unsigned int>(bIsSelected));

    OUString tabName;
    pViewData->GetDocument().GetName(nPart, tabName);
    jsonWriter.put("name", tabName);

    sal_Int64 hashCode;
    pViewData->GetDocument().GetHashCode(nPart, hashCode);
    jsonWriter.put("hash", hashCode);

    Size lastColRow = getDataArea(nPart);
    jsonWriter.put("lastcolumn", lastColRow.getWidth());
    jsonWriter.put("lastrow", lastColRow.getHeight());

    return OStringToOUString(jsonWriter.finishAndGetAsOString(), RTL_TEXTENCODING_UTF8);
}

OUString ScModelObj::getPartName( int nPart )
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return OUString();

    OUString sTabName;
    pViewData->GetDocument().GetName(nPart, sTabName);
    return sTabName;
}

OUString ScModelObj::getPartHash( int nPart )
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return OUString();

    sal_Int64 nHashCode;
    return (pViewData->GetDocument().GetHashCode(nPart, nHashCode) ? OUString::number(nHashCode) : OUString());
}

VclPtr<vcl::Window> ScModelObj::getDocWindow()
{
    SolarMutexGuard aGuard;

    ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false);

    // FIXME: Can this happen? What should we do?
    if (!pViewShell)
        return VclPtr<vcl::Window>();

    if (VclPtr<vcl::Window> pWindow = SfxLokHelper::getInPlaceDocWindow(pViewShell))
        return pWindow;

    return pViewShell->GetViewData().GetActiveWin();
}

Size ScModelObj::getDocumentSize()
{
    SCCOL nTiledRenderingAreaEndCol = 0;
    SCROW nTiledRenderingAreaEndRow = 0;
    return getDocumentSize(nTiledRenderingAreaEndCol, nTiledRenderingAreaEndRow);
}

Size ScModelObj::getDocumentSize(SCCOL& rnTiledRenderingAreaEndCol, SCROW& rnTiledRenderingAreaEndRow)
{
    Size aSize(10, 10); // minimum size

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return aSize;

    SCTAB nTab = pViewData->GetTabNo();
    rnTiledRenderingAreaEndCol = 0;
    rnTiledRenderingAreaEndRow = 0;
    const ScDocument& rDoc = pDocShell->GetDocument();

    rDoc.GetTiledRenderingArea(nTab, rnTiledRenderingAreaEndCol, rnTiledRenderingAreaEndRow);

    const ScDocument* pThisDoc = &rDoc;
    const double fPPTX = pViewData->GetPPTX();
    const double fPPTY = pViewData->GetPPTY();

    auto GetColWidthPx = [pThisDoc, fPPTX, nTab](SCCOL nCol) {
        const sal_uInt16 nSize = pThisDoc->GetColWidth(nCol, nTab);
        return ScViewData::ToPixel(nSize, fPPTX);
    };

    tools::Long nDocWidthPixel = pViewData->GetLOKWidthHelper().computePosition(rnTiledRenderingAreaEndCol, GetColWidthPx);
    tools::Long nDocHeightPixel = pThisDoc->GetScaledRowHeight(0, rnTiledRenderingAreaEndRow, nTab, fPPTY);

    if (nDocWidthPixel > 0 && nDocHeightPixel > 0)
    {
        // convert to twips
        aSize.setWidth(nDocWidthPixel / fPPTX);
        aSize.setHeight(nDocHeightPixel / fPPTY);
    }
    else
    {
        // convert to twips
        aSize.setWidth(rDoc.GetColWidth(0, rnTiledRenderingAreaEndCol, nTab));
        aSize.setHeight(rDoc.GetRowHeight(0, rnTiledRenderingAreaEndRow, nTab));
    }

    return aSize;
}

Size ScModelObj::getDataArea(long nPart)
{
    Size aSize(1, 1);

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData || !pDocShell)
        return aSize;

    SCTAB nTab = nPart;
    SCCOL nEndCol = 0;
    SCROW nEndRow = 0;
    ScDocument& rDoc = pDocShell->GetDocument();

    ScTable* pTab = rDoc.FetchTable(nTab);
    if (!pTab)
        return aSize;

    pTab->GetCellArea(nEndCol, nEndRow);
    aSize = Size(nEndCol, nEndRow);

    return aSize;
}

void ScModelObj::postKeyEvent(int nType, int nCharCode, int nKeyCode)
{
    SolarMutexGuard aGuard;
    SfxLokHelper::postKeyEventAsync(getDocWindow(), nType, nCharCode, nKeyCode);
}

void ScModelObj::postMouseEvent(int nType, int nX, int nY, int nCount, int nButtons, int nModifier)
{
    SolarMutexGuard aGuard;

    ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false);

    // FIXME: Can this happen? What should we do?
    if (!pViewShell)
        return;

    ScViewData* pViewData = &pViewShell->GetViewData();

    ScGridWindow* pGridWindow = pViewData->GetActiveWin();

    if (!pGridWindow)
        return;

    SCTAB nTab = pViewData->GetTabNo();
    const ScDocument& rDoc = pDocShell->GetDocument();
    bool bDrawNegativeX = rDoc.IsNegativePage(nTab);
    if (SfxLokHelper::testInPlaceComponentMouseEventHit(pViewShell, nType, nX, nY, nCount,
                                                        nButtons, nModifier, pViewData->GetPPTX(),
                                                        pViewData->GetPPTY(), bDrawNegativeX))
        return;

    Point aPointTwip(nX, nY);

    // Check if a control is hit
    Point aPointHMM = o3tl::convert(aPointTwip, o3tl::Length::twip, o3tl::Length::mm100);
    Point aPointHMMDraw(bDrawNegativeX ? -aPointHMM.X() : aPointHMM.X(), aPointHMM.Y());
    ScDrawLayer* pDrawLayer = pDocShell->GetDocument().GetDrawLayer();
    SdrPage* pPage = pDrawLayer->GetPage(sal_uInt16(nTab));
    SdrView* pDrawView = pViewData->GetViewShell()->GetScDrawView();
    if (LokControlHandler::postMouseEvent(pPage, pDrawView, *pGridWindow, nType, aPointHMMDraw, nCount, nButtons, nModifier))
        return;

    if (!pGridWindow->HasChildPathFocus(true))
        pGridWindow->GrabFocus();

    // Calc operates in pixels...
    const Point aPosition(nX * pViewData->GetPPTX() + pGridWindow->GetOutOffXPixel(),
                          nY * pViewData->GetPPTY() + pGridWindow->GetOutOffYPixel());

    VclEventId aEvent = VclEventId::NONE;
    MouseEvent aData(aPosition, nCount, MouseEventModifiers::SIMPLECLICK, nButtons, nModifier);
    aData.setLogicPosition(aPointHMM);
    switch (nType)
    {
        case LOK_MOUSEEVENT_MOUSEBUTTONDOWN:
            aEvent = VclEventId::WindowMouseButtonDown;
            break;
        case LOK_MOUSEEVENT_MOUSEBUTTONUP:
            aEvent = VclEventId::WindowMouseButtonUp;
            break;
        case LOK_MOUSEEVENT_MOUSEMOVE:
            aEvent = VclEventId::WindowMouseMove;
            break;
        default:
            break;
    }

    Application::LOKHandleMouseEvent(aEvent, pGridWindow, &aData);
}

void ScModelObj::setTextSelection(int nType, int nX, int nY)
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScTabViewShell* pViewShell = pViewData->GetViewShell();

    LokChartHelper aChartHelper(pViewShell);
    if (aChartHelper.setTextSelection(nType, nX, nY))
        return;

    ScInputHandler* pInputHandler = ScModule::get()->GetInputHdl(pViewShell);
    ScDrawView* pDrawView = pViewData->GetScDrawView();

    bool bHandled = false;

    if (pInputHandler && pInputHandler->IsInputMode())
    {
        // forwarding to editeng - we are editing the cell content
        EditView* pTableView = pInputHandler->GetTableView();
        assert(pTableView);

        Point aPoint(convertTwipToMm100(nX), convertTwipToMm100(nY));

        if (pTableView && pTableView->GetOutputArea().Contains(aPoint))
        {
            switch (nType)
            {
                case LOK_SETTEXTSELECTION_START:
                    pTableView->SetCursorLogicPosition(aPoint, /*bPoint=*/false, /*bClearMark=*/false);
                    break;
                case LOK_SETTEXTSELECTION_END:
                    pTableView->SetCursorLogicPosition(aPoint, /*bPoint=*/true, /*bClearMark=*/false);
                    break;
                case LOK_SETTEXTSELECTION_RESET:
                    pTableView->SetCursorLogicPosition(aPoint, /*bPoint=*/true, /*bClearMark=*/true);
                    break;
                default:
                    assert(false);
                    break;
            }
            bHandled = true;
        }
    }
    else if (pDrawView && pDrawView->IsTextEdit())
    {
        // forwarding to editeng - we are editing the text in shape
        OutlinerView* pOutlinerView = pDrawView->GetTextEditOutlinerView();
        EditView& rEditView = pOutlinerView->GetEditView();

        Point aPoint(convertTwipToMm100(nX), convertTwipToMm100(nY));
        switch (nType)
        {
            case LOK_SETTEXTSELECTION_START:
                rEditView.SetCursorLogicPosition(aPoint, /*bPoint=*/false, /*bClearMark=*/false);
                break;
            case LOK_SETTEXTSELECTION_END:
                rEditView.SetCursorLogicPosition(aPoint, /*bPoint=*/true, /*bClearMark=*/false);
                break;
            case LOK_SETTEXTSELECTION_RESET:
                rEditView.SetCursorLogicPosition(aPoint, /*bPoint=*/true, /*bClearMark=*/true);
                break;
            default:
                assert(false);
                break;
        }
        bHandled = true;
    }

    if (!bHandled)
    {
        // just update the cell selection
        ScGridWindow* pGridWindow = pViewData->GetActiveWin();
        if (!pGridWindow)
            return;

        // move the cell selection handles
        pGridWindow->SetCellSelectionPixel(nType, nX * pViewData->GetPPTX(), nY * pViewData->GetPPTY());
    }
}

uno::Reference<datatransfer::XTransferable> ScModelObj::getSelection()
{
    SolarMutexGuard aGuard;

    TransferableDataHelper aDataHelper;
    uno::Reference<datatransfer::XTransferable> xTransferable;

    if (ScViewData* pViewData = ScDocShell::GetViewData())
    {
        if ( ScEditShell * pShell = dynamic_cast<ScEditShell*>( pViewData->GetViewShell()->GetViewFrame().GetDispatcher()->GetShell(0) ) )
            xTransferable = pShell->GetEditView()->GetTransferable();
        else if ( nullptr != dynamic_cast<ScDrawTextObjectBar*>( pViewData->GetViewShell()->GetViewFrame().GetDispatcher()->GetShell(0) ))
        {
            ScDrawView* pView = pViewData->GetScDrawView();
            OutlinerView* pOutView = pView->GetTextEditOutlinerView();
            if (pOutView)
                xTransferable = pOutView->GetEditView().GetTransferable();
        }
        else if ( ScDrawShell * pDrawShell = dynamic_cast<ScDrawShell*>( pViewData->GetViewShell()->GetViewFrame().GetDispatcher()->GetShell(0) ) )
            xTransferable = pDrawShell->GetDrawView()->CopyToTransferable();
        else
            xTransferable = pViewData->GetViewShell()->CopyToTransferable();
    }

    if (!xTransferable.is())
        xTransferable.set( aDataHelper.GetTransferable() );

    return xTransferable;
}

void ScModelObj::setGraphicSelection(int nType, int nX, int nY)
{
    SolarMutexGuard aGuard;

    ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false);

    // FIXME: Can this happen? What should we do?
    if (!pViewShell)
        return;

    ScViewData* pViewData = &pViewShell->GetViewData();

    ScGridWindow* pGridWindow = pViewData->GetActiveWin();

    double fPPTX = pViewData->GetPPTX();
    double fPPTY = pViewData->GetPPTY();

    pViewShell = pViewData->GetViewShell();
    LokChartHelper aChartHelper(pViewShell);
    if (aChartHelper.setGraphicSelection(nType, nX, nY, fPPTX, fPPTY))
        return;

    int nPixelX = nX * fPPTX;
    int nPixelY = nY * fPPTY;

    switch (nType)
    {
    case LOK_SETGRAPHICSELECTION_START:
        {
            MouseEvent aClickEvent(Point(nPixelX, nPixelY), 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
            pGridWindow->MouseButtonDown(aClickEvent);
            MouseEvent aMoveEvent(Point(nPixelX, nPixelY), 0, MouseEventModifiers::SIMPLEMOVE, MOUSE_LEFT);
            pGridWindow->MouseMove(aMoveEvent);
        }
        break;
    case LOK_SETGRAPHICSELECTION_END:
        {
            MouseEvent aMoveEvent(Point(nPixelX, nPixelY), 0, MouseEventModifiers::SIMPLEMOVE, MOUSE_LEFT);
            pGridWindow->MouseMove(aMoveEvent);
            MouseEvent aClickEvent(Point(nPixelX, nPixelY), 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
            pGridWindow->MouseButtonUp(aClickEvent);
        }
        break;
    default:
        assert(false);
        break;
    }
}

void ScModelObj::resetSelection()
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScTabViewShell* pViewShell = pViewData->GetViewShell();

    // deselect the shapes & texts
    ScDrawView* pDrawView = pViewShell->GetScDrawView();
    if (pDrawView)
    {
        pDrawView->ScEndTextEdit();
        pDrawView->UnmarkAll();
    }
    else
        pViewShell->Unmark();

    // and hide the cell and text selection
    pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_TEXT_SELECTION, ""_ostr);
    SfxLokHelper::notifyOtherViews(pViewShell, LOK_CALLBACK_TEXT_VIEW_SELECTION, "selection", ""_ostr);
}

void ScModelObj::setClipboard(const uno::Reference<datatransfer::clipboard::XClipboard>& xClipboard)
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    pViewData->GetActiveWin()->SetClipboard(xClipboard);
}

bool ScModelObj::isMimeTypeSupported()
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return false;


    TransferableDataHelper aDataHelper(TransferableDataHelper::CreateFromSystemClipboard(pViewData->GetActiveWin()));
    return EditEngine::HasValidData(aDataHelper.GetTransferable());
}

static void lcl_sendLOKDocumentBackground(const ScViewData* pViewData)
{
    ScDocShell* pDocSh = pViewData->GetDocShell();
    ScDocument& rDoc = pDocSh->GetDocument();
    const SfxPoolItem& rItem(rDoc.getCellAttributeHelper().getDefaultCellAttribute().GetItem(ATTR_BACKGROUND));
    const SvxBrushItem& rBackground = static_cast<const SvxBrushItem&>(rItem);
    const Color& rColor = rBackground.GetColor();

    ScTabViewShell* pViewShell = pViewData->GetViewShell();
    pViewShell->libreOfficeKitViewCallback(LOK_CALLBACK_DOCUMENT_BACKGROUND_COLOR, rColor.AsRGBHexString().toUtf8());
}

void ScModelObj::setClientZoom(int nTilePixelWidth_, int nTilePixelHeight_, int nTileTwipWidth_, int nTileTwipHeight_)
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    // Currently in LOK clients the doc background cannot be changed, so send this sparingly as possible but for every view.
    // FIXME: Find a better place to trigger this callback where it would be called just once per view creation.
    // Doing this in ScTabViewShell init code does not work because callbacks do not work at that point for the first view.
    lcl_sendLOKDocumentBackground(pViewData);

    const Fraction newZoomX(o3tl::toTwips(nTilePixelWidth_, o3tl::Length::px), nTileTwipWidth_);
    const Fraction newZoomY(o3tl::toTwips(nTilePixelHeight_, o3tl::Length::px), nTileTwipHeight_);

    double fDeltaPPTX = std::abs(ScGlobal::nScreenPPTX * static_cast<double>(newZoomX) - pViewData->GetPPTX());
    double fDeltaPPTY = std::abs(ScGlobal::nScreenPPTY * static_cast<double>(newZoomY) - pViewData->GetPPTY());
    constexpr double fEps = 1E-08;

    if (pViewData->GetZoomX() == newZoomX && pViewData->GetZoomY() == newZoomY && fDeltaPPTX < fEps && fDeltaPPTY < fEps)
        return;

    pViewData->SetZoom(newZoomX, newZoomY, true);
    if (ScTabViewShell* pViewShell = pViewData->GetViewShell())
        pViewShell->SyncGridWindowMapModeFromDrawMapMode();
    // sync zoom to Input Handler like ScTabViewShell::Activate does
    if (ScInputHandler* pHdl = ScModule::get()->GetInputHdl())
        pHdl->SetRefScale(pViewData->GetZoomX(), pViewData->GetZoomY());

    // refresh our view's take on other view's cursors & selections
    ScGridWindow* pGridWindow = pViewData->GetActiveWin();
    pGridWindow->UpdateEditViewPos();
    pGridWindow->updateKitOtherCursors();
    pGridWindow->updateOtherKitSelections();
    pGridWindow->resetCachedViewGridOffsets();

    if (ScDrawView* pDrawView = pViewData->GetScDrawView())
        pDrawView->resetGridOffsetsForAllSdrPageViews();
}

void ScModelObj::getRowColumnHeaders(const tools::Rectangle& rRectangle, tools::JsonWriter& rJsonWriter)
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScTabView* pTabView = pViewData->GetView();
    if (!pTabView)
        return;

    pTabView->getRowColumnHeaders(rRectangle, rJsonWriter);
}

OString ScModelObj::getSheetGeometryData(bool bColumns, bool bRows, bool bSizes, bool bHidden,
                                         bool bFiltered, bool bGroups)
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return ""_ostr;

    ScTabView* pTabView = pViewData->GetView();
    if (!pTabView)
        return ""_ostr;

    return pTabView->getSheetGeometryData(bColumns, bRows, bSizes, bHidden, bFiltered, bGroups);
}

void ScModelObj::getCellCursor(tools::JsonWriter& rJsonWriter)
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScGridWindow* pGridWindow = pViewData->GetActiveWin();
    if (!pGridWindow)
        return;

    rJsonWriter.put("commandName", ".uno:CellCursor");
    rJsonWriter.put("commandValues", pGridWindow->getCellCursor());
}

PointerStyle ScModelObj::getPointer()
{
    SolarMutexGuard aGuard;

    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return PointerStyle::Arrow;

    ScGridWindow* pGridWindow = pViewData->GetActiveWin();
    if (!pGridWindow)
        return PointerStyle::Arrow;

    return pGridWindow->GetPointer();
}

void ScModelObj::getTrackedChanges(tools::JsonWriter& rJson)
{
    if (pDocShell)
    {
        if (ScChangeTrack* pChangeTrack = pDocShell->GetDocument().GetChangeTrack())
            pChangeTrack->GetChangeTrackInfo(rJson);
    }
}

void ScModelObj::setClientVisibleArea(const tools::Rectangle& rRectangle)
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    // set the PgUp/PgDown offset
    pViewData->ForcePageUpDownOffset(rRectangle.GetHeight());

    // Store the visible area so that we can use at places like shape insertion
    pViewData->setLOKVisibleArea(rRectangle);

    if (comphelper::LibreOfficeKit::isCompatFlagSet(
            comphelper::LibreOfficeKit::Compat::scPrintTwipsMsgs))
    {
        ScTabView* pTabView = pViewData->GetView();
        if (pTabView)
            pTabView->extendTiledAreaIfNeeded();
    }
}

void ScModelObj::setOutlineState(bool bColumn, int nLevel, int nIndex, bool bHidden)
{
    ScViewData* pViewData = ScDocShell::GetViewData();
    if (!pViewData)
        return;

    ScDBFunc* pFunc = pViewData->GetView();

    if (pFunc)
        pFunc->SetOutlineState(bColumn, nLevel, nIndex, bHidden);
}

void ScModelObj::getPostIts(tools::JsonWriter& rJsonWriter)
{
    if (!pDocShell)
        return;

    ScDocument& rDoc = pDocShell->GetDocument();
    std::vector<sc::NoteEntry> aNotes;
    rDoc.GetAllNoteEntries(aNotes);

    auto commentsNode = rJsonWriter.startArray("comments");
    for (const sc::NoteEntry& aNote : aNotes)
    {
        auto commentNode = rJsonWriter.startStruct();

        rJsonWriter.put("id", aNote.mpNote->GetId());
        rJsonWriter.put("tab", aNote.maPos.Tab());
        rJsonWriter.put("author", aNote.mpNote->GetAuthor());
        rJsonWriter.put("dateTime", aNote.mpNote->GetDate());
        rJsonWriter.put("text", aNote.mpNote->GetText());

        // Calculating the cell cursor position
        if (ScViewData* pViewData = ScDocShell::GetViewData())
        {
            ScGridWindow* pGridWindow = pViewData->GetActiveWin();
            if (pGridWindow)
            {
                rJsonWriter.put("cellRange", ScPostIt::NoteRangeToJsonString(rDoc, aNote.maPos));
            }
        }
    }
}

OString ScPostIt::NoteRangeToJsonString(const ScDocument& rDoc, const ScAddress& rPos)
{
    SCCOL nX(rPos.Col());
    SCROW nY(rPos.Row());
    OString aStartCellAddress(OString::number(nX) + " " + OString::number(nY));
    const ScPatternAttr* pMarkPattern = rDoc.GetPattern(nX, nY, rPos.Tab());
    const ScMergeAttr* pMergeItem = nullptr;
    if (pMarkPattern && pMarkPattern->GetItemSet().GetItemState(ATTR_MERGE, false, &pMergeItem) == SfxItemState::SET)
    {
        SCCOL nCol = pMergeItem->GetColMerge();
        if (nCol > 1)
            nX += nCol - 1;
        SCROW nRow = pMergeItem->GetRowMerge();
        if (nRow > 1)
            nY += nRow - 1;
    }
    OString aEndCellAddress(OString::number(nX) + " " + OString::number(nY));
    return aStartCellAddress + " " + aEndCellAddress;
}

void ScModelObj::getPostItsPos(tools::JsonWriter& rJsonWriter)
{
    if (!pDocShell)
        return;

    ScDocument& rDoc = pDocShell->GetDocument();
    std::vector<sc::NoteEntry> aNotes;
    rDoc.GetAllNoteEntries(aNotes);

    auto commentsNode = rJsonWriter.startArray("commentsPos");
    for (const sc::NoteEntry& aNote : aNotes)
    {
        auto commentNode = rJsonWriter.startStruct();

        rJsonWriter.put("id", aNote.mpNote->GetId());
        rJsonWriter.put("tab", aNote.maPos.Tab());

        // Calculating the cell cursor position
        if (ScViewData* pViewData = ScDocShell::GetViewData())
        {
            ScGridWindow* pGridWindow = pViewData->GetActiveWin();
            if (pGridWindow)
            {
                rJsonWriter.put("cellRange", ScPostIt::NoteRangeToJsonString(rDoc, aNote.maPos));
            }
        }
    }
}

void ScModelObj::completeFunction(const OUString& rFunctionName)
{
    if (ScInputHandler* pHdl = ScModule::get()->GetInputHdl())
    {
        assert(!rFunctionName.isEmpty());
        pHdl->LOKPasteFunctionData(rFunctionName);
    }
}

OString ScModelObj::getViewRenderState(SfxViewShell* pViewShell)
{
    ScTabViewShell* pTabViewShell = dynamic_cast<ScTabViewShell*>(pViewShell);
    if (!pTabViewShell)
    {
        ScViewData* pViewData = ScDocShell::GetViewData();
        pTabViewShell = pViewData ? pViewData->GetViewShell() : nullptr;
    }

    if (pTabViewShell)
        return getTabViewRenderState(*pTabViewShell);

    return OString();
}

void ScModelObj::initializeForTiledRendering(const css::uno::Sequence<css::beans::PropertyValue>& rArguments)
{
    SolarMutexGuard aGuard;

    ScModule* mod = ScModule::get();
    // enable word autocompletion
    ScAppOptions aAppOptions(mod->GetAppOptions());
    aAppOptions.SetAutoComplete(true);
    mod->SetAppOptions(aAppOptions);

    OUString sThemeName;
    OUString sBackgroundThemeName;

    for (const beans::PropertyValue& rValue : rArguments)
    {
        if (rValue.Name == ".uno:SpellOnline" && rValue.Value.has<bool>())
        {
            ScViewData* pViewData = ScDocShell::GetViewData();
            if (ScTabViewShell* pTabViewShell = pViewData ? pViewData->GetViewShell() : nullptr)
                pTabViewShell->EnableAutoSpell(rValue.Value.get<bool>());
        }
        else if (rValue.Name == ".uno:ChangeTheme" && rValue.Value.has<OUString>())
            sThemeName = rValue.Value.get<OUString>();
        else if (rValue.Name == ".uno:InvertBackground" && rValue.Value.has<OUString>())
            sBackgroundThemeName = rValue.Value.get<OUString>();
    }

    // show us the text exactly
    ScInputOptions aInputOptions(mod->GetInputOptions());
    aInputOptions.SetTextWysiwyg(true);
    aInputOptions.SetReplaceCellsWarn(false);
    mod->SetInputOptions(aInputOptions);
    if (pDocShell)
        pDocShell->CalcOutputFactor();

    // when the "This document may contain formatting or content that cannot
    // be saved..." dialog appears, it is auto-cancelled with tiled rendering,
    // causing 'Save' being disabled; so let's always save to the original
    // format
    auto xChanges = comphelper::ConfigurationChanges::create();
    officecfg::Office::Common::Save::Document::WarnAlienFormat::set(false, xChanges);
    xChanges->commit();

    // if we know what theme the user wants, then we can dispatch that now early
    if (!sThemeName.isEmpty())
    {
        css::uno::Sequence<css::beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence(
        {
            { "NewTheme", uno::Any(sThemeName) }
        }));
        comphelper::dispatchCommand(u".uno:ChangeTheme"_ustr, aPropertyValues);
    }
    if (!sBackgroundThemeName.isEmpty())
    {
        css::uno::Sequence<css::beans::PropertyValue> aPropertyValues(comphelper::InitPropertySequence(
        {
            { "NewTheme", uno::Any(sBackgroundThemeName) }
        }));
        comphelper::dispatchCommand(".uno:InvertBackground", aPropertyValues);
    }
}

uno::Any SAL_CALL ScModelObj::queryInterface( const uno::Type& rType )
{
    uno::Any aReturn = ::cppu::queryInterface(rType,
                static_cast< sheet::XSpreadsheetDocument *>(this),
                static_cast< document::XActionLockable *>(this),
                static_cast< sheet::XCalculatable *>(this),
                static_cast< util::XProtectable *>(this),
                static_cast< drawing::XDrawPagesSupplier *>(this),
                static_cast< sheet::XGoalSeek *>(this),
                static_cast< sheet::XConsolidatable *>(this),
                static_cast< sheet::XDocumentAuditing *>(this),
                static_cast< style::XStyleFamiliesSupplier *>(this),
                static_cast< view::XRenderable *>(this),
                static_cast< document::XLinkTargetSupplier *>(this),
                static_cast< beans::XPropertySet *>(this),
                static_cast< lang::XMultiServiceFactory *>(this),
                static_cast< lang::XServiceInfo *>(this),
                static_cast< util::XChangesNotifier *>(this),
                static_cast< sheet::opencl::XOpenCLSelection *>(this),
                static_cast< chart2::XDataProviderAccess *>(this));
    if ( aReturn.hasValue() )
        return aReturn;

    uno::Any aRet(SfxBaseModel::queryInterface( rType ));
    if ( !aRet.hasValue()
        && rType != cppu::UnoType<css::document::XDocumentEventBroadcaster>::get()
        && rType != cppu::UnoType<css::frame::XController>::get()
        && rType != cppu::UnoType<css::frame::XFrame>::get()
        && rType != cppu::UnoType<css::script::XInvocation>::get()
        && rType != cppu::UnoType<css::beans::XFastPropertySet>::get()
        && rType != cppu::UnoType<css::awt::XWindow>::get())
    {
        GetFormatter();
        if ( xNumberAgg.is() )
            aRet = xNumberAgg->queryAggregation( rType );
    }

    return aRet;
}

void SAL_CALL ScModelObj::acquire() noexcept
{
    SfxBaseModel::acquire();
}

void SAL_CALL ScModelObj::release() noexcept
{
    SfxBaseModel::release();
}

uno::Sequence<uno::Type> SAL_CALL ScModelObj::getTypes()
{
    static const uno::Sequence<uno::Type> aTypes = [&]()
    {
        uno::Sequence<uno::Type> aAggTypes;
        if ( GetFormatter().is() )
        {
            const uno::Type& rProvType = cppu::UnoType<lang::XTypeProvider>::get();
            uno::Any aNumProv(xNumberAgg->queryAggregation(rProvType));
            if(auto xNumProv
               = o3tl::tryAccess<uno::Reference<lang::XTypeProvider>>(aNumProv))
            {
                aAggTypes = (*xNumProv)->getTypes();
            }
        }
        return comphelper::concatSequences(
            SfxBaseModel::getTypes(),
            aAggTypes,
            uno::Sequence<uno::Type>
            {
                cppu::UnoType<sheet::XSpreadsheetDocument>::get(),
                cppu::UnoType<document::XActionLockable>::get(),
                cppu::UnoType<sheet::XCalculatable>::get(),
                cppu::UnoType<util::XProtectable>::get(),
                cppu::UnoType<drawing::XDrawPagesSupplier>::get(),
                cppu::UnoType<sheet::XGoalSeek>::get(),
                cppu::UnoType<sheet::XConsolidatable>::get(),
                cppu::UnoType<sheet::XDocumentAuditing>::get(),
                cppu::UnoType<style::XStyleFamiliesSupplier>::get(),
                cppu::UnoType<view::XRenderable>::get(),
                cppu::UnoType<document::XLinkTargetSupplier>::get(),
                cppu::UnoType<beans::XPropertySet>::get(),
                cppu::UnoType<lang::XMultiServiceFactory>::get(),
                cppu::UnoType<lang::XServiceInfo>::get(),
                cppu::UnoType<util::XChangesNotifier>::get(),
                cppu::UnoType<sheet::opencl::XOpenCLSelection>::get(),
            } );
    }();
    return aTypes;
}

uno::Sequence<sal_Int8> SAL_CALL ScModelObj::getImplementationId()
{
    return css::uno::Sequence<sal_Int8>();
}

void ScModelObj::Notify( SfxBroadcaster& rBC, const SfxHint& rHint )
{
    //  Not interested in reference update hints here

    const SfxHintId nId = rHint.GetId();
    if ( nId == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // has become invalid
        if (xNumberAgg.is())
        {
            SvNumberFormatsSupplierObj* pNumFmt =
                comphelper::getFromUnoTunnel<SvNumberFormatsSupplierObj>(
                        uno::Reference<util::XNumberFormatsSupplier>(xNumberAgg, uno::UNO_QUERY) );
            if ( pNumFmt )
                pNumFmt->SetNumberFormatter( nullptr );
        }

        pPrintFuncCache.reset();     // must be deleted because it has a pointer to the DocShell
        m_pPrintState.reset();
    }
    else if ( nId == SfxHintId::DataChanged )
    {
        //  cached data for rendering become invalid when contents change
        //  (if a broadcast is added to SetDrawModified, is has to be tested here, too)

        pPrintFuncCache.reset();
        m_pPrintState.reset();

        // handle "OnCalculate" sheet events (search also for VBA event handlers)
        if ( pDocShell )
        {
            ScDocument& rDoc = pDocShell->GetDocument();
            if ( rDoc.GetVbaEventProcessor().is() )
            {
                // If the VBA event processor is set, HasAnyCalcNotification is much faster than HasAnySheetEventScript
                if ( rDoc.HasAnyCalcNotification() && rDoc.HasAnySheetEventScript( ScSheetEventId::CALCULATE, true ) )
                    HandleCalculateEvents();
            }
            else
            {
                if ( rDoc.HasAnySheetEventScript( ScSheetEventId::CALCULATE ) )
                    HandleCalculateEvents();
            }
        }
    }

    // always call parent - SfxBaseModel might need to handle the same hints again
    SfxBaseModel::Notify( rBC, rHint );     // SfxBaseModel is derived from SfxListener
}

// XSpreadsheetDocument

uno::Reference<sheet::XSpreadsheets> SAL_CALL ScModelObj::getSheets()
{
    return getScSheets();
}

rtl::Reference<ScTableSheetsObj> ScModelObj::getScSheets()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return new ScTableSheetsObj(pDocShell);
    return nullptr;
}

css::uno::Reference< ::css::chart2::data::XDataProvider > SAL_CALL ScModelObj::createDataProvider()
{
    if (pDocShell)
    {
        return css::uno::Reference< ::css::chart2::data::XDataProvider > (
            ScServiceProvider::MakeInstance(ScServiceProvider::Type::CHDATAPROV, pDocShell), uno::UNO_QUERY);
    }
    return nullptr;
}

// XStyleFamiliesSupplier

uno::Reference<container::XNameAccess> SAL_CALL ScModelObj::getStyleFamilies()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return new ScStyleFamiliesObj(pDocShell);
    return nullptr;
}

// XRenderable

static OutputDevice* lcl_GetRenderDevice( const uno::Sequence<beans::PropertyValue>& rOptions )
{
    OutputDevice* pRet = nullptr;
    for (const beans::PropertyValue& rProp : rOptions)
    {
        const OUString & rPropName = rProp.Name;

        if (rPropName == SC_UNONAME_RENDERDEV)
        {
            uno::Reference<awt::XDevice> xRenderDevice(rProp.Value, uno::UNO_QUERY);
            if ( xRenderDevice.is() )
            {
                VCLXDevice* pDevice = dynamic_cast<VCLXDevice*>( xRenderDevice.get() );
                if ( pDevice )
                {
                    pRet = pDevice->GetOutputDevice().get();
                    pRet->SetDigitLanguage( ScModule::GetOptDigitLanguage() );
                }
            }
        }
    }
    return pRet;
}

static bool lcl_ParseTarget( const OUString& rTarget, ScRange& rTargetRange, tools::Rectangle& rTargetRect,
                        bool& rIsSheet, ScDocument& rDoc, SCTAB nSourceTab )
{
    // test in same order as in SID_CURRENTCELL execute

    ScAddress aAddress;
    SCTAB nNameTab;
    sal_Int32 nNumeric = 0;

    bool bRangeValid = false;
    bool bRectValid = false;

    if ( rTargetRange.Parse( rTarget, rDoc ) & ScRefFlags::VALID )
    {
        bRangeValid = true;             // range reference
    }
    else if ( aAddress.Parse( rTarget, rDoc ) & ScRefFlags::VALID )
    {
        rTargetRange = aAddress;
        bRangeValid = true;             // cell reference
    }
    else if ( ScRangeUtil::MakeRangeFromName( rTarget, rDoc, nSourceTab, rTargetRange ) ||
              ScRangeUtil::MakeRangeFromName( rTarget, rDoc, nSourceTab, rTargetRange, RUTL_DBASE ) )
    {
        bRangeValid = true;             // named range or database range
    }
    else if ( comphelper::string::isdigitAsciiString(rTarget) &&
              ( nNumeric = rTarget.toInt32() ) > 0 && nNumeric <= rDoc.MaxRow()+1 )
    {
        // row number is always mapped to cell A(row) on the same sheet
        rTargetRange = ScAddress( 0, static_cast<SCROW>(nNumeric-1), nSourceTab );     // target row number is 1-based
        bRangeValid = true;             // row number
    }
    else if ( rDoc.GetTable( rTarget, nNameTab ) )
    {
        rTargetRange = ScAddress(0,0,nNameTab);
        bRangeValid = true;             // sheet name
        rIsSheet = true;                // needs special handling (first page of the sheet)
    }
    else
    {
        // look for named drawing object

        ScDrawLayer* pDrawLayer = rDoc.GetDrawLayer();
        if ( pDrawLayer )
        {
            SCTAB nTabCount = rDoc.GetTableCount();
            for (SCTAB i=0; i<nTabCount && !bRangeValid; i++)
            {
                SdrPage* pPage = pDrawLayer->GetPage(static_cast<sal_uInt16>(i));
                OSL_ENSURE(pPage,"Page ?");
                if (pPage)
                {
                    SdrObjListIter aIter( pPage, SdrIterMode::DeepWithGroups );
                    SdrObject* pObject = aIter.Next();
                    while (pObject && !bRangeValid)
                    {
                        if ( ScDrawLayer::GetVisibleName( pObject ) == rTarget )
                        {
                            rTargetRect = pObject->GetLogicRect();              // 1/100th mm
                            rTargetRange = rDoc.GetRange( i, rTargetRect );    // underlying cells
                            bRangeValid = bRectValid = true;                    // rectangle is valid
                        }
                        pObject = aIter.Next();
                    }
                }
            }
        }
    }
    if ( bRangeValid && !bRectValid )
    {
        //  get rectangle for cell range
        rTargetRect = rDoc.GetMMRect( rTargetRange.aStart.Col(), rTargetRange.aStart.Row(),
                                      rTargetRange.aEnd.Col(),   rTargetRange.aEnd.Row(),
                                      rTargetRange.aStart.Tab() );
    }

    return bRangeValid;
}

static Printer* lcl_GetPrinter(const uno::Sequence<beans::PropertyValue>& rOptions)
{
    Printer* pPrinter = nullptr;
    OutputDevice* pDev = lcl_GetRenderDevice(rOptions);
    if (pDev && pDev->GetOutDevType() == OUTDEV_PRINTER)
        pPrinter = dynamic_cast<Printer*>(pDev);
    return pPrinter;
}

static Size lcl_GetPrintPageSize(Size aSize)
{
    aSize.setWidth(o3tl::convert(aSize.Width(), o3tl::Length::mm100, o3tl::Length::twip));
    aSize.setHeight(o3tl::convert(aSize.Height(), o3tl::Length::mm100, o3tl::Length::twip));
    return aSize;
}

bool ScModelObj::FillRenderMarkData( const uno::Any& aSelection,
                                     const uno::Sequence< beans::PropertyValue >& rOptions,
                                     ScMarkData& rMark,
                                     ScPrintSelectionStatus& rStatus, OUString& rPagesStr,
                                     bool& rbRenderToGraphic ) const
{
    OSL_ENSURE( !rMark.IsMarked() && !rMark.IsMultiMarked(), "FillRenderMarkData: MarkData must be empty" );
    OSL_ENSURE( pDocShell, "FillRenderMarkData: DocShell must be set" );

    bool bDone = false;

    uno::Reference<frame::XController> xView;

    // defaults when no options are passed: all sheets, include empty pages
    bool bSelectedSheetsOnly = false;
    bool bSuppressEmptyPages = true;

    bool bHasPrintContent = false;
    sal_Int32 nPrintContent = 0;        // all sheets / selected sheets / selected cells
    sal_Int32 nPrintRange = 0;          // all pages / pages
    sal_Int32 nEOContent = 0;          // even pages / odd pages
    OUString aPageRange;           // "pages" edit value

    for( const auto& rOption : rOptions )
    {
        if ( rOption.Name == "IsOnlySelectedSheets" )
        {
            rOption.Value >>= bSelectedSheetsOnly;
        }
        else if ( rOption.Name == "IsSuppressEmptyPages" )
        {
            rOption.Value >>= bSuppressEmptyPages;
        }
        else if ( rOption.Name == "PageRange" )
        {
            rOption.Value >>= aPageRange;
        }
        else if ( rOption.Name == "PrintRange" )
        {
            rOption.Value >>= nPrintRange;
        }
        else if ( rOption.Name == "EvenOdd" )
        {
            rOption.Value >>= nEOContent;
        }
        else if ( rOption.Name == "PrintContent" )
        {
            bHasPrintContent = true;
            rOption.Value >>= nPrintContent;
        }
        else if ( rOption.Name == "View" )
        {
            rOption.Value >>= xView;
        }
        else if ( rOption.Name == "RenderToGraphic" )
        {
            rOption.Value >>= rbRenderToGraphic;
        }
    }

    // "Print Content" selection wins over "Selected Sheets" option
    if ( bHasPrintContent )
        bSelectedSheetsOnly = ( nPrintContent != 0 );

    uno::Reference<uno::XInterface> xInterface(aSelection, uno::UNO_QUERY);
    if ( xInterface.is() )
    {
        ScCellRangesBase* pSelObj = dynamic_cast<ScCellRangesBase*>( xInterface.get() );
        uno::Reference< drawing::XShapes > xShapes( xInterface, uno::UNO_QUERY );
        if ( pSelObj && pSelObj->GetDocShell() == pDocShell )
        {
            bool bSheet = ( dynamic_cast<ScTableSheetObj*>( pSelObj ) != nullptr );
            bool bCursor = pSelObj->IsCursorOnly();
            const ScRangeList& rRanges = pSelObj->GetRangeList();

            rMark.MarkFromRangeList( rRanges, false );
            rMark.MarkToSimple();

            if ( rMark.IsMultiMarked() )
            {
                // #i115266# copy behavior of old printing:
                // treat multiple selection like a single selection with the enclosing range
                const ScRange& aMultiMarkArea = rMark.GetMultiMarkArea();
                rMark.ResetMark();
                rMark.SetMarkArea( aMultiMarkArea );
            }

            if ( rMark.IsMarked() && !rMark.IsMultiMarked() )
            {
                // a sheet object is treated like an empty selection: print the used area of the sheet

                if ( bCursor || bSheet )                // nothing selected -> use whole tables
                {
                    rMark.ResetMark();      // doesn't change table selection
                    rStatus.SetMode( ScPrintSelectionMode::Cursor );
                }
                else
                    rStatus.SetMode( ScPrintSelectionMode::Range );

                rStatus.SetRanges( rRanges );
                bDone = true;
            }
            // multi selection isn't supported
        }
        else if( xShapes.is() )
        {
            //print a selected ole object
            // multi selection isn't supported yet
            uno::Reference< drawing::XShape > xShape( xShapes->getByIndex(0), uno::UNO_QUERY );
            SdrObject* pSdrObj = SdrObject::getSdrObjectFromXShape( xShape );
            if( pSdrObj && pDocShell )
            {
                ScDocument& rDoc = pDocShell->GetDocument();
                tools::Rectangle aObjRect = pSdrObj->GetCurrentBoundRect();
                SCTAB nCurrentTab = ScDocShell::GetCurTab();
                ScRange aRange = rDoc.GetRange( nCurrentTab, aObjRect );
                rMark.SetMarkArea( aRange );

                if( rMark.IsMarked() && !rMark.IsMultiMarked() )
                {
                    rStatus.SetMode( ScPrintSelectionMode::RangeExclusivelyOleAndDrawObjects );
                    bDone = true;
                }
            }
        }
        else if ( comphelper::getFromUnoTunnel<ScModelObj>( xInterface ) == this )
        {
            //  render the whole document
            //  -> no selection, all sheets

            SCTAB nTabCount = pDocShell->GetDocument().GetTableCount();
            for (SCTAB nTab = 0; nTab < nTabCount; nTab++)
                rMark.SelectTable( nTab, true );
            rStatus.SetMode( ScPrintSelectionMode::Document );
            bDone = true;
        }
        // other selection types aren't supported
    }

    // restrict to selected sheets if a view is available
    uno::Reference<sheet::XSelectedSheetsSupplier> xSelectedSheets(xView, uno::UNO_QUERY);
    if (bSelectedSheetsOnly && pDocShell && xSelectedSheets.is())
    {
        const uno::Sequence<sal_Int32> aSelected = xSelectedSheets->getSelectedSheets();
        ScMarkData::MarkedTabsType aSelectedTabs;
        SCTAB nMaxTab = pDocShell->GetDocument().GetTableCount() -1;
        for (const auto& rSelected : aSelected)
        {
            SCTAB nSelected = static_cast<SCTAB>(rSelected);
            if (ValidTab(nSelected, nMaxTab))
                aSelectedTabs.insert(nSelected);
        }
        rMark.SetSelectedTabs(aSelectedTabs);
    }

    ScPrintOptions aNewOptions;
    aNewOptions.SetSkipEmpty( bSuppressEmptyPages );
    aNewOptions.SetAllSheets( !bSelectedSheetsOnly );
    rStatus.SetOptions( aNewOptions );

    // "PrintRange" enables (1) or disables (0) the "PageRange" edit
    if ( nPrintRange == 1 )
        rPagesStr = aPageRange;
    else
        rPagesStr.clear();

    return bDone;
}

sal_Int32 SAL_CALL ScModelObj::getRendererCount(const uno::Any& aSelection,
    const uno::Sequence<beans::PropertyValue>& rOptions)
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
    {
        throw lang::DisposedException( OUString(),
                static_cast< sheet::XSpreadsheetDocument* >(this) );
    }

    ScMarkData aMark(GetDocument()->GetSheetLimits());
    ScPrintSelectionStatus aStatus;
    OUString aPagesStr;
    bool bRenderToGraphic = false;
    if ( !FillRenderMarkData( aSelection, rOptions, aMark, aStatus, aPagesStr, bRenderToGraphic ) )
        return 0;

    Size aPrintPageSize;
    bool bPrintPageLandscape = false;
    bool bUsePrintDialogSetting = false;
    Printer* pPrinter = lcl_GetPrinter(rOptions);
    if (pPrinter)
    {
        if (pPrinter->IsUsePrintDialogSetting())
        {
            bUsePrintDialogSetting = true;
            bPrintPageLandscape = (pPrinter->GetOrientation() == Orientation::Landscape);
            aPrintPageSize = lcl_GetPrintPageSize(pPrinter->GetPrintPageSize());
        }
    }

    //  The same ScPrintFuncCache object in pPrintFuncCache is used as long as
    //  the same selection is used (aStatus) and the document isn't changed
    //  (pPrintFuncCache is cleared in Notify handler)

    if (!pPrintFuncCache || !pPrintFuncCache->IsSameSelection(aStatus) || bUsePrintDialogSetting)
    {
        pPrintFuncCache.reset(new ScPrintFuncCache(pDocShell, aMark, std::move(aStatus), aPrintPageSize,
                                                   bPrintPageLandscape, bUsePrintDialogSetting));
    }
    sal_Int32 nPages = pPrintFuncCache->GetPageCount();

    m_pPrintState.reset();
    maValidPages.clear();

    sal_Int32 nContent = 0;
    sal_Int32 nEOContent = 0;
    bool bSinglePageSheets = false;
    for ( const auto& rValue : rOptions)
    {
        if ( rValue.Name == "PrintRange" )
        {
            rValue.Value >>= nContent;
        }
        else if ( rValue.Name == "SinglePageSheets" )
        {
            rValue.Value >>= bSinglePageSheets;
        }
        else if ( rValue.Name == "EvenOdd" )
        {
            rValue.Value >>= nEOContent;
        }
    }

    if (bSinglePageSheets)
    {
        return pDocShell->GetDocument().GetTableCount();
    }

    bool bIsPrintEvenPages = (nEOContent != 1 && nContent == 0) || nContent != 0;
    bool bIsPrintOddPages = (nEOContent != 2 && nContent == 0) || nContent != 0;

    for ( sal_Int32 nPage = 1; nPage <= nPages; nPage++ )
    {
        if ( (bIsPrintEvenPages && IsOnEvenPage( nPage )) || (bIsPrintOddPages && !IsOnEvenPage( nPage )) )
            maValidPages.push_back( nPage );
    }

    sal_Int32 nSelectCount = static_cast<sal_Int32>( maValidPages.size() );

    if ( nEOContent == 1 || nEOContent == 2 ) // even pages / odd pages
        return nSelectCount;

    if ( !aPagesStr.isEmpty() )
    {
        StringRangeEnumerator aRangeEnum( aPagesStr, 0, nPages-1 );
        nSelectCount = aRangeEnum.size();
    }
    return (nSelectCount > 0) ? nSelectCount : 1;
}

static sal_Int32 lcl_GetRendererNum( sal_Int32 nSelRenderer, std::u16string_view rPagesStr, sal_Int32 nTotalPages )
{
    if ( rPagesStr.empty() )
        return nSelRenderer;

    StringRangeEnumerator aRangeEnum( rPagesStr, 0, nTotalPages-1 );
    StringRangeEnumerator::Iterator aIter = aRangeEnum.begin();
    StringRangeEnumerator::Iterator aEnd  = aRangeEnum.end();
    for ( ; nSelRenderer > 0 && aIter != aEnd; --nSelRenderer )
        ++aIter;

    return *aIter; // returns -1 if reached the end
}

static bool lcl_renderSelectionToGraphic( bool bRenderToGraphic, const ScPrintSelectionStatus& rStatus )
{
    return bRenderToGraphic && rStatus.GetMode() == ScPrintSelectionMode::Range;
}

uno::Sequence<beans::PropertyValue> SAL_CALL ScModelObj::getRenderer( sal_Int32 nSelRenderer,
                                    const uno::Any& aSelection, const uno::Sequence<beans::PropertyValue>& rOptions  )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
    {
        throw lang::DisposedException( OUString(),
                static_cast< sheet::XSpreadsheetDocument* >(this) );
    }

    ScMarkData aMark(pDocShell->GetDocument().GetSheetLimits());
    ScPrintSelectionStatus aStatus;
    OUString aPagesStr;
    // #i115266# if FillRenderMarkData fails, keep nTotalPages at 0, but still handle getRenderer(0) below
    tools::Long nTotalPages = 0;
    bool bRenderToGraphic = false;
    bool bSinglePageSheets = false;
    if ( FillRenderMarkData( aSelection, rOptions, aMark, aStatus, aPagesStr, bRenderToGraphic ) )
    {
        if ( !pPrintFuncCache || !pPrintFuncCache->IsSameSelection( aStatus ) )
        {
            pPrintFuncCache.reset(new ScPrintFuncCache( pDocShell, aMark, aStatus ));
        }
        nTotalPages = pPrintFuncCache->GetPageCount();
    }

    for ( const auto& rValue : rOptions)
    {
        if ( rValue.Name == "SinglePageSheets" )
        {
            rValue.Value >>= bSinglePageSheets;
            break;
        }
    }

    if (bSinglePageSheets)
        nTotalPages = pDocShell->GetDocument().GetTableCount();

    sal_Int32 nRenderer = lcl_GetRendererNum( nSelRenderer, aPagesStr, nTotalPages );

    if ( nRenderer < 0 )
    {
        if ( nSelRenderer != 0 )
            throw lang::IllegalArgumentException();

        // getRenderer(0) is used to query the settings, so it must always return something

        awt::Size aPageSize;
        if (lcl_renderSelectionToGraphic( bRenderToGraphic, aStatus))
        {
            assert( aMark.IsMarked());
            const ScRange& aRange = aMark.GetMarkArea();
            tools::Rectangle aMMRect( pDocShell->GetDocument().GetMMRect(
                    aRange.aStart.Col(), aRange.aStart.Row(),
                    aRange.aEnd.Col(), aRange.aEnd.Row(), aRange.aStart.Tab()));
            aPageSize.Width = aMMRect.GetWidth();
            aPageSize.Height = aMMRect.GetHeight();
        }
        else
        {
            SCTAB const nCurTab = 0;      //! use current sheet from view?
            ScPrintFunc aDefaultFunc( pDocShell, pDocShell->GetPrinter(), nCurTab );
            Size aTwips = aDefaultFunc.GetPageSize();
            aPageSize.Width = convertTwipToMm100(aTwips.Width());
            aPageSize.Height = convertTwipToMm100(aTwips.Height());
        }

        uno::Sequence<beans::PropertyValue> aSequence( comphelper::InitPropertySequence({
            { SC_UNONAME_PAGESIZE, uno::Any(aPageSize) }
        }));

        if( ! pPrinterOptions )
            pPrinterOptions.reset(new ScPrintUIOptions);
        else
            pPrinterOptions->SetDefaults();
        pPrinterOptions->appendPrintUIOptions( aSequence );
        return aSequence;

    }

    //  printer is used as device (just for page layout), draw view is not needed

    SCTAB nTab;
    if (bSinglePageSheets)
        nTab = nSelRenderer;
    else if ( !maValidPages.empty() )
        nTab = pPrintFuncCache->GetTabForPage( maValidPages.at( nRenderer )-1 );
    else
        nTab = pPrintFuncCache->GetTabForPage( nRenderer );


    ScRange aRange;
    const ScRange* pSelRange = nullptr;
    if ( bSinglePageSheets )
    {
        SCCOL nStartCol;
        SCROW nStartRow;
        const ScDocument* pDocument = &pDocShell->GetDocument();
        pDocument->GetDataStart( nTab, nStartCol, nStartRow );
        SCCOL nEndCol;
        SCROW nEndRow;
        pDocument->GetPrintArea( nTab, nEndCol, nEndRow );

        aRange.aStart = ScAddress(nStartCol, nStartRow, nTab);
        aRange.aEnd = ScAddress(nEndCol, nEndRow, nTab);

        table::CellRangeAddress aRangeAddress( nTab,
                        aRange.aStart.Col(), aRange.aStart.Row(),
                        aRange.aEnd.Col(), aRange.aEnd.Row() );
        tools::Rectangle aMMRect( pDocShell->GetDocument().GetMMRect(
                    aRange.aStart.Col(), aRange.aStart.Row(),
                    aRange.aEnd.Col(), aRange.aEnd.Row(), aRange.aStart.Tab()));

        const awt::Size aPageSize(aMMRect.GetWidth(), aMMRect.GetHeight());
        const awt::Point aCalcPagePos(aMMRect.Left(), aMMRect.Top());

        uno::Sequence<beans::PropertyValue> aSequence
        {
            comphelper::makePropertyValue(SC_UNONAME_PAGESIZE, aPageSize),
            // #i111158# all positions are relative to the whole page, including non-printable area
            comphelper::makePropertyValue(SC_UNONAME_INC_NP_AREA, true),
            comphelper::makePropertyValue(SC_UNONAME_SOURCERANGE, aRangeAddress),
            comphelper::makePropertyValue(SC_UNONAME_CALCPAGESIZE, aPageSize), // TODO aPageSize too ?
            comphelper::makePropertyValue(SC_UNONAME_CALCPAGEPOS, aCalcPagePos)
        };

        if( ! pPrinterOptions )
            pPrinterOptions.reset(new ScPrintUIOptions);
        else
            pPrinterOptions->SetDefaults();
        pPrinterOptions->appendPrintUIOptions( aSequence );
        return aSequence;
    }
    else if ( aMark.IsMarked() )
    {
        aRange = aMark.GetMarkArea();
        pSelRange = &aRange;
    }

    awt::Size aPageSize;
    bool bWasCellRange = false;
    ScRange aCellRange;
    if (lcl_renderSelectionToGraphic( bRenderToGraphic, aStatus))
    {
        bWasCellRange = true;
        aCellRange = aRange;
        tools::Rectangle aMMRect( pDocShell->GetDocument().GetMMRect(
                    aRange.aStart.Col(), aRange.aStart.Row(),
                    aRange.aEnd.Col(), aRange.aEnd.Row(), aRange.aStart.Tab()));
        aPageSize.Width = aMMRect.GetWidth();
        aPageSize.Height = aMMRect.GetHeight();
    }
    else
    {
        Size aPrintPageSize;
        bool bPrintPageLandscape = false;
        bool bUsePrintDialogSetting = false;
        Printer* pPrinter = lcl_GetPrinter(rOptions);
        if (pPrinter)
        {
            if (pPrinter->IsUsePrintDialogSetting())
            {
                bUsePrintDialogSetting = true;
                bPrintPageLandscape = (pPrinter->GetOrientation() == Orientation::Landscape);
                aPrintPageSize = lcl_GetPrintPageSize(pPrinter->GetPrintPageSize());
            }
        }

        std::unique_ptr<ScPrintFunc, o3tl::default_delete<ScPrintFunc>> pPrintFunc;
        if (m_pPrintState && m_pPrintState->nPrintTab == nTab)
            pPrintFunc.reset(new ScPrintFunc(pDocShell, pDocShell->GetPrinter(), *m_pPrintState,
                                             &aStatus.GetOptions(), aPrintPageSize,
                                             bPrintPageLandscape,
                                             bUsePrintDialogSetting));
        else
            pPrintFunc.reset(new ScPrintFunc(pDocShell, pDocShell->GetPrinter(), nTab,
                                             pPrintFuncCache->GetFirstAttr(nTab), nTotalPages,
                                             pSelRange, &aStatus.GetOptions(), nullptr,
                                             aPrintPageSize, bPrintPageLandscape,
                                             bUsePrintDialogSetting));
        pPrintFunc->SetRenderFlag( true );

        sal_Int32 nContent = 0;
        sal_Int32 nEOContent = 0;
        for ( const auto& rValue : rOptions)
        {
            if ( rValue.Name == "PrintRange" )
            {
                rValue.Value >>= nContent;
            }
            else if ( rValue.Name == "EvenOdd" )
            {
                rValue.Value >>= nEOContent;
            }
        }

        MultiSelection aPage;
        aPage.SetTotalRange( Range(0,RANGE_MAX) );

        bool bOddOrEven = (nContent == 0 && nEOContent == 1) || (nContent == 1 && nEOContent == 2); // even pages or odd pages
        // tdf#127682 when odd/even allow nRenderer of 0 even when maValidPages is empty
        // to allow PrinterController::abortJob to spool an empty page as part of
        // its abort procedure
        if (bOddOrEven && !maValidPages.empty())
            aPage.Select( maValidPages.at(nRenderer) );
        else
            aPage.Select( nRenderer+1 );

        tools::Long nDisplayStart = pPrintFuncCache->GetDisplayStart( nTab );
        tools::Long nTabStart = pPrintFuncCache->GetTabStart( nTab );

        (void)pPrintFunc->DoPrint( aPage, nTabStart, nDisplayStart, false, nullptr );

        bWasCellRange = pPrintFunc->GetLastSourceRange( aCellRange );
        Size aTwips = pPrintFunc->GetPageSize();

        if (!m_pPrintState || nRenderer == nTabStart)
        {
            m_pPrintState.reset(new ScPrintState());
            pPrintFunc->GetPrintState(*m_pPrintState);
        }

        aPageSize.Width = convertTwipToMm100(aTwips.Width());
        aPageSize.Height = convertTwipToMm100(aTwips.Height());
    }

    tools::Long nPropCount = bWasCellRange ? 5 : 4;
    uno::Sequence<beans::PropertyValue> aSequence(nPropCount);
    beans::PropertyValue* pArray = aSequence.getArray();
    pArray[0].Name = SC_UNONAME_PAGESIZE;
    pArray[0].Value <<= aPageSize;
    // #i111158# all positions are relative to the whole page, including non-printable area
    pArray[1].Name = SC_UNONAME_INC_NP_AREA;
    pArray[1].Value <<= true;
    if ( bWasCellRange )
    {
        table::CellRangeAddress aRangeAddress( nTab,
                        aCellRange.aStart.Col(), aCellRange.aStart.Row(),
                        aCellRange.aEnd.Col(), aCellRange.aEnd.Row() );
        tools::Rectangle aMMRect( pDocShell->GetDocument().GetMMRect(
                    aCellRange.aStart.Col(), aCellRange.aStart.Row(),
                    aCellRange.aEnd.Col(), aCellRange.aEnd.Row(), aCellRange.aStart.Tab()));

        const awt::Size aCalcPageSize(aMMRect.GetWidth(), aMMRect.GetHeight());
        const awt::Point aCalcPagePos(aMMRect.Left(), aMMRect.Top());

        pArray[2].Name = SC_UNONAME_SOURCERANGE;
        pArray[2].Value <<= aRangeAddress;
        pArray[3].Name = SC_UNONAME_CALCPAGESIZE;
        pArray[3].Value <<= aCalcPageSize;
        pArray[4].Name = SC_UNONAME_CALCPAGEPOS;
        pArray[4].Value <<= aCalcPagePos;
    }

    if( ! pPrinterOptions )
        pPrinterOptions.reset(new ScPrintUIOptions);
    else
        pPrinterOptions->SetDefaults();
    pPrinterOptions->appendPrintUIOptions( aSequence );
    return aSequence;
}

static void lcl_PDFExportHelper(const OutputDevice* pDev, const OUString& rTabName, bool bIsFirstPage)
{
    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
    if (pPDF)
    {
        css::lang::Locale const docLocale(Application::GetSettings().GetLanguageTag().getLocale());
        pPDF->SetDocumentLocale(docLocale);

        // first page of a sheet: add outline item for the sheet name

        if (pPDF->GetIsExportBookmarks())
        {
            // the sheet starts at the top of the page
            tools::Rectangle aArea(pDev->PixelToLogic(tools::Rectangle(0, 0, 0, 0)));
            sal_Int32 nDestID = pPDF->CreateDest(aArea);
            // top-level
            pPDF->CreateOutlineItem(-1/*nParent*/, rTabName, nDestID);
        }
        // #i56629# add the named destination stuff
        if (pPDF->GetIsExportNamedDestinations())
        {
            tools::Rectangle aArea(pDev->PixelToLogic(tools::Rectangle(0, 0, 0, 0)));
            //need the PDF page number here
            pPDF->CreateNamedDest(rTabName, aArea);
        }

        if (pPDF->GetIsExportTaggedPDF())
        {
            if (bIsFirstPage)
                pPDF->WrapBeginStructureElement(vcl::pdf::StructElement::Document, u"Workbook"_ustr);
            else
            {   // if there is a new worksheet(not first), delete and add new ScPDFState
                assert(pPDF->GetScPDFState());
                delete pPDF->GetScPDFState();
                pPDF->SetScPDFState(nullptr);
            }

            assert(pPDF->GetScPDFState() == nullptr);
            pPDF->SetScPDFState(new ScEnhancedPDFState());
        }
    }
}

static void lcl_PDFExportBookmarkHelper(OutputDevice* pDev, ScDocument& rDoc,
                                        const std::unique_ptr<ScPrintFuncCache>& pPrintFuncCache,
                                        const ScMarkData& rMark, sal_Int32 nTab)
{
    //  resolve the hyperlinks for PDF export

    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
    if (!pPDF || pPDF->GetBookmarks().empty())
        return;

    //  iterate over the hyperlinks that were output for this page

    std::vector<vcl::PDFExtOutDevBookmarkEntry>& rBookmarks = pPDF->GetBookmarks();
    for (const auto& rBookmark : rBookmarks)
    {
        OUString aBookmark = rBookmark.aBookmark;
        if (aBookmark.toChar() == '#')
        {
            //  try to resolve internal link

            OUString aTarget(aBookmark.copy(1));

            ScRange aTargetRange;
            tools::Rectangle aTargetRect; // 1/100th mm
            bool bIsSheet = false;
            bool bValid = lcl_ParseTarget(aTarget, aTargetRange, aTargetRect, bIsSheet, rDoc, nTab);

            if (bValid)
            {
                sal_Int32 nPage = -1;
                tools::Rectangle aArea;
                if (bIsSheet)
                {
                    //  Get first page for sheet (if nothing from that sheet is printed,
                    //  this page can show a different sheet)
                    nPage = pPrintFuncCache->GetTabStart(aTargetRange.aStart.Tab());
                    aArea = pDev->PixelToLogic(tools::Rectangle(0, 0, 0, 0));
                }
                else
                {
                    pPrintFuncCache->InitLocations(rMark, pDev); // does nothing if already initialized

                    ScPrintPageLocation aLocation;
                    if (pPrintFuncCache->FindLocation(aTargetRange.aStart, aLocation))
                    {
                        nPage = aLocation.nPage;

                        // get the rectangle of the page's cell range in 1/100th mm
                        ScRange aLocRange = aLocation.aCellRange;
                        tools::Rectangle aLocationMM = rDoc.GetMMRect(
                            aLocRange.aStart.Col(), aLocRange.aStart.Row(), aLocRange.aEnd.Col(),
                            aLocRange.aEnd.Row(), aLocRange.aStart.Tab());
                        tools::Rectangle aLocationPixel = aLocation.aRectangle;

                        // Scale and move the target rectangle from aLocationMM to aLocationPixel,
                        // to get the target rectangle in pixels.
                        assert(aLocationPixel.GetWidth() != 0 && aLocationPixel.GetHeight() != 0);

                        Fraction aScaleX(aLocationPixel.GetWidth(), aLocationMM.GetWidth());
                        Fraction aScaleY(aLocationPixel.GetHeight(), aLocationMM.GetHeight());

                        tools::Long nX1
                            = aLocationPixel.Left()
                            + static_cast<tools::Long>(
                                Fraction(aTargetRect.Left() - aLocationMM.Left(), 1) * aScaleX);
                        tools::Long nX2
                            = aLocationPixel.Left()
                            + static_cast<tools::Long>(
                                Fraction(aTargetRect.Right() - aLocationMM.Left(), 1) * aScaleX);
                        tools::Long nY1
                            = aLocationPixel.Top()
                            + static_cast<tools::Long>(
                                Fraction(aTargetRect.Top() - aLocationMM.Top(), 1) * aScaleY);
                        tools::Long nY2
                            = aLocationPixel.Top()
                            + static_cast<tools::Long>(
                                Fraction(aTargetRect.Bottom() - aLocationMM.Top(), 1) * aScaleY);

                        if (nX1 > aLocationPixel.Right())
                            nX1 = aLocationPixel.Right();
                        if (nX2 > aLocationPixel.Right())
                            nX2 = aLocationPixel.Right();
                        if (nY1 > aLocationPixel.Bottom())
                            nY1 = aLocationPixel.Bottom();
                        if (nY2 > aLocationPixel.Bottom())
                            nY2 = aLocationPixel.Bottom();

                        // The link target area is interpreted using the device's MapMode at
                        // the time of the CreateDest call, so PixelToLogic can be used here,
                        // regardless of the MapMode that is actually selected.
                        aArea = pDev->PixelToLogic(tools::Rectangle(nX1, nY1, nX2, nY2));
                    }
                }

                if (nPage >= 0)
                    pPDF->SetLinkDest(rBookmark.nLinkId, pPDF->CreateDest(aArea, nPage));
            }
        }
        else
        {
            //  external link, use as-is
            pPDF->SetLinkURL(rBookmark.nLinkId, aBookmark);
        }
    }
    rBookmarks.clear();
}

static void lcl_SetMediaScreen(const uno::Reference<drawing::XShape>& xMediaShape,
                               const OutputDevice* pDev, const tools::Rectangle& aRect,
                               sal_Int32 nPageNumb)
{
    OUString sMediaURL;
    uno::Reference<beans::XPropertySet> xPropSet(xMediaShape, uno::UNO_QUERY);
    xPropSet->getPropertyValue(u"MediaURL"_ustr) >>= sMediaURL;
    if (sMediaURL.isEmpty())
        return;
    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
    if (!pPDF)
        return;

    OUString sTitle;
    xPropSet->getPropertyValue(u"Title"_ustr) >>= sTitle;
    OUString sDescription;
    xPropSet->getPropertyValue(u"Description"_ustr) >>= sDescription;
    OUString const altText(sTitle.isEmpty() ? sDescription
                           : sDescription.isEmpty()
                               ? sTitle
                               : OUString::Concat(sTitle) + OUString::Concat("\n")
                                     + OUString::Concat(sDescription));

    OUString const mimeType(xPropSet->getPropertyValue(u"MediaMimeType"_ustr).get<OUString>());
    SdrObject* pSdrObj(SdrObject::getSdrObjectFromXShape(xMediaShape));
    sal_Int32 nScreenId = pPDF->CreateScreen(aRect, altText, mimeType, nPageNumb, pSdrObj);
    if (sMediaURL.startsWith("vnd.sun.star.Package:"))
    {
        // Embedded media
        OUString aTempFileURL;
        xPropSet->getPropertyValue(u"PrivateTempFileURL"_ustr) >>= aTempFileURL;
        pPDF->SetScreenStream(nScreenId, aTempFileURL);
    }
    else // Linked media
        pPDF->SetScreenURL(nScreenId, sMediaURL);
}

static void lcl_PDFExportMediaShapeScreen(const OutputDevice* pDev, const std::unique_ptr<ScPrintState>& rState,
                                          ScDocument& rDoc, SCTAB nTab, tools::Long nStartPage,
                                          bool bSinglePageSheets)
{
    ScDrawLayer* pDrawLayer = rDoc.GetDrawLayer();
    vcl::PDFExtOutDevData* pPDF = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
    if (pPDF && pPDF->GetIsExportTaggedPDF() && pDrawLayer)
    {

        if (!bSinglePageSheets)
        {
            SdrPage* pPage = pDrawLayer->GetPage(static_cast<sal_uInt16>(nTab));
            OSL_ENSURE(pPage, "Page ?");
            if (pPage)
            {
                ScStyleSheetPool* pStylePool = rDoc.GetStyleSheetPool();
                SfxStyleSheetBase* pStyleSheet = pStylePool->Find(rDoc.GetPageStyle(nTab), SfxStyleFamily::Page);
                SfxItemSet* pItemSet = &pStyleSheet->GetItemSet();

                tools::Long nLeftMargin(pItemSet->Get(ATTR_LRSPACE).ResolveLeft({}));
                nLeftMargin = o3tl::convert(nLeftMargin, o3tl::Length::twip, o3tl::Length::mm100);

                tools::Long nTopMargin(pItemSet->Get(ATTR_ULSPACE).GetUpper());
                nTopMargin = o3tl::convert(nTopMargin, o3tl::Length::twip, o3tl::Length::mm100);

                tools::Long nHeader = 0;
                const SvxSetItem* pHeaderSetItem = &pItemSet->Get(ATTR_PAGE_HEADERSET);
                bool bHasHdr = pHeaderSetItem->GetItemSet().Get(ATTR_PAGE_ON).GetValue();
                if (bHasHdr)
                {
                    const SfxItemSet* pHeaderSet = &pHeaderSetItem->GetItemSet();
                    tools::Long nHdrHeight = pHeaderSet->Get(ATTR_PAGE_SIZE).GetSize().Height();
                    nHeader = o3tl::convert(nHdrHeight, o3tl::Length::twip, o3tl::Length::mm100);
                }

                bool bTopDown = pItemSet->Get(ATTR_PAGE_TOPDOWN).GetValue();

                SdrObjListIter aIter(pPage, SdrIterMode::DeepWithGroups);
                SdrObject* pObj = aIter.Next();
                while (pObj && pObj->IsVisible())
                {
                    uno::Reference<drawing::XShape> xShape(pObj->getUnoShape(), uno::UNO_QUERY);
                    if (xShape->getShapeType() == "com.sun.star.drawing.MediaShape")
                    {
                        SCCOL nX1, nX2;
                        SCROW nY1, nY2;
                        sal_Int32 nPageNumb = nStartPage;
                        if (bTopDown) // top-bottom page order
                        {
                            nX1 = 0;
                            for (size_t i = 0; i < rState->m_aRanges.m_nPagesX; ++i)
                            {
                                nX2 = (*rState->m_aRanges.m_xPageEndX)[i];
                                for (size_t j = 0; j < rState->m_aRanges.m_nPagesY; ++j)
                                {
                                    auto& rPageRow = (*rState->m_aRanges.m_xPageRows)[j];
                                    nY1 = rPageRow.GetStartRow();
                                    nY2 = rPageRow.GetEndRow();

                                    tools::Rectangle aPageRect(rDoc.GetMMRect(nX1, nY1, nX2, nY2, nTab));
                                    tools::Rectangle aTmpRect(aPageRect.GetIntersection(pObj->GetCurrentBoundRect()));
                                    if (!aTmpRect.IsEmpty())
                                    {
                                        tools::Long nPosX(aTmpRect.getX() - aPageRect.getX() + nLeftMargin);
                                        tools::Long nPosY(aTmpRect.getY() - aPageRect.getY() + nHeader + nTopMargin);
                                        tools::Rectangle aRect(Point(nPosX, nPosY), aTmpRect.GetSize());
                                        lcl_SetMediaScreen(xShape, pDev, aRect, nPageNumb);
                                    }
                                    ++nPageNumb;
                                }
                                nX1 = nX2 + 1;
                            }
                        }
                        else // left to right page order
                        {
                            for (size_t i = 0; i < rState->m_aRanges.m_nPagesY; ++i)
                            {
                                auto& rPageRow = (*rState->m_aRanges.m_xPageRows)[i];
                                nY1 = rPageRow.GetStartRow();
                                nY2 = rPageRow.GetEndRow();
                                nX1 = 0;
                                for (size_t j = 0; j < rState->m_aRanges.m_nPagesX; ++j)
                                {
                                    nX2 = (*rState->m_aRanges.m_xPageEndX)[j];

                                    tools::Rectangle aPageRect(rDoc.GetMMRect(nX1, nY1, nX2, nY2, nTab));
                                    tools::Rectangle aTmpRect(aPageRect.GetIntersection(pObj->GetCurrentBoundRect()));
                                    if (!aTmpRect.IsEmpty())
                                    {
                                        tools::Long nPosX(aTmpRect.getX() - aPageRect.getX() + nLeftMargin);
                                        tools::Long nPosY(aTmpRect.getY() - aPageRect.getY() + nHeader + nTopMargin);
                                        tools::Rectangle aRect(Point(nPosX, nPosY), aTmpRect.GetSize());
                                        lcl_SetMediaScreen(xShape, pDev, aRect, nPageNumb);
                                    }
                                    ++nPageNumb;
                                    nX1 = nX2 + 1;
                                }
                            }
                        }
                    }
                    pObj = aIter.Next();
                }
            }
        }
        else    // export whole sheet
        {
            SCTAB nTabCount = rDoc.GetTableCount();
            for (SCTAB i = 0; i < nTabCount; ++i)
            {
                SdrPage* pPage = pDrawLayer->GetPage(static_cast<sal_uInt16>(i));
                OSL_ENSURE(pPage, "Page ?");
                if (pPage)
                {
                    SdrObjListIter aIter(pPage, SdrIterMode::DeepWithGroups);
                    SdrObject* pObj = aIter.Next();
                    while (pObj && pObj->IsVisible())
                    {
                        uno::Reference<drawing::XShape> xShape(pObj->getUnoShape(), uno::UNO_QUERY);
                        if (xShape->getShapeType() == "com.sun.star.drawing.MediaShape")
                        {
                            tools::Rectangle aRect(pObj->GetCurrentBoundRect());
                            lcl_SetMediaScreen(xShape, pDev, aRect, i);
                        }
                        pObj = aIter.Next();
                    }
                }
            }
        }
    }
}

void SAL_CALL ScModelObj::render( sal_Int32 nSelRenderer, const uno::Any& aSelection,
                                    const uno::Sequence<beans::PropertyValue>& rOptions )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
    {
        throw lang::DisposedException( OUString(),
                static_cast< sheet::XSpreadsheetDocument* >(this) );
    }

    ScMarkData aMark(pDocShell->GetDocument().GetSheetLimits());
    ScPrintSelectionStatus aStatus;
    OUString aPagesStr;
    bool bRenderToGraphic = false;
    bool bSinglePageSheets = false;
    bool bIsFirstPage = false;
    bool bIsLastPage = false;
    if ( !FillRenderMarkData( aSelection, rOptions, aMark, aStatus, aPagesStr, bRenderToGraphic ) )
        throw lang::IllegalArgumentException();

    if ( !pPrintFuncCache || !pPrintFuncCache->IsSameSelection( aStatus ) )
    {
        pPrintFuncCache.reset(new ScPrintFuncCache( pDocShell, aMark, aStatus ));
    }
    tools::Long nTotalPages = pPrintFuncCache->GetPageCount();

    for ( const auto& rValue : rOptions)
    {
        if ( rValue.Name == "SinglePageSheets" )
        {
            rValue.Value >>= bSinglePageSheets;
        }
        else if (rValue.Name == "IsFirstPage")
        {
            rValue.Value >>= bIsFirstPage;
        }
        else if (rValue.Name == "IsLastPage")
        {
            rValue.Value >>= bIsLastPage;
        }
    }

    if (bSinglePageSheets)
        nTotalPages = pDocShell->GetDocument().GetTableCount();

    // if no pages counted then user must be trying to print sheet/selection without any content (i.e empty)
    if (nTotalPages == 0)
    {
        ScPrintOptions aNewOptions = aStatus.GetOptions();
        aNewOptions.SetSkipEmpty(false);
        aStatus.SetOptions(aNewOptions);

        pPrintFuncCache.reset(new ScPrintFuncCache( pDocShell, aMark, aStatus ));
        nTotalPages = pPrintFuncCache->GetPageCount();
    }

    sal_Int32 nRenderer = lcl_GetRendererNum( nSelRenderer, aPagesStr, nTotalPages ); // 0, "", 0
    if ( nRenderer < 0 )
        throw lang::IllegalArgumentException();

    OutputDevice* pDev = lcl_GetRenderDevice( rOptions );
    if ( !pDev )
        throw lang::IllegalArgumentException();

    ScDocument& rDoc = pDocShell->GetDocument();

    SCTAB nTab;
    if (!maValidPages.empty())
        nTab = pPrintFuncCache->GetTabForPage(maValidPages.at(nRenderer) - 1);
    else
        nTab = pPrintFuncCache->GetTabForPage(nRenderer);

    tools::Long nTabStart = pPrintFuncCache->GetTabStart(nTab);

    if (nRenderer == nTabStart)
        lcl_PDFExportMediaShapeScreen(pDev, m_pPrintState, rDoc, nTab, nTabStart, bSinglePageSheets);

    ScRange aRange;
    const ScRange* pSelRange = nullptr;
    if ( bSinglePageSheets )
    {
        awt::Size aPageSize;
        SCCOL nStartCol;
        SCROW nStartRow;
        rDoc.GetDataStart( nSelRenderer, nStartCol, nStartRow );
        SCCOL nEndCol;
        SCROW nEndRow;
        rDoc.GetPrintArea( nSelRenderer, nEndCol, nEndRow );

        aRange.aStart = ScAddress(nStartCol, nStartRow, nSelRenderer);
        aRange.aEnd = ScAddress(nEndCol, nEndRow, nSelRenderer);

        tools::Rectangle aMMRect( pDocShell->GetDocument().GetMMRect(
                    aRange.aStart.Col(), aRange.aStart.Row(),
                    aRange.aEnd.Col(), aRange.aEnd.Row(), aRange.aStart.Tab()));

        aPageSize.Width = aMMRect.GetWidth();
        aPageSize.Height = aMMRect.GetHeight();

        //Set visible tab
        SCTAB nVisTab = rDoc.GetVisibleTab();
        if (nVisTab != nSelRenderer)
        {
            nVisTab = nSelRenderer;
            rDoc.SetVisibleTab(nVisTab);
        }

        OUString aTabName;
        rDoc.GetName(nVisTab, aTabName);
        lcl_PDFExportHelper(pDev, aTabName, bIsFirstPage);

        pDocShell->DoDraw(pDev, Point(0,0), Size(aPageSize.Width, aPageSize.Height), JobSetup());

        vcl::PDFExtOutDevData* pPDFData = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
        if (pPDFData && pPDFData->GetIsExportTaggedPDF() && bIsLastPage)
        {
            pPDFData->EndStructureElement();  // Workbook
            assert(pPDFData->GetScPDFState());
            delete pPDFData->GetScPDFState();
            pPDFData->SetScPDFState(nullptr);
        }

        lcl_PDFExportBookmarkHelper(pDev, rDoc, pPrintFuncCache, aMark, nVisTab);

        return;
    }
    else if ( aMark.IsMarked() )
    {
        aRange = aMark.GetMarkArea();
        pSelRange = &aRange;
    }

    if (lcl_renderSelectionToGraphic( bRenderToGraphic, aStatus))
    {
        // Similar to as in and when calling ScTransferObj::PaintToDev()

        tools::Rectangle aBound( Point(), pDev->GetOutputSize());

        ScViewData aViewData(rDoc);

        aViewData.SetTabNo( aRange.aStart.Tab() );
        aViewData.SetScreen( aRange.aStart.Col(), aRange.aStart.Row(), aRange.aEnd.Col(), aRange.aEnd.Row() );

        const double nPrintFactor = 1.0;    /* XXX: currently (2017-08-28) is not evaluated */
        // The bMetaFile argument maybe could be
        // pDev->GetConnectMetaFile() != nullptr
        // but for some yet unknown reason does not draw cell content if true.
        ScPrintFunc::DrawToDev( rDoc, pDev, nPrintFactor, aBound, aViewData, false /*bMetaFile*/ );

        return;
    }

    struct DrawViewKeeper
    {
        std::unique_ptr<FmFormView> mpDrawView;
        DrawViewKeeper() {}
        ~DrawViewKeeper()
        {
            if (mpDrawView)
            {
                mpDrawView->HideSdrPage();
                mpDrawView.reset();
            }
        }
    } aDrawViewKeeper;

    ScDrawLayer* pModel = rDoc.GetDrawLayer();

    if( pModel )
    {
        aDrawViewKeeper.mpDrawView.reset( new FmFormView(
            *pModel,
            pDev) );
        aDrawViewKeeper.mpDrawView->ShowSdrPage(aDrawViewKeeper.mpDrawView->GetModel().GetPage(nTab));
        aDrawViewKeeper.mpDrawView->SetPrintPreview();
    }

    Size aPrintPageSize;
    bool bPrintPageLandscape = false;
    bool bUsePrintDialogSetting = false;
    Printer* pPrinter = lcl_GetPrinter(rOptions);
    if (pPrinter)
    {
        if (pPrinter->IsUsePrintDialogSetting())
        {
            bUsePrintDialogSetting = true;
            bPrintPageLandscape = (pPrinter->GetOrientation() == Orientation::Landscape);
            aPrintPageSize = lcl_GetPrintPageSize(pPrinter->GetPrintPageSize());
        }
    }

    //  to increase performance, ScPrintState might be used here for subsequent
    //  pages of the same sheet

    std::unique_ptr<ScPrintFunc, o3tl::default_delete<ScPrintFunc>> pPrintFunc;
    if (m_pPrintState && m_pPrintState->nPrintTab == nTab
        && ! pSelRange) // tdf#120161 use selection to set required printed area
        pPrintFunc.reset(new ScPrintFunc(pDev, pDocShell, *m_pPrintState, &aStatus.GetOptions(),
                                         aPrintPageSize, bPrintPageLandscape,
                                         bUsePrintDialogSetting));
    else
        pPrintFunc.reset(new ScPrintFunc(pDev, pDocShell, nTab, pPrintFuncCache->GetFirstAttr(nTab), nTotalPages, pSelRange, &aStatus.GetOptions()));

    pPrintFunc->SetDrawView( aDrawViewKeeper.mpDrawView.get() );
    pPrintFunc->SetRenderFlag( true );
    if( aStatus.GetMode() == ScPrintSelectionMode::RangeExclusivelyOleAndDrawObjects )
        pPrintFunc->SetExclusivelyDrawOleAndDrawObjects();

    sal_Int32 nContent = 0;
    sal_Int32 nEOContent = 0;
    for ( const auto& rValue : rOptions)
    {
        if ( rValue.Name == "PrintRange" )
        {
            rValue.Value >>= nContent;
        }
        else if ( rValue.Name == "EvenOdd" )
        {
            rValue.Value >>= nEOContent;
        }
    }

    MultiSelection aPage;
    aPage.SetTotalRange( Range(0,RANGE_MAX) );

    bool bOddOrEven = (nContent == 0 && nEOContent == 1) || (nContent == 0 && nEOContent == 2); // even pages or odd pages
    // tdf#127682 when odd/even allow nRenderer of 0 even when maValidPages is empty
    // to allow PrinterController::abortJob to spool an empty page as part of
    // its abort procedure
    if (bOddOrEven && !maValidPages.empty())
        aPage.Select( maValidPages.at( nRenderer ) );
    else
        aPage.Select( nRenderer+1 );

    tools::Long nDisplayStart = pPrintFuncCache->GetDisplayStart( nTab );

    if ( nRenderer == nTabStart || bIsFirstPage )
    {
        OUString aTabName;
        rDoc.GetName(nTab, aTabName);
        lcl_PDFExportHelper(pDev, aTabName, bIsFirstPage);
    }

    (void)pPrintFunc->DoPrint( aPage, nTabStart, nDisplayStart, true, nullptr );

    if (pPrinter)
    {
        // reset the print area created by the Print Dialog to the page style's print area
        if (pPrinter->IsUsePrintDialogSetting())
        {
            bUsePrintDialogSetting = false;
            if (m_pPrintState && m_pPrintState->nPrintTab == nTab && !pSelRange)
                pPrintFunc.reset(new ScPrintFunc(pDev, pDocShell, *m_pPrintState,
                                                 &aStatus.GetOptions(), aPrintPageSize,
                                                 bPrintPageLandscape, bUsePrintDialogSetting));
        }
    }

    vcl::PDFExtOutDevData* pPDFData = dynamic_cast<vcl::PDFExtOutDevData*>(pDev->GetExtOutDevData());
    if (pPDFData && pPDFData->GetIsExportTaggedPDF() && bIsLastPage)
    {
        pPDFData->EndStructureElement();  // Workbook
        assert(pPDFData->GetScPDFState());
        delete pPDFData->GetScPDFState();
        pPDFData->SetScPDFState(nullptr);
    }

    if (!m_pPrintState)
    {
        m_pPrintState.reset(new ScPrintState());
        pPrintFunc->GetPrintState(*m_pPrintState);
    }

    lcl_PDFExportBookmarkHelper(pDev, rDoc, pPrintFuncCache, aMark, nTab);
}

// XLinkTargetSupplier

uno::Reference<container::XNameAccess> SAL_CALL ScModelObj::getLinks()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return new ScLinkTargetTypesObj(pDocShell);
    return nullptr;
}

// XActionLockable

sal_Bool SAL_CALL ScModelObj::isActionLocked()
{
    SolarMutexGuard aGuard;
    bool bLocked = false;
    if (pDocShell)
        bLocked = ( pDocShell->GetLockCount() != 0 );
    return bLocked;
}

void SAL_CALL ScModelObj::addActionLock()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        pDocShell->LockDocument();
}

void SAL_CALL ScModelObj::removeActionLock()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        pDocShell->UnlockDocument();
}

void SAL_CALL ScModelObj::setActionLocks( sal_Int16 nLock )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        pDocShell->SetLockCount(nLock);
}

sal_Int16 SAL_CALL ScModelObj::resetActionLocks()
{
    SolarMutexGuard aGuard;
    sal_uInt16 nRet = 0;
    if (pDocShell)
    {
        nRet = pDocShell->GetLockCount();
        pDocShell->SetLockCount(0);
    }
    return nRet;
}

void SAL_CALL ScModelObj::lockControllers()
{
    SolarMutexGuard aGuard;
    SfxBaseModel::lockControllers();
    if (pDocShell)
        pDocShell->LockPaint();
}

void SAL_CALL ScModelObj::unlockControllers()
{
    SolarMutexGuard aGuard;
    if (hasControllersLocked())
    {
        SfxBaseModel::unlockControllers();
        if (pDocShell)
            pDocShell->UnlockPaint();
    }
}

// XCalculate

void SAL_CALL ScModelObj::calculate()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        comphelper::ProfileZone aZone("calculate");
        pDocShell->DoRecalc(true);
    }
    else
    {
        OSL_FAIL("no DocShell");     //! throw exception?
    }
}

void SAL_CALL ScModelObj::calculateAll()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        comphelper::ProfileZone aZone("calculateAll");
        pDocShell->DoHardRecalc();
    }
    else
    {
        OSL_FAIL("no DocShell");     //! throw exception?
    }
}

sal_Bool SAL_CALL ScModelObj::isAutomaticCalculationEnabled()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return pDocShell->GetDocument().GetAutoCalc();

    OSL_FAIL("no DocShell");     //! throw exception?
    return false;
}

void SAL_CALL ScModelObj::enableAutomaticCalculation( sal_Bool bEnabledIn )
{
    bool bEnabled(bEnabledIn);
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        ScDocument& rDoc = pDocShell->GetDocument();
        if ( rDoc.GetAutoCalc() != bEnabled )
        {
            rDoc.SetAutoCalc( bEnabled );
            pDocShell->SetDocumentModified();
        }
    }
    else
    {
        OSL_FAIL("no DocShell");     //! throw exception?
    }
}

// XProtectable

void SAL_CALL ScModelObj::protect( const OUString& aPassword )
{
    SolarMutexGuard aGuard;
    // #i108245# if already protected, don't change anything
    if ( pDocShell && !pDocShell->GetDocument().IsDocProtected() )
    {
        pDocShell->GetDocFunc().Protect( TABLEID_DOC, aPassword );
    }
}

void SAL_CALL ScModelObj::unprotect( const OUString& aPassword )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        bool bDone = pDocShell->GetDocFunc().Unprotect( TABLEID_DOC, aPassword, true );
        if (!bDone)
            throw lang::IllegalArgumentException();
    }
}

sal_Bool SAL_CALL ScModelObj::isProtected()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return pDocShell->GetDocument().IsDocProtected();

    OSL_FAIL("no DocShell");     //! throw exception?
    return false;
}

// XDrawPagesSupplier

uno::Reference<drawing::XDrawPages> SAL_CALL ScModelObj::getDrawPages()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return new ScDrawPagesObj(pDocShell);

    OSL_FAIL("no DocShell");     //! throw exception?
    return nullptr;
}

// XGoalSeek

sheet::GoalResult SAL_CALL ScModelObj::seekGoal(
                                const table::CellAddress& aFormulaPosition,
                                const table::CellAddress& aVariablePosition,
                                const OUString& aGoalValue )
{
    SolarMutexGuard aGuard;
    sheet::GoalResult aResult;
    aResult.Divergence = DBL_MAX;       // not found
    if (pDocShell)
    {
        weld::WaitObject aWait( ScDocShell::GetActiveDialogParent() );
        ScDocument& rDoc = pDocShell->GetDocument();
        double fValue = 0.0;
        bool bFound = rDoc.Solver(
                    static_cast<SCCOL>(aFormulaPosition.Column), static_cast<SCROW>(aFormulaPosition.Row), aFormulaPosition.Sheet,
                    static_cast<SCCOL>(aVariablePosition.Column), static_cast<SCROW>(aVariablePosition.Row), aVariablePosition.Sheet,
                    aGoalValue, fValue );
        aResult.Result = fValue;
        if (bFound)
            aResult.Divergence = 0.0;   //! this is a lie
    }
    return aResult;
}

// XConsolidatable

uno::Reference<sheet::XConsolidationDescriptor> SAL_CALL ScModelObj::createConsolidationDescriptor(
                                sal_Bool bEmpty )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScConsolidationDescriptor> pNew = new ScConsolidationDescriptor;
    if ( pDocShell && !bEmpty )
    {
        ScDocument& rDoc = pDocShell->GetDocument();
        const ScConsolidateParam* pParam = rDoc.GetConsolidateDlgData();
        if (pParam)
            pNew->SetParam( *pParam );
    }
    return pNew;
}

void SAL_CALL ScModelObj::consolidate(
    const uno::Reference<sheet::XConsolidationDescriptor>& xDescriptor )
{
    SolarMutexGuard aGuard;
    //  in theory, this could also be a different object, so use only
    //  public XConsolidationDescriptor interface to copy the data into
    //  ScConsolidationDescriptor object
    //! but if this already is ScConsolidationDescriptor, do it directly via getImplementation?

    rtl::Reference< ScConsolidationDescriptor > xImpl(new ScConsolidationDescriptor);
    xImpl->setFunction( xDescriptor->getFunction() );
    xImpl->setSources( xDescriptor->getSources() );
    xImpl->setStartOutputPosition( xDescriptor->getStartOutputPosition() );
    xImpl->setUseColumnHeaders( xDescriptor->getUseColumnHeaders() );
    xImpl->setUseRowHeaders( xDescriptor->getUseRowHeaders() );
    xImpl->setInsertLinks( xDescriptor->getInsertLinks() );

    if (pDocShell)
    {
        const ScConsolidateParam& rParam = xImpl->GetParam();
        pDocShell->DoConsolidate( rParam );
        pDocShell->GetDocument().SetConsolidateDlgData( std::unique_ptr<ScConsolidateParam>(new ScConsolidateParam(rParam)) );
    }
}

// XDocumentAuditing

void SAL_CALL ScModelObj::refreshArrows()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        pDocShell->GetDocFunc().DetectiveRefresh();
}

// XViewDataSupplier
uno::Reference< container::XIndexAccess > SAL_CALL ScModelObj::getViewData(  )
{
    uno::Reference < container::XIndexAccess > xRet( SfxBaseModel::getViewData() );

    if( !xRet.is() )
    {
        SolarMutexGuard aGuard;
        if (pDocShell && pDocShell->GetCreateMode() == SfxObjectCreateMode::EMBEDDED)
        {
            rtl::Reference< comphelper::IndexedPropertyValuesContainer > xCont = new comphelper::IndexedPropertyValuesContainer();
            xRet = xCont;

            OUString sName;
            pDocShell->GetDocument().GetName( pDocShell->GetDocument().GetVisibleTab(), sName );
            SCCOL nPosLeft = pDocShell->GetDocument().GetPosLeft();
            SCROW nPosTop = pDocShell->GetDocument().GetPosTop();
            uno::Sequence< beans::PropertyValue > aSeq{
                comphelper::makePropertyValue(SC_ACTIVETABLE, sName),
                comphelper::makePropertyValue(SC_POSITIONLEFT, nPosLeft),
                comphelper::makePropertyValue(SC_POSITIONTOP, nPosTop)
            };
            xCont->insertByIndex( 0, uno::Any( aSeq ) );
        }
    }

    return xRet;
}

//  XPropertySet (Doc-Options)
//! provide them also to the application?

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScModelObj::getPropertySetInfo()
{
    SolarMutexGuard aGuard;
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( aPropSet.getPropertyMap() ));
    return aRef;
}

void SAL_CALL ScModelObj::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;

    if (!pDocShell)
        return;

    ScDocument& rDoc = pDocShell->GetDocument();
    const ScDocOptions& rOldOpt = rDoc.GetDocOptions();
    ScDocOptions aNewOpt = rOldOpt;
    //  Don't recalculate while loading XML, when the formula text is stored
    //  Recalculation after loading is handled separately.
    bool bHardRecalc = !rDoc.IsImportingXML();

    bool bOpt = ScDocOptionsHelper::setPropertyValue( aNewOpt, aPropSet.getPropertyMap(), aPropertyName, aValue );
    if (bOpt)
    {
        // done...
        if ( aPropertyName == SC_UNO_IGNORECASE ||
             aPropertyName == SC_UNONAME_REGEXP ||
             aPropertyName == SC_UNONAME_WILDCARDS ||
             aPropertyName == SC_UNO_LOOKUPLABELS )
            bHardRecalc = false;
    }
    else if (aPropertyName == SC_UNO_SPELLONLINE)
    {
        if (ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false))
            pViewShell->EnableAutoSpell(ScUnoHelpFunctions::GetBoolFromAny(aValue));
    }
    else if ( aPropertyName == SC_UNONAME_CLOCAL )
    {
        lang::Locale aLocale;
        if ( aValue >>= aLocale )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );
            eLatin = ScUnoConversion::GetLanguage(aLocale);
            rDoc.SetLanguage( eLatin, eCjk, eCtl );
        }
    }
    else if ( aPropertyName == SC_UNO_CODENAME )
    {
        OUString sCodeName;
        if ( aValue >>= sCodeName )
            rDoc.SetCodeName( sCodeName );
    }
    else if ( aPropertyName == SC_UNO_CJK_CLOCAL )
    {
        lang::Locale aLocale;
        if ( aValue >>= aLocale )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );
            eCjk = ScUnoConversion::GetLanguage(aLocale);
            rDoc.SetLanguage( eLatin, eCjk, eCtl );
        }
    }
    else if ( aPropertyName == SC_UNO_CTL_CLOCAL )
    {
        lang::Locale aLocale;
        if ( aValue >>= aLocale )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );
            eCtl = ScUnoConversion::GetLanguage(aLocale);
            rDoc.SetLanguage( eLatin, eCjk, eCtl );
        }
    }
    else if ( aPropertyName == SC_UNO_APPLYFMDES )
    {
        //  model is created if not there
        ScDrawLayer* pModel = pDocShell->MakeDrawLayer();
        pModel->SetOpenInDesignMode( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );

        SfxBindings* pBindings = pDocShell->GetViewBindings();
        if (pBindings)
            pBindings->Invalidate( SID_FM_OPEN_READONLY );
    }
    else if ( aPropertyName == SC_UNO_AUTOCONTFOC )
    {
        //  model is created if not there
        ScDrawLayer* pModel = pDocShell->MakeDrawLayer();
        pModel->SetAutoControlFocus( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );

        SfxBindings* pBindings = pDocShell->GetViewBindings();
        if (pBindings)
            pBindings->Invalidate( SID_FM_AUTOCONTROLFOCUS );
    }
    else if ( aPropertyName == SC_UNO_ISLOADED )
    {
        pDocShell->SetEmpty( !ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    }
    else if ( aPropertyName == SC_UNO_ISUNDOENABLED )
    {
        bool bUndoEnabled = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        rDoc.EnableUndo( bUndoEnabled );
        pDocShell->GetUndoManager()->SetMaxUndoActionCount(
            bUndoEnabled
            ? officecfg::Office::Common::Undo::Steps::get() : 0);
    }
    else if ( aPropertyName == SC_UNO_RECORDCHANGES )
    {
        bool bRecordChangesEnabled = ScUnoHelpFunctions::GetBoolFromAny( aValue );

        bool bChangeAllowed = true;
        if (!bRecordChangesEnabled)
            bChangeAllowed = !pDocShell->HasChangeRecordProtection();

        if (bChangeAllowed)
            pDocShell->SetChangeRecording(bRecordChangesEnabled);
    }
    else if ( aPropertyName == SC_UNO_ISADJUSTHEIGHTENABLED )
    {
        if( ScUnoHelpFunctions::GetBoolFromAny( aValue ) )
            rDoc.UnlockAdjustHeight();
        else
            rDoc.LockAdjustHeight();
    }
    else if ( aPropertyName == SC_UNO_ISEXECUTELINKENABLED )
    {
        rDoc.EnableExecuteLink( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    }
    else if ( aPropertyName == SC_UNO_ISCHANGEREADONLYENABLED )
    {
        rDoc.EnableChangeReadOnly( ScUnoHelpFunctions::GetBoolFromAny( aValue ) );
    }
    else if ( aPropertyName == "BuildId" )
    {
        aValue >>= maBuildId;
    }
    else if ( aPropertyName == "SavedObject" )    // set from chart after saving
    {
        OUString aObjName;
        aValue >>= aObjName;
        if ( !aObjName.isEmpty() )
            rDoc.RestoreChartListener( aObjName );
    }
    else if ( aPropertyName == SC_UNO_INTEROPGRABBAG )
    {
        setGrabBagItem(aValue);
    }
    else if (aPropertyName == SC_UNO_THEME)
    {
        SdrModel& rSdrModel = getSdrModelFromUnoModel();
        uno::Reference<util::XTheme> xTheme;
        if (aValue >>= xTheme)
        {
            auto& rUnoTheme = dynamic_cast<UnoTheme&>(*xTheme);
            rSdrModel.setTheme(rUnoTheme.getTheme());
        }
    }

    if ( aNewOpt != rOldOpt )
    {
        rDoc.SetDocOptions( aNewOpt );
        //! Recalc only for options that need it?
        if ( bHardRecalc )
            pDocShell->DoHardRecalc();
        pDocShell->SetDocumentModified();
    }
}

uno::Any SAL_CALL ScModelObj::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    uno::Any aRet;

    if (pDocShell)
    {
        ScDocument& rDoc = pDocShell->GetDocument();
        const ScDocOptions& rOpt = rDoc.GetDocOptions();
        aRet = ScDocOptionsHelper::getPropertyValue( rOpt, aPropSet.getPropertyMap(), aPropertyName );
        if ( aRet.hasValue() )
        {
            // done...
        }
        else if (aPropertyName == SC_UNO_SPELLONLINE)
        {
            if (ScTabViewShell* pViewShell = pDocShell->GetBestViewShell(false))
                aRet <<= pViewShell->IsAutoSpell();
        }
        else if ( aPropertyName == SC_UNONAME_CLOCAL )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );

            lang::Locale aLocale;
            ScUnoConversion::FillLocale( aLocale, eLatin );
            aRet <<= aLocale;
        }
        else if ( aPropertyName == SC_UNO_CODENAME )
        {
            aRet <<= rDoc.GetCodeName();
        }

        else if ( aPropertyName == SC_UNO_CJK_CLOCAL )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );

            lang::Locale aLocale;
            ScUnoConversion::FillLocale( aLocale, eCjk );
            aRet <<= aLocale;
        }
        else if ( aPropertyName == SC_UNO_CTL_CLOCAL )
        {
            LanguageType eLatin, eCjk, eCtl;
            rDoc.GetLanguage( eLatin, eCjk, eCtl );

            lang::Locale aLocale;
            ScUnoConversion::FillLocale( aLocale, eCtl );
            aRet <<= aLocale;
        }
        else if ( aPropertyName == SC_UNO_NAMEDRANGES )
        {
            aRet <<= uno::Reference<sheet::XNamedRanges>(new ScGlobalNamedRangesObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_DATABASERNG )
        {
            aRet <<= uno::Reference<sheet::XDatabaseRanges>(new ScDatabaseRangesObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_UNNAMEDDBRNG )
        {
            aRet <<= uno::Reference<sheet::XUnnamedDatabaseRanges>(new ScUnnamedDatabaseRangesObj(pDocShell));
        }
        else if ( aPropertyName == SC_UNO_COLLABELRNG )
        {
            aRet <<= uno::Reference<sheet::XLabelRanges>(new ScLabelRangesObj( pDocShell, true ));
        }
        else if ( aPropertyName == SC_UNO_ROWLABELRNG )
        {
            aRet <<= uno::Reference<sheet::XLabelRanges>(new ScLabelRangesObj( pDocShell, false ));
        }
        else if ( aPropertyName == SC_UNO_AREALINKS )
        {
            aRet <<= uno::Reference<sheet::XAreaLinks>(new ScAreaLinksObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_DDELINKS )
        {
            aRet <<= uno::Reference<container::XNameAccess>(new ScDDELinksObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_EXTERNALDOCLINKS )
        {
            aRet <<= uno::Reference<sheet::XExternalDocLinks>(new ScExternalDocLinksObj(pDocShell));
        }
        else if ( aPropertyName == SC_UNO_SHEETLINKS )
        {
            aRet <<= uno::Reference<container::XNameAccess>(new ScSheetLinksObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_APPLYFMDES )
        {
            // default for no model is TRUE
            ScDrawLayer* pModel = rDoc.GetDrawLayer();
            bool bOpenInDesign = pModel == nullptr || pModel->GetOpenInDesignMode();
            aRet <<= bOpenInDesign;
        }
        else if ( aPropertyName == SC_UNO_AUTOCONTFOC )
        {
            // default for no model is FALSE
            ScDrawLayer* pModel = rDoc.GetDrawLayer();
            bool bAutoControlFocus = pModel && pModel->GetAutoControlFocus();
            aRet <<= bAutoControlFocus;
        }
        else if ( aPropertyName == SC_UNO_FORBIDDEN )
        {
            aRet <<= uno::Reference<i18n::XForbiddenCharacters>(new ScForbiddenCharsObj( pDocShell ));
        }
        else if ( aPropertyName == SC_UNO_HASDRAWPAGES )
        {
            aRet <<= (pDocShell->GetDocument().GetDrawLayer() != nullptr);
        }
        else if ( aPropertyName == SC_UNO_BASICLIBRARIES )
        {
            aRet <<= pDocShell->GetBasicContainer();
        }
        else if ( aPropertyName == SC_UNO_DIALOGLIBRARIES )
        {
            aRet <<= pDocShell->GetDialogContainer();
        }
        else if ( aPropertyName == SC_UNO_VBAGLOBNAME )
        {
            /*  #i111553# This property provides the name of the constant that
                will be used to store this model in the global Basic manager.
                That constant will be equivalent to 'ThisComponent' but for
                each application, so e.g. a 'ThisExcelDoc' and a 'ThisWordDoc'
                constant can co-exist, as required by VBA. */
            aRet <<= u"ThisExcelDoc"_ustr;
        }
        else if ( aPropertyName == SC_UNO_RUNTIMEUID )
        {
            aRet <<= getRuntimeUID();
        }
        else if ( aPropertyName == SC_UNO_HASVALIDSIGNATURES )
        {
            aRet <<= hasValidSignatures();
        }
        else if ( aPropertyName == SC_UNO_ALLOWLINKUPDATE)
        {
            comphelper::EmbeddedObjectContainer& rEmbeddedObjectContainer = pDocShell->getEmbeddedObjectContainer();
            aRet <<= rEmbeddedObjectContainer.getUserAllowsLinkUpdate();
        }
        else if ( aPropertyName == SC_UNO_ISLOADED )
        {
            aRet <<= !pDocShell->IsEmpty();
        }
        else if ( aPropertyName == SC_UNO_ISUNDOENABLED )
        {
            aRet <<= rDoc.IsUndoEnabled();
        }
        else if ( aPropertyName == SC_UNO_RECORDCHANGES )
        {
            aRet <<= pDocShell->IsChangeRecording();
        }
        else if ( aPropertyName == SC_UNO_ISRECORDCHANGESPROTECTED )
        {
            aRet <<= pDocShell->HasChangeRecordProtection();
        }
        else if ( aPropertyName == SC_UNO_ISADJUSTHEIGHTENABLED )
        {
            aRet <<= !( rDoc.IsAdjustHeightLocked() );
        }
        else if ( aPropertyName == SC_UNO_ISEXECUTELINKENABLED )
        {
            aRet <<= rDoc.IsExecuteLinkEnabled();
        }
        else if ( aPropertyName == SC_UNO_ISCHANGEREADONLYENABLED )
        {
            aRet <<= rDoc.IsChangeReadOnlyEnabled();
        }
        else if ( aPropertyName == SC_UNO_REFERENCEDEVICE )
        {
            rtl::Reference<VCLXDevice> pXDev = new VCLXDevice();
            pXDev->SetOutputDevice( rDoc.GetRefDevice() );
            aRet <<= uno::Reference< awt::XDevice >( pXDev );
        }
        else if ( aPropertyName == "BuildId" )
        {
            aRet <<= maBuildId;
        }
        else if ( aPropertyName == "InternalDocument" )
        {
            aRet <<= (pDocShell->GetCreateMode() == SfxObjectCreateMode::INTERNAL);
        }
        else if ( aPropertyName == SC_UNO_INTEROPGRABBAG )
        {
            getGrabBagItem(aRet);
        }
        else if (aPropertyName == SC_UNO_THEME)
        {
            SdrModel& rSdrModel = getSdrModelFromUnoModel();
            css::uno::Reference<css::util::XTheme> xTheme;
            auto pTheme = rSdrModel.getTheme();
            if (pTheme)
                xTheme = model::theme::createXTheme(pTheme);
            aRet <<= xTheme;
        }
    }

    return aRet;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScModelObj )

// XMultiServiceFactory

css::uno::Reference<css::uno::XInterface> ScModelObj::create(
    OUString const & aServiceSpecifier,
    css::uno::Sequence<css::uno::Any> const * arguments)
{
    using ServiceType = ScServiceProvider::Type;

    uno::Reference<uno::XInterface> xRet;
    ServiceType nType = ScServiceProvider::GetProviderType(aServiceSpecifier);
    if ( nType != ServiceType::INVALID )
    {
        //  drawing layer tables must be kept as long as the model is alive
        //  return stored instance if already set
        switch ( nType )
        {
            case ServiceType::GRADTAB:    xRet.set(xDrawGradTab);     break;
            case ServiceType::HATCHTAB:   xRet.set(xDrawHatchTab);    break;
            case ServiceType::BITMAPTAB:  xRet.set(xDrawBitmapTab);   break;
            case ServiceType::TRGRADTAB:  xRet.set(xDrawTrGradTab);   break;
            case ServiceType::MARKERTAB:  xRet.set(xDrawMarkerTab);   break;
            case ServiceType::DASHTAB:    xRet.set(xDrawDashTab);     break;
            case ServiceType::CHDATAPROV: xRet.set(xChartDataProv);   break;
            case ServiceType::VBAOBJECTPROVIDER: xRet.set(xObjProvider); break;
            default: break;
        }

        // #i64497# If a chart is in a temporary document during clipboard paste,
        // there should be no data provider, so that own data is used
        bool bCreate =
                ( nType != ServiceType::CHDATAPROV ||
                ( pDocShell->GetCreateMode() != SfxObjectCreateMode::INTERNAL ));
        // this should never happen, i.e. the temporary document should never be
        // loaded, because this unlinks the data
        assert(bCreate);

        if ( !xRet.is() && bCreate )
        {
            xRet.set(ScServiceProvider::MakeInstance( nType, pDocShell ));

            //  store created instance
            switch ( nType )
            {
                case ServiceType::GRADTAB:    xDrawGradTab.set(xRet);     break;
                case ServiceType::HATCHTAB:   xDrawHatchTab.set(xRet);    break;
                case ServiceType::BITMAPTAB:  xDrawBitmapTab.set(xRet);   break;
                case ServiceType::TRGRADTAB:  xDrawTrGradTab.set(xRet);   break;
                case ServiceType::MARKERTAB:  xDrawMarkerTab.set(xRet);   break;
                case ServiceType::DASHTAB:    xDrawDashTab.set(xRet);     break;
                case ServiceType::CHDATAPROV: xChartDataProv.set(xRet);   break;
                case ServiceType::VBAOBJECTPROVIDER: xObjProvider.set(xRet); break;
                default: break;
            }
        }
    }
    else
    {
        //  we offload everything we don't know to SvxFmMSFactory,
        //  it'll throw exception if this isn't okay ...

        try
        {
            xRet = arguments == nullptr
                ? SvxFmMSFactory::createInstance(aServiceSpecifier)
                : SvxFmMSFactory::createInstanceWithArguments(
                    aServiceSpecifier, *arguments);
            // extra block to force deletion of the temporary before ScShapeObj ctor (setDelegator)
        }
        catch ( lang::ServiceNotRegisteredException & )
        {
        }

        //  if the drawing factory created a shape, a ScShapeObj has to be used
        //  to support own properties like ImageMap:

        uno::Reference<drawing::XShape> xShape( xRet, uno::UNO_QUERY );
        if ( xShape.is() )
        {
            xRet.clear();               // for aggregation, xShape must be the object's only ref
            new ScShapeObj( xShape );   // aggregates object and modifies xShape
            xRet.set(xShape);
        }
    }
    return xRet;
}

uno::Reference<uno::XInterface> SAL_CALL ScModelObj::createInstance(
                                const OUString& aServiceSpecifier )
{
    SolarMutexGuard aGuard;
    return create(aServiceSpecifier, nullptr);
}

uno::Reference<uno::XInterface> SAL_CALL ScModelObj::createInstanceWithArguments(
                                const OUString& ServiceSpecifier,
                                const uno::Sequence<uno::Any>& aArgs )
{
    //! distinguish between own services and those of drawing layer?

    SolarMutexGuard aGuard;
    uno::Reference<uno::XInterface> xInt(create(ServiceSpecifier, &aArgs));

    if ( aArgs.hasElements() )
    {
        //  used only for cell value binding so far - it can be initialized after creating

        uno::Reference<lang::XInitialization> xInit( xInt, uno::UNO_QUERY );
        if ( xInit.is() )
            xInit->initialize( aArgs );
    }

    return xInt;
}

uno::Sequence<OUString> SAL_CALL ScModelObj::getAvailableServiceNames()
{
    SolarMutexGuard aGuard;

    return comphelper::concatSequences( ScServiceProvider::GetAllServiceNames(),
                                        SvxFmMSFactory::getAvailableServiceNames() );
}

// XServiceInfo
OUString SAL_CALL ScModelObj::getImplementationName()
{
    return u"ScModelObj"_ustr;
    /* // Matching the .component information:
       return OUString( "com.sun.star.comp.Calc.SpreadsheetDocument" );
    */
}

sal_Bool SAL_CALL ScModelObj::supportsService( const OUString& rServiceName )
{
    return cppu::supportsService(this, rServiceName);
}

uno::Sequence<OUString> SAL_CALL ScModelObj::getSupportedServiceNames()
{
    return {SCMODELOBJ_SERVICE, SCDOCSETTINGS_SERVICE, SCDOC_SERVICE};
}

// XUnoTunnel

sal_Int64 SAL_CALL ScModelObj::getSomething(
                const uno::Sequence<sal_Int8 >& rId )
{
    if ( comphelper::isUnoTunnelId<ScModelObj>(rId) )
    {
        return comphelper::getSomething_cast(this);
    }

    if ( comphelper::isUnoTunnelId<SfxObjectShell>(rId) )
    {
        return comphelper::getSomething_cast(pDocShell);
    }

    //  aggregated number formats supplier has XUnoTunnel, too
    //  interface from aggregated object must be obtained via queryAggregation

    sal_Int64 nRet = SfxBaseModel::getSomething( rId );
    if ( nRet )
        return nRet;

    if ( GetFormatter().is() )
    {
        const uno::Type& rTunnelType = cppu::UnoType<lang::XUnoTunnel>::get();
        uno::Any aNumTunnel(xNumberAgg->queryAggregation(rTunnelType));
        if(auto xTunnelAgg = o3tl::tryAccess<uno::Reference<lang::XUnoTunnel>>(
               aNumTunnel))
        {
            return (*xTunnelAgg)->getSomething( rId );
        }
    }

    return 0;
}

const uno::Sequence<sal_Int8>& ScModelObj::getUnoTunnelId()
{
    static const comphelper::UnoIdInit theScModelObjUnoTunnelId;
    return theScModelObjUnoTunnelId.getSeq();
}

// XChangesNotifier

void ScModelObj::addChangesListener( const uno::Reference< util::XChangesListener >& aListener )
{
    SolarMutexGuard aGuard;
    maChangesListeners.addInterface( aListener );
}

void ScModelObj::removeChangesListener( const uno::Reference< util::XChangesListener >& aListener )
{
    SolarMutexGuard aGuard;
    maChangesListeners.removeInterface( aListener );
}

bool ScModelObj::HasChangesListeners() const
{
    if ( maChangesListeners.getLength() > 0 )
        return true;

    // "change" event set in any sheet?
    return pDocShell && pDocShell->GetDocument().HasAnySheetEventScript(ScSheetEventId::CHANGE);
}

namespace
{

void lcl_dataAreaInvalidation(ScModelObj* pModel,
                              const ScRangeList& rRanges,
                              bool bInvalidateDataArea, bool bExtendDataArea)
{
    size_t nRangeCount = rRanges.size();

    for ( size_t nIndex = 0; nIndex < nRangeCount; ++nIndex )
    {
        ScRange const & rRange = rRanges[ nIndex ];
        ScAddress const & rEnd = rRange.aEnd;
        SCTAB nTab = rEnd.Tab();

        bool bAreaExtended = false;

        if (bExtendDataArea)
        {
            const Size aCurrentDataArea = pModel->getDataArea( nTab );

            SCCOL nLastCol = aCurrentDataArea.Width();
            SCROW nLastRow = aCurrentDataArea.Height();

            bAreaExtended = rEnd.Col() > nLastCol || rEnd.Row() > nLastRow;
        }

        bool bInvalidate = bAreaExtended || bInvalidateDataArea;
        if ( bInvalidate )
        {
            if ( comphelper::LibreOfficeKit::isActive() )
                SfxLokHelper::notifyPartSizeChangedAllViews( pModel, nTab );
        }
    }
}

};

void ScModelObj::NotifyChanges( const OUString& rOperation, const ScRangeList& rRanges,
    const uno::Sequence< beans::PropertyValue >& rProperties )
{
    OUString aOperation = rOperation;
    bool bIsDataAreaInvalidateType = aOperation == "data-area-invalidate";
    bool bIsDataAreaExtendType = aOperation == "data-area-extend";

    bool bInvalidateDataArea = bIsDataAreaInvalidateType
        || HelperNotifyChanges::isDataAreaInvalidateType(aOperation);
    bool bExtendDataArea = bIsDataAreaExtendType || aOperation == "cell-change";

    if ( pDocShell )
    {
        lcl_dataAreaInvalidation(this, rRanges, bInvalidateDataArea, bExtendDataArea);

        // check if we were called only to update data area
        if (bIsDataAreaInvalidateType || bIsDataAreaExtendType)
            return;

        // backward-compatibility Operation conversion
        // FIXME: make sure it can be passed
        if (rOperation == "delete-content" || rOperation == "undo"
            || rOperation == "redo" || rOperation == "paste")
            aOperation = "cell-change";
    }

    if ( pDocShell && HasChangesListeners() )
    {
        util::ChangesEvent aEvent;
        aEvent.Source.set(getXWeak());
        aEvent.Base <<= aEvent.Source;

        size_t nRangeCount = rRanges.size();
        aEvent.Changes.realloc( static_cast< sal_Int32 >( nRangeCount ) );
        auto pChanges = aEvent.Changes.getArray();
        for ( size_t nIndex = 0; nIndex < nRangeCount; ++nIndex )
        {
            uno::Reference< table::XCellRange > xRangeObj;

            ScRange const & rRange = rRanges[ nIndex ];
            if ( rRange.aStart == rRange.aEnd )
            {
                xRangeObj.set( new ScCellObj( pDocShell, rRange.aStart ) );
            }
            else
            {
                xRangeObj.set( new ScCellRangeObj( pDocShell, rRange ) );
            }

            util::ElementChange& rChange = pChanges[ static_cast< sal_Int32 >( nIndex ) ];
            rChange.Accessor <<= aOperation;
            rChange.Element <<= rProperties;
            rChange.ReplacedElement <<= xRangeObj;
        }

        ::comphelper::OInterfaceIteratorHelper3 aIter( maChangesListeners );
        while ( aIter.hasMoreElements() )
        {
            try
            {
                aIter.next()->changesOccurred( aEvent );
            }
            catch( uno::Exception& )
            {
            }
        }
    }

    // handle sheet events
    //! separate method with ScMarkData? Then change HasChangesListeners back.
    if ( !(aOperation == "cell-change" && pDocShell) )
        return;

    ScMarkData aMarkData(pDocShell->GetDocument().GetSheetLimits());
    aMarkData.MarkFromRangeList( rRanges, false );
    ScDocument& rDoc = pDocShell->GetDocument();
    SCTAB nTabCount = rDoc.GetTableCount();
    for (const SCTAB& nTab : aMarkData)
    {
        if (nTab >= nTabCount)
            break;
        const ScSheetEvents* pEvents = rDoc.GetSheetEvents(nTab);
        if (pEvents)
        {
            const OUString* pScript = pEvents->GetScript(ScSheetEventId::CHANGE);
            if (pScript)
            {
                ScRangeList aTabRanges;     // collect ranges on this sheet
                size_t nRangeCount = rRanges.size();
                for ( size_t nIndex = 0; nIndex < nRangeCount; ++nIndex )
                {
                    ScRange const & rRange = rRanges[ nIndex ];
                    if ( rRange.aStart.Tab() == nTab )
                        aTabRanges.push_back( rRange );
                }
                size_t nTabRangeCount = aTabRanges.size();
                if ( nTabRangeCount > 0 )
                {
                    uno::Reference<uno::XInterface> xTarget;
                    if ( nTabRangeCount == 1 )
                    {
                        ScRange const & rRange = aTabRanges[ 0 ];
                        if ( rRange.aStart == rRange.aEnd )
                            xTarget.set( cppu::getXWeak( new ScCellObj( pDocShell, rRange.aStart ) ) );
                        else
                            xTarget.set( cppu::getXWeak( new ScCellRangeObj( pDocShell, rRange ) ) );
                    }
                    else
                        xTarget.set( cppu::getXWeak( new ScCellRangesObj( pDocShell, aTabRanges ) ) );

                    uno::Sequence<uno::Any> aParams{ uno::Any(xTarget) };

                    uno::Any aRet;
                    uno::Sequence<sal_Int16> aOutArgsIndex;
                    uno::Sequence<uno::Any> aOutArgs;

                    /*ErrCode eRet =*/ pDocShell->CallXScript( *pScript, aParams, aRet, aOutArgsIndex, aOutArgs );
                }
            }
        }
    }
}

void ScModelObj::HandleCalculateEvents()
{
    if (!pDocShell)
        return;

    ScDocument& rDoc = pDocShell->GetDocument();
    // don't call events before the document is visible
    // (might also set a flag on SfxEventHintId::LoadFinished and only disable while loading)
    if ( rDoc.IsDocVisible() )
    {
        SCTAB nTabCount = rDoc.GetTableCount();
        for (SCTAB nTab = 0; nTab < nTabCount; nTab++)
        {
            if (rDoc.HasCalcNotification(nTab))
            {
                if (const ScSheetEvents* pEvents = rDoc.GetSheetEvents( nTab ))
                {
                    if (const OUString* pScript = pEvents->GetScript(ScSheetEventId::CALCULATE))
                    {
                        uno::Any aRet;
                        uno::Sequence<uno::Any> aParams;
                        uno::Sequence<sal_Int16> aOutArgsIndex;
                        uno::Sequence<uno::Any> aOutArgs;
                        pDocShell->CallXScript( *pScript, aParams, aRet, aOutArgsIndex, aOutArgs );
                    }
                }

                try
                {
                    uno::Reference< script::vba::XVBAEventProcessor > xVbaEvents( rDoc.GetVbaEventProcessor(), uno::UNO_SET_THROW );
                    uno::Sequence< uno::Any > aArgs{ uno::Any(nTab) };
                    xVbaEvents->processVbaEvent( ScSheetEvents::GetVbaSheetEventId( ScSheetEventId::CALCULATE ), aArgs );
                }
                catch( uno::Exception& )
                {
                }
            }
        }
    }
    rDoc.ResetCalcNotifications();
}

// XOpenCLSelection

sal_Bool ScModelObj::isOpenCLEnabled()
{
    return ScCalcConfig::isOpenCLEnabled();
}

void ScModelObj::enableOpenCL(sal_Bool bEnable)
{
    if (ScCalcConfig::isOpenCLEnabled() == static_cast<bool>(bEnable))
        return;
    if (ScCalcConfig::getForceCalculationType() != ForceCalculationNone)
        return;

    std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
    officecfg::Office::Common::Misc::UseOpenCL::set(bEnable, batch);
    batch->commit();

    ScCalcConfig aConfig = ScInterpreter::GetGlobalConfig();
    if (bEnable)
        aConfig.setOpenCLConfigToDefault();
    ScInterpreter::SetGlobalConfig(aConfig);

#if HAVE_FEATURE_OPENCL
    sc::FormulaGroupInterpreter::switchOpenCLDevice(u"", true);
#endif

    ScDocument* pDoc = GetDocument();
    pDoc->CheckVectorizationState();

}

void ScModelObj::enableAutomaticDeviceSelection(sal_Bool bForce)
{
    ScCalcConfig aConfig = ScInterpreter::GetGlobalConfig();
    aConfig.mbOpenCLAutoSelect = true;
    ScInterpreter::SetGlobalConfig(aConfig);
    ScModule* mod = ScModule::get();
    ScFormulaOptions aOptions = mod->GetFormulaOptions();
    aOptions.SetCalcConfig(aConfig);
    mod->SetFormulaOptions(aOptions);
#if !HAVE_FEATURE_OPENCL
    (void) bForce;
#else
    sc::FormulaGroupInterpreter::switchOpenCLDevice(u"", true, bForce);
#endif
}

void ScModelObj::disableAutomaticDeviceSelection()
{
    ScCalcConfig aConfig = ScInterpreter::GetGlobalConfig();
    aConfig.mbOpenCLAutoSelect = false;
    ScInterpreter::SetGlobalConfig(aConfig);
    ScModule* mod = ScModule::get();
    ScFormulaOptions aOptions = mod->GetFormulaOptions();
    aOptions.SetCalcConfig(aConfig);
    mod->SetFormulaOptions(aOptions);
}

void ScModelObj::selectOpenCLDevice( sal_Int32 nPlatform, sal_Int32 nDevice )
{
    if(nPlatform < 0 || nDevice < 0)
        throw uno::RuntimeException();

#if !HAVE_FEATURE_OPENCL
    throw uno::RuntimeException();
#else
    std::vector<OpenCLPlatformInfo> aPlatformInfo;
    sc::FormulaGroupInterpreter::fillOpenCLInfo(aPlatformInfo);
    if(o3tl::make_unsigned(nPlatform) >= aPlatformInfo.size())
        throw uno::RuntimeException();

    if(o3tl::make_unsigned(nDevice) >= aPlatformInfo[nPlatform].maDevices.size())
        throw uno::RuntimeException();

    OUString aDeviceString = aPlatformInfo[nPlatform].maVendor + " " + aPlatformInfo[nPlatform].maDevices[nDevice].maName;
    sc::FormulaGroupInterpreter::switchOpenCLDevice(aDeviceString, false);
#endif
}

sal_Int32 ScModelObj::getPlatformID()
{
#if !HAVE_FEATURE_OPENCL
    return -1;
#else
    sal_Int32 nPlatformId;
    sal_Int32 nDeviceId;
    sc::FormulaGroupInterpreter::getOpenCLDeviceInfo(nDeviceId, nPlatformId);
    return nPlatformId;
#endif
}

sal_Int32 ScModelObj::getDeviceID()
{
#if !HAVE_FEATURE_OPENCL
    return -1;
#else
    sal_Int32 nPlatformId;
    sal_Int32 nDeviceId;
    sc::FormulaGroupInterpreter::getOpenCLDeviceInfo(nDeviceId, nPlatformId);
    return nDeviceId;
#endif
}

uno::Sequence< sheet::opencl::OpenCLPlatform > ScModelObj::getOpenCLPlatforms()
{
#if !HAVE_FEATURE_OPENCL
    return uno::Sequence<sheet::opencl::OpenCLPlatform>();
#else
    std::vector<OpenCLPlatformInfo> aPlatformInfo;
    sc::FormulaGroupInterpreter::fillOpenCLInfo(aPlatformInfo);

    uno::Sequence<sheet::opencl::OpenCLPlatform> aRet(aPlatformInfo.size());
    auto aRetRange = asNonConstRange(aRet);
    for(size_t i = 0; i < aPlatformInfo.size(); ++i)
    {
        aRetRange[i].Name = aPlatformInfo[i].maName;
        aRetRange[i].Vendor = aPlatformInfo[i].maVendor;

        aRetRange[i].Devices.realloc(aPlatformInfo[i].maDevices.size());
        auto pDevices = aRetRange[i].Devices.getArray();
        for(size_t j = 0; j < aPlatformInfo[i].maDevices.size(); ++j)
        {
            const OpenCLDeviceInfo& rDevice = aPlatformInfo[i].maDevices[j];
            pDevices[j].Name = rDevice.maName;
            pDevices[j].Vendor = rDevice.maVendor;
            pDevices[j].Driver = rDevice.maDriver;
        }
    }

    return aRet;
#endif
}

namespace {

/// @throws css::uno::RuntimeException
void setOpcodeSubsetTest(bool bFlag)
{
    std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
    officecfg::Office::Calc::Formula::Calculation::OpenCLSubsetOnly::set(bFlag, batch);
    batch->commit();
}

}

void ScModelObj::enableOpcodeSubsetTest()
{
    setOpcodeSubsetTest(true);
}

void ScModelObj::disableOpcodeSubsetTest()
{
    setOpcodeSubsetTest(false);
}

sal_Bool ScModelObj::isOpcodeSubsetTested()
{
    return officecfg::Office::Calc::Formula::Calculation::OpenCLSubsetOnly::get();
}

void ScModelObj::setFormulaCellNumberLimit( sal_Int32 number )
{
    std::shared_ptr<comphelper::ConfigurationChanges> batch(comphelper::ConfigurationChanges::create());
    officecfg::Office::Calc::Formula::Calculation::OpenCLMinimumDataSize::set(number, batch);
    batch->commit();
}

sal_Int32 ScModelObj::getFormulaCellNumberLimit()
{
    return officecfg::Office::Calc::Formula::Calculation::OpenCLMinimumDataSize::get();
}

ScDrawPagesObj::ScDrawPagesObj(ScDocShell* pDocSh) :
    pDocShell( pDocSh )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScDrawPagesObj::~ScDrawPagesObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScDrawPagesObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    //  we don't care about update of references here

    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

uno::Reference<drawing::XDrawPage> ScDrawPagesObj::GetObjectByIndex_Impl(sal_Int32 nIndex) const
{
    if (pDocShell)
    {
        ScDrawLayer* pDrawLayer = pDocShell->MakeDrawLayer();
        OSL_ENSURE(pDrawLayer,"Cannot create Draw-Layer");
        if ( pDrawLayer && nIndex >= 0 && nIndex < pDocShell->GetDocument().GetTableCount() )
        {
            SdrPage* pPage = pDrawLayer->GetPage(static_cast<sal_uInt16>(nIndex));
            OSL_ENSURE(pPage,"Draw-Page not found");
            if (pPage)
            {
                return uno::Reference<drawing::XDrawPage> (pPage->getUnoPage(), uno::UNO_QUERY);
            }
        }
    }
    return nullptr;
}

// XDrawPages

uno::Reference<drawing::XDrawPage> SAL_CALL ScDrawPagesObj::insertNewByIndex( sal_Int32 nPos )
{
    SolarMutexGuard aGuard;
    uno::Reference<drawing::XDrawPage> xRet;
    if (pDocShell)
    {
        OUString aNewName;
        pDocShell->GetDocument().CreateValidTabName(aNewName);
        if ( pDocShell->GetDocFunc().InsertTable( static_cast<SCTAB>(nPos),
                                                  aNewName, true, true ) )
            xRet.set(GetObjectByIndex_Impl( nPos ));
    }
    return xRet;
}

void SAL_CALL ScDrawPagesObj::remove( const uno::Reference<drawing::XDrawPage>& xPage )
{
    SolarMutexGuard aGuard;
    SvxDrawPage* pImp = comphelper::getFromUnoTunnel<SvxDrawPage>( xPage );
    if ( pDocShell && pImp )
    {
        SdrPage* pPage = pImp->GetSdrPage();
        if (pPage)
        {
            SCTAB nPageNum = static_cast<SCTAB>(pPage->GetPageNum());
            pDocShell->GetDocFunc().DeleteTable( nPageNum, true );
        }
    }
}

// XIndexAccess

sal_Int32 SAL_CALL ScDrawPagesObj::getCount()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return pDocShell->GetDocument().GetTableCount();
    return 0;
}

uno::Any SAL_CALL ScDrawPagesObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    uno::Reference<drawing::XDrawPage> xPage(GetObjectByIndex_Impl(nIndex));
    if (!xPage.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(xPage);
}

uno::Type SAL_CALL ScDrawPagesObj::getElementType()
{
    return cppu::UnoType<drawing::XDrawPage>::get();
}

sal_Bool SAL_CALL ScDrawPagesObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

ScTableSheetsObj::ScTableSheetsObj(ScDocShell* pDocSh) :
    pDocShell( pDocSh )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScTableSheetsObj::~ScTableSheetsObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScTableSheetsObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    // we don't care about update of references here

    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

// XSpreadsheets

rtl::Reference<ScTableSheetObj> ScTableSheetsObj::GetSheetByIndex(sal_Int32 nIndex) const
{
    if ( pDocShell && nIndex >= 0 && nIndex < pDocShell->GetDocument().GetTableCount() )
        return new ScTableSheetObj( pDocShell, static_cast<SCTAB>(nIndex) );

    return nullptr;
}

rtl::Reference<ScTableSheetObj> ScTableSheetsObj::GetObjectByName_Impl(const OUString& aName) const
{
    if (pDocShell)
    {
        SCTAB nIndex;
        if ( pDocShell->GetDocument().GetTable( aName, nIndex ) )
            return new ScTableSheetObj( pDocShell, nIndex );
    }
    return nullptr;
}

void SAL_CALL ScTableSheetsObj::insertNewByName( const OUString& aName, sal_Int16 nPosition )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        bDone = pDocShell->GetDocFunc().InsertTable( nPosition, aName, true, true );
    }
    if (!bDone)
        throw uno::RuntimeException(u"ScTableSheetsObj::insertNewByName(): Illegal object name or bad index. Duplicate name?"_ustr);      // no other exceptions specified
}

void SAL_CALL ScTableSheetsObj::moveByName( const OUString& aName, sal_Int16 nDestination )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        SCTAB nSource;
        if ( pDocShell->GetDocument().GetTable( aName, nSource ) )
            bDone = pDocShell->MoveTable( nSource, nDestination, false, true );
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

void SAL_CALL ScTableSheetsObj::copyByName( const OUString& aName,
    const OUString& aCopy, sal_Int16 nDestination )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        SCTAB nSource;
        if ( pDocShell->GetDocument().GetTable( aName, nSource ) )
        {
            bDone = pDocShell->MoveTable( nSource, nDestination, true, true );
            if (bDone)
            {
                // #i92477# any index past the last sheet means "append" in MoveTable
                SCTAB nResultTab = static_cast<SCTAB>(nDestination);
                SCTAB nTabCount = pDocShell->GetDocument().GetTableCount();    // count after copying
                if (nResultTab >= nTabCount)
                    nResultTab = nTabCount - 1;

                bDone = pDocShell->GetDocFunc().RenameTable( nResultTab, aCopy,
                                                             true, true );
            }
        }
    }
    if (!bDone)
        throw uno::RuntimeException(u"ScTableSheetsObj::copyByName(): Illegal object name or bad index. Duplicate name?"_ustr);      // no other exceptions specified
}

void SAL_CALL ScTableSheetsObj::insertByName( const OUString& aName, const uno::Any& aElement )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    bool bIllArg = false;

    //! Type of aElement can be some specific interface instead of XInterface

    if ( pDocShell )
    {
        uno::Reference<uno::XInterface> xInterface(aElement, uno::UNO_QUERY);
        if ( xInterface.is() )
        {
            ScTableSheetObj* pSheetObj = dynamic_cast<ScTableSheetObj*>( xInterface.get() );
            if ( pSheetObj && !pSheetObj->GetDocShell() )   // not inserted yet?
            {
                ScDocument& rDoc = pDocShell->GetDocument();
                SCTAB nDummy;
                if ( rDoc.GetTable( aName, nDummy ) )
                {
                    //  name already exists
                    throw container::ElementExistException();
                }
                SCTAB nPosition = rDoc.GetTableCount();
                bDone = pDocShell->GetDocFunc().InsertTable( nPosition, aName,
                                                             true, true );
                if (bDone)
                    pSheetObj->InitInsertSheet( pDocShell, nPosition );
                //  set document and new range in the object
            }
            else
                bIllArg = true;
        }
        else
            bIllArg = true;
    }

    if (!bDone)
    {
        if (bIllArg)
            throw lang::IllegalArgumentException();
        else
            throw uno::RuntimeException();      // ElementExistException is handled above
    }
}

void SAL_CALL ScTableSheetsObj::replaceByName( const OUString& aName, const uno::Any& aElement )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    bool bIllArg = false;

    //! Type of aElement can be some specific interface instead of XInterface

    if ( pDocShell )
    {
        uno::Reference<uno::XInterface> xInterface(aElement, uno::UNO_QUERY);
        if ( xInterface.is() )
        {
            ScTableSheetObj* pSheetObj = dynamic_cast<ScTableSheetObj*>( xInterface.get() );
            if ( pSheetObj && !pSheetObj->GetDocShell() )   // not inserted yet?
            {
                SCTAB nPosition;
                if ( !pDocShell->GetDocument().GetTable( aName, nPosition ) )
                {
                    //  not found
                    throw container::NoSuchElementException();
                }

                if ( pDocShell->GetDocFunc().DeleteTable( nPosition, true ) )
                {
                    //  InsertTable can't really go wrong now
                    bDone = pDocShell->GetDocFunc().InsertTable( nPosition, aName, true, true );
                    if (bDone)
                        pSheetObj->InitInsertSheet( pDocShell, nPosition );
                }

            }
            else
                bIllArg = true;
        }
        else
            bIllArg = true;
    }

    if (!bDone)
    {
        if (bIllArg)
            throw lang::IllegalArgumentException();
        else
            throw uno::RuntimeException();      // NoSuchElementException is handled above
    }
}

void SAL_CALL ScTableSheetsObj::removeByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if (pDocShell)
    {
        SCTAB nIndex;
        if ( !pDocShell->GetDocument().GetTable( aName, nIndex ) )
            throw container::NoSuchElementException(); // not found
        bDone = pDocShell->GetDocFunc().DeleteTable( nIndex, true );
    }

    if (!bDone)
        throw uno::RuntimeException();      // NoSuchElementException is handled above
}

sal_Int32 ScTableSheetsObj::importSheet(
    const uno::Reference < sheet::XSpreadsheetDocument > & xDocSrc,
    const OUString& srcName, const sal_Int32 nDestPosition )
{
    //pDocShell is the destination
    ScDocument& rDocDest = pDocShell->GetDocument();

    // Source document docShell
    if ( !xDocSrc.is() )
        throw uno::RuntimeException();
    ScModelObj* pObj = comphelper::getFromUnoTunnel<ScModelObj>(xDocSrc);
    ScDocShell* pDocShellSrc = static_cast<ScDocShell*>(pObj->GetEmbeddedObject());

    // SourceSheet Position and does srcName exists ?
    SCTAB nIndexSrc;
    if ( !pDocShellSrc->GetDocument().GetTable( srcName, nIndexSrc ) )
        throw lang::IllegalArgumentException();

    // Check the validity of destination index.
    SCTAB nCount = rDocDest.GetTableCount();
    SCTAB nIndexDest = static_cast<SCTAB>(nDestPosition);
    if (nIndexDest > nCount || nIndexDest < 0)
        throw lang::IndexOutOfBoundsException();

    // Transfer Tab
    pDocShell->TransferTab(
        *pDocShellSrc, nIndexSrc, nIndexDest, true/*bInsertNew*/, true/*bNotifyAndPaint*/ );

    return nIndexDest;
}

// XCellRangesAccess

uno::Reference< table::XCell > SAL_CALL ScTableSheetsObj::getCellByPosition( sal_Int32 nColumn, sal_Int32 nRow, sal_Int32 nSheet )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xSheet = GetSheetByIndex(static_cast<sal_uInt16>(nSheet));
    if (! xSheet.is())
        throw lang::IndexOutOfBoundsException();

    return xSheet->getCellByPosition(nColumn, nRow);
}

uno::Reference< table::XCellRange > SAL_CALL ScTableSheetsObj::getCellRangeByPosition( sal_Int32 nLeft, sal_Int32 nTop, sal_Int32 nRight, sal_Int32 nBottom, sal_Int32 nSheet )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xSheet = GetSheetByIndex(static_cast<sal_uInt16>(nSheet));
    if (! xSheet.is())
        throw lang::IndexOutOfBoundsException();

    return xSheet->getCellRangeByPosition(nLeft, nTop, nRight, nBottom);
}

uno::Sequence < uno::Reference< table::XCellRange > > SAL_CALL ScTableSheetsObj::getCellRangesByName( const OUString& aRange )
{
    SolarMutexGuard aGuard;
    uno::Sequence < uno::Reference < table::XCellRange > > xRet;

    ScRangeList aRangeList;
    ScDocument& rDoc = pDocShell->GetDocument();
    if (!ScRangeStringConverter::GetRangeListFromString( aRangeList, aRange, rDoc, ::formula::FormulaGrammar::CONV_OOO, ';' ))
        throw lang::IllegalArgumentException();

    size_t nCount = aRangeList.size();
    if (!nCount)
        throw lang::IllegalArgumentException();

    xRet.realloc(nCount);
    auto pRet = xRet.getArray();
    for( size_t nIndex = 0; nIndex < nCount; nIndex++ )
    {
        const ScRange & rRange = aRangeList[ nIndex ];
        pRet[nIndex] = new ScCellRangeObj(pDocShell, rRange);
    }

    return xRet;
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScTableSheetsObj::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.SpreadsheetsEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScTableSheetsObj::getCount()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
        return pDocShell->GetDocument().GetTableCount();
    return 0;
}

uno::Any SAL_CALL ScTableSheetsObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xSheet(GetSheetByIndex(nIndex));
    if (!xSheet.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<sheet::XSpreadsheet>(xSheet));

//    return uno::Any();
}

uno::Type SAL_CALL ScTableSheetsObj::getElementType()
{
    return cppu::UnoType<sheet::XSpreadsheet>::get();
}

sal_Bool SAL_CALL ScTableSheetsObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

// XNameAccess

uno::Any SAL_CALL ScTableSheetsObj::getByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xSheet(GetObjectByName_Impl(aName));
    if (!xSheet.is())
        throw container::NoSuchElementException();

    return uno::Any(uno::Reference<sheet::XSpreadsheet>(xSheet));
}

uno::Sequence<OUString> SAL_CALL ScTableSheetsObj::getElementNames()
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        ScDocument& rDoc = pDocShell->GetDocument();
        SCTAB nCount = rDoc.GetTableCount();
        OUString aName;
        uno::Sequence<OUString> aSeq(nCount);
        OUString* pAry = aSeq.getArray();
        for (SCTAB i=0; i<nCount; i++)
        {
            rDoc.GetName( i, aName );
            pAry[i] = aName;
        }
        return aSeq;
    }
    return uno::Sequence<OUString>();
}

sal_Bool SAL_CALL ScTableSheetsObj::hasByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        SCTAB nIndex;
        if ( pDocShell->GetDocument().GetTable( aName, nIndex ) )
            return true;
    }
    return false;
}

ScTableColumnsObj::ScTableColumnsObj(ScDocShell* pDocSh, SCTAB nT, SCCOL nSC, SCCOL nEC) :
    pDocShell( pDocSh ),
    nTab     ( nT ),
    nStartCol( nSC ),
    nEndCol  ( nEC )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScTableColumnsObj::~ScTableColumnsObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScTableColumnsObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::ScUpdateRef )
    {
        //! update of references for sheet and its start/end
    }
    else if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

// XTableColumns

rtl::Reference<ScTableColumnObj> ScTableColumnsObj::GetObjectByIndex_Impl(sal_Int32 nIndex) const
{
    SCCOL nCol = static_cast<SCCOL>(nIndex) + nStartCol;
    if ( pDocShell && nCol <= nEndCol )
        return new ScTableColumnObj( pDocShell, nCol, nTab );

    return nullptr;    // wrong index
}

rtl::Reference<ScTableColumnObj> ScTableColumnsObj::GetObjectByName_Impl(std::u16string_view aName) const
{
    SCCOL nCol = 0;
    if (pDocShell && ::AlphaToCol(pDocShell->GetDocument(), nCol, aName))
        if (nCol >= nStartCol && nCol <= nEndCol)
            return new ScTableColumnObj( pDocShell, nCol, nTab );

    return nullptr;
}

void SAL_CALL ScTableColumnsObj::insertByIndex( sal_Int32 nPosition, sal_Int32 nCount )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if ( pDocShell )
    {
        const ScDocument& rDoc = pDocShell->GetDocument();
        if ( nCount > 0 && nPosition >= 0 && nStartCol+nPosition <= nEndCol &&
            nStartCol+nPosition+nCount-1 <= rDoc.MaxCol() )
        {
            ScRange aRange( static_cast<SCCOL>(nStartCol+nPosition), 0, nTab,
                            static_cast<SCCOL>(nStartCol+nPosition+nCount-1), rDoc.MaxRow(), nTab );
            bDone = pDocShell->GetDocFunc().InsertCells( aRange, nullptr, INS_INSCOLS_BEFORE, true, true );
        }
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

void SAL_CALL ScTableColumnsObj::removeByIndex( sal_Int32 nIndex, sal_Int32 nCount )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    //  the range to be deleted has to lie within the object
    if ( pDocShell )
    {
        const ScDocument& rDoc = pDocShell->GetDocument();
        if ( nCount > 0 && nIndex >= 0 && nStartCol+nIndex+nCount-1 <= nEndCol )
        {
            ScRange aRange( static_cast<SCCOL>(nStartCol+nIndex), 0, nTab,
                            static_cast<SCCOL>(nStartCol+nIndex+nCount-1), rDoc.MaxRow(), nTab );
            bDone = pDocShell->GetDocFunc().DeleteCells( aRange, nullptr, DelCellCmd::Cols, true );
        }
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScTableColumnsObj::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.table.TableColumnsEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScTableColumnsObj::getCount()
{
    SolarMutexGuard aGuard;
    return nEndCol - nStartCol + 1;
}

uno::Any SAL_CALL ScTableColumnsObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableColumnObj> xColumn(GetObjectByIndex_Impl(nIndex));
    if (!xColumn.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<table::XCellRange>(xColumn));

}

uno::Type SAL_CALL ScTableColumnsObj::getElementType()
{
    return cppu::UnoType<table::XCellRange>::get();
}

sal_Bool SAL_CALL ScTableColumnsObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

uno::Any SAL_CALL ScTableColumnsObj::getByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableColumnObj> xColumn(GetObjectByName_Impl(aName));
    if (!xColumn.is())
        throw container::NoSuchElementException();

    return uno::Any(uno::Reference<table::XCellRange>(xColumn));
}

uno::Sequence<OUString> SAL_CALL ScTableColumnsObj::getElementNames()
{
    SolarMutexGuard aGuard;
    SCCOL nCount = nEndCol - nStartCol + 1;
    uno::Sequence<OUString> aSeq(nCount);
    OUString* pAry = aSeq.getArray();
    for (SCCOL i=0; i<nCount; i++)
        pAry[i] = ::ScColToAlpha( nStartCol + i );

    return aSeq;
}

sal_Bool SAL_CALL ScTableColumnsObj::hasByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    SCCOL nCol = 0;
    if (pDocShell && ::AlphaToCol(pDocShell->GetDocument(), nCol, aName))
        if (nCol >= nStartCol && nCol <= nEndCol)
            return true;

    return false;       // not found
}

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScTableColumnsObj::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( lcl_GetColumnsPropertyMap() ));
    return aRef;
}

void SAL_CALL ScTableColumnsObj::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
        throw uno::RuntimeException();

    std::vector<sc::ColRowSpan> aColArr(1, sc::ColRowSpan(nStartCol,nEndCol));
    ScDocFunc& rFunc = pDocShell->GetDocFunc();

    if ( aPropertyName == SC_UNONAME_CELLWID )
    {
        sal_Int32 nNewWidth = 0;
        if ( aValue >>= nNewWidth )
            rFunc.SetWidthOrHeight(
                true, aColArr, nTab, SC_SIZE_ORIGINAL, o3tl::toTwips(nNewWidth, o3tl::Length::mm100), true, true);
    }
    else if ( aPropertyName == SC_UNONAME_CELLVIS )
    {
        bool bVis = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        ScSizeMode eMode = bVis ? SC_SIZE_SHOW : SC_SIZE_DIRECT;
        rFunc.SetWidthOrHeight(true, aColArr, nTab, eMode, 0, true, true);
        //  SC_SIZE_DIRECT with size 0: hide
    }
    else if ( aPropertyName == SC_UNONAME_OWIDTH )
    {
        bool bOpt = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        if (bOpt)
            rFunc.SetWidthOrHeight(
                true, aColArr, nTab, SC_SIZE_OPTIMAL, STD_EXTRA_WIDTH, true, true);
        // sal_False for columns currently has no effect
    }
    else if ( aPropertyName == SC_UNONAME_NEWPAGE || aPropertyName == SC_UNONAME_MANPAGE )
    {
        //! single function to set/remove all breaks?
        bool bSet = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        for (SCCOL nCol=nStartCol; nCol<=nEndCol; nCol++)
            if (bSet)
                rFunc.InsertPageBreak( true, ScAddress(nCol,0,nTab), true, true );
            else
                rFunc.RemovePageBreak( true, ScAddress(nCol,0,nTab), true, true );
    }
}

uno::Any SAL_CALL ScTableColumnsObj::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
        throw uno::RuntimeException();

    ScDocument& rDoc = pDocShell->GetDocument();
    uno::Any aAny;

    //! loop over all columns for current state?

    if ( aPropertyName == SC_UNONAME_CELLWID )
    {
        // for hidden column, return original height
        sal_uInt16 nWidth = rDoc.GetOriginalWidth( nStartCol, nTab );
        aAny <<= static_cast<sal_Int32>(convertTwipToMm100(nWidth));
    }
    else if ( aPropertyName == SC_UNONAME_CELLVIS )
    {
        bool bVis = !rDoc.ColHidden(nStartCol, nTab);
        aAny <<= bVis;
    }
    else if ( aPropertyName == SC_UNONAME_OWIDTH )
    {
        bool bOpt = !(rDoc.GetColFlags( nStartCol, nTab ) & CRFlags::ManualSize);
        aAny <<= bOpt;
    }
    else if ( aPropertyName == SC_UNONAME_NEWPAGE )
    {
        ScBreakType nBreak = rDoc.HasColBreak(nStartCol, nTab);
        aAny <<= (nBreak != ScBreakType::NONE);
    }
    else if ( aPropertyName == SC_UNONAME_MANPAGE )
    {
        ScBreakType nBreak = rDoc.HasColBreak(nStartCol, nTab);
        aAny <<= bool(nBreak & ScBreakType::Manual);
    }

    return aAny;
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScTableColumnsObj )

ScTableRowsObj::ScTableRowsObj(ScDocShell* pDocSh, SCTAB nT, SCROW nSR, SCROW nER) :
    pDocShell( pDocSh ),
    nTab     ( nT ),
    nStartRow( nSR ),
    nEndRow  ( nER )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScTableRowsObj::~ScTableRowsObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScTableRowsObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::ScUpdateRef )
    {
        //! update of references for sheet and its start/end
    }
    else if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

// XTableRows

rtl::Reference<ScTableRowObj> ScTableRowsObj::GetObjectByIndex_Impl(sal_Int32 nIndex) const
{
    SCROW nRow = static_cast<SCROW>(nIndex) + nStartRow;
    if ( pDocShell && nRow <= nEndRow )
        return new ScTableRowObj( pDocShell, nRow, nTab );

    return nullptr;    // wrong index
}

void SAL_CALL ScTableRowsObj::insertByIndex( sal_Int32 nPosition, sal_Int32 nCount )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    if ( pDocShell )
    {
        const ScDocument& rDoc = pDocShell->GetDocument();
        if ( nCount > 0 && nPosition >= 0 && nStartRow+nPosition <= nEndRow &&
            nStartRow+nPosition+nCount-1 <= rDoc.MaxRow() )
        {
            ScRange aRange( 0, static_cast<SCROW>(nStartRow+nPosition), nTab,
                            rDoc.MaxCol(), static_cast<SCROW>(nStartRow+nPosition+nCount-1), nTab );
            bDone = pDocShell->GetDocFunc().InsertCells( aRange, nullptr, INS_INSROWS_BEFORE, true, true );
        }
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

void SAL_CALL ScTableRowsObj::removeByIndex( sal_Int32 nIndex, sal_Int32 nCount )
{
    SolarMutexGuard aGuard;
    bool bDone = false;
    // the range to be deleted has to lie within the object
    if ( pDocShell && nCount > 0 && nIndex >= 0 && nStartRow+nIndex+nCount-1 <= nEndRow )
    {
        const ScDocument& rDoc = pDocShell->GetDocument();
        ScRange aRange( 0, static_cast<SCROW>(nStartRow+nIndex), nTab,
                        rDoc.MaxCol(), static_cast<SCROW>(nStartRow+nIndex+nCount-1), nTab );
        bDone = pDocShell->GetDocFunc().DeleteCells( aRange, nullptr, DelCellCmd::Rows, true );
    }
    if (!bDone)
        throw uno::RuntimeException();      // no other exceptions specified
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScTableRowsObj::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.table.TableRowsEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScTableRowsObj::getCount()
{
    SolarMutexGuard aGuard;
    return nEndRow - nStartRow + 1;
}

uno::Any SAL_CALL ScTableRowsObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableRowObj> xRow(GetObjectByIndex_Impl(nIndex));
    if (!xRow.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<table::XCellRange>(xRow));
}

uno::Type SAL_CALL ScTableRowsObj::getElementType()
{
    return cppu::UnoType<table::XCellRange>::get();
}

sal_Bool SAL_CALL ScTableRowsObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScTableRowsObj::getPropertySetInfo()
{
    static uno::Reference<beans::XPropertySetInfo> aRef(
        new SfxItemPropertySetInfo( lcl_GetRowsPropertyMap() ));
    return aRef;
}

void SAL_CALL ScTableRowsObj::setPropertyValue(
                        const OUString& aPropertyName, const uno::Any& aValue )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
        throw uno::RuntimeException();

    ScDocFunc& rFunc = pDocShell->GetDocFunc();
    ScDocument& rDoc = pDocShell->GetDocument();
    std::vector<sc::ColRowSpan> aRowArr(1, sc::ColRowSpan(nStartRow,nEndRow));

    if ( aPropertyName == SC_UNONAME_OHEIGHT )
    {
        sal_Int32 nNewHeight = 0;
        if ( rDoc.IsImportingXML() && ( aValue >>= nNewHeight ) )
        {
            // used to set the stored row height for rows with optimal height when loading.

            // TODO: It's probably cleaner to use a different property name
            // for this.
            rDoc.SetRowHeightOnly( nStartRow, nEndRow, nTab, o3tl::toTwips(nNewHeight, o3tl::Length::mm100) );
        }
        else
        {
            bool bOpt = ScUnoHelpFunctions::GetBoolFromAny( aValue );
            if (bOpt)
                rFunc.SetWidthOrHeight(false, aRowArr, nTab, SC_SIZE_OPTIMAL, 0, true, true);
            else
            {
                //! manually set old heights again?
            }
        }
    }
    else if ( aPropertyName == SC_UNONAME_CELLHGT )
    {
        sal_Int32 nNewHeight = 0;
        if ( aValue >>= nNewHeight )
        {
            if (rDoc.IsImportingXML())
            {
                // TODO: This is a band-aid fix.  Eventually we need to
                // re-work ods' style import to get it to set styles to
                // ScDocument directly.
                rDoc.SetRowHeightOnly( nStartRow, nEndRow, nTab, o3tl::toTwips(nNewHeight, o3tl::Length::mm100) );
                rDoc.SetManualHeight( nStartRow, nEndRow, nTab, true );
            }
            else
                rFunc.SetWidthOrHeight(
                    false, aRowArr, nTab, SC_SIZE_ORIGINAL, o3tl::toTwips(nNewHeight, o3tl::Length::mm100), true, true);
        }
    }
    else if ( aPropertyName == SC_UNONAME_CELLVIS )
    {
        bool bVis = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        ScSizeMode eMode = bVis ? SC_SIZE_SHOW : SC_SIZE_DIRECT;
        rFunc.SetWidthOrHeight(false, aRowArr, nTab, eMode, 0, true, true);
        //  SC_SIZE_DIRECT with size 0: hide
    }
    else if ( aPropertyName == SC_UNONAME_VISFLAG )
    {
        // #i116460# Shortcut to only set the flag, without drawing layer update etc.
        // Should only be used from import filters.
        rDoc.SetRowHidden(nStartRow, nEndRow, nTab, !ScUnoHelpFunctions::GetBoolFromAny( aValue ));
    }
    else if ( aPropertyName == SC_UNONAME_CELLFILT )
    {
        //! undo etc.
        if (ScUnoHelpFunctions::GetBoolFromAny( aValue ))
            rDoc.SetRowFiltered(nStartRow, nEndRow, nTab, true);
        else
            rDoc.SetRowFiltered(nStartRow, nEndRow, nTab, false);
    }
    else if ( aPropertyName == SC_UNONAME_NEWPAGE || aPropertyName == SC_UNONAME_MANPAGE )
    {
        //! single function to set/remove all breaks?
        bool bSet = ScUnoHelpFunctions::GetBoolFromAny( aValue );
        for (SCROW nRow=nStartRow; nRow<=nEndRow; nRow++)
            if (bSet)
                rFunc.InsertPageBreak( false, ScAddress(0,nRow,nTab), true, true );
            else
                rFunc.RemovePageBreak( false, ScAddress(0,nRow,nTab), true, true );
    }
    else if ( aPropertyName == SC_UNONAME_CELLBACK || aPropertyName == SC_UNONAME_CELLTRAN )
    {
        // #i57867# Background color is specified for row styles in the file format,
        // so it has to be supported along with the row properties (import only).

        // Use ScCellRangeObj to set the property for all cells in the rows
        // (this means, the "row attribute" must be set before individual cell attributes).

        ScRange aRange( 0, nStartRow, nTab, rDoc.MaxCol(), nEndRow, nTab );
        uno::Reference<beans::XPropertySet> xRangeObj = new ScCellRangeObj( pDocShell, aRange );
        xRangeObj->setPropertyValue( aPropertyName, aValue );
    }
}

void ScTableRowsObj::setPropertyValueIsFiltered(SolarMutexGuard& /*rGuard*/, bool b )
{
    ScDocument& rDoc = pDocShell->GetDocument();
    //! undo etc.
    rDoc.SetRowFiltered(nStartRow, nEndRow, nTab, b);
}

uno::Any SAL_CALL ScTableRowsObj::getPropertyValue( const OUString& aPropertyName )
{
    SolarMutexGuard aGuard;
    if (!pDocShell)
        throw uno::RuntimeException();

    ScDocument& rDoc = pDocShell->GetDocument();
    uno::Any aAny;

    //! loop over all rows for current state?

    if ( aPropertyName == SC_UNONAME_CELLHGT )
    {
        // for hidden row, return original height
        sal_uInt16 nHeight = rDoc.GetOriginalHeight( nStartRow, nTab );
        aAny <<= static_cast<sal_Int32>(convertTwipToMm100(nHeight));
    }
    else if ( aPropertyName == SC_UNONAME_CELLVIS )
    {
        SCROW nLastRow;
        bool bVis = !rDoc.RowHidden(nStartRow, nTab, nullptr, &nLastRow);
        aAny <<= bVis;
    }
    else if ( aPropertyName == SC_UNONAME_CELLFILT )
    {
        bool bVis = rDoc.RowFiltered(nStartRow, nTab);
        aAny <<= bVis;
    }
    else if ( aPropertyName == SC_UNONAME_OHEIGHT )
    {
        bool bOpt = !(rDoc.GetRowFlags( nStartRow, nTab ) & CRFlags::ManualSize);
        aAny <<= bOpt;
    }
    else if ( aPropertyName == SC_UNONAME_NEWPAGE )
    {
        ScBreakType nBreak = rDoc.HasRowBreak(nStartRow, nTab);
        aAny <<= (nBreak != ScBreakType::NONE);
    }
    else if ( aPropertyName == SC_UNONAME_MANPAGE )
    {
        ScBreakType nBreak = rDoc.HasRowBreak(nStartRow, nTab);
        aAny <<= bool(nBreak & ScBreakType::Manual);
    }
    else if ( aPropertyName == SC_UNONAME_CELLBACK || aPropertyName == SC_UNONAME_CELLTRAN )
    {
        // Use ScCellRangeObj to get the property from the cell range
        // (for completeness only, this is not used by the XML filter).

        ScRange aRange( 0, nStartRow, nTab, rDoc.MaxCol(), nEndRow, nTab );
        uno::Reference<beans::XPropertySet> xRangeObj = new ScCellRangeObj( pDocShell, aRange );
        aAny = xRangeObj->getPropertyValue( aPropertyName );
    }

    return aAny;
}

bool ScTableRowsObj::getPropertyValueOHeight( SolarMutexGuard& /*rGuard*/ )
{
    ScDocument& rDoc = pDocShell->GetDocument();
    return !(rDoc.GetRowFlags( nStartRow, nTab ) & CRFlags::ManualSize);
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScTableRowsObj )

ScSpreadsheetSettingsObj::~ScSpreadsheetSettingsObj()
{
}

// XPropertySet

uno::Reference<beans::XPropertySetInfo> SAL_CALL ScSpreadsheetSettingsObj::getPropertySetInfo()
{
    return nullptr;
}

void SAL_CALL ScSpreadsheetSettingsObj::setPropertyValue(
                        const OUString& /* aPropertyName */, const uno::Any& /* aValue */ )
{
}

uno::Any SAL_CALL ScSpreadsheetSettingsObj::getPropertyValue( const OUString& /* aPropertyName */ )
{
    return uno::Any();
}

SC_IMPL_DUMMY_PROPERTY_LISTENER( ScSpreadsheetSettingsObj )

ScAnnotationsObj::ScAnnotationsObj(ScDocShell* pDocSh, SCTAB nT) :
    pDocShell( pDocSh ),
    nTab( nT )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScAnnotationsObj::~ScAnnotationsObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScAnnotationsObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    //! adjust nTab when updating references!!!

    if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

bool ScAnnotationsObj::GetAddressByIndex_Impl( sal_Int32 nIndex, ScAddress& rPos ) const
{
    if (!pDocShell)
        return false;

    ScDocument& rDoc = pDocShell->GetDocument();
    rPos = rDoc.GetNotePosition(nIndex, nTab);
    return rPos.IsValid();
}

rtl::Reference<ScAnnotationObj> ScAnnotationsObj::GetObjectByIndex_Impl( sal_Int32 nIndex ) const
{
    if (pDocShell)
    {
        ScAddress aPos;
        if ( GetAddressByIndex_Impl( nIndex, aPos ) )
            return new ScAnnotationObj( pDocShell, aPos );
    }
    return nullptr;
}

// XSheetAnnotations

void SAL_CALL ScAnnotationsObj::insertNew(
        const table::CellAddress& aPosition, const OUString& rText )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        OSL_ENSURE( aPosition.Sheet == nTab, "addAnnotation with a wrong Sheet" );
        ScAddress aPos( static_cast<SCCOL>(aPosition.Column), static_cast<SCROW>(aPosition.Row), nTab );
        pDocShell->GetDocFunc().ReplaceNote( aPos, rText, nullptr, nullptr, true );
    }
}

void SAL_CALL ScAnnotationsObj::removeByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    if (pDocShell)
    {
        ScAddress aPos;
        if ( GetAddressByIndex_Impl( nIndex, aPos ) )
        {
            ScMarkData aMarkData(pDocShell->GetDocument().GetSheetLimits());
            aMarkData.SelectTable( aPos.Tab(), true );
            aMarkData.SetMultiMarkArea( ScRange(aPos) );

            pDocShell->GetDocFunc().DeleteContents( aMarkData, InsertDeleteFlags::NOTE, true, true );
        }
    }
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScAnnotationsObj::createEnumeration()
{
    //! iterate directly (more efficiently)?

    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.CellAnnotationsEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScAnnotationsObj::getCount()
{
    SolarMutexGuard aGuard;
    sal_Int32 nCount = 0;
    if (pDocShell)
    {
        const ScDocument& rDoc = pDocShell->GetDocument();
        for (SCCOL nCol : rDoc.GetAllocatedColumnsRange(nTab, 0, rDoc.MaxCol()))
            nCount += rDoc.GetNoteCount(nTab, nCol);
    }
    return nCount;
}

uno::Any SAL_CALL ScAnnotationsObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScAnnotationObj> xAnnotation(GetObjectByIndex_Impl(nIndex));
    if (!xAnnotation.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<sheet::XSheetAnnotation>(xAnnotation));
}

uno::Type SAL_CALL ScAnnotationsObj::getElementType()
{
    return cppu::UnoType<sheet::XSheetAnnotation>::get();
}

sal_Bool SAL_CALL ScAnnotationsObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

ScScenariosObj::ScScenariosObj(ScDocShell* pDocSh, SCTAB nT) :
    pDocShell( pDocSh ),
    nTab     ( nT )
{
    pDocShell->GetDocument().AddUnoObject(*this);
}

ScScenariosObj::~ScScenariosObj()
{
    SolarMutexGuard g;

    if (pDocShell)
        pDocShell->GetDocument().RemoveUnoObject(*this);
}

void ScScenariosObj::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    if ( rHint.GetId() == SfxHintId::ScUpdateRef )
    {
        //! update of references for sheet and its start/end
    }
    else if ( rHint.GetId() == SfxHintId::Dying )
    {
        pDocShell = nullptr;       // became invalid
    }
}

// XScenarios

bool ScScenariosObj::GetScenarioIndex_Impl( std::u16string_view rName, SCTAB& rIndex )
{
    //! Case-insensitive ????

    if ( pDocShell )
    {
        OUString aTabName;
        ScDocument& rDoc = pDocShell->GetDocument();
        SCTAB nCount = static_cast<SCTAB>(getCount());
        for (SCTAB i=0; i<nCount; i++)
            if (rDoc.GetName( nTab+i+1, aTabName ))
                if (aTabName == rName)
                {
                    rIndex = i;
                    return true;
                }
    }

    return false;
}

rtl::Reference<ScTableSheetObj> ScScenariosObj::GetObjectByIndex_Impl(sal_Int32 nIndex)
{
    sal_uInt16 nCount = static_cast<sal_uInt16>(getCount());
    if ( pDocShell && nIndex >= 0 && nIndex < nCount )
        return new ScTableSheetObj( pDocShell, nTab+static_cast<SCTAB>(nIndex)+1 );

    return nullptr;    // no document or wrong index
}

rtl::Reference<ScTableSheetObj> ScScenariosObj::GetObjectByName_Impl(std::u16string_view aName)
{
    SCTAB nIndex;
    if ( pDocShell && GetScenarioIndex_Impl( aName, nIndex ) )
        return new ScTableSheetObj( pDocShell, nTab+nIndex+1 );

    return nullptr;    // not found
}

void SAL_CALL ScScenariosObj::addNewByName( const OUString& aName,
                                const uno::Sequence<table::CellRangeAddress>& aRanges,
                                const OUString& aComment )
{
    SolarMutexGuard aGuard;
    if ( !pDocShell )
        return;

    ScMarkData aMarkData(pDocShell->GetDocument().GetSheetLimits());
    aMarkData.SelectTable( nTab, true );

    for (const table::CellRangeAddress& rRange : aRanges)
    {
        OSL_ENSURE( rRange.Sheet == nTab, "addScenario with a wrong Tab" );
        ScRange aRange( static_cast<SCCOL>(rRange.StartColumn), static_cast<SCROW>(rRange.StartRow), nTab,
                        static_cast<SCCOL>(rRange.EndColumn),   static_cast<SCROW>(rRange.EndRow),   nTab );

        aMarkData.SetMultiMarkArea( aRange );
    }

    ScScenarioFlags const nFlags = ScScenarioFlags::ShowFrame | ScScenarioFlags::PrintFrame
                                 | ScScenarioFlags::TwoWay    | ScScenarioFlags::Protected;

    pDocShell->MakeScenario( nTab, aName, aComment, COL_LIGHTGRAY, nFlags, aMarkData );
}

void SAL_CALL ScScenariosObj::removeByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    SCTAB nIndex;
    if ( pDocShell && GetScenarioIndex_Impl( aName, nIndex ) )
        pDocShell->GetDocFunc().DeleteTable( nTab+nIndex+1, true );
}

// XEnumerationAccess

uno::Reference<container::XEnumeration> SAL_CALL ScScenariosObj::createEnumeration()
{
    SolarMutexGuard aGuard;
    return new ScIndexEnumeration(this, u"com.sun.star.sheet.ScenariosEnumeration"_ustr);
}

// XIndexAccess

sal_Int32 SAL_CALL ScScenariosObj::getCount()
{
    SolarMutexGuard aGuard;
    SCTAB nCount = 0;
    if ( pDocShell )
    {
        ScDocument& rDoc = pDocShell->GetDocument();
        if (!rDoc.IsScenario(nTab))
        {
            SCTAB nTabCount = rDoc.GetTableCount();
            SCTAB nNext = nTab + 1;
            while (nNext < nTabCount && rDoc.IsScenario(nNext))
            {
                ++nCount;
                ++nNext;
            }
        }
    }
    return nCount;
}

uno::Any SAL_CALL ScScenariosObj::getByIndex( sal_Int32 nIndex )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xScen(GetObjectByIndex_Impl(nIndex));
    if (!xScen.is())
        throw lang::IndexOutOfBoundsException();

    return uno::Any(uno::Reference<sheet::XScenario>(xScen));
}

uno::Type SAL_CALL ScScenariosObj::getElementType()
{
    return cppu::UnoType<sheet::XScenario>::get();
}

sal_Bool SAL_CALL ScScenariosObj::hasElements()
{
    SolarMutexGuard aGuard;
    return ( getCount() != 0 );
}

uno::Any SAL_CALL ScScenariosObj::getByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    rtl::Reference<ScTableSheetObj> xScen(GetObjectByName_Impl(aName));
    if (!xScen.is())
        throw container::NoSuchElementException();

    return uno::Any(uno::Reference<sheet::XScenario>(xScen));
}

uno::Sequence<OUString> SAL_CALL ScScenariosObj::getElementNames()
{
    SolarMutexGuard aGuard;
    SCTAB nCount = static_cast<SCTAB>(getCount());
    uno::Sequence<OUString> aSeq(nCount);

    if ( pDocShell )    // otherwise Count = 0
    {
        OUString aTabName;
        ScDocument& rDoc = pDocShell->GetDocument();
        OUString* pAry = aSeq.getArray();
        for (SCTAB i=0; i<nCount; i++)
            if (rDoc.GetName( nTab+i+1, aTabName ))
                pAry[i] = aTabName;
    }

    return aSeq;
}

sal_Bool SAL_CALL ScScenariosObj::hasByName( const OUString& aName )
{
    SolarMutexGuard aGuard;
    SCTAB nIndex;
    return GetScenarioIndex_Impl( aName, nIndex );
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
