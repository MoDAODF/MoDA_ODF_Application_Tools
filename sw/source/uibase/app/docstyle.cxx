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

#include <memory>
#include <sal/config.h>
#include <sal/log.hxx>

#include <cstdlib>

#include <hintids.hxx>
#include <rtl/ustrbuf.hxx>
#include <svl/itemiter.hxx>
#include <svl/eitem.hxx>
#include <unotools/syslocale.hxx>
#include <editeng/boxitem.hxx>
#include <editeng/numitem.hxx>
#include <editeng/lrspitem.hxx>
#include <drawdoc.hxx>
#include <fmtcol.hxx>
#include <uitool.hxx>
#include <wrtsh.hxx>
#include <docsh.hxx>
#include <frmfmt.hxx>
#include <charfmt.hxx>
#include <tblafmt.hxx>
#include <poolfmt.hxx>
#include <pagedesc.hxx>
#include <docstyle.hxx>
#include <docary.hxx>
#include <ccoll.hxx>
#include <doc.hxx>
#include <IDocumentUndoRedo.hxx>
#include <IDocumentStylePoolAccess.hxx>
#include <IDocumentSettingAccess.hxx>
#include <IDocumentDrawModelAccess.hxx>
#include <IDocumentState.hxx>
#include <cmdid.h>
#include <strings.hrc>
#include <paratr.hxx>
#include <SwStyleNameMapper.hxx>
#include <svl/cjkoptions.hxx>
#include <svl/ctloptions.hxx>
#include <unotools/intlwrapper.hxx>
#include <numrule.hxx>
#include <svx/xdef.hxx>
#include <SwRewriter.hxx>
#include <hints.hxx>
#include <frameformats.hxx>
#include <svx/xfillit0.hxx>
#include <svx/xflftrit.hxx>
#include <svx/drawitem.hxx>
#include <editeng/eeitem.hxx>

// The Format names in the list of all names have the
// following family as their first character:

#define cCHAR       u'c'
#define cPARA       u'p'
#define cFRAME      u'f'
#define cPAGE       u'g'
#define cNUMRULE    u'n'
#define cTABSTYLE   u't'
#define cCELLSTYLE  u'b'

using namespace com::sun::star;

// At the names' publication, this character is removed again and the
// family is newly generated.

// In addition now there is the Bit bPhysical. In case this Bit is
// TRUE, the Pool-Formatnames are not being submitted.

namespace {

class SwImplShellAction
{
    SwWrtShell* pSh;
    std::unique_ptr<CurrShell> pCurrSh;
public:
    explicit SwImplShellAction( SwDoc& rDoc );
    ~SwImplShellAction() COVERITY_NOEXCEPT_FALSE;
    SwImplShellAction(const SwImplShellAction&) = delete;
    SwImplShellAction& operator=(const SwImplShellAction&) = delete;
};

}

SwImplShellAction::SwImplShellAction( SwDoc& rDoc )
{
    if( rDoc.GetDocShell() )
        pSh = rDoc.GetDocShell()->GetWrtShell();
    else
        pSh = nullptr;

    if( pSh )
    {
        pCurrSh.reset( new CurrShell( pSh ) );
        pSh->StartAllAction();
    }
}

SwImplShellAction::~SwImplShellAction() COVERITY_NOEXCEPT_FALSE
{
    if( pCurrSh )
    {
        pSh->EndAllAction();
        pCurrSh.reset();
    }
}

// find/create SwCharFormate
// possibly fill Style
static SwCharFormat* lcl_FindCharFormat( SwDoc& rDoc,
                            const OUString& rName,
                            SwDocStyleSheet* pStyle = nullptr,
                            bool bCreate = true )
{
    SwCharFormat*  pFormat = nullptr;
    if (!rName.isEmpty())
    {
        pFormat = rDoc.FindCharFormatByName( rName );
        if( !pFormat && rName == SwResId(STR_POOLCHR_STANDARD))
        {
            // Standard-Character template
            pFormat = rDoc.GetDfltCharFormat();
        }

        if( !pFormat && bCreate )
        {   // explore Pool
            const sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(rName, SwGetPoolIdFromName::ChrFmt);
            if(nId != USHRT_MAX)
                pFormat = rDoc.getIDocumentStylePoolAccess().GetCharFormatFromPool(nId);
        }
    }
    if(pStyle)
    {
        if(pFormat)
        {
            pStyle->SetPhysical(true);
            SwFormat* p = pFormat->DerivedFrom();
            if( p && !p->IsDefault() )
                pStyle->PresetParent( p->GetName() );
            else
                pStyle->PresetParent( OUString() );
        }
        else
            pStyle->SetPhysical(false);
    }
    return pFormat;
}

// find/create ParaFormats
// fill Style
static SwTextFormatColl* lcl_FindParaFormat(  SwDoc& rDoc,
                                const OUString& rName,
                                SwDocStyleSheet* pStyle = nullptr,
                                bool bCreate = true )
{
    SwTextFormatColl*   pColl = nullptr;

    if (!rName.isEmpty())
    {
        pColl = rDoc.FindTextFormatCollByName( rName );
        if( !pColl && bCreate )
        {   // explore Pool
            const sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(rName, SwGetPoolIdFromName::TxtColl);
            if(nId != USHRT_MAX)
                pColl = rDoc.getIDocumentStylePoolAccess().GetTextCollFromPool(nId);
        }
    }

    if(pStyle)
    {
        if(pColl)
        {
            pStyle->SetPhysical(true);
            if( pColl->DerivedFrom() && !pColl->DerivedFrom()->IsDefault() )
                pStyle->PresetParent( pColl->DerivedFrom()->GetName() );
            else
                pStyle->PresetParent( OUString() );

            SwTextFormatColl& rNext = pColl->GetNextTextFormatColl();
            pStyle->PresetFollow(rNext.GetName());
        }
        else
            pStyle->SetPhysical(false);
    }
    return pColl;
}

// Border formats
static SwFrameFormat* lcl_FindFrameFormat(   SwDoc& rDoc,
                            const OUString& rName,
                            SwDocStyleSheet* pStyle = nullptr,
                            bool bCreate = true )
{
    SwFrameFormat* pFormat = nullptr;
    if( !rName.isEmpty() )
    {
        pFormat = rDoc.FindFrameFormatByName( rName );
        if( !pFormat && bCreate )
        {   // explore Pool
            const sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(rName, SwGetPoolIdFromName::FrmFmt);
            if(nId != USHRT_MAX)
                pFormat = rDoc.getIDocumentStylePoolAccess().GetFrameFormatFromPool(nId);
        }
    }

    if(pStyle)
    {
        if(pFormat)
        {
            pStyle->SetPhysical(true);
            if( pFormat->DerivedFrom() && !pFormat->DerivedFrom()->IsDefault() )
                pStyle->PresetParent( pFormat->DerivedFrom()->GetName() );
            else
                pStyle->PresetParent( OUString() );
        }
        else
            pStyle->SetPhysical(false);
    }
    return pFormat;
}

// Page descriptors
static const SwPageDesc* lcl_FindPageDesc( SwDoc&  rDoc,
                                    const OUString& rName,
                                    SwDocStyleSheet* pStyle = nullptr,
                                    bool bCreate = true )
{
    const SwPageDesc* pDesc = nullptr;

    if (!rName.isEmpty())
    {
        pDesc = rDoc.FindPageDesc(rName);
        if( !pDesc && bCreate )
        {
            sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(rName, SwGetPoolIdFromName::PageDesc);
            if(nId != USHRT_MAX)
                pDesc = rDoc.getIDocumentStylePoolAccess().GetPageDescFromPool(nId);
        }
    }

    if(pStyle)
    {
        if(pDesc)
        {
            pStyle->SetPhysical(true);
            if(pDesc->GetFollow())
                pStyle->PresetFollow(pDesc->GetFollow()->GetName());
            else
                pStyle->PresetParent( OUString() );
        }
        else
            pStyle->SetPhysical(false);
    }
    return pDesc;
}

static const SwNumRule* lcl_FindNumRule(   SwDoc&  rDoc,
                                    const OUString& rName,
                                    SwDocStyleSheet* pStyle = nullptr,
                                    bool bCreate = true )
{
    const SwNumRule* pRule = nullptr;

    if (!rName.isEmpty())
    {
        pRule = rDoc.FindNumRulePtr( rName );
        if( !pRule && bCreate )
        {
            sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(rName, SwGetPoolIdFromName::NumRule);
            if(nId != USHRT_MAX)
                pRule = rDoc.getIDocumentStylePoolAccess().GetNumRuleFromPool(nId);
        }
    }

    if(pStyle)
    {
        if(pRule)
        {
            pStyle->SetPhysical(true);
            pStyle->PresetParent( OUString() );
        }
        else
            pStyle->SetPhysical(false);
    }
    return pRule;
}

static SwTableAutoFormat* lcl_FindTableStyle(SwDoc& rDoc, const OUString& rName, SwDocStyleSheet *pStyle = nullptr, bool bCreate = true)
{
    SwTableAutoFormat* pFormat = nullptr;

    if (!rName.isEmpty())
    {
        pFormat = rDoc.GetTableStyles().FindAutoFormat(rName);
        if (!pFormat && bCreate)
        {
            SwTableAutoFormat aNew(rName);
            rDoc.GetTableStyles().AddAutoFormat(aNew);
        }
    }

    if(pStyle)
    {
        if(pFormat)
        {
            pStyle->SetPhysical(true);
            pStyle->PresetParent(OUString());
        }
        else
            pStyle->SetPhysical(false);
    }
    return pFormat;
}

static const SwBoxAutoFormat* lcl_FindCellStyle(SwDoc& rDoc, const OUString& rName, SwDocStyleSheet *pStyle)
{
    const SwBoxAutoFormat* pFormat = rDoc.GetCellStyles().GetBoxFormat(rName);

    if (!pFormat)
    {
        const auto& aTableTemplateMap = SwTableAutoFormat::GetTableTemplateMap();
        SwTableAutoFormatTable& rTableStyles = rDoc.GetTableStyles();
        for (size_t i=0; i < rTableStyles.size() && !pFormat; ++i)
        {
            const SwTableAutoFormat& rTableStyle = rTableStyles[i];
            for (size_t nBoxFormat=0; nBoxFormat < aTableTemplateMap.size() && !pFormat; ++nBoxFormat)
            {
                    const sal_uInt32 nBoxIndex = aTableTemplateMap[nBoxFormat];
                    const SwBoxAutoFormat& rBoxFormat = rTableStyle.GetBoxFormat(nBoxIndex);
                    OUString sBoxFormatName;
                    SwStyleNameMapper::FillProgName(rTableStyle.GetName(), sBoxFormatName, SwGetPoolIdFromName::TabStyle);
                    sBoxFormatName += rTableStyle.GetTableTemplateCellSubName(rBoxFormat);
                    if (rName == sBoxFormatName)
                        pFormat = &rBoxFormat;
            }
        }
    }

    if(pStyle)
    {
        if(pFormat)
        {
            pStyle->SetPhysical(true);
            pStyle->PresetParent(OUString());
        }
        else
            pStyle->SetPhysical(false);
    }
    return pFormat;
}

sal_uInt32 SwStyleSheetIterator::SwPoolFormatList::FindName(SfxStyleFamily eFam,
                                                         const OUString &rName)
{
    if(!maImpl.empty())
    {
        sal_Unicode cStyle(0);
        switch( eFam )
        {
        case SfxStyleFamily::Char:
            cStyle = cCHAR;
            break;
        case SfxStyleFamily::Para:
            cStyle = cPARA;
            break;
        case SfxStyleFamily::Frame:
            cStyle = cFRAME;
            break;
        case SfxStyleFamily::Page:
            cStyle = cPAGE;
            break;
        case SfxStyleFamily::Pseudo:
            cStyle = cNUMRULE;
            break;
        case SfxStyleFamily::Table:
            cStyle = cTABSTYLE;
            break;
        case SfxStyleFamily::Cell:
            cStyle = cCELLSTYLE;
            break;
        default:
            cStyle = ' ';
            break;
        }
        const OUString sSrch = OUStringChar(cStyle) + rName;

        UniqueHash::const_iterator it = maUnique.find(sSrch);
        if (it != maUnique.end())
        {
            sal_uInt32 nIdx = it->second;
            assert (nIdx < maImpl.size());
            assert (maImpl.size() == maUnique.size());
            return nIdx;
        }
    }
    return SAL_MAX_UINT32;
}

void SwStyleSheetIterator::SwPoolFormatList::rehash()
{
    maUnique.clear();
    for (size_t i = 0; i < maImpl.size(); i++)
        maUnique[maImpl[i]] = i;
    assert (maImpl.size() == maUnique.size());
}

void SwStyleSheetIterator::SwPoolFormatList::RemoveName(SfxStyleFamily eFam,
                                                     const OUString &rName)
{
    sal_uInt32 nTmpPos = FindName( eFam, rName );
    if( nTmpPos < maImpl.size() )
        maImpl.erase(maImpl.begin() + nTmpPos);

    // assumption: this seldom occurs, the iterator is built, then emptied.
    rehash();
    assert (maImpl.size() == maUnique.size());
}

// Add Strings to the list of templates
void SwStyleSheetIterator::SwPoolFormatList::Append( char cChar, const OUString& rStr )
{
    const OUString aStr = OUStringChar(cChar) + rStr;

    UniqueHash::const_iterator it = maUnique.find(aStr);
    if (it != maUnique.end())
        return;

    maUnique[aStr] = static_cast<sal_uInt32>(maImpl.size());
    maImpl.push_back(aStr);
}

