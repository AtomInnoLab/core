/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <swmodeltestbase.hxx>

#include <comphelper/processfactory.hxx>
#include <osl/file.hxx>
#include <comphelper/propertyvalue.hxx>
#include <comphelper/scopeguard.hxx>
#include <vcl/scheduler.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/dispatch.hxx>

#include <com/sun/star/frame/XLayoutManager.hpp>
#include <com/sun/star/frame/XDispatchHelper.hpp>
#include <com/sun/star/frame/XDispatchProvider.hpp>
#include <com/sun/star/frame/XComponentLoader.hpp>
#include <com/sun/star/frame/XStorable2.hpp>
#include <com/sun/star/lang/XMultiServiceFactory.hpp>
#include <com/sun/star/packages/zip/ZipFileAccess.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <com/sun/star/text/XParagraphCursor.hpp>

#include <cmdid.h>
#include <unotxdoc.hxx>
#include <docsh.hxx>
#include <wrtsh.hxx>
#include <swmodule.hxx>
#include <view.hxx>
#include <IDocumentRedlineAccess.hxx>

/// Covers sw/source/uibase/uiview/ fixes.
class SwUibaseUiviewTest : public SwModelTestBase
{
public:
    SwUibaseUiviewTest()
        : SwModelTestBase(u"/sw/qa/uibase/uiview/data/"_ustr)
    {
    }
};

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testUpdateAllObjectReplacements)
{
    // Make a temporary copy of the test document
    createTempCopy(u"updateall-objectreplacements.odt");

    /* BASIC code that exhibits the problem:

    desktop = CreateUnoService("com.sun.star.frame.Desktop")
    Dim props(0) as new com.sun.star.beans.PropertyValue
    props(0).Name = "Hidden"
    props(0).Value = true
    component = desktop.loadComponentFromURL("file://.../test.odt", "_default", 0, props)
    Wait 1000 ' workaround
    dispatcher = createUnoService("com.sun.star.frame.DispatchHelper")
    frame = component.CurrentController.Frame
    dispatcher.executeDispatch(frame, ".uno:UpdateAll", "", 0, Array())
    component.storeSelf(Array())
    component.dispose()
    */

    uno::Reference<lang::XMultiServiceFactory> xFactory(comphelper::getProcessServiceFactory());

    // Load the copy
    uno::Reference<uno::XInterface> xInterface
        = xFactory->createInstance(u"com.sun.star.frame.Desktop"_ustr);
    uno::Reference<frame::XComponentLoader> xComponentLoader(xInterface, uno::UNO_QUERY);
    uno::Sequence<beans::PropertyValue> aLoadArgs{ comphelper::makePropertyValue(u"Hidden"_ustr,
                                                                                 true) };
    mxComponent = xComponentLoader->loadComponentFromURL(maTempFile.GetURL(), u"_default"_ustr, 0,
                                                         aLoadArgs);

    // Perform the .uno:UpdateAll call and save
    xInterface = xFactory->createInstance(u"com.sun.star.frame.DispatchHelper"_ustr);
    uno::Reference<frame::XDispatchHelper> xDispatchHelper(xInterface, uno::UNO_QUERY);
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<frame::XDispatchProvider> xDispatchProvider(
        xModel->getCurrentController()->getFrame(), uno::UNO_QUERY);
    uno::Sequence<beans::PropertyValue> aNoArgs;
    xDispatchHelper->executeDispatch(xDispatchProvider, u".uno:UpdateAll"_ustr, OUString(), 0,
                                     aNoArgs);
    uno::Reference<frame::XStorable2> xStorable(mxComponent, uno::UNO_QUERY);
    xStorable->storeSelf(aNoArgs);

    // Check the contents of the updated copy and verify that ObjectReplacements are there
    uno::Reference<packages::zip::XZipFileAccess2> xNameAccess
        = packages::zip::ZipFileAccess::createWithURL(comphelper::getComponentContext(xFactory),
                                                      maTempFile.GetURL());

    CPPUNIT_ASSERT(xNameAccess->hasByName(u"ObjectReplacements/Components"_ustr));
    CPPUNIT_ASSERT(xNameAccess->hasByName(u"ObjectReplacements/Components_1"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testUpdateReplacementNosetting)
{
    // Load a copy of the document in hidden mode.
    OUString aSourceURL = createFileURL(u"update-replacement-nosetting.odt");
    CPPUNIT_ASSERT_EQUAL(osl::FileBase::E_None, osl::File::copy(aSourceURL, maTempFile.GetURL()));
    loadWithParams(maTempFile.GetURL(), { comphelper::makePropertyValue(u"Hidden"_ustr, true) });

    // Update "everything" (including object replacements) and save it.
    dispatchCommand(mxComponent, u".uno:UpdateAll"_ustr, {});
    uno::Reference<frame::XStorable2> xStorable(mxComponent, uno::UNO_QUERY);
    xStorable->storeSelf({});

    // Check the contents of the updated copy.
    uno::Reference<uno::XComponentContext> xContext = comphelper::getProcessComponentContext();
    uno::Reference<packages::zip::XZipFileAccess2> xNameAccess
        = packages::zip::ZipFileAccess::createWithURL(xContext, maTempFile.GetURL());

    // Without the accompanying fix in place, this test would have failed, because the embedded
    // object replacement image was not generated.
    CPPUNIT_ASSERT(xNameAccess->hasByName(u"ObjectReplacements/Components"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testKeepRatio)
{
    // Given a document with a custom KeepRatio:
    createSwDoc("keep-ratio.fodt");

    // Then make sure we read the custom value:
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pViewOption = pWrtShell->GetViewOptions();
    comphelper::ScopeGuard g([pWrtShell, pViewOption] {
        SwViewOption aViewOption(*pViewOption);
        aViewOption.SetKeepRatio(false);
        SwModule::get()->ApplyUsrPref(aViewOption, &pWrtShell->GetView());
    });
    // Without the accompanying fix in place, this test would have failed, because KeepRatio was not
    // mapped to settings.xml
    CPPUNIT_ASSERT(pViewOption->IsKeepRatio());

    // Then export as well:
    save(u"writer8"_ustr);
    xmlDocUniquePtr pXmlDoc = parseExport(u"settings.xml"_ustr);
    assertXPathContent(pXmlDoc, "//config:config-item[@config:name='KeepRatio']", u"true");
}

namespace
{
/// Interception implementation that disables .uno:Zoom on Image1, but not on Image2.
struct ImageInterceptor : public cppu::WeakImplHelper<frame::XDispatchProviderInterceptor>
{
    uno::Reference<view::XSelectionSupplier> m_xSelectionSupplier;
    uno::Reference<frame::XDispatchProvider> m_xMaster;
    uno::Reference<frame::XDispatchProvider> m_xSlave;
    int m_nEnabled = 0;
    int m_nDisabled = 0;

public:
    ImageInterceptor(const uno::Reference<lang::XComponent>& xComponent);

    // XDispatchProviderInterceptor
    uno::Reference<frame::XDispatchProvider> SAL_CALL getMasterDispatchProvider() override;
    uno::Reference<frame::XDispatchProvider> SAL_CALL getSlaveDispatchProvider() override;
    void SAL_CALL setMasterDispatchProvider(
        const uno::Reference<frame::XDispatchProvider>& xNewSupplier) override;
    void SAL_CALL
    setSlaveDispatchProvider(const uno::Reference<frame::XDispatchProvider>& xNewSupplier) override;

    // XDispatchProvider
    uno::Reference<frame::XDispatch> SAL_CALL queryDispatch(const util::URL& rURL,
                                                            const OUString& rTargetFrameName,
                                                            sal_Int32 SearchFlags) override;
    uno::Sequence<uno::Reference<frame::XDispatch>> SAL_CALL
    queryDispatches(const uno::Sequence<frame::DispatchDescriptor>& rRequests) override;
};
}

ImageInterceptor::ImageInterceptor(const uno::Reference<lang::XComponent>& xComponent)
{
    uno::Reference<frame::XModel2> xModel(xComponent, uno::UNO_QUERY);
    CPPUNIT_ASSERT(xModel.is());
    m_xSelectionSupplier.set(xModel->getCurrentController(), uno::UNO_QUERY);
    CPPUNIT_ASSERT(m_xSelectionSupplier.is());
}

uno::Reference<frame::XDispatchProvider> ImageInterceptor::getMasterDispatchProvider()
{
    return m_xMaster;
}

uno::Reference<frame::XDispatchProvider> ImageInterceptor::getSlaveDispatchProvider()
{
    return m_xSlave;
}

void ImageInterceptor::setMasterDispatchProvider(
    const uno::Reference<frame::XDispatchProvider>& xNewSupplier)
{
    m_xMaster = xNewSupplier;
}

void ImageInterceptor::setSlaveDispatchProvider(
    const uno::Reference<frame::XDispatchProvider>& xNewSupplier)
{
    m_xSlave = xNewSupplier;
}

uno::Reference<frame::XDispatch> ImageInterceptor::queryDispatch(const util::URL& rURL,
                                                                 const OUString& rTargetFrameName,
                                                                 sal_Int32 nSearchFlags)
{
    // Disable the UNO command based on the currently selected image, i.e. this can't be cached when
    // a different image is selected. Originally this was .uno:SetBorderStyle, but let's pick a
    // command which is active when running cppunit tests:
    if (rURL.Complete == ".uno:Zoom")
    {
        uno::Reference<container::XNamed> xImage;
        m_xSelectionSupplier->getSelection() >>= xImage;
        if (xImage.is() && xImage->getName() == "Image1")
        {
            ++m_nDisabled;
            return {};
        }

        ++m_nEnabled;
    }

    return m_xSlave->queryDispatch(rURL, rTargetFrameName, nSearchFlags);
}

uno::Sequence<uno::Reference<frame::XDispatch>>
ImageInterceptor::queryDispatches(const uno::Sequence<frame::DispatchDescriptor>& /*rRequests*/)
{
    return {};
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testSwitchBetweenImages)
{
    // Given a document with 2 images, and an interceptor catching an UNO command that specific to
    // the current selection:
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    uno::Reference<lang::XMultiServiceFactory> xMSF(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XText> xText = xTextDocument->getText();
    uno::Reference<text::XTextCursor> xCursor = xText->createTextCursor();
    for (int i = 0; i < 2; ++i)
    {
        uno::Reference<beans::XPropertySet> xTextGraphic(
            xMSF->createInstance(u"com.sun.star.text.TextGraphicObject"_ustr), uno::UNO_QUERY);
        xTextGraphic->setPropertyValue(u"AnchorType"_ustr,
                                       uno::Any(text::TextContentAnchorType_AS_CHARACTER));
        xTextGraphic->setPropertyValue(u"Size"_ustr, uno::Any(awt::Size(5000, 5000)));
        uno::Reference<text::XTextContent> xTextContent(xTextGraphic, uno::UNO_QUERY);
        xText->insertTextContent(xCursor, xTextContent, false);
    }
    pWrtShell->SttEndDoc(/*bStt=*/false);
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<frame::XDispatchProviderInterception> xRegistration(
        xModel->getCurrentController()->getFrame(), uno::UNO_QUERY);
    rtl::Reference pInterceptor(new ImageInterceptor(mxComponent));

    xRegistration->registerDispatchProviderInterceptor(pInterceptor);
    pInterceptor->m_nEnabled = 0;
    pInterceptor->m_nDisabled = 0;

    // When selecting the first image:
    selectShape(1);

    // Then make sure the UNO command is disabled:
    CPPUNIT_ASSERT_EQUAL(0, pInterceptor->m_nEnabled);
    CPPUNIT_ASSERT_GREATEREQUAL(1, pInterceptor->m_nDisabled);

    // Given a clean state:
    pInterceptor->m_nEnabled = 0;
    pInterceptor->m_nDisabled = 0;

    // When selecting the second image:
    selectShape(2);

    // Then make sure the UNO command is enabled:
    CPPUNIT_ASSERT_GREATEREQUAL(1, pInterceptor->m_nEnabled);
    CPPUNIT_ASSERT_EQUAL(0, pInterceptor->m_nDisabled);

    // Given a clean state:
    pInterceptor->m_nEnabled = 0;
    pInterceptor->m_nDisabled = 0;

    // When selecting the first image, again (this time not changing the selection type):
    selectShape(1);

    // Then make sure the UNO command is disabled:
    CPPUNIT_ASSERT_EQUAL(0, pInterceptor->m_nEnabled);
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected greater or equal than: 1
    // - Actual  : 0
    // i.e. selecting the first image didn't result in a disabled UNO command.
    CPPUNIT_ASSERT_GREATEREQUAL(1, pInterceptor->m_nDisabled);
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testPrintPreview)
{
    // Given a normal Writer view, in half-destroyed state, similar to what
    // SfxViewFrame::SwitchToViewShell_Impl() does in practice:
    createSwDoc();
    SwDocShell* pDocShell = getSwDocShell();
    SwView* pView = pDocShell->GetView();
    FmFormShell* pFormShell = pView->GetFormShell();
    pView->SetFormShell(reinterpret_cast<FmFormShell*>(-1));
    pView->SetDying();

    // When selecting a shell, similar to what happens the doc size changes:
    // Then make sure we don't crash:
    pView->SelectShell();

    // Restore the state and shut down.
    pView->SetFormShell(pFormShell);
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, TestTdf152839_Formtext)
{
    createSwDoc("tdf152839_formtext.rtf");

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    sal_Int32 nHeight
        = getXPath(pXmlDoc, "/root/page[1]/body/tab[1]/row[2]/cell[1]/txt/infos/bounds", "height")
              .toInt32();
    CPPUNIT_ASSERT_EQUAL(sal_Int32(723), nHeight);
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testEditInReadonly)
{
    createSwDoc("editinsection.odt");

    SwDocShell* pDocShell = getSwDocShell();
    SwView* pView = pDocShell->GetView();

    pView->GetViewFrame().GetDispatcher()->Execute(SID_EDITDOC, SfxCallMode::SYNCHRON);

    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XParagraphCursor> xParaCursor(xTextDocument->getText()->createTextCursor(),
                                                       uno::UNO_QUERY);

    uno::Reference<view::XSelectionSupplier> xSelSupplier(xModel->getCurrentController(),
                                                          uno::UNO_QUERY_THROW);

    xSelSupplier->select(css::uno::Any(xParaCursor));
    std::unique_ptr<SfxPoolItem> pItem;
    SfxItemState eState = pView->GetViewFrame().GetBindings().QueryState(FN_INSERT_TABLE, pItem);
    //status disabled in read only content
    CPPUNIT_ASSERT_EQUAL(SfxItemState::DISABLED, eState);

    //move cursor to section
    xParaCursor->gotoNextParagraph(false);
    xSelSupplier->select(css::uno::Any(xParaCursor));
    eState = pView->GetViewFrame().GetBindings().QueryState(FN_INSERT_TABLE, pItem);
    //status default in editable section
    CPPUNIT_ASSERT_EQUAL(SfxItemState::DEFAULT, eState);
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testShowTextobjectbarInReadonly)
{
    createSwDoc("tdf146549.odt");

    SwDocShell* pDocShell = getSwDocShell();
    SwView* pView = pDocShell->GetView();

    pView->GetViewFrame().GetDispatcher()->Execute(SID_EDITDOC, SfxCallMode::SYNCHRON);

    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextDocument> xTextDocument(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XParagraphCursor> xParaCursor(xTextDocument->getText()->createTextCursor(),
                                                       uno::UNO_QUERY);

    uno::Reference<view::XSelectionSupplier> xSelSupplier(xModel->getCurrentController(),
                                                          uno::UNO_QUERY_THROW);

    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    SfxViewFrame& rViewFrame = pWrtShell->GetView().GetViewFrame();
    uno::Reference<com::sun::star::frame::XLayoutManager> xLayoutManager;
    uno::Reference<beans::XPropertySet> xPropSet(rViewFrame.GetFrame().GetFrameInterface(),
                                                 uno::UNO_QUERY);
    xLayoutManager.set(xPropSet->getPropertyValue(u"LayoutManager"_ustr), uno::UNO_QUERY);

    // move the cursor to the non-editable section
    xSelSupplier->select(css::uno::Any(xParaCursor));

    bool bShow;
    bShow = xLayoutManager->isElementVisible(u"private:resource/toolbar/drawtextobjectbar"_ustr);
    CPPUNIT_ASSERT_EQUAL(false, bShow); // the formatting toolbar should be hidden

    // move the cursor to the editable section
    xParaCursor->gotoNextParagraph(false);
    xSelSupplier->select(css::uno::Any(xParaCursor));

    bShow = xLayoutManager->isElementVisible(u"private:resource/toolbar/drawtextobjectbar"_ustr);
    CPPUNIT_ASSERT_EQUAL(true, bShow); // the formatting toolbar should be shown

    // move the cursor to the non-editable section
    xParaCursor->gotoPreviousParagraph(false);
    xSelSupplier->select(css::uno::Any(xParaCursor));

    bShow = xLayoutManager->isElementVisible(u"private:resource/toolbar/drawtextobjectbar"_ustr);
    CPPUNIT_ASSERT_EQUAL(false, bShow); // the formatting toolbar should be hidden
}

CPPUNIT_TEST_FIXTURE(SwUibaseUiviewTest, testReinstateTrackedChangeState)
{
    // Given a document with a deletion:
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->Insert("abcd");
    RedlineFlags nMode = pWrtShell->GetRedlineFlags();
    pWrtShell->SetRedlineFlags(nMode | RedlineFlags::On);
    pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 1, /*bBasicCall=*/false);
    pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/true, 2, /*bBasicCall=*/false);
    pWrtShell->DelRight();

    // When the cursor is inside a redline:
    pWrtShell->SttPara(/*bSelect=*/false);
    pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/false, 2, /*bBasicCall=*/false);

    // Then make sure that reinstate is enabled:
    SwView* pView = getSwDocShell()->GetView();
    std::unique_ptr<SfxPoolItem> pItem;
    SfxItemState eState
        = pView->GetViewFrame().GetBindings().QueryState(FN_REDLINE_REINSTATE_DIRECT, pItem);
    CPPUNIT_ASSERT_EQUAL(SfxItemState::DEFAULT, eState);

    // When the cursor is outside a redline:
    pWrtShell->SttPara(/*bSelect=*/false);

    // Then make sure that reinstate is disabled:
    eState = pView->GetViewFrame().GetBindings().QueryState(FN_REDLINE_REINSTATE_DIRECT, pItem);
    // Without the accompanying fix in place, this test would have failed with:
    // - Expected: 1 (DISABLED)
    // - Actual  : 32 (DEFAULT)
    // i.e. reinstate was always enabled.
    CPPUNIT_ASSERT_EQUAL(SfxItemState::DISABLED, eState);
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
