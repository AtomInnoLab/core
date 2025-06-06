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

#include <svx/AccessibleShape.hxx>
#include <svx/AccessibleShapeInfo.hxx>
#include <com/sun/star/accessibility/AccessibleRole.hpp>
#include <com/sun/star/accessibility/AccessibleStateType.hpp>
#include <com/sun/star/accessibility/AccessibleRelationType.hpp>
#include <com/sun/star/accessibility/AccessibleEventId.hpp>
#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/document/XShapeEventBroadcaster.hpp>
#include <com/sun/star/lang/IndexOutOfBoundsException.hpp>
#include <com/sun/star/drawing/FillStyle.hpp>
#include <com/sun/star/text/XText.hpp>
#include <sal/log.hxx>
#include <editeng/unoedsrc.hxx>
#include <svx/AccessibleTextHelper.hxx>
#include <svx/ChildrenManager.hxx>
#include <svx/IAccessibleParent.hxx>
#include <svx/IAccessibleViewForwarder.hxx>
#include <svx/unoshtxt.hxx>
#include <svx/svdobj.hxx>
#include <svx/unoapi.hxx>
#include <svx/svdpage.hxx>
#include <svx/ShapeTypeHandler.hxx>
#include <svx/SvxShapeTypes.hxx>

#include <vcl/svapp.hxx>
#include <vcl/window.hxx>
#include <unotools/accessiblerelationsethelper.hxx>
#include <svx/svdview.hxx>
#include <comphelper/diagnose_ex.hxx>
#include <cppuhelper/queryinterface.hxx>
#include <comphelper/sequence.hxx>
#include "AccessibleEmptyEditSource.hxx"

#include <algorithm>
#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::accessibility;
using ::com::sun::star::uno::Reference;
using ::com::sun::star::lang::IndexOutOfBoundsException;
using ::com::sun::star::uno::RuntimeException;

