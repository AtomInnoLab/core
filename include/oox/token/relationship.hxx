/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#ifndef INCLUDED_OOX_TOKEN_RELATIONSHIP_HXX
#define INCLUDED_OOX_TOKEN_RELATIONSHIP_HXX

#include <oox/dllapi.h>

#include <rtl/ustring.hxx>

namespace oox
{
enum class Relationship
{
    ACTIVEXCONTROLBINARY,
    CHART,
    CHARTCOLORSTYLE, // for chartex
    CHARTEX,
    CHARTSTYLE, // for chartex
    CHARTUSERSHAPES,
    COMMENTS,
    COMMENTAUTHORS,
    COMMENTSEXTENDED,
    CONNECTIONS,
    CONTROL,
    CTRLPROP,
    CUSTOMXML,
    CUSTOMXMLPROPS,
    DIAGRAMCOLORS,
    DIAGRAMDATA,
    DIAGRAMDRAWING,
    DIAGRAMLAYOUT,
    DIAGRAMQUICKSTYLE,
    DRAWING,
    ENDNOTES,
    EXTERNALLINKPATH,
    FONT,
    FONTTABLE,
    FOOTER,
    FOOTNOTES,
    GLOSSARYDOCUMENT,
    HDPHOTO,
    HEADER,
    HYPERLINK,
    IMAGE,
    MEDIA,
    NOTESMASTER,
    NOTESSLIDE,
    NUMBERING,
    OFFICEDOCUMENT,
    OLEOBJECT,
    PACKAGE,
    PRESPROPS,
    SETTINGS,
    SHAREDSTRINGS,
    SLIDE,
    SLIDELAYOUT,
    SLIDEMASTER,
    STYLES,
    THEME,
    VBAPROJECT,
    VIDEO,
    AUDIO,
    VMLDRAWING,
    WORDVBADATA,
    WORKSHEET,
    NUM_ENTRIES // last, unused
};

OUString OOX_DLLPUBLIC getRelationship(Relationship eRelationship);
}

#endif // INCLUDED_OOX_TOKEN_RELATIONSHIP_HXX

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
