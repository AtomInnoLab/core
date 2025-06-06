/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <com/sun/star/drawing/FillStyle.hpp>
#include <swmodeltestbase.hxx>
#include <cntfrm.hxx>
#include <itabenum.hxx>
#include <ndtxt.hxx>
#include <wrtsh.hxx>
#include <edtwin.hxx>
#include <drawdoc.hxx>
#include <view.hxx>
#include <com/sun/star/text/XTextColumns.hpp>

#include <svx/svdpage.hxx>
#include <svx/svdview.hxx>
#include <svl/itemiter.hxx>
#include <vcl/filter/PDFiumLibrary.hxx>

#include <dbfld.hxx>
#include <txatbase.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentRedlineAccess.hxx>
#include <IDocumentLayoutAccess.hxx>
#include <UndoManager.hxx>

#include <svl/stritem.hxx>
#include <sfx2/viewfrm.hxx>
#include <sfx2/dispatch.hxx>
#include <cmdid.h>
#include <tools/json_writer.hxx>
#include <tools/UnitConversion.hxx>
#include <boost/property_tree/json_parser.hpp>

#include <com/sun/star/text/XTextTable.hpp>
#include <com/sun/star/text/XTextViewCursorSupplier.hpp>
#include <com/sun/star/view/XSelectionSupplier.hpp>
#include <o3tl/cppunittraitshelper.hxx>
#include <swdtflvr.hxx>
#include <comphelper/propertysequence.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/scopeguard.hxx>
#include <editeng/swafopt.hxx>
#include <editeng/unolingu.hxx>
#include <LibreOfficeKit/LibreOfficeKitEnums.h>
#include <vcl/scheduler.hxx>
#include <config_fonts.h>
#include <test/htmltesttools.hxx>
#include <wrthtml.hxx>
#include <dbmgr.hxx>
#include <rootfrm.hxx>
#include <pagefrm.hxx>
#include <sortedobjs.hxx>
#include <flyfrms.hxx>
#include <tabfrm.hxx>
#include <unotxdoc.hxx>
#include <wrong.hxx>
#include <com/sun/star/linguistic2/LinguServiceManager.hpp>
#include <com/sun/star/linguistic2/XLinguProperties.hpp>
#include <com/sun/star/linguistic2/XSpellChecker1.hpp>
#include <linguistic/misc.hxx>

#include <workctrl.hxx>

using namespace com::sun::star;
using namespace com::sun::star::beans;
using namespace com::sun::star::lang;
using namespace com::sun::star::uno;
using namespace com::sun::star::linguistic2;
using namespace linguistic;

namespace
{
sal_Int32 lcl_getAttributeIDFromHints(const SwpHints& hints)
{
    for (size_t i = 0; i < hints.Count(); ++i)
    {
        const SwTextAttr* hint = hints.Get(i);
        if (hint->Which() == RES_TXTATR_AUTOFMT)
        {
            const SwFormatAutoFormat& rFmt = hint->GetAutoFormat();
            SfxItemIter aIter(*rFmt.GetStyleHandle());
            return aIter.GetCurItem()->Which();
        }
    }
    return -1;
}

uno::Reference<XLinguServiceManager2> GetLngSvcMgr_Impl()
{
    uno::Reference<XComponentContext> xContext(comphelper::getProcessComponentContext());
    uno::Reference<XLinguServiceManager2> xRes = LinguServiceManager::create(xContext);
    return xRes;
}
} //namespace

