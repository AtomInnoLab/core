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

#include <rtl/ref.hxx>
#include <svl/listener.hxx>
#include <svl/style.hxx>
#include "unocoll.hxx"
#include "tblafmt.hxx"
#include <com/sun/star/style/XStyle.hpp>
#include <com/sun/star/style/XStyleLoader.hpp>
#include <com/sun/star/container/XNameContainer.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/beans/XPropertyState.hpp>
#include <cppuhelper/implbase.hxx>

#include <com/sun/star/style/XAutoStyleFamily.hpp>
#include <com/sun/star/style/XAutoStyles.hpp>
#include <com/sun/star/style/XAutoStyle.hpp>

#include "coreframestyle.hxx"
#include "istyleaccess.hxx"
#include "unobasestyle.hxx"
#include <memory>
#include <map>

namespace com::sun::star::document { class XEventsSupplier; }

class SwDocShell;
class SwAutoStylesEnumImpl;
class SfxItemSet;
class SwXStyle;
class SwXTextCellStyle;
class SwXPageStyle;
class SwXFrameStyle;
class StyleFamilyEntry;
class SwXStyleFamily;
class SwXAutoStyleFamily;

class SAL_DLLPUBLIC_RTTI SwXStyleFamilies final : public cppu::WeakImplHelper
<
    css::container::XIndexAccess,
    css::container::XNameAccess,
    css::lang::XServiceInfo,
    css::style::XStyleLoader
>,
    public SwUnoCollection
{
    SwDocShell*         m_pDocShell;

    std::map<SfxStyleFamily, rtl::Reference<SwXStyleFamily>> m_vFamilies;

    virtual ~SwXStyleFamilies() override;
public:
    SwXStyleFamilies(SwDocShell& rDocShell);

    //XNameAccess
    SW_DLLPUBLIC virtual css::uno::Any SAL_CALL getByName(const OUString& Name) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getElementNames() override;
    virtual sal_Bool SAL_CALL hasByName(const OUString& Name) override;

    //XIndexAccess
    virtual sal_Int32 SAL_CALL getCount() override;
    virtual css::uno::Any SAL_CALL getByIndex(sal_Int32 nIndex) override;

    //XElementAccess
    virtual css::uno::Type SAL_CALL getElementType(  ) override;
    virtual sal_Bool SAL_CALL hasElements(  ) override;

    //XStyleLoader
    virtual void SAL_CALL loadStylesFromURL(const OUString& rURL, const css::uno::Sequence< css::beans::PropertyValue >& aOptions) override;
    virtual css::uno::Sequence< css::beans::PropertyValue > SAL_CALL getStyleLoaderOptions() override;

    //XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(const OUString& ServiceName) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

    SW_DLLPUBLIC rtl::Reference<SwXStyleFamily> GetPageStyles();
    SW_DLLPUBLIC rtl::Reference<SwXStyleFamily> GetCharacterStyles();
    SW_DLLPUBLIC rtl::Reference<SwXStyleFamily> GetParagraphStyles();
    SW_DLLPUBLIC rtl::Reference<SwXStyleFamily> GetNumberingStyles();
    SW_DLLPUBLIC rtl::Reference<SwXStyleFamily> GetStylesByName(const OUString& rName);
    rtl::Reference<SwXStyleFamily> GetStylesByIndex(sal_Int32 nIndex);

    static css::uno::Reference<css::style::XStyle> CreateStyle(SfxStyleFamily eFamily, SwDoc& rDoc);
    static rtl::Reference<SwXStyle> CreateStyleCharOrParaOrPseudo(SfxStyleFamily eFamily, SwDoc& rDoc);
    static rtl::Reference<SwXPageStyle> CreateStylePage(SwDoc& rDoc);
    static rtl::Reference<SwXFrameStyle> CreateStyleFrame(SwDoc& rDoc);
    static rtl::Reference<SwXTextTableStyle> CreateStyleTable(SwDoc& rDoc);
    static rtl::Reference<SwXTextCellStyle> CreateStyleCell(SwDoc& rDoc);
    // FIXME: This is very ugly as is the whole conditional paragraph style
    // hackety. Should be folded into CreateStyle hopefully one day
    static css::uno::Reference<css::style::XStyle> CreateStyleCondParagraph(SwDoc& rDoc);
};

