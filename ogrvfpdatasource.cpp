/******************************************************************************
 * $Id$
 *
 * Project:  VFP Translator
 * Purpose:  Implements OGRVFPDataSource class
 * Author:   Martin Landa, landa.martin gmail.com
 *
 ******************************************************************************
 * Copyright (c) 2015-2016, Martin Landa <landa.martin gmail.com>
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
#include "cpl_csv.h"
#include "vfp_3.1-pskel.h"

CPL_CVSID("$Id$");

/************************************************************************/
/*                          OGRVFPDataSource()                          */
/************************************************************************/

OGRVFPDataSource::OGRVFPDataSource()
{
    pszName = NULL;
    pszVersion = NULL;
    
    validity = VFP_VALIDITY_UNKNOWN;
    
#ifdef HAVE_EXPAT
    oCurrentParser = NULL;
    nDataHandlerCounter = 0;
#endif

    nLayers = 0;
    papoLayers = NULL;
}

/************************************************************************/
/*                         ~OGRVFPDataSource()                          */
/************************************************************************/

OGRVFPDataSource::~OGRVFPDataSource()

{
    for( int i = 0; i < nLayers; i++ )
        delete papoLayers[i];
    CPLFree( papoLayers );
    CPLFree( pszName );
    CPLFree( pszVersion );
}

#ifdef HAVE_EXPAT

/************************************************************************/
/*                startElementValidateCbk()                             */
/************************************************************************/

void OGRVFPDataSource::startElementValidateCbk(const char *pszName, const char **ppszAttr)
{
    if (validity == VFP_VALIDITY_UNKNOWN)
    {
        if (strcmp(pszName, "v:vfp") == 0)
        {
            validity = VFP_VALIDITY_VALID;
        }
        else
        {
            validity = VFP_VALIDITY_INVALID;
        }
    }
}


/************************************************************************/
/*                      dataHandlerValidateCbk()                        */
/************************************************************************/

void OGRVFPDataSource::dataHandlerValidateCbk(CPL_UNUSED const char *data,
                                              CPL_UNUSED int nLen)
{
    nDataHandlerCounter ++;
    if (nDataHandlerCounter >= BUFSIZ)
    {
        CPLError(CE_Failure, CPLE_AppDefined,
                 "File probably corrupted (million laugh pattern)");
        XML_StopParser(oCurrentParser, XML_FALSE);
    }
}

static void XMLCALL startElementValidateCbk(void *pUserData, const char *pszName, const char **ppszAttr)
{
    OGRVFPDataSource* poDS = (OGRVFPDataSource*) pUserData;
    poDS->startElementValidateCbk(pszName, ppszAttr);
}

static void XMLCALL dataHandlerValidateCbk(void *pUserData, const char *data, int nLen)
{
    OGRVFPDataSource* poDS = (OGRVFPDataSource*) pUserData;
    poDS->dataHandlerValidateCbk(data, nLen);
}
#endif

/************************************************************************/
/*                                Open()                                */
/************************************************************************/