class SwUiWriterTest6 : public SwModelTestBase, public HtmlTestTools
{
public:
    SwUiWriterTest6()
        : SwModelTestBase(u"/sw/qa/extras/uiwriter/data/"_ustr)
    {
    }
};

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf116640)
{
    createSwDoc();

    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "Columns", uno::Any(sal_Int32(2)) } }));

    dispatchCommand(mxComponent, u".uno:InsertSection"_ustr, aArgs);

    uno::Reference<text::XTextSectionsSupplier> xTextSectionsSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xSections(xTextSectionsSupplier->getTextSections(),
                                                      uno::UNO_QUERY);
    uno::Reference<beans::XPropertySet> xTextSection(xSections->getByIndex(0), uno::UNO_QUERY);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xSections->getCount());

    uno::Reference<text::XTextColumns> xTextColumns
        = getProperty<uno::Reference<text::XTextColumns>>(xTextSection, u"TextColumns"_ustr);
    CPPUNIT_ASSERT_EQUAL(sal_Int16(2), xTextColumns->getColumnCount());

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), xSections->getCount());

    dispatchCommand(mxComponent, u".uno:Redo"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xSections->getCount());

    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), xSections->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf108524)
{
    createSwDoc("tdf108524.odt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    // In total we expect two cells containing a section.
    assertXPath(pXmlDoc, "/root/page/body/tab/row/cell/section", 2);

    assertXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell/section", 1);
    // This was 0, section wasn't split, instead it was only on the first page
    // and it was cut off.
    assertXPath(pXmlDoc, "/root/page[2]/body/tab/row/cell/section", 1);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testLinesInSectionInTable)
{
    // This is similar to testTdf108524(), but the page boundary now is not in
    // the middle of a multi-line paragraph: the section only contains oneliner
    // paragraphs instead.
    createSwDoc("lines-in-section-in-table.odt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    // In total we expect two cells containing a section.
    assertXPath(pXmlDoc, "/root/page/body/tab/row/cell/section", 2);

    assertXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell/section", 1);
    // This was 0, section wasn't split, instead it was only on the first page
    // and it was cut off.
    assertXPath(pXmlDoc, "/root/page[2]/body/tab/row/cell/section", 1);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testLinesMoveBackwardsInSectionInTable)
{
#if HAVE_MORE_FONTS
    // Assert that paragraph "4" is on page 1 and "5" is on page 2.
    createSwDoc("lines-in-section-in-table.odt");
    SwDoc* pDoc = getSwDoc();
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "/root/page", 2);
    SwNodeOffset nPara4Node(
        getXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell[1]/section/txt[last()]", "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"4"_ustr, pDoc->GetNodes()[nPara4Node]->GetTextNode()->GetText());
    SwNodeOffset nPara5Node(
        getXPath(pXmlDoc, "/root/page[2]/body/tab/row/cell[1]/section/txt[1]", "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"5"_ustr, pDoc->GetNodes()[nPara5Node]->GetTextNode()->GetText());

    // Remove paragraph "4".
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    while (pWrtShell->GetCursor()->GetPointNode().GetIndex() < nPara4Node)
        pWrtShell->Down(/*bSelect=*/false);
    pWrtShell->EndPara();
    pWrtShell->Up(/*bSelect=*/true);
    pWrtShell->DelLeft();

    // Assert that paragraph "5" is now moved back to page 1 and is the last paragraph there.
    pXmlDoc = parseLayoutDump();
    SwNodeOffset nPage1LastNode(
        getXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell[1]/section/txt[last()]", "txtNodeIndex")
            .toUInt32());
    // This was "3", paragraph "4" was deleted, but "5" was not moved backwards from page 2.
    CPPUNIT_ASSERT_EQUAL(u"5"_ustr, pDoc->GetNodes()[nPage1LastNode]->GetTextNode()->GetText());
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTableInSection)
{
#if HAVE_MORE_FONTS
    // The document has a section, containing a table that spans over 2 pages.
    createSwDoc("table-in-sect.odt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    // In total we expect 4 cells.
    assertXPath(pXmlDoc, "/root/page/body/section/tab/row/cell", 4);

    // Assert that on both pages the section contains 2 cells.
    assertXPath(pXmlDoc, "/root/page[1]/body/section/tab/row/cell", 2);
    assertXPath(pXmlDoc, "/root/page[2]/body/section/tab/row/cell", 2);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTableInNestedSection)
{
#if HAVE_MORE_FONTS
    // The document has a nested section, containing a table that spans over 2 pages.
    // This crashed the layout.
    createSwDoc("rhbz739252-3.odt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    // Make sure the table is inside a section and spans over 2 pages.
    assertXPath(pXmlDoc, "//page[1]//section/tab", 1);
    assertXPath(pXmlDoc, "//page[2]//section/tab", 1);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf112741)
{
#if HAVE_MORE_FONTS
    createSwDoc("tdf112741.fodt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    // This was 5 pages.
    assertXPath(pXmlDoc, "//page", 4);
    assertXPath(pXmlDoc, "//page[1]/body/tab/row/cell/tab/row/cell/section", 1);
    assertXPath(pXmlDoc, "//page[2]/body/tab/row/cell/tab/row/cell/section", 1);
    // This failed, 3rd page contained no sections.
    assertXPath(pXmlDoc, "//page[3]/body/tab/row/cell/tab/row/cell/section", 1);
    assertXPath(pXmlDoc, "//page[4]/body/tab/row/cell/tab/row/cell/section", 1);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf112860)
{
#if HAVE_MORE_FONTS
    // The document has a split section inside a nested table, and also a table
    // in the footer.
    // This crashed the layout.
    createSwDoc("tdf112860.fodt");
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf113287)
{
#if HAVE_MORE_FONTS
    createSwDoc("tdf113287.fodt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "//page", 2);
    sal_uInt32 nCellTop
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell[1]/infos/bounds", "top").toUInt32();
    sal_uInt32 nSectionTop
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell[1]/section/infos/bounds", "top")
              .toUInt32();
    // Make sure section frame is inside the cell frame.
    // Expected greater than 4593, was only 3714.
    CPPUNIT_ASSERT_GREATER(nCellTop, nSectionTop);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf113445)
{
#if HAVE_MORE_FONTS
    // Force multiple-page view.
    createSwDoc("tdf113445.fodt");
    SwDocShell* pDocShell = getSwDocShell();
    SwView* pView = pDocShell->GetView();
    pView->SetViewLayout(/*nColumns=*/2, /*bBookMode=*/false);
    calcLayout();

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "//page", 2);
    sal_uInt32 nPage1Left = getXPath(pXmlDoc, "//page[1]/infos/bounds", "left").toUInt32();
    sal_uInt32 nPage2Left = getXPath(pXmlDoc, "//page[2]/infos/bounds", "left").toUInt32();
    // Make sure that page 2 is on the right hand side of page 1, not below it.
    CPPUNIT_ASSERT_GREATER(nPage1Left, nPage2Left);

    // Insert a new paragraph at the start of the document.
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->StartOfSection();
    pWrtShell->SplitNode();
    pXmlDoc = parseLayoutDump();

    // Make sure that Table2:C5 and Table2:D5 has its section frame inside the cell frame.
    sal_uInt32 nCell3Top
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell/tab/row[4]/cell[3]/infos/bounds", "top")
              .toUInt32();
    sal_uInt32 nSection3Top
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell/tab/row[4]/cell[3]/section/infos/bounds",
                   "top")
              .toUInt32();
    CPPUNIT_ASSERT_GREATER(nCell3Top, nSection3Top);
    sal_uInt32 nCell4Top
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell/tab/row[4]/cell[4]/infos/bounds", "top")
              .toUInt32();
    sal_uInt32 nSection4Top
        = getXPath(pXmlDoc, "//page[2]/body/tab/row/cell/tab/row[4]/cell[4]/section/infos/bounds",
                   "top")
              .toUInt32();
    CPPUNIT_ASSERT_GREATER(nCell4Top, nSection4Top);
    // Also check if the two cells in the same row have the same top position.
    // This was 4818, expected only 1672.
    CPPUNIT_ASSERT_EQUAL(nCell3Top, nCell4Top);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf113686)
{
#if HAVE_MORE_FONTS
    createSwDoc("tdf113686.fodt");
    SwDoc* pDoc = getSwDoc();
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "/root/page", 2);
    SwNodeOffset nPage1LastNode(
        getXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell[1]/tab/row/cell[1]/txt[last()]",
                 "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"Table2:A1-P10"_ustr,
                         pDoc->GetNodes()[nPage1LastNode]->GetTextNode()->GetText());
    SwNodeOffset nPage2FirstNode(
        getXPath(pXmlDoc, "/root/page[2]/body/tab/row/cell[1]/section/txt[1]", "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"Table1:A1"_ustr,
                         pDoc->GetNodes()[nPage2FirstNode]->GetTextNode()->GetText());

    // Remove page 2.
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    while (pWrtShell->GetCursor()->Start()->GetNodeIndex() < nPage1LastNode)
        pWrtShell->Down(/*bSelect=*/false);
    pWrtShell->EndPara();
    for (int i = 0; i < 3; ++i)
        pWrtShell->Up(/*bSelect=*/true);
    pWrtShell->DelLeft();

    // Assert that the second page is removed.
    pXmlDoc = parseLayoutDump();
    // This was still 2, content from 2nd page was not moved.
    assertXPath(pXmlDoc, "/root/page", 1);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTableInSectionInTable)
{
#if HAVE_MORE_FONTS
    // The document has a table, containing a section, containing a nested
    // table.
    // This crashed the layout.
    createSwDoc("i95698.odt");
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testSectionInTableInTable)
{
#if HAVE_MORE_FONTS
    // The document has a nested table, containing a multi-line section at a
    // page boundary.
    // This crashed the layout later in SwFrame::IsFootnoteAllowed().
    createSwDoc("tdf112109.fodt");
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testSectionInTableInTable2)
{
#if HAVE_MORE_FONTS
    createSwDoc("split-section-in-nested-table.fodt");
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    sal_uInt32 nSection1
        = getXPath(pXmlDoc, "//page[1]//body/tab/row/cell/tab/row/cell/section", "id").toUInt32();
    sal_uInt32 nSection1Follow
        = getXPath(pXmlDoc, "//page[1]//body/tab/row/cell/tab/row/cell/section", "follow")
              .toUInt32();
    // This failed, the section wasn't split inside a nested table.
    sal_uInt32 nSection2
        = getXPath(pXmlDoc, "//page[2]//body/tab/row/cell/tab/row/cell/section", "id").toUInt32();
    sal_uInt32 nSection2Precede
        = getXPath(pXmlDoc, "//page[2]//body/tab/row/cell/tab/row/cell/section", "precede")
              .toUInt32();

    // Make sure that the first's follow and the second's precede is correct.
    CPPUNIT_ASSERT_EQUAL(nSection2, nSection1Follow);
    CPPUNIT_ASSERT_EQUAL(nSection1, nSection2Precede);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testSectionInTableInTable3)
{
#if HAVE_MORE_FONTS
    createSwDoc("tdf113153.fodt");

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    uno::Reference<container::XNamed> xTable(xTables->getByIndex(1), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(u"Table16"_ustr, xTable->getName());

    uno::Reference<text::XTextTable> xRowSupplier(xTable, uno::UNO_QUERY);
    uno::Reference<table::XTableRows> xRows = xRowSupplier->getRows();
    uno::Reference<beans::XPropertySet> xRow(xRows->getByIndex(1), uno::UNO_QUERY);
    xRow->setPropertyValue(u"IsSplitAllowed"_ustr, uno::Any(true));
    // This never returned.
    calcLayout();

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    sal_uInt32 nTable1 = getXPath(pXmlDoc, "//page[1]//body/tab", "id").toUInt32();
    sal_uInt32 nTable1Follow = getXPath(pXmlDoc, "//page[1]//body/tab", "follow").toUInt32();
    sal_uInt32 nTable2 = getXPath(pXmlDoc, "//page[2]//body/tab", "id").toUInt32();
    sal_uInt32 nTable2Precede = getXPath(pXmlDoc, "//page[2]//body/tab", "precede").toUInt32();
    sal_uInt32 nTable2Follow = getXPath(pXmlDoc, "//page[2]//body/tab", "follow").toUInt32();
    sal_uInt32 nTable3 = getXPath(pXmlDoc, "//page[3]//body/tab", "id").toUInt32();
    sal_uInt32 nTable3Precede = getXPath(pXmlDoc, "//page[3]//body/tab", "precede").toUInt32();

    // Make sure the outer table frames are linked together properly.
    CPPUNIT_ASSERT_EQUAL(nTable2, nTable1Follow);
    CPPUNIT_ASSERT_EQUAL(nTable1, nTable2Precede);
    CPPUNIT_ASSERT_EQUAL(nTable3, nTable2Follow);
    CPPUNIT_ASSERT_EQUAL(nTable2, nTable3Precede);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testSectionInTableInTable4)
{
#if HAVE_MORE_FONTS
    createSwDoc("tdf113520.fodt");
    SwDoc* pDoc = getSwDoc();
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "/root/page", 3);
    SwNodeOffset nPage1LastNode(
        getXPath(pXmlDoc, "/root/page[1]/body/tab/row/cell[1]/tab/row/cell[1]/section/txt[last()]",
                 "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"Section1:P10"_ustr,
                         pDoc->GetNodes()[nPage1LastNode]->GetTextNode()->GetText());
    SwNodeOffset nPage3FirstNode(
        getXPath(pXmlDoc, "/root/page[3]/body/tab/row/cell[1]/tab/row/cell[1]/section/txt[1]",
                 "txtNodeIndex")
            .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"Section1:P23"_ustr,
                         pDoc->GetNodes()[nPage3FirstNode]->GetTextNode()->GetText());

    // Remove page 2.
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    while (pWrtShell->GetCursor()->Start()->GetNodeIndex() < nPage1LastNode)
        pWrtShell->Down(/*bSelect=*/false);
    pWrtShell->EndPara();
    while (pWrtShell->GetCursor()->End()->GetNodeIndex() < nPage3FirstNode)
        pWrtShell->Down(/*bSelect=*/true);
    pWrtShell->EndPara(/*bSelect=*/true);
    pWrtShell->DelLeft();

    // Assert that the page is removed.
    pXmlDoc = parseLayoutDump();
    // This was 3, page 2 was emptied, but it wasn't removed.
    assertXPath(pXmlDoc, "/root/page", 2);

    // Make sure the outer table frames are linked together properly.
    sal_uInt32 nTable1 = getXPath(pXmlDoc, "//page[1]//body/tab", "id").toUInt32();
    sal_uInt32 nTable1Follow = getXPath(pXmlDoc, "//page[1]//body/tab", "follow").toUInt32();
    sal_uInt32 nTable2 = getXPath(pXmlDoc, "//page[2]//body/tab", "id").toUInt32();
    sal_uInt32 nTable2Precede = getXPath(pXmlDoc, "//page[2]//body/tab", "precede").toUInt32();
    CPPUNIT_ASSERT_EQUAL(nTable2, nTable1Follow);
    CPPUNIT_ASSERT_EQUAL(nTable1, nTable2Precede);
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf112160)
{
#if HAVE_MORE_FONTS
    // Assert that the A2 cell is on page 1.
    createSwDoc("tdf112160.fodt");
    SwDoc* pDoc = getSwDoc();
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    SwNodeOffset nA2CellNode(getXPath(pXmlDoc,
                                      "/root/page[1]/body/tab/row[2]/cell[1]/section/txt[last()]",
                                      "txtNodeIndex")
                                 .toUInt32());
    CPPUNIT_ASSERT_EQUAL(u"Table1.A2"_ustr,
                         pDoc->GetNodes()[nA2CellNode]->GetTextNode()->GetText());

    // Append a new paragraph to the end of the A2 cell.
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    while (pWrtShell->GetCursor()->GetPointNode().GetIndex() < nA2CellNode)
        pWrtShell->Down(/*bSelect=*/false);
    pWrtShell->EndPara();
    pWrtShell->SplitNode();

    // Assert that after A2 got extended, D2 stays on page 1.
    pXmlDoc = parseLayoutDump();
    sal_uInt32 nD2CellNode
        = getXPath(pXmlDoc, "/root/page[1]/body/tab/row[2]/cell[last()]/section/txt[last()]",
                   "txtNodeIndex")
              .toUInt32();
    // This was Table1.C2, Table1.D2 was moved to the next page, unexpected.
    CPPUNIT_ASSERT_EQUAL(u"Table1.D2"_ustr,
                         pDoc->GetNodes()[SwNodeOffset(nD2CellNode)]->GetTextNode()->GetText());
#endif
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf114536)
{
    // This crashed in SwTextFormatter::MergeCharacterBorder() due to a
    // use after free.
    createSwDoc("tdf114536.odt");
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testParagraphOfTextRange)
{
    createSwDoc("paragraph-of-text-range.odt");

    // Enter the table.
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->Down(/*bSelect=*/false);
    CPPUNIT_ASSERT(pWrtShell->IsCursorInTable());
    // Enter the section.
    pWrtShell->Down(/*bSelect=*/false);
    CPPUNIT_ASSERT(pWrtShell->IsDirectlyInSection());

    // Assert that we get the right paragraph object.
    uno::Reference<frame::XModel> xModel(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XTextViewCursorSupplier> xController(xModel->getCurrentController(),
                                                              uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xViewCursor = xController->getViewCursor();
    // This failed as there were no TextParagraph property.
    auto xParagraph = getProperty<uno::Reference<text::XTextRange>>(xViewCursor->getStart(),
                                                                    u"TextParagraph"_ustr);
    CPPUNIT_ASSERT_EQUAL(u"In section"_ustr, xParagraph->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf99689TableOfContents)
{
    createSwDoc("tdf99689.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->GotoNextTOXBase();
    const SwTOXBase* pTOXBase = pWrtShell->GetCurTOX();
    pWrtShell->UpdateTableOf(*pTOXBase);
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwTextNode* pTitleNode = pShell->GetCursor()->GetPointNode().GetTextNode();
    SwNodeIndex aIdx(*pTitleNode);
    // skip the title
    SwNodes::GoNext(&aIdx);

    // skip the first header. No attributes there.
    // next node should contain superscript
    SwTextNode* pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    sal_uInt16 nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);

    // next node should contain subscript
    pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf99689TableOfFigures)
{
    createSwDoc("tdf99689_figures.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->GotoNextTOXBase();
    const SwTOXBase* pTOXBase = pWrtShell->GetCurTOX();
    pWrtShell->UpdateTableOf(*pTOXBase);
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwTextNode* pTitleNode = pShell->GetCursor()->GetPointNode().GetTextNode();
    SwNodeIndex aIdx(*pTitleNode);

    // skip the title
    // next node should contain subscript
    SwTextNode* pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    sal_uInt16 nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);

    // next node should contain superscript
    pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf99689TableOfTables)
{
    createSwDoc("tdf99689_tables.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    pWrtShell->GotoNextTOXBase();
    const SwTOXBase* pTOXBase = pWrtShell->GetCurTOX();
    pWrtShell->UpdateTableOf(*pTOXBase);
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwTextNode* pTitleNode = pShell->GetCursor()->GetPointNode().GetTextNode();
    SwNodeIndex aIdx(*pTitleNode);

    // skip the title
    // next node should contain superscript
    SwTextNode* pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    sal_uInt16 nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);

    // next node should contain subscript
    pNext = static_cast<SwTextNode*>(SwNodes::GoNext(&aIdx));
    CPPUNIT_ASSERT(pNext->HasHints());
    nAttrType = lcl_getAttributeIDFromHints(pNext->GetSwpHints());
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(RES_CHRATR_ESCAPEMENT), nAttrType);
}

// tdf#112448: Fix: take correct line height
//
// When line metrics is not calculated we need to call CalcRealHeight()
// before usage of the Height() and GetRealHeight().
CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf112448)
{
    createSwDoc("tdf112448.odt");

    // check actual number of line breaks in the paragraph
    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPath(pXmlDoc, "/root/page/body/txt/SwParaPortion/SwLineLayout", 2);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf113790)
{
    createSwDoc("tdf113790.docx");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    // Create the clipboard document.
    SwDoc aClipboard;
    aClipboard.SetClipBoard(true);

    // Go to fourth line - to "ABCD" bulleted list item
    pWrtShell->Down(/*bSelect=*/false, 4);
    pWrtShell->SelPara(nullptr);
    CPPUNIT_ASSERT_EQUAL(u"ABCD"_ustr, pWrtShell->GetSelText());
    pWrtShell->Copy(aClipboard);

    // Go down to next-to-last (empty) line above "Title3"
    pWrtShell->Down(/*bSelect=*/false, 4);
    pWrtShell->Paste(aClipboard);

    // Save it as DOCX & load it again
    saveAndReload(u"Office Open XML Text"_ustr);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157937)
{
    createSwDoc("tdf130088.docx");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // select paragraph
    pWrtShell->SelPara(nullptr);

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // show changes
    CPPUNIT_ASSERT_MESSAGE(
        "redlines should be visible",
        IDocumentRedlineAccess::IsShowChanges(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    // cycle case with change tracking
    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});
    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    // This resulted freezing
    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157988)
{
    createSwDoc("tdf130088.docx");
    SwDoc* pDoc = getSwDoc();

    // select the second word
    dispatchCommand(mxComponent, u".uno:GoToNextWord"_ustr, {});
    dispatchCommand(mxComponent, u".uno:SelectWord"_ustr, {});

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // show changes
    CPPUNIT_ASSERT_MESSAGE(
        "redlines should be visible",
        IDocumentRedlineAccess::IsShowChanges(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    // cycle case with change tracking

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodalesSodales"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    // This was false (missing revert of the tracked change)
    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodalesSodales"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt"));

    // tdf#141198 cycle case without selection: the word under the cursor

    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    dispatchCommand(mxComponent, u".uno:GoRight"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodalesSodales"));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt"));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157667)
{
    createSwDoc("tdf130088.docx");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // select the first three words
    pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/true, 25, /*bBasicCall=*/false);

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // show changes
    CPPUNIT_ASSERT_MESSAGE(
        "redlines should be visible",
        IDocumentRedlineAccess::IsShowChanges(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    // cycle case with change tracking

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesSodales tinciduntTincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    // This was false (missing revert of the tracked change)
    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesINTEGER SODALES tincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesSodales tinciduntTincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt tristique."));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf158039)
{
    createSwDoc("tdf130088.docx");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // select the first sentence
    pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/true, 26, /*bBasicCall=*/false);

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // show changes
    CPPUNIT_ASSERT_MESSAGE(
        "redlines should be visible",
        IDocumentRedlineAccess::IsShowChanges(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    // cycle case with change tracking

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesSodales tinciduntTincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    // This was false (missing revert of the tracked change)
    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesINTEGER SODALES tincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith(
        "Integer sodalesSodales tinciduntTincidunt tristique."));

    dispatchCommand(mxComponent, u".uno:ChangeCaseRotateCase"_ustr, {});

    CPPUNIT_ASSERT(getParagraph(1)->getString().startsWith("Integer sodales tincidunt tristique."));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf108048)
{
    createSwDoc();

    uno::Sequence<beans::PropertyValue> aPropertyValues = comphelper::InitPropertySequence({
        { "Kind", uno::Any(sal_Int16(3)) },
        { "TemplateName", uno::Any(u"Default Page Style"_ustr) },
        { "PageNumber", uno::Any(sal_uInt16(6)) }, // Even number to avoid auto-inserted blank page
        { "PageNumberFilled", uno::Any(true) },
    });
    dispatchCommand(mxComponent, u".uno:InsertBreak"_ustr, aPropertyValues);
    CPPUNIT_ASSERT_EQUAL(2, getParagraphs());
    CPPUNIT_ASSERT_EQUAL(2, getPages());

    // The inserted page must have page number set to 6
    uno::Reference<text::XTextRange> xPara = getParagraph(2);
    sal_uInt16 nPageNumber = getProperty<sal_uInt16>(xPara, u"PageNumberOffset"_ustr);
    CPPUNIT_ASSERT_EQUAL(sal_uInt16(6), nPageNumber);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf113481)
{
    createSwDoc("tdf113481-IVS.odt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // One backspace should completely remove the CJK ideograph variation sequence
    pWrtShell->EndPara();
    // Before: U+8FBA U+E0102. After: empty
    pWrtShell->DelLeft();
    const uno::Reference<text::XTextRange> xPara1 = getParagraph(1);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), xPara1->getString().getLength());

    // Also variation sequence of weak characters that are treated as CJK script
    pWrtShell->Down(false);
    pWrtShell->EndPara();
    // Before: U+4E2D U+2205 U+FE00. After: U+4E2D U+2205
    pWrtShell->DelLeft();
    const uno::Reference<text::XTextRange> xPara2 = getParagraph(2);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xPara2->getString().getLength());
    CPPUNIT_ASSERT_EQUAL(u'\x4E2D', xPara2->getString()[0]);

    // Also variation sequence of other scripts
    pWrtShell->Down(false);
    pWrtShell->EndPara();
    // Before: U+1820 U+180B. After: U+1820
    pWrtShell->DelLeft();
    const uno::Reference<text::XTextRange> xPara3 = getParagraph(3);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), xPara3->getString().getLength());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf115013)
{
    static constexpr OUString sColumnName(u"Name with spaces, \"quotes\" and \\backslashes"_ustr);

    utl::TempFileNamed aTempDir(nullptr, true);
    aTempDir.EnableKillingFile();
    const OUString aWorkDir = aTempDir.GetURL();

    //create new writer document
    createSwDoc();
    SwDoc* pDoc = getSwDoc();

    {
        // Load and register data source
        OUString sDataSource
            = SwDBManager::LoadAndRegisterDataSource(createFileURL(u"datasource.ods"), &aWorkDir);
        CPPUNIT_ASSERT(!sDataSource.isEmpty());

        // Insert a new field type for the mailmerge field
        SwDBData aDBData;
        aDBData.sDataSource = sDataSource;
        aDBData.sCommand = "Sheet1";
        SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
        CPPUNIT_ASSERT(pWrtShell);
        SwDBFieldType* pFieldType = static_cast<SwDBFieldType*>(
            pWrtShell->InsertFieldType(SwDBFieldType(pDoc, sColumnName, aDBData)));
        CPPUNIT_ASSERT(pFieldType);

        // Insert the field into document
        SwDBField aField(pFieldType);
        pWrtShell->InsertField2(aField);
    }
    // Save it as DOCX & load it again
    saveAndReload(u"Office Open XML Text"_ustr);
    pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwPaM* pCursor = pShell->GetCursor();
    CPPUNIT_ASSERT(pCursor);

    // Get the field at the beginning of the document
    SwDBField* pField = dynamic_cast<SwDBField*>(SwCursorShell::GetFieldAtCursor(pCursor, true));
    CPPUNIT_ASSERT(pField);
    OUString sColumn = static_cast<SwDBFieldType*>(pField->GetTyp())->GetColumnName();
    // The column name must come correct after round trip
    CPPUNIT_ASSERT_EQUAL(sColumnName, sColumn);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf115065)
{
    // In the document, the tables have table style assigned
    // Source table (first one) has two rows;
    // destination (second one) has only one row
    createSwDoc("tdf115065.odt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    pWrtShell->GotoTable(UIName(u"Table2"_ustr));
    SwRect aRect = pWrtShell->GetCurrFrame()->getFrameArea();
    // Destination point is the middle of the first cell of second table
    Point ptTo(aRect.Left() + aRect.Width() / 2, aRect.Top() + aRect.Height() / 2);

    pWrtShell->GotoTable(UIName(u"Table1"_ustr));
    aRect = pWrtShell->GetCurrFrame()->getFrameArea();
    // Source point is the middle of the first cell of first table
    Point ptFrom(aRect.Left() + aRect.Width() / 2, aRect.Top() + aRect.Height() / 2);

    pWrtShell->SelTableCol();
    // The copy operation (or closing document after that) segfaulted
    pWrtShell->Copy(*pWrtShell, ptFrom, ptTo);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf84806_MovingMultipleTableRows)
{
    // Moving of multiple table rows.
    // Source table (first one) has two rows;
    // destination (second one) has only one row
    createSwDoc("tdf115065.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables->getCount());
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table2"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTable2(xTableNames->getByName(u"Table2"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable2->getRows()->getCount());

    // without redlining
    CPPUNIT_ASSERT_MESSAGE("redlining should be off",
                           !pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    sw::UndoManager& rUndoManager = pDoc->GetUndoManager();

    pWrtShell->GotoTable(UIName(u"Table2"_ustr));
    SwRect aRect = pWrtShell->GetCurrFrame()->getFrameArea();
    // Destination point is the middle of the first cell of second table
    Point ptTo(aRect.Left() + aRect.Width() / 2, aRect.Top() + aRect.Height() / 2);

    // Move rows of the first table into the second table
    pWrtShell->GotoTable(UIName(u"Table1"_ustr));
    pWrtShell->SelTable();
    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);
    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    // This was 2 tables
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTables->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable2->getRows()->getCount());

    // Undo results 2 tables
    rUndoManager.Undo();
    uno::Reference<container::XIndexAccess> xTables2(xTablesSupplier->getTextTables(),
                                                     uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables2->getCount());
    uno::Reference<text::XTextTable> xTable1b(xTableNames->getByName(u"Table1"_ustr),
                                              uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTable2b(xTableNames->getByName(u"Table2"_ustr),
                                              uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1b->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable2b->getRows()->getCount());

    // FIXME assert with Redo()
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf147181_TrackedMovingOfMultipleTableRows)
{
    // Tracked moving of multiple table rows.
    // Source table (first one) has two rows;
    // destination (second one) has only one row
    createSwDoc("tdf115065.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xTables(xTablesSupplier->getTextTables(),
                                                    uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables->getCount());
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table2"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTable2(xTableNames->getByName(u"Table2"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable2->getRows()->getCount());

    // FIXME: doesn't work with empty rows, yet
    pWrtShell->Insert(u"x"_ustr);
    pWrtShell->Down(false);
    pWrtShell->Insert(u"x"_ustr);

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // show changes
    CPPUNIT_ASSERT_MESSAGE(
        "redlines should be visible",
        IDocumentRedlineAccess::IsShowChanges(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    sw::UndoManager& rUndoManager = pDoc->GetUndoManager();

    pWrtShell->GotoTable(UIName(u"Table2"_ustr));
    SwRect aRect = pWrtShell->GetCurrFrame()->getFrameArea();
    // Destination point is the middle of the first cell of second table
    Point ptTo(aRect.Left() + aRect.Width() / 2, aRect.Top() + aRect.Height() / 2);

    // Move rows of the first table into the second table
    pWrtShell->GotoTable(UIName(u"Table1"_ustr));
    pWrtShell->SelTable();
    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);
    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    // still 2 tables, but the second one has got 3 rows
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable2->getRows()->getCount());

    // accept changes results 1 table (removing moved table)
    dispatchCommand(mxComponent, u".uno:AcceptAllTrackedChanges"_ustr, {});
    uno::Reference<container::XIndexAccess> xTables2(xTablesSupplier->getTextTables(),
                                                     uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTables2->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable2->getRows()->getCount());

    // Undo results 2 tables
    rUndoManager.Undo();
    rUndoManager.Undo();
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables2->getCount());
    uno::Reference<text::XTextTable> xTable1b(xTableNames->getByName(u"Table1"_ustr),
                                              uno::UNO_QUERY);
    uno::Reference<text::XTextTable> xTable2b(xTableNames->getByName(u"Table2"_ustr),
                                              uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1b->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable2b->getRows()->getCount());

    // reject changes results 2 table again, with the original row counts
    dispatchCommand(mxComponent, u".uno:RejectAllTrackedChanges"_ustr, {});
    uno::Reference<container::XIndexAccess> xTables3(xTablesSupplier->getTextTables(),
                                                     uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTables3->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1b->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable2b->getRows()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157492_TrackedMovingRow)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Create a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 4, 3);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable1->getColumns()->getCount());

    // fill table with data
    SwXTextDocument* pTextDoc = getSwTextDoc();
    for (int i = 0; i < 3; ++i)
    {
        pWrtShell->Insert(u"x"_ustr);
        pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_RIGHT);
    }

    Scheduler::ProcessEventsToIdle();

    uno::Reference<text::XTextRange> xCellA1(xTable1->getCellByName(u"A1"_ustr), uno::UNO_QUERY);
    xCellA1->setString(u"A1"_ustr);
    uno::Reference<text::XTextRange> xCellB1(xTable1->getCellByName(u"B1"_ustr), uno::UNO_QUERY);
    xCellB1->setString(u"B1"_ustr);
    uno::Reference<text::XTextRange> xCellC1(xTable1->getCellByName(u"C1"_ustr), uno::UNO_QUERY);
    xCellC1->setString(u"C1"_ustr);

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[1]/cell[1]/txt", u"A1");
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[1]/cell[2]/txt", u"B1");
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[1]/cell[3]/txt", u"C1");

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // Move first column of the table before the third column by drag & drop
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower();
    SwFrame* pRow1 = pTable->GetLower();
    SwFrame* pCellA1 = pRow1->GetLower();
    SwFrame* pRow3 = pRow1->GetNext()->GetNext();
    SwFrame* pCellA3 = pRow3->GetLower();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    const SwRect& rCellA3Rect = pCellA3->getFrameArea();
    Point ptTo(rCellA3Rect.Left() + rCellA3Rect.Width() / 2,
               rCellA3Rect.Top() + rCellA3Rect.Height() / 2);
    // select first table row by using the middle point of the left border of row 1
    Point ptRow(rCellA1Rect.Left() - 5, rCellA1Rect.Top() + rCellA1Rect.Height() / 2);
    pWrtShell->SelectTableRowCol(ptRow);

    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);

    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    // reject changes results 4 rows again, not 5
    dispatchCommand(mxComponent, u".uno:RejectAllTrackedChanges"_ustr, {});

    xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable2(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable2->getColumns()->getCount());
    // This was 5 (moving row without change tracking)
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable2->getRows()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf154599_MovingColumn)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Create a table with less columns than rows
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 4, 3);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable1->getColumns()->getCount());

    // without redlining
    CPPUNIT_ASSERT_MESSAGE("redlining should be off",
                           !pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // Move first column of the table before the third column by drag & drop

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower();
    SwFrame* pRow1 = pTable->GetLower();
    SwFrame* pCellA1 = pRow1->GetLower();
    SwFrame* pCellC1 = pCellA1->GetNext()->GetNext();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    const SwRect& rCellC1Rect = pCellC1->getFrameArea();
    Point ptTo(rCellC1Rect.Left() + rCellC1Rect.Width() / 2,
               rCellC1Rect.Top() + rCellC1Rect.Height() / 2);
    // select first table column by using the middle point of the top border of column A
    Point ptColumn(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() - 5);
    pWrtShell->SelectTableRowCol(ptColumn);

    // This crashed here before the fix.
    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);

    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable1->getColumns()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf155846_MovingColumn)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Create a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 4, 3);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable1->getColumns()->getCount());

    // fill table with data
    SwXTextDocument* pTextDoc = getSwTextDoc();
    for (int i = 0; i < 4; ++i)
    {
        pWrtShell->Insert(u"x"_ustr);
        pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_DOWN);
    }

    Scheduler::ProcessEventsToIdle();

    uno::Reference<text::XTextRange> xCellA1(xTable1->getCellByName(u"A1"_ustr), uno::UNO_QUERY);
    xCellA1->setString(u"A1"_ustr);
    uno::Reference<text::XTextRange> xCellA2(xTable1->getCellByName(u"A2"_ustr), uno::UNO_QUERY);
    xCellA2->setString(u"A2"_ustr);
    uno::Reference<text::XTextRange> xCellA3(xTable1->getCellByName(u"A3"_ustr), uno::UNO_QUERY);
    xCellA3->setString(u"A3"_ustr);
    uno::Reference<text::XTextRange> xCellA4(xTable1->getCellByName(u"A4"_ustr), uno::UNO_QUERY);
    xCellA4->setString(u"A4"_ustr);

    xmlDocUniquePtr pXmlDoc = parseLayoutDump();
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[1]/cell[1]/txt", u"A1");
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[2]/cell[1]/txt", u"A2");
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[3]/cell[1]/txt", u"A3");
    assertXPathContent(pXmlDoc, "/root/page/body/tab/row[4]/cell[1]/txt", u"A4");

    // enable redlining
    dispatchCommand(mxComponent, u".uno:TrackChanges"_ustr, {});
    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // Move first column of the table before the third column by drag & drop
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower();
    SwFrame* pRow1 = pTable->GetLower();
    SwFrame* pCellA1 = pRow1->GetLower();
    SwFrame* pCellC1 = pCellA1->GetNext()->GetNext();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    const SwRect& rCellC1Rect = pCellC1->getFrameArea();
    Point ptTo(rCellC1Rect.Left() + rCellC1Rect.Width() / 2,
               rCellC1Rect.Top() + rCellC1Rect.Height() / 2);
    // select first table column by using the middle point of the top border of column A
    Point ptColumn(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() - 5);
    pWrtShell->SelectTableRowCol(ptColumn);

    // This crashed here before the fix.
    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);

    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    // reject changes results 3 columns again, not 4
    dispatchCommand(mxComponent, u".uno:RejectAllTrackedChanges"_ustr, {});

    xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable2(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable2->getRows()->getCount());
    // This was 4 (moving column without change tracking)
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable2->getColumns()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf154771_MovingMultipleColumns)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Create a table with less columns than rows
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 5, 4);

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getColumns()->getCount());

    // without redlining
    CPPUNIT_ASSERT_MESSAGE("redlining should be off",
                           !pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    // Move first two columns of the table before column D by drag & drop

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower();
    SwFrame* pRow1 = pTable->GetLower();
    SwFrame* pCellA1 = pRow1->GetLower();
    SwFrame* pCellB1 = pCellA1->GetNext();
    SwFrame* pCellD1 = pCellB1->GetNext()->GetNext();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    const SwRect& rCellB1Rect = pCellB1->getFrameArea();
    const SwRect& rCellD1Rect = pCellD1->getFrameArea();
    Point ptTo(rCellD1Rect.Left() + rCellD1Rect.Width() / 2,
               rCellD1Rect.Top() + rCellD1Rect.Height() / 2);
    // select first two table columns by using
    // the middle point of the top border of column A
    // and middle point of the top border of column B
    Point ptColumnA(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() - 5);
    const Point ptColumnB(rCellB1Rect.Left() + rCellB1Rect.Width() / 2, rCellB1Rect.Top() - 5);
    pWrtShell->SelectTableRowCol(ptColumnA, &ptColumnB);

    rtl::Reference<SwTransferable> xTransfer = new SwTransferable(*pWrtShell);
    xTransfer->PrivateDrop(*pWrtShell, ptTo, /*bMove=*/true, /*bXSelection=*/true);

    CPPUNIT_ASSERT_EQUAL(sal_Int32(5), xTable1->getRows()->getCount());
    // This was 5 before the fix (only the first selected column was moved, the
    // other ones were copied instead of moving)
    CPPUNIT_ASSERT_EQUAL(sal_Int32(4), xTable1->getColumns()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf44773)
{
    // allow resizing table rows, if cursor outside the table
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // insert an empty paragraph
    pWrtShell->SplitNode();

    // create a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 2, 1);

    // the cursor is not inside the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable1->getColumns()->getCount());

    Scheduler::ProcessEventsToIdle();

    // set table row height by drag & drop
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();

    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower()->GetNext();
    SwFrame* pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    SwFrame* pCellA1 = pRow1->GetLower();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();
    // select center of the bottom border of the first table cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight);
    // double the row height
    Point ptTo(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + 2 * nRowHeight);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                           MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    Point aTo = rEditWin.LogicToPixel(ptTo);
    MouseEvent aMoveEvent(aTo, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                          MOUSE_LEFT);
    TrackingEvent aTEvt(aMoveEvent, TrackingEventFlags::Repeat);
    // drag & drop of cell border inside the document (and outside the table)
    // still based on the ruler code, use that to simulate dragging
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt);
    TrackingEvent aTEvt2(aMoveEvent, TrackingEventFlags::End);
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt2);
    Scheduler::ProcessEventsToIdle();
    rEditWin.CaptureMouse();
    rEditWin.ReleaseMouse();

    // this was 396 (not modified row height previously)
    CPPUNIT_ASSERT_GREATER(tools::Long(750), pCellA1->getFrameArea().Height());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157833)
{
    // allow resizing table rows & columns using a minimal hit area
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // insert an empty paragraph
    pWrtShell->SplitNode();

    // create a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 2, 1);

    // the cursor is not inside the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable1->getColumns()->getCount());

    Scheduler::ProcessEventsToIdle();

    // set table row height by drag & drop
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();

    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower()->GetNext();
    SwFrame* pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    SwFrame* pCellA1 = pRow1->GetLower();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();
    // select center of the bottom border of the first table cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight);
    // double the row height
    Point ptTo(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + 2 * nRowHeight);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    Point aArea(aFrom.X(), aFrom.Y() + 2);
    MouseEvent aClickEvent(aArea, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                           MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    Point aTo = rEditWin.LogicToPixel(ptTo);
    MouseEvent aMoveEvent(aTo, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                          MOUSE_LEFT);
    TrackingEvent aTEvt(aMoveEvent, TrackingEventFlags::Repeat);
    // drag & drop of cell border inside the document (and outside the table)
    // still based on the ruler code, use that to simulate dragging
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt);
    TrackingEvent aTEvt2(aMoveEvent, TrackingEventFlags::End);
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt2);
    Scheduler::ProcessEventsToIdle();
    rEditWin.CaptureMouse();
    rEditWin.ReleaseMouse();

    // this was 396 (not modified row height previously, when clicking only in a 2-pixel distance
    // from the center of the border)
    CPPUNIT_ASSERT_GREATER(tools::Long(750), pCellA1->getFrameArea().Height());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf155692)
{
    // allow resizing table rows & columns using a normal hit area
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // insert an empty paragraph
    pWrtShell->SplitNode();

    // create a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    (void)&pWrtShell->InsertTable(TableOpt, 2, 1);

    // the cursor is not inside the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    uno::Reference<text::XTextTablesSupplier> xTablesSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XNameAccess> xTableNames = xTablesSupplier->getTextTables();
    CPPUNIT_ASSERT(xTableNames->hasByName(u"Table1"_ustr));
    uno::Reference<text::XTextTable> xTable1(xTableNames->getByName(u"Table1"_ustr),
                                             uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable1->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), xTable1->getColumns()->getCount());

    Scheduler::ProcessEventsToIdle();

    // set table row height by drag & drop
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();

    SwFrame* pPage = pLayout->Lower();
    SwFrame* pBody = pPage->GetLower();
    SwFrame* pTable = pBody->GetLower()->GetNext();
    SwFrame* pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    SwFrame* pCellA1 = pRow1->GetLower();
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();
    // select center of the bottom border of the first table cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight);
    // double the row height
    Point ptTo(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + 2 * nRowHeight);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    Point aArea(aFrom.X(), aFrom.Y() + 5);
    MouseEvent aClickEvent(aArea, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                           MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    Point aTo = rEditWin.LogicToPixel(ptTo);
    MouseEvent aMoveEvent(aTo, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                          MOUSE_LEFT);
    TrackingEvent aTEvt(aMoveEvent, TrackingEventFlags::Repeat);
    // drag & drop of cell border inside the document (and outside the table)
    // still based on the ruler code, use that to simulate dragging
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt);
    TrackingEvent aTEvt2(aMoveEvent, TrackingEventFlags::End);
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt2);
    Scheduler::ProcessEventsToIdle();
    rEditWin.CaptureMouse();
    rEditWin.ReleaseMouse();

    // this was 396 (not modified row height previously, when clicking only in a 5-pixel distance
    // from the center of the border)
    CPPUNIT_ASSERT_GREATER(tools::Long(750), pCellA1->getFrameArea().Height());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf160842)
{
    createSwDoc("tdf160842.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow2 = pTable->GetLower()->GetNext();
    CPPUNIT_ASSERT(pRow2->IsRowFrame());
    auto pCellA2 = pRow2->GetLower();
    CPPUNIT_ASSERT(pCellA2);
    const SwRect& rCellA2Rect = pCellA2->getFrameArea();
    auto nRowHeight = rCellA2Rect.Height();
    // select center of the bottom cell
    Point ptFrom(rCellA2Rect.Left() + rCellA2Rect.Width() / 2, rCellA2Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // the cursor is in the table
    CPPUNIT_ASSERT(pWrtShell->IsCursorInTable());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf160836)
{
    createSwDoc("tdf160842.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // set table row height by drag & drop at images cropped by the fixed row height
    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();
    // select center of the bottom border of the first table cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight);
    // halve the row height
    Point ptTo(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + 0.5 * nRowHeight);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                           MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    Point aTo = rEditWin.LogicToPixel(ptTo);
    MouseEvent aMoveEvent(aTo, 1, MouseEventModifiers::SIMPLECLICK | MouseEventModifiers::SELECT,
                          MOUSE_LEFT);
    TrackingEvent aTEvt(aMoveEvent, TrackingEventFlags::Repeat);
    // drag & drop of cell border inside the document (and outside the table)
    // still based on the ruler code, use that to simulate dragging
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt);
    TrackingEvent aTEvt2(aMoveEvent, TrackingEventFlags::End);
    getSwDocShell()->GetView()->GetVRuler().Tracking(aTEvt2);
    Scheduler::ProcessEventsToIdle();
    rEditWin.CaptureMouse();
    rEditWin.ReleaseMouse();

    // this was 3910 (not modified row height previously)
    CPPUNIT_ASSERT_LESS(tools::Long(2000), pCellA1->getFrameArea().Height());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf161261)
{
    createSwDoc("tdf160842.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();

    // select image by clicking on it at the center of the upper cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // Then make sure that the image is selected:
    SelectionType eType = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Graphic, eType);

    uno::Reference<drawing::XShape> xShape = getShape(2);
    CPPUNIT_ASSERT(xShape.is());

    // zoom image by drag & drop using right bottom handle of the image
    const SwRect& rSelRect = pWrtShell->GetAnyCurRect(CurRectType::Frame);
    Point ptFromHandle(rSelRect.Right(), rSelRect.Bottom());
    Point aFromHandle = rEditWin.LogicToPixel(ptFromHandle);
    Point ptTo(rSelRect.Left() + rSelRect.Width() * 1.5, rSelRect.Top() + rSelRect.Height() * 1.5);
    Point aTo = rEditWin.LogicToPixel(ptTo);
    MouseEvent aClickEvent2(aFromHandle, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent2);
    MouseEvent aClickEvent3(aTo, 0, MouseEventModifiers::SIMPLEMOVE, MOUSE_LEFT);
    rEditWin.MouseMove(aClickEvent3);
    rEditWin.MouseMove(aClickEvent3);
    MouseEvent aClickEvent4(aTo, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonUp(aClickEvent4);
    Scheduler::ProcessEventsToIdle();

    // Make sure image is greater than before, instead of minimizing it to the cell size
    // This was 8707 and 6509
    CPPUNIT_ASSERT_GREATER(sal_Int32(10000), xShape->getSize().Width);
    CPPUNIT_ASSERT_GREATER(sal_Int32(8000), xShape->getSize().Height);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf161332)
{
    createSwDoc("tdf160842.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();

    // select text frame by clicking on it at the right side of the upper cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width(), rCellA1Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // Then make sure that the text frame is selected:
    SelectionType eType = pWrtShell->GetSelectionType();
    // This was false (SelectionType::Graphic)
    CPPUNIT_ASSERT_EQUAL(SelectionType::Frame, eType);

    // remove selection
    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    // select text frame by clicking on it at the right side of the bottom cell
    auto pRow2 = pRow1->GetNext();
    CPPUNIT_ASSERT(pRow2->IsRowFrame());
    auto pCellA2 = pRow2->GetLower();
    CPPUNIT_ASSERT(pCellA2);
    const SwRect& rCellA2Rect = pCellA2->getFrameArea();
    auto nRow2Height = rCellA2Rect.Height();
    Point ptFrom2(rCellA2Rect.Left() + rCellA2Rect.Width(), rCellA2Rect.Top() + nRow2Height / 2);
    Point aFrom2 = rEditWin.LogicToPixel(ptFrom2);
    MouseEvent aClickEvent2(aFrom2, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent2);
    rEditWin.MouseButtonUp(aClickEvent2);

    // Then make sure that the text frame is selected:
    SelectionType eType2 = pWrtShell->GetSelectionType();
    // This was false (SelectionType::Graphic)
    CPPUNIT_ASSERT_EQUAL(SelectionType::Frame, eType2);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf161426)
{
    createSwDoc("tdf161426.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    auto pCellB1 = pCellA1->GetNext();
    const SwRect& rCellB1Rect = pCellB1->getFrameArea();
    auto nRowHeight = rCellB1Rect.Height();

    // select text frame by clicking on it at the right side of the upper right cell
    Point ptFrom(rCellB1Rect.Left() + rCellB1Rect.Width(), rCellB1Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // Then make sure that the text frame is selected:
    SelectionType eType = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Frame, eType);

    // remove selection
    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    // select text frame by clicking on it at the right side of the bottom right cell
    auto pRow2 = pRow1->GetNext();
    CPPUNIT_ASSERT(pRow2->IsRowFrame());
    auto pCellA2 = pRow2->GetLower();
    CPPUNIT_ASSERT(pCellA2);
    auto pCellB2 = pCellA2->GetNext();
    CPPUNIT_ASSERT(pCellB2);
    const SwRect& rCellB2Rect = pCellB2->getFrameArea();
    auto nRow2Height = rCellB2Rect.Height();
    Point ptFrom2(rCellB2Rect.Left() + rCellB2Rect.Width(), rCellB2Rect.Top() + nRow2Height / 2);
    Point aFrom2 = rEditWin.LogicToPixel(ptFrom2);
    MouseEvent aClickEvent2(aFrom2, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent2);
    rEditWin.MouseButtonUp(aClickEvent2);

    // Then make sure that the text frame is selected:
    SelectionType eType2 = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Frame, eType2);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf161426_content)
{
    createSwDoc("tdf161426.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    auto pCellB1 = pCellA1->GetNext();
    const SwRect& rCellB1Rect = pCellB1->getFrameArea();
    auto nRowHeight = rCellB1Rect.Height();

    // select content of the B1 by clicking on the center of it
    Point ptFrom(rCellB1Rect.Left() + rCellB1Rect.Width() / 2, rCellB1Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // Then make sure that the cursor in the table:
    SelectionType eType2 = pWrtShell->GetSelectionType();
    // This was false
    bool bCursorInTable = eType2 == (SelectionType::Text | SelectionType::Table);
    CPPUNIT_ASSERT(bCursorInTable);

    // select content of the B2 by clicking on the center of it
    auto pRow2 = pRow1->GetNext();
    CPPUNIT_ASSERT(pRow2->IsRowFrame());
    auto pCellA2 = pRow2->GetLower();
    CPPUNIT_ASSERT(pCellA2);
    auto pCellB2 = pCellA2->GetNext();
    CPPUNIT_ASSERT(pCellB2);
    const SwRect& rCellB2Rect = pCellB2->getFrameArea();
    auto nRow2Height = rCellB2Rect.Height();
    Point ptFrom2(rCellB2Rect.Left() + rCellB2Rect.Width() / 2,
                  rCellB2Rect.Top() + nRow2Height / 2);
    Point aFrom2 = rEditWin.LogicToPixel(ptFrom2);
    MouseEvent aClickEvent2(aFrom2, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent2);
    rEditWin.MouseButtonUp(aClickEvent2);

    // Then make sure that the cursor in the table:
    SelectionType eType3 = pWrtShell->GetSelectionType();
    // This was false
    bCursorInTable = eType3 == (SelectionType::Text | SelectionType::Table);
    CPPUNIT_ASSERT(bCursorInTable);

    // select content of the A2 by clicking on the center of it
    const SwRect& rCellA2Rect = pCellA2->getFrameArea();
    Point ptFrom3(rCellA2Rect.Left() + rCellA2Rect.Width() / 2,
                  rCellA2Rect.Top() + nRow2Height / 2);
    Point aFrom3 = rEditWin.LogicToPixel(ptFrom3);
    MouseEvent aClickEvent3(aFrom3, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent3);
    rEditWin.MouseButtonUp(aClickEvent3);

    // Then make sure that the cursor in the table:
    SelectionType eType4 = pWrtShell->GetSelectionType();
    // This was false
    bCursorInTable = eType4 == (SelectionType::Text | SelectionType::Table);
    CPPUNIT_ASSERT(bCursorInTable);

    // select content of the A1 by clicking on the center of it
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRow1Height = rCellA1Rect.Height();
    Point ptFrom4(rCellA1Rect.Left() + rCellA1Rect.Width() / 2,
                  rCellA1Rect.Top() + nRow1Height / 2);
    Point aFrom4 = rEditWin.LogicToPixel(ptFrom4);
    MouseEvent aClickEvent4(aFrom4, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent4);
    rEditWin.MouseButtonUp(aClickEvent4);

    // Then make sure that the text frame is selected:
    SelectionType eType5 = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Graphic, eType5);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf161360)
{
    createSwDoc("tdf160842.fodt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // the cursor is not in the table
    CPPUNIT_ASSERT(!pWrtShell->IsCursorInTable());

    SwRootFrame* pLayout = pDoc->getIDocumentLayoutAccess().GetCurrentLayout();
    auto pPage = dynamic_cast<SwPageFrame*>(pLayout->Lower());
    CPPUNIT_ASSERT(pPage);
    const SwSortedObjs& rPageObjs = *pPage->GetSortedObjs();
    CPPUNIT_ASSERT_EQUAL(static_cast<size_t>(1), rPageObjs.size());
    auto pPageFly = dynamic_cast<SwFlyAtContentFrame*>(rPageObjs[0]);
    CPPUNIT_ASSERT(pPageFly);
    auto pTable = dynamic_cast<SwTabFrame*>(pPageFly->GetLower());
    CPPUNIT_ASSERT(pTable);
    auto pRow1 = pTable->GetLower();
    CPPUNIT_ASSERT(pRow1->IsRowFrame());
    auto pCellA1 = pRow1->GetLower();
    CPPUNIT_ASSERT(pCellA1);
    const SwRect& rCellA1Rect = pCellA1->getFrameArea();
    auto nRowHeight = rCellA1Rect.Height();

    // select image by clicking on it at the center of the upper cell
    Point ptFrom(rCellA1Rect.Left() + rCellA1Rect.Width() / 2, rCellA1Rect.Top() + nRowHeight / 2);
    vcl::Window& rEditWin = getSwDocShell()->GetView()->GetEditWin();
    Point aFrom = rEditWin.LogicToPixel(ptFrom);
    MouseEvent aClickEvent(aFrom, 1, MouseEventModifiers::SIMPLECLICK, MOUSE_LEFT);
    rEditWin.MouseButtonDown(aClickEvent);
    rEditWin.MouseButtonUp(aClickEvent);

    // Then make sure that the image is selected:
    SelectionType eType = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Graphic, eType);

    // select the text frame instead of the image
    // by pressing Escape
    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    // Then make sure that the cursor in the table:
    SelectionType eType2 = pWrtShell->GetSelectionType();
    // This was false (only SelectionType::Text)
    bool bCursorInTable = eType2 == (SelectionType::Text | SelectionType::Table);
    CPPUNIT_ASSERT(bCursorInTable);

    // select the text frame by pressing Escape again
    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    eType2 = pWrtShell->GetSelectionType();
    CPPUNIT_ASSERT_EQUAL(SelectionType::Frame, eType2);

    // deselect the text frame by pressing Escape again
    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    eType2 = pWrtShell->GetSelectionType();
    // The text cursor is after the floating table
    CPPUNIT_ASSERT_EQUAL(SelectionType::Text, eType2);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157533)
{
    // load a table with objects positioned at beginning of text lines
    createSwDoc("tdf157533.fodt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    auto pShell = getSwDocShell()->GetFEShell();
    CPPUNIT_ASSERT(pShell);

    auto xModel = mxComponent.queryThrow<frame::XModel>();
    uno::Reference<drawing::XShape> xShape(getShapeByName(u"Objet2"));
    uno::Reference<view::XSelectionSupplier> xCtrl(xModel->getCurrentController(), uno::UNO_QUERY);
    xCtrl->select(uno::Any(xShape));

    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    // Then make sure that the cursor in the table:
    SelectionType eType2 = pWrtShell->GetSelectionType();
    // This was false (only SelectionType::Text)
    bool bCursorInTable = eType2 == (SelectionType::Text | SelectionType::Table);
    CPPUNIT_ASSERT(bCursorInTable);

    SwTextNode* pTextNode = pWrtShell->GetCursor()->GetPointNode().GetTextNode();
    // This was false (not in the same paragraph and cell)
    CPPUNIT_ASSERT(pTextNode->GetText().indexOf("and the second formula") > -1);

    uno::Reference<drawing::XShape> xShape2(getShapeByName(u"Objet11"));
    xCtrl->select(uno::Any(xShape2));

    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    SwTextNode* pTextNode2 = pWrtShell->GetCursor()->GetPointNode().GetTextNode();
    // This was false (lost text cursor inside the frame of the formula)
    CPPUNIT_ASSERT(pTextNode2->GetTableBox());
    SwTableNode* pTableNode = pWrtShell->GetCursor()->GetPointNode().FindTableNode();
    SwTable& rTable = pTableNode->GetTable();
    // cursor in the same cell
    bool bSameBox = pTextNode2->GetTableBox() == rTable.GetTableBox(u"A1"_ustr);
    CPPUNIT_ASSERT(bSameBox);

    uno::Reference<drawing::XShape> xShape3(getShapeByName(u"Objet10"));
    xCtrl->select(uno::Any(xShape3));

    dispatchCommand(mxComponent, u".uno:Escape"_ustr, {});

    SwTextNode* pTextNode3 = pWrtShell->GetCursor()->GetPointNode().GetTextNode();
    // This was false (lost text cursor inside the frame of the formula)
    CPPUNIT_ASSERT(pTextNode3->GetTableBox());
    // cursor in the same cell
    bSameBox = pTextNode3->GetTableBox() == rTable.GetTableBox(u"B1"_ustr);
    CPPUNIT_ASSERT(bSameBox);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf115132)
{
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    std::vector<OUString> vTestTableNames;

    // Create an empty paragraph that will separate first table from the rest
    pWrtShell->SplitNode();
    pWrtShell->StartOfSection();
    // Create a table at the start of document body
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    const SwTable* pTable = &pWrtShell->InsertTable(TableOpt, 2, 3);
    const SwTableFormat* pFormat = pTable->GetFrameFormat();
    CPPUNIT_ASSERT(pFormat);
    vTestTableNames.push_back(pFormat->GetName().toString());
    pWrtShell->EndOfSection();
    // Create a table after a paragraph
    pTable = &pWrtShell->InsertTable(TableOpt, 2, 3);
    pFormat = pTable->GetFrameFormat();
    CPPUNIT_ASSERT(pFormat);
    vTestTableNames.push_back(pFormat->GetName().toString());
    // Create a table immediately after the previous
    pTable = &pWrtShell->InsertTable(TableOpt, 2, 3);
    pFormat = pTable->GetFrameFormat();
    CPPUNIT_ASSERT(pFormat);
    vTestTableNames.push_back(pFormat->GetName().toString());
    // Create a nested table in the middle of last row
    pWrtShell->GotoTable(UIName(vTestTableNames.back()));
    for (int i = 0; i < 4; ++i)
        pWrtShell->GoNextCell(false);
    pTable = &pWrtShell->InsertTable(TableOpt, 2, 3);
    pFormat = pTable->GetFrameFormat();
    CPPUNIT_ASSERT(pFormat);
    vTestTableNames.push_back(pFormat->GetName().toString());

    // Now check that in any cell in all tables we don't go out of a cell
    // using Delete or Backspace. We test cases when a table is the first node;
    // when we are in a first/middle/last cell in a row; when there's a paragraph
    // before/after this cell; when there's another table before/after this cell;
    // in nested table.
    for (const auto& rTableName : vTestTableNames)
    {
        pWrtShell->GotoTable(UIName(rTableName));
        do
        {
            const SwStartNode* pNd = pWrtShell->GetCursor()->GetPointNode().FindTableBoxStartNode();
            pWrtShell->DelRight();
            CPPUNIT_ASSERT_EQUAL(pNd,
                                 pWrtShell->GetCursor()->GetPointNode().FindTableBoxStartNode());
            pWrtShell->DelLeft();
            CPPUNIT_ASSERT_EQUAL(pNd,
                                 pWrtShell->GetCursor()->GetPointNode().FindTableBoxStartNode());
        } while (pWrtShell->GoNextCell(false));
    }
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testXDrawPagesSupplier)
{
    createSwDoc();
    uno::Reference<drawing::XDrawPagesSupplier> xDrawPagesSupplier(mxComponent, uno::UNO_QUERY);
    CPPUNIT_ASSERT_MESSAGE("XDrawPagesSupplier interface is unavailable", xDrawPagesSupplier.is());
    uno::Reference<drawing::XDrawPages> xDrawPages = xDrawPagesSupplier->getDrawPages();
    CPPUNIT_ASSERT(xDrawPages.is());
    CPPUNIT_ASSERT_EQUAL_MESSAGE("There must be only a single DrawPage in Writer documents",
                                 sal_Int32(1), xDrawPages->getCount());
    uno::Any aDrawPage = xDrawPages->getByIndex(0);
    uno::Reference<drawing::XDrawPage> xDrawPageFromXDrawPages(aDrawPage, uno::UNO_QUERY);
    CPPUNIT_ASSERT(xDrawPageFromXDrawPages.is());

    uno::Reference<drawing::XDrawPageSupplier> xDrawPageSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage = xDrawPageSupplier->getDrawPage();
    CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "The DrawPage accessed using XDrawPages must be the same as using XDrawPageSupplier",
        xDrawPage.get(), xDrawPageFromXDrawPages.get());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf116403)
{
    createSwDoc("tdf116403-considerborders.odt");
    // Check that before ToX update, the tab stop position is the old one
    uno::Reference<text::XTextRange> xParagraph = getParagraph(2, u"1\t1"_ustr);
    auto aTabs = getProperty<uno::Sequence<style::TabStop>>(xParagraph, u"ParaTabStops"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aTabs.getLength());
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(17000), aTabs[0].Position);

    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwTOXBase* pTOX = pWrtShell->GetTOX(0);
    CPPUNIT_ASSERT(pTOX);
    pWrtShell->UpdateTableOf(*pTOX);

    xParagraph = getParagraph(2, u"1\t1"_ustr);
    aTabs = getProperty<uno::Sequence<style::TabStop>>(xParagraph, u"ParaTabStops"_ustr);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(1), aTabs.getLength());
    // This was still 17000, refreshing ToX didn't take borders spacings and widths into account
    CPPUNIT_ASSERT_EQUAL_MESSAGE("Page borders must be considered for right-aligned tabstop",
                                 static_cast<sal_Int32>(17000 - 2 * 500 - 2 * 1 - 1),
                                 aTabs[0].Position);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testHtmlCopyImages)
{
    // Load a document with an image.
    createSwDoc("image.odt");
    SwDoc* pDoc = getSwDoc();

    // Trigger the copy part of HTML copy&paste.
    WriterRef xWrt = new SwHTMLWriter(/*rBaseURL=*/OUString());
    CPPUNIT_ASSERT(xWrt.is());

    xWrt->m_bWriteClipboardDoc = true;
    xWrt->m_bWriteOnlyFirstTable = false;
    xWrt->SetShowProgress(false);
    {
        SvFileStream aStream(maTempFile.GetURL(), StreamMode::WRITE | StreamMode::TRUNC);
        SwWriter aWrt(aStream, *pDoc);
        aWrt.Write(xWrt);
    }
    htmlDocUniquePtr pHtmlDoc = parseHtml(maTempFile);
    CPPUNIT_ASSERT(pHtmlDoc);

    // This failed, image was lost during HTML copy.
    OUString aImage = getXPath(pHtmlDoc, "/html/body/p/img", "src");
    // Also make sure that the image is not embedded (e.g. Word doesn't handle
    // embedded images).
    CPPUNIT_ASSERT(aImage.startsWith("file:///"));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf158198)
{
    createSwDoc("tdf158198.odt");
    uno::Reference<text::XBookmarksSupplier> xBookmarksSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<container::XIndexAccess> xBookmarksByIdx(xBookmarksSupplier->getBookmarks(),
                                                            uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), xBookmarksByIdx->getCount());
    uno::Reference<container::XNameAccess> xBookmarksByName = xBookmarksSupplier->getBookmarks();
    CPPUNIT_ASSERT(xBookmarksByName->hasByName(u"WORD"_ustr));
    CPPUNIT_ASSERT(xBookmarksByName->hasByName(u"PARAGRAPH"_ustr));

    uno::Reference<text::XTextContent> xBookmark1(xBookmarksByName->getByName(u"WORD"_ustr),
                                                  uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xAnchor1 = xBookmark1->getAnchor();

    uno::Reference<text::XTextContent> xBookmark2(xBookmarksByName->getByName(u"PARAGRAPH"_ustr),
                                                  uno::UNO_QUERY);
    uno::Reference<text::XTextRange> xAnchor2 = xBookmark2->getAnchor();
    CPPUNIT_ASSERT_EQUAL(u"{WORD}"_ustr, xAnchor1->getString());
    CPPUNIT_ASSERT_EQUAL(u"{PARAGRAPH}"_ustr, xAnchor2->getString());

    xAnchor1->setString("");
    xAnchor2->setString("");

    // Without the fix in place, this test would have failed here
    // - Expected: 2
    // - Actual  : 1
    CPPUNIT_ASSERT_EQUAL(static_cast<sal_Int32>(2), xBookmarksByIdx->getCount());
    CPPUNIT_ASSERT(xAnchor1->getString().isEmpty());
    CPPUNIT_ASSERT(xAnchor2->getString().isEmpty());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf116789)
{
    createSwDoc("tdf116789.fodt");
    uno::Reference<text::XBookmarksSupplier> xBookmarksSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<text::XText> xText1;
    uno::Reference<text::XText> xText2;
    {
        uno::Reference<text::XTextContent> xBookmark(
            xBookmarksSupplier->getBookmarks()->getByName(u"Bookmark 1"_ustr), uno::UNO_QUERY);
        xText1 = xBookmark->getAnchor()->getText();
    }
    {
        uno::Reference<text::XTextContent> xBookmark(
            xBookmarksSupplier->getBookmarks()->getByName(u"Bookmark 1"_ustr), uno::UNO_QUERY);
        xText2 = xBookmark->getAnchor()->getText();
    }
    // This failed, we got two different SwXCell for the same bookmark anchor text.
    CPPUNIT_ASSERT_EQUAL(xText1, xText2);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf91801)
{
    // Tests calculation with several user field variables without prior user fields
    createSwDoc("tdf91801.fodt");
    uno::Reference<text::XTextTable> xTable(getParagraphOrTable(1), uno::UNO_QUERY);
    uno::Reference<table::XCell> xCell(xTable->getCellByName(u"A1"_ustr));
    CPPUNIT_ASSERT_EQUAL(555.0, xCell->getValue());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf51223)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    sw::UndoManager& rUndoManager = pDoc->GetUndoManager();
    SwNodeOffset nIndex = pWrtShell->GetCursor()->GetPointNode().GetIndex();
    pWrtShell->Insert(u"i"_ustr);
    pWrtShell->SplitNode(true);
    CPPUNIT_ASSERT_EQUAL(u"I"_ustr, static_cast<SwTextNode*>(pDoc->GetNodes()[nIndex])->GetText());
    rUndoManager.Undo();
    CPPUNIT_ASSERT_EQUAL(u"i"_ustr, static_cast<SwTextNode*>(pDoc->GetNodes()[nIndex])->GetText());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testFontEmbedding)
{
#if HAVE_MORE_FONTS && !defined(MACOSX)
    createSwDoc("testFontEmbedding.odt");

    OString aContentBaseXpath("/office:document-content/office:font-face-decls"_ostr);
    OString aSettingsBaseXpath(
        "/office:document-settings/office:settings/config:config-item-set"_ostr);

    xmlDocUniquePtr pXmlDoc;

    // Get document settings
    uno::Reference<lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY_THROW);
    uno::Reference<beans::XPropertySet> xProps(
        xFactory->createInstance(u"com.sun.star.document.Settings"_ustr), uno::UNO_QUERY_THROW);

    // Check font embedding state
    CPPUNIT_ASSERT_EQUAL(false, xProps->getPropertyValue(u"EmbedFonts"_ustr).get<bool>());
    CPPUNIT_ASSERT_EQUAL(false, xProps->getPropertyValue(u"EmbedOnlyUsedFonts"_ustr).get<bool>());
    // Font scripts should be enabled by default, however this has no effect unless "EmbedOnlyUsedFonts" is enabled
    CPPUNIT_ASSERT_EQUAL(true, xProps->getPropertyValue(u"EmbedLatinScriptFonts"_ustr).get<bool>());
    CPPUNIT_ASSERT_EQUAL(true, xProps->getPropertyValue(u"EmbedAsianScriptFonts"_ustr).get<bool>());
    CPPUNIT_ASSERT_EQUAL(true,
                         xProps->getPropertyValue(u"EmbedComplexScriptFonts"_ustr).get<bool>());

    // CASE 1 - no font embedding enabled

    // Save the document
    save(u"writer8"_ustr);
    CPPUNIT_ASSERT(maTempFile.IsValid());

    // Check setting - No font embedding should be enabled
    pXmlDoc = parseExport(u"settings.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedFonts']", u"false");

    // Check content - No font-face-src nodes should be present
    pXmlDoc = parseExport(u"content.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);

    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face", 6);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']");
    assertXPath(
        pXmlDoc,
        aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']/svg:font-face-src", 0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Sans1']/svg:font-face-src",
                0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif']/svg:font-face-src",
                0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif1']/svg:font-face-src",
                0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Carlito']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Carlito']/svg:font-face-src", 0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Caladea']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Caladea']/svg:font-face-src", 0);

    // CASE 2 - font embedding enabled, but embed used fonts disabled

    // Enable font embedding, disable embedding used font only
    xProps->setPropertyValue(u"EmbedFonts"_ustr, uno::Any(true));
    xProps->setPropertyValue(u"EmbedOnlyUsedFonts"_ustr, uno::Any(false));

    // Save the document again
    save(u"writer8"_ustr);
    CPPUNIT_ASSERT(maTempFile.IsValid());

    // Check setting - font embedding should be enabled + embed only used fonts and scripts
    pXmlDoc = parseExport(u"settings.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedFonts']", u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedOnlyUsedFonts']",
        u"false");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedLatinScriptFonts']",
        u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedAsianScriptFonts']",
        u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedComplexScriptFonts']",
        u"true");

    // Check content - font-face-src should be present only for "Liberation Sans" fonts

    pXmlDoc = parseExport(u"content.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);

    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face", 6);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']");
    assertXPath(
        pXmlDoc,
        aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']/svg:font-face-src", 1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Sans1']/svg:font-face-src",
                1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif']/svg:font-face-src",
                1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif1']/svg:font-face-src",
                1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Carlito']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Carlito']/svg:font-face-src", 1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Caladea']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Caladea']/svg:font-face-src", 1);

    // CASE 3 - font embedding enabled, embed only used fonts enabled

    // Enable font embedding and setting to embed used fonts only
    xProps->setPropertyValue(u"EmbedFonts"_ustr, uno::Any(true));
    xProps->setPropertyValue(u"EmbedOnlyUsedFonts"_ustr, uno::Any(true));
    xProps->setPropertyValue(u"EmbedLatinScriptFonts"_ustr, uno::Any(true));
    xProps->setPropertyValue(u"EmbedAsianScriptFonts"_ustr, uno::Any(true));
    xProps->setPropertyValue(u"EmbedComplexScriptFonts"_ustr, uno::Any(true));

    // Save the document again
    save(u"writer8"_ustr);
    CPPUNIT_ASSERT(maTempFile.IsValid());

    // Check setting - font embedding should be enabled + embed only used fonts and scripts
    pXmlDoc = parseExport(u"settings.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedFonts']", u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedOnlyUsedFonts']",
        u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedLatinScriptFonts']",
        u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedAsianScriptFonts']",
        u"true");
    assertXPathContent(
        pXmlDoc, aSettingsBaseXpath + "/config:config-item[@config:name='EmbedComplexScriptFonts']",
        u"true");

    // Check content - font-face-src should be present only for "Liberation Sans" fonts

    pXmlDoc = parseExport(u"content.xml"_ustr);
    CPPUNIT_ASSERT(pXmlDoc);

    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face", 6);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']");
    assertXPath(
        pXmlDoc,
        aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans']/svg:font-face-src", 0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Sans1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Sans1']/svg:font-face-src",
                0);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif']/svg:font-face-src",
                1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Liberation Serif1']");
    assertXPath(pXmlDoc,
                aContentBaseXpath
                    + "/style:font-face[@style:name='Liberation Serif1']/svg:font-face-src",
                1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Carlito']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Carlito']/svg:font-face-src", 1);
    assertXPath(pXmlDoc, aContentBaseXpath + "/style:font-face[@style:name='Caladea']");
    assertXPath(pXmlDoc,
                aContentBaseXpath + "/style:font-face[@style:name='Caladea']/svg:font-face-src", 0);