namespace accessibility {

namespace {

OUString GetOptionalProperty (
    const Reference<beans::XPropertySet>& rxSet,
    const OUString& rsPropertyName)
{
    OUString sValue;

    if (rxSet.is())
    {
        const Reference<beans::XPropertySetInfo> xInfo (rxSet->getPropertySetInfo());
        if ( ! xInfo.is() || xInfo->hasPropertyByName(rsPropertyName))
        {
            try
            {
                rxSet->getPropertyValue(rsPropertyName) >>= sValue;
            }
            catch (beans::UnknownPropertyException&)
            {
                // This exception should only be thrown when the property
                // does not exits (of course) and the XPropertySetInfo is
                // not available.
            }
        }
    }
    return sValue;
}

} // end of anonymous namespace

// internal
AccessibleShape::AccessibleShape (
    const AccessibleShapeInfo& rShapeInfo,
    const AccessibleShapeTreeInfo& rShapeTreeInfo)
    : AccessibleContextBase (rShapeInfo.mxParent,AccessibleRole::SHAPE),
      mxShape (rShapeInfo.mxShape),
      maShapeTreeInfo (rShapeTreeInfo),
      m_nIndexInParent(-1),
      mpParent (rShapeInfo.mpChildrenManager)
{
    m_pShape = SdrObject::getSdrObjectFromXShape(mxShape);
    UpdateNameAndDescription();
}

AccessibleShape::~AccessibleShape()
{
    mpChildrenManager.reset();
    mpText.reset();
    SAL_INFO("svx", "~AccessibleShape");

    // Unregistering from the various broadcasters should be unnecessary
    // since this destructor would not have been called if one of the
    // broadcasters would still hold a strong reference to this object.
}

void AccessibleShape::Init()
{
    // Update the OPAQUE and SELECTED shape.
    UpdateStates ();

    // Create a children manager when this shape has children of its own.
    Reference<drawing::XShapes> xShapes (mxShape, uno::UNO_QUERY);
    if (xShapes.is() && xShapes->getCount() > 0)
        mpChildrenManager.reset( new ChildrenManager (
            this, xShapes, maShapeTreeInfo, *this) );
    if (mpChildrenManager != nullptr)
        mpChildrenManager->Update();

    // Register at model as document::XEventListener.
    if (mxShape.is() && maShapeTreeInfo.GetModelBroadcaster().is())
        maShapeTreeInfo.GetModelBroadcaster()->addShapeEventListener(mxShape,
            static_cast<document::XShapeEventListener*>(this));

    // Beware! Here we leave the paths of the UNO API and descend into the
    // depths of the core.  Necessary for making the edit engine
    // accessible.
    Reference<text::XText> xText (mxShape, uno::UNO_QUERY);
    if (!xText.is())
        return;

    SdrView* pView = maShapeTreeInfo.GetSdrView ();
    const vcl::Window* pWindow = maShapeTreeInfo.GetWindow ();
    if (!(pView != nullptr && pWindow != nullptr && mxShape.is()))
        return;

    // #107948# Determine whether shape text is empty
    SdrObject* pSdrObject = SdrObject::getSdrObjectFromXShape(mxShape);
    if( !pSdrObject )
        return;

    SdrTextObj* pTextObj = DynCastSdrTextObj( pSdrObject  );
    const bool hasOutlinerParaObject = (pTextObj && pTextObj->CanCreateEditOutlinerParaObject()) || (pSdrObject->GetOutlinerParaObject() != nullptr);

    // create AccessibleTextHelper to handle this shape's text
    if( !hasOutlinerParaObject )
    {
        // empty text -> use proxy edit source to delay creation of EditEngine
        mpText.reset( new AccessibleTextHelper( std::make_unique<AccessibleEmptyEditSource >(*pSdrObject, *pView, *pWindow->GetOutDev()) ) );
    }
    else
    {
        // non-empty text -> use full-fledged edit source right away
        mpText.reset( new AccessibleTextHelper( std::make_unique<SvxTextEditSource >(*pSdrObject, nullptr, *pView, *pWindow->GetOutDev()) ) );
    }
    if( pWindow->HasFocus() )
        mpText->SetFocus();

    mpText->SetEventSource(this);
}


void AccessibleShape::UpdateStates()
{
    // Set the opaque state for certain shape types when their fill style is
    // solid.
    bool bShapeIsOpaque = false;
    switch (ShapeTypeHandler::Instance().GetTypeId (mxShape))
    {
        case DRAWING_PAGE:
        case DRAWING_RECTANGLE:
        case DRAWING_TEXT:
        {
            uno::Reference<beans::XPropertySet> xSet (mxShape, uno::UNO_QUERY);
            if (xSet.is())
            {
                try
                {
                    drawing::FillStyle aFillStyle;
                    bShapeIsOpaque =  ( xSet->getPropertyValue (u"FillStyle"_ustr) >>= aFillStyle)
                                        && aFillStyle == drawing::FillStyle_SOLID;
                }
                catch (css::beans::UnknownPropertyException&)
                {
                    // Ignore.
                }
            }
        }
    }
    if (bShapeIsOpaque)
        mnStateSet |= AccessibleStateType::OPAQUE;
    else
        mnStateSet &= ~AccessibleStateType::OPAQUE;

    // Set the selected state.
    bool bShapeIsSelected = false;
    // XXX fix_me this has to be done with an extra interface later on
    if ( m_pShape && maShapeTreeInfo.GetSdrView() )
    {
        bShapeIsSelected = maShapeTreeInfo.GetSdrView()->IsObjMarked(m_pShape);
    }

    if (bShapeIsSelected)
        mnStateSet |= AccessibleStateType::SELECTED;
    else
        mnStateSet &= ~AccessibleStateType::SELECTED;
}

OUString AccessibleShape::GetStyle() const
{
    return ShapeTypeHandler::CreateAccessibleBaseName( mxShape );
}

bool AccessibleShape::SetState (sal_Int64 aState)
{
    bool bStateHasChanged = false;

    if (aState == AccessibleStateType::FOCUSED && mpText != nullptr)
    {
        // Offer FOCUSED state to edit engine and detect whether the state
        // changes.
        bool bIsFocused = mpText->HaveFocus ();
        mpText->SetFocus();
        bStateHasChanged = (bIsFocused != mpText->HaveFocus ());
    }
    else
        bStateHasChanged = AccessibleContextBase::SetState (aState);

    return bStateHasChanged;
}


bool AccessibleShape::ResetState (sal_Int64 aState)
{
    bool bStateHasChanged = false;

    if (aState == AccessibleStateType::FOCUSED && mpText != nullptr)
    {
        // Try to remove FOCUSED state from the edit engine and detect
        // whether the state changes.
        bool bIsFocused = mpText->HaveFocus ();
        mpText->SetFocus (false);
        bStateHasChanged = (bIsFocused != mpText->HaveFocus ());
    }
    else
        bStateHasChanged = AccessibleContextBase::ResetState (aState);

    return bStateHasChanged;
}


bool AccessibleShape::GetState (sal_Int64 aState)
{
    if (aState == AccessibleStateType::FOCUSED && mpText != nullptr)
    {
        // Just delegate the call to the edit engine.  The state is not
        // merged into the state set.
        return mpText->HaveFocus();
    }
    else
        return AccessibleContextBase::GetState (aState);
}

// OverWrite the parent's getAccessibleName method
OUString SAL_CALL AccessibleShape::getAccessibleName()
{
    ensureAlive();
    if (m_pShape && !m_pShape->GetTitle().isEmpty())
        return CreateAccessibleName() + " " + m_pShape->GetTitle();
    else
        return CreateAccessibleName();
}

OUString SAL_CALL AccessibleShape::getAccessibleDescription()
{
    ensureAlive();
    if( m_pShape && !m_pShape->GetDescription().isEmpty())
        return m_pShape->GetDescription() ;

    return OUString();
}

// XAccessibleContext
/** The children of this shape come from two sources: The children from
    group or scene shapes and the paragraphs of text.
*/
sal_Int64 SAL_CALL
       AccessibleShape::getAccessibleChildCount ()
{
    if (!isAlive())
    {
        return 0;
    }

    sal_Int64 nChildCount = 0;

    // Add the number of shapes that are children of this shape.
    if (mpChildrenManager != nullptr)
        nChildCount += mpChildrenManager->GetChildCount ();
    // Add the number text paragraphs.
    if (mpText != nullptr)
        nChildCount += mpText->GetChildCount ();

    return nChildCount;
}


/** Forward the request to the shape.  Return the requested shape or throw
    an exception for a wrong index.
*/
uno::Reference<XAccessible> SAL_CALL
    AccessibleShape::getAccessibleChild (sal_Int64 nIndex)
{
    ensureAlive();

    uno::Reference<XAccessible> xChild;

    // Depending on the index decide whether to delegate this call to the
    // children manager or the edit engine.
    if ((mpChildrenManager != nullptr)
        && (nIndex < mpChildrenManager->GetChildCount()))
    {
        xChild = mpChildrenManager->GetChild (nIndex);
    }
    else if (mpText != nullptr)
    {
        sal_Int64 nI = nIndex;
        if (mpChildrenManager != nullptr)
            nI -= mpChildrenManager->GetChildCount();
        xChild = mpText->GetChild (nI);
    }
    else
        throw lang::IndexOutOfBoundsException (
            "shape has no child with index " + OUString::number(nIndex),
            getXWeak());

    return xChild;
}

uno::Reference<XAccessibleRelationSet> SAL_CALL
    AccessibleShape::getAccessibleRelationSet()
{
    ::osl::MutexGuard aGuard (m_aMutex);
    if (mpParent == nullptr)
        return uno::Reference<XAccessibleRelationSet>();

    rtl::Reference<::utl::AccessibleRelationSetHelper> pRelationSet = new utl::AccessibleRelationSetHelper;

    //this mxshape is the captioned shape
    uno::Sequence<uno::Reference<css::accessibility::XAccessible>> aSequence { mpParent->GetAccessibleCaption(mxShape) };
    if(aSequence[0])
    {
        pRelationSet->AddRelation(
                                  AccessibleRelation(AccessibleRelationType_DESCRIBED_BY, aSequence));
    }
    return pRelationSet;
}

/** Return a copy of the state set.
    Possible states are:
        ENABLED
        SHOWING
        VISIBLE
*/
sal_Int64 SAL_CALL
    AccessibleShape::getAccessibleStateSet()
{
    ::osl::MutexGuard aGuard (m_aMutex);

    if (!isAlive())
    {
        // Return a minimal state set that only contains the DEFUNC state.
        return AccessibleContextBase::getAccessibleStateSet ();
    }

    // Merge current FOCUSED state from edit engine.
    if (mpText)
    {
        if (mpText->HaveFocus())
            mnStateSet |= AccessibleStateType::FOCUSED;
        else
            mnStateSet &= ~AccessibleStateType::FOCUSED;
    }
    //Just when the document is not read-only,set states EDITABLE,RESIZABLE,MOVEABLE
    css::uno::Reference<XAccessible> xTempAcc = getAccessibleParent();
    if( xTempAcc.is() )
    {
        css::uno::Reference<XAccessibleContext>
                                xTempAccContext = xTempAcc->getAccessibleContext();
        if( xTempAccContext.is() )
        {
            sal_Int64 nState = xTempAccContext->getAccessibleStateSet();
            if (nState & AccessibleStateType::EDITABLE)
            {
                mnStateSet |= AccessibleStateType::EDITABLE;
                mnStateSet |= AccessibleStateType::RESIZABLE;
                mnStateSet |= AccessibleStateType::MOVEABLE;
            }
        }
    }

    sal_Int64 nRetStateSet = mnStateSet;

    if (mpParent && mpParent->IsDocumentSelAll())
    {
        nRetStateSet |= AccessibleStateType::SELECTED;
    }

    return nRetStateSet;
}

// XAccessibleComponent
/** The implementation below is at the moment straightforward.  It iterates
    over all children (and thereby instances all children which have not
    been already instantiated) until a child covering the specified point is
    found.
    This leaves room for improvement.  For instance, first iterate only over
    the already instantiated children and only if no match is found
    instantiate the remaining ones.
*/
uno::Reference<XAccessible > SAL_CALL
    AccessibleShape::getAccessibleAtPoint (
        const awt::Point& aPoint)
{
    ::osl::MutexGuard aGuard (m_aMutex);

    sal_Int64 nChildCount = getAccessibleChildCount ();
    for (sal_Int64 i = 0; i < nChildCount; ++i)
    {
        Reference<XAccessible> xChild (getAccessibleChild (i));
        if (xChild.is())
        {
            Reference<XAccessibleComponent> xChildComponent (
                xChild->getAccessibleContext(), uno::UNO_QUERY);
            if (xChildComponent.is())
            {
                awt::Rectangle aBBox (xChildComponent->getBounds());
                if ( (aPoint.X >= aBBox.X)
                    && (aPoint.Y >= aBBox.Y)
                    && (aPoint.X < aBBox.X+aBBox.Width)
                    && (aPoint.Y < aBBox.Y+aBBox.Height) )
                    return xChild;
            }
        }
    }

    // Have not found a child under the given point.  Returning empty
    // reference to indicate this.
    return uno::Reference<XAccessible>();
}


awt::Rectangle AccessibleShape::implGetBounds()
{
    awt::Rectangle aBoundingBox;
    if ( mxShape.is() )
    {

        static constexpr OUString sBoundRectName = u"BoundRect"_ustr;
        static constexpr OUString sAnchorPositionName = u"AnchorPosition"_ustr;

        // Get the shape's bounding box in internal coordinates (in 100th of
        // mm).  Use the property BoundRect.  Only if that is not supported ask
        // the shape for its position and size directly.
        Reference<beans::XPropertySet> xSet (mxShape, uno::UNO_QUERY);
        Reference<beans::XPropertySetInfo> xSetInfo;
        bool bFoundBoundRect = false;
        if (xSet.is())
        {
            xSetInfo = xSet->getPropertySetInfo ();
            if (xSetInfo.is())
            {
                if (xSetInfo->hasPropertyByName (sBoundRectName))
                {
                    try
                    {
                        uno::Any aValue = xSet->getPropertyValue (sBoundRectName);
                        aValue >>= aBoundingBox;
                        bFoundBoundRect = true;
                    }
                    catch (beans::UnknownPropertyException const&)
                    {
                        // Handled below (bFoundBoundRect stays false).
                    }
                }
                else
                    SAL_WARN("svx", "no property BoundRect");
            }
        }

        // Fallback when there is no BoundRect Property.
        if ( ! bFoundBoundRect )
        {
            awt::Point aPosition (mxShape->getPosition());
            awt::Size aSize (mxShape->getSize());
            aBoundingBox = awt::Rectangle (
                aPosition.X, aPosition.Y,
                aSize.Width, aSize.Height);

            // While BoundRects have absolute positions, the position returned
            // by XPosition::getPosition is relative.  Get the anchor position
            // (usually not (0,0) for Writer shapes).
            if (xSetInfo.is())
            {
                if (xSetInfo->hasPropertyByName (sAnchorPositionName))
                {
                    uno::Any aPos = xSet->getPropertyValue (sAnchorPositionName);
                    awt::Point aAnchorPosition;
                    aPos >>= aAnchorPosition;
                    aBoundingBox.X += aAnchorPosition.X;
                    aBoundingBox.Y += aAnchorPosition.Y;
                }
            }
        }

        // Transform coordinates from internal to pixel.
        if (maShapeTreeInfo.GetViewForwarder() == nullptr)
            throw uno::RuntimeException (
                u"AccessibleShape has no valid view forwarder"_ustr,
                getXWeak());
        ::Size aPixelSize = maShapeTreeInfo.GetViewForwarder()->LogicToPixel (
            ::Size (aBoundingBox.Width, aBoundingBox.Height));
        ::Point aPixelPosition = maShapeTreeInfo.GetViewForwarder()->LogicToPixel (
            ::Point (aBoundingBox.X, aBoundingBox.Y));

        // Clip the shape's bounding box with the bounding box of its parent.
        Reference<XAccessibleComponent> xParentComponent (
            getAccessibleParent(), uno::UNO_QUERY);
        if (xParentComponent.is())
        {
            // Make the coordinates relative to the parent.
            awt::Point aParentLocation (xParentComponent->getLocationOnScreen());
            int x = aPixelPosition.getX() - aParentLocation.X;
            int y = aPixelPosition.getY() - aParentLocation.Y;

            // Clip with parent (with coordinates relative to itself).
            ::tools::Rectangle aBBox (
                x, y, x + aPixelSize.getWidth(), y + aPixelSize.getHeight());
            awt::Size aParentSize (xParentComponent->getSize());
            ::tools::Rectangle aParentBBox (0,0, aParentSize.Width, aParentSize.Height);
            aBBox = aBBox.GetIntersection (aParentBBox);
            aBoundingBox = awt::Rectangle (
                aBBox.Left(),
                aBBox.Top(),
                aBBox.getOpenWidth(),
                aBBox.getOpenHeight());
        }
        else
        {
            SAL_INFO("svx", "parent does not support component");
            aBoundingBox = awt::Rectangle (
                aPixelPosition.getX(), aPixelPosition.getY(),
                aPixelSize.getWidth(), aPixelSize.getHeight());
        }
    }

    return aBoundingBox;
}

sal_Int32 SAL_CALL AccessibleShape::getForeground()
{
    ensureAlive();
    sal_Int32 nColor (0x0ffffffL);

    try
    {
        uno::Reference<beans::XPropertySet> aSet (mxShape, uno::UNO_QUERY);
        if (aSet.is())
        {
            uno::Any aColor;
            aColor = aSet->getPropertyValue (u"LineColor"_ustr);
            aColor >>= nColor;
        }
    }
    catch (const css::beans::UnknownPropertyException &)
    {
        // Ignore exception and return default color.
    }
    return nColor;
}


sal_Int32 SAL_CALL AccessibleShape::getBackground()
{
    ensureAlive();
    Color nColor;

    try
    {
        uno::Reference<beans::XPropertySet> aSet (mxShape, uno::UNO_QUERY);
        if (aSet.is())
        {
            uno::Any aColor;
            aColor = aSet->getPropertyValue (u"FillColor"_ustr);
            aColor >>= nColor;
            aColor = aSet->getPropertyValue (u"FillTransparence"_ustr);
            short nTrans=0;
            aColor >>= nTrans;
            Color crBk(nColor);
            if (nTrans == 0 )
            {
                crBk.SetAlpha(0);
            }
            else
            {
                nTrans = short(256 - nTrans / 100. * 256);
                crBk.SetAlpha(255 - sal_uInt8(nTrans));
            }
            nColor = crBk;
        }
    }
    catch (const css::beans::UnknownPropertyException &)
    {
        // Ignore exception and return default color.
    }
    return sal_Int32(nColor);
}

// XAccessibleEventBroadcaster
void SAL_CALL AccessibleShape::addAccessibleEventListener (
    const Reference<XAccessibleEventListener >& rxListener)
{
    AccessibleContextBase::addAccessibleEventListener(rxListener);

    if (isAlive())
    {

        if (mpText != nullptr)
            mpText->AddEventListener (rxListener);
    }
}


void SAL_CALL AccessibleShape::removeAccessibleEventListener (
    const Reference<XAccessibleEventListener >& rxListener)
{
    AccessibleContextBase::removeAccessibleEventListener (rxListener);
    if (mpText != nullptr)
        mpText->RemoveEventListener (rxListener);
}

// XInterface
css::uno::Any SAL_CALL
    AccessibleShape::queryInterface (const css::uno::Type & rType)
{
    css::uno::Any aReturn = AccessibleContextBase::queryInterface (rType);
    if ( ! aReturn.hasValue())
        aReturn = ::cppu::queryInterface (rType,
            static_cast<XAccessibleComponent*>(this),
            static_cast<XAccessibleExtendedComponent*>(this),
            static_cast< css::accessibility::XAccessibleSelection* >(this),
            static_cast< css::accessibility::XAccessibleExtendedAttributes* >(this),
            static_cast<document::XShapeEventListener*>(this),
            static_cast<lang::XUnoTunnel*>(this),
            static_cast<XAccessibleGroupPosition*>(this),
            static_cast<XAccessibleHypertext*>(this)
            );
    return aReturn;
}


void SAL_CALL
    AccessibleShape::acquire()
    noexcept
{
    AccessibleContextBase::acquire ();
}


void SAL_CALL
    AccessibleShape::release()
    noexcept
{
    AccessibleContextBase::release ();
}

// XAccessibleSelection
void SAL_CALL AccessibleShape::selectAccessibleChild( sal_Int64 )
{
}


sal_Bool SAL_CALL AccessibleShape::isAccessibleChildSelected( sal_Int64 nChildIndex )
{
    uno::Reference<XAccessible> xAcc = getAccessibleChild( nChildIndex );
    uno::Reference<XAccessibleContext> xContext;
    if( xAcc.is() )
    {
        xContext = xAcc->getAccessibleContext();
    }

    if( xContext.is() )
    {
        if( xContext->getAccessibleRole() == AccessibleRole::PARAGRAPH )
        {
            uno::Reference< css::accessibility::XAccessibleText >
                xText(xAcc, uno::UNO_QUERY);
            if( xText.is() )
            {
                if( xText->getSelectionStart() >= 0 ) return true;
            }
        }
        else if( xContext->getAccessibleRole() == AccessibleRole::SHAPE )
        {
            sal_Int64 pRState = xContext->getAccessibleStateSet();

            return bool(pRState & AccessibleStateType::SELECTED);
        }
    }

    return false;
}


void SAL_CALL AccessibleShape::clearAccessibleSelection(  )
{
}


void SAL_CALL AccessibleShape::selectAllAccessibleChildren(  )
{
}


sal_Int64 SAL_CALL AccessibleShape::getSelectedAccessibleChildCount()
{
    sal_Int64 nCount = 0;
    sal_Int64 TotalCount = getAccessibleChildCount();
    for( sal_Int64 i = 0; i < TotalCount; i++ )
        if( isAccessibleChildSelected(i) ) nCount++;

    return nCount;
}


Reference<XAccessible> SAL_CALL AccessibleShape::getSelectedAccessibleChild( sal_Int64 nSelectedChildIndex )
{
    if ( nSelectedChildIndex > getSelectedAccessibleChildCount() )
        throw IndexOutOfBoundsException();
    for (sal_Int64 i1 = 0, i2 = 0; i1 < getAccessibleChildCount(); i1++)
        if( isAccessibleChildSelected(i1) )
        {
            if( i2 == nSelectedChildIndex )
                return getAccessibleChild( i1 );
            i2++;
        }
    return Reference<XAccessible>();
}


void SAL_CALL AccessibleShape::deselectAccessibleChild( sal_Int64 )
{

}

// XAccessibleExtendedAttributes
OUString SAL_CALL AccessibleShape::getExtendedAttributes()
{
    if (getAccessibleRole() != AccessibleRole::SHAPE)
        return OUString();

    if (m_pShape)
        return "style:" + GetStyle() + ";";

    return OUString();
}

// XServiceInfo
OUString SAL_CALL
    AccessibleShape::getImplementationName()
{
    return u"AccessibleShape"_ustr;
}


uno::Sequence<OUString> SAL_CALL
    AccessibleShape::getSupportedServiceNames()
{
    ensureAlive();
    const css::uno::Sequence<OUString> vals { u"com.sun.star.drawing.AccessibleShape"_ustr };
    return comphelper::concatSequences(AccessibleContextBase::getSupportedServiceNames(), vals);
}

// XTypeProvider
uno::Sequence<uno::Type> SAL_CALL
    AccessibleShape::getTypes()
{
    ensureAlive();
    // Get list of types from the context base implementation, ...
    uno::Sequence<uno::Type> aTypeList (AccessibleContextBase::getTypes());
    // ... define local types
    uno::Sequence<uno::Type> localTypesList = {
        cppu::UnoType<lang::XEventListener>::get(),
        cppu::UnoType<document::XEventListener>::get(),
        cppu::UnoType<lang::XUnoTunnel>::get()
    };

    return comphelper::concatSequences(aTypeList, localTypesList);
}

// lang::XEventListener
/** Disposing calls are accepted only from the model: Just reset the
    reference to the model in the shape tree info.  Otherwise this object
    remains functional.
*/
void AccessibleShape::disposing (const lang::EventObject& aEvent)
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard (m_aMutex);

