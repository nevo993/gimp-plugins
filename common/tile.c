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

/*
 * This filter tiles an image to arbitrary width and height
 */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gtk/gtk.h>

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

typedef struct
{
  gint new_width;
  gint new_height;
  gint constrain;
  gint new_image;
} TileVals;

typedef struct
{
  GtkObject *width_adj;
  GtkObject *height_adj;
  gint       orig_width;
  gint       orig_height;
  gint       run;
} TileInterface;

/* Declare a local function.
 */
static void      query  (void);
static void      run    (gchar     *name,
			 gint       nparams,
			 GParam    *param,
			 gint      *nreturn_vals,
			 GParam   **return_vals);

static gint32    tile   (gint32     image_id,
			 gint32     drawable_id,
			 gint32    *layer_id);

static gint      tile_dialog               (gint          width,
					    gint          height);

static void      tile_ok_callback         (GtkWidget     *widget,
					   gpointer       data);
static void      tile_chain_button_update (GtkWidget     *widget,
					   gpointer       data);
static void      tile_adjustment_update   (GtkAdjustment *adjustment,
					   gpointer       data);


GPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

static TileVals tvals =
{
  1,     /* new_width  */
  1,     /* new_height */
  TRUE,  /* constrain  */
  TRUE   /* new_image  */
};

static TileInterface tint =
{
  NULL,  /* width_adj    */
  NULL,  /* height_adj   */
  0,     /* orig_width   */
  0,     /* orig_height  */
  FALSE  /* run          */
};

MAIN ()

static void
query (void)
{
  static GParamDef args[] =
  {
    { PARAM_INT32, "run_mode", "Interactive, non-interactive" },
    { PARAM_IMAGE, "image", "Input image (unused)" },
    { PARAM_DRAWABLE, "drawable", "Input drawable" },
    { PARAM_INT32, "new_width", "New (tiled) image width" },
    { PARAM_INT32, "new_height", "New (tiled) image height" },
    { PARAM_INT32, "new_image", "Create a new image?" },
  };
  static GParamDef return_vals[] =
  {
    { PARAM_IMAGE, "new_image", "Output image (N/A if new_image == FALSE)" },
    { PARAM_LAYER, "new_layer", "Output layer (N/A if new_image == FALSE)" },
  };
  static gint nargs = sizeof (args) / sizeof (args[0]);
  static gint nreturn_vals = sizeof (return_vals) / sizeof (return_vals[0]);

  INIT_I18N();

  gimp_install_procedure ("plug_in_tile",
			  "Create a new image which is a tiled version of the input drawable",
			  "This function creates a new image with a single layer sized to the specified 'new_width' and 'new_height' parameters.  The specified drawable is tiled into this layer.  The new layer will have the same type as the specified drawable and the new image will have a corresponding base type",
			  "Spencer Kimball & Peter Mattis",
			  "Spencer Kimball & Peter Mattis",
			  "1996-1997",
			  N_("<Image>/Filters/Map/Tile..."),
			  "RGB*, GRAY*, INDEXED*",
			  PROC_PLUG_IN,
			  nargs, nreturn_vals,
			  args, return_vals);
}

