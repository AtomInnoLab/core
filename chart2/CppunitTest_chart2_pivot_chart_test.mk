# -*- Mode: makefile-gmake; tab-width: 4; indent-tabs-mode: t -*-
#*************************************************************************
#
# This file is part of the LibreOffice project.
#
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.
#
#*************************************************************************

$(eval $(call gb_CppunitTest_CppunitTest,chart2_pivot_chart_test))

$(eval $(call gb_CppunitTest_use_externals,chart2_pivot_chart_test, \
	boost_headers \
	libxml2 \
))

$(eval $(call gb_CppunitTest_add_exception_objects,chart2_pivot_chart_test, \
    chart2/qa/extras/PivotChartTest \
))

$(eval $(call gb_CppunitTest_use_libraries,chart2_pivot_chart_test, \
    basegfx \
    comphelper \
    cppu \
    cppuhelper \
    drawinglayer \
    editeng \
    for \
    forui \
    i18nlangtag \
    msfilter \
    vcl \
    oox \
    sal \
    salhelper \
    sax \
    sb \
    sc \
    sw \
    sd \
    sfx \
    sot \
    subsequenttest \
    svl \
    svt \
    svx \
    svxcore \
    test \
    tl \
    tk \
    ucbhelper \
    unotest \
    utl \
    $(call gb_Helper_optional,SCRIPTING, \
        vbahelper) \
    xo \
    sw \
))

$(eval $(call gb_CppunitTest_set_include,chart2_pivot_chart_test,\
    -I$(SRCDIR)/chart2/inc \
    $$(INCLUDE) \
))

$(eval $(call gb_CppunitTest_use_sdk_api,chart2_pivot_chart_test))
$(eval $(call gb_CppunitTest_use_ure,chart2_pivot_chart_test))
$(eval $(call gb_CppunitTest_use_vcl,chart2_pivot_chart_test))

$(eval $(call gb_CppunitTest_use_components,chart2_pivot_chart_test,\
    basic/util/sb \
    animations/source/animcore/animcore \
    chart2/source/chart2 \
    comphelper/util/comphelp \
    configmgr/source/configmgr \
    dbaccess/util/dba \
    embeddedobj/util/embobj \
    eventattacher/source/evtatt \
    filter/source/config/cache/filterconfig1 \
    filter/source/odfflatxml/odfflatxml \
    filter/source/storagefilterdetect/storagefd \
    filter/source/xmlfilteradaptor/xmlfa \
    filter/source/xmlfilterdetect/xmlfd \
    forms/util/frm \
    framework/util/fwk \
    i18npool/util/i18npool \
    linguistic/source/lng \
    oox/util/oox \
    package/source/xstor/xstor \
    package/util/package2 \
    sax/source/expatwrap/expwrap \
    sc/util/sc \
    sc/util/scd \
    sc/util/scfilt \
    sw/util/sw \
    sw/util/swd \
    sw/util/msword \
    sd/util/sd \
    sd/util/sdd \
    $(call gb_Helper_optional,SCRIPTING, \
	    sc/util/vbaobj) \
    scaddins/source/analysis/analysis \
    scaddins/source/datefunc/date \
    scripting/source/basprov/basprov \
    scripting/util/scriptframe \
    sfx2/util/sfx \
    sot/util/sot \
    svl/source/fsstor/fsstorage \
    svl/util/svl \
	svtools/util/svt \
    svx/util/svx \
    svx/util/svxcore \
    sw/util/sw_writerfilter \
    toolkit/util/tk \
    vcl/vcl.common \
    ucb/source/core/ucb1 \
    ucb/source/ucp/file/ucpfile1 \
    ucb/source/ucp/tdoc/ucptdoc1 \
    unotools/util/utl \
    unoxml/source/rdf/unordf \
    unoxml/source/service/unoxml \
    uui/util/uui \
    xmloff/util/xo \
    xmlscript/util/xmlscript \
))

$(eval $(call gb_CppunitTest_use_configuration,chart2_pivot_chart_test))

$(eval $(call gb_CppunitTest_add_arguments,chart2_pivot_chart_test, \
    -env:arg-env=$(gb_Helper_LIBRARY_PATH_VAR)"$$$${$(gb_Helper_LIBRARY_PATH_VAR)+=$$$$$(gb_Helper_LIBRARY_PATH_VAR)}" \
))

# vim: set noet sw=4 ts=4:
