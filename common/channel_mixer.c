/****************************************************************************
 * This is a plugin for the GIMP v 2.0 or later.
 *
 * Copyright (C) 2002 Martin Guldahl <mguldahl@xmission.com>
 * Based on GTK code from:
 *    homomorphic (Copyright (C) 2001 Valter Marcus Hilden)
 *    rand-noted  (Copyright (C) 1998 Miles O'Neal)
 *    nlfilt      (Copyright (C) 1997 Eric L. Hernes)
 *    pagecurl    (Copyright (C) 1996 Federico Mena Quintero)
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************************/

#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#ifdef G_OS_WIN32
#include <libgimpbase/gimpwin32-io.h>
#endif

#include <libgimpmath/gimpmath.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"


#define PLUG_IN_NAME        "plug_in_colors_channel_mixer"
#define PLUG_IN_VERSION     "Channel Mixer 0.8"
#define HELP_ID             "plug-in-colors-channel-mixer"
#define PROGRESS_UPDATE_NUM 20
#define CM_LINE_SIZE        1024

typedef enum
{
  CM_RED_CHANNEL,
  CM_GREEN_CHANNEL,
  CM_BLUE_CHANNEL
} CmModeType;

typedef struct
{
  gdouble red_gain;
  gdouble green_gain;
  gdouble blue_gain;
} CmChannelType;

typedef struct
{
  CmChannelType  red;
  CmChannelType  green;
  CmChannelType  blue;
  CmChannelType  black;

  gboolean       monochrome_flag;
  gboolean       preview_flag;
  gboolean       preserve_luminosity_flag;

  CmModeType     output_channel;

  gchar         *filename;

  GtkAdjustment *red_data;
  GtkAdjustment *green_data;
  GtkAdjustment *blue_data;

  GtkWidget     *combo;

  CmModeType     old_output_channel;

  GtkWidget     *monochrome_toggle;
  GtkWidget     *preview;
  GtkWidget     *preview_toggle;
  GtkWidget     *preserve_luminosity_toggle;
} CmParamsType;

typedef struct
{
  gint     width;
  gint     height;
  gint     bpp;
  gdouble  scale;
  guchar  *bits;
} mwPreview;

#define PREVIEW_SIZE 200


static mwPreview *mw_preview_build_virgin (GimpDrawable *drw);
static mwPreview *mw_preview_build        (GimpDrawable *drw);

static void     query (void);
static void     run   (const gchar      *name,
                       gint              nparams,
                       const GimpParam  *param,
                       gint             *nreturn_vals,
                       GimpParam       **return_vals);

static void     channel_mixer (GimpDrawable *drawable);
static gboolean cm_dialog     (void);

static void cm_red_scale_callback           (GtkAdjustment    *adjustment,
                                             CmParamsType     *mix);
static void cm_green_scale_callback         (GtkAdjustment    *adjustment,
                                             CmParamsType     *mix);
static void cm_blue_scale_callback          (GtkAdjustment    *adjustment,
                                             CmParamsType     *mix);
static void cm_preview_callback             (GtkWidget        *widget,
                                             CmParamsType     *mix);
static void cm_monochrome_callback          (GtkWidget        *widget,
                                             CmParamsType     *mix);
static void cm_preserve_luminosity_callback (GtkWidget        *widget,
                                             CmParamsType     *mix);
static void cm_load_file_callback           (GtkWidget        *widget,
                                             CmParamsType     *mix);
static void cm_load_file_response_callback  (GtkFileSelection *filesel,
                                             gint              response_id,
                                             CmParamsType     *mix);
static void cm_save_file_callback           (GtkWidget        *widget,
                                             CmParamsType     *mix);
static void cm_save_file_response_callback  (GtkFileSelection *filesel,
                                             gint              response_id,
                                             CmParamsType     *mix);
static void cm_combo_callback               (GtkWidget        *widget,
                                             CmParamsType     *mix);

static gboolean cm_force_overwrite (const gchar *filename,
                                    GtkWidget   *parent);

static gdouble cm_calculate_norm (CmParamsType  *mix,
                                  CmChannelType *ch);

static inline guchar cm_mix_pixel (CmChannelType *ch,
                                   guchar         r,
                                   guchar         g,
                                   guchar         b,
                                   gdouble        norm);

static void cm_preview       (CmParamsType *mix);
static void cm_set_adjusters (CmParamsType *mix);

static void cm_save_file (CmParamsType *mix,
                          FILE         *fp);


GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run    /* run_proc   */
};

static CmParamsType mix =
{
  { 1.0, 0.0, 0.0 },
  { 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 1.0 },
  { 1.0, 0.0, 0.0 },
  FALSE,
  TRUE,
  FALSE,
  CM_RED_CHANNEL,
  NULL
};

static mwPreview *preview;

static gint sel_x1, sel_y1, sel_x2, sel_y2;
static gint sel_width, sel_height;


MAIN ()

