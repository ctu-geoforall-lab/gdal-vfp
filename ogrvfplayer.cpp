/******************************************************************************
 * $Id$
 *
 * Project:  VFP Translator
 * Purpose:  Implements OGRVFPLayer class.
 * Author:   Martin Landa, landa.martin gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2015, Martin Landa <landa.martin gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 ****************************************************************************/

#include "ogr_vfp.h"
#include "cpl_conv.h"
#include "cpl_string.h"
#include "cpl_minixml.h"
#include "ogr_p.h"

CPL_CVSID("$Id$");

/************************************************************************/
/*                            OGRGPXLayer()                             */
/*                                                                      */
/************************************************************************/

OGRVFPLayer::OGRVFPLayer( const char* pszFilename,
                          const char* pszLayerName,
                          OGRVFPDataSource* poDS)
{
    this->poDS = poDS;

    pszElementToScan = pszLayerName;

    nFeatures = 0;

    poFeatureDefn = new OGRFeatureDefn( pszLayerName );
    SetDescription( poFeatureDefn->GetName() );
    poFeatureDefn->Reference();

    /* set spatial reference 
       default is S-JTSK (EPSG: 5514) */
    poSRS = new OGRSpatialReference();
    if( poSRS->importFromEPSG(5514) != OGRERR_NONE)
    {
        delete poSRS;
        poSRS = NULL;
    }
    if( poFeatureDefn->GetGeomFieldCount() != 0 )
        poFeatureDefn->GetGeomFieldDefn(0)->SetSpatialRef(poSRS);

     poFeature = NULL;

#ifdef HAVE_EXPAT
    oParser = NULL;
    oSchemaParser = NULL;
#endif

    fpVFP = VSIFOpenL( pszFilename, "r" );
    if( fpVFP == NULL )
        {
            CPLError(CE_Failure, CPLE_AppDefined, "Cannot open %s", pszFilename);
            return;
        }

    LoadSchema();
     
    ResetReading();
}

/************************************************************************/
/*                            ~OGRVFPLayer()                            */
/************************************************************************/

OGRVFPLayer::~OGRVFPLayer()

{
#ifdef HAVE_EXPAT
    if (oParser)
        XML_ParserFree(oParser);
#endif
    poFeatureDefn->Release();
    
    if( poSRS != NULL )
        poSRS->Release();

    if (poFeature)
        delete poFeature;

    if (fpVFP)
        VSIFCloseL( fpVFP );
}

/************************************************************************/
/*                            ResetReading()                            */
/************************************************************************/

void OGRVFPLayer::ResetReading()

{
}

/************************************************************************/
/*                           GetNextFeature()                           */
/************************************************************************/

OGRFeature *OGRVFPLayer::GetNextFeature()
{
    return FALSE;
}

/************************************************************************/
/*                           TestCapability()                           */
/************************************************************************/

int OGRVFPLayer::TestCapability( const char * pszCap )
{
    return FALSE;
}

/************************************************************************/
/*                       LoadExtensionsSchema()                         */
/************************************************************************/

#ifdef HAVE_EXPAT

static void XMLCALL startElementLoadSchemaCbk(void *pUserData, const char *pszName, const char **ppszAttr)
{
    ((OGRVFPLayer*)pUserData)->startElementLoadSchemaCbk(pszName, ppszAttr);
}

static void XMLCALL endElementLoadSchemaCbk(void *pUserData, const char *pszName)
{
    ((OGRVFPLayer*)pUserData)->endElementLoadSchemaCbk(pszName);
}

static void XMLCALL dataHandlerLoadSchemaCbk(void *pUserData, const char *data, int nLen)
{
    ((OGRVFPLayer*)pUserData)->dataHandlerLoadSchemaCbk(data, nLen);
}

void OGRVFPLayer::LoadSchema()
{
    oSchemaParser = OGRCreateExpatXMLParser();
    XML_SetElementHandler(oSchemaParser, ::startElementLoadSchemaCbk, ::endElementLoadSchemaCbk);
    XML_SetCharacterDataHandler(oSchemaParser, ::dataHandlerLoadSchemaCbk);
    XML_SetUserData(oSchemaParser, this);

    VSIFSeekL( fpVFP, 0, SEEK_SET );

    bStopParsing = FALSE;
    nWithoutEventCounter = 0;
    interestingDepthLevel = depthLevel = 0;
    inInterestingElement = FALSE;
    
    char aBuf[BUFSIZ];
    int nDone;
    do
    {
        nDataHandlerCounter = 0;
        unsigned int nLen = (unsigned int)VSIFReadL( aBuf, 1, sizeof(aBuf), fpVFP );
        nDone = VSIFEofL(fpVFP);
        if (XML_Parse(oSchemaParser, aBuf, nLen, nDone) == XML_STATUS_ERROR)
        {
            CPLError(CE_Failure, CPLE_AppDefined,
                     "XML parsing of GPX file failed : %s at line %d, column %d",
                     XML_ErrorString(XML_GetErrorCode(oSchemaParser)),
                     (int)XML_GetCurrentLineNumber(oSchemaParser),
                     (int)XML_GetCurrentColumnNumber(oSchemaParser));
            bStopParsing = TRUE;
            break;
        }
        nWithoutEventCounter ++;
    } while (!nDone && !bStopParsing && nWithoutEventCounter < 10);

    if (nWithoutEventCounter == 10)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "Too much data inside one element. File probably corrupted");
        bStopParsing = TRUE;
    }

    XML_ParserFree(oSchemaParser);
    oSchemaParser = NULL;

    VSIFSeekL( fpVFP, 0, SEEK_SET );
}

void OGRVFPLayer::startElementLoadSchemaCbk(const char *pszName,
                                            CPL_UNUSED const char **ppszAttr)
{
    if (bStopParsing) return;

    nWithoutEventCounter = 0;

    if (strcmp(pszElementToScan, pszName) == 0)
    {
        inInterestingElement = TRUE;
        interestingDepthLevel = depthLevel;
        
        if (strcmp(pszName, "ucastnici") == 0)
        {
        }
    }
    else if (inInterestingElement)
    {
        if (depthLevel == interestingDepthLevel + 1)
        {
            
        }
        else
        {
            bStopParsing = TRUE;
        }
    }

    depthLevel++;
}

void OGRVFPLayer::endElementLoadSchemaCbk(const char *pszName)
{
}

void OGRVFPLayer::dataHandlerLoadSchemaCbk(const char *data, int nLen)
{
}
#else
void OGRVFPLayer::LoadSchema()
{
}
#endif