static void
run (gchar   *name,
     gint     nparams,
     GParam  *param,
     gint    *nreturn_vals,
     GParam **return_vals)
{
  static GParam values[3];
  GRunModeType  run_mode;
  GStatusType   status = STATUS_SUCCESS;
  gint32        new_layer;
  gint          width, height;

  run_mode = param[0].data.d_int32;

  *nreturn_vals = 3;
  *return_vals = values;

  values[0].type = PARAM_STATUS;
  values[0].data.d_status = status;
  values[1].type = PARAM_IMAGE;
  values[2].type = PARAM_LAYER;

  width = gimp_drawable_width (param[2].data.d_drawable);
  height = gimp_drawable_height (param[2].data.d_drawable);

  switch (run_mode)
    {
    case RUN_INTERACTIVE:
      INIT_I18N_UI();
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_tile", &tvals);

      /*  First acquire information with a dialog  */
      if (! tile_dialog (width, height))
	return;
      break;

    case RUN_NONINTERACTIVE:
      INIT_I18N();
      /*  Make sure all the arguments are there!  */
      if (nparams != 6)
	status = STATUS_CALLING_ERROR;
      if (status == STATUS_SUCCESS)
	{
	  tvals.new_width = param[3].data.d_int32;
	  tvals.new_height = param[4].data.d_int32;
	  tvals.new_image = (param[5].data.d_int32) ? TRUE : FALSE;
	}
      if (tvals.new_width < 0 || tvals.new_height < 0)
	status = STATUS_CALLING_ERROR;
      break;

    case RUN_WITH_LAST_VALS:
      INIT_I18N();
      /*  Possibly retrieve data  */
      gimp_get_data ("plug_in_tile", &tvals);
      break;

    default:
      break;
    }

  /*  Make sure that the drawable is gray or RGB color  */
  if (status == STATUS_SUCCESS)
    {
      gimp_progress_init ( _("Tiling..."));
      gimp_tile_cache_ntiles (2 * (width + 1) / gimp_tile_width ());

      values[1].data.d_image = tile (param[1].data.d_image, param[2].data.d_drawable, &new_layer);
      values[2].data.d_layer = new_layer;

      /*  Store data  */
      if (run_mode == RUN_INTERACTIVE)
	gimp_set_data ("plug_in_tile", &tvals, sizeof (TileVals));

      if (run_mode != RUN_NONINTERACTIVE)
	{
	  if (tvals.new_image)
	    gimp_display_new (values[1].data.d_image);
	  else
	    gimp_displays_flush ();
	}
    }

  values[0].data.d_status = status;
}

