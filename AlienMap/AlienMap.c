/**********************************************************************
 *  AlienMap (Co-)sine color transformation plug-in (Version 1.01)
 *  Daniel Cotting (cotting@mygale.org)
 **********************************************************************
 *  Official Homepage: http://www.mygale.org/~cotting
 **********************************************************************
 *  Homepages under construction: http://www.chez.com/cotting
 *                                http://www.cyberbrain.com/cotting
 *  You won't be able to see anything yet, as I don't really have the 
 *  time to build up these two sites :-( 
 *  Have a look at www.mygale.org/~cotting instead!
 **********************************************************************    
 */

/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <stdio.h>
#include <stdlib.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#include "logo.h"

/***** Magic numbers *****/

#define PREVIEW_SIZE 128
#define SCALE_WIDTH  200
#define ENTRY_WIDTH   45

#define SINUS   0
#define COSINUS 1
#define NONE    2

/***** Types *****/
typedef struct
{
  gdouble redstretch;
  gdouble greenstretch;
  gdouble bluestretch;
  gint    redmode;
  gint    greenmode;
  gint    bluemode;
} alienmap_vals_t;

typedef struct
{
  GtkWidget *preview;
  guchar    *image;
  guchar    *wimage;
  gint run;
} alienmap_interface_t;



/* Declare local functions. */

static void      query  (void);
static void      run    (char      *name,
        		 int        nparams,
        		 GParam    *param,
        		 int       *nreturn_vals,
        		 GParam   **return_vals);

static void      alienmap 	     (GDrawable  *drawable);
static void      alienmap_render_row (const guchar *src_row,
				      guchar *dest_row,
				      gint row,
				      gint row_width,
				      gint bytes, double, double, double);
static void      alienmap_get_pixel  (int x, int y, guchar *pixel);
static void    	 transform           (short int *, short int *, short int *,
				      double, double, double);

static void      build_preview_source_image( void);

static gint      alienmap_dialog        (void);
static void      dialog_update_preview  (void);
static void      dialog_scale_update    (GtkAdjustment *adjustment,
					 gdouble       *value);
static void      dialog_ok_callback     (GtkWidget *widget, gpointer data);
static void      alienmap_toggle_update (GtkWidget *widget,
					 gpointer   data);
static void      alienmap_logo_dialog   (void);

/***** Variables *****/

GtkWidget   *maindlg;
GtkWidget   *logodlg;

GPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static alienmap_interface_t wint =
{
  NULL,  /* preview */
  NULL,  /* image   */
  NULL,  /* wimage  */
  FALSE  /* run     */
};

static alienmap_vals_t wvals =
{
  128,
  128,
  128,
  COSINUS,
  SINUS,
  SINUS,
};

static GDrawable *drawable;
static gint       tile_width, tile_height;
static gint       img_width, img_height, img_bpp;
static gint       sel_x1, sel_y1, sel_x2, sel_y2;
static gint       sel_width, sel_height;
static gint       preview_width, preview_height;
static GTile     *the_tile = NULL;
static gdouble    cen_x, cen_y;
static gdouble    scale_x, scale_y;

/***** Functions *****/

MAIN ()

static void
query (void)
{
  static GParamDef args[] =
  {
    { PARAM_INT32,    "run_mode",     "Interactive, non-interactive" },
    { PARAM_IMAGE,    "image",        "Input image" },
    { PARAM_DRAWABLE, "drawable",     "Input drawable" },
    { PARAM_INT8,    "redstretch",   "Red component stretching factor (0-128)" },
    { PARAM_INT8,    "greenstretch", "Green component stretching factor (0-128)" },
    { PARAM_INT8,    "bluestretch",  "Blue component stretching factor (0-128)" },
    { PARAM_INT8,    "redmode",      "Red application mode (0:SIN;1:COS;2:NONE)" },
    { PARAM_INT8,    "greenmode",    "Green application mode (0:SIN;1:COS;2:NONE)" },
    { PARAM_INT8,    "bluemode",     "Blue application mode (0:SIN;1:COS;2:NONE)" },
  };
  static GParamDef *return_vals = NULL;
  static int nargs = sizeof (args) / sizeof (args[0]);
  static int nreturn_vals = 0;

  INIT_I18N();

  gimp_install_procedure ("plug_in_alienmap",
        		  "AlienMap Color Transformation Plug-In",
        		  "No help yet. Just try it and you'll see!",
        		  "Daniel Cotting (cotting@mygale.org, http://www.mygale.org/~cotting)",
        		  "Daniel Cotting (cotting@mygale.org, http://www.mygale.org/~cotting)",
        		  "1th May 1997",
        		  N_("<Image>/Filters/Colors/Map/Alien Map..."),
        		  "RGB*",
        		  PROC_PLUG_IN,
        		  nargs, nreturn_vals,
        		  args, return_vals);
}

