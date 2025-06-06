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
#ifndef INCLUDED_SVX_ZOOMSLIDERITEM_HXX
#define INCLUDED_SVX_ZOOMSLIDERITEM_HXX

#include <svl/intitem.hxx>
#include <svx/svxids.hrc>
#include <com/sun/star/uno/Sequence.hxx>
#include <svx/svxdllapi.h>


class SVX_DLLPUBLIC SvxZoomSliderItem final : public SfxUInt16Item
{
    css::uno::Sequence < sal_Int32 > maValues;
    sal_uInt16 mnMinZoom;
    sal_uInt16 mnMaxZoom;

public:
    static SfxPoolItem* CreateDefault();
    DECLARE_ITEM_TYPE_FUNCTION(SvxZoomSliderItem)
    SvxZoomSliderItem( sal_uInt16 nCurrentZoom = 100, sal_uInt16 nMinZoom = 20, sal_uInt16 nMaxZoom = 600, TypedWhichId<SvxZoomSliderItem> nWhich = SID_ATTR_ZOOMSLIDER );

    void AddSnappingPoint( sal_Int32 nNew );
    const css::uno::Sequence < sal_Int32 >& GetSnappingPoints() const { return maValues;}
    sal_uInt16 GetMinZoom() const {return mnMinZoom;}
    sal_uInt16 GetMaxZoom() const {return mnMaxZoom;}

    // "purely virtual methods" from the SfxPoolItem
    virtual bool            operator==( const SfxPoolItem& ) const override;
    virtual SvxZoomSliderItem* Clone( SfxItemPool *pPool = nullptr ) const override;
    virtual bool            QueryValue( css::uno::Any& rVal, sal_uInt8 nMemberId = 0 ) const override;
    virtual bool            PutValue( const css::uno::Any& rVal, sal_uInt8 nMemberId ) override;
};


#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