    try
    {
        if (aEvent.Source ==  maShapeTreeInfo.GetModelBroadcaster())
        {
            // Remove reference to model broadcaster to allow it to pass
            // away.
            maShapeTreeInfo.SetModelBroadcaster(nullptr);
        }

    }
    catch (uno::RuntimeException const&)
    {
        TOOLS_WARN_EXCEPTION("svx", "caught exception while disposing");
    }
    mpChildrenManager.reset();
    mxShape.clear();
    maShapeTreeInfo.dispose();
    mpText.reset();
}

// document::XShapeEventListener
void SAL_CALL
    AccessibleShape::notifyShapeEvent (const document::EventObject& rEventObject)
{
    if (rEventObject.EventName != "ShapeModified")
        return;

    //Need to update text children when receiving ShapeModified hint when exiting edit mode for text box
    if (mpText)
        mpText->UpdateChildren();


    // Some property of a shape has been modified.  Send an event
    // that indicates a change of the visible data to all listeners.
    CommitChange (
        AccessibleEventId::VISIBLE_DATA_CHANGED,
        uno::Any(),
        uno::Any(), -1);

    // Name and Description may have changed.  Update the local
    // values accordingly.
    UpdateNameAndDescription();
}

// lang::XUnoTunnel
UNO3_GETIMPLEMENTATION_IMPL(AccessibleShape)