static void
transform (short int *r,
	   short int *g,
	   short int *b,
	   double     redstretch,
	   double     greenstretch,
	   double     bluestretch)
{
  int red, green, blue;
  double pi = atan (1) * 4;

  red = *r;
  green = *g;
  blue = *b;

  switch (wvals.redmode)
    {
    case SINUS:
      red    = (int) redstretch*(1.0+sin((red/128.0-1)*pi));
      break;
    case COSINUS:
      red    = (int) redstretch*(1.0+cos((red/128.0-1)*pi));
      break;
    default:
      break;
    }

  switch (wvals.greenmode)
    {
    case SINUS:
      green    = (int) greenstretch*(1.0+sin((green/128.0-1)*pi));
      break;
    case COSINUS:
      green    = (int) greenstretch*(1.0+cos((green/128.0-1)*pi));
      break;
    default:
      break;
    }

  switch (wvals.bluemode)
    {
    case SINUS:
      blue    = (int) bluestretch*(1.0+sin((blue/128.0-1)*pi));
      break;
    case COSINUS:
      blue    = (int) bluestretch*(1.0+cos((blue/128.0-1)*pi));
      break;
    default:
      break;
    }

  if (red== 256)
    {
      red= 255;
    }
  if (green== 256)
    {
      green= 255;
    }
  if (blue== 256)
    {
      blue= 255;
    }

  *r = red;
  *g = green;
  *b = blue;
}


