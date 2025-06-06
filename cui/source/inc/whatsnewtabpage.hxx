/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#include <vcl/customweld.hxx>
#include <sfx2/tabdlg.hxx>
#include <vcl/graph.hxx>

class AnimatedBrand : public weld::CustomWidgetController
{
private:
    Graphic m_pGraphic;
    Size m_pGraphicSize;
    virtual void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle&) override;

public:
    AnimatedBrand();
    const Size& GetGraphicSize() const { return m_pGraphicSize; };
};

class WhatsNewTabPage : public SfxTabPage
{
private:
    AnimatedBrand m_aBrand;
    std::unique_ptr<weld::CustomWeld> m_pBrand;

public:
    WhatsNewTabPage(weld::Container* pPage, weld::DialogController* pController,
                    const SfxItemSet& rSet);
    static std::unique_ptr<SfxTabPage>
    Create(weld::Container* pPage, weld::DialogController* pController, const SfxItemSet* rSet);
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
