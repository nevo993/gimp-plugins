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
#include <string.h>
#include <errno.h>
#include <setjmp.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <jpeglib.h>
#include <jerror.h>

#ifdef HAVE_EXIF
#include <libexif/exif-data.h>
#endif /* HAVE_EXIF */

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#include "jpeg.h"
#include "jpeg-save.h"


typedef struct
{
  struct jpeg_compress_struct cinfo;
  struct jpeg_error_mgr jerr;
  gint          tile_height;
  FILE         *outfile;
  gboolean      has_alpha;
  gint          rowstride;
  guchar       *temp;
  guchar       *data;
  guchar       *src;
  GimpDrawable *drawable;
  GimpPixelRgn  pixel_rgn;
  const gchar  *file_name;
  gboolean      abort_me;
} PreviewPersistent;

static gboolean *abort_me = NULL;


static void      make_preview        (void);

static void      save_restart_update (GtkAdjustment    *adjustment,
                                      GtkWidget        *toggle);

static GtkWidget *restart_markers_scale = NULL;
static GtkWidget *restart_markers_label = NULL;
static GtkWidget *preview_size          = NULL;

/*
 * sg - This is the best I can do, I'm afraid... I think it will fail
 * if something bad really happens (but it might not). If you have a
 * better solution, send it ;-)
 */
static void
background_error_exit (j_common_ptr cinfo)
{
  if (abort_me)
    *abort_me = TRUE;
  (*cinfo->err->output_message) (cinfo);
}

static gboolean
background_jpeg_save (PreviewPersistent *pp)
{
  guchar *t;
  guchar *s;
  gint    i, j;
  gint    yend;

  if (pp->abort_me || (pp->cinfo.next_scanline >= pp->cinfo.image_height))
    {
      /* clean up... */
      if (pp->abort_me)
        {
          jpeg_abort_compress (&(pp->cinfo));
        }
      else
        {
          jpeg_finish_compress (&(pp->cinfo));
        }

      fclose (pp->outfile);
      jpeg_destroy_compress (&(pp->cinfo));

      g_free (pp->temp);
      g_free (pp->data);

      if (pp->drawable)
        gimp_drawable_detach (pp->drawable);

      /* display the preview stuff */
      if (!pp->abort_me)
        {
          struct stat buf;
          gchar       temp[128];

          stat (pp->file_name, &buf);
          g_snprintf (temp, sizeof (temp),
                      _("File size: %02.01f kB"),
                      (gdouble) (buf.st_size) / 1024.0);
          gtk_label_set_text (GTK_LABEL (preview_size), temp);

          /* and load the preview */
          load_image (pp->file_name, GIMP_RUN_NONINTERACTIVE, TRUE);
        }

      /* we cleanup here (load_image doesn't run in the background) */
      unlink (pp->file_name);

      if (abort_me == &(pp->abort_me))
        abort_me = NULL;

      g_free (pp);

      gimp_displays_flush ();
      gdk_flush ();

      return FALSE;
    }
  else
    {
      if ((pp->cinfo.next_scanline % pp->tile_height) == 0)
        {
          yend = pp->cinfo.next_scanline + pp->tile_height;
          yend = MIN (yend, pp->cinfo.image_height);
          gimp_pixel_rgn_get_rect (&pp->pixel_rgn, pp->data, 0,
                                   pp->cinfo.next_scanline,
                                   pp->cinfo.image_width,
                                   (yend - pp->cinfo.next_scanline));
          pp->src = pp->data;
        }

      t = pp->temp;
      s = pp->src;
      i = pp->cinfo.image_width;

      while (i--)
        {
          for (j = 0; j < pp->cinfo.input_components; j++)
            *t++ = *s++;
          if (pp->has_alpha)  /* ignore alpha channel */
            s++;
        }

      pp->src += pp->rowstride;
      jpeg_write_scanlines (&(pp->cinfo), (JSAMPARRAY) &(pp->temp), 1);

      return TRUE;
    }
}