int OGRVFPDataSource::Open( const char * pszFilename, int bUpdateIn)
{
    if (bUpdateIn)
    {
        CPLError(CE_Failure, CPLE_NotSupported,
                    "OGR/VFP driver does not support opening a file in update mode");
        return FALSE;
    }
    
#ifdef HAVE_EXPAT
    pszName = CPLStrdup( pszFilename );

    // try to open the file
    VSILFILE* fp = VSIFOpenL(pszFilename, "r");
    if (fp == NULL)
        return FALSE;

    validity = VFP_VALIDITY_UNKNOWN;
    CPLFree(pszVersion);
    pszVersion = NULL;

    XML_Parser oParser = OGRCreateExpatXMLParser();
    oCurrentParser = oParser;
    XML_SetUserData(oParser, this);
    XML_SetElementHandler(oParser, ::startElementValidateCbk, NULL);
    XML_SetCharacterDataHandler(oParser, ::dataHandlerValidateCbk);

    char aBuf[BUFSIZ];
    int nDone;
    unsigned int nLen;
    int nCount = 0;
    
    /* Begin to parse the file and look for the <v:vfp> element */
    /* It *MUST* be the first element of an XML file */
    /* So once we have read the first element, we know if we can */
    /* handle the file or not with that driver */
    do
    {
        nDataHandlerCounter = 0;
        nLen = (unsigned int) VSIFReadL( aBuf, 1, sizeof(aBuf), fp );
        nDone = VSIFEofL(fp);
        if (XML_Parse(oParser, aBuf, nLen, nDone) == XML_STATUS_ERROR)
        {
            if (nLen <= BUFSIZ-1)
                aBuf[nLen] = 0;
            else
                aBuf[BUFSIZ-1] = 0;
            if (strstr(aBuf, "<?xml") && strstr(aBuf, "<v:vfp"))
            {
                CPLError(CE_Failure, CPLE_AppDefined,
                        "XML parsing of VFP file failed : %s at line %d, column %d",
                        XML_ErrorString(XML_GetErrorCode(oParser)),
                        (int)XML_GetCurrentLineNumber(oParser),
                        (int)XML_GetCurrentColumnNumber(oParser));
            }
            validity = VFP_VALIDITY_INVALID;
            break;
        }
        
        if (validity == VFP_VALIDITY_INVALID || validity == VFP_VALIDITY_VALID)
        {
            break;
        }
        else
        {
            /* After reading 50 * BUFSIZE bytes, and not finding whether the file */
            /* is VFP or not, we give up and fail silently */
            nCount ++;
            if (nCount == 50)
                break;
        }
    } while (!nDone && nLen > 0 );
    
    XML_ParserFree(oParser);
    
    VSIFCloseL(fp);

    if (validity == VFP_VALIDITY_VALID)
    {
        CPLDebug("VFP", "%s seems to be a VFP file.", pszFilename);

        if (pszVersion == NULL)
        {
            /* Default to 2.0 */
            CPLError(CE_Warning, CPLE_AppDefined, "VFP schema version is unknown. "
                     "The driver may not be able to handle the file correctly "
                     "and will behave as if it is VFP 2.0.");
            pszVersion = CPLStrdup("2.0");
        }
        else if (strcmp(pszVersion, "2.0") == 0)
        {
            /* Fine */
        }
        else
        {
            CPLError(CE_Warning, CPLE_AppDefined,
                     "VFP schema version '%s' is not handled by the driver. "
                     "The driver may not be able to handle the file correctly "
                     "and will behave as if it is GPX 2.0.", pszVersion);
        }

        /* TODO: readed it from XSD */
        nLayers = 19;
        papoLayers = (OGRVFPLayer **) CPLRealloc(papoLayers, nLayers * sizeof(OGRVFPLayer*));
        papoLayers[0] = new OGRVFPLayer( pszName, "ucastnici", this);
        papoLayers[1] = new OGRVFPLayer( pszName, "narok", this);
        papoLayers[2] = new OGRVFPLayer( pszName, "navrh", this);
        papoLayers[3] = new OGRVFPLayer( pszName, "pneres", this);
        papoLayers[4] = new OGRVFPLayer( pszName, "pmimo", this);
        papoLayers[5] = new OGRVFPLayer( pszName, "bpej", this);
        papoLayers[6] = new OGRVFPLayer( pszName, "bpejr2", this);
        papoLayers[7] = new OGRVFPLayer( pszName, "mdp", this);
        papoLayers[8] = new OGRVFPLayer( pszName, "zs", this);
        papoLayers[9] = new OGRVFPLayer( pszName, "opu", this);
        papoLayers[10] = new OGRVFPLayer( pszName, "por", this);
        papoLayers[11] = new OGRVFPLayer( pszName, "pbre", this);
        papoLayers[12] = new OGRVFPLayer( pszName, "spoz", this);
        papoLayers[13] = new OGRVFPLayer( pszName, "pm", this);
        papoLayers[14] = new OGRVFPLayer( pszName, "mp", this);
        papoLayers[15] = new OGRVFPLayer( pszName, "meos", this);
        papoLayers[16] = new OGRVFPLayer( pszName, "meon", this);
        papoLayers[17] = new OGRVFPLayer( pszName, "hvpsz", this);
        papoLayers[18] = new OGRVFPLayer( pszName, "zs", this);
    }

    return (validity == VFP_VALIDITY_VALID);
#else
    char aBuf[256];
    VSILFILE* fp = VSIFOpenL(pszFilename, "r");
    if (fp)
    {
        unsigned int nLen = (unsigned int)VSIFReadL( aBuf, 1, 255, fp );
        aBuf[nLen] = 0;
        if (strstr(aBuf, "<?xml") && strstr(aBuf, "<v:vfp"))
        {
            CPLError(CE_Failure, CPLE_NotSupported,
                    "OGR/VFP driver has not been built with read support. Expat library required");
        }
        VSIFCloseL(fp);
    }
#endif
    return FALSE;
}

/************************************************************************/
/*                              GetLayer()                              */
/************************************************************************/

OGRLayer *OGRVFPDataSource::GetLayer( int iLayer )

{
    if( iLayer < 0 || iLayer >= nLayers )
        return NULL;
    else
        return papoLayers[iLayer];
}
