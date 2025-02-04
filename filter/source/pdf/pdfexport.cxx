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

#include <iostream>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>

#include <osl/file.hxx>
#include <tools/debug.hxx>
#include <tools/urlobj.hxx>
#include <tools/poly.hxx>
#include <tools/diagnose_ex.h>
#include <unotools/resmgr.hxx>
#include <vcl/canvastools.hxx>
#include <vcl/mapmod.hxx>
#include <vcl/gdimtf.hxx>
#include <comphelper/sequence.hxx>
#include <comphelper/string.hxx>
#include <comphelper/storagehelper.hxx>
#include <basegfx/polygon/b2dpolygon.hxx>
#include <basegfx/polygon/b2dpolypolygon.hxx>
#include <basegfx/polygon/b2dpolygontools.hxx>
#include <toolkit/awt/vclxdevice.hxx>
#include <unotools/configmgr.hxx>
#include <cppuhelper/compbase.hxx>
#include <cppuhelper/basemutex.hxx>

#include "pdfexport.hxx"
#include <strings.hrc>

#include <com/sun/star/beans/XPropertySet.hpp>
#include <com/sun/star/configuration/theDefaultProvider.hpp>
#include <com/sun/star/awt/XDevice.hpp>
#include <com/sun/star/frame/XModel.hpp>
#include <com/sun/star/frame/ModuleManager.hpp>
#include <com/sun/star/frame/XStorable.hpp>
#include <com/sun/star/document/XDocumentProperties.hpp>
#include <com/sun/star/document/XDocumentPropertiesSupplier.hpp>
#include <com/sun/star/container/XNameAccess.hpp>
#include <com/sun/star/view/XViewSettingsSupplier.hpp>
#include <com/sun/star/task/XInteractionRequest.hpp>
#include <com/sun/star/task/PDFExportException.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/io/XOutputStream.hpp>
#include <com/sun/star/lang/XServiceInfo.hpp>
#include <com/sun/star/drawing/XShapes.hpp>
#include <com/sun/star/security/XCertificate.hpp>
#include <com/sun/star/beans/XMaterialHolder.hpp>

#include <memory>

using namespace ::com::sun::star;
using namespace ::com::sun::star::io;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::lang;
using namespace ::com::sun::star::beans;
using namespace ::com::sun::star::view;
using namespace ::com::sun::star::graphic;


PDFExport::PDFExport( const Reference< XComponent >& rxSrcDoc,
                      const Reference< task::XStatusIndicator >& rxStatusIndicator,
                      const Reference< task::XInteractionHandler >& rxIH,
                      const Reference< XComponentContext >& xContext ) :
    mxSrcDoc                    ( rxSrcDoc ),
    mxContext                   ( xContext ),
    mxStatusIndicator           ( rxStatusIndicator ),
    mxIH                        ( rxIH ),
    mbUseTaggedPDF              ( false ),
    mnPDFTypeSelection          ( 0 ),
    mbPDFUACompliance           ( false),
    mbExportNotes               ( true ),
    mbExportPlaceholders        ( false ),
    mbUseReferenceXObject       ( false ),
    mbExportNotesPages          ( false ),
    mbExportOnlyNotesPages      ( false ),
    mbUseTransitionEffects      ( true ),
    mbExportBookmarks           ( true ),
    mbExportHiddenSlides        ( false ),
    mbSinglePageSheets          ( false ),
    mnOpenBookmarkLevels        ( -1 ),
    mbUseLosslessCompression    ( false ),
    mbReduceImageResolution     ( true ),
    mbSkipEmptyPages            ( true ),
    mbAddStream                 ( false ),
    mnMaxImageResolution        ( 300 ),
    mnQuality                   ( 80 ),
    mnFormsFormat               ( 0 ),
    mbExportFormFields          ( true ),
    mbAllowDuplicateFieldNames  ( false ),
    mnProgressValue             ( 0 ),
    mbRemoveTransparencies      ( false ),

    mbIsRedactMode              ( false ),

    mbHideViewerToolbar         ( false ),
    mbHideViewerMenubar         ( false ),
    mbHideViewerWindowControls  ( false ),
    mbFitWindow                 ( false ),
    mbCenterWindow              ( false ),
    mbOpenInFullScreenMode      ( false ),
    mbDisplayPDFDocumentTitle   ( true ),
    mnPDFDocumentMode           ( 0 ),
    mnPDFDocumentAction         ( 0 ),
    mnZoom                      ( 100 ),
    mnInitialPage               ( 1 ),
    mnPDFPageLayout             ( 0 ),

    mbEncrypt                   ( false ),
    mbRestrictPermissions       ( false ),
    mnPrintAllowed              ( 2 ),
    mnChangesAllowed            ( 4 ),
    mbCanCopyOrExtract          ( true ),
    mbCanExtractForAccessibility( true ),

    // #i56629
    mbExportRelativeFsysLinks       ( false ),
    mnDefaultLinkAction         ( 0 ),
    mbConvertOOoTargetToPDFTarget( false ),
    mbExportBmkToDest           ( false ),
    mbSignPDF                   ( false )
{
}


PDFExport::~PDFExport()
{
}