// UI-sided implementation of StyleSheets
// uses the Core-Engine
SwDocStyleSheet::SwDocStyleSheet(   SwDoc&                rDocument,
                                    SwDocStyleSheetPool&  rPool) :

    SfxStyleSheetBase( OUString(), &rPool, SfxStyleFamily::Char, SfxStyleSearchBits::Auto ),
    pCharFormat(nullptr),
    pColl(nullptr),
    pFrameFormat(nullptr),
    pDesc(nullptr),
    pNumRule(nullptr),
    pTableFormat(nullptr),
    pBoxFormat(nullptr),
    rDoc(rDocument),
    aCoreSet(
        rPool.GetPool(),
        svl::Items<
            RES_CHRATR_BEGIN, RES_CHRATR_END - 1,
            RES_PARATR_BEGIN, RES_FRMATR_END - 1,
            RES_UNKNOWNATR_BEGIN, RES_UNKNOWNATR_END - 1,
            // FillAttribute support:
            XATTR_FILL_FIRST, XATTR_FILL_LAST,
            SID_ATTR_BORDER_INNER, SID_ATTR_BORDER_INNER,
            SID_ATTR_PAGE, SID_ATTR_PAGE_EXT1,
            SID_ATTR_PAGE_HEADERSET, SID_ATTR_PAGE_FOOTERSET,
            SID_ATTR_PARA_MODEL, SID_ATTR_PARA_MODEL,
            // Items to hand over XPropertyList things like XColorList,
            // XHatchList, XGradientList, and XBitmapList to the Area TabPage:
            SID_COLOR_TABLE, SID_PATTERN_LIST,
            SID_SWREGISTER_COLLECTION, SID_SWREGISTER_COLLECTION,
            SID_ATTR_PARA_PAGENUM, SID_ATTR_PARA_PAGENUM,
            SID_SWREGISTER_MODE, SID_SWREGISTER_MODE,
            SID_ATTR_BRUSH_CHAR, SID_ATTR_BRUSH_CHAR,
            SID_ATTR_NUMBERING_RULE, SID_ATTR_NUMBERING_RULE,
            SID_ATTR_CHAR_GRABBAG, SID_ATTR_CHAR_GRABBAG,
            SID_ATTR_AUTO_STYLE_UPDATE, SID_ATTR_AUTO_STYLE_UPDATE,
            FN_PARAM_FTN_INFO, FN_PARAM_FTN_INFO,
            FN_COND_COLL, FN_COND_COLL>{}),
    bPhysical(false)
{
    nHelpId = UCHAR_MAX;
}

SwDocStyleSheet::SwDocStyleSheet( const SwDocStyleSheet& ) = default;

SwDocStyleSheet::~SwDocStyleSheet() = default;

void  SwDocStyleSheet::Reset()
{
    aName.clear();
    aFollow.clear();
    aParent.clear();
    SetPhysical(false);
}

void SwDocStyleSheet::SetGrabBagItem(const uno::Any& rVal)
{
    bool bChg = false;
    if (!bPhysical)
        FillStyleSheet(FillPhysical);

    SwFormat* pFormat = nullptr;
    switch (nFamily)
    {
        case SfxStyleFamily::Char:
            pFormat = rDoc.FindCharFormatByName(aName);
            if (pFormat)
            {
                pFormat->SetGrabBagItem(rVal);
                bChg = true;
            }
            break;
        case SfxStyleFamily::Para:
            pFormat = rDoc.FindTextFormatCollByName(aName);
            if (pFormat)
            {
                pFormat->SetGrabBagItem(rVal);
                bChg = true;
            }
            break;
        case SfxStyleFamily::Pseudo:
            {
                SwNumRule* pRule = rDoc.FindNumRulePtr(aName);
                if (pRule)
                {
                    pRule->SetGrabBagItem(rVal);
                    bChg = true;
                }
            }
            break;
        default:
            break;
    }

    if (bChg)
    {
        dynamic_cast<SwDocStyleSheetPool&>(*m_pPool).InvalidateIterator();
        m_pPool->Broadcast(SfxStyleSheetHint(SfxHintId::StyleSheetModified, *this));
        SwEditShell* pSh = rDoc.GetEditShell();
        if (pSh)
            pSh->CallChgLnk();
    }
}

void SwDocStyleSheet::GetGrabBagItem(uno::Any& rVal) const
{
    SwFormat* pFormat = nullptr;
    switch (nFamily)
    {
        case SfxStyleFamily::Char:
            pFormat = rDoc.FindCharFormatByName(aName);
            if (pFormat)
                pFormat->GetGrabBagItem(rVal);
            break;
        case SfxStyleFamily::Para:
            pFormat = rDoc.FindTextFormatCollByName(aName);
            if (pFormat)
                pFormat->GetGrabBagItem(rVal);
            break;
        case SfxStyleFamily::Pseudo:
            {
                SwNumRule* pRule = rDoc.FindNumRulePtr(aName);
                if (pRule)
                    pRule->GetGrabBagItem(rVal);
            }
            break;
        default:
            break;
    }
}
// virtual methods
void SwDocStyleSheet::SetHidden( bool bValue )
{
    bool bChg = false;
    if(!bPhysical)
        FillStyleSheet( FillPhysical );

    SwFormat* pFormat = nullptr;
    switch(nFamily)
    {
        case SfxStyleFamily::Char:
            pFormat = rDoc.FindCharFormatByName( aName );
            if ( pFormat )
            {
                pFormat->SetHidden( bValue );
                bChg = true;
            }
            break;

        case SfxStyleFamily::Para:
            pFormat = rDoc.FindTextFormatCollByName( aName );
            if ( pFormat )
            {
                pFormat->SetHidden( bValue );
                bChg = true;
            }
            break;

        case SfxStyleFamily::Frame:
            pFormat = rDoc.FindFrameFormatByName( aName );
            if ( pFormat )
            {
                pFormat->SetHidden( bValue );
                bChg = true;
            }
            break;

        case SfxStyleFamily::Page:
            {
                SwPageDesc* pPgDesc = rDoc.FindPageDesc(aName);
                if ( pPgDesc )
                {
                    pPgDesc->SetHidden( bValue );
                    bChg = true;
                }
            }
            break;

        case SfxStyleFamily::Pseudo:
            {
                SwNumRule* pRule = rDoc.FindNumRulePtr( aName );
                if ( pRule )
                {
                    pRule->SetHidden( bValue );
                    bChg = true;
                }
            }
            break;

        case SfxStyleFamily::Table:
            {
                SwTableAutoFormat* pTableAutoFormat = rDoc.GetTableStyles().FindAutoFormat( aName );
                if ( pTableAutoFormat )
                {
                    pTableAutoFormat->SetHidden( bValue );
                    bChg = true;
                }
            }
            break;

        default:
            break;
    }

    if( bChg )
    {
        // calling pPool->First() here would be quite slow...
        dynamic_cast<SwDocStyleSheetPool&>(*m_pPool).InvalidateIterator(); // internal list has to be updated
        m_pPool->Broadcast( SfxStyleSheetHint( SfxHintId::StyleSheetModified, *this ) );
        SwEditShell* pSh = rDoc.GetEditShell();
        if( pSh )
            pSh->CallChgLnk();
    }
}

bool SwDocStyleSheet::IsHidden( ) const
{
    bool bRet = false;

    SwFormat* pFormat = nullptr;
    switch(nFamily)
    {
        case SfxStyleFamily::Char:
            pFormat = rDoc.FindCharFormatByName( aName );
            bRet = pFormat && pFormat->IsHidden( );
            break;

        case SfxStyleFamily::Para:
            pFormat = rDoc.FindTextFormatCollByName( aName );
            bRet = pFormat && pFormat->IsHidden( );
            break;

        case SfxStyleFamily::Frame:
            pFormat = rDoc.FindFrameFormatByName( aName );
            bRet = pFormat && pFormat->IsHidden( );
            break;

        case SfxStyleFamily::Page:
            {
                SwPageDesc* pPgDesc = rDoc.FindPageDesc(aName);
                bRet = pPgDesc && pPgDesc->IsHidden( );
            }
            break;
        case SfxStyleFamily::Pseudo:
            {
                SwNumRule* pRule = rDoc.FindNumRulePtr( aName );
                bRet = pRule && pRule->IsHidden( );
            }
            break;
        case SfxStyleFamily::Table:
            {
                SwTableAutoFormat* pTableAutoFormat = rDoc.GetTableStyles().FindAutoFormat( aName );
                bRet = pTableAutoFormat && pTableAutoFormat->IsHidden( );
            }
            break;
        default:
            break;
    }

    return bRet;
}

const OUString&  SwDocStyleSheet::GetParent() const
{
    if( !bPhysical )
    {
        // check if it's already in document
        SwFormat* pFormat = nullptr;
        SwGetPoolIdFromName eGetType;
        switch(nFamily)
        {
        case SfxStyleFamily::Char:
            pFormat = rDoc.FindCharFormatByName( aName );
            eGetType = SwGetPoolIdFromName::ChrFmt;
            break;

        case SfxStyleFamily::Para:
            pFormat = rDoc.FindTextFormatCollByName( aName );
            eGetType = SwGetPoolIdFromName::TxtColl;
            break;

        case SfxStyleFamily::Frame:
            pFormat = rDoc.FindFrameFormatByName( aName );
            eGetType = SwGetPoolIdFromName::FrmFmt;
            break;

        case SfxStyleFamily::Page:
        case SfxStyleFamily::Pseudo:
        default:
            {
                static const OUString sEmpty;
                return sEmpty; // there's no parent
            }
        }

        OUString sTmp;
        if( !pFormat )         // not yet there, so default Parent
        {
            sal_uInt16 i = SwStyleNameMapper::GetPoolIdFromUIName( aName, eGetType );
            i = ::GetPoolParent( i );
            if( i && USHRT_MAX != i )
                SwStyleNameMapper::FillUIName( i, sTmp );
        }
        else
        {
            SwFormat* p = pFormat->DerivedFrom();
            if( p && !p->IsDefault() )
                sTmp = p->GetName();
        }
        SwDocStyleSheet* pThis = const_cast<SwDocStyleSheet*>(this);
        pThis->aParent = sTmp;
    }
    return aParent;
}

// Follower
const OUString&  SwDocStyleSheet::GetFollow() const
{
    if( !bPhysical )
    {
        SwDocStyleSheet* pThis = const_cast<SwDocStyleSheet*>(this);
        pThis->FillStyleSheet( FillAllInfo );
    }
    return aFollow;
}

// What Linkage is possible
bool  SwDocStyleSheet::HasFollowSupport() const
{
    switch(nFamily)
    {
        case SfxStyleFamily::Para :
        case SfxStyleFamily::Page : return true;
        case SfxStyleFamily::Frame:
        case SfxStyleFamily::Char :
        case SfxStyleFamily::Pseudo: return false;
        default:
            OSL_ENSURE(false, "unknown style family");
    }
    return false;
}

// Parent ?
bool  SwDocStyleSheet::HasParentSupport() const
{
    bool bRet = false;
    switch(nFamily)
    {
        case SfxStyleFamily::Char :
        case SfxStyleFamily::Para :
        case SfxStyleFamily::Frame: bRet = true;
            break;
        default:; //prevent warning
    }
    return bRet;
}

bool  SwDocStyleSheet::HasClearParentSupport() const
{
    bool bRet = false;
    switch(nFamily)
    {
        case SfxStyleFamily::Para :
        case SfxStyleFamily::Char :
        case SfxStyleFamily::Frame: bRet = true;
            break;
        default:; //prevent warning
    }
    return bRet;
}

