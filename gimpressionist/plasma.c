#include "config.h"

#include <stdlib.h>
#include <time.h>
#include <math.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>

#include "gimpressionist.h"
#include "ppmtool.h"

static int pfix(int n)
{
  if (n < 1) return 1;
  if (n > 255) return 255;
  return n;
}

#define PIXEL(y,x,z) p->col[(y)*rowstride+(x)*3+z]

static void mkplasma_sub(ppm_t *p, int x1, int x2, int y1, int y2, float turb)
{
  int rowstride = p->width * 3;
  int r=0;
  int xr, yr, nx, ny;
  xr = abs(x1-x2);
  yr = abs(y1-y2);

  if((xr==0) && (yr==0)) return;

  nx = (x1+x2)/2;
  ny = (y1+y2)/2;
  if(!PIXEL(y1,nx,r))
    PIXEL(y1,nx,r) = pfix((PIXEL(y1,x1,r)+PIXEL(y1,x2,r))/2.0+
                           turb * g_rand_double_range (gr, -xr/2.0, xr/2.0));
  if(!PIXEL(y2,nx,r))
    PIXEL(y2,nx,r) = pfix((PIXEL(y2,x1,r)+PIXEL(y2,x2,r))/2.0+
                           turb * g_rand_double_range (gr, -xr/2.0, xr/2.0));
  if(!PIXEL(ny,x1,r))
    PIXEL(ny,x1,r) = pfix((PIXEL(y1,x1,r)+PIXEL(y2,x1,r))/2.0+
                           turb * g_rand_double_range (gr, -yr/2.0, yr/2.0));
  if(!PIXEL(ny,x2,r))
    PIXEL(ny,x2,r) = pfix((PIXEL(y1,x2,r)+PIXEL(y2,x2,r))/2.0+
                           turb * g_rand_double_range (gr, -yr/2.0, yr/2.0));
  if(!PIXEL(ny,nx,r))
    PIXEL(ny,nx,r) = 
      pfix((PIXEL(y1,x1,r) + PIXEL(y1,x2,r) + PIXEL(y2,x1,r) +
            PIXEL(y2,x2,r))/4.0 + turb * 
            g_rand_double_range (gr, -(xr + yr)/4.0, (xr + yr)/4.0));

  if(xr>1) {
    mkplasma_sub(p,x1,nx,y1,ny, turb);
    mkplasma_sub(p,nx,x2,y1,ny, turb);
  }
  if(yr>1) {
    mkplasma_sub(p,x1,nx,ny,y2, turb);
    mkplasma_sub(p,nx,x2,ny,y2, turb);
  }
}

static void mkplasma_red(ppm_t *p, float turb)
{
  int x=0, y=0;
  int rowstride = p->width * 3;

  for(x = 0; x < p->width; x++)
    for(y = 0; y < p->height; y++)
      PIXEL(y,x,0) = 0;
  x--; y--;
  PIXEL(0,0,0) = g_rand_int_range (gr, 1, 256);
  PIXEL(y,0,0) = g_rand_int_range (gr, 1, 256);
  PIXEL(0,x,0) = g_rand_int_range (gr, 1, 256);
  PIXEL(y,x,0) = g_rand_int_range (gr, 1, 256);
  mkplasma_sub(p, 0, x, 0, y, turb);
}

void mkgrayplasma(ppm_t *p, float turb)
{
  int y,l;

  mkplasma_red(p, turb);
  l = p->width * 3 * p->height;
  for (y = 0; y < l; y += 3)
    p->col[y+1] = p->col[y+2] = p->col[y];
}