static void
run (char    *name,
     int      nparams,
     GParam  *param,
     int     *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[1];
/*   GDrawable *drawable; */
/*   gint32 image_ID; */
  GRunModeType  run_mode;
  double        xhsiz, yhsiz;
  int   	pwidth, pheight;
  GStatusType status = STATUS_SUCCESS;
 
  INIT_I18N_UI ();

  run_mode = param[0].data.d_int32;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;

  *nreturn_vals = 1;
  *return_vals = values;

  /*  Get the specified drawable  */
  drawable = gimp_drawable_get (param[2].data.d_drawable);
  /* image_ID = param[1].data.d_image; */
  tile_width  = gimp_tile_width();
  tile_height = gimp_tile_height();

  img_width  = gimp_drawable_width(drawable->id);
  img_height = gimp_drawable_height(drawable->id);
  img_bpp    = gimp_drawable_bpp(drawable->id);

  gimp_drawable_mask_bounds(drawable->id, &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  cen_x = (double) (sel_x2 - 1 + sel_x1) / 2.0;
  cen_y = (double) (sel_y2 - 1 + sel_y1) / 2.0;

  xhsiz = (double) (sel_width - 1) / 2.0;
  yhsiz = (double) (sel_height - 1) / 2.0;

  if (xhsiz < yhsiz)
    {
      scale_x = yhsiz / xhsiz;
      scale_y = 1.0;
    }
  else if (xhsiz > yhsiz)
    {
      scale_x = 1.0;
      scale_y = xhsiz / yhsiz;
    }
  else
    {
      scale_x = 1.0;
      scale_y = 1.0;
    }

  /* Calculate preview size */
  if (sel_width > sel_height)
    {
      pwidth  = MIN (sel_width, PREVIEW_SIZE);
      pheight = sel_height * pwidth / sel_width;
    }
  else
    {
      pheight = MIN (sel_height, PREVIEW_SIZE);
      pwidth  = sel_width * pheight / sel_height;
    }

  preview_width  = MAX (pwidth, 2);  /* Min size is 2 */
  preview_height = MAX (pheight, 2);

  /* See how we will run */
  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      /* Possibly retrieve data */
      gimp_get_data("plug_in_alienmap", &wvals);

      /* Get information from the dialog */
      if (!alienmap_dialog())
	return;

      break;

    case RUN_NONINTERACTIVE:
      /* Make sure all the arguments are present */
      if (nparams != 9)
	status = STATUS_CALLING_ERROR;

      if (status == STATUS_SUCCESS)
	{
	  wvals.redstretch   = param[3].data.d_int8;
	  wvals.greenstretch = param[4].data.d_int8;
	  wvals.bluestretch  = param[5].data.d_int8;
	  wvals.redmode      = param[6].data.d_int8;
	  wvals.greenmode    = param[7].data.d_int8;
	  wvals.bluemode     = param[8].data.d_int8;
	}

      break;

    case RUN_WITH_LAST_VALS:
      /* Possibly retrieve data */
      gimp_get_data ("plug_in_alienmap", &wvals);
      break;

    default:
      break;
    }

  if (status == STATUS_SUCCESS)
    {
      /*  Make sure that the drawable is indexed or RGB color  */
      if (gimp_drawable_is_rgb (drawable->id))
        {
          gimp_progress_init (_("AlienMap: Transforming..."));

	  /* Set the tile cache size */
	  gimp_tile_cache_ntiles(2*(drawable->width / gimp_tile_width()+1));

	  /* Run! */

/*          gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width ()
        			       + 1));*/
          alienmap (drawable);
	  if (run_mode != RUN_NONINTERACTIVE)
	    gimp_displays_flush();

	  /* Store data */
	  if (run_mode == RUN_INTERACTIVE)
	    gimp_set_data("plug_in_alienmap", &wvals, sizeof(alienmap_vals_t));
        }
      else
        {
/*           gimp_message("This filter only applies on RGB-images"); */
          status = STATUS_EXECUTION_ERROR;
        }
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

static void
alienmap_get_pixel (int     x,
		    int     y,
		    guchar *pixel)
{
  static gint row  = -1;
  static gint col  = -1;

  gint    newcol, newrow;
  gint    newcoloff, newrowoff;
  guchar *p;
  int     i;

  if ((x < 0) || (x >= img_width) || (y < 0) || (y >= img_height))
    {
      pixel[0] = 0;
      pixel[1] = 0;
      pixel[2] = 0;
      pixel[3] = 0;

      return;
    }

  newcol    = x / tile_width; /* The compiler should optimize this */
  newcoloff = x % tile_width;
  newrow    = y / tile_height;
  newrowoff = y % tile_height;

  if ((col != newcol) || (row != newrow) || (the_tile == NULL))
    {
      if (the_tile != NULL)
	gimp_tile_unref(the_tile, FALSE);

      the_tile = gimp_drawable_get_tile(drawable, FALSE, newrow, newcol);
      gimp_tile_ref(the_tile);
      col = newcol;
      row = newrow;
    }
  p = the_tile->data + the_tile->bpp * (the_tile->ewidth * newrowoff + newcoloff);
  for (i = img_bpp; i; i--)
    *pixel++ = *p++;
}

static void
alienmap_render_row (const guchar *src_row,
		     guchar       *dest_row,
		     gint          row,
		     gint          row_width,
		     gint          bytes,
		     double        redstretch,
		     double        greenstretch,
		     double        bluestretch)
{
  gint col, bytenum;

  for (col = 0; col < row_width ; col++)
    {
      short int v1, v2, v3;

      v1 = (short int)src_row[col*bytes];
      v2 = (short int)src_row[col*bytes +1];
      v3 = (short int)src_row[col*bytes +2];

      transform(&v1, &v2, &v3, redstretch, greenstretch, bluestretch);

      dest_row[col*bytes] = (int)v1;
      dest_row[col*bytes +1] = (int)v2;
      dest_row[col*bytes +2] = (int)v3;

      if (bytes>3)
        for (bytenum = 3; bytenum<bytes; bytenum++)
          {
            dest_row[col*bytes+bytenum] = src_row[col*bytes+bytenum];
          }
    }
}

static void
alienmap (GDrawable *drawable)
{
  GPixelRgn srcPR, destPR;
  gint width, height;
  gint bytes;
  guchar *src_row;
  guchar *dest_row;
  gint row;
  gint x1, y1, x2, y2;
  double redstretch,greenstretch,bluestretch;

  /* Get the input area. This is the bounding box of the selection in
   *  the image (or the entire image if there is no selection). Only
   *  operating on the input area is simply an optimization. It doesn't
   *  need to be done for correct operation. (It simply makes it go
   *  faster, since fewer pixels need to be operated on).
   */
  gimp_drawable_mask_bounds (drawable->id, &x1, &y1, &x2, &y2);

  /* Get the size of the input image. (This will/must be the same
   *  as the size of the output image.
   */
  width = drawable->width;
  height = drawable->height;
  bytes = drawable->bpp;

  /*  allocate row buffers  */
  src_row = (guchar *) malloc ((x2 - x1) * bytes);
  dest_row = (guchar *) malloc ((x2 - x1) * bytes);

  /*  initialize the pixel regions  */
  gimp_pixel_rgn_init (&srcPR, drawable, 0, 0, width, height, FALSE, FALSE);
  gimp_pixel_rgn_init (&destPR, drawable, 0, 0, width, height, TRUE, TRUE);

  redstretch = wvals.redstretch;
  greenstretch = wvals.greenstretch;
  bluestretch = wvals.bluestretch;

  for (row = y1; row < y2; row++)
    {
      gimp_pixel_rgn_get_row (&srcPR, src_row, x1, row, (x2 - x1));

      alienmap_render_row (src_row,
        		dest_row,
        		row,
        		(x2 - x1),
        		bytes,
        		redstretch, greenstretch, bluestretch);

      /*  store the dest  */
      gimp_pixel_rgn_set_row (&destPR, dest_row, x1, row, (x2 - x1));

      if ((row % 10) == 0)
        gimp_progress_update ((double) row / (double) (y2 - y1));
    }

  /*  update the processed region  */
  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->id, TRUE);
  gimp_drawable_update (drawable->id, x1, y1, (x2 - x1), (y2 - y1));

  free (src_row);
  free (dest_row);
}

static void
build_preview_source_image (void)
{
  double  left, right, bottom, top;
  double  px, py;
  double  dx, dy;
  int     x, y;
  guchar *p;
  guchar  pixel[4];

  wint.image  = g_malloc(preview_width * preview_height * 3 * sizeof(guchar));
  wint.wimage = g_malloc(preview_width * preview_height * 3 * sizeof(guchar));

  left   = sel_x1;
  right  = sel_x2 - 1;
  bottom = sel_y2 - 1;
  top    = sel_y1;

  dx = (right - left) / (preview_width - 1);
  dy = (bottom - top) / (preview_height - 1);

  py = top;

  p = wint.image;

  for (y = 0; y < preview_height; y++)
    {
      px = left;
      for (x = 0; x < preview_width; x++)
	{
	  alienmap_get_pixel((int) px, (int) py, pixel);

	  *p++ = pixel[0];
	  *p++ = pixel[1];
	  *p++ = pixel[2];

	  px += dx;
	}

      py += dy;
    }
}

static gint
alienmap_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *top_table;
  GtkWidget *frame;
  GtkWidget *toggle1;
  GtkWidget *toggle2;
  GtkWidget *toggle3;
  GtkWidget *table;
  GtkObject *adj;

  gimp_ui_init ("alienmap", TRUE);

  build_preview_source_image ();

  dialog = maindlg =
    gimp_dialog_new (_("AlienMap"), "alienmap",
		     gimp_plugin_help_func, "filters/alienmap.html",
		     GTK_WIN_POS_MOUSE,
		     FALSE, TRUE, FALSE,

		     _("About"), alienmap_logo_dialog,
		     NULL, NULL, NULL, FALSE, FALSE,
		     _("OK"), dialog_ok_callback,
		     NULL, NULL, NULL, TRUE, FALSE,
		     _("Cancel"), gtk_widget_destroy,
		     NULL, 1, NULL, FALSE, TRUE,

		     NULL);

  gtk_signal_connect (GTK_OBJECT (dialog), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  top_table = gtk_table_new (2, 4, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (top_table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (top_table), 4);
  gtk_table_set_col_spacings (GTK_TABLE (top_table), 4);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), top_table,
		      FALSE, FALSE, 0);
  gtk_widget_show (top_table);

  gimp_help_init ();

  /* Preview */
  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_table_attach (GTK_TABLE (top_table), frame, 0, 1, 0, 1, 0, 0, 0, 0);
  gtk_widget_show (frame);

  wint.preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (wint.preview), preview_width, preview_height);
  gtk_container_add (GTK_CONTAINER (frame), wint.preview);
  gtk_widget_show (wint.preview);

  /* Controls */
  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_attach (GTK_TABLE (top_table), table, 0, 4, 1, 2,
		    GTK_EXPAND | GTK_FILL, 0, 0, 0);
  gtk_widget_show (table);

  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
			      _("Red:"), SCALE_WIDTH, 0,
			      wvals.redstretch, 0, 128, 1, 8, 2,
			      TRUE, 0, 0,
			      _("Change intensity of the red channel"), NULL);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
		      GTK_SIGNAL_FUNC (dialog_scale_update),
		      &wvals.redstretch);
			      
  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
			      _("Green:"), SCALE_WIDTH, 0,
			      wvals.greenstretch, 0, 128, 1, 8, 2,
			      TRUE, 0, 0,
			      _("Change intensity of the green channel"), NULL);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
		      GTK_SIGNAL_FUNC (dialog_scale_update),
		      &wvals.greenstretch);
			      
  adj = gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
			      _("Blue:"), SCALE_WIDTH, 0,
			      wvals.bluestretch, 0, 128, 1, 8, 2,
			      TRUE, 0, 0,
			      _("Change intensity of the blue channel"), NULL);
  gtk_signal_connect (GTK_OBJECT (adj), "value_changed",
		      GTK_SIGNAL_FUNC (dialog_scale_update),
		      &wvals.bluestretch);

  /*  Redmode toggle box  */
  frame = gimp_radio_group_new2 (TRUE, _("Red"),
				 alienmap_toggle_update,
				 &wvals.redmode, (gpointer) wvals.redmode,

				 _("Sine"),   (gpointer) SINUS, &toggle1,
				 _("Cosine"), (gpointer) COSINUS, &toggle2,
				 _("None"),   (gpointer) NONE, &toggle3,

				 NULL);

  gimp_help_set_help_data (toggle1,
			   _("Use sine-function for red component."), NULL);
  gimp_help_set_help_data (toggle2,
			   _("Use cosine-function for red component."), NULL);
  gimp_help_set_help_data (toggle3,
			   _("Red channel: use linear mapping instead "
			     "of any trigonometrical function"), NULL);

  gtk_table_attach (GTK_TABLE (top_table), frame, 1, 2, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
  gtk_widget_show (frame);

  /*  Greenmode toggle box  */
  frame = gimp_radio_group_new2 (TRUE, _("Green"),
				 alienmap_toggle_update,
				 &wvals.greenmode, (gpointer) wvals.greenmode,

				 _("Sine"),   (gpointer) SINUS, &toggle1,
				 _("Cosine"), (gpointer) COSINUS, &toggle2,
				 _("None"),   (gpointer) NONE, &toggle3,

				 NULL);

  gimp_help_set_help_data (toggle1,
			   _("Use sine-function for green component."), NULL);
  gimp_help_set_help_data (toggle2,
			   _("Use cosine-function for green component."), NULL);
  gimp_help_set_help_data (toggle3,
			   _("Green channel: use linear mapping instead "
			     "of any trigonometrical function"), NULL);

  gtk_table_attach (GTK_TABLE (top_table), frame, 2, 3, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
  gtk_widget_show (frame);

  /*  Bluemode toggle box  */
  frame = gimp_radio_group_new2 (TRUE, _("Blue"),
				 alienmap_toggle_update,
				 &wvals.bluemode, (gpointer) wvals.bluemode,

				 _("Sine"),   (gpointer) SINUS, &toggle1,
				 _("Cosine"), (gpointer) COSINUS, &toggle2,
				 _("None"),   (gpointer) NONE, &toggle3,

				 NULL);

  gimp_help_set_help_data (toggle1,
			   _("Use sine-function for blue component."), NULL);
  gimp_help_set_help_data (toggle2,
			   _("Use cosine-function for blue component."), NULL);
  gimp_help_set_help_data (toggle3,
			   _("Blue channel: use linear mapping instead "
			     "of any trigonometrical function"), NULL);

  gtk_table_attach (GTK_TABLE (top_table), frame, 3, 4, 0, 1,
		    GTK_FILL | GTK_EXPAND, GTK_FILL | GTK_EXPAND, 0, 0);
  gtk_widget_show (frame);

  gtk_widget_show (dialog);
  dialog_update_preview ();

  gtk_main ();
  gimp_help_free ();
  gdk_flush ();

  if (the_tile != NULL)
    {
      gimp_tile_unref (the_tile, FALSE);
      the_tile = NULL;
    }

  g_free (wint.image);
  g_free (wint.wimage);

  return wint.run;
}

static void
dialog_update_preview (void)
{
  double  left, right, bottom, top;
  double  dx, dy;
  int  px, py;
  int     x, y;
  double  redstretch, greenstretch, bluestretch;
  short int r,g,b;
  double  scale_x, scale_y;
  guchar *p_ul, *i, *p;

  left   = sel_x1;
  right  = sel_x2 - 1;
  bottom = sel_y2 - 1;
  top    = sel_y1;
  dx = (right - left) / (preview_width - 1);
  dy = (bottom - top) / (preview_height - 1);

  redstretch = wvals.redstretch;
  greenstretch = wvals.greenstretch;
  bluestretch = wvals.bluestretch;

  scale_x = (double) (preview_width - 1) / (right - left);
  scale_y = (double) (preview_height - 1) / (bottom - top);

  py = 0;

  p_ul = wint.wimage;

  for (y = 0; y < preview_height; y++)
    {
      px = 0;

      for (x = 0; x < preview_width; x++)
	{
	  i = wint.image + 3 * (preview_width * py + px);
	  r = *i++;
	  g = *i++;
	  b = *i;
	  transform(&r,&g,&b,redstretch, greenstretch, bluestretch);
	  p_ul[0] = r;
	  p_ul[1] = g;
	  p_ul[2] = b;
	  p_ul += 3;
	  px += 1; /* dx; */
	}
      py +=1; /* dy; */
    }

  p = wint.wimage;

  for (y = 0; y < preview_height; y++)
    {
      gtk_preview_draw_row(GTK_PREVIEW(wint.preview), p, 0, y, preview_width);
      p += preview_width * 3;
    }
  gtk_widget_draw(wint.preview, NULL);
  gdk_flush();
}

static void
dialog_scale_update (GtkAdjustment *adjustment,
		     gdouble       *value)
{
  gimp_double_adjustment_update (adjustment, value);

  dialog_update_preview ();
}

static void
dialog_ok_callback (GtkWidget *widget,
		    gpointer   data)
{
  wint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
alienmap_toggle_update (GtkWidget *widget,
        		gpointer   data)
{
  gimp_radio_button_update (widget, data);

  dialog_update_preview ();
}

static void
alienmap_logo_dialog (void)
{
  GtkWidget *xlabel;
  GtkWidget *xlogo_box;
  GtkWidget *xpreview;
  GtkWidget *xframe, *xframe2;
  GtkWidget *xvbox;
  GtkWidget *xhbox;
  gchar  *text;
  guchar *temp, *temp2;
  guchar *datapointer;
  gint    y,x;

  if (!logodlg)
    {
      logodlg = gimp_dialog_new (_("About AlienMap"), "alienmap",
				 gimp_plugin_help_func, "filters/alienmap.html",
				 GTK_WIN_POS_MOUSE,
				 FALSE, TRUE, FALSE,

				 _("OK"), gtk_widget_hide,
				 NULL, 1, NULL, TRUE, TRUE,

				 NULL);

      gtk_signal_connect (GTK_OBJECT (logodlg), "destroy",
			  GTK_SIGNAL_FUNC (gtk_widget_destroyed),
			  &logodlg);
      gtk_quit_add_destroy (1, GTK_OBJECT (logodlg));

      xframe = gtk_frame_new (NULL);
      gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_ETCHED_IN);
      gtk_container_set_border_width (GTK_CONTAINER (xframe), 6);
      gtk_box_pack_start (GTK_BOX (GTK_DIALOG(logodlg)->vbox), xframe,
			  TRUE, TRUE, 0);
      xvbox = gtk_vbox_new (FALSE, 4);
      gtk_container_border_width (GTK_CONTAINER (xvbox), 4);
      gtk_container_add (GTK_CONTAINER (xframe), xvbox);

      /*  The logo frame & drawing area  */
      xhbox = gtk_hbox_new (FALSE, 5);
      gtk_box_pack_start (GTK_BOX (xvbox), xhbox, FALSE, TRUE, 0);
      
      xlogo_box = gtk_vbox_new (FALSE, 0);
      gtk_box_pack_start (GTK_BOX (xhbox), xlogo_box, FALSE, FALSE, 0);
      
      xframe2 = gtk_frame_new (NULL);
      gtk_frame_set_shadow_type (GTK_FRAME (xframe2), GTK_SHADOW_IN);
      gtk_box_pack_start (GTK_BOX (xlogo_box), xframe2, FALSE, FALSE, 0);
      
      xpreview = gtk_preview_new (GTK_PREVIEW_COLOR);
      gtk_preview_size (GTK_PREVIEW (xpreview), logo_width, logo_height);
      temp = g_malloc ((logo_width + 10) * 3);
      datapointer = header_data+logo_width*logo_height-1;

      for (y = 0; y < logo_height; y++)
        {
	  temp2=temp;
	  for (x = 0; x< logo_width; x++)
	    {
	      HEADER_PIXEL (datapointer,temp2); 
	      temp2+=3;
	    }
	  gtk_preview_draw_row (GTK_PREVIEW (xpreview),
				temp,
				0, y, logo_width); 
	}			  
      g_free (temp);
      gtk_container_add (GTK_CONTAINER (xframe2), xpreview);
      gtk_widget_show (xpreview);
      gtk_widget_show (xframe2);
      gtk_widget_show (xlogo_box);
      gtk_widget_show (xhbox);

      xhbox = gtk_hbox_new (FALSE, 4);
      gtk_box_pack_start (GTK_BOX(xvbox), xhbox, TRUE, TRUE, 0);
      text = ("\nCotting Software Productions\n"
	      "Bahnhofstrasse 31\n"
	      "CH-3066 Stettlen (Switzerland)\n\n"
	      "cotting@mygale.org\n"
	      "http://www.mygale.org/~cotting\n\n"
	      "AlienMap Plug-In for the GIMP\n"
	      "Version 1.01\n");
      xlabel = gtk_label_new (text);
      gtk_box_pack_start (GTK_BOX(xhbox), xlabel, TRUE, FALSE, 0);
      gtk_widget_show (xlabel);
      
      gtk_widget_show (xhbox);
      
      gtk_widget_show (xvbox);
      gtk_widget_show (xframe);
      gtk_widget_show (logodlg);
    }
  else
    {
      gtk_widget_show (logodlg);
      gdk_window_raise (logodlg->window);
    }
}
