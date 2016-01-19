# gdal-vfp
Experimental support for Czech Exchange Format for Land Consolidation (VFP) in GDAL library

## Compilation

Requirement: GDAL 2.1.0


             svn checkout https://svn.osgeo.org/gdal/trunk/gdal gdal
             cd ogr/ogrsf_frmts
             git clone https://github.com/ctu-osgeorel/gdal-vfp.git
             cd ../..
             patch -p0 < ogr/ogrsf_frmts/vfp/gdal.diff

### GNU Linux

             ./configure
             make
             sudo make install
