/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 */

#pragma once

#include <sal/config.h>
#include <svx/svxdllapi.h>
#include <editeng/editeng.hxx>
#include <editeng/editview.hxx>
#include <vcl/customweld.hxx>
#include <vcl/outdev.hxx>

class WeldEditAccessible;

class SVX_DLLPUBLIC WeldEditView : public weld::CustomWidgetController, public EditViewCallbacks
{
public:
    WeldEditView();
    virtual void SetDrawingArea(weld::DrawingArea* pDrawingArea) override;
    virtual int GetSurroundingText(OUString& rSurrounding) override;
    virtual bool DeleteSurroundingText(const Selection& rRange) override;

    void SetText(const OUString& rStr);

    OUString GetText() const;

    virtual FactoryFunction GetUITestFactory() const override;

    void SetModifyHdl(const Link<LinkParamNone*, void>& rLink);

    virtual EditView* GetEditView() const;
    virtual EditEngine* GetEditEngine() const;

    bool HasSelection() const;

    void Cut();
    void Copy();
    void Paste();

    virtual ~WeldEditView() override;

protected:
    std::unique_ptr<EditEngine> m_xEditEngine;
    std::unique_ptr<EditView> m_xEditView;
    rtl::Reference<WeldEditAccessible> m_xAccessible;

    virtual void makeEditEngine();

    void InitAccessible();

    void DoPaint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect);

    virtual void Paint(vcl::RenderContext& rRenderContext, const tools::Rectangle& rRect) override;
    virtual bool MouseMove(const MouseEvent& rMEvt) override;
    virtual bool MouseButtonDown(const MouseEvent& rMEvt) override;
    virtual bool MouseButtonUp(const MouseEvent& rMEvt) override;
    virtual bool KeyInput(const KeyEvent& rKEvt) override;
    virtual bool Command(const CommandEvent& rCEvt) override;
    virtual bool CanFocus() const;
    virtual void GetFocus() override;
    virtual void LoseFocus() override;
    virtual void Resize() override;

    virtual css::uno::Reference<css::accessibility::XAccessible> CreateAccessible() override;

    virtual void EditViewInvalidate(const tools::Rectangle& rRect) override { Invalidate(rRect); }

    virtual void EditViewSelectionChange() override { Invalidate(); }

    virtual OutputDevice& EditViewOutputDevice() const override
    {
        return GetDrawingArea()->get_ref_device();
    }

    virtual void EditViewInputContext(const InputContext& rInputContext) override
    {
        SetInputContext(rInputContext);
    }

    virtual void EditViewCursorRect(const tools::Rectangle& rRect, int nExtTextInputWidth) override
    {
        OutputDevice& rRefDevice = EditViewOutputDevice();
        SetCursorRect(rRefDevice.LogicToPixel(rRect),
                      rRefDevice.LogicToPixel(Size(nExtTextInputWidth, 0)).Width());
    }

    virtual weld::Widget* EditViewPopupParent() const override { return GetDrawingArea(); }
};

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