gboolean
save_image (const gchar *filename,
            gint32       image_ID,
            gint32       drawable_ID,
            gint32       orig_image_ID,
            gboolean     preview)
{
  GimpPixelRgn   pixel_rgn;
  GimpDrawable  *drawable;
  GimpImageType  drawable_type;
  struct jpeg_compress_struct cinfo;
  struct my_error_mgr         jerr;
  FILE     * volatile outfile;
  guchar   *temp, *t;
  guchar   *data;
  guchar   *src, *s;
  gchar    *name;
  gboolean  has_alpha;
  gint      rowstride, yend;
  gint      i, j;
#ifdef HAVE_EXIF
  gchar    *thumbnail_buffer        = NULL;
  gint      thumbnail_buffer_length = 0;
#endif

  drawable = gimp_drawable_get (drawable_ID);
  drawable_type = gimp_drawable_type (drawable_ID);
  gimp_pixel_rgn_init (&pixel_rgn, drawable,
                       0, 0, drawable->width, drawable->height, FALSE, FALSE);

#ifdef HAVE_EXIF

  /* Create the thumbnail JPEG in a buffer */

  if (jsvals.save_thumbnail)
    thumbnail_buffer_length = create_thumbnail (image_ID, drawable_ID,
                                                &thumbnail_buffer);

#endif /* HAVE_EXIF */

  if (!preview)
    {
      name = g_strdup_printf (_("Saving '%s'..."),
                              gimp_filename_to_utf8 (filename));
      gimp_progress_init (name);
      g_free (name);
    }

  /* Step 1: allocate and initialize JPEG compression object */

  /* We have to set up the error handler first, in case the initialization
   * step fails.  (Unlikely, but it could happen if you are out of memory.)
   * This routine fills in the contents of struct jerr, and returns jerr's
   * address which we place into the link field in cinfo.
   */
  cinfo.err = jpeg_std_error (&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  outfile = NULL;
  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp (jerr.setjmp_buffer))
    {
      /* If we get here, the JPEG code has signaled an error.
       * We need to clean up the JPEG object, close the input file, and return.
       */
      jpeg_destroy_compress (&cinfo);
      if (outfile)
        fclose (outfile);
      if (drawable)
        gimp_drawable_detach (drawable);

      return FALSE;
    }

  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress (&cinfo);

  /* Step 2: specify data destination (eg, a file) */
  /* Note: steps 2 and 3 can be done in either order. */

  /* Here we use the library-supplied code to send compressed data to a
   * stdio stream.  You can also write your own code to do something else.
   * VERY IMPORTANT: use "b" option to fopen() if you are on a machine that
   * requires it in order to write binary files.
   */
  if ((outfile = fopen (filename, "wb")) == NULL)
    {
      g_message (_("Could not open '%s' for writing: %s"),
                 gimp_filename_to_utf8 (filename), g_strerror (errno));
      return FALSE;
    }
  jpeg_stdio_dest (&cinfo, outfile);

  /* Get the input image and a pointer to its data.
   */
  switch (drawable_type)
    {
    case GIMP_RGB_IMAGE:
    case GIMP_GRAY_IMAGE:
      /* # of color components per pixel */
      cinfo.input_components = drawable->bpp;
      has_alpha = FALSE;
      break;

    case GIMP_RGBA_IMAGE:
    case GIMP_GRAYA_IMAGE:
      /*gimp_message ("jpeg: image contains a-channel info which will be lost");*/
      /* # of color components per pixel (minus the GIMP alpha channel) */
      cinfo.input_components = drawable->bpp - 1;
      has_alpha = TRUE;
      break;

    case GIMP_INDEXED_IMAGE:
      /*gimp_message ("jpeg: cannot operate on indexed color images");*/
      return FALSE;
      break;

    default:
      /*gimp_message ("jpeg: cannot operate on unknown image types");*/
      return FALSE;
      break;
    }

  /* Step 3: set parameters for compression */

  /* First we supply a description of the input image.
   * Four fields of the cinfo struct must be filled in:
   */
  /* image width and height, in pixels */
  cinfo.image_width  = drawable->width;
  cinfo.image_height = drawable->height;
  /* colorspace of input image */
  cinfo.in_color_space = (drawable_type == GIMP_RGB_IMAGE ||
                          drawable_type == GIMP_RGBA_IMAGE)
    ? JCS_RGB : JCS_GRAYSCALE;
  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults (&cinfo);

  jpeg_set_quality (&cinfo, (gint) (jsvals.quality + 0.5),
                    jsvals.baseline);
  cinfo.smoothing_factor = (gint) (jsvals.smoothing * 100);
  cinfo.optimize_coding = jsvals.optimize;

#ifdef HAVE_PROGRESSIVE_JPEG
  if (jsvals.progressive)
    {
      jpeg_simple_progression (&cinfo);
    }
#endif /* HAVE_PROGRESSIVE_JPEG */

  switch (jsvals.subsmp)
    {
    case 0:
    default:
      cinfo.comp_info[0].h_samp_factor = 2;
      cinfo.comp_info[0].v_samp_factor = 2;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;

    case 1:
      cinfo.comp_info[0].h_samp_factor = 2;
      cinfo.comp_info[0].v_samp_factor = 1;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;

    case 2:
      cinfo.comp_info[0].h_samp_factor = 1;
      cinfo.comp_info[0].v_samp_factor = 1;
      cinfo.comp_info[1].h_samp_factor = 1;
      cinfo.comp_info[1].v_samp_factor = 1;
      cinfo.comp_info[2].h_samp_factor = 1;
      cinfo.comp_info[2].v_samp_factor = 1;
      break;
    }

  cinfo.restart_interval = 0;
  cinfo.restart_in_rows = jsvals.restart;

  switch (jsvals.dct)
    {
    case 0:
    default:
      cinfo.dct_method = JDCT_ISLOW;
      break;

    case 1:
      cinfo.dct_method = JDCT_IFAST;
      break;

    case 2:
      cinfo.dct_method = JDCT_FLOAT;
      break;
    }

  {
    gdouble xresolution;
    gdouble yresolution;

    gimp_image_get_resolution (orig_image_ID, &xresolution, &yresolution);

    if (xresolution > 1e-5 && yresolution > 1e-5)
      {
        gdouble factor;

        factor = gimp_unit_get_factor (gimp_image_get_unit (orig_image_ID));

        if (factor == 2.54 /* cm */ ||
            factor == 25.4 /* mm */)
          {
            cinfo.density_unit = 2;  /* dots per cm */

            xresolution /= 2.54;
            yresolution /= 2.54;
          }
        else
          {
            cinfo.density_unit = 1;  /* dots per inch */
          }

        cinfo.X_density = xresolution;
        cinfo.Y_density = yresolution;
      }
  }

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress (&cinfo, TRUE);

#ifdef HAVE_EXIF
  if (jsvals.save_exif || jsvals.save_thumbnail)
    {
      guchar *exif_buf;
      guint   exif_buf_len;

      if ( (! jsvals.save_exif) || (! exif_data))
        exif_data = exif_data_new ();

      exif_data->data = thumbnail_buffer;
      exif_data->size = thumbnail_buffer_length;

      exif_data_save_data (exif_data, &exif_buf, &exif_buf_len);
      jpeg_write_marker (&cinfo, 0xe1, exif_buf, exif_buf_len);
      free (exif_buf);
    }
#endif /* HAVE_EXIF */

  /* Step 4.1: Write the comment out - pw */
  if (image_comment && *image_comment)
    {
      jpeg_write_marker (&cinfo, JPEG_COM,
                         (guchar *) image_comment, strlen (image_comment));
    }

  /* Step 5: while (scan lines remain to be written) */
  /*           jpeg_write_scanlines(...); */

  /* Here we use the library's state variable cinfo.next_scanline as the
   * loop counter, so that we don't have to keep track ourselves.
   * To keep things simple, we pass one scanline per call; you can pass
   * more if you wish, though.
   */
  /* JSAMPLEs per row in image_buffer */
  rowstride = drawable->bpp * drawable->width;
  temp = g_new (guchar, cinfo.image_width * cinfo.input_components);
  data = g_new (guchar, rowstride * gimp_tile_height ());

  /* fault if cinfo.next_scanline isn't initially a multiple of
   * gimp_tile_height */
  src = NULL;

  /*
   * sg - if we preview, we want this to happen in the background -- do
   * not duplicate code in the future; for now, it's OK
   */

  if (preview)
    {
      PreviewPersistent *pp = g_new (PreviewPersistent, 1);

      /* pass all the information we need */
      pp->cinfo       = cinfo;
      pp->tile_height = gimp_tile_height();
      pp->data        = data;
      pp->outfile     = outfile;
      pp->has_alpha   = has_alpha;
      pp->rowstride   = rowstride;
      pp->temp        = temp;
      pp->data        = data;
      pp->drawable    = drawable;
      pp->pixel_rgn   = pixel_rgn;
      pp->src         = NULL;
      pp->file_name   = filename;

      pp->abort_me    = FALSE;
      abort_me = &(pp->abort_me);

      pp->cinfo.err = jpeg_std_error(&(pp->jerr));
      pp->jerr.error_exit = background_error_exit;

      g_idle_add ((GSourceFunc) background_jpeg_save, pp);

      /* background_jpeg_save() will cleanup as needed */
      return TRUE;
    }

  while (cinfo.next_scanline < cinfo.image_height)
    {
      if ((cinfo.next_scanline % gimp_tile_height ()) == 0)
        {
          yend = cinfo.next_scanline + gimp_tile_height ();
          yend = MIN (yend, cinfo.image_height);
          gimp_pixel_rgn_get_rect (&pixel_rgn, data,
                                   0, cinfo.next_scanline,
                                   cinfo.image_width,
                                   (yend - cinfo.next_scanline));
          src = data;
        }

      t = temp;
      s = src;
      i = cinfo.image_width;

      while (i--)
        {
          for (j = 0; j < cinfo.input_components; j++)
            *t++ = *s++;
          if (has_alpha)  /* ignore alpha channel */
            s++;
        }

      src += rowstride;
      jpeg_write_scanlines (&cinfo, (JSAMPARRAY) &temp, 1);

      if ((cinfo.next_scanline % 5) == 0)
        gimp_progress_update ((gdouble) cinfo.next_scanline /
                              (gdouble) cinfo.image_height);
    }

  /* Step 6: Finish compression */
  jpeg_finish_compress (&cinfo);
  /* After finish_compress, we can close the output file. */
  fclose (outfile);

  /* Step 7: release JPEG compression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress (&cinfo);
  /* free the temporary buffer */
  g_free (temp);
  g_free (data);

  /* And we're done! */
  /*gimp_do_progress (1, 1);*/

  gimp_drawable_detach (drawable);

  return TRUE;
}