// determine textual description
OUString  SwDocStyleSheet::GetDescription(MapUnit eUnit)
{
    IntlWrapper aIntlWrapper(SvtSysLocale().GetUILanguageTag());

    const OUString sPlus(" + ");
    if ( SfxStyleFamily::Page == nFamily )
    {
        if( !pSet )
            GetItemSet();

        SfxItemIter aIter( *pSet );
        OUStringBuffer aDesc;

        for (const SfxPoolItem* pItem = aIter.GetCurItem(); pItem; pItem = aIter.NextItem())
        {
            if(!IsInvalidItem(pItem))
            {
                switch ( pItem->Which() )
                {
                    case RES_LR_SPACE:
                    case SID_ATTR_PAGE_SIZE:
                    case SID_ATTR_PAGE_MAXSIZE:
                    case SID_ATTR_PAGE_PAPERBIN:
                    case SID_ATTR_BORDER_INNER:
                        break;
                    default:
                    {
                        OUString aItemPresentation;
                        if ( !IsInvalidItem( pItem ) &&
                             m_pPool->GetPool().GetPresentation(
                                *pItem, eUnit, aItemPresentation, aIntlWrapper ) )
                        {
                            if ( !aDesc.isEmpty() && !aItemPresentation.isEmpty() )
                                aDesc.append(sPlus);
                            aDesc.append(aItemPresentation);
                        }
                    }
                }
            }
        }
        return aDesc.makeStringAndClear();
    }

    if ( SfxStyleFamily::Frame == nFamily || SfxStyleFamily::Para == nFamily || SfxStyleFamily::Char == nFamily )
    {
        if( !pSet )
            GetItemSet();

        SfxItemIter aIter( *pSet );
        OUStringBuffer aDesc;
        OUString sPageNum;
        OUString sModel;
        OUString sBreak;
        bool bHasWesternFontPrefix = false;
        bool bHasCJKFontPrefix = false;
        bool bHasCTLFontPrefix = false;
        SvtCJKOptions aCJKOptions;
        SvtCTLOptions aCTLOptions;

        // Get currently used FillStyle and remember, also need the XFillFloatTransparenceItem
        // to decide if gradient transparence is used
        const drawing::FillStyle eFillStyle(pSet->Get(XATTR_FILLSTYLE).GetValue());
        const bool bUseFloatTransparence(pSet->Get(XATTR_FILLFLOATTRANSPARENCE).IsEnabled());

        for (const SfxPoolItem* pItem = aIter.GetCurItem(); pItem; pItem = aIter.NextItem())
        {
            if(!IsInvalidItem(pItem))
            {
                switch ( pItem->Which() )
                {
                    case SID_ATTR_AUTO_STYLE_UPDATE:
                    case RES_PAGEDESC:
                        break;
                    default:
                    {
                        OUString aItemPresentation;
                        if ( !IsInvalidItem( pItem ) &&
                             m_pPool->GetPool().GetPresentation(
                                *pItem, eUnit, aItemPresentation, aIntlWrapper ) )
                        {
                            bool bIsDefault = false;
                            switch ( pItem->Which() )
                            {
                                case XATTR_FILLCOLOR:
                                {
                                    // only use active FillStyle information
                                    bIsDefault = (drawing::FillStyle_SOLID == eFillStyle);
                                    break;
                                }
                                case XATTR_FILLGRADIENT:
                                {
                                    // only use active FillStyle information
                                    bIsDefault = (drawing::FillStyle_GRADIENT == eFillStyle);
                                    break;
                                }
                                case XATTR_FILLHATCH:
                                {
                                    // only use active FillStyle information
                                    bIsDefault = (drawing::FillStyle_HATCH == eFillStyle);
                                    break;
                                }
                                case XATTR_FILLBITMAP:
                                {
                                    // only use active FillStyle information
                                    bIsDefault = (drawing::FillStyle_BITMAP == eFillStyle);
                                    break;
                                }
                                case XATTR_FILLTRANSPARENCE:
                                {
                                    // only active when not FloatTransparence
                                    bIsDefault = !bUseFloatTransparence;
                                    break;
                                }
                                case XATTR_FILLFLOATTRANSPARENCE:
                                {
                                    // only active when FloatTransparence
                                    bIsDefault = bUseFloatTransparence;
                                    break;
                                }

                                case SID_ATTR_PARA_PAGENUM:
                                    sPageNum = aItemPresentation;
                                    break;
                                case SID_ATTR_PARA_MODEL:
                                    sModel = aItemPresentation;
                                    break;
                                case RES_BREAK:
                                    sBreak = aItemPresentation;
                                    break;
                                case RES_CHRATR_CJK_FONT:
                                case RES_CHRATR_CJK_FONTSIZE:
                                case RES_CHRATR_CJK_LANGUAGE:
                                case RES_CHRATR_CJK_POSTURE:
                                case RES_CHRATR_CJK_WEIGHT:
                                if(aCJKOptions.IsCJKFontEnabled())
                                    bIsDefault = true;
                                if(!bHasCJKFontPrefix)
                                {
                                    aItemPresentation = SwResId(STR_CJK_FONT) + aItemPresentation;
                                    bHasCJKFontPrefix = true;
                                }
                                break;
                                case RES_CHRATR_CTL_FONT:
                                case RES_CHRATR_CTL_FONTSIZE:
                                case RES_CHRATR_CTL_LANGUAGE:
                                case RES_CHRATR_CTL_POSTURE:
                                case RES_CHRATR_CTL_WEIGHT:
                                if(aCTLOptions.IsCTLFontEnabled())
                                    bIsDefault = true;
                                if(!bHasCTLFontPrefix)
                                {
                                    aItemPresentation = SwResId(STR_CTL_FONT) + aItemPresentation;
                                    bHasCTLFontPrefix = true;
                                }
                                break;
                                case RES_CHRATR_FONT:
                                case RES_CHRATR_FONTSIZE:
                                case RES_CHRATR_LANGUAGE:
                                case RES_CHRATR_POSTURE:
                                case RES_CHRATR_WEIGHT:
                                if(!bHasWesternFontPrefix)
                                {
                                    aItemPresentation = SwResId(STR_WESTERN_FONT) + aItemPresentation;
                                    bHasWesternFontPrefix = true;
                                }
                                [[fallthrough]];
                                default:
                                    bIsDefault = true;
                            }
                            if(bIsDefault)
                            {
                                if ( !aDesc.isEmpty() && !aItemPresentation.isEmpty() )
                                    aDesc.append(sPlus);
                                aDesc.append(aItemPresentation);
                            }
                        }
                    }
                }
            }
        }
        // Special treatment for Break, Page template and Site offset
        if (!sModel.isEmpty())
        {
            if (!aDesc.isEmpty())
                aDesc.append(sPlus);
            aDesc.append(SwResId(STR_PAGEBREAK)).append(sPlus).append(sModel);
            if (sPageNum != "0")
            {
                aDesc.append(sPlus).append(SwResId(STR_PAGEOFFSET)).append(sPageNum);
            }
        }
        else if (!sBreak.isEmpty()) // Break can be valid only when NO Model
        {
            if (!aDesc.isEmpty())
                aDesc.append(sPlus);
            aDesc.append(sBreak);
        }
        return aDesc.makeStringAndClear();
    }

    if( SfxStyleFamily::Pseudo == nFamily )
    {
        return OUString();
    }

    return SfxStyleSheetBase::GetDescription(eUnit);
}

// Set names
bool  SwDocStyleSheet::SetName(const OUString& rStr, bool bReindexNow)
{
    if( rStr.isEmpty() )
        return false;

    if( aName != rStr )
    {
        if( !SfxStyleSheetBase::SetName(rStr, bReindexNow))
            return false;
    }
    else if(!bPhysical)
        FillStyleSheet( FillPhysical );

    bool bChg = false;
    switch(nFamily)
    {
        case SfxStyleFamily::Char :
        {
            OSL_ENSURE(pCharFormat, "SwCharFormat missing!");
            if( pCharFormat && pCharFormat->GetName() != rStr )
            {
                if (!pCharFormat->GetName().isEmpty())
                    rDoc.RenameFormat(*pCharFormat, rStr);
                else
                    pCharFormat->SetName(rStr);

                bChg = true;
            }
            break;
        }
        case SfxStyleFamily::Para :
        {
            OSL_ENSURE(pColl, "Collection missing!");
            if( pColl && pColl->GetName() != rStr )
            {
                if (!pColl->GetName().isEmpty())
                    rDoc.RenameFormat(*pColl, rStr);
                else
                    pColl->SetName(rStr);

                bChg = true;
            }
            break;
        }
        case SfxStyleFamily::Frame:
        {
            OSL_ENSURE(pFrameFormat, "FrameFormat missing!");
            if( pFrameFormat && pFrameFormat->GetName() != rStr )
            {
                if (!pFrameFormat->GetName().isEmpty())
                    rDoc.RenameFormat(*pFrameFormat, rStr);
                else
                    pFrameFormat->SetName( rStr );

                bChg = true;
            }
            break;
        }
        case SfxStyleFamily::Page :
            OSL_ENSURE(pDesc, "PageDesc missing!");
            if( pDesc && pDesc->GetName() != rStr )
            {
                // Set PageDesc - copy with earlier one - probably not
                // necessary for setting the name. So here we allow a
                // cast.
                SwPageDesc aPageDesc(*const_cast<SwPageDesc*>(pDesc));
                const OUString aOldName(aPageDesc.GetName());

                aPageDesc.SetName( rStr );
                bool const bDoesUndo = rDoc.GetIDocumentUndoRedo().DoesUndo();

                rDoc.GetIDocumentUndoRedo().DoUndo(!aOldName.isEmpty());
                rDoc.ChgPageDesc(aOldName, aPageDesc);
                rDoc.GetIDocumentUndoRedo().DoUndo(bDoesUndo);

                rDoc.getIDocumentState().SetModified();
                bChg = true;
            }
            break;
        case SfxStyleFamily::Pseudo:
            OSL_ENSURE(pNumRule, "NumRule missing!");

            if (pNumRule)
            {
                OUString aOldName = pNumRule->GetName();

                if (!aOldName.isEmpty())
                {
                    if ( aOldName != rStr &&
                         rDoc.RenameNumRule(aOldName, rStr))
                    {
                        pNumRule = rDoc.FindNumRulePtr(rStr);
                        rDoc.getIDocumentState().SetModified();

                        bChg = true;
                    }
                }
                else
                {
                    // #i91400#
                    const_cast<SwNumRule*>(pNumRule)->SetName( rStr, rDoc.getIDocumentListsAccess() );
                    rDoc.getIDocumentState().SetModified();

                    bChg = true;
                }
            }

            break;

        default:
            OSL_ENSURE(false, "unknown style family");
    }

    if( bChg )
    {
        m_pPool->First(nFamily);  // internal list has to be updated
        m_pPool->Broadcast( SfxStyleSheetHint( SfxHintId::StyleSheetModified, *this ) );
        SwEditShell* pSh = rDoc.GetEditShell();
        if( pSh )
            pSh->CallChgLnk();
    }
    return true;
}

// hierarchy of deduction
bool   SwDocStyleSheet::SetParent( const OUString& rStr)
{
    SwFormat* pFormat = nullptr, *pParent = nullptr;
    switch(nFamily)
    {
        case SfxStyleFamily::Char :
            OSL_ENSURE( pCharFormat, "SwCharFormat missing!" );
            if( nullptr != ( pFormat = pCharFormat ) && !rStr.isEmpty() )
                pParent = lcl_FindCharFormat(rDoc, rStr);
            break;

        case SfxStyleFamily::Para :
            OSL_ENSURE( pColl, "Collection missing!");
            if( nullptr != ( pFormat = pColl ) && !rStr.isEmpty() )
                pParent = lcl_FindParaFormat( rDoc, rStr );
            break;

        case SfxStyleFamily::Frame:
            OSL_ENSURE(pFrameFormat, "FrameFormat missing!");
            if( nullptr != ( pFormat = pFrameFormat ) && !rStr.isEmpty() )
                pParent = lcl_FindFrameFormat( rDoc, rStr );
            break;

        case SfxStyleFamily::Page:
        case SfxStyleFamily::Pseudo:
            break;
        default:
            OSL_ENSURE(false, "unknown style family");
    }

    bool bRet = false;
    if( pFormat && pFormat->DerivedFrom() &&
        pFormat->DerivedFrom()->GetName() != rStr )
    {
        {
            SwImplShellAction aTmp( rDoc );
            bRet = pFormat->SetDerivedFrom( pParent );
        }

        if( bRet )
        {
            aParent = rStr;
            m_pPool->Broadcast( SfxStyleSheetHint( SfxHintId::StyleSheetModified,
                            *this ) );
        }
    }

    return bRet;
}

// Set Follower
bool   SwDocStyleSheet::SetFollow( const OUString& rStr)
{
    if( !rStr.isEmpty() && !SfxStyleSheetBase::SetFollow( rStr ))
        return false;

    SwImplShellAction aTmpSh( rDoc );
    switch(nFamily)
    {
    case SfxStyleFamily::Para :
    {
        OSL_ENSURE(pColl, "Collection missing!");
        if( pColl )
        {
            SwTextFormatColl* pFollow = pColl;
            if( !rStr.isEmpty() && nullptr == (pFollow = lcl_FindParaFormat(rDoc, rStr) ))
                pFollow = pColl;

            pColl->SetNextTextFormatColl(*pFollow);
        }
        break;
    }
    case SfxStyleFamily::Page :
    {
        OSL_ENSURE(pDesc, "PageDesc missing!");
        if( pDesc )
        {
            const SwPageDesc* pFollowDesc = !rStr.isEmpty()
                                            ? lcl_FindPageDesc(rDoc, rStr)
                                            : nullptr;
            size_t nId = 0;
            if (pFollowDesc != pDesc->GetFollow() && rDoc.FindPageDesc(pDesc->GetName(), &nId))
            {
                SwPageDesc aDesc( *pDesc );
                aDesc.SetFollow( pFollowDesc );
                rDoc.ChgPageDesc( nId, aDesc );
                pDesc = &rDoc.GetPageDesc( nId );
            }
        }
        break;
    }
    case SfxStyleFamily::Char:
    case SfxStyleFamily::Frame:
    case SfxStyleFamily::Pseudo:
        break;
    default:
        OSL_ENSURE(false, "unknown style family");
    }

    return true;
}

static
std::unique_ptr<SfxItemSet> lcl_SwFormatToFlatItemSet(SwFormat const *const pFormat)
{
    // note: we don't add the odd items that GetItemSet() would add
    // because they don't seem relevant for preview
    std::vector<SfxItemSet const*> sets;
    sets.push_back(&pFormat->GetAttrSet());
    while (SfxItemSet const*const pParent = sets.back()->GetParent())
    {
        sets.push_back(pParent);
    }
    // start by copying top-level parent set
    std::unique_ptr<SfxItemSet> pRet(new SfxItemSet(*sets.back()));
    sets.pop_back();
    for (auto iter = sets.rbegin(); iter != sets.rend(); ++iter)
    {   // in reverse so child overrides parent
        pRet->Put(**iter);
    }
    return pRet;
}

std::unique_ptr<SfxItemSet> SwDocStyleSheet::GetItemSetForPreview()
{
    if (SfxStyleFamily::Page == nFamily || SfxStyleFamily::Pseudo == nFamily || SfxStyleFamily::Table == nFamily)
    {
        SAL_WARN("sw.ui", "GetItemSetForPreview not implemented for page or number or table style");
        return std::unique_ptr<SfxItemSet>();
    }
    if (!bPhysical)
    {
        // because not only this style, but also any number of its parents
        // (or follow style) may not actually exist in the document at this
        // time, return one "flattened" item set that contains all items from
        // all parents.
        std::unique_ptr<SfxItemSet> pRet;

        bool bModifiedEnabled = rDoc.getIDocumentState().IsEnableSetModified();
        rDoc.getIDocumentState().SetEnableSetModified(false);

        FillStyleSheet(FillPreview, &pRet);

        rDoc.getIDocumentState().SetEnableSetModified(bModifiedEnabled);

        assert(pRet);
        return pRet;
    }
    else
    {
        switch (nFamily)
        {
            case SfxStyleFamily::Char:
                return lcl_SwFormatToFlatItemSet(pCharFormat);
            case SfxStyleFamily::Para:
                return lcl_SwFormatToFlatItemSet(pColl);
            case SfxStyleFamily::Frame:
                return lcl_SwFormatToFlatItemSet(pFrameFormat);
            default:
                std::abort();
        }
    }
}

// extract ItemSet to Name and Family, Mask

SfxItemSet&   SwDocStyleSheet::GetItemSet()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );

    switch(nFamily)
    {
        case SfxStyleFamily::Char:
        case SfxStyleFamily::Para:
        case SfxStyleFamily::Frame:
            {
                SvxBoxInfoItem aBoxInfo( SID_ATTR_BORDER_INNER );
                aBoxInfo.SetTable( false );
                aBoxInfo.SetDist( true );   // always show gap field
                aBoxInfo.SetMinDist( true );// set minimum size in tables and paragraphs
                aBoxInfo.SetDefDist( MIN_BORDER_DIST );// always set Default-Gap
                    // Single lines can only have DontCare-Status in tables
                aBoxInfo.SetValid( SvxBoxInfoItemValidFlags::DISABLE );

                if( nFamily == SfxStyleFamily::Char )
                {
                    SAL_WARN_IF(!pCharFormat, "sw.ui", "Where's SwCharFormat");
                    aCoreSet.Put(pCharFormat->GetAttrSet());
                    aCoreSet.Put( aBoxInfo );

                    if(pCharFormat->DerivedFrom())
                        aCoreSet.SetParent(&pCharFormat->DerivedFrom()->GetAttrSet());
                }
                else if ( nFamily == SfxStyleFamily::Para )
                {
                    OSL_ENSURE(pColl, "Where's Collection");
                    aCoreSet.Put(pColl->GetAttrSet());
                    aCoreSet.Put( aBoxInfo );
                    aCoreSet.Put(SfxBoolItem(SID_ATTR_AUTO_STYLE_UPDATE, pColl->IsAutoUpdateFormat()));

                    if(pColl->DerivedFrom())
                        aCoreSet.SetParent(&pColl->DerivedFrom()->GetAttrSet());
                }
                else
                {
                    OSL_ENSURE(pFrameFormat, "Where's FrameFormat");
                    aCoreSet.Put(pFrameFormat->GetAttrSet());
                    aCoreSet.Put( aBoxInfo );
                    aCoreSet.Put(SfxBoolItem(SID_ATTR_AUTO_STYLE_UPDATE, pFrameFormat->IsAutoUpdateFormat()));

                    if(pFrameFormat->DerivedFrom())
                        aCoreSet.SetParent(&pFrameFormat->DerivedFrom()->GetAttrSet());

                    // create needed items for XPropertyList entries from the DrawModel so that
                    // the Area TabPage can access them
                    const SwDrawModel* pDrawModel = rDoc.getIDocumentDrawModelAccess().GetDrawModel();

                    aCoreSet.Put(SvxColorListItem(pDrawModel->GetColorList(), SID_COLOR_TABLE));
                    aCoreSet.Put(SvxGradientListItem(pDrawModel->GetGradientList(), SID_GRADIENT_LIST));
                    aCoreSet.Put(SvxHatchListItem(pDrawModel->GetHatchList(), SID_HATCH_LIST));
                    aCoreSet.Put(SvxBitmapListItem(pDrawModel->GetBitmapList(), SID_BITMAP_LIST));
                    aCoreSet.Put(SvxPatternListItem(pDrawModel->GetPatternList(), SID_PATTERN_LIST));
                }
            }
            break;

        case SfxStyleFamily::Page :
            {
                // set correct parent to get the drawing::FillStyle_NONE FillStyle as needed
                if(!aCoreSet.GetParent())
                {
                    aCoreSet.SetParent(&rDoc.GetDfltFrameFormat()->GetAttrSet());
                }

                OSL_ENSURE(pDesc, "No PageDescriptor");
                ::PageDescToItemSet(*const_cast<SwPageDesc*>(pDesc), aCoreSet);
            }
            break;

        case SfxStyleFamily::Pseudo:
            {
                OSL_ENSURE(pNumRule, "No NumRule");
                SvxNumRule aRule = pNumRule->MakeSvxNumRule();
                aCoreSet.Put(SvxNumBulletItem(aRule));
            }
            break;

        default:
            OSL_ENSURE(false, "unknown style family");
    }
    // Member of Baseclass
    pSet = &aCoreSet;

    return aCoreSet;
}

void SwDocStyleSheet::MergeIndentAttrsOfListStyle( SfxItemSet& rSet )
{
    if ( nFamily != SfxStyleFamily::Para )
    {
        return;
    }

    OSL_ENSURE( pColl, "<SwDocStyleSheet::MergeIndentAttrsOfListStyle(..)> - missing paragraph style");
    if ( !pColl->AreListLevelIndentsApplicable() )
        return;

    OSL_ENSURE( pColl->GetItemState( RES_PARATR_NUMRULE ) == SfxItemState::SET,
            "<SwDocStyleSheet::MergeIndentAttrsOfListStyle(..)> - list level indents are applicable at paragraph style, but no list style found. Serious defect." );
    const OUString sNumRule = pColl->GetNumRule().GetValue();
    if (sNumRule.isEmpty())
        return;

    const SwNumRule* pRule = rDoc.FindNumRulePtr( sNumRule );
    if( pRule )
    {
        const SwNumFormat& rFormat = pRule->Get( 0 );
        if ( rFormat.GetPositionAndSpaceMode() == SvxNumberFormat::LABEL_ALIGNMENT )
        {
            SvxLRSpaceItem aLR( RES_LR_SPACE );
            aLR.SetTextLeft( rFormat.GetIndentAt() );
            aLR.SetTextFirstLineOffset( static_cast<short>(rFormat.GetFirstLineIndent()) );
            rSet.Put( aLR );
        }
    }
}

// handling of parameter <bResetIndentAttrsAtParagraphStyle>
void SwDocStyleSheet::SetItemSet( const SfxItemSet& rSet,
                                  const bool bResetIndentAttrsAtParagraphStyle )
{
    // if applicable determine format first
    if(!bPhysical)
        FillStyleSheet( FillPhysical );

    SwImplShellAction aTmpSh( rDoc );

    OSL_ENSURE( &rSet != &aCoreSet, "SetItemSet with own Set is not allowed" );

    if (rDoc.GetIDocumentUndoRedo().DoesUndo())
    {
        SwRewriter aRewriter;
        aRewriter.AddRule( UndoArg1, GetName() );
        rDoc.GetIDocumentUndoRedo().StartUndo( SwUndoId::INSFMTATTR, &aRewriter );
    }

    SwFormat* pFormat = nullptr;
    std::unique_ptr<SwPageDesc> pNewDsc;
    size_t nPgDscPos = 0;

    switch(nFamily)
    {
        case SfxStyleFamily::Char :
            {
                OSL_ENSURE(pCharFormat, "Where's CharFormat");
                pFormat = pCharFormat;
            }
            break;

        case SfxStyleFamily::Para :
        {
            OSL_ENSURE(pColl, "Where's Collection");
            const SfxPoolItem* pAutoUpdate;
            if(SfxItemState::SET == rSet.GetItemState(SID_ATTR_AUTO_STYLE_UPDATE,false, &pAutoUpdate ))
            {
                pColl->SetAutoUpdateFormat(static_cast<const SfxBoolItem*>(pAutoUpdate)->GetValue());
            }

            const SwCondCollItem* pCondItem;
            if( SfxItemState::SET != rSet.GetItemState( FN_COND_COLL, false,
                reinterpret_cast<const SfxPoolItem**>(&pCondItem) ))
                pCondItem = nullptr;

            if( RES_CONDTXTFMTCOLL == pColl->Which() && pCondItem )
            {
                const CommandStruct* pCmds = SwCondCollItem::GetCmds();
                for(sal_uInt16 i = 0; i < COND_COMMAND_COUNT; i++)
                {
                    SwCollCondition aCond( nullptr, pCmds[ i ].nCnd, pCmds[ i ].nSubCond );
                    static_cast<SwConditionTextFormatColl*>(pColl)->RemoveCondition( aCond );
                    const OUString sStyle = pCondItem->GetStyle( i );
                    if (sStyle.isEmpty())
                        continue;
                    SwFormat *const pFindFormat = lcl_FindParaFormat( rDoc, sStyle );
                    if (pFindFormat)
                    {
                        aCond.RegisterToFormat( *pFindFormat );
                        static_cast<SwConditionTextFormatColl*>(pColl)->InsertCondition( aCond );
                    }
                }

                pColl->GetNotifier().Broadcast(sw::CondCollCondChg(*pColl));
            }
            else if( pCondItem && !pColl->HasWriterListeners() )
            {
                // no conditional template, then first create and adopt
                // all important values
                SwConditionTextFormatColl* pCColl = rDoc.MakeCondTextFormatColl(
                        pColl->GetName(), static_cast<SwTextFormatColl*>(pColl->DerivedFrom()) );
                if( pColl != &pColl->GetNextTextFormatColl() )
                    pCColl->SetNextTextFormatColl( pColl->GetNextTextFormatColl() );

                if( pColl->IsAssignedToListLevelOfOutlineStyle())
                    pCColl->AssignToListLevelOfOutlineStyle(pColl->GetAssignedOutlineStyleLevel());
                else
                    pCColl->DeleteAssignmentToListLevelOfOutlineStyle();

                const CommandStruct* pCmds = SwCondCollItem::GetCmds();
                for( sal_uInt16 i = 0; i < COND_COMMAND_COUNT; ++i )
                {
                    const OUString sStyle = pCondItem->GetStyle( i );
                    if (sStyle.isEmpty())
                        continue;
                    SwTextFormatColl *const pFindFormat = lcl_FindParaFormat( rDoc, sStyle );
                    if (pFindFormat)
                    {
                        pCColl->InsertCondition( SwCollCondition( pFindFormat,
                                    pCmds[ i ].nCnd, pCmds[ i ].nSubCond ) );
                    }
                }

                rDoc.DelTextFormatColl( pColl );
                pColl = pCColl;
            }
            if ( bResetIndentAttrsAtParagraphStyle &&
                 rSet.GetItemState( RES_PARATR_NUMRULE, false ) == SfxItemState::SET &&
                 rSet.GetItemState( RES_LR_SPACE, false ) != SfxItemState::SET &&
                 pColl->GetItemState( RES_LR_SPACE, false ) == SfxItemState::SET )
            {
                rDoc.ResetAttrAtFormat( RES_LR_SPACE, *pColl );
            }

            // #i56252: If a standard numbering style is assigned to a standard paragraph style
            // we have to create a physical instance of the numbering style. If we do not and
            // neither the paragraph style nor the numbering style is used in the document
            // the numbering style will not be saved with the document and the assignment got lost.
            const SfxPoolItem* pNumRuleItem = nullptr;
            if( SfxItemState::SET == rSet.GetItemState( RES_PARATR_NUMRULE, false, &pNumRuleItem ) )
            {   // Setting a numbering rule?
                const OUString sNumRule = static_cast<const SwNumRuleItem*>(pNumRuleItem)->GetValue();
                if (!sNumRule.isEmpty())
                {
                    SwNumRule* pRule = rDoc.FindNumRulePtr( sNumRule );
                    if( !pRule )
                    {   // Numbering rule not in use yet.
                        sal_uInt16 nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( sNumRule, SwGetPoolIdFromName::NumRule );
                        if( USHRT_MAX != nPoolId ) // It's a standard numbering rule
                        {
                            rDoc.getIDocumentStylePoolAccess().GetNumRuleFromPool( nPoolId ); // Create numbering rule (physical)
                        }
                    }
                }
            }

            pFormat = pColl;

            sal_uInt16 nId = pColl->GetPoolFormatId() &
                            ~ ( COLL_GET_RANGE_BITS | POOLGRP_NOCOLLID );
            switch( GetMask() & ( static_cast<SfxStyleSearchBits>(0x0fff) & ~SfxStyleSearchBits::SwCondColl ) )
            {
                case SfxStyleSearchBits::SwText:
                    nId |= COLL_TEXT_BITS;
                    break;
                case SfxStyleSearchBits::SwChapter:
                    nId |= COLL_DOC_BITS;
                    break;
                case SfxStyleSearchBits::SwList:
                    nId |= COLL_LISTS_BITS;
                    break;
                case SfxStyleSearchBits::SwIndex:
                    nId |= COLL_REGISTER_BITS;
                    break;
                case SfxStyleSearchBits::SwExtra:
                    nId |= COLL_EXTRA_BITS;
                    break;
                case SfxStyleSearchBits::SwHtml:
                    nId |= COLL_HTML_BITS;
                    break;
                default: break;
            }
            pColl->SetPoolFormatId( nId );
            break;
        }
        case SfxStyleFamily::Frame:
        {
            OSL_ENSURE(pFrameFormat, "Where's FrameFormat");
            const SfxPoolItem* pAutoUpdate;
            if(SfxItemState::SET == rSet.GetItemState(SID_ATTR_AUTO_STYLE_UPDATE,false, &pAutoUpdate ))
            {
                pFrameFormat->SetAutoUpdateFormat(static_cast<const SfxBoolItem*>(pAutoUpdate)->GetValue());
            }
            pFormat = pFrameFormat;
        }
        break;

        case SfxStyleFamily::Page :
            {
                OSL_ENSURE(pDesc, "Where's PageDescriptor");

                if (rDoc.FindPageDesc(pDesc->GetName(), &nPgDscPos))
                {
                    pNewDsc.reset( new SwPageDesc( *pDesc ) );
                    // #i48949# - no undo actions for the
                    // copy of the page style
                    ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
                    rDoc.CopyPageDesc(*pDesc, *pNewDsc); // #i7983#

                    pFormat = &pNewDsc->GetMaster();
                }
            }
            break;

        case SfxStyleFamily::Pseudo:
            {
                OSL_ENSURE(pNumRule, "Where's NumRule");

                if (!pNumRule)
                    break;

                const SfxPoolItem* pItem;
                switch( rSet.GetItemState( SID_ATTR_NUMBERING_RULE, false, &pItem ))
                {
                case SfxItemState::SET:
                {
                    SvxNumRule* pSetRule = static_cast<const SvxNumBulletItem*>(pItem)->GetNumRule();
                    pSetRule->UnLinkGraphics();
                    SwNumRule aSetRule(*pNumRule);
                    aSetRule.SetSvxRule(*pSetRule, &rDoc);
                    rDoc.ChgNumRuleFormats( aSetRule );
                }
                break;
                case SfxItemState::DONTCARE:
                // set NumRule to default values
                // what are the default values?
                {
                    SwNumRule aRule( pNumRule->GetName(),
                                     // #i89178#
                                     numfunc::GetDefaultPositionAndSpaceMode() );
                    rDoc.ChgNumRuleFormats( aRule );
                }
                break;
                default: break;
                }
            }
            break;

        default:
            OSL_ENSURE(false, "unknown style family");
    }

    if( pFormat && rSet.Count())
    {
        SfxItemIter aIter( rSet );
        const SfxPoolItem* pItem = aIter.GetCurItem();
        do
        {
            if( IsInvalidItem( pItem ) )            // Clear
            {
                // use method <SwDoc::ResetAttrAtFormat(..)> in order to
                // create an Undo object for the attribute reset.
                rDoc.ResetAttrAtFormat( rSet.GetWhichByPos(aIter.GetCurPos()),
                                        *pFormat );
            }

            pItem = aIter.NextItem();
        } while (pItem);
        SfxItemSet aSet(rSet);
        aSet.ClearInvalidItems();

        if(SfxStyleFamily::Frame == nFamily)
        {
            // Need to check for unique item for DrawingLayer items of type NameOrIndex
            // and evtl. correct that item to ensure unique names for that type. This call may
            // modify/correct entries inside of the given SfxItemSet
            rDoc.CheckForUniqueItemForLineFillNameOrIndex(aSet);
        }

        aCoreSet.ClearItem();

        if( pNewDsc )
        {
            ::ItemSetToPageDesc( aSet, *pNewDsc );
            rDoc.ChgPageDesc( nPgDscPos, *pNewDsc );
            pDesc = &rDoc.GetPageDesc( nPgDscPos );
            rDoc.PreDelPageDesc(pNewDsc.get()); // #i7983#
            pNewDsc.reset();
        }
        else
            rDoc.ChgFormat(*pFormat, aSet);       // put all that is set
    }
    else
    {
        aCoreSet.ClearItem();
        if( pNewDsc )       // we still need to delete it
        {
            rDoc.PreDelPageDesc(pNewDsc.get()); // #i7983#
            pNewDsc.reset();
        }
    }

    if (rDoc.GetIDocumentUndoRedo().DoesUndo())
    {
        rDoc.GetIDocumentUndoRedo().EndUndo(SwUndoId::END, nullptr);
    }
}

static void lcl_SaveStyles( SfxStyleFamily nFamily, std::vector<void*>& rArr, SwDoc& rDoc )
{
    switch( nFamily )
    {
    case SfxStyleFamily::Char:
        {
            const SwCharFormats& rTable = *rDoc.GetCharFormats();
            for( size_t n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                rArr.push_back( rTable[ n ] );
            }
        }
        break;
    case SfxStyleFamily::Para:
        {
            const SwTextFormatColls& rTable = *rDoc.GetTextFormatColls();
            for( size_t n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                rArr.push_back( rTable[ n ] );
            }
        }
        break;
    case SfxStyleFamily::Frame:
        {
            const SwFrameFormats& rTable = *rDoc.GetFrameFormats();
            for( size_t n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                rArr.push_back( rTable[ n ] );
            }
        }
        break;

    case SfxStyleFamily::Page:
        {
            for( size_t n = 0, nCnt = rDoc.GetPageDescCnt(); n < nCnt; ++n )
            {
                rArr.push_back( &rDoc.GetPageDesc( n ) );
            }
        }
        break;

    case SfxStyleFamily::Pseudo:
        {
            const SwNumRuleTable& rTable = rDoc.GetNumRuleTable();
            for( size_t n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                rArr.push_back( rTable[ n ] );
            }
        }
        break;
    default: break;
    }
}

static bool lcl_Contains(const std::vector<void*>& rArr, const void* p)
{
    return std::find( rArr.begin(), rArr.end(), p ) != rArr.end();
}

static void lcl_DeleteInfoStyles( SfxStyleFamily nFamily, std::vector<void*> const & rArr, SwDoc& rDoc )
{
    size_t n, nCnt;
    switch( nFamily )
    {
    case SfxStyleFamily::Char:
        {
            std::deque<sal_uInt16> aDelArr;
            const SwCharFormats& rTable = *rDoc.GetCharFormats();
            for( n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                if( !lcl_Contains( rArr, rTable[ n ] ))
                    aDelArr.push_front( n );
            }
            for( n = 0, nCnt = aDelArr.size(); n < nCnt; ++n )
                rDoc.DelCharFormat( aDelArr[ n ] );
        }
        break;

    case SfxStyleFamily::Para :
        {
            std::deque<sal_uInt16> aDelArr;
            const SwTextFormatColls& rTable = *rDoc.GetTextFormatColls();
            for( n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                if( !lcl_Contains( rArr, rTable[ n ] ))
                    aDelArr.push_front( n );
            }
            for( n = 0, nCnt = aDelArr.size(); n < nCnt; ++n )
                rDoc.DelTextFormatColl( aDelArr[ n ] );
        }
        break;

    case SfxStyleFamily::Frame:
        {
            std::deque<SwFrameFormat*> aDelArr;
            const SwFrameFormats& rTable = *rDoc.GetFrameFormats();
            for( n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                if( !lcl_Contains( rArr, rTable[ n ] ))
                    aDelArr.push_front( rTable[ n ] );
            }
            for( n = 0, nCnt = aDelArr.size(); n < nCnt; ++n )
                rDoc.DelFrameFormat( aDelArr[ n ] );
        }
        break;

    case SfxStyleFamily::Page:
        {
            std::deque<size_t> aDelArr;
            for( n = 0, nCnt = rDoc.GetPageDescCnt(); n < nCnt; ++n )
            {
                if( !lcl_Contains( rArr, &rDoc.GetPageDesc( n ) ))
                    aDelArr.push_front( n );
            }
            for( n = 0, nCnt = aDelArr.size(); n < nCnt; ++n )
                rDoc.DelPageDesc( aDelArr[ n ] );
        }
        break;

    case SfxStyleFamily::Pseudo:
        {
            std::deque<SwNumRule*> aDelArr;
            const SwNumRuleTable& rTable = rDoc.GetNumRuleTable();
            for( n = 0, nCnt = rTable.size(); n < nCnt; ++n )
            {
                if( !lcl_Contains( rArr, rTable[ n ] ))
                    aDelArr.push_front( rTable[ n ] );
            }
            for( n = 0, nCnt = aDelArr.size(); n < nCnt; ++n )
                rDoc.DelNumRule( aDelArr[ n ]->GetName() );
        }
        break;
    default: break;
    }
}

// determine the format
bool SwDocStyleSheet::FillStyleSheet(
    FillStyleType const eFType, std::unique_ptr<SfxItemSet> *const o_ppFlatSet)
{
    bool bRet = false;
    sal_uInt16 nPoolId = USHRT_MAX;
    SwFormat* pFormat = nullptr;

    bool bCreate = FillPhysical == eFType;
    bool bDeleteInfo = false;
    bool bFillOnlyInfo = FillAllInfo == eFType || FillPreview == eFType;
    std::vector<void*> aDelArr;
    bool const isModified(rDoc.getIDocumentState().IsModified());

    switch(nFamily)
    {
    case SfxStyleFamily::Char:
        pCharFormat = lcl_FindCharFormat(rDoc, aName, this, bCreate );
        bPhysical = nullptr != pCharFormat;
        if( bFillOnlyInfo && !bPhysical )
        {
            // create style (plus all needed parents) and clean it up
            // later - without affecting the undo/redo stack
            ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
            bDeleteInfo = true;
            ::lcl_SaveStyles( nFamily, aDelArr, rDoc );
            pCharFormat = lcl_FindCharFormat(rDoc, aName, this );
        }

        pFormat = pCharFormat;
        if( !bCreate && !pFormat )
        {
            if( aName == SwResId(STR_POOLCHR_STANDARD))
                nPoolId = 0;
            else
                nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::ChrFmt );
        }

        bRet = nullptr != pCharFormat || USHRT_MAX != nPoolId;

        if( bDeleteInfo )
            pCharFormat = nullptr;
        break;

    case SfxStyleFamily::Para:
        {
            pColl = lcl_FindParaFormat(rDoc, aName, this, bCreate);
            bPhysical = nullptr != pColl;
            if( bFillOnlyInfo && !bPhysical )
            {
                ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
                bDeleteInfo = true;
                ::lcl_SaveStyles( nFamily, aDelArr, rDoc );
                pColl = lcl_FindParaFormat(rDoc, aName, this );
            }

            pFormat = pColl;
            if( pColl )
                PresetFollow( pColl->GetNextTextFormatColl().GetName() );
            else if( !bCreate )
                nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::TxtColl );

            bRet = nullptr != pColl || USHRT_MAX != nPoolId;

            if( bDeleteInfo )
                pColl = nullptr;
        }
        break;

    case SfxStyleFamily::Frame:
        pFrameFormat = lcl_FindFrameFormat(rDoc,  aName, this, bCreate);
        bPhysical = nullptr != pFrameFormat;
        if (bFillOnlyInfo && !bPhysical)
        {
            ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
            bDeleteInfo = true;
            ::lcl_SaveStyles( nFamily, aDelArr, rDoc );
            pFrameFormat = lcl_FindFrameFormat(rDoc, aName, this );
        }
        pFormat = pFrameFormat;
        if( !bCreate && !pFormat )
            nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::FrmFmt );

        bRet = nullptr != pFrameFormat || USHRT_MAX != nPoolId;

        if( bDeleteInfo )
            pFrameFormat = nullptr;
        break;

    case SfxStyleFamily::Page:
        pDesc = lcl_FindPageDesc(rDoc, aName, this, bCreate);
        bPhysical = nullptr != pDesc;
        if( bFillOnlyInfo && !pDesc )
        {
            ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
            bDeleteInfo = true;
            ::lcl_SaveStyles( nFamily, aDelArr, rDoc );
            pDesc = lcl_FindPageDesc( rDoc, aName, this );
        }

        if( pDesc )
        {
            nPoolId = pDesc->GetPoolFormatId();
            nHelpId = pDesc->GetPoolHelpId();
            if( pDesc->GetPoolHlpFileId() != UCHAR_MAX )
                aHelpFile = *rDoc.GetDocPattern( pDesc->GetPoolHlpFileId() );
            else
                aHelpFile.clear();
        }
        else if( !bCreate )
            nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::PageDesc );
        SetMask( (USER_FMT & nPoolId) ? SfxStyleSearchBits::UserDefined : SfxStyleSearchBits::Auto );

        bRet = nullptr != pDesc || USHRT_MAX != nPoolId;
        if( bDeleteInfo )
            pDesc = nullptr;
        break;

    case SfxStyleFamily::Pseudo:
        pNumRule = lcl_FindNumRule(rDoc, aName, this, bCreate);
        bPhysical = nullptr != pNumRule;
        if( bFillOnlyInfo && !pNumRule )
        {
            ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
            bDeleteInfo = true;
            ::lcl_SaveStyles( nFamily, aDelArr, rDoc );
            pNumRule = lcl_FindNumRule( rDoc, aName, this );
        }

        if( pNumRule )
        {
            nPoolId = pNumRule->GetPoolFormatId();
            nHelpId = pNumRule->GetPoolHelpId();
            if( pNumRule->GetPoolHlpFileId() != UCHAR_MAX )
                aHelpFile = *rDoc.GetDocPattern( pNumRule->GetPoolHlpFileId() );
            else
                aHelpFile.clear();
        }
        else if( !bCreate )
            nPoolId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::NumRule );
        SetMask( (USER_FMT & nPoolId) ? SfxStyleSearchBits::UserDefined : SfxStyleSearchBits::Auto );

        bRet = nullptr != pNumRule || USHRT_MAX != nPoolId;

        if( bDeleteInfo )
            pNumRule = nullptr;
        break;

    case SfxStyleFamily::Table:
        pTableFormat = lcl_FindTableStyle(rDoc, aName, this, bCreate);
        SetMask((pTableFormat && pTableFormat->IsUserDefined()) ? SfxStyleSearchBits::UserDefined : SfxStyleSearchBits::Auto);
        bRet = bPhysical = (nullptr != pTableFormat);
        break;

    case SfxStyleFamily::Cell:
        pBoxFormat = lcl_FindCellStyle(rDoc, aName, this);
        bRet = bPhysical = (nullptr != pBoxFormat);
        break;
        default:; //prevent warning
    }

    if( SfxStyleFamily::Char == nFamily ||
        SfxStyleFamily::Para == nFamily ||
        SfxStyleFamily::Frame == nFamily )
    {
        if( pFormat )
            nPoolId = pFormat->GetPoolFormatId();

        SfxStyleSearchBits _nMask = SfxStyleSearchBits::Auto;
        if( pFormat == rDoc.GetDfltCharFormat() )
            _nMask |= SfxStyleSearchBits::ReadOnly;
        else if( USER_FMT & nPoolId )
            _nMask |= SfxStyleSearchBits::UserDefined;

        switch ( COLL_GET_RANGE_BITS & nPoolId )
        {
        case COLL_TEXT_BITS:     _nMask |= SfxStyleSearchBits::SwText;    break;
        case COLL_DOC_BITS :     _nMask |= SfxStyleSearchBits::SwChapter; break;
        case COLL_LISTS_BITS:    _nMask |= SfxStyleSearchBits::SwList;    break;
        case COLL_REGISTER_BITS: _nMask |= SfxStyleSearchBits::SwIndex;   break;
        case COLL_EXTRA_BITS:    _nMask |= SfxStyleSearchBits::SwExtra;   break;
        case COLL_HTML_BITS:     _nMask |= SfxStyleSearchBits::SwHtml;    break;
        }

        if( pFormat )
        {
            OSL_ENSURE( bPhysical, "Format not found" );

            nHelpId = pFormat->GetPoolHelpId();
            if( pFormat->GetPoolHlpFileId() != UCHAR_MAX )
                aHelpFile = *rDoc.GetDocPattern( pFormat->GetPoolHlpFileId() );
            else
                aHelpFile.clear();

            if( RES_CONDTXTFMTCOLL == pFormat->Which() )
                _nMask |= SfxStyleSearchBits::SwCondColl;

            if (FillPreview == eFType)
            {
                assert(o_ppFlatSet);
                *o_ppFlatSet = lcl_SwFormatToFlatItemSet(pFormat);
            }
        }

        SetMask( _nMask );
    }
    if( bDeleteInfo && bFillOnlyInfo )
    {
        ::sw::UndoGuard const ug(rDoc.GetIDocumentUndoRedo());
        ::lcl_DeleteInfoStyles( nFamily, aDelArr, rDoc );
        if (!isModified)
        {
            rDoc.getIDocumentState().ResetModified();
        }
    }
    return bRet;
}