// IAccessibleViewForwarderListener
void AccessibleShape::ViewForwarderChanged()
{
    // Inform all listeners that the graphical representation (i.e. size
    // and/or position) of the shape has changed.
    CommitChange (AccessibleEventId::VISIBLE_DATA_CHANGED,
        uno::Any(),
        uno::Any(), -1);

    // Tell children manager of the modified view forwarder.
    if (mpChildrenManager != nullptr)
        mpChildrenManager->ViewForwarderChanged();

    // update our children that our screen position might have changed
    if( mpText )
        mpText->UpdateChildren();
}

// protected internal
// Set this object's name if is different to the current name.
OUString AccessibleShape::CreateAccessibleBaseName()
{
    return ShapeTypeHandler::CreateAccessibleBaseName( mxShape );
}


OUString AccessibleShape::CreateAccessibleName()
{
    return GetFullAccessibleName(this);
}

OUString AccessibleShape::GetFullAccessibleName (AccessibleShape *shape)
{
    OUString sName (shape->CreateAccessibleBaseName());
    // Append the shape's index to the name to disambiguate between shapes
    // of the same type.  If such an index where not given to the
    // constructor then use the z-order instead.  If even that does not exist
    // we throw an exception.
    OUString nameStr;
    if (shape->m_pShape)
        nameStr = shape->m_pShape->GetName();
    if (nameStr.isEmpty())
    {
        sName += " ";
    }
    else
    {
        sName = nameStr;
    }

    //If the new produced name if not the same with last,notify name changed
    //Event
    if (aAccName != sName && !aAccName.isEmpty())
    {
        uno::Any aOldValue, aNewValue;
        aOldValue <<= aAccName;
        aNewValue <<= sName;
        CommitChange(
            AccessibleEventId::NAME_CHANGED,
            aNewValue,
            aOldValue, -1);
    }
    aAccName = sName;
    return sName;
}