bool PDFExport::ExportSelection( vcl::PDFWriter& rPDFWriter,
    Reference< css::view::XRenderable > const & rRenderable,
    const Any& rSelection,
    const StringRangeEnumerator& rRangeEnum,
    Sequence< PropertyValue >& rRenderOptions,
    sal_Int32 nPageCount )
{
    bool        bRet = false;
    try
    {
        Any* pFirstPage = nullptr;
        Any* pLastPage = nullptr;

        bool bExportNotesPages = false;

        for( sal_Int32 nData = 0, nDataCount = rRenderOptions.getLength(); nData < nDataCount; ++nData )
        {
            if ( rRenderOptions[ nData ].Name == "IsFirstPage" )
                pFirstPage = &rRenderOptions[ nData ].Value;
            else if ( rRenderOptions[ nData ].Name == "IsLastPage" )
                pLastPage = &rRenderOptions[ nData ].Value;
            else if ( rRenderOptions[ nData ].Name == "ExportNotesPages" )
                rRenderOptions[ nData ].Value >>= bExportNotesPages;
        }

        OutputDevice* pOut = rPDFWriter.GetReferenceDevice();

        if( pOut )
        {
            if ( nPageCount )
            {
                vcl::PDFExtOutDevData& rPDFExtOutDevData = dynamic_cast<vcl::PDFExtOutDevData&>(*pOut->GetExtOutDevData());
                rPDFExtOutDevData.SetIsExportNotesPages( bExportNotesPages );

                sal_Int32 nCurrentPage(0);
                StringRangeEnumerator::Iterator aIter = rRangeEnum.begin();
                StringRangeEnumerator::Iterator aEnd  = rRangeEnum.end();
                while ( aIter != aEnd )
                {
                    const Sequence< PropertyValue > aRenderer( rRenderable->getRenderer( *aIter, rSelection, rRenderOptions ) );
                    awt::Size                   aPageSize;

                    for( const PropertyValue& rProp : aRenderer )
                    {
                        if ( rProp.Name == "PageSize" )
                        {
                            rProp.Value >>= aPageSize;
                            break;
                        }
                    }

                    rPDFExtOutDevData.SetCurrentPageNumber( nCurrentPage );

                    GDIMetaFile                 aMtf;
                    const MapMode               aMapMode( MapUnit::Map100thMM );
                    const Size                  aMtfSize( aPageSize.Width, aPageSize.Height );

                    pOut->Push();
                    pOut->EnableOutput( false );
                    pOut->SetMapMode( aMapMode );

                    aMtf.SetPrefSize( aMtfSize );
                    aMtf.SetPrefMapMode( aMapMode );
                    aMtf.Record( pOut );

                    // #i35176#
                    // IsLastPage property.
                    const sal_Int32 nCurrentRenderer = *aIter;
                    ++aIter;
                    if ( pLastPage && aIter == aEnd )
                        *pLastPage <<= true;

                    rRenderable->render( nCurrentRenderer, rSelection, rRenderOptions );

                    aMtf.Stop();
                    aMtf.WindStart();

                    if( aMtf.GetActionSize() &&
                             ( !mbSkipEmptyPages || aPageSize.Width || aPageSize.Height ) )
                    {
                        // We convert the whole metafile into a bitmap to get rid of the
                        // text covered by redaction shapes
                        if (mbIsRedactMode)
                        {
                            try
                            {
                                Graphic aGraph(aMtf);
                                // use antialiasing to improve how graphic objects look
                                BitmapEx bmp = aGraph.GetBitmapEx(GraphicConversionParameters(Size(0, 0), false, true, false));
                                Graphic bgraph(bmp);
                                aMtf = bgraph.GetGDIMetaFile();
                            }
                            catch(const Exception&)
                            {
                                TOOLS_WARN_EXCEPTION("filter.pdf", "Something went wrong while converting metafile to bitmap");
                            }
                        }

                        ImplExportPage(rPDFWriter, rPDFExtOutDevData, aMtf);
                        bRet = true;
                    }

                    pOut->Pop();

                    if ( mxStatusIndicator.is() )
                        mxStatusIndicator->setValue( mnProgressValue );
                    if ( pFirstPage )
                        *pFirstPage <<= false;

                    ++mnProgressValue;
                    ++nCurrentPage;
                }
            }
            else
            {
                bRet = true;                            // #i18334# nPageCount == 0,
                rPDFWriter.NewPage( 10000, 10000 );     // creating dummy page
                rPDFWriter.SetMapMode(MapMode(MapUnit::Map100thMM));
            }
        }
    }
    catch(const RuntimeException &)
    {
    }
    return bRet;
}

namespace {

class PDFExportStreamDoc : public vcl::PDFOutputStream
{
private:

    Reference< XComponent >             m_xSrcDoc;
    Sequence< beans::NamedValue >       m_aPreparedPassword;

public:

    PDFExportStreamDoc( const Reference< XComponent >& xDoc, const Sequence<beans::NamedValue>& rPwd )
    : m_xSrcDoc( xDoc ),
      m_aPreparedPassword( rPwd )
    {}

    virtual void write( const Reference< XOutputStream >& xStream ) override;
};

}

void PDFExportStreamDoc::write( const Reference< XOutputStream >& xStream )
{
    Reference< css::frame::XStorable > xStore( m_xSrcDoc, UNO_QUERY );
    if( !xStore.is() )
        return;

    Sequence< beans::PropertyValue > aArgs( 2 + (m_aPreparedPassword.hasElements() ? 1 : 0) );
    aArgs.getArray()[0].Name = "FilterName";
    aArgs.getArray()[1].Name = "OutputStream";
    aArgs.getArray()[1].Value <<= xStream;
    if( m_aPreparedPassword.hasElements() )
    {
        aArgs.getArray()[2].Name = "EncryptionData";
        aArgs.getArray()[2].Value <<= m_aPreparedPassword;
    }

    try
    {
        xStore->storeToURL( "private:stream", aArgs );
    }
    catch( const IOException& )
    {
    }
}


