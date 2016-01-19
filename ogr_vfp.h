/******************************************************************************
 * $Id$
 *
 * Project:  VFP Translator
 * Purpose:  Definition of classes for OGR .vfp driver.
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

#ifndef _OGR_VFP_H_INCLUDED
#define _OGR_VFP_H_INCLUDED

#include "ogrsf_frmts.h"

#ifdef HAVE_EXPAT
#include "ogr_expat.h"
#endif

class OGRVFPDataSource;


/************************************************************************/
/*                             OGRVFPLayer                              */
/************************************************************************/

class OGRVFPLayer : public OGRLayer
{
private:
    OGRFeatureDefn*    poFeatureDefn;
    OGRSpatialReference *poSRS;
    OGRVFPDataSource*  poDS;

    int                nFeatures;

    const char*        pszElementToScan;

#ifdef HAVE_EXPAT
    XML_Parser         oParser;
    XML_Parser         oSchemaParser;
#endif

    OGRFeature*        poFeature;

    VSILFILE*          fpVFP; /* Large file API */

    void               LoadSchema();

    bool               bStopParsing;
    int                nWithoutEventCounter;
    int                nDataHandlerCounter;
    int                depthLevel;
    bool               inInterestingElement;
    int                interestingDepthLevel;

public:
    OGRVFPLayer(const char *pszFilename,
                const char* layerName,
                OGRVFPDataSource* poDS);
    ~OGRVFPLayer();

    void                ResetReading();
    OGRFeature *        GetNextFeature();
    

    OGRFeatureDefn *    GetLayerDefn() { return poFeatureDefn; }
    
    int                 TestCapability( const char * );

#ifdef HAVE_EXPAT
    void                startElementLoadSchemaCbk(const char *pszName, const char **ppszAttr);
    void                endElementLoadSchemaCbk(const char *pszName);
    void                dataHandlerLoadSchemaCbk(const char *data, int nLen);
#endif
};

/************************************************************************/
/*                           OGRVFPDataSource                           */
/************************************************************************/

typedef enum
{
    VFP_VALIDITY_UNKNOWN,
    VFP_VALIDITY_INVALID,
    VFP_VALIDITY_VALID
} OGRVFPValidity;

class OGRVFPDataSource : public OGRDataSource
{
private:
    char*               pszName;

    OGRVFPLayer**       papoLayers;
    int                 nLayers;

    OGRVFPValidity      validity;
    char*               pszVersion;

#ifdef HAVE_EXPAT
    XML_Parser          oCurrentParser;
    int                 nDataHandlerCounter;
#endif

public:
    OGRVFPDataSource();
    ~OGRVFPDataSource();
    
    const char*         GetName() { return pszName; }

    int                 Open( const char * pszFilename,
                              int bUpdate );
    
    int                 GetLayerCount() { return nLayers; }
    OGRLayer*           GetLayer( int );
    
#ifdef HAVE_EXPAT
    void                startElementValidateCbk(const char *pszName, const char **ppszAttr);
    void                dataHandlerValidateCbk(const char *data, int nLen);
#endif

};

#endif /* ndef _OGR_VFP_H_INCLUDED */