// protected
void AccessibleShape::disposing()
{
    SolarMutexGuard aSolarGuard;
    ::osl::MutexGuard aGuard (m_aMutex);

    // Make sure to send an event that this object loses the focus in the
    // case that it has the focus.
    mnStateSet &= ~AccessibleStateType::FOCUSED;

    // Unregister from model.
    if (mxShape.is() && maShapeTreeInfo.GetModelBroadcaster().is())
        maShapeTreeInfo.GetModelBroadcaster()->removeShapeEventListener(mxShape,
            static_cast<document::XShapeEventListener*>(this));

    // Release the child containers.
    if (mpChildrenManager != nullptr)
    {
        mpChildrenManager.reset();
    }
    if (mpText != nullptr)
    {
        mpText->Dispose();
        mpText.reset();
    }

    // Cleanup.  Remove references to objects to allow them to be
    // destroyed.
    mxShape = nullptr;
    maShapeTreeInfo.dispose();

    // Call base classes.
    AccessibleContextBase::disposing();
}

sal_Int64 SAL_CALL AccessibleShape::getAccessibleIndexInParent()
{
    ensureAlive();

    if (m_nIndexInParent != -1)
        return m_nIndexInParent;

    return AccessibleContextBase::getAccessibleIndexInParent();
}


void AccessibleShape::UpdateNameAndDescription()
{
    // Ignore missing title, name, or description.  There are fallbacks for
    // them.
    try
    {
        Reference<beans::XPropertySet> xSet (mxShape, uno::UNO_QUERY_THROW);

        // Get the accessible name.
        OUString sString = GetOptionalProperty(xSet, u"Title"_ustr);
        if (!sString.isEmpty())
        {
            SetAccessibleName(sString, AccessibleContextBase::FromShape);
        }
        else
        {
            sString = GetOptionalProperty(xSet, u"Name"_ustr);
            if (!sString.isEmpty())
                SetAccessibleName(sString, AccessibleContextBase::FromShape);
        }

        // Get the accessible description.
        sString = GetOptionalProperty(xSet, u"Description"_ustr);
        if (!sString.isEmpty())
            SetAccessibleDescription(sString, AccessibleContextBase::FromShape);
    }
    catch (uno::RuntimeException&)
    {
    }
}