// Create new format in Core
void SwDocStyleSheet::Create()
{
    switch(nFamily)
    {
        case SfxStyleFamily::Char :
            pCharFormat = lcl_FindCharFormat( rDoc, aName );
            if( !pCharFormat )
                pCharFormat = rDoc.MakeCharFormat(aName,
                                            rDoc.GetDfltCharFormat());
            pCharFormat->SetAuto(false);
            break;

        case SfxStyleFamily::Para :
            pColl = lcl_FindParaFormat( rDoc, aName );
            if( !pColl )
            {
                SwTextFormatColl *pPar = (*rDoc.GetTextFormatColls())[0];
                if( nMask & SfxStyleSearchBits::SwCondColl )
                    pColl = rDoc.MakeCondTextFormatColl( aName, pPar );
                else
                    pColl = rDoc.MakeTextFormatColl( aName, pPar );
            }
            break;

        case SfxStyleFamily::Frame:
            pFrameFormat = lcl_FindFrameFormat( rDoc, aName );
            if( !pFrameFormat )
                pFrameFormat = rDoc.MakeFrameFormat(aName, rDoc.GetDfltFrameFormat(), false, false);

            break;

        case SfxStyleFamily::Page :
            pDesc = lcl_FindPageDesc( rDoc, aName );
            if( !pDesc )
            {
                pDesc = rDoc.MakePageDesc(aName);
            }
            break;

        case SfxStyleFamily::Pseudo:
            pNumRule = lcl_FindNumRule( rDoc, aName );
            if( !pNumRule )
            {
                const OUString sTmpNm( aName.isEmpty() ? rDoc.GetUniqueNumRuleName() : aName );
                SwNumRule* pRule = rDoc.GetNumRuleTable()[
                    rDoc.MakeNumRule( sTmpNm, nullptr, false,
                                      // #i89178#
                                      numfunc::GetDefaultPositionAndSpaceMode() ) ];
                pRule->SetAutoRule( false );
                if( aName.isEmpty() )
                {
                    // #i91400#
                    pRule->SetName( aName, rDoc.getIDocumentListsAccess() );
                }
                pNumRule = pRule;
            }
            break;

        case SfxStyleFamily::Table:
            if (aName.isEmpty())
                return;
            pTableFormat = lcl_FindTableStyle(rDoc, aName);
            if (!pTableFormat)
            {
                rDoc.MakeTableStyle(aName);
                pTableFormat = rDoc.GetTableStyles().FindAutoFormat(aName);
                SAL_WARN_IF(!pTableFormat, "sw.ui", "Recently added auto format not found");
            }
            break;
        default:; //prevent warning
    }
    bPhysical = true;
    aCoreSet.ClearItem();
}

SwCharFormat* SwDocStyleSheet::GetCharFormat()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    return pCharFormat;
}

SwTextFormatColl* SwDocStyleSheet::GetCollection()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    return pColl;
}

const SwPageDesc* SwDocStyleSheet::GetPageDesc()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    return pDesc;
}

const SwNumRule * SwDocStyleSheet::GetNumRule()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    return pNumRule;
}


void SwDocStyleSheet::SetNumRule(const SwNumRule& rRule)
{
    OSL_ENSURE(pNumRule, "Where is the NumRule");
    rDoc.ChgNumRuleFormats( rRule );
}

SwTableAutoFormat* SwDocStyleSheet::GetTableFormat()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    assert(pTableFormat && "SwDocStyleSheet table style, SwTableAutoFormat not found");
    return pTableFormat;
}

// re-generate Name AND Family from String
// First() and Next() (see below) insert an identification letter at Pos.1

void SwDocStyleSheet::PresetNameAndFamily(const OUString& rName)
{
    switch( rName[0] )
    {
    case cPARA:     nFamily = SfxStyleFamily::Para; break;
    case cFRAME:    nFamily = SfxStyleFamily::Frame; break;
    case cPAGE:     nFamily = SfxStyleFamily::Page; break;
    case cNUMRULE:  nFamily = SfxStyleFamily::Pseudo; break;
    case cTABSTYLE: nFamily = SfxStyleFamily::Table; break;
    default:        nFamily = SfxStyleFamily::Char; break;
    }
    aName = rName.copy(1);
}

// Is the format physically present yet
void SwDocStyleSheet::SetPhysical(bool bPhys)
{
    bPhysical = bPhys;

    if(!bPhys)
    {
        pCharFormat = nullptr;
        pColl    = nullptr;
        pFrameFormat  = nullptr;
        pDesc    = nullptr;
    }
}

SwFrameFormat* SwDocStyleSheet::GetFrameFormat()
{
    if(!bPhysical)
        FillStyleSheet( FillPhysical );
    return pFrameFormat;
}

bool  SwDocStyleSheet::IsUsed() const
{
    if( !bPhysical )
    {
        SwDocStyleSheet* pThis = const_cast<SwDocStyleSheet*>(this);
        pThis->FillStyleSheet( FillOnlyName );
    }

    if( !bPhysical )
        return false;

    const sw::BroadcastingModify* pMod;
    switch( nFamily )
    {
    case SfxStyleFamily::Char : pMod = pCharFormat;   break;
    case SfxStyleFamily::Para : pMod = pColl;      break;
    case SfxStyleFamily::Frame: pMod = pFrameFormat;    break;
    case SfxStyleFamily::Page : pMod = pDesc;      break;

    case SfxStyleFamily::Pseudo:
            return pNumRule && rDoc.IsUsed(*pNumRule);

    case SfxStyleFamily::Table:
            return pTableFormat && rDoc.IsUsed( *pTableFormat );

    default:
        OSL_ENSURE(false, "unknown style family");
        return false;
    }
    return rDoc.IsUsed( *pMod );
}

OUString SwDocStyleSheet::GetUsedBy()
{
    return pNumRule ? pNumRule->MakeParagraphStyleListString() : OUString();
}

sal_uLong  SwDocStyleSheet::GetHelpId( OUString& rFile )
{
    sal_uInt16 nId = 0;
    sal_uInt16 nPoolId = 0;
    unsigned char nFileId = UCHAR_MAX;

    rFile = "swrhlppi.hlp";

    const SwFormat* pTmpFormat = nullptr;
    switch( nFamily )
    {
    case SfxStyleFamily::Char :
        if( !pCharFormat &&
            nullptr == (pCharFormat = lcl_FindCharFormat( rDoc, aName, nullptr, false )) )
        {
            nId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::ChrFmt );
            return USHRT_MAX == nId ? 0 : nId;
        }
        pTmpFormat = pCharFormat;
        break;

    case SfxStyleFamily::Para:
        if( !pColl &&
            nullptr == ( pColl = lcl_FindParaFormat( rDoc, aName, nullptr, false )) )
        {
            nId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::TxtColl );
            return USHRT_MAX == nId ? 0 : nId;
        }
        pTmpFormat = pColl;
        break;

    case SfxStyleFamily::Frame:
        if( !pFrameFormat &&
            nullptr == ( pFrameFormat = lcl_FindFrameFormat( rDoc, aName, nullptr, false ) ) )
        {
            nId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::FrmFmt );
            return USHRT_MAX == nId ? 0 : nId;
        }
        pTmpFormat = pFrameFormat;
        break;

    case SfxStyleFamily::Page:
        if( !pDesc &&
            nullptr == ( pDesc = lcl_FindPageDesc( rDoc, aName, nullptr, false ) ) )
        {
            nId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::PageDesc );
            return USHRT_MAX == nId ? 0 : nId;
        }

        nId = pDesc->GetPoolHelpId();
        nFileId = pDesc->GetPoolHlpFileId();
        nPoolId = pDesc->GetPoolFormatId();
        break;

    case SfxStyleFamily::Pseudo:
        if( !pNumRule &&
            nullptr == ( pNumRule = lcl_FindNumRule( rDoc, aName, nullptr, false ) ) )
        {
            nId = SwStyleNameMapper::GetPoolIdFromUIName( aName, SwGetPoolIdFromName::NumRule );
            return USHRT_MAX == nId ? 0 : nId;
        }

        nId = pNumRule->GetPoolHelpId();
        nFileId = pNumRule->GetPoolHlpFileId();
        nPoolId = pNumRule->GetPoolFormatId();
        break;

    default:
        OSL_ENSURE(false, "unknown style family");
        return 0;
    }

    if( pTmpFormat )
    {
        nId = pTmpFormat->GetPoolHelpId();
        nFileId = pTmpFormat->GetPoolHlpFileId();
        nPoolId = pTmpFormat->GetPoolFormatId();
    }

    if( UCHAR_MAX != nFileId )
    {
        const OUString *pTemplate = rDoc.GetDocPattern( nFileId );
        if( pTemplate )
        {
            rFile = *pTemplate;
        }
    }
    else if( !IsPoolUserFormat( nPoolId ) )
    {
        nId = nPoolId;
    }

    // because SFX acts like that, with HelpId:
    if( USHRT_MAX == nId )
        nId = 0;        // don't show Help accordingly

    return nId;
}

void  SwDocStyleSheet::SetHelpId( const OUString& r, sal_uLong nId )
{
    sal_uInt8 nFileId = static_cast< sal_uInt8 >(rDoc.SetDocPattern( r ));
    sal_uInt16 nHId = static_cast< sal_uInt16 >(nId); //!! SFX changed over to ULONG arbitrarily!

    SwFormat* pTmpFormat = nullptr;
    switch( nFamily )
    {
    case SfxStyleFamily::Char : pTmpFormat = pCharFormat;    break;
    case SfxStyleFamily::Para : pTmpFormat = pColl;       break;
    case SfxStyleFamily::Frame: pTmpFormat = pFrameFormat;     break;
    case SfxStyleFamily::Page :
        const_cast<SwPageDesc*>(pDesc)->SetPoolHelpId( nHId );
        const_cast<SwPageDesc*>(pDesc)->SetPoolHlpFileId( nFileId );
        break;

    case SfxStyleFamily::Pseudo:
        const_cast<SwNumRule*>(pNumRule)->SetPoolHelpId( nHId );
        const_cast<SwNumRule*>(pNumRule)->SetPoolHlpFileId( nFileId );
        break;

    default:
        OSL_ENSURE(false, "unknown style family");
        return ;
    }
    if( pTmpFormat )
    {
        pTmpFormat->SetPoolHelpId( nHId );
        pTmpFormat->SetPoolHlpFileId( nFileId );
    }
}

// methods for DocStyleSheetPool
SwDocStyleSheetPool::SwDocStyleSheetPool( SwDoc& rDocument, bool bOrg )
    : SfxStyleSheetBasePool(rDocument.GetAttrPool())
    , mxStyleSheet(new SwDocStyleSheet(rDocument, *this))
    , rDoc(rDocument)
{
    bOrganizer = bOrg;
}

SwDocStyleSheetPool::~SwDocStyleSheetPool()
{
}

SfxStyleSheetBase&   SwDocStyleSheetPool::Make( const OUString&   rName,
                                                SfxStyleFamily  eFam,
                                                SfxStyleSearchBits _nMask)
{
    mxStyleSheet->PresetName(rName);
    mxStyleSheet->PresetParent(OUString());
    mxStyleSheet->PresetFollow(OUString());
    mxStyleSheet->SetMask(_nMask) ;
    mxStyleSheet->SetFamily(eFam);
    mxStyleSheet->SetPhysical(true);
    mxStyleSheet->Create();

    return *mxStyleSheet;
}

SfxStyleSheetBase*   SwDocStyleSheetPool::Create( const SfxStyleSheetBase& /*rOrg*/)
{
    OSL_ENSURE(false , "Create in SW-Stylesheet-Pool not possible" );
    return nullptr;
}

SfxStyleSheetBase*   SwDocStyleSheetPool::Create( const OUString &,
                                                  SfxStyleFamily, SfxStyleSearchBits )
{
    OSL_ENSURE( false, "Create in SW-Stylesheet-Pool not possible" );
    return nullptr;
}

std::unique_ptr<SfxStyleSheetIterator> SwDocStyleSheetPool::CreateIterator( SfxStyleFamily eFam, SfxStyleSearchBits _nMask )
{
    return std::make_unique<SwStyleSheetIterator>(*this, eFam, _nMask);
}

void SwDocStyleSheetPool::dispose()
{
    mxStyleSheet.clear();
}

void SwDocStyleSheetPool::Remove( SfxStyleSheetBase* pStyle)
{
    if( !pStyle )
        return;

    bool bBroadcast = true;
    SwImplShellAction aTmpSh( rDoc );
    const OUString sName = pStyle->GetName();
    switch( pStyle->GetFamily() )
    {
    case SfxStyleFamily::Char:
        {
            SwCharFormat* pFormat = lcl_FindCharFormat(rDoc, sName, nullptr, false );
            if(pFormat)
                rDoc.DelCharFormat(pFormat);
        }
        break;
    case SfxStyleFamily::Para:
        {
            SwTextFormatColl* pColl = lcl_FindParaFormat(rDoc, sName, nullptr, false );
            if(pColl)
                rDoc.DelTextFormatColl(pColl);
        }
        break;
    case SfxStyleFamily::Frame:
        {
            SwFrameFormat* pFormat = lcl_FindFrameFormat(rDoc, sName, nullptr, false );
            if(pFormat)
                rDoc.DelFrameFormat(pFormat);
        }
        break;
    case SfxStyleFamily::Page :
        {
            rDoc.DelPageDesc(sName);
        }
        break;

    case SfxStyleFamily::Pseudo:
        {
            if( !rDoc.DelNumRule( sName ) )
                // Only send Broadcast, when something was deleted
                bBroadcast = false;
        }
        break;

    case SfxStyleFamily::Table:
        {
            rDoc.DelTableStyle(sName);
        }
        break;

    default:
        OSL_ENSURE(false, "unknown style family");
        bBroadcast = false;
    }

    if( bBroadcast )
        Broadcast( SfxStyleSheetHint( SfxHintId::StyleSheetErased, *pStyle ) );
}

