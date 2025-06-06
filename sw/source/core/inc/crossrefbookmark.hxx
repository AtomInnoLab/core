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

#ifndef INCLUDED_SW_SOURCE_CORE_INC_CROSSREFBOOKMARK_HXX
#define INCLUDED_SW_SOURCE_CORE_INC_CROSSREFBOOKMARK_HXX

#include <sal/config.h>

#include <string_view>

#include "bookmark.hxx"
#include <rtl/ustring.hxx>
#include <vcl/keycod.hxx>

namespace sw::mark {
        class CrossRefBookmark
            : public Bookmark
        {
        public:
            CrossRefBookmark(const SwPaM& rPaM,
                const vcl::KeyCode& rCode,
                const SwMarkName& rName,
                std::u16string_view rPrefix);

            // getters
            virtual SwPosition& GetOtherMarkPos() const override;
            virtual SwPosition& GetMarkStart() const override
                { return const_cast<SwPosition&>(*m_oPos1); }
            virtual SwPosition& GetMarkEnd() const override
                { return const_cast<SwPosition&>(*m_oPos1); }
            virtual std::pair<SwPosition&,SwPosition&> GetMarkStartEnd() const override
                { return { const_cast<SwPosition&>(*m_oPos1), const_cast<SwPosition&>(*m_oPos1) }; }
            virtual bool IsExpanded() const override
                { return false; }

            virtual void SetMarkPos(const SwPosition& rNewPos) override;
            virtual void SetOtherMarkPos(const SwPosition&) override
            {
                assert(false &&
                    "<CrossRefBookmark::SetOtherMarkPos(..)>"
                    " - misusage of CrossRefBookmark: other bookmark position isn't allowed to be set." );
            }
            virtual void ClearOtherMarkPos() override
            {
                assert(false &&
                    "<SwCrossRefBookmark::ClearOtherMarkPos(..)>"
                    " - misusage of CrossRefBookmark: other bookmark position isn't allowed to be set or cleared." );
            }
        };

        class CrossRefHeadingBookmark final
            : public CrossRefBookmark
        {
        public:
            CrossRefHeadingBookmark(const SwPaM& rPaM,
                const vcl::KeyCode& rCode,
                const SwMarkName& rName);
            static bool IsLegalName(const SwMarkName& rName);
        };

        class CrossRefNumItemBookmark final
            : public CrossRefBookmark
        {
        public:
            CrossRefNumItemBookmark(const SwPaM& rPaM,
                const vcl::KeyCode& rCode,
                const SwMarkName& rName);
            static bool IsLegalName(const SwMarkName& rName);
        };
}

#endif

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