//  Return this object's role.
sal_Int16 SAL_CALL AccessibleShape::getAccessibleRole()
{
    sal_Int16 nAccessibleRole =  AccessibleRole::SHAPE ;
    switch (ShapeTypeHandler::Instance().GetTypeId (mxShape))
    {
        case     DRAWING_GRAPHIC_OBJECT:
                 nAccessibleRole =  AccessibleRole::GRAPHIC ;               break;
        case     DRAWING_OLE:
                 nAccessibleRole =  AccessibleRole::EMBEDDED_OBJECT ;       break;

        default:
            nAccessibleRole = AccessibleContextBase::getAccessibleRole();
            break;
    }

    return nAccessibleRole;
}

namespace {

//sort the drawing objects from up to down, from left to right
struct XShapePosCompareHelper
{
    bool operator() ( const uno::Reference<drawing::XShape>& xshape1,
        const uno::Reference<drawing::XShape>& xshape2 ) const
    {
        SdrObject* pObj1 = SdrObject::getSdrObjectFromXShape(xshape1);
        SdrObject* pObj2 = SdrObject::getSdrObjectFromXShape(xshape2);
        if(pObj1 && pObj2)
            return pObj1->GetOrdNum() < pObj2->GetOrdNum();
        else
            return false;
    }
};

}
//end of group position