bool  SwDocStyleSheetPool::SetParent( SfxStyleFamily eFam,
                                      const OUString &rStyle, const OUString &rParent )
{
    SwFormat* pFormat = nullptr, *pParent = nullptr;
    switch( eFam )
    {
    case SfxStyleFamily::Char :
        if( nullptr != ( pFormat = lcl_FindCharFormat( rDoc, rStyle ) ) && !rParent.isEmpty() )
            pParent = lcl_FindCharFormat(rDoc, rParent );
        break;

    case SfxStyleFamily::Para :
        if( nullptr != ( pFormat = lcl_FindParaFormat( rDoc, rStyle ) ) && !rParent.isEmpty() )
            pParent = lcl_FindParaFormat( rDoc, rParent );
        break;

    case SfxStyleFamily::Frame:
        if( nullptr != ( pFormat = lcl_FindFrameFormat( rDoc, rStyle ) ) && !rParent.isEmpty() )
            pParent = lcl_FindFrameFormat( rDoc, rParent );
        break;

    case SfxStyleFamily::Page:
    case SfxStyleFamily::Pseudo:
        break;

    default:
        OSL_ENSURE(false, "unknown style family");
    }

    bool bRet = false;
    if( pFormat && pFormat->DerivedFrom() &&
        pFormat->DerivedFrom()->GetName() != rParent )
    {
        {
            SwImplShellAction aTmpSh( rDoc );
            bRet = pFormat->SetDerivedFrom( pParent );
        }

        if( bRet )
        {
            // only for Broadcasting
            mxStyleSheet->PresetName( rStyle );
            mxStyleSheet->PresetParent( rParent );
            if( SfxStyleFamily::Para == eFam )
                mxStyleSheet->PresetFollow( static_cast<SwTextFormatColl*>(pFormat)->
                        GetNextTextFormatColl().GetName() );
            else
                mxStyleSheet->PresetFollow( OUString() );

            Broadcast( SfxStyleSheetHint( SfxHintId::StyleSheetModified, *mxStyleSheet ) );
        }
    }

    return bRet;
}