static OUString getMimetypeForDocument( const Reference< XComponentContext >& xContext,
                                        const Reference< XComponent >& xDoc ) throw()
{
    OUString aDocMimetype;
    try
    {
        // get document service name
        Reference< css::frame::XStorable > xStore( xDoc, UNO_QUERY );
        Reference< frame::XModuleManager2 > xModuleManager = frame::ModuleManager::create(xContext);
        if( xStore.is() )
        {
            OUString aDocServiceName = xModuleManager->identify( Reference< XInterface >( xStore, uno::UNO_QUERY ) );
            if ( !aDocServiceName.isEmpty() )
            {
                // get the actual filter name
                Reference< lang::XMultiServiceFactory > xConfigProvider =
                    configuration::theDefaultProvider::get( xContext );
                uno::Sequence< uno::Any > aArgs( 1 );
                beans::NamedValue aPathProp;
                aPathProp.Name = "nodepath";
                aPathProp.Value <<= OUString( "/org.openoffice.Setup/Office/Factories/" );
                aArgs[0] <<= aPathProp;

                Reference< container::XNameAccess > xSOFConfig(
                    xConfigProvider->createInstanceWithArguments(
                        "com.sun.star.configuration.ConfigurationAccess", aArgs ),
                    uno::UNO_QUERY );

                Reference< container::XNameAccess > xApplConfig;
                xSOFConfig->getByName( aDocServiceName ) >>= xApplConfig;
                if ( xApplConfig.is() )
                {
                    OUString aFilterName;
                    xApplConfig->getByName( "ooSetupFactoryActualFilter" ) >>= aFilterName;
                    if( !aFilterName.isEmpty() )
                    {
                        // find the related type name
                        OUString aTypeName;
                        Reference< container::XNameAccess > xFilterFactory(
                            xContext->getServiceManager()->createInstanceWithContext("com.sun.star.document.FilterFactory", xContext),
                            uno::UNO_QUERY );

                        Sequence< beans::PropertyValue > aFilterData;
                        xFilterFactory->getByName( aFilterName ) >>= aFilterData;
                        for ( const beans::PropertyValue& rProp : std::as_const(aFilterData) )
                            if ( rProp.Name == "Type" )
                                rProp.Value >>= aTypeName;

                        if ( !aTypeName.isEmpty() )
                        {
                            // find the mediatype
                            Reference< container::XNameAccess > xTypeDetection(
                                xContext->getServiceManager()->createInstanceWithContext("com.sun.star.document.TypeDetection", xContext),
                                UNO_QUERY );

                            Sequence< beans::PropertyValue > aTypeData;
                            xTypeDetection->getByName( aTypeName ) >>= aTypeData;
                            for ( const beans::PropertyValue& rProp : std::as_const(aTypeData) )
                                if ( rProp.Name == "MediaType" )
                                    rProp.Value >>= aDocMimetype;
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
    }
    return aDocMimetype;
}


bool PDFExport::Export( const OUString& rFile, const Sequence< PropertyValue >& rFilterData )
{
    INetURLObject   aURL( rFile );
    bool        bRet = false;

    std::set< vcl::PDFWriter::ErrorCode > aErrors;

    if( aURL.GetProtocol() != INetProtocol::File )
    {
        OUString aTmp;

        if( osl::FileBase::getFileURLFromSystemPath( rFile, aTmp ) == osl::FileBase::E_None )
            aURL = INetURLObject(aTmp);
    }

    if( aURL.GetProtocol() == INetProtocol::File )
    {
        Reference< XRenderable > xRenderable( mxSrcDoc, UNO_QUERY );

        if( xRenderable.is() )
        {
            rtl::Reference<VCLXDevice>  xDevice(new VCLXDevice);
            OUString                    aPageRange;
            Any                         aSelection;
            vcl::PDFWriter::PDFWriterContext aContext;
            OUString aOpenPassword, aPermissionPassword;
            Reference< beans::XMaterialHolder > xEnc;
            Sequence< beans::NamedValue > aPreparedPermissionPassword;


            // getting the string for the creator
            OUString aCreator;
            Reference< XServiceInfo > xInfo( mxSrcDoc, UNO_QUERY );
            if ( xInfo.is() )
            {
                if ( xInfo->supportsService( "com.sun.star.presentation.PresentationDocument" ) )
                    aCreator += "Impress";
                else if ( xInfo->supportsService( "com.sun.star.drawing.DrawingDocument" ) )
                    aCreator += "Draw";
                else if ( xInfo->supportsService( "com.sun.star.text.TextDocument" ) )
                    aCreator += "Writer";
                else if ( xInfo->supportsService( "com.sun.star.sheet.SpreadsheetDocument" ) )
                    aCreator += "Calc";
                else if ( xInfo->supportsService( "com.sun.star.formula.FormulaProperties"  ) )
                    aCreator += "Math";
            }

            Reference< document::XDocumentPropertiesSupplier > xDocumentPropsSupplier( mxSrcDoc, UNO_QUERY );
            if ( xDocumentPropsSupplier.is() )
            {
                Reference< document::XDocumentProperties > xDocumentProps( xDocumentPropsSupplier->getDocumentProperties() );
                if ( xDocumentProps.is() )
                {
                    aContext.DocumentInfo.Title = xDocumentProps->getTitle();
                    aContext.DocumentInfo.Author = xDocumentProps->getAuthor();
                    aContext.DocumentInfo.Subject = xDocumentProps->getSubject();
                    aContext.DocumentInfo.Keywords = ::comphelper::string::convertCommaSeparated(xDocumentProps->getKeywords());
                }
            }
            // getting the string for the producer
            aContext.DocumentInfo.Producer =
                utl::ConfigManager::getProductName() +
                " " +
                utl::ConfigManager::getProductVersion();
            aContext.DocumentInfo.Creator = aCreator;

            for ( const beans::PropertyValue& rProp : rFilterData )
            {
                if ( rProp.Name == "PageRange" )
                    rProp.Value >>= aPageRange;
                else if ( rProp.Name == "Selection" )
                    aSelection = rProp.Value;
                else if ( rProp.Name == "UseLosslessCompression" )
                    rProp.Value >>= mbUseLosslessCompression;
                else if ( rProp.Name == "Quality" )
                    rProp.Value >>= mnQuality;
                else if ( rProp.Name == "ReduceImageResolution" )
                    rProp.Value >>= mbReduceImageResolution;
                else if ( rProp.Name == "IsSkipEmptyPages" )
                    rProp.Value >>= mbSkipEmptyPages;
                else if ( rProp.Name == "MaxImageResolution" )
                    rProp.Value >>= mnMaxImageResolution;
                else if ( rProp.Name == "UseTaggedPDF" )
                    rProp.Value >>= mbUseTaggedPDF;
                else if ( rProp.Name == "SelectPdfVersion" )
                    rProp.Value >>= mnPDFTypeSelection;
                else if ( rProp.Name == "PDFUACompliance" )
                    rProp.Value >>= mbPDFUACompliance;
                else if ( rProp.Name == "ExportNotes" )
                    rProp.Value >>= mbExportNotes;
                else if ( rProp.Name == "ExportNotesPages" )
                    rProp.Value >>= mbExportNotesPages;
                else if ( rProp.Name == "ExportOnlyNotesPages" )
                    rProp.Value >>= mbExportOnlyNotesPages;
                else if ( rProp.Name == "UseTransitionEffects" )
                    rProp.Value >>= mbUseTransitionEffects;
                else if ( rProp.Name == "ExportFormFields" )
                    rProp.Value >>= mbExportFormFields;
                else if ( rProp.Name == "FormsType" )
                    rProp.Value >>= mnFormsFormat;
                else if ( rProp.Name == "AllowDuplicateFieldNames" )
                    rProp.Value >>= mbAllowDuplicateFieldNames;
                // viewer properties
                else if ( rProp.Name == "HideViewerToolbar" )
                    rProp.Value >>= mbHideViewerToolbar;
                else if ( rProp.Name == "HideViewerMenubar" )
                    rProp.Value >>= mbHideViewerMenubar;
                else if ( rProp.Name == "HideViewerWindowControls" )
                    rProp.Value >>= mbHideViewerWindowControls;
                else if ( rProp.Name == "ResizeWindowToInitialPage" )
                    rProp.Value >>= mbFitWindow;
                else if ( rProp.Name == "CenterWindow" )
                    rProp.Value >>= mbCenterWindow;
                else if ( rProp.Name == "OpenInFullScreenMode" )
                    rProp.Value >>= mbOpenInFullScreenMode;
                else if ( rProp.Name == "DisplayPDFDocumentTitle" )
                    rProp.Value >>= mbDisplayPDFDocumentTitle;
                else if ( rProp.Name == "InitialView" )
                    rProp.Value >>= mnPDFDocumentMode;
                else if ( rProp.Name == "Magnification" )
                    rProp.Value >>= mnPDFDocumentAction;
                else if ( rProp.Name == "Zoom" )
                    rProp.Value >>= mnZoom;
                else if ( rProp.Name == "InitialPage" )
                    rProp.Value >>= mnInitialPage;
                else if ( rProp.Name == "PageLayout" )
                    rProp.Value >>= mnPDFPageLayout;
                else if ( rProp.Name == "FirstPageOnLeft" )
                    rProp.Value >>= aContext.FirstPageLeft;
                else if ( rProp.Name == "IsAddStream" )
                    rProp.Value >>= mbAddStream;
                else if ( rProp.Name == "Watermark" )
                    rProp.Value >>= msWatermark;
                else if ( rProp.Name == "TiledWatermark" )
                    rProp.Value >>= msTiledWatermark;
                // now all the security related properties...
                else if ( rProp.Name == "EncryptFile" )
                    rProp.Value >>= mbEncrypt;
                else if ( rProp.Name == "DocumentOpenPassword" )
                    rProp.Value >>= aOpenPassword;
                else if ( rProp.Name == "RestrictPermissions" )
                    rProp.Value >>= mbRestrictPermissions;
                else if ( rProp.Name == "PermissionPassword" )
                    rProp.Value >>= aPermissionPassword;
                else if ( rProp.Name == "PreparedPasswords" )
                    rProp.Value >>= xEnc;
                else if ( rProp.Name == "PreparedPermissionPassword" )
                    rProp.Value >>= aPreparedPermissionPassword;
                else if ( rProp.Name == "Printing" )
                    rProp.Value >>= mnPrintAllowed;
                else if ( rProp.Name == "Changes" )
                    rProp.Value >>= mnChangesAllowed;
                else if ( rProp.Name == "EnableCopyingOfContent" )
                    rProp.Value >>= mbCanCopyOrExtract;
                else if ( rProp.Name == "EnableTextAccessForAccessibilityTools" )
                    rProp.Value >>= mbCanExtractForAccessibility;
                // i56629 links extra (relative links and other related stuff)
                else if ( rProp.Name == "ExportLinksRelativeFsys" )
                    rProp.Value >>= mbExportRelativeFsysLinks;
                else if ( rProp.Name == "PDFViewSelection" )
                    rProp.Value >>= mnDefaultLinkAction;
                else if ( rProp.Name == "ConvertOOoTargetToPDFTarget" )
                    rProp.Value >>= mbConvertOOoTargetToPDFTarget;
                else if ( rProp.Name == "ExportBookmarksToPDFDestination" )
                    rProp.Value >>= mbExportBmkToDest;
                else if ( rProp.Name == "ExportBookmarks" )
                    rProp.Value >>= mbExportBookmarks;
                else if ( rProp.Name == "ExportHiddenSlides" )
                    rProp.Value >>= mbExportHiddenSlides;
                else if ( rProp.Name == "SinglePageSheets" )
                    rProp.Value >>= mbSinglePageSheets;
                else if ( rProp.Name == "OpenBookmarkLevels" )
                    rProp.Value >>= mnOpenBookmarkLevels;
                else if ( rProp.Name == "SignPDF" )
                    rProp.Value >>= mbSignPDF;
                else if ( rProp.Name == "SignatureLocation" )
                    rProp.Value >>= msSignLocation;
                else if ( rProp.Name == "SignatureReason" )
                    rProp.Value >>= msSignReason;
                else if ( rProp.Name == "SignatureContactInfo" )
                    rProp.Value >>= msSignContact;
                else if ( rProp.Name == "SignaturePassword" )
                    rProp.Value >>= msSignPassword;
                else if ( rProp.Name == "SignatureCertificate" )
                    rProp.Value >>= maSignCertificate;
                else if ( rProp.Name == "SignatureTSA" )
                    rProp.Value >>= msSignTSA;
                else if ( rProp.Name == "ExportPlaceholders" )
                    rProp.Value >>= mbExportPlaceholders;
                else if ( rProp.Name == "UseReferenceXObject" )
                    rProp.Value >>= mbUseReferenceXObject;
                // Redaction & bitmap related stuff
                else if ( rProp.Name == "IsRedactMode" )
                    rProp.Value >>= mbIsRedactMode;
            }

            aContext.URL        = aURL.GetMainURL(INetURLObject::DecodeMechanism::ToIUri);

            // set the correct version, depending on user request
            switch( mnPDFTypeSelection )
            {
            default:
            case 0:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_6;
                break;
            case 1:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_1;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = true;  // does not allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 2:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_2;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = false; // does allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 3:
                aContext.Version    = vcl::PDFWriter::PDFVersion::PDF_A_3;
                mbUseTaggedPDF = true;          // force the tagged PDF as well
                mbRemoveTransparencies = false; // does allow transparencies
                mbEncrypt = false;              // no encryption
                xEnc.clear();
                break;
            case 15:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_5;
                break;
            case 16:
                aContext.Version = vcl::PDFWriter::PDFVersion::PDF_1_6;
                break;
            }

            // PDF/UA support
            aContext.UniversalAccessibilityCompliance = mbPDFUACompliance;
            if (mbPDFUACompliance)
            {
                mbUseTaggedPDF = true;
            }

            // copy in context the values default in the constructor or set by the FilterData sequence of properties
            aContext.Tagged     = mbUseTaggedPDF;

            // values used in viewer
            aContext.HideViewerToolbar          = mbHideViewerToolbar;
            aContext.HideViewerMenubar          = mbHideViewerMenubar;
            aContext.HideViewerWindowControls   = mbHideViewerWindowControls;
            aContext.FitWindow                  = mbFitWindow;
            aContext.CenterWindow               = mbCenterWindow;
            aContext.OpenInFullScreenMode       = mbOpenInFullScreenMode;
            aContext.DisplayPDFDocumentTitle    = mbDisplayPDFDocumentTitle;
            aContext.InitialPage                = mnInitialPage-1;
            aContext.OpenBookmarkLevels         = mnOpenBookmarkLevels;

            switch( mnPDFDocumentMode )
            {
                default:
                case 0:
                    aContext.PDFDocumentMode = vcl::PDFWriter::ModeDefault;
                    break;
                case 1:
                    aContext.PDFDocumentMode = vcl::PDFWriter::UseOutlines;
                    break;
                case 2:
                    aContext.PDFDocumentMode = vcl::PDFWriter::UseThumbs;
                    break;
            }
            switch( mnPDFDocumentAction )
            {
                default:
                case 0:
                    aContext.PDFDocumentAction = vcl::PDFWriter::ActionDefault;
                    break;
                case 1:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitInWindow;
                    break;
                case 2:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitWidth;
                    break;
                case 3:
                    aContext.PDFDocumentAction = vcl::PDFWriter::FitVisible;
                    break;
                case 4:
                    aContext.PDFDocumentAction = vcl::PDFWriter::ActionZoom;
                    aContext.Zoom = mnZoom;
                    break;
            }

            switch( mnPDFPageLayout )
            {
                default:
                case 0:
                    aContext.PageLayout = vcl::PDFWriter::DefaultLayout;
                    break;
                case 1:
                    aContext.PageLayout = vcl::PDFWriter::SinglePage;
                    break;
                case 2:
                    aContext.PageLayout = vcl::PDFWriter::Continuous;
                    break;
                case 3:
                    aContext.PageLayout = vcl::PDFWriter::ContinuousFacing;
                    break;
            }

            aContext.FirstPageLeft = false;

            // check if PDF/A, which does not allow encryption
            if( aContext.Version != vcl::PDFWriter::PDFVersion::PDF_A_1 )
            {
                // set check for permission change password
                // if not enabled and no permission password, force permissions to default as if PDF where without encryption
                if( mbRestrictPermissions && (xEnc.is() || !aPermissionPassword.isEmpty()) )
                {
                    mbEncrypt = true; // permission set as desired, done after
                }
                else
                {
                    // force permission to default
                    mnPrintAllowed                  = 2 ;
                    mnChangesAllowed                = 4 ;
                    mbCanCopyOrExtract              = true;
                    mbCanExtractForAccessibility    = true ;
                }

                switch( mnPrintAllowed )
                {
                case 0: // initialized when aContext is build, means no printing
                    break;
                default:
                case 2:
                    aContext.Encryption.CanPrintFull            = true;
                    [[fallthrough]];
                case 1:
                    aContext.Encryption.CanPrintTheDocument     = true;
                    break;
                }

                switch( mnChangesAllowed )
                {
                case 0: // already in struct PDFSecPermissions CTOR
                    break;
                case 1:
                    aContext.Encryption.CanAssemble             = true;
                    break;
                case 2:
                    aContext.Encryption.CanFillInteractive      = true;
                    break;
                case 3:
                    aContext.Encryption.CanAddOrModify          = true;
                    break;
                default:
                case 4:
                    aContext.Encryption.CanModifyTheContent     =
                        aContext.Encryption.CanCopyOrExtract    =
                        aContext.Encryption.CanAddOrModify      =
                        aContext.Encryption.CanFillInteractive  = true;
                    break;
                }

                aContext.Encryption.CanCopyOrExtract                = mbCanCopyOrExtract;
                aContext.Encryption.CanExtractForAccessibility  = mbCanExtractForAccessibility;
                if( mbEncrypt && ! xEnc.is() )
                    xEnc = vcl::PDFWriter::InitEncryption( aPermissionPassword, aOpenPassword );
                if( mbEncrypt && !aPermissionPassword.isEmpty() && ! aPreparedPermissionPassword.hasElements() )
                    aPreparedPermissionPassword = comphelper::OStorageHelper::CreatePackageEncryptionData( aPermissionPassword );
            }
            // after this point we don't need the legacy clear passwords anymore
            // however they are still inside the passed filter data sequence
            // which is sadly out of our control
            aPermissionPassword.clear();
            aOpenPassword.clear();

            /*
            * FIXME: the entries are only implicitly defined by the resource file. Should there
            * ever be an additional form submit format this could get invalid.
            */
            switch( mnFormsFormat )
            {
                case 1:
                    aContext.SubmitFormat = vcl::PDFWriter::PDF;
                    break;
                case 2:
                    aContext.SubmitFormat = vcl::PDFWriter::HTML;
                    break;
                case 3:
                    aContext.SubmitFormat = vcl::PDFWriter::XML;
                    break;
                default:
                case 0:
                    aContext.SubmitFormat = vcl::PDFWriter::FDF;
                    break;
            }
            aContext.AllowDuplicateFieldNames = mbAllowDuplicateFieldNames;

            // get model
            Reference< frame::XModel > xModel( mxSrcDoc, UNO_QUERY );
            {
                // #i56629: Relative link stuff
                // set the base URL of the file: then base URL
                aContext.BaseURL = xModel->getURL();
                // relative link option is private to PDF Export filter and limited to local filesystem only
                aContext.RelFsys = mbExportRelativeFsysLinks;
                // determine the default acton for PDF links
                switch( mnDefaultLinkAction )
                {
                default:
                    // default: URI, without fragment conversion (the bookmark in PDF may not work)
                case 0:
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIAction;
                    break;
                case 1:
                    // view PDF through the reader application
                    aContext.ForcePDFAction = true;
                    aContext.DefaultLinkAction = vcl::PDFWriter::LaunchAction;
                    break;
                case 2:
                    // view PDF through an Internet browser
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIActionDestination;
                    break;
                }
                aContext.ConvertOOoTargetToPDFTarget = mbConvertOOoTargetToPDFTarget;

                // check for Link Launch action, not allowed on PDF/A-1
                // this code chunk checks when the filter is called from scripting
                if( aContext.Version == vcl::PDFWriter::PDFVersion::PDF_A_1 &&
                    aContext.DefaultLinkAction == vcl::PDFWriter::LaunchAction )
                {
                    // force the similar allowed URI action
                    aContext.DefaultLinkAction = vcl::PDFWriter::URIActionDestination;
                    // and remove the remote goto action forced on PDF file
                    aContext.ForcePDFAction = false;
                }
            }

            aContext.SignPDF = mbSignPDF;
            aContext.SignLocation = msSignLocation;
            aContext.SignContact = msSignContact;
            aContext.SignReason = msSignReason;
            aContext.SignPassword = msSignPassword;
            aContext.SignCertificate = maSignCertificate;
            aContext.SignTSA = msSignTSA;
            aContext.UseReferenceXObject = mbUseReferenceXObject;

            // all context data set, time to create the printing device
            std::unique_ptr<vcl::PDFWriter> pPDFWriter(new vcl::PDFWriter( aContext, xEnc ));
            OutputDevice*       pOut = pPDFWriter->GetReferenceDevice();

            DBG_ASSERT( pOut, "PDFExport::Export: no reference device" );
            xDevice->SetOutputDevice(pOut);

            if( mbAddStream )
            {
                // export stream
                // get mimetype
                OUString aSrcMimetype = getMimetypeForDocument( mxContext, mxSrcDoc );
                pPDFWriter->AddStream( aSrcMimetype,
                                       new PDFExportStreamDoc( mxSrcDoc, aPreparedPermissionPassword )
                                       );
            }

            if ( pOut )
            {
                DBG_ASSERT( pOut->GetExtOutDevData() == nullptr, "PDFExport: ExtOutDevData already set!!!" );
                std::unique_ptr<vcl::PDFExtOutDevData> pPDFExtOutDevData(new vcl::PDFExtOutDevData( *pOut ));
                pOut->SetExtOutDevData( pPDFExtOutDevData.get() );
                pPDFExtOutDevData->SetIsExportNotes( mbExportNotes );
                pPDFExtOutDevData->SetIsExportTaggedPDF( mbUseTaggedPDF );
                pPDFExtOutDevData->SetIsExportTransitionEffects( mbUseTransitionEffects );
                pPDFExtOutDevData->SetIsExportFormFields( mbExportFormFields );
                pPDFExtOutDevData->SetIsExportBookmarks( mbExportBookmarks );
                pPDFExtOutDevData->SetIsExportHiddenSlides( mbExportHiddenSlides );
                pPDFExtOutDevData->SetIsSinglePageSheets( mbSinglePageSheets );
                pPDFExtOutDevData->SetIsLosslessCompression( mbUseLosslessCompression );
                pPDFExtOutDevData->SetCompressionQuality( mnQuality );
                pPDFExtOutDevData->SetIsReduceImageResolution( mbReduceImageResolution );
                pPDFExtOutDevData->SetIsExportNamedDestinations( mbExportBmkToDest );

                Sequence< PropertyValue > aRenderOptions( 8 );
                aRenderOptions[ 0 ].Name = "RenderDevice";
                aRenderOptions[ 0 ].Value <<= uno::Reference<awt::XDevice>(xDevice.get());
                aRenderOptions[ 1 ].Name = "ExportNotesPages";
                aRenderOptions[ 1 ].Value <<= false;
                Any& rExportNotesValue = aRenderOptions[ 1 ].Value;
                aRenderOptions[ 2 ].Name = "IsFirstPage";
                aRenderOptions[ 2 ].Value <<= true;
                aRenderOptions[ 3 ].Name = "IsLastPage";
                aRenderOptions[ 3 ].Value <<= false;
                aRenderOptions[ 4 ].Name = "IsSkipEmptyPages";
                aRenderOptions[ 4 ].Value <<= mbSkipEmptyPages;
                aRenderOptions[ 5 ].Name = "PageRange";
                aRenderOptions[ 5 ].Value <<= aPageRange;
                aRenderOptions[ 6 ].Name = "ExportPlaceholders";
                aRenderOptions[ 6 ].Value <<= mbExportPlaceholders;
                aRenderOptions[ 7 ].Name = "SinglePageSheets";
                aRenderOptions[ 7 ].Value <<= mbSinglePageSheets;

                if( !aPageRange.isEmpty() || !aSelection.hasValue() )
                {
                    aSelection = Any();
                    aSelection <<= mxSrcDoc;
                }
                bool bExportNotesPages = false;
                bool bReChangeToNormalView = false;
                const OUString sShowOnlineLayout( "ShowOnlineLayout" );
                bool bReHideWhitespace = false;
                const OUString sHideWhitespace("HideWhitespace");
                uno::Reference< beans::XPropertySet > xViewProperties;

                if ( aCreator == "Writer" )
                {
                    // #i92835: if Writer is in web layout mode this has to be switched to normal view and back to web view in the end
                    try
                    {
                        Reference< view::XViewSettingsSupplier > xVSettingsSupplier( xModel->getCurrentController(), uno::UNO_QUERY_THROW );
                        xViewProperties = xVSettingsSupplier->getViewSettings();
                        xViewProperties->getPropertyValue( sShowOnlineLayout ) >>= bReChangeToNormalView;
                        if( bReChangeToNormalView )
                        {
                            xViewProperties->setPropertyValue( sShowOnlineLayout, uno::makeAny( false ) );
                        }

                        // Also, disable hide-whitespace during export.
                        xViewProperties->getPropertyValue(sHideWhitespace) >>= bReHideWhitespace;
                        if (bReHideWhitespace)
                        {
                            xViewProperties->setPropertyValue(sHideWhitespace, uno::makeAny(false));
                        }
                    }
                    catch( const uno::Exception& )
                    {
                    }

                }

                const sal_Int32 nPageCount = xRenderable->getRendererCount( aSelection, aRenderOptions );

                if ( mbExportNotesPages && aCreator == "Impress" )
                {
                    uno::Reference< drawing::XShapes > xShapes;     // do not allow to export notes when exporting a selection
                    if ( ! ( aSelection >>= xShapes ) )
                        bExportNotesPages = true;
                }
                const bool bExportPages = !bExportNotesPages || !mbExportOnlyNotesPages;

                if( aPageRange.isEmpty() || mbSinglePageSheets)
                {
                    aPageRange = OUString::number( 1 ) + "-" + OUString::number(nPageCount );
                }
                StringRangeEnumerator aRangeEnum( aPageRange, 0, nPageCount-1 );

                if ( mxStatusIndicator.is() )
                {
                    std::locale loc(Translate::Create("flt"));
                    sal_Int32 nTotalPageCount = aRangeEnum.size();
                    if ( bExportPages && bExportNotesPages )
                        nTotalPageCount *= 2;
                    mxStatusIndicator->start(Translate::get(PDF_PROGRESS_BAR, loc), nTotalPageCount);
                }

                bRet = nPageCount > 0;

                if ( bRet && bExportPages )
                    bRet = ExportSelection( *pPDFWriter, xRenderable, aSelection, aRangeEnum, aRenderOptions, nPageCount );

                if ( bRet && bExportNotesPages )
                {
                    rExportNotesValue <<= true;
                    bRet = ExportSelection( *pPDFWriter, xRenderable, aSelection, aRangeEnum, aRenderOptions, nPageCount );
                }
                if ( mxStatusIndicator.is() )
                    mxStatusIndicator->end();

                // if during the export the doc locale was set copy it to PDF writer
                const css::lang::Locale& rLoc( pPDFExtOutDevData->GetDocumentLocale() );
                if( !rLoc.Language.isEmpty() )
                    pPDFWriter->SetDocumentLocale( rLoc );

                if( bRet )
                {
                    pPDFExtOutDevData->PlayGlobalActions( *pPDFWriter );
                    bRet = pPDFWriter->Emit();
                    aErrors = pPDFWriter->GetErrors();
                }
                pOut->SetExtOutDevData( nullptr );
                if( bReChangeToNormalView )
                {
                    try
                    {
                        xViewProperties->setPropertyValue( sShowOnlineLayout, uno::makeAny( true ) );
                    }
                    catch( const uno::Exception& )
                    {
                    }
                }
                if( bReHideWhitespace )
                {
                    try
                    {
                        xViewProperties->setPropertyValue( sHideWhitespace, uno::makeAny( true ) );
                    }
                    catch( const uno::Exception& )
                    {
                    }
                }
            }
        }
    }

    // show eventual errors during export
    showErrors( aErrors );

    return bRet;
}


namespace
{

typedef cppu::WeakComponentImplHelper< task::XInteractionRequest > PDFErrorRequestBase;

class PDFErrorRequest : private cppu::BaseMutex,
                        public PDFErrorRequestBase
{
    task::PDFExportException maExc;
public:
    explicit PDFErrorRequest( const task::PDFExportException& i_rExc );

    // XInteractionRequest
    virtual uno::Any SAL_CALL getRequest() override;
    virtual uno::Sequence< uno::Reference< task::XInteractionContinuation > > SAL_CALL getContinuations() override;
};


PDFErrorRequest::PDFErrorRequest( const task::PDFExportException& i_rExc ) :
    PDFErrorRequestBase( m_aMutex ),
    maExc( i_rExc )
{
}


uno::Any SAL_CALL PDFErrorRequest::getRequest()
{
    osl::MutexGuard const guard( m_aMutex );

    uno::Any aRet;
    aRet <<= maExc;
    return aRet;
}


uno::Sequence< uno::Reference< task::XInteractionContinuation > > SAL_CALL PDFErrorRequest::getContinuations()
{
    return uno::Sequence< uno::Reference< task::XInteractionContinuation > >();
}

} // end anonymous namespace


void PDFExport::showErrors( const std::set< vcl::PDFWriter::ErrorCode >& rErrors )
{
    if( ! rErrors.empty() && mxIH.is() )
    {
        task::PDFExportException aExc;
        aExc.ErrorCodes = comphelper::containerToSequence<sal_Int32>( rErrors );
        Reference< task::XInteractionRequest > xReq( new PDFErrorRequest( aExc ) );
        mxIH->handle( xReq );
    }
}


void PDFExport::ImplExportPage( vcl::PDFWriter& rWriter, vcl::PDFExtOutDevData& rPDFExtOutDevData, const GDIMetaFile& rMtf )
{
    //Rectangle(Point, Size) creates a rectangle off by 1, use Rectangle(long, long, long, long) instead
    basegfx::B2DPolygon aSize(tools::Polygon(tools::Rectangle(0, 0, rMtf.GetPrefSize().Width(), rMtf.GetPrefSize().Height())).getB2DPolygon());
    basegfx::B2DPolygon aSizePDF(OutputDevice::LogicToLogic(aSize, rMtf.GetPrefMapMode(), MapMode(MapUnit::MapPoint)));
    basegfx::B2DRange aRangePDF(aSizePDF.getB2DRange());
    tools::Rectangle       aPageRect( Point(), rMtf.GetPrefSize() );

    rWriter.NewPage( aRangePDF.getWidth(), aRangePDF.getHeight() );
    rWriter.SetMapMode( rMtf.GetPrefMapMode() );

    vcl::PDFWriter::PlayMetafileContext aCtx;
    GDIMetaFile aMtf;
    if( mbRemoveTransparencies )
    {
        aCtx.m_bTransparenciesWereRemoved = rWriter.GetReferenceDevice()->
            RemoveTransparenciesFromMetaFile( rMtf, aMtf, mnMaxImageResolution, mnMaxImageResolution,
                                              false, true, mbReduceImageResolution );
    }
    else
    {
        aMtf = rMtf;
    }
    aCtx.m_nMaxImageResolution      = mbReduceImageResolution ? mnMaxImageResolution : 0;
    aCtx.m_bOnlyLosslessCompression = mbUseLosslessCompression;
    aCtx.m_nJPEGQuality             = mnQuality;


    rWriter.SetClipRegion( basegfx::B2DPolyPolygon(
        basegfx::utils::createPolygonFromRect( vcl::unotools::b2DRectangleFromRectangle(aPageRect) ) ) );

    rWriter.PlayMetafile( aMtf, aCtx, &rPDFExtOutDevData );

    rPDFExtOutDevData.ResetSyncData();

    if (!msWatermark.isEmpty())
    {
        ImplWriteWatermark( rWriter, Size(aRangePDF.getWidth(), aRangePDF.getHeight()) );
    }
    else if (!msTiledWatermark.isEmpty())
    {
        ImplWriteTiledWatermark( rWriter, Size(aRangePDF.getWidth(), aRangePDF.getHeight()) );
    }
}


void PDFExport::ImplWriteWatermark( vcl::PDFWriter& rWriter, const Size& rPageSize )
{
    vcl::Font aFont( "Helvetica", Size( 0, 3*rPageSize.Height()/4 ) );
    aFont.SetItalic( ITALIC_NONE );
    aFont.SetWidthType( WIDTH_NORMAL );
    aFont.SetWeight( WEIGHT_NORMAL );
    aFont.SetAlignment( ALIGN_BOTTOM );
    tools::Long nTextWidth = rPageSize.Width();
    if( rPageSize.Width() < rPageSize.Height() )
    {
        nTextWidth = rPageSize.Height();
        aFont.SetOrientation( Degree10(2700) );
    }

    // adjust font height for text to fit
    OutputDevice* pDev = rWriter.GetReferenceDevice();
    pDev->Push();
    pDev->SetFont( aFont );
    pDev->SetMapMode( MapMode( MapUnit::MapPoint ) );
    int w = 0;
    while( ( w = pDev->GetTextWidth( msWatermark ) ) > nTextWidth )
    {
        if (w == 0)
            break;
        tools::Long nNewHeight = aFont.GetFontHeight() * nTextWidth / w;
        if( nNewHeight == aFont.GetFontHeight() )
        {
            nNewHeight--;
            if( nNewHeight <= 0 )
                break;
        }
        aFont.SetFontHeight( nNewHeight );
        pDev->SetFont( aFont );
    }
    tools::Long nTextHeight = pDev->GetTextHeight();
    // leave some maneuvering room for rounding issues, also
    // some fonts go a little outside ascent/descent
    nTextHeight += nTextHeight/20;
    pDev->Pop();

    rWriter.Push();
    rWriter.SetMapMode( MapMode( MapUnit::MapPoint ) );
    rWriter.SetFont( aFont );
    rWriter.SetTextColor( COL_LIGHTGREEN );
    Point aTextPoint;
    tools::Rectangle aTextRect;
    if( rPageSize.Width() > rPageSize.Height() )
    {
        aTextPoint = Point( (rPageSize.Width()-w)/2,
                            rPageSize.Height()-(rPageSize.Height()-nTextHeight)/2 );
        aTextRect = tools::Rectangle( Point( (rPageSize.Width()-w)/2,
                                      (rPageSize.Height()-nTextHeight)/2 ),
                               Size( w, nTextHeight ) );
    }
    else
    {
        aTextPoint = Point( (rPageSize.Width()-nTextHeight)/2,
                            (rPageSize.Height()-w)/2 );
        aTextRect = tools::Rectangle( aTextPoint, Size( nTextHeight, w ) );
    }
    rWriter.SetClipRegion();
    rWriter.BeginTransparencyGroup();
    rWriter.DrawText( aTextPoint, msWatermark );
    rWriter.EndTransparencyGroup( aTextRect, 50 );
    rWriter.Pop();
}

void PDFExport::ImplWriteTiledWatermark( vcl::PDFWriter& rWriter, const Size& rPageSize )
{
    const sal_Int32 nHoriWatermark = ((rPageSize.Width()) / 200) + 1; // 橫向浮水印總數
    const tools::Long nTileWidth = rPageSize.Width() / nHoriWatermark; // 每塊拼貼寬度
    const tools::Long nTextWidth = nTileWidth * 0.9; // 文字寬度是拼貼大小的 9/10
    const Size aTileSize(nTileWidth, nTileWidth); // 每個拼貼區域大小
    const Size aTextSize(nTextWidth, nTextWidth); // 每個文字區域大小
    const Point aTextOffset((nTileWidth - nTextWidth) / 2, (nTileWidth - nTextWidth) / 2); // 文字區域在拼貼區域置中偏移值
    const sal_Int32 nTileVertCount = rPageSize.Height() / aTileSize.Height(); // 垂直浮水印總數
    // 拼貼區域相對頁面的偏移值
    const Point aPageOffset((rPageSize.Width() - (nHoriWatermark * aTileSize.Width())) / 2,
                            (rPageSize.Height() - (nTileVertCount * aTileSize.Height())) / 2);

    // 沒有製作過浮水印 bitmap
    if (maTiledWatermarkBmp.IsEmpty())
    {
        const tools::Long nDefaultFontSize = 40; // 預設字型大小
        OUString watermark = msTiledWatermark.trim();
        OUString aText = watermark;
        sal_uInt32 nAngle = 450; // 預設角度
        sal_uInt8 nTransparency = 80; // 預設透明度

        vcl::Font aFont;
        // 如果浮水印是 JSON 格式的話
        if (watermark.startsWith("{") && watermark.endsWith("}"))
        {
            boost::property_tree::ptree aTree;
            std::stringstream aStream(watermark.toUtf8().getStr());
            boost::property_tree::read_json(aStream, aTree);

            // 浮水印文字
            aText = OUString::fromUtf8(aTree.get<std::string>("text", "").c_str());
            // 沒有指定浮水印文字就結束
            if (aText.isEmpty())
            {
                return;
            }

            // 字型名稱
            OUString aFamilyName = OUString::fromUtf8(aTree.get<std::string>("familyname", "Liberation Sans").c_str());
            aFont = vcl::Font(aFamilyName, Size(0, nDefaultFontSize));

            // 角度
            nAngle = aTree.get<sal_uInt32>("angle", 45) * 10;
            // 不透明度
            double nOpacity = aTree.get<double>("opacity", 0.2);
            // 轉成透明度%
            nTransparency = (1 - nOpacity) * 255;
            // 顏色
            aFont.SetColor(Color::STRtoRGB(OUString::fromUtf8(aTree.get<std::string>("color", "#000000").c_str())));
            // 粗體
            aFont.SetWeight(aTree.get<bool>("bold", false) ? WEIGHT_BOLD : WEIGHT_NORMAL);
            // 斜體
            aFont.SetItalic(aTree.get<bool>("italic", false) ? ITALIC_NORMAL : ITALIC_NONE);
            // 是否浮雕字
            std::string aRelief = aTree.get<std::string>("relief", "");
            if (aRelief == "embossed") // 浮凸
            {
                aFont.SetRelief(FontRelief::Embossed);
            }
            else if (aRelief == "engraved") // 雕刻
            {
                aFont.SetRelief(FontRelief::Engraved);
            }
            else // 未指定浮雕字
            {
                aFont.SetOutline(aTree.get<bool>("outline", false)); // 輪廓(中空)
                aFont.SetShadow(aTree.get<bool>("shadow", false)); // 陰影
            }
        }
        // 採用預設的浮水印字型設定
        else
        {
            aFont = vcl::Font("Liberation Sans", Size(0, nDefaultFontSize));
            aFont.SetItalic(ITALIC_NONE);
            aFont.SetWidthType(WIDTH_NORMAL);
            aFont.SetWeight(WEIGHT_NORMAL);
            aFont.SetAlignment(ALIGN_BOTTOM);
            aFont.SetColor(COL_LIGHTGREEN);
        }

        // 浮水印 bitmap 大小，此 szie 產生的圖較大，畫在 pdf 上，縮放時不致產生肉眼可見的失真
        const tools::Long nBmpWidth = 512;
        const Size aBmpSize(nBmpWidth, nBmpWidth);
        const tools::Rectangle aTextRect(Point(0, 0), aBmpSize); // 文字繪製範圍
        auto aDevice(VclPtr<VirtualDevice>::Create(DeviceFormat::DEFAULT, DeviceFormat::BITMASK));
        aDevice->SetFont(aFont);
        aDevice->SetOutputSizePixel(aBmpSize);
        // 文字允許多行，各行以 \n 分隔
        std::vector<OUString> aLines = comphelper::string::split(aText, '\n');
        // 找出最長行
        sal_Int32 nFontWidth = 0;
        for (auto &aTmpText : aLines)
        {
            const tools::Long nTmpWidth = aDevice->GetTextWidth(aTmpText);
            // 找出最寬的文字
            if (nTmpWidth > nFontWidth)
            {
                nFontWidth = nTmpWidth;
            }
        }
        // 重新縮放字型大小
        aFont.SetFontHeight(nDefaultFontSize * (nBmpWidth / static_cast<double>(nFontWidth) / 1.05));
        aDevice->SetFont(aFont); // 重設字型
        aDevice->SetBackground(COL_TRANSPARENT); // 透明背景
        // 繪製文字
        aDevice->DrawText(aTextRect, aText,
                        DrawTextFlags::Center
                        | DrawTextFlags::VCenter
                        | DrawTextFlags::MultiLine
                        | DrawTextFlags::WordBreak);
        // 保存點陣圖資料
        maTiledWatermarkBmp = aDevice->GetBitmapEx(Point(0, 0), aBmpSize);
        // 空的就結束
        if (maTiledWatermarkBmp.IsEmpty())
        {
            return;
        }
        else
        {
            // 轉換透明度
            maTiledWatermarkBmp.AdjustTransparency(nTransparency);
            // 旋轉角度
            if (nAngle)
            {
                maTiledWatermarkBmp.Rotate(Degree10(nAngle), COL_TRANSPARENT);
            }
        }
    }

    rWriter.Push();
    rWriter.SetMapMode(MapMode(MapUnit::MapPoint));
    // 繪製整頁浮水印
    for (sal_Int32 w = 0; w < nHoriWatermark; w ++)
    {
        for (sal_Int32 h = 0 ; h < nTileVertCount; h++)
        {
            // 計算拼貼區域定位點
            const Point aBmpPoint(
                (w * aTileSize.Width()) + aPageOffset.X() + aTextOffset.X(),
                (h * aTileSize.Height()) + aPageOffset.Y() + aTextOffset.Y());
            rWriter.DrawBitmapEx(aBmpPoint, aTextSize, maTiledWatermarkBmp);
        }
    }
    rWriter.Pop();
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