static void
make_preview (void)
{
  destroy_preview ();

  if (jsvals.preview)
    {
      gchar *tn = gimp_temp_name ("jpeg");

      if (! undo_touched)
        {
          /* we freeze undo saving so that we can avoid sucking up
           * tile cache with our unneeded preview steps. */
          gimp_image_undo_freeze (image_ID_global);

          undo_touched = TRUE;
        }

      save_image (tn,
                  image_ID_global,
                  drawable_ID_global,
                  orig_image_ID_global,
                  TRUE);

      if (display_ID == -1)
        display_ID = gimp_display_new (image_ID_global);
    }
  else
    {
      gtk_label_set_text (GTK_LABEL (preview_size), _("File size: unknown"));
      gimp_displays_flush ();
    }

  gtk_widget_set_sensitive (preview_size, jsvals.preview);
}

void
destroy_preview (void)
{
  if (abort_me)
    *abort_me = TRUE;   /* signal the background save to stop */

  if (drawable_global)
    {
      gimp_drawable_detach (drawable_global);
      drawable_global = NULL;
    }

  if (layer_ID_global != -1 && image_ID_global != -1)
    {
      /*  assuming that reference counting is working correctly,
          we do not need to delete the layer, removing it from
          the image should be sufficient  */
      gimp_image_remove_layer (image_ID_global, layer_ID_global);

      layer_ID_global = -1;
    }
}

