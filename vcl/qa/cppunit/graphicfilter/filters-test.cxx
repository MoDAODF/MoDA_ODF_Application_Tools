/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <sal/config.h>

#include <cstdlib>

#include <unotest/filters-test.hxx>
#include <test/bootstrapfixture.hxx>

#include <comphelper/fileformat.h>

#include <vcl/graphicfilter.hxx>
#include <tools/stream.hxx>
#include <com/sun/star/beans/PropertyValue.hpp>

using namespace ::com::sun::star;

/* Implementation of Filters test */

class VclFiltersTest :
    public test::FiltersTest,
    public test::BootstrapFixture
{
    std::unique_ptr<GraphicFilter> mpGraphicFilter;
public:
    VclFiltersTest() :
        BootstrapFixture(true, false),
        mpGraphicFilter(new GraphicFilter(false))
    {}

    virtual bool load(const OUString &,
        const OUString &rURL, const OUString &,
        SfxFilterFlags, SotClipboardFormatId, unsigned int) override;

    void checkExportImport(std::u16string_view aFilterShortName);

    /**
     * Ensure CVEs remain unbroken
     */
    void testCVEs();

    void testScaling();
    void testExportImport();

    CPPUNIT_TEST_SUITE(VclFiltersTest);
    CPPUNIT_TEST(testCVEs);
    CPPUNIT_TEST(testScaling);
    CPPUNIT_TEST(testExportImport);
    CPPUNIT_TEST_SUITE_END();
};

bool VclFiltersTest::load(const OUString &,
    const OUString &rURL, const OUString &,
    SfxFilterFlags, SotClipboardFormatId, unsigned int)
{
    SvFileStream aFileStream(rURL, StreamMode::READ);
    Graphic aGraphic;
    bool bRetval(ERRCODE_NONE == mpGraphicFilter->ImportGraphic(aGraphic, rURL, aFileStream));

    if (!bRetval)
    {
        // if error occurred, we are done
        return bRetval;
    }

    // if not and we have an embedded Vector Graphic Data, trigger it's interpretation
    // to check for error. Graphic with VectorGraphicData (Svg/Emf/Wmf) load without error
    // as long as one of the three types gets detected. Thus, cycles like load/save in
    // other format will work (what may be wanted). For the test framework it was indirectly
    // intended to trigger an error when load in the sense of deep data interpretation fails,
    // so we need to trigger this here
    if (aGraphic.getVectorGraphicData())
    {
        if (aGraphic.getVectorGraphicData()->getRange().isEmpty())
        {
            // invalid file or file with no content
            return false;
        }
    }

    return true;
}

void VclFiltersTest::testScaling()
{
    for (BmpScaleFlag i = BmpScaleFlag::Default; i <= BmpScaleFlag::BiLinear; i = static_cast<BmpScaleFlag>(static_cast<int>(i) + 1))
    {
        Bitmap aBitmap( Size( 413, 409 ), 24 );
        BitmapEx aBitmapEx( aBitmap );

        fprintf( stderr, "scale with type %d\n", int( i ) );
        CPPUNIT_ASSERT( aBitmapEx.Scale( 0.1937046, 0.193154, i ) );
        Size aAfter( aBitmapEx.GetSizePixel() );
        fprintf( stderr, "size %" SAL_PRIdINT64 ", %" SAL_PRIdINT64 "\n", sal_Int64(aAfter.Width()), sal_Int64(aAfter.Height()) );
        CPPUNIT_ASSERT( std::abs (aAfter.Height() - aAfter.Width()) <= 1 );
    }
}

void VclFiltersTest::checkExportImport(std::u16string_view aFilterShortName)
{
    Bitmap aBitmap( Size( 100, 100 ), 24 );
    aBitmap.Erase(COL_WHITE);

    SvMemoryStream aStream;
    aStream.SetVersion( SOFFICE_FILEFORMAT_CURRENT );

    css::uno::Sequence< css::beans::PropertyValue > aFilterData( 3 );
    aFilterData[ 0 ].Name = "Interlaced";
    aFilterData[ 0 ].Value <<= sal_Int32(0);
    aFilterData[ 1 ].Name = "Compression";
    aFilterData[ 1 ].Value <<= sal_Int32(1);
    aFilterData[ 2 ].Name = "Quality";
    aFilterData[ 2 ].Value <<= sal_Int32(90);

    sal_uInt16 aFilterType = mpGraphicFilter->GetExportFormatNumberForShortName(aFilterShortName);
    mpGraphicFilter->ExportGraphic(BitmapEx(aBitmap), OUString(), aStream, aFilterType, &aFilterData );

    CPPUNIT_ASSERT(aStream.Tell() > 0);

    aStream.Seek( STREAM_SEEK_TO_BEGIN );

    Graphic aLoadedGraphic;
    mpGraphicFilter->ImportGraphic( aLoadedGraphic, OUString(), aStream );

    BitmapEx aLoadedBitmapEx = aLoadedGraphic.GetBitmapEx();
    Size aSize = aLoadedBitmapEx.GetSizePixel();

    CPPUNIT_ASSERT_EQUAL(tools::Long(100), aSize.Width());
    CPPUNIT_ASSERT_EQUAL(tools::Long(100), aSize.Height());
}

void VclFiltersTest::testExportImport()
{
    fprintf(stderr, "Check ExportImport JPG\n");
    checkExportImport(u"jpg");
    fprintf(stderr, "Check ExportImport PNG\n");
    checkExportImport(u"png");
    fprintf(stderr, "Check ExportImport BMP\n");
    checkExportImport(u"bmp");
    fprintf(stderr, "Check ExportImport WEBP\n");
    checkExportImport(u"webp");
}

void VclFiltersTest::testCVEs()
{
#ifndef DISABLE_CVE_TESTS
    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/wmf/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/emf/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/png/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/jpg/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/gif/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/bmp/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/xbm/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/xpm/"));

    testDir(OUString(),
        m_directories.getURLFromSrc("/vcl/qa/cppunit/graphicfilter/data/svm/"));
#endif
}

CPPUNIT_TEST_SUITE_REGISTRATION(VclFiltersTest);

CPPUNIT_PLUGIN_IMPLEMENT();

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
