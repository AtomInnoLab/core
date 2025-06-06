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
#ifndef INCLUDED_SVL_HINT_HXX
#define INCLUDED_SVL_HINT_HXX

#include <ostream>
#include <string>

#include <svl/svldllapi.h>

/// hint ids, mostly used to avoid dynamic_cast of SfxHint
enum class SfxHintId {
    NONE,
    Dying,
    NameChanged,
    TitleChanged,
    DataChanged,
    DocChanged,
    UpdateDone,
    Deinitializing,
    ModeChanged,
    ColorsChanged,
    ThemeColorsChanged,
    LanguageChanged,
    RedlineChanged,
    DocumentRepair,
    SvxViewChanged,
    PoolItem,
    SfxStyleSheetPool,
    INetURLHistory,
    SfxOpenUrl,

// svx navigator tree
    FmNavViewMarksChanged,
    FmNavRequestSelect,
    FmNavInserted,
    FmNavModelReplaced,
    FmNavRemoved,
    FmNavNameChanged,
    FmNavCleared,

// svx filter navigator
    FmFilterInserted,
    FilterClearing,
    FmFilterRemoved,
    FmFilterTextChanged,
    FmFilterCurrentChanged,

// VCL text hints
    TextParaInserted,
    TextParaRemoved,
    TextParaContentChanged,
    TextHeightChanged,
    TextFormatPara,
    TextFormatted,
    TextModified,
    TextProcessNotifications,
    TextViewScrolled,
    TextViewSelectionChanged,
    TextViewCaretChanged,

// BASIC hints
    BasicDataWanted,
    BasicDataChanged,
    BasicInfoWanted,
    BasicStart,
    BasicStop,

// basctl
    BasCtlDlgEd,

    ScriptDocumentChanged,

// reportdesign
    ReportDesignDlgEd,

// SVX
    FmDesignModeChanged,
// SVX edit source
    EditSourceParasMoved,
    EditSourceSelectionChanged,

// SC hints
    ScDataChanged,
    ScTableOpDirty,
    ScCalcAll,
    ScReference,
    ScDrawLayerNew,
    ScDbAreasChanged,
    ScAreaChanged,
    ScAreasChanged,
    ScTablesChanged,
    ScDrawChanged,
    ScDocNameChanged,
    ScAreaLinksChanged,
    ScShowRangeFinder,
    ScDocSaved,
    ScForceSetTab,
    ScNavigatorUpdateAll,
    ScAnyDataChanged,
    ScPrintOptions,
    ScRefModeChanged,
    ScKillEditView,
    ScKillEditViewNoPaint,
    ScHiddenRowsChanged,
    ScSelectionChanged,
    ScClearCache,
    ScTabDeleted,
    ScTabSizeChanged,
    ScPaint,
    ScUpdateRef,
    ScLinkRefreshed,
    ScAutoStyle,
    ScDBRangeRefreshed,
    ScDataPilotModified,
    ScTables,
    ScEditView,
    ScUnoRefUndo,
    ScBulkData,

// SC accessibility hints
    ScAccTableChanged,
    ScAccCursorChanged,
    ScAccVisAreaChanged,
    ScAccEnterEditMode,
    ScAccLeaveEditMode,
    ScAccMakeDrawLayer,
    ScAccWindowResized,
    ScAccGridWinFocusLost,
    ScAccGridWinFocusGot,
    ScAccWinFocusGot,
    ScAccWinFocusLost,

// sd hints
    SdViewShell,

// SFX stylesheet
    StyleSheetModified,  // changed (used by the SfxStyleSheetHint class)
    StyleSheetModifiedExtended,  // changed (used by the SfxStyleSheetModifiedHint class)
    StyleSheetChanged,  // erased and re-created (replaced)
    StyleSheetErased,  // erased
    StyleSheetInDestruction,  // in the process of being destructed
    StylesSpotlightModified,  // what styles to spotlight in a document changed

// STARMATH
    MathFormatChanged,
    SmNewUserFormula,

// Sw
    SwDrawViewsCreated,
    SwSplitNodeOperation,
    SwSectionFrameMoveAndDelete,
    SwNavigatorUpdateTracking,
    SwNavigatorSelectOutlinesWithSelections,
    SwPreGraphicArrived,
    SwPostGraphicArrived,
    SwGraphicPieceArrived,
    SwLinkedGraphicStreamArrived,
    SwLegacyModify,
    SwCollectTextMarks,
    SwCollectTextTOXMarksForLayout,
    SwDrawFrameFormat,
    SwCheckDrawFrameFormatLayer,
    SwContactChanged,
    SwDrawFormatLayoutCopy,
    SwRestoreFlyAnchor,
    SwCreatePortion,
    SwCollectTextObjects,
    SwGetZOrder,
    SwGetObjectConnected,
    SwFindSdrObject,
    SwWW8AnchorConv,
    SwField,
    SwFindFormatForField,
    SwFindFormatForPostItId,
    SwCollectPostIts,
    SwHasHiddenInformationNotes,
    SwGatherNodeIndex,
    SwGatherRefFields,
    SwGatherFields,
    SwNameChanged, // this can possibly be replaced by the generic NameChanged above
    SwInsertText,
    SwDeleteText,
    SwDeleteChar,
    SwSectionHidden,
    SwTitleChanged,
    SwDescriptionChanged,
    SwDocPosUpdate,
    SwDocPosUpdateAtIndex,
    SwTableHeadingChange,
    SwVirtPageNumHint,
    SwAutoFormatUsedHint,
    SwFormatField,
    SwFindRedline,
    SwModifyChanged,
    SwAttr,
    SwDocumentDying,
    SwRedlineDelText,
    SwRedlineUnDelText,
    SwMoveText,
    SwRedlineContentAtPos,
    SwRedlineShowChanged,
    SwTableBoxFormatChanged,
    SwFindContentFrame,
    SwTableLineFormatChanged,
    SwMoveTableBox,
    SwMoveTableLine,
    SwCondCollCondChg,
    SwGatherDdeTables,
    SwUnoCursorHint,
    SwPageDesc,
    SwPageFootnote,
    SwLinkAnchorSearch,
    SwInRangeSearch,
    SwGrfRereadAndInCache,
    SwFindUnoTextTableRowInstance,
    SwFindUnoCellInstance,
    SwRemoveUnoObject,
    SwHiddenParaPrint,
    SwFormatChange,
    SwAttrSetChange,
    SwObjectDying,
    SwUpdateAttr,