static void
query (void)
{
  static GimpParamDef args[] =
  {
    { GIMP_PDB_INT32, "run_mode", "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE, "image", "Input image (unused)" },
    { GIMP_PDB_DRAWABLE, "drawable", "Input drawable" },
    { GIMP_PDB_INT32, "monochrome", "Monochrome (TRUE or FALSE)" },
    { GIMP_PDB_FLOAT, "rr_gain", "Set the red gain for the red channel" },
    { GIMP_PDB_FLOAT, "rg_gain", "Set the green gain for the red channel" },
    { GIMP_PDB_FLOAT, "rb_gain", "Set the blue gain for the red channel" },
    { GIMP_PDB_FLOAT, "gr_gain", "Set the red gain for the green channel" },
    { GIMP_PDB_FLOAT, "gg_gain", "Set the green gain for the green channel" },
    { GIMP_PDB_FLOAT, "gb_gain", "Set the blue gain for the green channel" },
    { GIMP_PDB_FLOAT, "br_gain", "Set the red gain for the blue channel" },
    { GIMP_PDB_FLOAT, "bg_gain", "Set the green gain for the blue channel" },
    { GIMP_PDB_FLOAT, "bb_gain", "Set the blue gain for the blue channel" }
  };

  gimp_install_procedure (PLUG_IN_NAME,
                          "Mix RGB Channels.",
                          "This plug-in mixes the RGB channels.",
                          "Martin Guldahl <mguldahl@xmission.com>",
                          "Martin Guldahl <mguldahl@xmission.com>",
                          "2002",
                          N_("<Image>/Filters/Colors/Channel Mi_xer.."),
                          "RGB*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (args), 0,
                          args, NULL);
}

/*----------------------------------------------------------------------
 *  run() - main routine
 *
 *  This handles the main interaction with the GIMP itself,
 *  and invokes the routine that actually does the work.
 *--------------------------------------------------------------------*/
static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[1];
  GimpDrawable      *drawable;
  GimpRunMode        run_mode;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;

  run_mode = param[0].data.d_int32;

  INIT_I18N ();

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = status;

  drawable = gimp_drawable_get (param[2].data.d_drawable);

  gimp_drawable_mask_bounds (drawable->drawable_id,
                             &sel_x1, &sel_y1, &sel_x2, &sel_y2);

  sel_width  = sel_x2 - sel_x1;
  sel_height = sel_y2 - sel_y1;

  if (gimp_drawable_is_rgb (drawable->drawable_id))
    {
      switch (run_mode)
        {
        case GIMP_RUN_INTERACTIVE:
          gimp_get_data (PLUG_IN_NAME, &mix);

          preview = mw_preview_build (drawable);

          if (! cm_dialog ())
            {
              gimp_drawable_detach (drawable);
              return;
            }

          break;

        case GIMP_RUN_NONINTERACTIVE:
          mix.monochrome_flag = param[3].data.d_int32;

          if (mix.monochrome_flag == 1)
            {
              mix.black.red_gain   = param[4].data.d_float;
              mix.black.green_gain = param[5].data.d_float;
              mix.black.blue_gain  = param[6].data.d_float;
            }
          else
            {
              mix.red.red_gain     = param[4].data.d_float;
              mix.red.green_gain   = param[5].data.d_float;
              mix.red.blue_gain    = param[6].data.d_float;
              mix.green.red_gain   = param[7].data.d_float;
              mix.green.green_gain = param[8].data.d_float;
              mix.green.blue_gain  = param[9].data.d_float;
              mix.blue.red_gain    = param[10].data.d_float;
              mix.blue.green_gain  = param[11].data.d_float;
              mix.blue.blue_gain   = param[12].data.d_float;
            }
          break;

        case GIMP_RUN_WITH_LAST_VALS:
          gimp_get_data (PLUG_IN_NAME, &mix);
          break;

        default:
          break;
        }

      if (status == GIMP_PDB_SUCCESS)
        {
          /* printf("Channel Mixer:: Mode:%d  r %f  g %f  b %f\n ",
                 param[3].data.d_int32, mix.black.red_gain,
                 mix.black.green_gain, mix.black.blue_gain); */

          gimp_progress_init (_(PLUG_IN_VERSION));

          channel_mixer (drawable);

          if (run_mode != GIMP_RUN_NONINTERACTIVE)
            gimp_displays_flush ();

          if (run_mode == GIMP_RUN_INTERACTIVE)
            gimp_set_data (PLUG_IN_NAME, &mix, sizeof (CmParamsType));
        }
    }
  else
    {
      status = GIMP_PDB_EXECUTION_ERROR;
    }

  values[0].data.d_status = status;

  gimp_drawable_detach (drawable);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static gdouble
cm_calculate_norm (CmParamsType  *mix,
                   CmChannelType *ch)
{
  gdouble sum;

  sum = ch->red_gain + ch->green_gain + ch->blue_gain;

  if (sum == 0.0 || mix->preserve_luminosity_flag == FALSE)
    return 1.0;

  return fabs (1 / sum);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static inline guchar
cm_mix_pixel (CmChannelType *ch,
              guchar         r,
              guchar         g,
              guchar         b,
              gdouble        norm)
{
  gdouble c = ch->red_gain * r + ch->green_gain * g + ch->blue_gain * b;

  c *= norm;

  if (c > 255.0)
    c = 255.0;

  if (c < 0.0)
    c = 0.0;

  return (guchar) c;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
channel_mixer (GimpDrawable *drawable)
{
  GimpPixelRgn  src_rgn, dest_rgn;
  guchar       *src, *dest;
  gpointer      pr;
  gint          x, y;
  gint          total, processed = 0;
  gboolean      has_alpha;
  gdouble       red_norm, green_norm, blue_norm, black_norm;

  has_alpha = gimp_drawable_has_alpha (drawable->drawable_id);

  total = sel_width * sel_height;

  gimp_tile_cache_ntiles (2 * (drawable->width / gimp_tile_width () + 1));

  gimp_pixel_rgn_init (&src_rgn, drawable,
                       sel_x1, sel_y1, sel_width, sel_height, FALSE, FALSE);
  gimp_pixel_rgn_init (&dest_rgn, drawable,
                       sel_x1, sel_y1, sel_width, sel_height, TRUE, TRUE);

  red_norm   = cm_calculate_norm (&mix, &mix.red);
  green_norm = cm_calculate_norm (&mix, &mix.green);
  blue_norm  = cm_calculate_norm (&mix, &mix.blue);
  black_norm = cm_calculate_norm (&mix, &mix.black);

  for (pr = gimp_pixel_rgns_register (2, &src_rgn, &dest_rgn);
       pr != NULL; pr = gimp_pixel_rgns_process (pr))
    {
      gint offset;

      for (y = 0; y < src_rgn.h; y++)
        {
          src  = src_rgn.data  + y * src_rgn.rowstride;
          dest = dest_rgn.data + y * dest_rgn.rowstride;

          offset = 0;

          for (x = 0; x < src_rgn.w; x++)
            {
              guchar r, g, b;

              r = *(src + offset);
              g = *(src + offset + 1);
              b = *(src + offset + 2);

              if (mix.monochrome_flag == TRUE)
                {
                  *(dest + offset) =
                    *(dest + offset + 1) =
                    *(dest + offset + 2) =
                    cm_mix_pixel (&mix.black, r, g, b, black_norm);
                }
              else
                {
                  *(dest + offset) =
                    cm_mix_pixel (&mix.red, r, g, b, red_norm);
                  *(dest + offset + 1) =
                    cm_mix_pixel (&mix.green, r, g, b, green_norm);
                  *(dest + offset + 2) =
                    cm_mix_pixel (&mix.blue, r, g, b, blue_norm);
                }

              offset += 3;

              if (has_alpha)
                {
                  *(dest + offset) = *(src + offset);
                  offset++;
                }

              /* update progress */
              if ((++processed % (total / PROGRESS_UPDATE_NUM + 1)) == 0)
                gimp_progress_update ((gdouble) processed / (gdouble) total);
            }
        }
    }

  gimp_progress_update (1.0);

  gimp_drawable_flush (drawable);
  gimp_drawable_merge_shadow (drawable->drawable_id, TRUE);
  gimp_drawable_update (drawable->drawable_id,
                        sel_x1, sel_y1, sel_width, sel_height);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static gint
cm_dialog (void)
{
  GtkWidget *dialog;
  GtkWidget *vbox;
  GtkWidget *frame;
  GtkWidget *hbox;
  GtkWidget *button;
  GtkWidget *label;
  GtkWidget *xframe;
  GtkWidget *table;
  gdouble    red_value, green_value, blue_value;
  gboolean   run;

  gimp_ui_init ("channel_mixer", FALSE);

  /* get values */
  if (mix.monochrome_flag == TRUE)
    {
      red_value   = mix.black.red_gain   * 100;
      green_value = mix.black.green_gain * 100;
      blue_value  = mix.black.blue_gain  * 100;
    }
  else
    {
      switch (mix.output_channel)
        {
        case CM_RED_CHANNEL:
          red_value   = mix.red.red_gain   * 100;
          green_value = mix.red.green_gain * 100;
          blue_value  = mix.red.blue_gain  * 100;
          break;

        case CM_GREEN_CHANNEL:
          red_value   = mix.green.red_gain   * 100;
          green_value = mix.green.green_gain * 100;
          blue_value  = mix.green.blue_gain  * 100;
          break;

        case CM_BLUE_CHANNEL:
          red_value   = mix.blue.red_gain   * 100;
          green_value = mix.blue.green_gain * 100;
          blue_value  = mix.blue.blue_gain  * 100;
          break;

        default:
          g_assert_not_reached ();

          red_value = green_value = blue_value = 0.0;
          break;
        }
    }

  dialog = gimp_dialog_new (_("Channel Mixer"), "mixer",
                            NULL, 0,
                            gimp_standard_help_func, HELP_ID,

                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                            GTK_STOCK_OK,     GTK_RESPONSE_OK,

                            NULL);

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
  gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dialog)->vbox), hbox);
  gtk_widget_show (hbox);

  /*........................................................... */
  /* preview */
  vbox = gtk_vbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  frame = gtk_frame_new (_("Preview"));
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  xframe = gtk_frame_new (NULL);
  gtk_container_set_border_width (GTK_CONTAINER (xframe), 4);
  gtk_frame_set_shadow_type (GTK_FRAME (xframe), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (frame), xframe);
  gtk_widget_show (xframe);

  mix.preview = gtk_preview_new (GTK_PREVIEW_COLOR);
  gtk_preview_size (GTK_PREVIEW (mix.preview), preview->width, preview->height);
  gtk_container_add (GTK_CONTAINER (xframe), mix.preview);
  gtk_widget_show (mix.preview);

  /*  The preview toggle  */
  mix.preview_toggle = gtk_check_button_new_with_mnemonic (_("_Preview"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mix.preview_toggle),
                                mix.preview_flag);
  gtk_box_pack_start (GTK_BOX (vbox), mix.preview_toggle, FALSE, FALSE, 0);
  gtk_widget_show (mix.preview_toggle);

  g_signal_connect (mix.preview_toggle, "toggled",
                    G_CALLBACK (cm_preview_callback),
                    &mix);

  /*........................................................... */
  /* controls */
  vbox = gtk_vbox_new (FALSE, 4);
  gtk_box_pack_start (GTK_BOX (hbox), vbox, TRUE, TRUE, 0);
  gtk_widget_show (vbox);

  /*........................................................... */
  frame = gtk_frame_new (NULL);
  gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  hbox = gtk_hbox_new (FALSE, 4);
  gtk_frame_set_label_widget (GTK_FRAME (frame), hbox);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("O_utput Channel:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  mix.combo = gimp_int_combo_box_new (_("Red"),   CM_RED_CHANNEL,
                                      _("Green"), CM_GREEN_CHANNEL,
                                      _("Blue"),  CM_BLUE_CHANNEL,
                                      NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (mix.combo),
                                 mix.output_channel);

  g_signal_connect (mix.combo, "changed",
                    G_CALLBACK (cm_combo_callback),
                    &mix);

  gtk_box_pack_start (GTK_BOX (hbox), mix.combo, FALSE, FALSE, 0);
  gtk_widget_show (mix.combo);

  if (mix.monochrome_flag)
    gtk_widget_set_sensitive (mix.combo, FALSE);

  gtk_label_set_mnemonic_widget (GTK_LABEL (label), mix.combo);

  /*........................................................... */

  table = gtk_table_new (3, 3, FALSE);
  gtk_container_set_border_width (GTK_CONTAINER (table), 4);
  gtk_table_set_row_spacings (GTK_TABLE (table), 2);
  gtk_table_set_col_spacings (GTK_TABLE (table), 4);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);

  mix.red_data =
    GTK_ADJUSTMENT (gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                                          _("_Red:"), 150, -1,
                                          red_value, -200.0, 200.0,
                                          1.0, 10.0, 1,
                                          TRUE, 0.0, 0.0,
                                          NULL, NULL));

  g_signal_connect (mix.red_data, "value_changed",
                    G_CALLBACK (cm_red_scale_callback),
                    &mix);

  mix.green_data =
    GTK_ADJUSTMENT (gimp_scale_entry_new (GTK_TABLE (table), 0, 1,
                                          _("_Green:"), 150, -1,
                                          green_value, -200.0, 200.0,
                                          1.0, 10.0, 1,
                                          TRUE, 0.0, 0.0,
                                          NULL, NULL));

  g_signal_connect (mix.green_data, "value_changed",
                    G_CALLBACK (cm_green_scale_callback),
                    &mix);


  mix.blue_data =
    GTK_ADJUSTMENT (gimp_scale_entry_new (GTK_TABLE (table), 0, 2,
                                          _("_Blue:"), 150, -1,
                                          blue_value, -200.0, 200.0,
                                          1.0, 10.0, 1,
                                          TRUE, 0.0, 0.0,
                                          NULL, NULL));

  g_signal_connect (mix.blue_data, "value_changed",
                    G_CALLBACK (cm_blue_scale_callback),
                    &mix);

  /*  The monochrome toggle  */
  mix.monochrome_toggle = gtk_check_button_new_with_mnemonic (_("_Monochrome"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mix.monochrome_toggle),
                                mix.monochrome_flag);
  gtk_box_pack_start (GTK_BOX (vbox), mix.monochrome_toggle, FALSE, FALSE, 0);
  gtk_widget_show (mix.monochrome_toggle);

  g_signal_connect (mix.monochrome_toggle, "toggled",
                    G_CALLBACK (cm_monochrome_callback),
                    &mix);

  /*  The preserve luminosity toggle  */
  mix.preserve_luminosity_toggle =
    gtk_check_button_new_with_mnemonic (_("Preserve _Luminosity"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON
                                (mix.preserve_luminosity_toggle),
                                mix.preserve_luminosity_flag);
  gtk_box_pack_start (GTK_BOX (vbox), mix.preserve_luminosity_toggle,
                      FALSE, FALSE, 0);
  gtk_widget_show (mix.preserve_luminosity_toggle);

  g_signal_connect (mix.preserve_luminosity_toggle, "toggled",
                    G_CALLBACK (cm_preserve_luminosity_callback),
                    &mix);

  /*........................................................... */
  /*  Horizontal box for file i/o  */
  hbox = gtk_hbox_new (FALSE, 4);
  gtk_box_pack_end (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  button = gtk_button_new_from_stock (GTK_STOCK_OPEN);
  gtk_container_add (GTK_CONTAINER (hbox), button);
  gtk_widget_show (button);

  g_signal_connect (button, "clicked",
                    G_CALLBACK (cm_load_file_callback),
                    &mix);

  button = gtk_button_new_from_stock (GTK_STOCK_SAVE);
  gtk_container_add (GTK_CONTAINER (hbox), button);
  gtk_widget_show (button);

  g_signal_connect (button, "clicked",
                    G_CALLBACK (cm_save_file_callback),
                    &mix);

  /*........................................................... */

  if (mix.preview_flag)
    cm_preview (&mix);

  gtk_widget_show (dialog);

  run = (gimp_dialog_run (GIMP_DIALOG (dialog)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dialog);

  return run;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_red_scale_callback (GtkAdjustment *adjustment,
                       CmParamsType  *mix)
{
  if (mix->monochrome_flag == TRUE)
    mix->black.red_gain = adjustment->value / 100.0;
  else
    switch (mix->output_channel)
      {
      case CM_RED_CHANNEL:
        mix->red.red_gain = adjustment->value / 100.0;
        break;
      case CM_GREEN_CHANNEL:
        mix->green.red_gain = adjustment->value / 100.0;
        break;
      case CM_BLUE_CHANNEL:
        mix->blue.red_gain = adjustment->value / 100.0;
        break;
      }

  if (mix->preview_flag)
    cm_preview (mix);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_green_scale_callback (GtkAdjustment *adjustment,
                         CmParamsType  *mix)
{
  if (mix->monochrome_flag == TRUE)
    mix->black.green_gain = adjustment->value / 100.0;
  else
    switch (mix->output_channel)
      {
      case CM_RED_CHANNEL:
        mix->red.green_gain = adjustment->value / 100.0;
        break;
      case CM_GREEN_CHANNEL:
        mix->green.green_gain = adjustment->value / 100.0;
        break;
      case CM_BLUE_CHANNEL:
        mix->blue.green_gain = adjustment->value / 100.0;
        break;
      }

  if (mix->preview_flag)
    cm_preview (mix);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_blue_scale_callback (GtkAdjustment *adjustment,
                        CmParamsType  *mix)
{
  if (mix->monochrome_flag == TRUE)
    mix->black.blue_gain = adjustment->value / 100.0;
  else
    switch (mix->output_channel)
      {
      case CM_RED_CHANNEL:
        mix->red.blue_gain = adjustment->value / 100.0;
        break;
      case CM_GREEN_CHANNEL:
        mix->green.blue_gain = adjustment->value / 100.0;
        break;
      case CM_BLUE_CHANNEL:
        mix->blue.blue_gain = adjustment->value / 100.0;
        break;
      }

  if (mix->preview_flag)
    cm_preview (mix);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_preview (CmParamsType *mix)
{
  guchar  *dst, *src, *srcp, *dstp;
  gint     x, y;
  gint     offset, rowsize;
  gdouble  red_norm, green_norm, blue_norm, black_norm;

  red_norm   = cm_calculate_norm (mix, &mix->red);
  green_norm = cm_calculate_norm (mix, &mix->green);
  blue_norm  = cm_calculate_norm (mix, &mix->blue);
  black_norm = cm_calculate_norm (mix, &mix->black);

  rowsize = preview->width * preview->bpp;
  src = preview->bits;
  dst = g_malloc (rowsize);

  for (y = 0; y < preview->height; y++)
    {
      srcp = src + y * rowsize;
      offset = 0;

      for (x = 0; x < preview->width; x++)
        {
          guchar r, g, b;

          dstp = dst;
          r = *(srcp + offset);
          g = *(srcp + offset + 1);
          b = *(srcp + offset + 2);

          if (mix->monochrome_flag == TRUE)
            {
              *(dstp + offset) =
                *(dstp + offset + 1) =
                *(dstp + offset + 2) =
                cm_mix_pixel (&mix->black, r, g, b, black_norm);
            }
          else
            {
              *(dstp + offset) =
                cm_mix_pixel (&mix->red, r, g, b, red_norm);
              *(dstp + offset + 1) =
                cm_mix_pixel (&mix->green, r, g, b, green_norm);
              *(dstp + offset + 2) =
                cm_mix_pixel (&mix->blue, r, g, b, blue_norm);
            }

          offset += preview->bpp;
        }

      gtk_preview_draw_row (GTK_PREVIEW (mix->preview),
                            dst, 0, y, preview->width);
    }

  gtk_widget_queue_draw (mix->preview);
  g_free (dst);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static mwPreview *
mw_preview_build_virgin (GimpDrawable *drawable)
{
  mwPreview *mwp;

  mwp = g_new (mwPreview, 1);

  if (sel_width > sel_height)
    {
      mwp->width = MIN (sel_width, PREVIEW_SIZE);
      mwp->scale = (gdouble) sel_width / (gdouble) mwp->width;
      mwp->height = sel_height / mwp->scale;
    }
  else
    {
      mwp->height = MIN (sel_height, PREVIEW_SIZE);
      mwp->scale = (gdouble) sel_height / (gdouble) mwp->height;
      mwp->width = sel_width / mwp->scale;
    }

  mwp->bpp = 3;
  mwp->bits = NULL;

  return mwp;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static mwPreview *
mw_preview_build (GimpDrawable *drawable)
{
  mwPreview    *mwp;
  gint          x, y, b;
  guchar       *bc;
  guchar       *drwBits;
  GimpPixelRgn  pr;

  mwp = mw_preview_build_virgin (drawable);

  gimp_pixel_rgn_init (&pr, drawable, sel_x1, sel_y1, sel_width, sel_height,
                       FALSE, FALSE);

  drwBits = g_new (guchar, sel_width * drawable->bpp);

  bc = mwp->bits = g_new (guchar, mwp->width * mwp->height * mwp->bpp);

  for (y = 0; y < mwp->height; y++)
    {
      gimp_pixel_rgn_get_row (&pr, drwBits,
                              sel_x1,
                              sel_y1 + (y * sel_height) / mwp->height,
                              sel_width);

      for (x = 0; x < mwp->width; x++)
        {
          for (b = 0; b < 3; b++)
            {
              *bc++ = *(drwBits +
                        ((gint) (x * mwp->scale) * drawable->bpp) +
                        b % drawable->bpp);
            }
        }
    }

  g_free (drwBits);

  return mwp;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_preview_callback (GtkWidget    *widget,
                     CmParamsType *mix)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    {
      mix->preview_flag = TRUE;
      cm_preview (mix);
    }
  else
    mix->preview_flag = FALSE;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_monochrome_callback (GtkWidget    *widget,
                        CmParamsType *mix)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    {
      mix->old_output_channel = mix->output_channel;
      mix->monochrome_flag = TRUE;
      gtk_widget_set_sensitive (mix->combo, FALSE);
    }
  else
    {
      mix->output_channel = mix->old_output_channel;
      mix->monochrome_flag = FALSE;
      gtk_widget_set_sensitive (mix->combo, TRUE);
    }

  cm_set_adjusters (mix);

  if (mix->preview_flag)
    cm_preview (mix);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_preserve_luminosity_callback (GtkWidget    *widget,
                                 CmParamsType *mix)
{
  if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
    mix->preserve_luminosity_flag = TRUE;
  else
    mix->preserve_luminosity_flag = FALSE;

  if (mix->preview_flag)
    cm_preview (mix);
}

static gchar *
cm_settings_filename (CmParamsType *mix)
{
  gchar *filename;

  if (! mix->filename)
    mix->filename = g_strdup ("settings");

  if (! g_path_is_absolute (mix->filename))
    {
      gchar *basedir;

      basedir = g_build_filename (gimp_directory (), "channel-mixer", NULL);

      if (! g_file_test (basedir, G_FILE_TEST_IS_DIR))
        mkdir (basedir, 0775);

      filename = g_build_filename (basedir, mix->filename, NULL);

      g_free (basedir);
    }
  else
    {
      filename = g_strdup (mix->filename);
    }

  return filename;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_load_file_callback (GtkWidget    *widget,
                       CmParamsType *mix)
{
  static GtkWidget *filesel = NULL;
  gchar            *fname;

  if (!filesel)
    {
      filesel = gtk_file_selection_new (_("Load Channel Mixer Settings"));

      gtk_window_set_transient_for (GTK_WINDOW (filesel),
                                    GTK_WINDOW (gtk_widget_get_toplevel (widget)));

      gimp_help_connect (filesel, gimp_standard_help_func, HELP_ID, NULL);

      g_signal_connect (filesel, "response",
                        G_CALLBACK (cm_load_file_response_callback),
                        mix);
      g_signal_connect (filesel, "delete_event",
                        G_CALLBACK (gtk_true),
                        NULL);
    }

  fname = cm_settings_filename (mix);
  gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), fname);
  g_free (fname);

  gtk_window_present (GTK_WINDOW (filesel));
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_load_file_response_callback (GtkFileSelection *fs,
                                gint              response_id,
                                CmParamsType     *mix)
{
  FILE *fp;

  if (response_id == GTK_RESPONSE_OK)
    {
      g_free (mix->filename);

      mix->filename = g_strdup (gtk_file_selection_get_filename (fs));

      fp = fopen (mix->filename, "r");

      if (fp)
        {
          gchar buf[3][CM_LINE_SIZE];

          buf[0][0] = '\0';
          buf[1][0] = '\0';
          buf[2][0] = '\0';

          fgets (buf[0], CM_LINE_SIZE - 1, fp);

          fscanf (fp, "%*s %s", buf[0]);
          if (strcmp (buf[0], "RED") == 0)
            mix->output_channel = CM_RED_CHANNEL;
          else if (strcmp (buf[0], "GREEN") == 0)
            mix->output_channel = CM_GREEN_CHANNEL;
          else if (strcmp (buf[0], "BLUE") == 0)
            mix->output_channel = CM_BLUE_CHANNEL;

          fscanf (fp, "%*s %s", buf[0]);
          if (strcmp (buf[0], "TRUE") == 0)
            mix->preview_flag = TRUE;
          else
            mix->preview_flag = FALSE;

          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mix->preview_toggle),
                                        mix->preview_flag);

          fscanf (fp, "%*s %s", buf[0]);
          if (strcmp (buf[0], "TRUE") == 0)
            mix->monochrome_flag = TRUE;
          else
            mix->monochrome_flag = FALSE;

          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mix->monochrome_toggle),
                                        mix->monochrome_flag);

          fscanf (fp, "%*s %s", buf[0]);
          if (strcmp (buf[0], "TRUE") == 0)
            mix->preserve_luminosity_flag = TRUE;
          else
            mix->preserve_luminosity_flag = FALSE;

          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (mix->preserve_luminosity_toggle),
                                        mix->preserve_luminosity_flag);

          fscanf (fp, "%*s %s %s %s", buf[0], buf[1], buf[2]);
          mix->red.red_gain   = g_ascii_strtod (buf[0], NULL);
          mix->red.green_gain = g_ascii_strtod (buf[1], NULL);
          mix->red.blue_gain  = g_ascii_strtod (buf[2], NULL);

          fscanf (fp, "%*s %s %s %s", buf[0], buf[1], buf[2]);
          mix->green.red_gain   = g_ascii_strtod (buf[0], NULL);
          mix->green.green_gain = g_ascii_strtod (buf[1], NULL);
          mix->green.blue_gain  = g_ascii_strtod (buf[2], NULL);

          fscanf (fp, "%*s %s %s %s", buf[0], buf[1], buf[2]);
          mix->blue.red_gain   = g_ascii_strtod (buf[0], NULL);
          mix->blue.green_gain = g_ascii_strtod (buf[1], NULL);
          mix->blue.blue_gain  = g_ascii_strtod (buf[2], NULL);

          fscanf (fp, "%*s %s %s %s", buf[0], buf[1], buf[2]);
          mix->black.red_gain   = g_ascii_strtod (buf[0], NULL);
          mix->black.green_gain = g_ascii_strtod (buf[1], NULL);
          mix->black.blue_gain  = g_ascii_strtod (buf[2], NULL);

          fclose (fp);

          gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (mix->combo),
                                         mix->output_channel);
          cm_set_adjusters (mix);

          if (mix->preview_flag)
            cm_preview (mix);
        }
      else
        {
          g_message (_("Could not open '%s' for reading: %s"),
                     gimp_filename_to_utf8 (mix->filename),
                     g_strerror (errno));
        }
    }

  gtk_widget_hide (GTK_WIDGET (fs));
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_save_file_callback (GtkWidget    *widget,
                       CmParamsType *mix)
{
  static GtkWidget *filesel = NULL;
  gchar            *fname;

  if (!filesel)
    {
      filesel = gtk_file_selection_new (_("Save Channel Mixer Settings"));

      gtk_window_set_transient_for (GTK_WINDOW (filesel),
                                    GTK_WINDOW (gtk_widget_get_toplevel (widget)));

      gimp_help_connect (filesel, gimp_standard_help_func, HELP_ID, NULL);

      g_signal_connect (filesel, "response",
                        G_CALLBACK (cm_save_file_response_callback),
                        mix);
      g_signal_connect (filesel, "delete_event",
                        G_CALLBACK (gtk_true),
                        NULL);
    }

  fname = cm_settings_filename (mix);
  gtk_file_selection_set_filename (GTK_FILE_SELECTION (filesel), fname);
  g_free (fname);

  gtk_window_present (GTK_WINDOW (filesel));
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_save_file_response_callback (GtkFileSelection *fs,
                                gint              response_id,
                                CmParamsType     *mix)
{
  const gchar *filename;
  FILE        *file = NULL;

  if (response_id != GTK_RESPONSE_OK)
    {
      gtk_widget_hide (GTK_WIDGET (fs));
      return;
    }

  filename = gtk_file_selection_get_filename (fs);
  if (! filename)
    return;

  if (g_file_test (filename, G_FILE_TEST_EXISTS))
    {
      if (g_file_test (filename, G_FILE_TEST_IS_DIR))
        {
          gchar *path = g_build_filename (filename, G_DIR_SEPARATOR_S, NULL);

          gtk_file_selection_set_filename (fs, path);

          g_free (path);

          return;
        }

      if (! cm_force_overwrite (filename, GTK_WIDGET (fs)))
        return;
    }

  file = fopen (filename, "w");

  if (! file)
    {
      g_message (_("Could not open '%s' for writing: %s"),
                 gimp_filename_to_utf8 (filename), g_strerror (errno));
      return;
    }

  g_free (mix->filename);
  mix->filename = g_strdup (filename);

  cm_save_file (mix, file);

  g_message (_("Parameters were Saved to '%s'"),
             gimp_filename_to_utf8 (filename));

  gtk_widget_hide (GTK_WIDGET (fs));
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static gboolean
cm_force_overwrite (const gchar *filename,
                    GtkWidget   *parent)
{
  GtkWidget *dlg;
  GtkWidget *label;
  GtkWidget *hbox;
  gchar     *buffer;
  gboolean   overwrite;

  dlg = gimp_dialog_new (_("Channel Mixer File Operation Warning"),
                         "channel_mixer",
                         parent, GTK_DIALOG_MODAL,
                         gimp_standard_help_func, HELP_ID,

                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                         GTK_STOCK_OK,     GTK_RESPONSE_OK,

                         NULL);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), hbox, FALSE, FALSE, 6);
  gtk_widget_show (hbox);

  buffer = g_strdup_printf (_("File '%s' exists.\n"
                              "Overwrite it?"), filename);
  label = gtk_label_new (buffer);
  g_free (buffer);

  gtk_label_set_justify (GTK_LABEL (label), GTK_JUSTIFY_LEFT);
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 6);
  gtk_widget_show (label);

  gtk_widget_show (dlg);

  overwrite = (gimp_dialog_run (GIMP_DIALOG (dlg)) == GTK_RESPONSE_OK);

  gtk_widget_destroy (dlg);

  return overwrite;
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_save_file (CmParamsType *mix,
              FILE         *fp)
{
  const gchar *str = NULL;
  gchar        buf[3][G_ASCII_DTOSTR_BUF_SIZE];

  switch (mix->output_channel)
    {
    case CM_RED_CHANNEL:
      str = "RED";
      break;
    case CM_GREEN_CHANNEL:
      str = "GREEN";
      break;
    case CM_BLUE_CHANNEL:
      str = "BLUE";
      break;
    default:
      g_assert_not_reached ();
      break;
    }

  fprintf (fp, "# Channel Mixer Configuration File\n");

  fprintf (fp, "CHANNEL: %s\n", str);
  fprintf (fp, "PREVIEW: %s\n",
           mix->preview_flag ? "TRUE" : "FALSE");
  fprintf (fp, "MONOCHROME: %s\n",
           mix->monochrome_flag ? "TRUE" : "FALSE");
  fprintf (fp, "PRESERVE_LUMINOSITY: %s\n",
           mix->preserve_luminosity_flag ? "TRUE" : "FALSE");

  fprintf (fp, "RED: %s %s %s\n",
           g_ascii_formatd (buf[0], sizeof (buf[0]), "%5.3f",
                            mix->red.red_gain),
           g_ascii_formatd (buf[1], sizeof (buf[1]), "%5.3f",
                            mix->red.green_gain),
           g_ascii_formatd (buf[2], sizeof (buf[2]), "%5.3f",
                            mix->red.blue_gain));

  fprintf (fp, "GREEN: %s %s %s\n",
           g_ascii_formatd (buf[0], sizeof (buf[0]), "%5.3f",
                            mix->green.red_gain),
           g_ascii_formatd (buf[1], sizeof (buf[1]), "%5.3f",
                            mix->green.green_gain),
           g_ascii_formatd (buf[2], sizeof (buf[2]), "%5.3f",
                            mix->green.blue_gain));

  fprintf (fp, "BLUE: %s %s %s\n",
           g_ascii_formatd (buf[0], sizeof (buf[0]), "%5.3f",
                            mix->blue.red_gain),
           g_ascii_formatd (buf[1], sizeof (buf[1]), "%5.3f",
                            mix->blue.green_gain),
           g_ascii_formatd (buf[2], sizeof (buf[2]), "%5.3f",
                            mix->blue.blue_gain));

  fprintf (fp, "BLACK: %s %s %s\n",
           g_ascii_formatd (buf[0], sizeof (buf[0]), "%5.3f",
                            mix->black.red_gain),
           g_ascii_formatd (buf[1], sizeof (buf[1]), "%5.3f",
                            mix->black.green_gain),
           g_ascii_formatd (buf[2], sizeof (buf[2]), "%5.3f",
                            mix->black.blue_gain));

  fclose (fp);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_combo_callback (GtkWidget    *widget,
                   CmParamsType *mix)
{
  gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (widget),
                                 (gint *) &mix->output_channel);

  cm_set_adjusters (mix);
}

/*----------------------------------------------------------------------
 *
 *--------------------------------------------------------------------*/
static void
cm_set_adjusters (CmParamsType *mix)
{
  if (mix->monochrome_flag == TRUE)
    {
      gtk_adjustment_set_value (mix->red_data,   mix->black.red_gain   * 100.0);
      gtk_adjustment_set_value (mix->green_data, mix->black.green_gain * 100.0);
      gtk_adjustment_set_value (mix->blue_data,  mix->black.blue_gain  * 100.0);

      return;
    }

  switch (mix->output_channel)
    {
    case CM_RED_CHANNEL:
      gtk_adjustment_set_value (mix->red_data,   mix->red.red_gain   * 100.0);
      gtk_adjustment_set_value (mix->green_data, mix->red.green_gain * 100.0);
      gtk_adjustment_set_value (mix->blue_data,  mix->red.blue_gain  * 100.0);
      break;

    case CM_GREEN_CHANNEL:
      gtk_adjustment_set_value (mix->red_data,   mix->green.red_gain   * 100.0);
      gtk_adjustment_set_value (mix->green_data, mix->green.green_gain * 100.0);
      gtk_adjustment_set_value (mix->blue_data,  mix->green.blue_gain  * 100.0);
      break;

    case CM_BLUE_CHANNEL:
      gtk_adjustment_set_value (mix->red_data,   mix->blue.red_gain   * 100.0);
      gtk_adjustment_set_value (mix->green_data, mix->blue.green_gain * 100.0);
      gtk_adjustment_set_value (mix->blue_data,  mix->blue.blue_gain  * 100.0);
      break;
    }
}
