/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4; fill-column: 100 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_SFX2_INC_AUTOREDACTDIALOG_HXX
#define INCLUDED_SFX2_INC_AUTOREDACTDIALOG_HXX

#include <memory>
#include <sal/config.h>
#include <sfx2/basedlgs.hxx>
#include <sfx2/objsh.hxx>

enum RedactionTargetType
{
    REDACTION_TARGET_TEXT,
    REDACTION_TARGET_REGEX,
    REDACTION_TARGET_PREDEFINED,
    REDACTION_TARGET_UNKNOWN
};

/// Keeps information for a single redaction target
struct RedactionTarget
{
    OUString sName;
    RedactionTargetType sType;
    OUString sContent;
    bool bCaseSensitive;
    bool bWholeWords;
    sal_uInt32 nID;
};

/// Used to display the targets list
class TargetsTable
{
    std::unique_ptr<weld::TreeView> m_xControl;
    int GetRowByTargetName(std::u16string_view sName);

public:
    TargetsTable(std::unique_ptr<weld::TreeView> xControl);
    void InsertTarget(RedactionTarget* pTarget);
    RedactionTarget* GetTargetByName(std::u16string_view sName);
    OUString GetNameProposal() const;

    int get_selected_index() const { return m_xControl->get_selected_index(); }
    std::vector<int> get_selected_rows() const { return m_xControl->get_selected_rows(); }
    void clear() { m_xControl->clear(); }
    void remove(int nRow) { m_xControl->remove(nRow); }
    void select(int nRow) { m_xControl->select(nRow); }
    OUString get_id(int nRow) const { return m_xControl->get_id(nRow); }

    // Sync data on the targets box with the data on the target
    void setRowData(int nRowIndex, const RedactionTarget* pTarget);

    void connect_row_activated(const Link<weld::TreeView&, bool>& rLink)
    {
        m_xControl->connect_row_activated(rLink);
    };
};

namespace sfx2
{
class FileDialogHelper;
}

enum class StartFileDialogType
{
    Open,
    SaveAs
};

class SfxAutoRedactDialog final : public SfxDialogController
{
    SfxObjectShellLock m_xDocShell;
    std::vector<std::pair<std::unique_ptr<RedactionTarget>, OUString>> m_aTableTargets;
    std::unique_ptr<sfx2::FileDialogHelper> m_pFileDlg;
    bool m_bIsValidState;
    bool m_bTargetsCopied;

    TargetsTable m_aTargetsBox;
    std::unique_ptr<weld::Button> m_xLoadBtn;
    std::unique_ptr<weld::Button> m_xSaveBtn;
    std::unique_ptr<weld::Button> m_xAddBtn;
    std::unique_ptr<weld::Button> m_xEditBtn;
    std::unique_ptr<weld::Button> m_xDeleteBtn;

    DECL_LINK(Load, weld::Button&, void);
    DECL_LINK(Save, weld::Button&, void);
    DECL_LINK(AddHdl, weld::Button&, void);
    DECL_LINK(EditHdl, weld::Button&, void);
    DECL_LINK(DeleteHdl, weld::Button&, void);
    DECL_LINK(DoubleClickEditHdl, weld::TreeView&, bool);
    DECL_LINK(LoadHdl, sfx2::FileDialogHelper*, void);
    DECL_LINK(SaveHdl, sfx2::FileDialogHelper*, void);

    void StartFileDialog(StartFileDialogType nType, const OUString& rTitle);
    /// Carry out proper addition both to the targets box, and to the tabletargets vector.
    void addTarget(std::unique_ptr<RedactionTarget> pTarget);
    /// Clear all targets both visually and from the targets vector
    void clearTargets();

public:
    SfxAutoRedactDialog(weld::Window* pParent);
    virtual ~SfxAutoRedactDialog() override;

    /// Check if the dialog has any valid redaction targets.
    bool hasTargets() const;
    /// Check if the dialog is in a valid state.
    bool isValidState() const { return m_bIsValidState; }
    /** Copies targets vector
     *  Does a shallow copy.
     *  Returns true if successful.
     */
    bool getTargets(std::vector<std::pair<RedactionTarget, OUString>>& r_aTargets);
};

class SfxAddTargetDialog final : public weld::GenericDialogController
{
private:
    std::unique_ptr<weld::Entry> m_xName;
    std::unique_ptr<weld::ComboBox> m_xType;
    std::unique_ptr<weld::Label> m_xLabelContent;
    std::unique_ptr<weld::Entry> m_xContent;
    std::unique_ptr<weld::Label> m_xLabelPredefContent;
    std::unique_ptr<weld::ComboBox> m_xPredefContent;
    std::unique_ptr<weld::CheckButton> m_xCaseSensitive;
    std::unique_ptr<weld::CheckButton> m_xWholeWords;

    DECL_LINK(SelectTypeHdl, weld::ComboBox&, void);

public:
    SfxAddTargetDialog(weld::Window* pWindow, const OUString& rName);
    SfxAddTargetDialog(weld::Window* pWindow, const OUString& sName,
                       const RedactionTargetType& eTargetType, const OUString& sContent,
                       bool bCaseSensitive, bool bWholeWords);

    OUString getName() const { return m_xName->get_text(); }
    RedactionTargetType getType() const;
    OUString getContent() const;
    bool isCaseSensitive() const
    {
        return m_xCaseSensitive->get_state() == TriState::TRISTATE_TRUE;
    }
    bool isWholeWords() const { return m_xWholeWords->get_state() == TriState::TRISTATE_TRUE; }
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab cinoptions=b1,g0,N-s cinkeys+=0=break: */