static gint32
tile (gint32  image_id,
      gint32  drawable_id,
      gint32 *layer_id)
{
  GPixelRgn src_rgn, dest_rgn;
  GDrawable *drawable, *new_layer;
  GImageType image_type;
  gint32 new_image_id;
  gint old_width, old_height;
  gint width, height;
  gint i, j, k;
  gint progress, max_progress;
  gint nreturn_vals;
  gpointer pr;

  /* initialize */

  image_type = RGB;
  new_image_id = 0;

  old_width = gimp_drawable_width (drawable_id);
  old_height = gimp_drawable_height (drawable_id);

  if (tvals.new_image)
    {
      /*  create  a new image  */
      switch (gimp_drawable_type (drawable_id))
	{
	case RGB_IMAGE : case RGBA_IMAGE:
	  image_type = RGB;
	  break;
	case GRAY_IMAGE : case GRAYA_IMAGE:
	  image_type = GRAY;
	  break;
	case INDEXED_IMAGE : case INDEXEDA_IMAGE:
	  image_type = INDEXED;
	  break;
	}

      new_image_id = gimp_image_new (tvals.new_width, tvals.new_height,
				     image_type);
      *layer_id = gimp_layer_new (new_image_id, _("Background"),
				  tvals.new_width, tvals.new_height,
				  gimp_drawable_type (drawable_id),
				  100, NORMAL_MODE);
      gimp_image_add_layer (new_image_id, *layer_id, 0);
      new_layer = gimp_drawable_get (*layer_id);

      /*  Get the specified drawable  */
      drawable = gimp_drawable_get (drawable_id);
    }
  else
    {
      gimp_undo_push_group_start (image_id);

      gimp_run_procedure ("gimp_image_resize", &nreturn_vals,
			  PARAM_IMAGE, image_id,
			  PARAM_INT32, tvals.new_width,
			  PARAM_INT32, tvals.new_height,
			  PARAM_INT32, 0,
			  PARAM_INT32, 0,
			  PARAM_END);

      if (gimp_drawable_is_layer (drawable_id))
	gimp_run_procedure ("gimp_layer_resize", &nreturn_vals,
			    PARAM_LAYER, drawable_id,
			    PARAM_INT32, tvals.new_width,
			    PARAM_INT32, tvals.new_height,
			    PARAM_INT32, 0,
			    PARAM_INT32, 0,
			    PARAM_END);

      /*  Get the specified drawable  */
      drawable = gimp_drawable_get (drawable_id);
      new_layer = drawable;
    }

  /*  progress  */
  progress = 0;
  max_progress = tvals.new_width * tvals.new_height;

  /*  tile...  */
  for (i = 0; i < tvals.new_height; i += old_height)
    {
      height = old_height;
      if (height + i > tvals.new_height)
	height = tvals.new_height - i;

      for (j = 0; j < tvals.new_width; j += old_width)
	{
	  width = old_width;
	  if (width + j > tvals.new_width)
	    width = tvals.new_width - j;

	  gimp_pixel_rgn_init (&src_rgn, drawable, 0, 0, width, height, FALSE, FALSE);
	  gimp_pixel_rgn_init (&dest_rgn, new_layer, j, i, width, height, TRUE, FALSE);

	  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn); pr != NULL;
	       pr = gimp_pixel_rgns_process (pr))
	    {
	      for (k = 0; k < src_rgn.h; k++)
		memcpy (dest_rgn.data + k * dest_rgn.rowstride,
			src_rgn.data + k * src_rgn.rowstride,
			src_rgn.w * src_rgn.bpp);

	      progress += src_rgn.w * src_rgn.h;
	      gimp_progress_update ((double) progress / (double) max_progress);
	    }
	}
    }

  /*  copy the colormap, if necessary  */
  if (image_type == INDEXED && tvals.new_image)
    {
      int ncols;
      guchar *cmap;

      cmap = gimp_image_get_cmap (image_id, &ncols);
      gimp_image_set_cmap (new_image_id, cmap, ncols);
      g_free (cmap);
    }

  if (tvals.new_image)
    {
      gimp_drawable_flush (new_layer);
      gimp_drawable_detach (new_layer);
    }
  else
    gimp_run_procedure ("gimp_undo_push_group_end", &nreturn_vals,
			PARAM_IMAGE, image_id,
			PARAM_END);

  gimp_drawable_flush (drawable);
  gimp_drawable_detach (drawable);

  return new_image_id;
}