gboolean
save_dialog (void)
{
  GtkWidget     *dialog;
  GtkWidget     *vbox;
  GtkObject     *entry;
  GtkWidget     *table;
  GtkWidget     *table2;
  GtkWidget     *expander;
  GtkWidget     *frame;
  GtkWidget     *toggle;
  GtkWidget     *spinbutton;
  GtkObject     *scale_data;
  GtkWidget     *label;

  GtkWidget     *progressive;
  GtkWidget     *baseline;
  GtkWidget     *restart;

#ifdef HAVE_EXIF
  GtkWidget     *exif_toggle;
  GtkWidget     *thumbnail_toggle;
#endif /* HAVE_EXIF */

  GtkWidget     *preview;
  GtkWidget     *combo;

  GtkWidget     *text_view;
  GtkTextBuffer *text_buffer;
  GtkWidget     *scrolled_window;

  GimpImageType  dtype;
  gchar         *text;
  gboolean       run;

  dialog = gimp_dialog_new (_("Save as JPEG"), "jpeg",
                            NULL, 0,
                            gimp_standard_help_func, "file-jpeg-save",

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  gtk_window_set_resizable (GTK_WINDOW (dialog), FALSE);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dialog)->vbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  table = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  entry = gimp_scale_entry_new (GTK_TABLE (table), 0, 0, _("_Quality:"),
                                SCALE_WIDTH, 0, jsvals.quality,
                                0.0, 100.0, 1.0, 10.0, 0,
                                TRUE, 0.0, 0.0,
                                _("JPEG quality parameter"),
                                "file-jpeg-save-quality");

  g_signal_connect (entry, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &jsvals.quality);
  g_signal_connect (entry, "value_changed",
                    G_CALLBACK (make_preview),
                    NULL);

  preview_size = gtk_label_new (_("File size: unknown"));
  gtk_misc_set_alignment (GTK_MISC (preview_size), 0.0, 0.5);
  gimp_label_set_attributes (GTK_LABEL (preview_size),
                             PANGO_ATTR_STYLE, PANGO_STYLE_ITALIC,
                             -1);
  gtk_box_pack_start (GTK_BOX (vbox), preview_size, FALSE, FALSE, 0);
  gtk_widget_show (preview_size);

  preview =
    gtk_check_button_new_with_mnemonic (_("Show _Preview in image window"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (preview), jsvals.preview);
  gtk_box_pack_start (GTK_BOX (vbox), preview, FALSE, FALSE, 0);
  gtk_widget_show (preview);

  g_signal_connect (preview, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.preview);
  g_signal_connect (preview, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);


  text = g_strdup_printf ("<b>%s</b>", _("_Advanced Options"));
  expander = gtk_expander_new_with_mnemonic (text);
  gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);
  g_free (text);

  gtk_box_pack_start (GTK_BOX (vbox), expander, TRUE, TRUE, 0);
  gtk_widget_show (expander);

  vbox = gtk_vbox_new (FALSE, 12);
  gtk_container_add (GTK_CONTAINER (expander), vbox);
  gtk_widget_show (vbox);

  frame = gimp_frame_new ("<expander>");
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  table = gtk_table_new (4, 6, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacing (GTK_TABLE (table), 1, 12);
  gtk_container_add (GTK_CONTAINER (frame), table);

  table2 = gtk_table_new (1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table2), 6);
  gtk_table_attach (GTK_TABLE (table), table2,
                    2, 6, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (table2);

  entry = gimp_scale_entry_new (GTK_TABLE (table2), 0, 0, _("_Smoothing:"),
                                100, 0, jsvals.smoothing,
                                0.0, 1.0, 0.01, 0.1, 2,
                                TRUE, 0.0, 0.0,
                                NULL,
                                "file-jpeg-save-smoothing");
  g_signal_connect (entry, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &jsvals.smoothing);
  g_signal_connect (entry, "value_changed",
                    G_CALLBACK (make_preview),
                    NULL);

  restart_markers_label = gtk_label_new (_("Frequency (rows):"));
  gtk_misc_set_alignment (GTK_MISC (restart_markers_label), 1.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), restart_markers_label, 4, 5, 1, 2,
                    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (restart_markers_label);

  restart_markers_scale = spinbutton =
    gimp_spin_button_new (&scale_data,
                          (jsvals.restart == 0) ? 1 : jsvals.restart,
                          1.0, 64.0, 1.0, 1.0, 64.0, 1.0, 0);
  gtk_table_attach (GTK_TABLE (table), spinbutton, 5, 6, 1, 2,
                    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (spinbutton);

  restart = gtk_check_button_new_with_label (_("Use restart markers"));
  gtk_table_attach (GTK_TABLE (table), restart, 2, 4, 1, 2,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (restart);

  gtk_widget_set_sensitive (restart_markers_label, jsvals.restart);
  gtk_widget_set_sensitive (restart_markers_scale, jsvals.restart);

  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (save_restart_update),
                    restart);
  g_signal_connect_swapped (restart, "toggled",
                            G_CALLBACK (save_restart_update),
                            scale_data);

  toggle = gtk_check_button_new_with_label (_("Optimize"));
  gtk_table_attach (GTK_TABLE (table), toggle, 0, 1, 0, 1,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (toggle);

  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.optimize);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), jsvals.optimize);

  progressive = gtk_check_button_new_with_label (_("Progressive"));
  gtk_table_attach (GTK_TABLE (table), progressive, 0, 1, 1, 2,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (progressive);

  g_signal_connect (progressive, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.progressive);
  g_signal_connect (progressive, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (progressive),
                                jsvals.progressive);

#ifndef HAVE_PROGRESSIVE_JPEG
  gtk_widget_set_sensitive (progressive, FALSE);
#endif

  baseline = gtk_check_button_new_with_label (_("Force baseline JPEG"));
  gtk_table_attach (GTK_TABLE (table), baseline, 0, 1, 2, 3,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (baseline);

  g_signal_connect (baseline, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.baseline);
  g_signal_connect (baseline, "toggled",
                    G_CALLBACK (make_preview),
                    NULL);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (baseline),
                                jsvals.baseline);

#ifdef HAVE_EXIF
  exif_toggle = gtk_check_button_new_with_label (_("Save EXIF data"));
  gtk_table_attach (GTK_TABLE (table), exif_toggle, 0, 1, 3, 4,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (exif_toggle);

  g_signal_connect (exif_toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.save_exif);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (exif_toggle),
                                jsvals.save_exif && exif_data);

  gtk_widget_set_sensitive (exif_toggle, exif_data != NULL);

  thumbnail_toggle = gtk_check_button_new_with_label (_("Save thumbnail"));
  gtk_table_attach (GTK_TABLE (table), thumbnail_toggle, 0, 1, 4, 5,
                    GTK_FILL, 0, 0, 0);
  gtk_widget_show (thumbnail_toggle);

  g_signal_connect (thumbnail_toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &jsvals.save_thumbnail);

  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (thumbnail_toggle),
                                jsvals.save_thumbnail);
#endif /* HAVE_EXIF */

  /* Subsampling */
  label = gtk_label_new (_("Subsampling:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 2, 3, 2, 3,
                    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  combo = gimp_int_combo_box_new ("2x2,1x1,1x1",         0,
                                  "2x1,1x1,1x1 (4:2:2)", 1,
                                  "1x1,1x1,1x1",         2,
                                  NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), jsvals.subsmp);
  gtk_table_attach (GTK_TABLE (table), combo, 3, 6, 2, 3,
                    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (combo);

  g_signal_connect (combo, "changed",
                    G_CALLBACK (gimp_int_combo_box_get_active),
                    &jsvals.subsmp);
  g_signal_connect (combo, "changed",
                    G_CALLBACK (make_preview),
                    NULL);

  dtype = gimp_drawable_type (drawable_ID_global);
  if (dtype != GIMP_RGB_IMAGE && dtype != GIMP_RGBA_IMAGE)
    gtk_widget_set_sensitive (combo, FALSE);

  /* DCT method */
  label = gtk_label_new (_("DCT method:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0, 0.5);
  gtk_table_attach (GTK_TABLE (table), label, 2, 3, 3, 4,
                    GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);

  combo = gimp_int_combo_box_new (_("Fast Integer"),   1,
                                  _("Integer"),        0,
                                  _("Floating-Point"), 2,
                                  NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), jsvals.dct);
  gtk_table_attach (GTK_TABLE (table), combo, 3, 6, 3, 4,
                    GTK_FILL | GTK_EXPAND, GTK_FILL, 0, 0);
  gtk_widget_show (combo);

  g_signal_connect (combo, "changed",
                    G_CALLBACK (gimp_int_combo_box_get_active),
                    &jsvals.dct);
  g_signal_connect (combo, "changed",
                    G_CALLBACK (make_preview),
                    NULL);

  frame = gimp_frame_new (_("Comment"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);
  gtk_widget_show (frame);

  scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolled_window),
                                       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_widget_set_size_request (scrolled_window, 250, 50);
  gtk_container_add (GTK_CONTAINER (frame), scrolled_window);
  gtk_widget_show (scrolled_window);

  text_buffer = gtk_text_buffer_new (NULL);
  if (image_comment)
    gtk_text_buffer_set_text (text_buffer, image_comment, -1);

  text_view = gtk_text_view_new_with_buffer (text_buffer);
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (text_view), GTK_WRAP_WORD);

  gtk_container_add (GTK_CONTAINER (scrolled_window), text_view);
  gtk_widget_show (text_view);

  g_object_unref (text_buffer);

  gtk_widget_show (frame);

  gtk_widget_show (table);
  gtk_widget_show (dialog);

  make_preview ();

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  if (run)
    {
      GtkTextIter start_iter;
      GtkTextIter end_iter;

      gtk_text_buffer_get_bounds (text_buffer, &start_iter, &end_iter);
      image_comment = gtk_text_buffer_get_text (text_buffer,
                                                &start_iter, &end_iter, FALSE);
    }

  gtk_widget_destroy (dialog);

  destroy_preview ();
  gimp_displays_flush ();

  return run;
}

static void
save_restart_update (GtkAdjustment *adjustment,
                     GtkWidget     *toggle)
{
  jsvals.restart = GTK_TOGGLE_BUTTON (toggle)->active ? adjustment->value : 0;

  gtk_widget_set_sensitive (restart_markers_label, jsvals.restart);
  gtk_widget_set_sensitive (restart_markers_scale, jsvals.restart);

  make_preview ();
}

#ifdef HAVE_EXIF

gchar *tbuffer = NULL;
gchar *tbuffer2 = NULL;

gint tbuffer_count = 0;

typedef struct
{
  struct jpeg_destination_mgr  pub;   /* public fields */
  gchar                       *buffer;
  gint                         size;
} my_destination_mgr;

typedef my_destination_mgr *my_dest_ptr;

void
init_destination (j_compress_ptr cinfo)
{
}

gboolean
empty_output_buffer (j_compress_ptr cinfo)
{
  my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

  tbuffer_count = tbuffer_count + 16384;
  tbuffer = (gchar *) g_realloc(tbuffer, tbuffer_count);
  g_memmove (tbuffer + tbuffer_count - 16384, tbuffer2, 16384);

  dest->pub.next_output_byte = tbuffer2;
  dest->pub.free_in_buffer   = 16384;

  return TRUE;
}

void
term_destination (j_compress_ptr cinfo)
{
  my_dest_ptr dest = (my_dest_ptr) cinfo->dest;

  tbuffer_count = (tbuffer_count + 16384) - (dest->pub.free_in_buffer);

  tbuffer = (gchar *) g_realloc (tbuffer, tbuffer_count);
  g_memmove(tbuffer + tbuffer_count - (16384 - dest->pub.free_in_buffer), tbuffer2, 16384 - dest->pub.free_in_buffer);
}

gint
create_thumbnail (gint32   image_ID,
                  gint32   drawable_ID,
                  gchar  **thumbnail_buffer)
{
  GimpDrawable  *drawable;
  gint           req_width, req_height, bpp, rbpp;
  guchar        *thumbnail_data = NULL;
  struct jpeg_compress_struct cinfo;
  struct my_error_mgr         jerr;
  my_dest_ptr dest;
  gboolean  alpha = FALSE;
  JSAMPROW  scanline[1];
  guchar   *buf = NULL;
  gint      i;

  drawable = gimp_drawable_get (drawable_ID);

  req_width  = 196;
  req_height = 196;

  if (MIN(drawable->width, drawable->height) < 196)
    req_width = req_height = MIN(drawable->width, drawable->height);

  thumbnail_data = gimp_drawable_get_thumbnail_data (drawable_ID,
                                                     &req_width, &req_height,
                                                     &bpp);

  if (! thumbnail_data)
    return 0;

  rbpp = bpp;

  if ((bpp == 2) || (bpp == 4))
    {
      alpha = TRUE;
      rbpp = bpp - 1;
    }

  buf = (gchar *) g_malloc (req_width * bpp);

  if (!buf)
    return 0;

  tbuffer2 = (gchar *) g_malloc(16384);

  if (!tbuffer2)
    {
      g_free (buf);
      return 0;
    }

  tbuffer_count = 0;

  cinfo.err = jpeg_std_error (&jerr.pub);
  jerr.pub.error_exit = my_error_exit;

  /* Establish the setjmp return context for my_error_exit to use. */
  if (setjmp (jerr.setjmp_buffer))
    {
      /* If we get here, the JPEG code has signaled an error.
       * We need to clean up the JPEG object, free memory, and return.
       */
      jpeg_destroy_compress (&cinfo);

      if (thumbnail_data)
        {
          g_free (thumbnail_data);
          thumbnail_data = NULL;
        }

      if (buf)
        {
          g_free (buf);
          buf = NULL;
        }

      if (tbuffer2)
        {
          g_free (tbuffer2);
          tbuffer2 = NULL;
        }

      if (drawable)
        gimp_drawable_detach (drawable);

      return 0;
    }

  /* Now we can initialize the JPEG compression object. */
  jpeg_create_compress (&cinfo);

  if (cinfo.dest == NULL)
    cinfo.dest = (struct jpeg_destination_mgr *)
      (*cinfo.mem->alloc_small) ((j_common_ptr) &cinfo, JPOOL_PERMANENT,
                                 sizeof(my_destination_mgr));

  dest = (my_dest_ptr) cinfo.dest;
  dest->pub.init_destination    = init_destination;
  dest->pub.empty_output_buffer = empty_output_buffer;
  dest->pub.term_destination    = term_destination;

  dest->pub.next_output_byte = tbuffer2;
  dest->pub.free_in_buffer   = 16384;

  dest->buffer = tbuffer2;
  dest->size   = 16384;

  cinfo.input_components = rbpp;
  cinfo.image_width      = req_width;
  cinfo.image_height     = req_height;

  /* colorspace of input image */
  cinfo.in_color_space = (rbpp == 3) ? JCS_RGB : JCS_GRAYSCALE;

  /* Now use the library's routine to set default compression parameters.
   * (You must set at least cinfo.in_color_space before calling this,
   * since the defaults depend on the source color space.)
   */
  jpeg_set_defaults (&cinfo);

  jpeg_set_quality (&cinfo, (gint) (jsvals.quality + 0.5),
                    jsvals.baseline);

  /* Step 4: Start compressor */

  /* TRUE ensures that we will write a complete interchange-JPEG file.
   * Pass TRUE unless you are very sure of what you're doing.
   */
  jpeg_start_compress (&cinfo, TRUE);

  while (cinfo.next_scanline < (unsigned int) req_height)
    {
      for (i = 0; i < req_width; i++)
        {
          buf[(i * rbpp) + 0] = thumbnail_data[(cinfo.next_scanline * req_width * bpp) + (i * bpp) + 0];

          if (rbpp == 3)
            {
              buf[(i * rbpp) + 1] = thumbnail_data[(cinfo.next_scanline * req_width * bpp) + (i * bpp) + 1];
              buf[(i * rbpp) + 2] = thumbnail_data[(cinfo.next_scanline * req_width * bpp) + (i * bpp) + 2];
            }
        }

      scanline[0] = buf;
      jpeg_write_scanlines (&cinfo, scanline, 1);
  }

  /* Step 6: Finish compression */
  jpeg_finish_compress (&cinfo);

  /* Step 7: release JPEG compression object */

  /* This is an important step since it will release a good deal of memory. */
  jpeg_destroy_compress (&cinfo);

  /* And we're done! */

  if (thumbnail_data)
    {
      g_free (thumbnail_data);
      thumbnail_data = NULL;
    }

  if (buf)
    {
      g_free (buf);
      buf = NULL;
    }

  if (drawable)
    {
      gimp_drawable_detach (drawable);
      drawable = NULL;
    }

  *thumbnail_buffer = tbuffer;

  return tbuffer_count;
}

#endif /* HAVE_EXIF */

