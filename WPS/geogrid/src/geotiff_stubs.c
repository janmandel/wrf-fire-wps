
#ifdef _HAS_GEOTIFF

#include "geotiff_stubs.h"

#include <geotiff.h>
#include <geo_normalize.h>
#include <geovalues.h>

void get_tile_size(TIFF *filep, int *x, int *y) {
  if( TIFFIsTiled(filep) ) {
    TIFFGetField(filep,TIFFTAG_TILEWIDTH,x);
    TIFFGetField(filep,TIFFTAG_TILELENGTH,y);
  }
  else {
    TIFFGetField(filep,TIFFTAG_IMAGEWIDTH,x);
    *y=TIFFStripSize(filep);
  }
}

void geotiff_header(
    TIFF *filep,
    int *nx,
    int *ny,
    int *nz,
    int *tilex,
    int *tiley,
    int *proj,
    fltType *dx,
    fltType *dy,
    int *known_x,
    int *known_y,
    fltType *known_lat,
    fltType *known_lon,
    fltType *stdlon,
    fltType *truelat1,
    fltType *truelat2,
    int *status
    ) {

  double tmpdble;
  int tmpint;
  double x,y;

  *status=0;

  GTIF *gtifh=GTIFNew(filep);
  GTIFDefn g;
  GTIFGetDefn(gtifh,&g);

  *nx=I_INVALID;
  *ny=I_INVALID;
  *nz=I_INVALID;
  *tilex=I_INVALID;
  *tiley=I_INVALID;
  *proj=I_INVALID;
  *dx=F_INVALID;
  *dy=F_INVALID;
  *known_x=I_INVALID;
  *known_y=I_INVALID;
  *known_lat=F_INVALID;
  *known_lon=F_INVALID;
  *stdlon=F_INVALID;
  *truelat1=F_INVALID;
  *truelat2=F_INVALID;

  x=0;
  y=0;
  *known_x=x;
  *known_y=y;

  if ( !GTIFImageToPCS( gtifh, &x, &y) ) *status=1;

  if(g.Model == ModelTypeGeographic) {
    *proj=(int) regular_ll;
    *known_lon=x;
    *known_lat=y;
  }
  else {
    switch (g.CTProjection) {
      case CT_AlbersEqualArea:
        *proj=(int) albers_nad83;
        break;
      case CT_TransverseMercator:
        *proj=(int) mercator;
        break;
      case CT_PolarStereographic:
        *proj=(int) polar;
	break;
      case CT_LambertConfConic:
	*proj=(int) lambert;
	break;
      default:
        fprintf(stderr,"Unsupported projection ID: %i\n",g.CTProjection);
        *status=1;
    }

    if( !GTIFProj4ToLatLong( &g, 1, &x, &y) ) *status=1;
    *known_lon=x;
    *known_lat=y; 
  }
  

  GTIFKeyGet(gtifh,ProjStdParallel1GeoKey,&tmpdble,0,1);
  *truelat1=tmpdble;
  GTIFKeyGet(gtifh,ProjStdParallel2GeoKey,&tmpdble,0,1);
  *truelat2=tmpdble;
  GTIFKeyGet(gtifh,ProjCenterLongGeoKey,&tmpdble,0,1);
  *stdlon=tmpdble;

  TIFFGetField(filep,TIFFTAG_XRESOLUTION,&tmpdble);
  *dx=tmpdble;
  TIFFGetField(filep,TIFFTAG_YRESOLUTION,&tmpdble);
  *dy=tmpdble;
  TIFFGetField(filep,TIFFTAG_IMAGEWIDTH,&tmpint);
  *nx=tmpint;
  TIFFGetField(filep,TIFFTAG_IMAGELENGTH,&tmpint);
  *ny=tmpint;

  if( !TIFFGetField(filep,TIFFTAG_IMAGEDEPTH,nz) ) *nz=1;
  
  get_tile_size(filep,tilex,tiley);
}

int geotiff_check(TIFF *filep) {
  int nx,ny,nz,tx,ty,proj,known_x,known_y,status;
  fltType dx,dy,known_lat,known_lon,stdlon,truelat1,truelat2;
  
  geotiff_header(filep,&nx,&ny,&nz,&tx,&ty,&proj,&dx,&dy,&known_x,&known_y,
                 &known_lat,&known_lon,&stdlon,&truelat1,&truelat2,
		 &status);
  return (status);

}

void geotiff_open(char *filename,TIFF *filep,int *status) {
  *status=0;
  if (!_HAVE_PROJ4) {
    *status=1;
  }
  filep=XTIFFOpen(filename,"r");
  if (!filep) *status=1;
}

void geotiff_close(TIFF *filep) {
  XTIFFClose(filep);
}

void get_pointer_size(int *psize) {
  TIFF *p;
  *psize=sizeof(p);
}

int read_tile_tiled(TIFF *filep,int xtile,int ytile,void *buffer) {
  int status,result;
  status=0;
  result=TIFFReadTile(filep,buffer,xtile,ytile,0,0);
  if(result == -1) status=99;
  return(status);
}

int read_tile_stripped(TIFF *filep,int xtile,int ytile,void *buffer) {
  int status,result;
  status=0;
  result=TIFFReadEncodedStrip(filep,ytile,buffer,xtile);
  if(result == -1) status=99;
  return(status);
}

void read_geotiff_tile(TIFF *filep, int *xtile, int *ytile, 
                       int *nx, int *ny, int *nz, fltType *buffer, 
		       int *status) {
  int tx,ty,mx,my,i;
  int np,sf,tilesize;
  void *tilebuf;
  get_tile_size(filep,&tx,&ty);
  TIFFGetField(filep,TIFFTAG_IMAGEWIDTH,&mx);
  TIFFGetField(filep,TIFFTAG_IMAGELENGTH,&my);

  if(tx != *nx || ty != *ny || *nz != 1) {
    *status=99;
    return;
  }
  if(*xtile > mx/tx || *xtile < 0 || *ytile > my/ty || *ytile < 0) {
    *status=1;
    return;
  }
 
  TIFFGetField(filep,TIFFTAG_SAMPLEFORMAT,&sf);
  TIFFGetField(filep,TIFFTAG_BITSPERSAMPLE,&np);
  tilesize=tx*ty*(np/8);
  tilebuf=_TIFFmalloc(tilesize);

  if(TIFFIsTiled) {
    *status=read_tile_tiled(filep,*xtile,*ytile,tilebuf);
  }
  else {
    *status=read_tile_stripped(filep,*xtile,*ytile,tilebuf);
  }

  switch (sf) {
    case SAMPLEFORMAT_UINT:
      switch (np) {
	case 8:
	  CONVERT_BUFFER(uint8,1)
	  break;
	case 16:
	  CONVERT_BUFFER(uint16,2)
	  break;
	case 32:
	  CONVERT_BUFFER(uint32,4)
	  break;
	default:
	  *status=1;
      }
      break;
    case SAMPLEFORMAT_INT:
      switch (np) {
	case 8:
	  CONVERT_BUFFER(int8,1)
	  break;
	case 16:
	  CONVERT_BUFFER(int16,2)
	  break;
	case 32:
          CONVERT_BUFFER(int32,4)
	  break;
	default:
	  *status=1;
      }
      break;
    case SAMPLEFORMAT_IEEEFP:
      switch (np) {
	case 8*sizeof(float):
          CONVERT_BUFFER(float,sizeof(float))
	  break;
	case 8*sizeof(double):
	  CONVERT_BUFFER(double,sizeof(double))
	  break;
	default:
	  *status=1;
      }
      break;
    default:
      *status=1;
      break;
  }

  _TIFFfree(tilebuf);
}

#else
int dummy_c_function() {
  return 0;
}
#endif