static gint
tile_dialog (gint width, gint height)
{
  GtkWidget *dlg;
  GtkWidget *frame;
  GtkWidget *table;
  GtkWidget *spinbutton;
  GtkWidget *chain;
  GtkWidget *toggle;
  gchar **argv;
  gint    argc;

  argc    = 1;
  argv    = g_new (gchar *, 1);
  argv[0] = g_strdup ("tile");

  gtk_init (&argc, &argv);
  gtk_rc_parse (gimp_gtkrc ());

  tint.orig_width  = width;
  tint.orig_height = height;
  tvals.new_width  = width;
  tvals.new_height = height;

  dlg = gimp_dialog_new ( _("Tile"), "tile",
			 gimp_plugin_help_func, "filters/tile.html",
			 GTK_WIN_POS_MOUSE,
			 FALSE, TRUE, FALSE,

			 _("OK"), tile_ok_callback,
			 NULL, NULL, NULL, TRUE, FALSE,
			 _("Cancel"), gtk_widget_destroy,
			 NULL, 1, NULL, FALSE, TRUE,

			 NULL);

  gtk_signal_connect (GTK_OBJECT (dlg), "destroy",
		      GTK_SIGNAL_FUNC (gtk_main_quit),
		      NULL);

  /*  parameter settings  */
  frame = gtk_frame_new (_("Tile to New Size"));
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_ETCHED_IN);
  gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), frame, TRUE, TRUE, 0);

  table = gtk_table_new (3, 3, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);

  spinbutton = gimp_spin_button_new (&tint.width_adj, width,
				     1, width, 1, 10, 0, 1, 0);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 0,
			     _("Width:"), 1.0, 0.5,
			     spinbutton, 1, FALSE);
  gtk_signal_connect (GTK_OBJECT (tint.width_adj), "value_changed",
		      GTK_SIGNAL_FUNC (tile_adjustment_update),
		      &tvals.new_width);

  spinbutton = gimp_spin_button_new (&tint.height_adj, height,
				     1, height, 1, 10, 0, 1, 0);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
			     _("Height:"), 1.0, 0.5,
			     spinbutton, 1, FALSE);
  gtk_signal_connect (GTK_OBJECT (tint.height_adj), "value_changed",
		      GTK_SIGNAL_FUNC (tile_adjustment_update),
		      &tvals.new_height);

  chain = gimp_chain_button_new (GIMP_CHAIN_RIGHT);
  gimp_chain_button_set_active (GIMP_CHAIN_BUTTON (chain), tvals.constrain);
  gtk_table_attach (GTK_TABLE (table), chain, 2, 3, 0, 2,
		    GTK_SHRINK, GTK_FILL | GTK_EXPAND, 0, 0);
  gtk_signal_connect (GTK_OBJECT (GIMP_CHAIN_BUTTON (chain)->button), "clicked",
		      GTK_SIGNAL_FUNC (tile_chain_button_update),
		      chain);
  gtk_widget_show (chain);

  toggle = gtk_check_button_new_with_label (_("New Image"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 3, 2, 3,
		    GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
  gtk_signal_connect (GTK_OBJECT (toggle), "toggled",
		      GTK_SIGNAL_FUNC (gimp_toggle_button_update),
		      &tvals.new_image);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), tvals.new_image);
  gtk_widget_show (toggle);

  gtk_widget_show (table);
  gtk_widget_show (frame);
  gtk_widget_show (dlg);

  gtk_main ();
  gdk_flush ();

  return tint.run;
}


/*  Tile interface functions  */

static void
tile_ok_callback (GtkWidget *widget,
		  gpointer   data)
{
  tint.run = TRUE;

  gtk_widget_destroy (GTK_WIDGET (data));
}

static void
tile_chain_button_update (GtkWidget *widget,
			  gpointer   data)
{
  tvals.constrain = gimp_chain_button_get_active (GIMP_CHAIN_BUTTON (data));
}

static void
tile_adjustment_update (GtkAdjustment *adjustment,
			gpointer       data)
{
  gint val;

  val = (gint) adjustment->value;

  if (tvals.constrain)
    {
      if ((tint.orig_width != 0) && (tint.orig_height != 0))
	{
	  if ((GtkObject *) adjustment == tint.width_adj &&
	      tvals.new_width != val)
	    {
	      tvals.new_width = val;

	      tvals.new_height = (int) ((tvals.new_width * tint.orig_height) /
					tint.orig_width);

	      gtk_signal_handler_block_by_data (GTK_OBJECT (tint.height_adj),
						&tvals.new_height);
	      gtk_adjustment_set_value (GTK_ADJUSTMENT (tint.height_adj),
					tvals.new_height);
	      gtk_signal_handler_unblock_by_data (GTK_OBJECT (tint.height_adj),
						  &tvals.new_height);
	    }
	  else if ((GtkObject *) adjustment == tint.height_adj &&
		   tvals.new_height != val)
	    {
	      tvals.new_height = val;

	      tvals.new_width = (int) ((tvals.new_height * tint.orig_width) /
				       tint.orig_height);

	      gtk_signal_handler_block_by_data (GTK_OBJECT (tint.width_adj),
						&tvals.new_width);
	      gtk_adjustment_set_value (GTK_ADJUSTMENT (tint.width_adj),
					tvals.new_width);
	      gtk_signal_handler_unblock_by_data (GTK_OBJECT (tint.width_adj),
						  &tvals.new_width);
	    }
	}
    }
  else
    {
      *((int *) data) = val;
    }
}