// XAccessibleGroupPosition
uno::Sequence< sal_Int32 > SAL_CALL
AccessibleShape::getGroupPosition( const uno::Any& )
{
    // we will return the:
    // [0] group level
    // [1] similar items counts in the group
    // [2] the position of the object in the group
    uno::Sequence< sal_Int32 > aRet{ 0, 0, 0 };

    css::uno::Reference<XAccessible> xParent = getAccessibleParent();
    if (!xParent.is())
    {
        return aRet;
    }
    SdrObject *pObj = SdrObject::getSdrObjectFromXShape(mxShape);


    if(pObj == nullptr )
    {
        return aRet;
    }

    // Compute object's group level.
    sal_Int32 nGroupLevel = 0;
    SdrObject * pUper = pObj->getParentSdrObjectFromSdrObject();
    while( pUper )
    {
        ++nGroupLevel;
        pUper = pUper->getParentSdrObjectFromSdrObject();
    }

    css::uno::Reference<XAccessibleContext> xParentContext = xParent->getAccessibleContext();
    if( xParentContext->getAccessibleRole()  == AccessibleRole::DOCUMENT ||
            xParentContext->getAccessibleRole()  == AccessibleRole::DOCUMENT_PRESENTATION ||
            xParentContext->getAccessibleRole()  == AccessibleRole::DOCUMENT_SPREADSHEET ||
            xParentContext->getAccessibleRole()  == AccessibleRole::DOCUMENT_TEXT )//Document
    {
        Reference< XAccessibleGroupPosition > xGroupPosition( xParent,uno::UNO_QUERY );
        if ( xGroupPosition.is() )
        {
            aRet = xGroupPosition->getGroupPosition( uno::Any( getAccessibleContext() ) );
        }
        return aRet;
    }
    if (xParentContext->getAccessibleRole() != AccessibleRole::SHAPE)
    {
        return aRet;
    }

    SdrObjList *pGrpList = nullptr;
    if( pObj->getParentSdrObjectFromSdrObject() )
        pGrpList = pObj->getParentSdrObjectFromSdrObject()->GetSubList();
    else
        return aRet;

    std::vector< uno::Reference<drawing::XShape> > vXShapes;
    if (pGrpList)
    {
        const size_t nObj = pGrpList->GetObjCount();
        for(size_t i = 0 ; i < nObj ; ++i)
        {
            SdrObject *pSubObj = pGrpList->GetObj(i);
            if (pSubObj &&
                xParentContext->getAccessibleChild(i)->getAccessibleContext()->getAccessibleRole() != AccessibleRole::GROUP_BOX)
            {
                vXShapes.push_back( GetXShapeForSdrObject(pSubObj) );
            }
        }
    }

    std::sort( vXShapes.begin(), vXShapes.end(), XShapePosCompareHelper() );

    //get the index of the selected object in the group
    //we start counting position from 1
    sal_Int32 nPos = 1;
    for ( const auto& rpShape : vXShapes )
    {
        if ( rpShape.get() == mxShape.get() )
        {
            sal_Int32* pArray = aRet.getArray();
            pArray[0] = nGroupLevel;
            pArray[1] = vXShapes.size();
            pArray[2] = nPos;
            break;
        }
        nPos++;
    }

    return aRet;
}