    ThisIsAnSdrHint,
    ThisIsAnSfxEventHint
};

template< typename charT, typename traits >
inline std::basic_ostream<charT, traits> & operator <<(
    std::basic_ostream<charT, traits> & stream, const SfxHintId& id )
{
    switch(id)
    {
    case SfxHintId::NONE: return stream << "NONE";
    case SfxHintId::Dying: return stream << "Dying";
    case SfxHintId::NameChanged: return stream << "NameChanged";
    case SfxHintId::TitleChanged: return stream << "TitleChanged";
    case SfxHintId::DataChanged: return stream << "DataChanged";
    case SfxHintId::DocChanged: return stream << "DocChanged";
    case SfxHintId::UpdateDone: return stream << "UpdateDone";
    case SfxHintId::Deinitializing: return stream << "Deinitializing";
    case SfxHintId::ModeChanged: return stream << "ModeChanged";
    case SfxHintId::ColorsChanged: return stream << "ColorsChanged";
    case SfxHintId::ThemeColorsChanged: return stream << "ThemeColorsChanged";
    case SfxHintId::LanguageChanged: return stream << "LanguageChanged";
    case SfxHintId::RedlineChanged: return stream << "RedlineChanged";
    case SfxHintId::DocumentRepair: return stream << "DocumentRepair";
    case SfxHintId::TextParaInserted: return stream << "TextParaInserted";
    case SfxHintId::TextParaRemoved: return stream << "TextParaRemoved";
    case SfxHintId::TextParaContentChanged: return stream << "TextParaContentChanged";
    case SfxHintId::TextHeightChanged: return stream << "TextHeightChanged";
    case SfxHintId::TextFormatPara: return stream << "TextFormatPara";
    case SfxHintId::TextFormatted: return stream << "TextFormatted";
    case SfxHintId::TextModified: return stream << "TextModified";
    case SfxHintId::TextProcessNotifications: return stream << "TextProcessNotifications";
    case SfxHintId::TextViewScrolled: return stream << "TextViewScrolled";
    case SfxHintId::TextViewSelectionChanged: return stream << "TextViewSelectionChanged";
    case SfxHintId::TextViewCaretChanged: return stream << "TextViewCaretChanged";
    case SfxHintId::BasicDataWanted: return stream << "BasicDataWanted";
    case SfxHintId::BasicDataChanged: return stream << "BasicDataChanged";
    case SfxHintId::BasicInfoWanted: return stream << "BasicInfoWanted";
    case SfxHintId::BasicStart: return stream << "BasicStart";
    case SfxHintId::BasicStop: return stream << "BasicStop";
    case SfxHintId::ScriptDocumentChanged: return stream << "ScriptDocumentChanged";
    case SfxHintId::FmDesignModeChanged: return stream << "FmDesignModeChanged";
    case SfxHintId::EditSourceParasMoved: return stream << "EditSourceParasMoved";
    case SfxHintId::EditSourceSelectionChanged: return stream << "EditSourceSelectionChanged";
    case SfxHintId::ScDataChanged: return stream << "ScDataChanged";
    case SfxHintId::ScTableOpDirty: return stream << "ScTableOpDirty";
    case SfxHintId::ScCalcAll: return stream << "ScCalcAll";
    case SfxHintId::ScReference: return stream << "ScReference";
    case SfxHintId::ScDrawLayerNew: return stream << "ScDrawLayerNew";
    case SfxHintId::ScDbAreasChanged: return stream << "ScDbAreasChanged";
    case SfxHintId::ScAreaChanged: return stream << "ScAreaChanged";
    case SfxHintId::ScAreasChanged: return stream << "ScAreasChanged";
    case SfxHintId::ScTablesChanged: return stream << "ScTablesChanged";
    case SfxHintId::ScDrawChanged: return stream << "ScDrawChanged";
    case SfxHintId::ScDocNameChanged: return stream << "ScDocNameChanged";
    case SfxHintId::ScAreaLinksChanged: return stream << "ScAreaLinksChanged";
    case SfxHintId::ScShowRangeFinder: return stream << "ScShowRangeFinder";
    case SfxHintId::ScDocSaved: return stream << "ScDocSaved";
    case SfxHintId::ScForceSetTab: return stream << "ScForceSetTab";
    case SfxHintId::ScNavigatorUpdateAll: return stream << "ScNavigatorUpdateAll";
    case SfxHintId::ScAnyDataChanged: return stream << "ScAnyDataChanged";
    case SfxHintId::ScPrintOptions: return stream << "ScPrintOptions";
    case SfxHintId::ScRefModeChanged: return stream << "ScRefModeChanged";
    case SfxHintId::ScKillEditView: return stream << "ScKillEditView";
    case SfxHintId::ScKillEditViewNoPaint: return stream << "ScKillEditViewNoPaint";
    case SfxHintId::ScHiddenRowsChanged: return stream << "ScHiddenRowsChanged";
    case SfxHintId::ScSelectionChanged: return stream << "ScSelectionChanged";
    case SfxHintId::ScClearCache: return stream << "ScClearCache";
    case SfxHintId::ScAccTableChanged: return stream << "ScAccTableChanged";
    case SfxHintId::ScAccCursorChanged: return stream << "ScAccCursorChanged";
    case SfxHintId::ScAccVisAreaChanged: return stream << "ScAccVisAreaChanged";
    case SfxHintId::ScAccEnterEditMode: return stream << "ScAccEnterEditMode";
    case SfxHintId::ScAccLeaveEditMode: return stream << "ScAccLeaveEditMode";
    case SfxHintId::ScAccMakeDrawLayer: return stream << "ScAccMakeDrawLayer";
    case SfxHintId::ScAccWindowResized: return stream << "ScAccWindowResized";
    case SfxHintId::StyleSheetModified: return stream << "StyleSheetModified";
    case SfxHintId::StyleSheetModifiedExtended: return stream << "StyleSheetModifiedExtended";
    case SfxHintId::StyleSheetChanged: return stream << "StyleSheetChanged";
    case SfxHintId::StyleSheetErased: return stream << "StyleSheetErased";
    case SfxHintId::StyleSheetInDestruction: return stream << "StyleSheetInDestruction";
    case SfxHintId::MathFormatChanged: return stream << "MathFormatChanged";
    case SfxHintId::SwDrawViewsCreated: return stream << "SwDrawViewsCreated";
    case SfxHintId::SwSplitNodeOperation: return stream << "SwSplitNodeOperation";
    case SfxHintId::SwSectionFrameMoveAndDelete: return stream << "SwSectionFrameMoveAndDelete";
    case SfxHintId::SwNavigatorUpdateTracking: return stream << "SwNavigatorUpdateTracking";
    case SfxHintId::SwNavigatorSelectOutlinesWithSelections:
        return stream << "SwNavigatorSelectOutlinesWithSelections";
    case SfxHintId::SwCollectTextMarks: return stream << "SwCollectTextMarks";
    case SfxHintId::SwCollectTextTOXMarksForLayout: return stream << "SwCollectTextTOXMarksForLayout";
    case SfxHintId::SwFormatField: return stream << "SwFormatField";
    case SfxHintId::SwFindRedline: return stream << "SwFindRedline";
    case SfxHintId::SwModifyChanged: return stream << "SwModifyChanged";
    case SfxHintId::SwAttr: return stream << "SwAttr";
    case SfxHintId::SwDocumentDying: return stream << "SwDocumentDying";
    case SfxHintId::SwRedlineDelText: return stream << "SwRedlineDelText";
    case SfxHintId::SwRedlineUnDelText: return stream << "SwRedlineUnDelText";
    case SfxHintId::SwMoveText: return stream << "SwMoveText";
    case SfxHintId::ThisIsAnSdrHint: return stream << "SdrHint";
    default: return stream << "unk(" << std::to_string(int(id)) << ")";
    }
}

class SVL_DLLPUBLIC SfxHint
{
private:
    SfxHintId mnId;
public:
    SfxHint() : mnId(SfxHintId::NONE) {}
    explicit SfxHint( SfxHintId nId ) : mnId(nId) {}
    virtual ~SfxHint() {};

    SfxHint(SfxHint const &) = default;
    SfxHint(SfxHint &&) = default;
    SfxHint & operator =(SfxHint const &) = default;
    SfxHint & operator =(SfxHint &&) = default;

    SfxHintId GetId() const { return mnId; }
};

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