// access to all automatic style families
class SwXAutoStyles final :
    public cppu::WeakImplHelper< css::style::XAutoStyles >,
    public SwUnoCollection
{
    SwDocShell *m_pDocShell;
    rtl::Reference< SwXAutoStyleFamily > m_xAutoCharStyles;
    rtl::Reference< SwXAutoStyleFamily > m_xAutoRubyStyles;
    rtl::Reference< SwXAutoStyleFamily > m_xAutoParaStyles;
    virtual ~SwXAutoStyles() override;

public:
    SwXAutoStyles(SwDocShell& rDocShell);

    //XIndexAccess
    virtual sal_Int32 SAL_CALL getCount() override;
    virtual css::uno::Any SAL_CALL getByIndex(sal_Int32 nIndex) override;

    //XElementAccess
    virtual css::uno::Type SAL_CALL getElementType(  ) override;
    virtual sal_Bool SAL_CALL hasElements(  ) override;

    //XNameAccess
    virtual css::uno::Any SAL_CALL getByName(const OUString& Name) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getElementNames() override;
    virtual sal_Bool SAL_CALL hasByName(const OUString& Name) override;

};

// access to a family of automatic styles (character or paragraph or ...)
class SwXAutoStyleFamily final : public cppu::WeakImplHelper< css::style::XAutoStyleFamily >, public SvtListener
{
    SwDocShell *m_pDocShell;
    IStyleAccess::SwAutoStyleFamily m_eFamily;


public:
    SwXAutoStyleFamily(SwDocShell* pDocShell, IStyleAccess::SwAutoStyleFamily eFamily);
    virtual ~SwXAutoStyleFamily() override;

    //XAutoStyleFamily
    virtual css::uno::Reference< css::style::XAutoStyle > SAL_CALL insertStyle( const css::uno::Sequence< css::beans::PropertyValue >& Values ) override;

    //XEnumerationAccess
    virtual css::uno::Reference< css::container::XEnumeration > SAL_CALL createEnumeration(  ) override;

    //XElementAccess
    virtual css::uno::Type SAL_CALL getElementType(  ) override;
    virtual sal_Bool SAL_CALL hasElements(  ) override;

    virtual void Notify( const SfxHint&) override;
};

class SwXAutoStylesEnumerator final : public cppu::WeakImplHelper< css::container::XEnumeration >, public SvtListener
{
    std::unique_ptr<SwAutoStylesEnumImpl> m_pImpl;
public:
    SwXAutoStylesEnumerator( SwDoc& rDoc, IStyleAccess::SwAutoStyleFamily eFam );
    virtual ~SwXAutoStylesEnumerator() override;

    //XEnumeration
    virtual sal_Bool SAL_CALL hasMoreElements(  ) override;
    virtual css::uno::Any SAL_CALL nextElement(  ) override;

    virtual void Notify( const SfxHint&) override;
};

// an automatic style
class SwXAutoStyle final : public cppu::WeakImplHelper
<
    css::beans::XPropertySet,
    css::beans::XPropertyState,
    css::style::XAutoStyle