OUString AccessibleShape::getObjectLink( const uno::Any& )
{
    OUString aRet;

    SdrObject *pObj = SdrObject::getSdrObjectFromXShape(mxShape);
    if(pObj == nullptr )
    {
        return aRet;
    }
    if (maShapeTreeInfo.GetDocumentWindow().is())
    {
        Reference< XAccessibleGroupPosition > xGroupPosition( maShapeTreeInfo.GetDocumentWindow(), uno::UNO_QUERY );
        if (xGroupPosition.is())
        {
            aRet = xGroupPosition->getObjectLink( uno::Any( getAccessibleContext() ) );
        }
    }
    return aRet;
}

// XAccessibleHypertext
sal_Int32 SAL_CALL AccessibleShape::getHyperLinkCount()
{
    // MT: Introduced with IA2 CWS, but SvxAccessibleHyperlink was redundant to svx::AccessibleHyperlink which we introduced meanwhile.
    // Code need to be adapted...
    return 0;

    /*
    SvxAccessibleHyperlink* pLink = new SvxAccessibleHyperlink(m_pShape,this);
    if (pLink->IsValidHyperlink())
        return 1;
    else
        return 0;
    */
}
uno::Reference< XAccessibleHyperlink > SAL_CALL
    AccessibleShape::getHyperLink( sal_Int32 )
{
    uno::Reference< XAccessibleHyperlink > xRet;
    // MT: Introduced with IA2 CWS, but SvxAccessibleHyperlink was redundant to svx::AccessibleHyperlink which we introduced meanwhile.
    // Code need to be adapted...
    /*
    SvxAccessibleHyperlink* pLink = new SvxAccessibleHyperlink(m_pShape,this);
    if (pLink->IsValidHyperlink())
        xRet = pLink;
    if( !xRet.is() )
        throw css::lang::IndexOutOfBoundsException();
    */
    return xRet;
}
sal_Int32 SAL_CALL AccessibleShape::getHyperLinkIndex( sal_Int32 )
{
    return 0;
}
// XAccessibleText
sal_Int32 SAL_CALL AccessibleShape::getCaretPosition(  ){return 0;}
sal_Bool SAL_CALL AccessibleShape::setCaretPosition( sal_Int32 ){return false;}
sal_Unicode SAL_CALL AccessibleShape::getCharacter( sal_Int32 ){return 0;}
css::uno::Sequence< css::beans::PropertyValue > SAL_CALL AccessibleShape::getCharacterAttributes( sal_Int32, const css::uno::Sequence< OUString >& )
{
    uno::Sequence< css::beans::PropertyValue > aValues(0);
    return aValues;
}
css::awt::Rectangle SAL_CALL AccessibleShape::getCharacterBounds( sal_Int32 )
{
    return css::awt::Rectangle(0, 0, 0, 0 );
}
sal_Int32 SAL_CALL AccessibleShape::getCharacterCount(  ){return 0;}
sal_Int32 SAL_CALL AccessibleShape::getIndexAtPoint( const css::awt::Point& ){return 0;}
OUString SAL_CALL AccessibleShape::getSelectedText(  ){return OUString();}
sal_Int32 SAL_CALL AccessibleShape::getSelectionStart(  ){return 0;}
sal_Int32 SAL_CALL AccessibleShape::getSelectionEnd(  ){return 0;}
sal_Bool SAL_CALL AccessibleShape::setSelection( sal_Int32, sal_Int32 ){return true;}
OUString SAL_CALL AccessibleShape::getText(  ){return OUString();}
OUString SAL_CALL AccessibleShape::getTextRange( sal_Int32, sal_Int32 ){return OUString();}
css::accessibility::TextSegment SAL_CALL AccessibleShape::getTextAtIndex( sal_Int32, sal_Int16 )
{
    css::accessibility::TextSegment aResult;
    return aResult;
}
css::accessibility::TextSegment SAL_CALL AccessibleShape::getTextBeforeIndex( sal_Int32, sal_Int16 )
{
    css::accessibility::TextSegment aResult;
    return aResult;
}
css::accessibility::TextSegment SAL_CALL AccessibleShape::getTextBehindIndex( sal_Int32, sal_Int16 )
{
    css::accessibility::TextSegment aResult;
    return aResult;
}
sal_Bool SAL_CALL AccessibleShape::copyText( sal_Int32, sal_Int32 ){return true;}
sal_Bool SAL_CALL AccessibleShape::scrollSubstringTo( sal_Int32, sal_Int32, AccessibleScrollType ){return false;}

} // end of namespace accessibility

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