#endif
}

// Unit test for fix inconsistent bookmark behavior around at-char/as-char anchored frames
//
// We have a placeholder character in the sw doc model for as-char anchored frames,
// so it's possible to have a bookmark before/after the frame or a non-collapsed bookmark
// which covers the frame. The same is not true for at-char anchored frames,
// where the anchor points to a doc model position, but there is no placeholder character.
// If a bookmark is created covering the start and end of the anchor of the frame,
// internally we create a collapsed bookmark which has the same position as the anchor of the frame.
// When this doc model is handled by SwXParagraph::createEnumeration(),
// first the frame and then the bookmark is appended to the text portion enumeration,
// so your bookmark around the frame is turned into a collapsed bookmark after the frame.
// (The same happens when we roundtrip an ODT document representing this doc model.)
//
// Fix the problem by inserting collapsed bookmarks with affected anchor positions
// (same position is the anchor for an at-char frame) into the enumeration in two stages:
// first the start of them before frames and then the end of them + other bookmarks.
// This way UNO API users get their non-collapsed bookmarks around at-char anchored frames,
// similar to as-char ones.
CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testInconsistentBookmark)
{
    // create test document with text and bookmark
    {
        createSwDoc("testInconsistentBookmark.ott");
        SwDoc* pDoc = getSwDoc();
        IDocumentMarkAccess& rIDMA(*pDoc->getIDocumentMarkAccess());
        SwNodeIndex aIdx(pDoc->GetNodes().GetEndOfContent(), -1);
        SwCursor aPaM(SwPosition(aIdx), nullptr);
        aPaM.SetMark();
        aPaM.MovePara(GoCurrPara, fnParaStart);
        aPaM.MovePara(GoCurrPara, fnParaEnd);
        rIDMA.makeMark(aPaM, SwMarkName(u"Mark"_ustr), IDocumentMarkAccess::MarkType::BOOKMARK,
                       ::sw::mark::InsertMode::New);
        aPaM.Exchange();
        aPaM.DeleteMark();
    }

    // save document and verify the bookmark scoup
    {
        // save document
        save(u"writer8"_ustr);

        // load only content.xml
        xmlDocUniquePtr pXmlDoc = parseExport(u"content.xml"_ustr);
        static constexpr const char* aPath(
            "/office:document-content/office:body/office:text/text:p");

        const int pos1 = getXPathPosition(pXmlDoc, aPath, "bookmark-start");
        const int pos2 = getXPathPosition(pXmlDoc, aPath, "control");
        const int pos3 = getXPathPosition(pXmlDoc, aPath, "bookmark-end");

        CPPUNIT_ASSERT_GREATER(pos1, pos2);
        CPPUNIT_ASSERT_GREATER(pos2, pos3);
    }
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testSpellOnlineParameter)
{
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pOpt = pWrtShell->GetViewOptions();
    bool bSet = pOpt->IsOnlineSpell();

    uno::Sequence<beans::PropertyValue> params
        = comphelper::InitPropertySequence({ { "Enable", uno::Any(!bSet) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);
    CPPUNIT_ASSERT_EQUAL(!bSet, pOpt->IsOnlineSpell());

    // set the same state as now and we don't expect any change (no-toggle)
    params = comphelper::InitPropertySequence({ { "Enable", uno::Any(!bSet) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);
    CPPUNIT_ASSERT_EQUAL(!bSet, pOpt->IsOnlineSpell());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf124603)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pOpt = pWrtShell->GetViewOptions();
    uno::Sequence<beans::PropertyValue> params
        = comphelper::InitPropertySequence({ { "Enable", uno::Any(true) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);

    // Automatic Spell Checking is enabled

    CPPUNIT_ASSERT(pOpt->IsOnlineSpell());

    // check available en_US dictionary and test spelling with it
    uno::Reference<XLinguServiceManager2> xLngSvcMgr(GetLngSvcMgr_Impl());
    uno::Reference<XSpellChecker1> xSpell;
    xSpell.set(xLngSvcMgr->getSpellChecker(), UNO_QUERY);
    LanguageType eLang
        = LanguageTag::convertToLanguageType(lang::Locale(u"en"_ustr, u"US"_ustr, OUString()));
    if (xSpell.is() && xSpell->hasLanguage(static_cast<sal_uInt16>(eLang)))
    {
        // Type a correct word

        emulateTyping(u"the ");
        SwCursorShell* pShell(pDoc->GetEditShell());
        CPPUNIT_ASSERT(pShell);
        SwTextNode* pNode = pShell->GetCursor()->GetPointNode().GetTextNode();
        // no bad word
        CPPUNIT_ASSERT_EQUAL(static_cast<SwWrongList*>(nullptr), pNode->GetWrong());

        // Create a bad word from the good: "the" -> "thex"

        pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 1, /*bBasicCall=*/false);
        emulateTyping(u"x");
        // tdf#92036 pending spell checking
        bool bPending = !pNode->GetWrong() || !pNode->GetWrong()->Count();
        CPPUNIT_ASSERT(bPending);

        // Move right, leave the bad word - since the fix for tdf#136294, this triggers the check

        pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/false, 1, /*bBasicCall=*/false);
        Scheduler::ProcessEventsToIdle();
        CPPUNIT_ASSERT(pNode->GetWrong());
        // This was 0 (pending spell checking)
        CPPUNIT_ASSERT_EQUAL(sal_uInt16(1), pNode->GetWrong()->Count());
    }
}

#if !defined(MACOSX)
CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf45949)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pOpt = pWrtShell->GetViewOptions();
    uno::Sequence<beans::PropertyValue> params
        = comphelper::InitPropertySequence({ { "Enable", uno::Any(true) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);

    // Automatic Spell Checking is enabled
    CPPUNIT_ASSERT(pOpt->IsOnlineSpell());

    // check available en_US dictionary and test spelling with it
    uno::Reference<XLinguServiceManager2> xLngSvcMgr(GetLngSvcMgr_Impl());
    uno::Reference<XSpellChecker1> xSpell;
    xSpell.set(xLngSvcMgr->getSpellChecker(), UNO_QUERY);
    LanguageType eLang
        = LanguageTag::convertToLanguageType(lang::Locale(u"en"_ustr, u"US"_ustr, OUString()));
    if (xSpell.is() && xSpell->hasLanguage(static_cast<sal_uInt16>(eLang)))
    {
        emulateTyping(u"baaad http://www.baaad.org baaad");
        SwCursorShell* pShell(pDoc->GetEditShell());
        CPPUNIT_ASSERT(pShell);
        SwTextNode* pNode = pShell->GetCursor()->GetPointNode().GetTextNode();

        // tdf#152492: Without the fix in place, this test would have failed with
        // - Expected: 1
        // - Actual  : 3
        CPPUNIT_ASSERT_EQUAL(sal_uInt16(1), pNode->GetWrong()->Count());

        pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 10, /*bBasicCall=*/false);
        emulateTyping(u" ");

        CPPUNIT_ASSERT_EQUAL(sal_uInt16(2), pNode->GetWrong()->Count());

        pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 6, /*bBasicCall=*/false);
        emulateTyping(u" ");

        // Move down to trigger spell checking
        pWrtShell->Down(/*bSelect=*/false, 1);
        Scheduler::ProcessEventsToIdle();

        // Without the fix in place, this test would have failed with
        // - Expected: 3
        // - Actual  : 2
        CPPUNIT_ASSERT_EQUAL(sal_uInt16(3), pNode->GetWrong()->Count());
    }
}
#endif

#if !defined(MACOSX)
CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf157442)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pOpt = pWrtShell->GetViewOptions();
    uno::Sequence<beans::PropertyValue> params
        = comphelper::InitPropertySequence({ { "Enable", uno::Any(true) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);

    // Automatic Spell Checking is enabled
    CPPUNIT_ASSERT(pOpt->IsOnlineSpell());

    // check available en_US dictionary and test spelling with it
    uno::Reference<XLinguServiceManager2> xLngSvcMgr(GetLngSvcMgr_Impl());
    uno::Reference<XSpellChecker1> xSpell;
    xSpell.set(xLngSvcMgr->getSpellChecker(), UNO_QUERY);
    LanguageType eLang
        = LanguageTag::convertToLanguageType(lang::Locale(u"en"_ustr, u"US"_ustr, OUString()));
    if (xSpell.is() && xSpell->hasLanguage(static_cast<sal_uInt16>(eLang)))
    {
        uno::Reference<linguistic2::XLinguProperties> xLinguProperties(
            LinguMgr::GetLinguPropertySet());

        // Spell with digits is disabled by default
        CPPUNIT_ASSERT_EQUAL(sal_False, xLinguProperties->getIsSpellWithDigits());

        emulateTyping(u"ErrorError Treee2 ");
        SwCursorShell* pShell(pDoc->GetEditShell());
        CPPUNIT_ASSERT(pShell);
        SwTextNode* pNode = pShell->GetCursor()->GetPointNode().GetTextNode();

        // Without the fix in place, this test would have crashed because GetWrong() returns nullptr
        CPPUNIT_ASSERT_EQUAL(sal_uInt16(1), pNode->GetWrong()->Count());
    }
}
#endif

#if !defined(MACOSX)
CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf65535)
{
    createSwDoc("tdf65535.fodt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    const SwViewOption* pOpt = pWrtShell->GetViewOptions();
    uno::Sequence<beans::PropertyValue> params
        = comphelper::InitPropertySequence({ { "Enable", uno::Any(true) } });
    dispatchCommand(mxComponent, u".uno:SpellOnline"_ustr, params);

    // Automatic Spell Checking is enabled

    CPPUNIT_ASSERT(pOpt->IsOnlineSpell());

    // check available en_US dictionary and test spelling with it
    uno::Reference<XLinguServiceManager2> xLngSvcMgr(GetLngSvcMgr_Impl());
    uno::Reference<XSpellChecker1> xSpell;
    xSpell.set(xLngSvcMgr->getSpellChecker(), UNO_QUERY);
    LanguageType eLang
        = LanguageTag::convertToLanguageType(lang::Locale(u"en"_ustr, u"US"_ustr, OUString()));
    if (xSpell.is() && xSpell->hasLanguage(static_cast<sal_uInt16>(eLang)))
    {
        // trigger online spell checking by (a few) spaces to be sure to get it

        emulateTyping(u" ");

        // FIXME: inserting a space before the bad word removes the red underline
        // Insert a second space to get the red underline (back)

        pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 1, /*bBasicCall=*/false);
        emulateTyping(u" ");

        // Select the bad word (right to left, as during right click)

        pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/false, 5, /*bBasicCall=*/false);
        pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/true, 4, /*bBasicCall=*/false);

        // choose the word "Baaed" from the spelling suggestions of the context menu

        SfxViewShell* pViewShell = SfxViewShell::Current();
        CPPUNIT_ASSERT(pViewShell);
        {
            static constexpr OUStringLiteral sApplyRule(u"Spelling_Baaed");
            SfxStringItem aApplyItem(FN_PARAM_1, sApplyRule);
            pViewShell->GetViewFrame().GetDispatcher()->ExecuteList(
                SID_SPELLCHECK_APPLY_SUGGESTION, SfxCallMode::SYNCHRON, { &aApplyItem });
        }

        // check the replacement in the text

        CPPUNIT_ASSERT_EQUAL(u"  Baaed"_ustr, getParagraph(1)->getString());
    }

    // check the remaining comment

    tools::JsonWriter aJsonWriter;
    SwXTextDocument* pTextDoc = getSwTextDoc();
    pTextDoc->getPostIts(aJsonWriter);
    OString pChar = aJsonWriter.finishAndGetAsOString();
    std::stringstream aStream((std::string(pChar)));
    boost::property_tree::ptree aTree;
    boost::property_tree::read_json(aStream, aTree);
    OString sCommentText;
    for (const boost::property_tree::ptree::value_type& rValue : aTree.get_child("comments"))
    {
        const boost::property_tree::ptree& rComment = rValue.second;
        sCommentText = OString(rComment.get<std::string>("html"));
    }
    // This was false (lost comment with spelling replacement)
    CPPUNIT_ASSERT_EQUAL("<div>with comment</div>"_ostr, sCommentText);
}
#endif

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testRedlineAutoCorrect)
{
    createSwDoc("redline-autocorrect.fodt");
    SwDoc* pDoc = getSwDoc();

    dispatchCommand(mxComponent, u".uno:GoToEndOfDoc"_ustr, {});

    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // show tracked deletion with enabled change tracking
    RedlineFlags const nMode(pWrtShell->GetRedlineFlags() | RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & (RedlineFlags::ShowDelete | RedlineFlags::ShowInsert));
    pWrtShell->SetRedlineFlags(nMode);
    CPPUNIT_ASSERT(nMode & RedlineFlags::ShowDelete);

    CPPUNIT_ASSERT_MESSAGE("redlining should be on",
                           pDoc->getIDocumentRedlineAccess().IsRedlineOn());

    emulateTyping(u" ");

    // tdf#83419 This was "Ts " removing the deletion of "t" silently by sentence capitalization
    OUString sReplaced(u"ts "_ustr);
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // hide delete redlines
    pWrtShell->SetRedlineFlags(nMode & ~RedlineFlags::ShowDelete);

    // repeat it with not visible redlining
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    emulateTyping(u" ");

    sReplaced = "S ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // show delete redlines
    pWrtShell->SetRedlineFlags(nMode);

    // This still keep the tracked deletion, capitalize only the visible text "s"
    // with tracked deletion of the original character
    sReplaced = "tsS ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // repeat it with visible redlining and word auto replacement of "tset"
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});
    dispatchCommand(mxComponent, u".uno:Undo"_ustr, {});

    emulateTyping(u"et ");
    // This was "Ttest" removing the tracked deletion silently.
    // Don't replace, if a redline starts or ends within the text.
    sReplaced = "tset ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // Otherwise replace it
    emulateTyping(u"tset ");
    sReplaced = "tset test ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // Including capitalization
    emulateTyping(u"end. word ");
    sReplaced = "tset test end. Word ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // tracked deletions after the correction point doesn't affect autocorrect
    dispatchCommand(mxComponent, u".uno:GoToStartOfDoc"_ustr, {});
    emulateTyping(u"a ");
    sReplaced = "A tset test end. Word ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testRedlineAutoCorrect2)
{
    createSwDoc("redline-autocorrect2.fodt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    dispatchCommand(mxComponent, u".uno:GoToEndOfDoc"_ustr, {});

    // show tracked deletion
    RedlineFlags const nMode(pWrtShell->GetRedlineFlags() | RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & (RedlineFlags::ShowDelete | RedlineFlags::ShowInsert));
    pWrtShell->SetRedlineFlags(nMode);
    CPPUNIT_ASSERT(nMode & RedlineFlags::ShowDelete);

    emulateTyping(u"... ");

    // This was "LoremLorem,…," (duplicating the deleted comma, but without deletion)
    // Don't replace, if a redline starts or ends within the text.
    OUString sReplaced = u"Lorem,... "_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // Continue it:
    emulateTyping(u"Lorem,... ");
    sReplaced = u"Lorem,... Lorem,… "_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testEmojiAutoCorrect)
{
    createSwDoc("redline-autocorrect3.fodt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Emoji replacement (:snowman: -> ☃)

    // without change tracking
    CPPUNIT_ASSERT(!(pWrtShell->GetRedlineFlags() & RedlineFlags::On));
    emulateTyping(u":snowman:");
    OUString sReplaced = u"☃Lorem,"_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // with change tracking (showing redlines)
    RedlineFlags const nMode(pWrtShell->GetRedlineFlags() | RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & (RedlineFlags::ShowDelete | RedlineFlags::ShowInsert));
    pWrtShell->SetRedlineFlags(nMode);
    CPPUNIT_ASSERT(nMode & RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & RedlineFlags::ShowDelete);

    emulateTyping(u":snowman:");
    sReplaced = u"☃☃Lorem,"_ustr;

    // tdf#140674 This was ":snowman:" instead of autocorrect
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf153423)
{
    createSwDoc();
    SvxSwAutoFormatFlags flags(*SwEditShell::GetAutoFormatFlags());
    comphelper::ScopeGuard const g([=]() { SwEditShell::SetAutoFormatFlags(&flags); });
    flags.bSetNumRule = true;
    SwEditShell::SetAutoFormatFlags(&flags);

    emulateTyping(u"1. Item 1");

    SwXTextDocument* pTextDoc = getSwTextDoc();
    pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_RETURN);
    pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYUP, 0, KEY_RETURN);
    Scheduler::ProcessEventsToIdle();

    // Without the fix in place, this test would have failed with
    // - Expected: 1.
    // - Actual  : 10.
    CPPUNIT_ASSERT_EQUAL(u"1."_ustr,
                         getProperty<OUString>(getParagraph(1), u"ListLabelString"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf133589)
{
    // Hungarian test document with right-to-left paragraph setting
    createSwDoc("tdf133589.fodt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);
    // translitere words to Old Hungarian
    emulateTyping(u"székely ");
    OUString sReplaced(u"𐳥𐳋𐳓𐳉𐳗 "_ustr);
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // disambiguate consonants: asszony -> asz|szony
    emulateTyping(u"asszony ");
    sReplaced += u"𐳀𐳥𐳥𐳛𐳚 ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // disambiguate consonants: kosszarv -> kos|szarv
    // (add explicit ZWSP temporarily for consonant disambiguation, because the requested
    // hu_HU hyphenation dictionary isn't installed on all testing platform)
    // pWrtShell->Insert(u"kosszarv");
    emulateTyping(u"kos\u200Bszarv ");
    sReplaced += u"𐳓𐳛𐳤𐳥𐳀𐳢𐳮 ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // transliterate numbers to Old Hungarian
    emulateTyping(u"2020 ");
    sReplaced += u"𐳺𐳺𐳿𐳼𐳼 ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // tdf#147546 transliterate punctuation marks

    // question mark
    emulateTyping(u"Kérdőjel?");
    sReplaced += u"𐲓𐳋𐳢𐳇𐳟𐳒𐳉𐳖";
    OUString sReplaced2(sReplaced + "?");
    CPPUNIT_ASSERT_EQUAL(sReplaced2, getParagraph(1)->getString());
    emulateTyping(u" ");
    sReplaced += u"⸮ ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // comma
    emulateTyping(u"Vessző,");
    sReplaced += u"𐲮𐳉𐳥𐳥𐳟";
    sReplaced2 = sReplaced + ",";
    CPPUNIT_ASSERT_EQUAL(sReplaced2, getParagraph(1)->getString());
    emulateTyping(u" ");
    sReplaced += u"⹁ ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // semicolon
    emulateTyping(u"pontosvessző;");
    sReplaced += u"𐳠𐳛𐳙𐳦𐳛𐳤𐳮𐳉𐳥𐳥𐳟";
    sReplaced2 = sReplaced + ";";
    CPPUNIT_ASSERT_EQUAL(sReplaced2, getParagraph(1)->getString());
    emulateTyping(u" ");
    sReplaced += u"⁏ ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
    // quotation marks
    emulateTyping(u"„idézőjel” ");
    sReplaced += u"⹂𐳐𐳇𐳋𐳯𐳟𐳒𐳉𐳖‟ ";
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // tdf#148672 transliterate word with closing bracket
    emulateTyping(u"word] ");
    sReplaced += u"𐳮𐳛𐳢𐳇] "; // This was "word]" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // tdf#148672 transliterate words with parenthesis (libnumbertext 1.0.11)
    emulateTyping(u"(word) ");
    sReplaced += u"(𐳮𐳛𐳢𐳇) "; // This was "(word)" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"(word ");
    sReplaced += u"(𐳮𐳛𐳢𐳇 "; // This was "(word" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"word) ");
    sReplaced += u"𐳮𐳛𐳢𐳇) "; // This was "word)" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"{word} ");
    sReplaced += u"{𐳮𐳛𐳢𐳇} "; // This was "(word)" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"{word ");
    sReplaced += u"{𐳮𐳛𐳢𐳇 "; // This was "(word" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"word} ");
    sReplaced += u"𐳮𐳛𐳢𐳇} "; // This was "word)" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"[word] ");
    sReplaced += u"[𐳮𐳛𐳢𐳇] "; // This was "(word)" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    emulateTyping(u"[word ");
    sReplaced += u"[𐳮𐳛𐳢𐳇 "; // This was "(word" (no transliteration)
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testAutoCorr)
{
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    //Normal AutoCorrect
    emulateTyping(u"tset ");
    CPPUNIT_ASSERT_EQUAL(u"Test "_ustr, getParagraph(1)->getString());

    //AutoCorrect with change style to bolt
    emulateTyping(u"Bolt ");
    const uno::Reference<text::XTextRange> xRun = getRun(getParagraph(1), 2);
    CPPUNIT_ASSERT_EQUAL(u"Bolt"_ustr, xRun->getString());
    CPPUNIT_ASSERT_EQUAL(u"Arial"_ustr, getProperty<OUString>(xRun, u"CharFontName"_ustr));

    //AutoCorrect inserts Table with 2 rows and 3 columns
    emulateTyping(u"4xx ");
    const uno::Reference<text::XTextTable> xTable(getParagraphOrTable(2), uno::UNO_QUERY);
    CPPUNIT_ASSERT_EQUAL(sal_Int32(2), xTable->getRows()->getCount());
    CPPUNIT_ASSERT_EQUAL(sal_Int32(3), xTable->getColumns()->getCount());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf130274)
{
    createSwDoc();
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* const pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    CPPUNIT_ASSERT(!pWrtShell->GetLayout()->IsHideRedlines());
    CPPUNIT_ASSERT(
        !IDocumentRedlineAccess::IsRedlineOn(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));

    // "tset" may be replaced by the AutoCorrect in the test profile
    emulateTyping(u"tset");
    // select from left to right
    pWrtShell->Left(SwCursorSkipMode::Chars, /*bSelect=*/false, 4, /*bBasicCall=*/false);
    pWrtShell->Right(SwCursorSkipMode::Chars, /*bSelect=*/true, 4, /*bBasicCall=*/false);

    pWrtShell->SetRedlineFlags(pWrtShell->GetRedlineFlags() | RedlineFlags::On);
    // this would crash in AutoCorrect
    emulateTyping(u".");

    CPPUNIT_ASSERT(!pDoc->getIDocumentRedlineAccess().GetRedlineTable().empty());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf83260)
{
    createSwDoc("tdf83260-1.odt");
    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // enabled but not shown
    CPPUNIT_ASSERT(pWrtShell->GetLayout()->IsHideRedlines());
#if 0
    CPPUNIT_ASSERT(IDocumentRedlineAccess::IsHideChanges(
            pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));
#endif
    CPPUNIT_ASSERT(
        IDocumentRedlineAccess::IsRedlineOn(pDoc->getIDocumentRedlineAccess().GetRedlineFlags()));
    CPPUNIT_ASSERT(!pDoc->getIDocumentRedlineAccess().GetRedlineTable().empty());

    // the document contains redlines that are combined with CompressRedlines()
    // if that happens during AutoCorrect then indexes in Undo are off -> crash
    emulateTyping(u"tset ");
    sw::UndoManager& rUndoManager = pDoc->GetUndoManager();
    auto const nActions(rUndoManager.GetUndoActionCount());
    for (auto i = nActions; 0 < i; --i)
    {
        rUndoManager.Undo();
    }
    // check that every text node has a layout frame
    for (SwNodeOffset i(0); i < pDoc->GetNodes().Count(); ++i)
    {
        if (SwTextNode const* const pNode = pDoc->GetNodes()[i]->GetTextNode())
        {
            CPPUNIT_ASSERT(pNode->getLayoutFrame(nullptr, nullptr, nullptr));
        }
    }
    for (auto i = nActions; 0 < i; --i)
    {
        rUndoManager.Redo();
    }
    for (SwNodeOffset i(0); i < pDoc->GetNodes().Count(); ++i)
    {
        if (SwTextNode const* const pNode = pDoc->GetNodes()[i]->GetTextNode())
        {
            CPPUNIT_ASSERT(pNode->getLayoutFrame(nullptr, nullptr, nullptr));
        }
    }
    for (auto i = nActions; 0 < i; --i)
    {
        rUndoManager.Undo();
    }
    for (SwNodeOffset i(0); i < pDoc->GetNodes().Count(); ++i)
    {
        if (SwTextNode const* const pNode = pDoc->GetNodes()[i]->GetTextNode())
        {
            CPPUNIT_ASSERT(pNode->getLayoutFrame(nullptr, nullptr, nullptr));
        }
    }
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf139922)
{
    createSwDoc();

    SwXTextDocument* pTextDoc = getSwTextDoc();
    pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_RETURN);
    Scheduler::ProcessEventsToIdle();

    emulateTyping(u"this is a SEntence. this is a SEntence.");

    // Without the fix in place, this test would have failed with
    // - Expected: This is a Sentence. This is a Sentence.
    // - Actual  : this is a Sentence. This is a Sentence.
    CPPUNIT_ASSERT_EQUAL(u"This is a Sentence. This is a Sentence."_ustr,
                         getParagraph(2)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf143176)
{
    // Hungarian test document with right-to-left paragraph setting
    createSwDoc("tdf143176.fodt");

    // transliterate the document to Old Hungarian (note: it only works
    // with right-to-left text direction and Default Paragraph Style)
    dispatchCommand(mxComponent, u".uno:AutoFormatApply"_ustr, {});

    // This was the original "Lorem ipsum..."
    CPPUNIT_ASSERT_EQUAL(u"𐲖𐳛𐳢𐳉𐳘 𐳐𐳠𐳤𐳪𐳘 𐳇𐳛𐳖𐳛𐳢 "
                         u"𐳤𐳐𐳦 𐳀𐳘𐳉𐳦⹁"_ustr,
                         getParagraph(1)->getString());
    CPPUNIT_ASSERT_EQUAL(u"𐳄𐳛𐳙𐳤𐳉𐳄𐳦𐳉𐳦𐳪𐳢 "
                         u"𐳀𐳇𐳐𐳠𐳐𐳤𐳄𐳐𐳙𐳍 𐳉𐳖𐳐𐳦."_ustr,
                         getParagraph(2)->getString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testInsertLongDateFormat)
{
    // only for Hungarian, yet
    createSwDoc("tdf133524.fodt");
    dispatchCommand(mxComponent, u".uno:InsertDateField"_ustr, {});
    // Make sure that the document starts with a field now, and its expanded string value contains space
    const uno::Reference<text::XTextRange> xField = getRun(getParagraph(1), 1);
    CPPUNIT_ASSERT_EQUAL(u"TextField"_ustr, getProperty<OUString>(xField, u"TextPortionType"_ustr));
    // the date format was "YYYY-MM-DD", but now "YYYY. MMM DD."
    CPPUNIT_ASSERT(xField->getString().indexOf(" ") > -1);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf129270)
{
    createSwDoc("tdf129270.odt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // Go to document end
    pWrtShell->SttEndDoc(/*bStt=*/false);

    // Press enter
    SwXTextDocument* pTextDoc = getSwTextDoc();
    pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_RETURN);
    Scheduler::ProcessEventsToIdle();

    // Numbering for previous outline should remain the same "2"
    CPPUNIT_ASSERT_EQUAL(u"2"_ustr,
                         getProperty<OUString>(getParagraph(4), u"ListLabelString"_ustr));

    // Numbering for newly created outline should be "2.1"
    CPPUNIT_ASSERT_EQUAL(u"2.1"_ustr,
                         getProperty<OUString>(getParagraph(5), u"ListLabelString"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testInsertPdf)
{
    auto pPdfium = vcl::pdf::PDFiumLibrary::get();
    if (!pPdfium)
    {
        return;
    }

    createSwDoc();

    // insert the PDF into the document
    uno::Sequence<beans::PropertyValue> aArgs(comphelper::InitPropertySequence(
        { { "FileName", uno::Any(createFileURL(u"hello-world.pdf")) } }));
    dispatchCommand(mxComponent, u".uno:InsertGraphic"_ustr, aArgs);

    // Save and load cycle
    saveAndReload(u"writer8"_ustr);

    uno::Reference<drawing::XShape> xShape = getShape(1);
    // Assert that we have a replacement graphics
    auto xReplacementGraphic
        = getProperty<uno::Reference<graphic::XGraphic>>(xShape, u"ReplacementGraphic"_ustr);
    CPPUNIT_ASSERT(xReplacementGraphic.is());

    auto xGraphic = getProperty<uno::Reference<graphic::XGraphic>>(xShape, u"Graphic"_ustr);
    CPPUNIT_ASSERT(xGraphic.is());
    // Assert that the graphic is a PDF
    CPPUNIT_ASSERT_EQUAL(u"application/pdf"_ustr,
                         getProperty<OUString>(xGraphic, u"MimeType"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf143760WrapContourToOff)
{
    // Actually, this is an ooxmlexport test. It is here because here is a ready environment
    // to change a shape by dispatchCommand.
    createSwDoc("tdf143760_ContourToWrapOff.docx");
    SwDoc* pDoc = getSwDoc();
    CPPUNIT_ASSERT_EQUAL(true, getProperty<bool>(getShape(1), u"SurroundContour"_ustr));

    // Mark the object
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    SdrPage* pPage = pDoc->getIDocumentDrawModelAccess().GetDrawModel()->GetPage(0);
    SdrObject* pObject = pPage->GetObj(0);
    CPPUNIT_ASSERT(pObject);
    SdrView* pView = pWrtShell->GetDrawView();
    pView->MarkObj(pObject, pView->GetSdrPageView());

    // Set "wrap off"
    dispatchCommand(mxComponent, u".uno:WrapOff"_ustr, {});
    CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(getShape(1), u"SurroundContour"_ustr));

    // Without fix this had failed, because the shape was written to file with contour.
    saveAndReload(u"Office Open XML Text"_ustr);
    CPPUNIT_ASSERT_EQUAL(false, getProperty<bool>(getShape(1), u"SurroundContour"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testHatchFill)
{
    createSwDoc();

    // Add a rectangle shape to the document.
    uno::Reference<css::lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XShape> xShape(
        xFactory->createInstance(u"com.sun.star.drawing.RectangleShape"_ustr), uno::UNO_QUERY);
    xShape->setSize(awt::Size(10000, 10000));
    xShape->setPosition(awt::Point(1000, 1000));
    uno::Reference<beans::XPropertySet> xShapeProps(xShape, uno::UNO_QUERY);
    xShapeProps->setPropertyValue(u"FillStyle"_ustr, uno::Any(drawing::FillStyle_HATCH));
    xShapeProps->setPropertyValue(u"FillHatchName"_ustr, uno::Any(u"Black 0 Degrees"_ustr));
    xShapeProps->setPropertyValue(u"FillBackground"_ustr, uno::Any(false));
    xShapeProps->setPropertyValue(u"FillTransparence"_ustr, uno::Any(sal_Int32(30)));
    uno::Reference<drawing::XDrawPageSupplier> xDrawPageSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage = xDrawPageSupplier->getDrawPage();
    xDrawPage->add(xShape);

    // Save it as DOCX and load it again.
    saveAndReload(u"Office Open XML Text"_ustr);
    CPPUNIT_ASSERT_EQUAL(1, getShapes());

    // tdf#127989 Without fix this had failed, because the background of the hatch was not set as 'no background'.
    CPPUNIT_ASSERT(!getProperty<bool>(getShape(1), u"FillBackground"_ustr));

    // tdf#146822 Without fix this had failed, because the transparency value of the hatch was not exported.
    CPPUNIT_ASSERT_EQUAL(sal_Int32(30),
                         getProperty<sal_Int32>(getShape(1), u"FillTransparence"_ustr));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testNestedGroupTextBoxCopyCrash)
{
    createSwDoc("tdf149550.docx");

    CPPUNIT_ASSERT_EQUAL(1, getShapes());

    dispatchCommand(mxComponent, u".uno:SelectAll"_ustr, {});
    dispatchCommand(mxComponent, u".uno:Copy"_ustr, {});
    // This crashed here before the fix.
    SwXTextDocument* pTextDoc = getSwTextDoc();
    pTextDoc->postKeyEvent(LOK_KEYEVENT_KEYINPUT, 0, KEY_ESCAPE);
    Scheduler::ProcessEventsToIdle();
    dispatchCommand(mxComponent, u".uno:Paste"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(2, getShapes());

    auto pLayout = parseLayoutDump();
    // There must be 2 textboxes!
    assertXPath(pLayout, "/root/page/body/txt/anchored/fly[2]");
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testCrashOnExit)
{
    // Load the bugdoc with a table and a textbox shape inside.
    createSwDoc("tdf142715.odt");

    // Get the textbox selected
    CPPUNIT_ASSERT_EQUAL(1, getShapes());

    selectShape(1);
    auto xShape = getShape(1);
    uno::Reference<beans::XPropertySet> xProperties(xShape, uno::UNO_QUERY);

    // Check if the textbox is selected
    CPPUNIT_ASSERT_EQUAL(true, xProperties->getPropertyValue(u"TextBox"_ustr).get<bool>());

    // Remove the textbox
    dispatchCommand(mxComponent, u".uno:RemoveTextBox"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(false, xProperties->getPropertyValue(u"TextBox"_ustr).get<bool>());

    // Readd the textbox (to run the textboxhelper::create() method)
    dispatchCommand(mxComponent, u".uno:AddTextBox"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(true, xProperties->getPropertyValue(u"TextBox"_ustr).get<bool>());

    // save and reload
    // Before the fix this crashed here and could not reopen.
    saveAndReload(u"writer8"_ustr);
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testCaptionShape)
{
    createSwDoc();

    // Add a caption shape to the document.
    uno::Reference<css::lang::XMultiServiceFactory> xFactory(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XShape> xShape(
        xFactory->createInstance(u"com.sun.star.drawing.CaptionShape"_ustr), uno::UNO_QUERY);
    xShape->setSize(awt::Size(10000, 10000));
    xShape->setPosition(awt::Point(1000, 1000));
    uno::Reference<drawing::XDrawPageSupplier> xDrawPageSupplier(mxComponent, uno::UNO_QUERY);
    uno::Reference<drawing::XDrawPage> xDrawPage = xDrawPageSupplier->getDrawPage();
    xDrawPage->add(xShape);

    // Save it as DOCX and load it again.
    saveAndReload(u"Office Open XML Text"_ustr);

    // Without fix in place, the shape was lost on export.
    CPPUNIT_ASSERT_EQUAL(1, getShapes());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf151828_Comment2)
{
    createSwDoc();

    // Add a basic shape to the document.
    uno::Sequence<beans::PropertyValue> aArgs(
        comphelper::InitPropertySequence({ { "KeyModifier", uno::Any(KEY_MOD1) } }));
    dispatchCommand(mxComponent, u".uno:BasicShapes"_ustr, aArgs);

    auto xBasicShape = getShape(1);
    auto pObject = SdrObject::getSdrObjectFromXShape(xBasicShape);

    CPPUNIT_ASSERT_EQUAL(1, getShapes());

    // rename the shape name
    pObject->SetName(u"Shape"_ustr);

    // cut and paste it
    dispatchCommand(mxComponent, u".uno:Cut"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(0, getShapes());

    dispatchCommand(mxComponent, u".uno:Paste"_ustr, {});

    CPPUNIT_ASSERT_EQUAL(1, getShapes());

    // it is required to get the shape object again after paste
    xBasicShape = getShape(1);
    pObject = SdrObject::getSdrObjectFromXShape(xBasicShape);

    // Without fix in place, the shape name was 'Shape 1' after paste.
    CPPUNIT_ASSERT_EQUAL(u"Shape"_ustr, pObject->GetName());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf151828)
{
    createSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // insert a table
    SwInsertTableOptions TableOpt(SwInsertTableFlags::DefaultBorder, 0);
    pWrtShell->InsertTable(TableOpt, 1, 1);

    // move cursor into the table
    CPPUNIT_ASSERT(pWrtShell->MoveTable(GotoPrevTable, fnTableStart));

    SwFrameFormat* pFormat = pWrtShell->GetTableFormat();
    CPPUNIT_ASSERT(pFormat);

    // set name of table to 'MyTableName'
    pWrtShell->SetTableName(*pFormat, UIName(u"MyTableName"_ustr));

    // cut and paste the table
    dispatchCommand(mxComponent, u".uno:SelectTable"_ustr, {});
    dispatchCommand(mxComponent, u".uno:Cut"_ustr, {});
    dispatchCommand(mxComponent, u".uno:Paste"_ustr, {});

    // move cursor into the pasted table
    CPPUNIT_ASSERT(pWrtShell->MoveTable(GotoPrevTable, fnTableStart));

    pFormat = pWrtShell->GetTableFormat();
    CPPUNIT_ASSERT(pFormat);

    // Before the fix the pasted table name was 'Table1'.
    CPPUNIT_ASSERT_EQUAL(u"MyTableName"_ustr, pFormat->GetName().toString());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf146178)
{
    createSwDoc();

    SwDoc* pDoc = getSwDoc();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwPaM* pCursor = pShell->GetCursor();

    // insert two fields
    dispatchCommand(mxComponent, u".uno:InsertTimeField"_ustr, {});
    dispatchCommand(mxComponent, u".uno:InsertDateField"_ustr, {});

    // navigate by field
    SwView::SetMoveType(NID_FIELD);

    // set cursor to the start of the document
    pWrtShell->SttEndDoc(false);
    // navigate to the previous field
    dispatchCommand(mxComponent, u".uno:ScrollToPrevious"_ustr, {});
    // Before the fix the position would be 0, navigation did not wrap to end of document
    CPPUNIT_ASSERT_EQUAL(sal_Int32(1), pCursor->GetPoint()->GetContentIndex());

    // set cursor to the end of the document
    pWrtShell->SttEndDoc(false);
    // navigate to the next field
    dispatchCommand(mxComponent, u".uno:ScrollToNext"_ustr, {});
    // Before the fix the position would be 1, navigation did not wrap to start of document
    CPPUNIT_ASSERT_EQUAL(sal_Int32(0), pCursor->GetPoint()->GetContentIndex());
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf106663HeaderTextFrameGoToNextPlacemarker)
{
    createSwDoc("testTdf106663.odt");

    SwDoc* pDoc = getSwDoc();
    SwCursorShell* pShell(pDoc->GetEditShell());
    CPPUNIT_ASSERT(pShell);
    SwPaM* pCursor = pShell->GetCursor();
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();

    // Move the cursor into the fly frame of the document's header
    pWrtShell->GotoFly(UIName(u"FrameInHeader"_ustr), FLYCNTTYPE_FRM, false);

    // Check that GoToNextPlacemarker highlights the first field instead of the second one
    dispatchCommand(mxComponent, u".uno:GoToNextPlacemarker"_ustr, {});
    // Without the fix in place, this test would have failed with
    // - Expected: Heading
    // - Actual  : Some other marker
    // i.e. the GoToNextPlacemarker command skipped the first field
    CPPUNIT_ASSERT(pCursor->GetPoint()->GetNode().GetTextNode()->GetText().startsWith("Heading"));
}

CPPUNIT_TEST_FIXTURE(SwUiWriterTest6, testTdf158454)
{
    createSwDoc("tdf158454.odt");
    SwWrtShell* pWrtShell = getSwDocShell()->GetWrtShell();
    CPPUNIT_ASSERT(pWrtShell);

    // * without change tracking
    CPPUNIT_ASSERT(!(pWrtShell->GetRedlineFlags() & RedlineFlags::On));

    // Thai single autocorrect (อนุญาติ -> อนุญาต)
    emulateTyping(u"อนุญาติ ");
    OUString sReplaced = u"อนุญาต (จบ)"_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // Thai multiple autocorrects (กงศุลสังเกตุกระทันหัน -> กงสุลสังเกตกะทันหัน)
    emulateTyping(u"กงศุลสังเกตุกระทันหัน ");
    sReplaced = u"อนุญาต กงสุลสังเกตกะทันหัน (จบ)"_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // * with change tracking (showing redlines)
    RedlineFlags const nMode(pWrtShell->GetRedlineFlags() | RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & (RedlineFlags::ShowDelete | RedlineFlags::ShowInsert));
    pWrtShell->SetRedlineFlags(nMode);
    CPPUNIT_ASSERT(nMode & RedlineFlags::On);
    CPPUNIT_ASSERT(nMode & RedlineFlags::ShowDelete);

    // Thai single autocorrect (อนุญาติ -> อนุญาต)
    emulateTyping(u"อนุญาติ ");
    sReplaced = u"อนุญาต กงสุลสังเกตกะทันหัน อนุญาต (จบ)"_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());

    // Thai multiple autocorrects (กงศุลสังเกตุกระทันหัน -> กงสุลสังเกตกะทันหัน)
    emulateTyping(u"กงศุลสังเกตุกระทันหัน ");
    sReplaced = u"อนุญาต กงสุลสังเกตกะทันหัน อนุญาต กงสุลสังเกตกะทันหัน (จบ)"_ustr;
    CPPUNIT_ASSERT_EQUAL(sReplaced, getParagraph(1)->getString());
}

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