>,
    public SvtListener
{
private:
    std::shared_ptr<SfxItemSet>         mpSet;
    IStyleAccess::SwAutoStyleFamily     meFamily;
    SwDoc&                              mrDoc;

    /// @throws css::beans::UnknownPropertyException
    /// @throws css::lang::WrappedTargetException
    /// @throws css::uno::RuntimeException
    css::uno::Sequence< css::uno::Any > GetPropertyValues_Impl( const css::uno::Sequence< OUString >& aPropertyNames );

public:

    SwXAutoStyle( SwDoc* pDoc, std::shared_ptr<SfxItemSet> pInitSet, IStyleAccess::SwAutoStyleFamily eFam );
    virtual ~SwXAutoStyle() override;

    //XPropertySet
    virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL getPropertySetInfo(  ) override;
    virtual void SAL_CALL setPropertyValue( const OUString& aPropertyName, const css::uno::Any& aValue ) override;
    virtual css::uno::Any SAL_CALL getPropertyValue( const OUString& PropertyName ) override;
    virtual void SAL_CALL addPropertyChangeListener( const OUString& aPropertyName, const css::uno::Reference< css::beans::XPropertyChangeListener >& xListener ) override;
    virtual void SAL_CALL removePropertyChangeListener( const OUString& aPropertyName, const css::uno::Reference< css::beans::XPropertyChangeListener >& aListener ) override;
    virtual void SAL_CALL addVetoableChangeListener( const OUString& PropertyName, const css::uno::Reference< css::beans::XVetoableChangeListener >& aListener ) override;
    virtual void SAL_CALL removeVetoableChangeListener( const OUString& PropertyName, const css::uno::Reference< css::beans::XVetoableChangeListener >& aListener ) override;

    //XMultiPropertySet
    virtual void SAL_CALL setPropertyValues( const css::uno::Sequence< OUString >& aPropertyNames, const css::uno::Sequence< css::uno::Any >& aValues ) override;
    virtual css::uno::Sequence< css::uno::Any > SAL_CALL getPropertyValues( const css::uno::Sequence< OUString >& aPropertyNames ) override;
    virtual void SAL_CALL addPropertiesChangeListener( const css::uno::Sequence< OUString >& aPropertyNames, const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener ) override;
    virtual void SAL_CALL removePropertiesChangeListener( const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener ) override;
    virtual void SAL_CALL firePropertiesChangeEvent( const css::uno::Sequence< OUString >& aPropertyNames, const css::uno::Reference< css::beans::XPropertiesChangeListener >& xListener ) override;

    //XPropertyState
    virtual css::beans::PropertyState SAL_CALL getPropertyState( const OUString& PropertyName ) override;
    virtual css::uno::Sequence< css::beans::PropertyState > SAL_CALL getPropertyStates( const css::uno::Sequence< OUString >& aPropertyName ) override;
    virtual void SAL_CALL setPropertyToDefault( const OUString& PropertyName ) override;
    virtual css::uno::Any SAL_CALL getPropertyDefault( const OUString& aPropertyName ) override;

    //XMultiPropertyStates
    virtual void SAL_CALL setAllPropertiesToDefault(  ) override;
    virtual void SAL_CALL setPropertiesToDefault( const css::uno::Sequence< OUString >& aPropertyNames ) override;
    virtual css::uno::Sequence< css::uno::Any > SAL_CALL getPropertyDefaults( const css::uno::Sequence< OUString >& aPropertyNames ) override;

    // Special
    virtual css::uno::Sequence< css::beans::PropertyValue > SAL_CALL getProperties() override;

    virtual void Notify( const SfxHint& ) override;

};

typedef std::map<OUString, sal_Int32> CellStyleNameMap;

/// A text table style is a UNO API wrapper for a SwTableAutoFormat
class SwXTextTableStyle final : public cppu::ImplInheritanceHelper
<
    SwXBaseStyle,
    css::container::XNameContainer,
    css::lang::XServiceInfo