SfxStyleSheetBase* SwDocStyleSheetPool::Find( const OUString& rName,
                                              SfxStyleFamily eFam, SfxStyleSearchBits n )
{
    SfxStyleSearchBits nSMask = n;
    if( SfxStyleFamily::Para == eFam &&  rDoc.getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE) )
    {
        // then only HTML-Templates are of interest
        if( SfxStyleSearchBits::All == nSMask )
            nSMask = SfxStyleSearchBits::SwHtml | SfxStyleSearchBits::UserDefined | SfxStyleSearchBits::Used;
        else
            nSMask &= SfxStyleSearchBits::Used | SfxStyleSearchBits::UserDefined |
                                SfxStyleSearchBits::SwCondColl | SfxStyleSearchBits::SwHtml;
        if( nSMask == SfxStyleSearchBits::Auto )
            nSMask = SfxStyleSearchBits::SwHtml;
    }

    const bool bSearchUsed = ( n != SfxStyleSearchBits::All && n & SfxStyleSearchBits::Used );
    const sw::BroadcastingModify* pMod = nullptr;

    mxStyleSheet->SetPhysical( false );
    mxStyleSheet->PresetName( rName );
    mxStyleSheet->SetFamily( eFam );
    bool bFnd = mxStyleSheet->FillStyleSheet( SwDocStyleSheet::FillOnlyName );

    if( mxStyleSheet->IsPhysical() )
    {
        switch( eFam )
        {
        case SfxStyleFamily::Char:
            pMod = mxStyleSheet->GetCharFormat();
            break;

        case SfxStyleFamily::Para:
            pMod = mxStyleSheet->GetCollection();
            break;

        case SfxStyleFamily::Frame:
            pMod = mxStyleSheet->GetFrameFormat();
            break;

        case SfxStyleFamily::Page:
            pMod = mxStyleSheet->GetPageDesc();
            break;

        case SfxStyleFamily::Pseudo:
            {
                const SwNumRule* pRule = mxStyleSheet->GetNumRule();
                if( pRule &&
                    !bSearchUsed &&
                    (( nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                            ? !(pRule->GetPoolFormatId() & USER_FMT)
                                // searched for used and found none
                            : bSearchUsed ))
                    bFnd = false;
            }
            break;

        case SfxStyleFamily::Table:
        case SfxStyleFamily::Cell:
            break;
        default:
            OSL_ENSURE(false, "unknown style family");
        }
    }

    // then evaluate the mask:
    if( pMod && !bSearchUsed )
    {
        const sal_uInt16 nId = SfxStyleFamily::Page == eFam
                        ? static_cast<const SwPageDesc*>(pMod)->GetPoolFormatId()
                        : static_cast<const SwFormat*>(pMod)->GetPoolFormatId();

        if( ( nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
            ? !(nId & USER_FMT)
                // searched for used and found none
            : bSearchUsed )
            bFnd = false;
    }
    return bFnd ? mxStyleSheet.get() : nullptr;
}

SwStyleSheetIterator::SwStyleSheetIterator(SwDocStyleSheetPool& rBase,
                                SfxStyleFamily eFam, SfxStyleSearchBits n )
    : SfxStyleSheetIterator(&rBase, eFam, n)
    , mxIterSheet(new SwDocStyleSheet(rBase.GetDoc(), rBase))
    , mxStyleSheet(new SwDocStyleSheet(rBase.GetDoc(), rBase))
{
    bFirstCalled = false;
    nLastPos = 0;
    StartListening(rBase);
}

SwStyleSheetIterator::~SwStyleSheetIterator()
{
    EndListening( *mxIterSheet->GetPool() );
}

sal_Int32  SwStyleSheetIterator::Count()
{
    // let the list fill correctly!!
    if( !bFirstCalled )
        First();
    return aLst.size();
}

SfxStyleSheetBase* SwStyleSheetIterator::operator[]( sal_Int32 nIdx )
{
    // found
    if( !bFirstCalled )
        First();
    mxStyleSheet->PresetNameAndFamily( aLst[ nIdx ] );
    mxStyleSheet->SetPhysical( false );
    mxStyleSheet->FillStyleSheet( SwDocStyleSheet::FillOnlyName );

    return mxStyleSheet.get();
}

SfxStyleSheetBase*  SwStyleSheetIterator::First()
{
    // Delete old list
    bFirstCalled = true;
    nLastPos = 0;
    aLst.clear();

    // Delete current
    mxIterSheet->Reset();

    SwDoc& rDoc = static_cast<SwDocStyleSheetPool*>(pBasePool)->GetDoc();
    const SfxStyleSearchBits nSrchMask = nMask;
    const bool bIsSearchUsed = SearchUsed();

    bool bSearchHidden( nMask & SfxStyleSearchBits::Hidden );
    bool bOnlyHidden = nMask == SfxStyleSearchBits::Hidden;

    const bool bOrganizer = static_cast<SwDocStyleSheetPool*>(pBasePool)->IsOrganizerMode();
    bool bAll = ( nSrchMask & SfxStyleSearchBits::AllVisible ) == SfxStyleSearchBits::AllVisible;

    if( nSearchFamily == SfxStyleFamily::Char
     || nSearchFamily == SfxStyleFamily::All )
    {
        const size_t nArrLen = rDoc.GetCharFormats()->size();
        for( size_t i = 0; i < nArrLen; i++ )
        {
            SwCharFormat* pFormat = (*rDoc.GetCharFormats())[ i ];

            const bool bUsed = bIsSearchUsed && (bOrganizer || rDoc.IsUsed(*pFormat));
            if( ( !bSearchHidden && pFormat->IsHidden() && !bUsed ) || ( pFormat->IsDefault() && pFormat != rDoc.GetDfltCharFormat() ) )
                continue;

            if ( nSrchMask == SfxStyleSearchBits::Hidden && !pFormat->IsHidden( ) )
                continue;

            if( !bUsed )
            {
                // Standard is no User template
                const sal_uInt16 nId = rDoc.GetDfltCharFormat() == pFormat ?
                        sal_uInt16( RES_POOLCHR_INET_NORMAL ):
                                pFormat->GetPoolFormatId();
                if( (nSrchMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                    ? !(nId & USER_FMT)
                        // searched for used and found none
                    : bIsSearchUsed )
                {
                    continue;
                }

                if(  rDoc.getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE) && !(nId & USER_FMT) &&
                    ( RES_POOLCHR_HTML_BEGIN > nId || nId >= RES_POOLCHR_HTML_END ) &&
                    RES_POOLCHR_INET_NORMAL != nId &&
                    RES_POOLCHR_INET_VISIT != nId &&
                    RES_POOLCHR_FOOTNOTE  != nId &&
                    RES_POOLCHR_ENDNOTE != nId )
                    continue;
            }

            aLst.Append( cCHAR, pFormat == rDoc.GetDfltCharFormat()
                        ? SwResId(STR_POOLCHR_STANDARD)
                        : pFormat->GetName() );
        }

        // PoolFormat
        if( bAll )
        {
            if( ! rDoc.getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE) )
                AppendStyleList(SwStyleNameMapper::GetChrFormatUINameArray(),
                                bIsSearchUsed, bSearchHidden, bOnlyHidden,
                                SwGetPoolIdFromName::ChrFmt, cCHAR);
            else
            {
                aLst.Append( cCHAR, SwStyleNameMapper::GetChrFormatUINameArray()[
                        RES_POOLCHR_INET_NORMAL - RES_POOLCHR_BEGIN ] );
                aLst.Append( cCHAR, SwStyleNameMapper::GetChrFormatUINameArray()[
                        RES_POOLCHR_INET_VISIT - RES_POOLCHR_BEGIN ] );
                aLst.Append( cCHAR, SwStyleNameMapper::GetChrFormatUINameArray()[
                        RES_POOLCHR_ENDNOTE - RES_POOLCHR_BEGIN ] );
                aLst.Append( cCHAR, SwStyleNameMapper::GetChrFormatUINameArray()[
                        RES_POOLCHR_FOOTNOTE - RES_POOLCHR_BEGIN ] );
            }
            AppendStyleList(SwStyleNameMapper::GetHTMLChrFormatUINameArray(),
                                bIsSearchUsed, bSearchHidden, bOnlyHidden,
                                SwGetPoolIdFromName::ChrFmt, cCHAR);
        }
    }

    if( nSearchFamily == SfxStyleFamily::Para ||
        nSearchFamily == SfxStyleFamily::All )
    {
        SfxStyleSearchBits nSMask = nSrchMask;
        if(  rDoc.getIDocumentSettingAccess().get(DocumentSettingId::HTML_MODE) )
        {
            // then only HTML-Template are of interest
            if( SfxStyleSearchBits::AllVisible == ( nSMask & SfxStyleSearchBits::AllVisible ) )
                nSMask = SfxStyleSearchBits::SwHtml | SfxStyleSearchBits::UserDefined |
                            SfxStyleSearchBits::Used;
            else
                nSMask &= SfxStyleSearchBits::Used | SfxStyleSearchBits::UserDefined |
                                SfxStyleSearchBits::SwCondColl | SfxStyleSearchBits::SwHtml;
            if( nSMask == SfxStyleSearchBits::Auto )
                nSMask = SfxStyleSearchBits::SwHtml;
        }

        const size_t nArrLen = rDoc.GetTextFormatColls()->size();
        for( size_t i = 0; i < nArrLen; i++ )
        {
            SwTextFormatColl* pColl = (*rDoc.GetTextFormatColls())[ i ];

            const bool bUsed = bOrganizer || rDoc.IsUsed(*pColl);
            if ( ( !bSearchHidden && pColl->IsHidden( ) && !bUsed ) || pColl->IsDefault() )
                continue;

            if ( nSMask == SfxStyleSearchBits::Hidden && !pColl->IsHidden( ) )
                continue;

            if( !(bIsSearchUsed && bUsed ))
            {
                const sal_uInt16 nId = pColl->GetPoolFormatId();
                auto tmpMask = nSMask & ~SfxStyleSearchBits::Used;
                if (tmpMask == SfxStyleSearchBits::UserDefined)
                {
                    if(!IsPoolUserFormat(nId)) continue;
                }
                else if (tmpMask == SfxStyleSearchBits::SwText)
                {
                    if((nId & COLL_GET_RANGE_BITS) != COLL_TEXT_BITS) continue;
                }
                else if (tmpMask == SfxStyleSearchBits::SwChapter)
                {
                    if((nId  & COLL_GET_RANGE_BITS) != COLL_DOC_BITS) continue;
                }
                else if (tmpMask == SfxStyleSearchBits::SwList)
                {
                    if((nId  & COLL_GET_RANGE_BITS) != COLL_LISTS_BITS) continue;
                }
                else if (tmpMask == SfxStyleSearchBits::SwIndex)
                {
                    if((nId  & COLL_GET_RANGE_BITS) != COLL_REGISTER_BITS) continue;
                }
                else if (tmpMask == SfxStyleSearchBits::SwExtra)
                {
                    if((nId  & COLL_GET_RANGE_BITS) != COLL_EXTRA_BITS) continue;
                }
                else if (tmpMask == (SfxStyleSearchBits::SwHtml | SfxStyleSearchBits::UserDefined)
                         || tmpMask == SfxStyleSearchBits::SwHtml)
                {
                    if((tmpMask & SfxStyleSearchBits::UserDefined) && IsPoolUserFormat(nId))
                        ; // do nothing
                    else if( (nId & COLL_GET_RANGE_BITS) != COLL_HTML_BITS)
                    {
                        // but some we also want to see in this section
                        bool bContinue = true;
                        switch( nId )
                        {
                        case RES_POOLCOLL_SEND_ADDRESS: //  --> ADDRESS
                        case RES_POOLCOLL_TABLE_HDLN:   //  --> TH
                        case RES_POOLCOLL_TABLE:        //  --> TD
                        case RES_POOLCOLL_TEXT:         //  --> P
                        case RES_POOLCOLL_HEADLINE_BASE://  --> H
                        case RES_POOLCOLL_HEADLINE1:    //  --> H1
                        case RES_POOLCOLL_HEADLINE2:    //  --> H2
                        case RES_POOLCOLL_HEADLINE3:    //  --> H3
                        case RES_POOLCOLL_HEADLINE4:    //  --> H4
                        case RES_POOLCOLL_HEADLINE5:    //  --> H5
                        case RES_POOLCOLL_HEADLINE6:    //  --> H6
                        case RES_POOLCOLL_STANDARD:     //  --> P
                        case RES_POOLCOLL_FOOTNOTE:
                        case RES_POOLCOLL_ENDNOTE:
                            bContinue = false;
                            break;
                        }
                        if( bContinue )
                            continue;
                    }
                }
                else if (tmpMask == SfxStyleSearchBits::SwCondColl)
                {
                    if( RES_CONDTXTFMTCOLL != pColl->Which() ) continue;
                }
                else
                {
                    // searched for used and found none
                    if( bIsSearchUsed )
                        continue;
                }
            }
            aLst.Append( cPARA, pColl->GetName() );
        }

        bAll = ( nSMask & SfxStyleSearchBits::AllVisible ) == SfxStyleSearchBits::AllVisible;
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwText )
            AppendStyleList(SwStyleNameMapper::GetTextUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA );
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwChapter )
            AppendStyleList(SwStyleNameMapper::GetDocUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA ) ;
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwList )
            AppendStyleList(SwStyleNameMapper::GetListsUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA ) ;
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwIndex )
            AppendStyleList(SwStyleNameMapper::GetRegisterUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA ) ;
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwExtra )
            AppendStyleList(SwStyleNameMapper::GetExtraUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA ) ;
        if ( bAll || (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwCondColl )
        {
            if( !bIsSearchUsed ||
                rDoc.getIDocumentStylePoolAccess().IsPoolTextCollUsed( RES_POOLCOLL_TEXT ))
                aLst.Append( cPARA, SwStyleNameMapper::GetTextUINameArray()[
                        RES_POOLCOLL_TEXT - RES_POOLCOLL_TEXT_BEGIN ] );
        }
        if ( bAll ||
            (nSMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::SwHtml ||
            (nSMask & ~SfxStyleSearchBits::Used) ==
                        (SfxStyleSearchBits::SwHtml | SfxStyleSearchBits::UserDefined) )
        {
            AppendStyleList(SwStyleNameMapper::GetHTMLUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::TxtColl, cPARA ) ;
            if( !bAll )
            {
                // then also the ones, that we are mapping:
                static sal_uInt16 aPoolIds[] = {
                    RES_POOLCOLL_SEND_ADDRESS, //  --> ADDRESS
                    RES_POOLCOLL_TABLE_HDLN,    //  --> TH
                    RES_POOLCOLL_TABLE,     //  --> TD
                    RES_POOLCOLL_STANDARD,      //  --> P
                    RES_POOLCOLL_TEXT,          //  --> P
                    RES_POOLCOLL_HEADLINE_BASE, //  --> H
                    RES_POOLCOLL_HEADLINE1, //  --> H1
                    RES_POOLCOLL_HEADLINE2, //  --> H2
                    RES_POOLCOLL_HEADLINE3, //  --> H3
                    RES_POOLCOLL_HEADLINE4, //  --> H4
                    RES_POOLCOLL_HEADLINE5, //  --> H5
                    RES_POOLCOLL_HEADLINE6, //  --> H6
                    RES_POOLCOLL_FOOTNOTE,
                    RES_POOLCOLL_ENDNOTE,
                    0
                    };

                sal_uInt16* pPoolIds = aPoolIds;
                OUString s;
                while( *pPoolIds )
                {
                    if( !bIsSearchUsed || rDoc.getIDocumentStylePoolAccess().IsPoolTextCollUsed( *pPoolIds ) )
                    {
                        s = SwStyleNameMapper::GetUIName( *pPoolIds, s );
                        aLst.Append( cPARA, s);
                    }
                    ++pPoolIds;
                }
            }
        }
    }

    if( nSearchFamily == SfxStyleFamily::Frame ||
        nSearchFamily == SfxStyleFamily::All )
    {
        const size_t nArrLen = rDoc.GetFrameFormats()->size();
        for( size_t i = 0; i < nArrLen; i++ )
        {
            const SwFrameFormat* pFormat = (*rDoc.GetFrameFormats())[ i ];

            bool bUsed = bIsSearchUsed && ( bOrganizer || rDoc.IsUsed(*pFormat));
            if( ( !bSearchHidden && pFormat->IsHidden( ) && !bUsed ) || pFormat->IsDefault() || pFormat->IsAuto() )
                continue;

            if ( nSrchMask == SfxStyleSearchBits::Hidden && !pFormat->IsHidden( ) )
                continue;

            const sal_uInt16 nId = pFormat->GetPoolFormatId();
            if( !bUsed )
            {
                if( (nSrchMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                    ? !(nId & USER_FMT)
                    // searched for used and found none
                    : bIsSearchUsed )
                {
                    continue;
                }
            }

            aLst.Append( cFRAME, pFormat->GetName() );
        }

        // PoolFormat
        if ( bAll )
            AppendStyleList(SwStyleNameMapper::GetFrameFormatUINameArray(),
                                    bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::FrmFmt, cFRAME);
    }

    if( nSearchFamily == SfxStyleFamily::Page ||
        nSearchFamily == SfxStyleFamily::All )
    {
        const size_t nCount = rDoc.GetPageDescCnt();
        for(size_t i = 0; i < nCount; ++i)
        {
            const SwPageDesc& rDesc = rDoc.GetPageDesc(i);
            const sal_uInt16 nId = rDesc.GetPoolFormatId();
            bool bUsed = bIsSearchUsed && ( bOrganizer || rDoc.IsUsed(rDesc));
            if( !bUsed )
            {
                if ( ( !bSearchHidden && rDesc.IsHidden() ) ||
                       ( (nSrchMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                    ? !(nId & USER_FMT)
                    // searched for used and found none
                    : bIsSearchUsed ) )
                    continue;
            }

            if ( nSrchMask == SfxStyleSearchBits::Hidden && !rDesc.IsHidden( ) )
                continue;

            aLst.Append( cPAGE, rDesc.GetName() );
        }
        if ( bAll )
            AppendStyleList(SwStyleNameMapper::GetPageDescUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::PageDesc, cPAGE);
    }

    if( nSearchFamily == SfxStyleFamily::Pseudo ||
        nSearchFamily == SfxStyleFamily::All )
    {
        const SwNumRuleTable& rNumTable = rDoc.GetNumRuleTable();
        for(size_t i = 0; i < rNumTable.size(); ++i)
        {
            const SwNumRule& rRule = *rNumTable[ i ];
            if( !rRule.IsAutoRule() )
            {
                if ( nSrchMask == SfxStyleSearchBits::Hidden && !rRule.IsHidden( ) )
                    continue;

                bool bUsed = bIsSearchUsed && (bOrganizer || rDoc.IsUsed(rRule));
                if( !bUsed )
                {
                    if( ( !bSearchHidden && rRule.IsHidden() ) ||
                           ( (nSrchMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                        ? !(rRule.GetPoolFormatId() & USER_FMT)
                        // searched for used and found none
                        : bIsSearchUsed ) )
                        continue;
                }

                aLst.Append( cNUMRULE, rRule.GetName() );
            }
        }
        if ( bAll )
            AppendStyleList(SwStyleNameMapper::GetNumRuleUINameArray(),
                            bIsSearchUsed, bSearchHidden, bOnlyHidden, SwGetPoolIdFromName::NumRule, cNUMRULE);
    }

    if( nSearchFamily == SfxStyleFamily::Table ||
        nSearchFamily == SfxStyleFamily::All )
    {
        const SwTableAutoFormatTable& rTableStyles = rDoc.GetTableStyles();
        for(size_t i = 0; i < rTableStyles.size(); ++i)
        {
            const SwTableAutoFormat& rTableStyle = rTableStyles[i];

            bool bUsed = bIsSearchUsed && (bOrganizer || rDoc.IsUsed(rTableStyle));
            if(!bUsed)
            {
                if(nSrchMask == SfxStyleSearchBits::Hidden && !rTableStyle.IsHidden())
                    continue;

                if( (!bSearchHidden && rTableStyle.IsHidden() ) ||
                        ( (nSrchMask & ~SfxStyleSearchBits::Used) == SfxStyleSearchBits::UserDefined
                    ? !rTableStyle.IsUserDefined()
                    // searched for used and found none
                    : bIsSearchUsed ) )
                    continue;
            }

            aLst.Append( cTABSTYLE, rTableStyle.GetName() );
        }
    }

    if( nSearchFamily == SfxStyleFamily::Cell ||
        nSearchFamily == SfxStyleFamily::All )
    {
        const auto& aTableTemplateMap = SwTableAutoFormat::GetTableTemplateMap();
        if (rDoc.HasTableStyles())
        {
            const SwTableAutoFormatTable& rTableStyles = rDoc.GetTableStyles();
            for(size_t i = 0; i < rTableStyles.size(); ++i)
            {
                const SwTableAutoFormat& rTableStyle = rTableStyles[i];
                for(size_t nBoxFormat = 0; nBoxFormat < aTableTemplateMap.size(); ++nBoxFormat)
                {
                    const sal_uInt32 nBoxIndex = aTableTemplateMap[nBoxFormat];
                    const SwBoxAutoFormat& rBoxFormat = rTableStyle.GetBoxFormat(nBoxIndex);
                    OUString sBoxFormatName;
                    SwStyleNameMapper::FillProgName(rTableStyle.GetName(), sBoxFormatName, SwGetPoolIdFromName::TabStyle);
                    sBoxFormatName += rTableStyle.GetTableTemplateCellSubName(rBoxFormat);
                    aLst.Append( cCELLSTYLE, sBoxFormatName );
                }
            }
        }
        const SwCellStyleTable& rCellStyles = rDoc.GetCellStyles();
        for(size_t i = 0; i < rCellStyles.size(); ++i)
            aLst.Append( cCELLSTYLE, rCellStyles[i].GetName() );
    }

    if(!aLst.empty())
    {
        nLastPos = SAL_MAX_UINT32;
        return Next();
    }
    return nullptr;
}

SfxStyleSheetBase* SwStyleSheetIterator::Next()
{
    assert(bFirstCalled);
    ++nLastPos;
    if(nLastPos < aLst.size())
    {
        mxIterSheet->PresetNameAndFamily(aLst[nLastPos]);
        mxIterSheet->SetPhysical( false );
        mxIterSheet->SetMask( nMask );
        if(mxIterSheet->pSet)
        {
            mxIterSheet->pSet->ClearItem();
            mxIterSheet->pSet= nullptr;
        }
        return mxIterSheet.get();
    }
    return nullptr;
}

SfxStyleSheetBase* SwStyleSheetIterator::Find(const OUString& rName)
{
    // searching
    if( !bFirstCalled )
        First();

    nLastPos = aLst.FindName( nSearchFamily, rName );
    if( SAL_MAX_UINT32 != nLastPos )
    {
        // found
        mxStyleSheet->PresetNameAndFamily(aLst[nLastPos]);
        // new name is set, so determine its Data
        mxStyleSheet->FillStyleSheet( SwDocStyleSheet::FillOnlyName );
        if( !mxStyleSheet->IsPhysical() )
            mxStyleSheet->SetPhysical( false );

        return mxStyleSheet.get();
    }
    return nullptr;
}

void SwStyleSheetIterator::AppendStyleList(const std::vector<OUString>& rList,
                                            bool bTestUsed, bool bTestHidden, bool bOnlyHidden,
                                            SwGetPoolIdFromName nSection, char cType )
{
    SwDoc& rDoc = static_cast<SwDocStyleSheetPool*>(pBasePool)->GetDoc();
    bool bUsed = false;
    for (const auto & i : rList)
    {
        bool bHidden = false;
        sal_uInt16 nId = SwStyleNameMapper::GetPoolIdFromUIName(i, nSection);
        switch ( nSection )
        {
            case SwGetPoolIdFromName::TxtColl:
                {
                    bUsed = rDoc.getIDocumentStylePoolAccess().IsPoolTextCollUsed( nId );
                    SwFormat* pFormat = rDoc.FindTextFormatCollByName( i );
                    bHidden = pFormat && pFormat->IsHidden( );
                }
                break;
            case SwGetPoolIdFromName::ChrFmt:
                {
                    bUsed = rDoc.getIDocumentStylePoolAccess().IsPoolFormatUsed( nId );
                    SwFormat* pFormat = rDoc.FindCharFormatByName( i );
                    bHidden = pFormat && pFormat->IsHidden( );
                }
                break;
            case SwGetPoolIdFromName::FrmFmt:
                {
                    bUsed = rDoc.getIDocumentStylePoolAccess().IsPoolFormatUsed( nId );
                    SwFormat* pFormat = rDoc.FindFrameFormatByName( i );
                    bHidden = pFormat && pFormat->IsHidden( );
                }
                break;
            case SwGetPoolIdFromName::PageDesc:
                {
                    bUsed = rDoc.getIDocumentStylePoolAccess().IsPoolPageDescUsed( nId );
                    SwPageDesc* pPgDesc = rDoc.FindPageDesc(i);
                    bHidden = pPgDesc && pPgDesc->IsHidden( );
                }
                break;
            case SwGetPoolIdFromName::NumRule:
                {
                    SwNumRule* pRule = rDoc.FindNumRulePtr( i );
                    bUsed = pRule && rDoc.IsUsed(*pRule);
                    bHidden = pRule && pRule->IsHidden( );
                }
                break;
            default:
                OSL_ENSURE( false, "unknown PoolFormat-Id" );
        }

        bool bMatchHidden = ( bTestHidden && ( bHidden || !bOnlyHidden ) ) || ( !bTestHidden && ( !bHidden || bUsed ) );
        if ( ( !bTestUsed && bMatchHidden ) || ( bTestUsed && bUsed ) )
            aLst.Append( cType, i );
    }
}

void SwDocStyleSheetPool::InvalidateIterator()
{
    if (SfxStyleSheetIterator* pIter = GetCachedIterator())
        dynamic_cast<SwStyleSheetIterator&>(*pIter).InvalidateIterator();
}

void SwStyleSheetIterator::InvalidateIterator()
{
    // potentially we could send an SfxHint to Notify but currently it's
    // iterating over the vector anyway so would still be slow - why does
    // this iterator not use a map?
    bFirstCalled = false;
    nLastPos = 0;
    aLst.clear();
}

void SwStyleSheetIterator::Notify( SfxBroadcaster&, const SfxHint& rHint )
{
    // search and remove from View-List!!
    const SfxStyleSheetHint* pStyleSheetHint = dynamic_cast<const SfxStyleSheetHint*>(&rHint);
    if( pStyleSheetHint &&
        SfxHintId::StyleSheetErased == pStyleSheetHint->GetId() )
    {
        SfxStyleSheetBase* pStyle = pStyleSheetHint->GetStyleSheet();

        if (pStyle)
            aLst.RemoveName(pStyle->GetFamily(), pStyle->GetName());
    }
}

/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