>
{
    SwDocShell* m_pDocShell;
    SwTableAutoFormat* m_pTableAutoFormat;
    /// Stores SwTableAutoFormat when this is not a physical style.
    std::unique_ptr<SwTableAutoFormat> m_pTableAutoFormat_Impl;
    /// If true, then it points to a core object, if false, then this is a created, but not-yet-inserted format.
    bool m_bPhysical;

    enum {
        FIRST_ROW_STYLE = 0,
        LAST_ROW_STYLE,
        FIRST_COLUMN_STYLE,
        LAST_COLUMN_STYLE,
        EVEN_ROWS_STYLE,
        ODD_ROWS_STYLE,
        EVEN_COLUMNS_STYLE,
        ODD_COLUMNS_STYLE,
        BODY_STYLE,
        BACKGROUND_STYLE,
        // loext namespace
        FIRST_ROW_START_COLUMN_STYLE,
        FIRST_ROW_END_COLUMN_STYLE,
        LAST_ROW_START_COLUMN_STYLE,
        LAST_ROW_END_COLUMN_STYLE,
        FIRST_ROW_EVEN_COLUMN_STYLE,
        LAST_ROW_EVEN_COLUMN_STYLE,
        STYLE_COUNT
    };

    /// Fills m_aCellStyles with SwXTextCellStyles pointing to children of this style.
    void UpdateCellStylesMapping();
    static const CellStyleNameMap& GetCellStyleNameMap();
    rtl::Reference<SwXTextCellStyle> m_aCellStyles[STYLE_COUNT];
public:
    SwXTextTableStyle(SwDocShell* pDocShell, SwTableAutoFormat* pTableAutoFormat);
    /// Create non physical style
    SwXTextTableStyle(SwDocShell* pDocShell, const TableStyleName& rTableAutoFormatName);

    /// This function looks for a SwTableAutoFormat with given name. Returns nullptr if could not be found.
    static SwTableAutoFormat* GetTableAutoFormat(SwDocShell* pDocShell, const TableStyleName& sName);
    /// Returns box format assigned to this style
    SwTableAutoFormat* GetTableFormat();
    void SetPhysical();

    //XStyle
    virtual sal_Bool SAL_CALL isUserDefined() override;
    virtual sal_Bool SAL_CALL isInUse() override;
    virtual OUString SAL_CALL getParentStyle() override;
    virtual void SAL_CALL setParentStyle(const OUString& aParentStyle ) override;

    //XNamed
    virtual OUString SAL_CALL getName() override;
    virtual void SAL_CALL setName(const OUString& rName) override;

    //XPropertySet
    virtual css::uno::Reference<css::beans::XPropertySetInfo> SAL_CALL getPropertySetInfo() override;
    virtual void SAL_CALL setPropertyValue(const OUString& aPropertyName, const css::uno::Any& aValue) override;
    virtual css::uno::Any SAL_CALL getPropertyValue(const OUString& PropertyName) override;
    virtual void SAL_CALL addPropertyChangeListener(const OUString& aPropertyName, const css::uno::Reference<css::beans::XPropertyChangeListener>& xListener) override;
    virtual void SAL_CALL removePropertyChangeListener(const OUString& aPropertyName, const css::uno::Reference<css::beans::XPropertyChangeListener>& aListener) override;
    virtual void SAL_CALL addVetoableChangeListener(const OUString& PropertyName, const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;
    virtual void SAL_CALL removeVetoableChangeListener(const OUString& PropertyName, const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;

    //XNameAccess
    virtual css::uno::Any SAL_CALL getByName(const OUString& rName) override;
    virtual css::uno::Sequence<OUString> SAL_CALL getElementNames() override;
    virtual sal_Bool SAL_CALL hasByName(const OUString& rName) override;

    //XNameContainer
    virtual void SAL_CALL insertByName(const OUString& rName, const css::uno::Any& aElement) override;
    virtual void SAL_CALL replaceByName(const OUString& rName, const css::uno::Any& aElement) override;
    virtual void SAL_CALL removeByName(const OUString& rName) override;

    //XElementAccess
    virtual css::uno::Type SAL_CALL getElementType() override;
    virtual sal_Bool SAL_CALL hasElements() override;

    //XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static rtl::Reference<SwXTextTableStyle> CreateXTextTableStyle(SwDocShell* pDocShell, const TableStyleName& rTableAutoFormatName);
};

/// A text cell style is a UNO API wrapper for a SwBoxAutoFormat core class
class SwXTextCellStyle final : public cppu::ImplInheritanceHelper
<
    SwXBaseStyle,
    css::beans::XPropertyState,
    css::lang::XServiceInfo
>
{
    SwDocShell* m_pDocShell;
    SwBoxAutoFormat* m_pBoxAutoFormat;
    /// Stores SwBoxAutoFormat when this is not a physical style.
    std::shared_ptr<SwBoxAutoFormat> m_pBoxAutoFormat_Impl;
    /// UIName of the table style that contains this cell style
    TableStyleName m_sTableStyleUIName;
    /// There are no built-in cell style names - presumably these don't need to be converted.
    UIName m_sName;
    /// If true, then it points to a core object, if false, then this is a created, but not-yet-inserted format.
    bool m_bPhysical;

 public:
    SwXTextCellStyle(SwDocShell* pDocShell, SwBoxAutoFormat* pBoxAutoFormat, TableStyleName sParentStyle);
    /// Create non physical style
    SwXTextCellStyle(SwDocShell* pDocShell, UIName sName);

    /**
    * This function looks for a SwBoxAutoFormat with given name. Parses the name and returns parent name.
    * @param pDocShell pointer to a SwDocShell.
    * @param sName Name of a SwBoxAutoFormat to look for.
    * @param pParentName Optional output. Pointer to an OUString where parsed parent name will be returned.
    * @return Pointer to a SwBoxAutoFormat, nullptr if not found.
    */
    static SwBoxAutoFormat* GetBoxAutoFormat(SwDocShell* pDocShell, const UIName& sName, TableStyleName* pParentName);
    /// returns box format assigned to this style
    SwBoxAutoFormat* GetBoxFormat();
    /// Sets the address of SwBoxAutoFormat this style is bound to. Usable only when style is physical.
    void SetBoxFormat(SwBoxAutoFormat* pBoxFormat);
    void SetPhysical();
    bool IsPhysical() const;

    //XStyle
    virtual sal_Bool SAL_CALL isUserDefined() override;
    virtual sal_Bool SAL_CALL isInUse() override;
    virtual OUString SAL_CALL getParentStyle() override;
    virtual void SAL_CALL setParentStyle(const OUString& aParentStyle ) override;

    //XNamed
    virtual OUString SAL_CALL getName() override;
    virtual void SAL_CALL setName(const OUString& sName) override;

    //XPropertySet
    virtual css::uno::Reference<css::beans::XPropertySetInfo> SAL_CALL getPropertySetInfo() override;
    virtual void SAL_CALL setPropertyValue(const OUString& aPropertyName, const css::uno::Any& aValue) override;
    virtual css::uno::Any SAL_CALL getPropertyValue(const OUString& PropertyName) override;
    virtual void SAL_CALL addPropertyChangeListener(const OUString& aPropertyName, const css::uno::Reference<css::beans::XPropertyChangeListener>& xListener) override;
    virtual void SAL_CALL removePropertyChangeListener(const OUString& aPropertyName, const css::uno::Reference<css::beans::XPropertyChangeListener>& aListener) override;
    virtual void SAL_CALL addVetoableChangeListener(const OUString& PropertyName, const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;
    virtual void SAL_CALL removeVetoableChangeListener(const OUString& PropertyName, const css::uno::Reference<css::beans::XVetoableChangeListener>& aListener) override;

    //XPropertyState
    virtual css::beans::PropertyState SAL_CALL getPropertyState(const OUString& PropertyName) override;
    virtual css::uno::Sequence<css::beans::PropertyState> SAL_CALL getPropertyStates(const css::uno::Sequence< OUString >& aPropertyName) override;
    virtual void SAL_CALL setPropertyToDefault(const OUString& PropertyName) override;
    virtual css::uno::Any SAL_CALL getPropertyDefault(const OUString& aPropertyName) override;

    //XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    virtual css::uno::Sequence<OUString> SAL_CALL getSupportedServiceNames() override;

    static rtl::Reference<SwXTextCellStyle> CreateXTextCellStyle(SwDocShell* pDocShell, const UIName& sName);
};

class SW_DLLPUBLIC SwXStyleFamily final : public cppu::WeakImplHelper
<
    css::container::XNameContainer,
    css::lang::XServiceInfo,
    css::container::XIndexAccess,
    css::beans::XPropertySet
>
, public SfxListener
{
    const StyleFamilyEntry& m_rEntry;
    SfxStyleSheetBasePool* m_pBasePool;
    SwDocShell* m_pDocShell;

    SwXStyle* FindStyle(const UIName& rStyleName) const;
    sal_Int32 GetCountOrName(UIName* pString, sal_Int32 nIndex = SAL_MAX_INT32);
    rtl::Reference<SwXBaseStyle> getStyle(const SfxStyleSheetBase* pBase, const UIName& rStyleName);
    static const StyleFamilyEntry& InitEntry(SfxStyleFamily eFamily);
public:
    SwXStyleFamily(SwDocShell* pDocShell, const SfxStyleFamily eFamily);

    //XIndexAccess
    virtual sal_Int32 SAL_CALL getCount() override;
    virtual css::uno::Any SAL_CALL getByIndex(sal_Int32 nIndex) override;

    //XElementAccess
    virtual css::uno::Type SAL_CALL getElementType(  ) override;
    virtual sal_Bool SAL_CALL hasElements(  ) override;

    //XNameAccess
    virtual css::uno::Any SAL_CALL getByName(const OUString& Name) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getElementNames() override;
    virtual sal_Bool SAL_CALL hasByName(const OUString& Name) override;

    //XNameContainer
    virtual void SAL_CALL insertByName(const OUString& Name, const css::uno::Any& Element) override;
    virtual void SAL_CALL replaceByName(const OUString& Name, const css::uno::Any& Element) override;
    virtual void SAL_CALL removeByName(const OUString& Name) override;

    //XPropertySet
    virtual css::uno::Reference< css::beans::XPropertySetInfo > SAL_CALL getPropertySetInfo(  ) override;
    virtual void SAL_CALL setPropertyValue( const OUString&, const css::uno::Any&) override;
    virtual css::uno::Any SAL_CALL getPropertyValue( const OUString& PropertyName ) override;
    virtual void SAL_CALL addPropertyChangeListener( const OUString&, const css::uno::Reference<css::beans::XPropertyChangeListener>&) override;
    virtual void SAL_CALL removePropertyChangeListener( const OUString&, const css::uno::Reference<css::beans::XPropertyChangeListener>&) override;
    virtual void SAL_CALL addVetoableChangeListener(const OUString&, const css::uno::Reference<css::beans::XVetoableChangeListener>&) override;
    virtual void SAL_CALL removeVetoableChangeListener(const OUString&, const css::uno::Reference<css::beans::XVetoableChangeListener>&) override;

    //SfxListener
    virtual void Notify(SfxBroadcaster& rBC, const SfxHint& rHint) override;

    //XServiceInfo
    virtual OUString SAL_CALL getImplementationName() override;
    virtual sal_Bool SAL_CALL supportsService(const OUString& rServiceName) override;
    virtual css::uno::Sequence< OUString > SAL_CALL getSupportedServiceNames() override;

    rtl::Reference<SwXBaseStyle> getStyleByUIName(const UIName& rName);
    rtl::Reference<SwXBaseStyle> getStyleByName(const OUString& rName);
    rtl::Reference<SwXPageStyle> getPageStyleByName(const OUString& rName);
    rtl::Reference<SwXStyle> getCharacterStyleByName(const OUString& rName);
    rtl::Reference<SwXStyle> getParagraphStyleByName(const OUString& rName);
    void insertStyleByName(const OUString& Name, const rtl::Reference<SwXStyle>& Element);
private:
    void insertStyleByNameImpl(const rtl::Reference<SwXStyle>& Element, const UIName& sStyleName);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
