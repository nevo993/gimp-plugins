/*
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This is a plug-in for the GIMP.
 *
 * Generates images containing vector type drawings.
 *
 * Copyright (C) 1997 Andy Thomas  alt@picnic.demon.co.uk
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
#include <errno.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <string.h>

#ifdef __GNUC__
#warning GTK_DISABLE_DEPRECATED
#endif
#undef GTK_DISABLE_DEPRECATED

#include <gtk/gtk.h>

#ifdef G_OS_WIN32
#  include <io.h>
#  ifndef W_OK
#    define W_OK 2
#  endif
#endif

#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf/gdk-pixdata.h>
#include "libgimp/stdplugins-intl.h"

#include "gfig.h"
#include "gfig-style.h"
#include "gfig-dialog.h"
#include "gfig-arc.h"
#include "gfig-bezier.h"
#include "gfig-circle.h"
#include "gfig-dobject.h"
#include "gfig-ellipse.h"
#include "gfig-grid.h"
#include "gfig-line.h"
#include "gfig-poly.h"
#include "gfig-preview.h"
#include "gfig-spiral.h"
#include "gfig-star.h"
#include "gfig-stock.h"

#include "pix-data.h"

#define PREVIEW_SIZE     400

#define SCALE_WIDTH      120

#define BRUSH_PREVIEW_SZ 32
#define SEL_BUTTON_WIDTH 100
#define SEL_BUTTON_HEIGHT 20

#define PREVIEW_MASK  (GDK_EXPOSURE_MASK       | \
                       GDK_POINTER_MOTION_MASK | \
                       GDK_BUTTON_PRESS_MASK   | \
                       GDK_BUTTON_RELEASE_MASK | \
                       GDK_BUTTON_MOTION_MASK  | \
                       GDK_KEY_PRESS_MASK      | \
                       GDK_KEY_RELEASE_MASK)

#define GRID_TYPE_MENU   1
#define GRID_RENDER_MENU 2
#define GRID_IGNORE      0
#define GRID_HIGHTLIGHT  1
#define GRID_RESTORE     2

#define PAINT_LAYERS_MENU 1
#define PAINT_BGS_MENU    2
#define PAINT_TYPE_MENU   3

#define SELECT_TYPE_MENU      1
#define SELECT_ARCTYPE_MENU   2
#define SELECT_TYPE_MENU_FILL 3
#define SELECT_TYPE_MENU_WHEN 4

#define OBJ_SELECT_GT 1
#define OBJ_SELECT_LT 2
#define OBJ_SELECT_EQ 4

/* Values when first invoked */
SelectItVals selvals =
{
  {
    MIN_GRID + (MAX_GRID - MIN_GRID)/2, /* Gridspacing     */
    RECT_GRID,            /* Default to rectangle type     */
    FALSE,                /* drawgrid                      */
    FALSE,                /* snap2grid                     */
    FALSE,                /* lockongrid                    */
    TRUE                  /* show control points           */
  },
  FALSE,                  /* show image                    */
  MIN_UNDO + (MAX_UNDO - MIN_UNDO)/2,  /* Max level of undos */
  TRUE,                   /* Show pos updates              */
  0.0,                    /* Brush fade                    */
  0.0,                    /* Brush gradient                */
  20.0,                   /* Air bursh pressure            */
  ORIGINAL_LAYER,         /* Draw all objects on one layer */
  LAYER_TRANS_BG,         /* New layers background         */
  PAINT_BRUSH_TYPE,       /* Default to use brushes        */
  FALSE,                  /* reverse lines                 */
  TRUE,                   /* Scale to image when painting  */
  1.0,                    /* Scale to image fp             */
  FALSE,                  /* Approx circles by drawing lines */
  BRUSH_BRUSH_TYPE,       /* Default to use a brush        */
  LINE                    /* Initial object type           */
};

selection_option selopt =
{
  ADD,          /* type */
  FALSE,        /* Antia */
  FALSE,        /* Feather */
  10.0,         /* feather radius */
  ARC_SEGMENT,  /* Arc as a segment */
  FILL_PATTERN, /* Fill as pattern */
  FILL_EACH,    /* Fill after each selection */
  100.0,        /* Max opacity */
};

/* Must keep in step with the above */
typedef struct
{
  void      *gridspacing;
  GtkWidget *gridtypemenu;
  GtkWidget *drawgrid;
  GtkWidget *snap2grid;
  GtkWidget *lockongrid;
  GtkWidget *showcontrol;
} GfigOptWidgets;

static GfigOptWidgets gfig_opt_widget;
static gchar         *gfig_path       = NULL;
static GtkWidget     *page_menu_bg;


static void      gfig_response             (GtkWidget *widget,
                                            gint       response_id,
                                            gpointer   data);
static void      load_button_callback      (GtkWidget *widget,
                                            gpointer   data);
static void      merge_button_callback     (GtkWidget *widget,
                                            gpointer   data);
static void      gfig_new_gc               (void);
static void      gfig_save_menu_callback   (GtkWidget *widget,
                                            gpointer   data);
static void      gfig_list_load_all        (const gchar *path);
static void      gfig_save                 (GtkWidget *parent);
static void      gfig_list_free_all        (void);
static void      create_save_file_chooser  (GFigObj   *obj,
                                            gchar     *tpath,
                                            GtkWidget *parent);
static GtkWidget *draw_buttons             (GtkWidget *ww);
static void      select_combo_callback     (GtkWidget *widget,
                                            gpointer   data);
static void      adjust_grid_callback      (GtkWidget *widget,
                                            gpointer   data);
static void      options_dialog_callback   (GtkWidget *widget,
                                            gpointer   data);
static gint      gfig_scale_x              (gint       x);
static gint      gfig_scale_y              (gint       y);
static void      toggle_show_image         (void);
static void      gridtype_combo_callback   (GtkWidget *widget,
                                            gpointer data);

static void      gfig_load_file_chooser_response (GtkFileChooser *chooser,
                                                  gint            response_id,
                                                  gpointer        data);
static void      file_chooser_response     (GtkFileChooser *chooser,
                                            gint            response_id,
                                            GFigObj        *obj);
static GtkWidget *but_with_pix             (const gchar  *stock_id,
                                            GSList      **group,
                                            gint          baction);
static gint     bezier_button_press        (GtkWidget      *widget,
                                            GdkEventButton *event,
                                            gpointer        data);
static GtkWidget *obj_select_buttons       (void);
static void     paint_combo_callback       (GtkWidget *widget,
                                            gpointer   data);


gint
gfig_dialog (void)
{
  GtkWidget *main_hbox;
  GtkWidget *vbox;
  GFigObj   *gfig;
  GimpParasite *parasite;
  gint          newlayer;
  GtkWidget *menubar;
  GtkWidget *menuitem;
  GtkWidget *menu;
  GtkWidget *combo;
  GtkWidget *frame;
  gint       k;
  gint       img_width;
  gint       img_height;

  gimp_ui_init ("gfig", TRUE);

  img_width  = gimp_drawable_width (gfig_context->drawable_id);
  img_height = gimp_drawable_height (gfig_context->drawable_id);

  /*
   * See if there is a "gfig" parasite.  If so, this is a gfig layer,
   * and we start by clearing it to transparent.
   * If not, we create a new transparent layer.
   */
  gfig_list = NULL;
  undo_water_mark = -1;
  parasite = gimp_drawable_parasite_find (gfig_context->drawable_id, "gfig");
  for (k=0; k < 1000; k++)
    gfig_context->style[k] = NULL;
  gfig_context->num_styles = 0;
  gfig_context->enable_repaint = FALSE;

  /* debug */
  gfig_context->debug_styles = FALSE;

  /* initial gimp and default styles */
  gfig_read_gimp_style (&gfig_context->gimp_style, "Gimp");
  gfig_context->current_style = &gfig_context->default_style;
  gfig_style_set_all_sources (&gfig_context->gimp_style, STYLE_SOURCE_GIMP);
  gfig_style_append (&gfig_context->gimp_style);
  gfig_read_gimp_style (&gfig_context->default_style, "Base");
  gfig_style_set_all_sources (&gfig_context->default_style, STYLE_SOURCE_DEFAULT);
  gfig_style_append (&gfig_context->default_style);

  if (parasite)
    {
      gimp_drawable_fill (gfig_context->drawable_id, GIMP_TRANSPARENT_FILL);
      gimp_parasite_free (parasite);
    }
  else
    {
      newlayer = gimp_layer_new (gfig_context->image_id, "GFig", img_width, img_height,
                                 GIMP_RGBA_IMAGE, 100., GIMP_NORMAL_MODE);
      gimp_drawable_fill (newlayer, GIMP_TRANSPARENT_FILL);
      gimp_image_add_layer (gfig_context->image_id, newlayer, -1);
      gfig_context->drawable_id = newlayer;
    }


  gfig_drawable = gimp_drawable_get (gfig_context->drawable_id);

  gfig_stock_init ();

  gfig_path = gimp_gimprc_query ("gfig-path");

  if (! gfig_path)
    {
      gchar *gimprc = gimp_personal_rc_file ("gimprc");
      gchar *full_path;
      gchar *esc_path;

      full_path =
        g_strconcat ("${gimp_dir}", G_DIR_SEPARATOR_S, "gfig",
                     G_SEARCHPATH_SEPARATOR_S,
                     "${gimp_data_dir}", G_DIR_SEPARATOR_S, "gfig",
                     NULL);
      esc_path = g_strescape (full_path, NULL);
      g_free (full_path);

      g_message (_("No %s in gimprc:\n"
                   "You need to add an entry like\n"
                   "(%s \"%s\")\n"
                   "to your %s file."),
                 "gfig-path", "gfig-path", esc_path,
                 gimp_filename_to_utf8 (gimprc));

      g_free (gimprc);
      g_free (esc_path);
    }

  /* Start building the dialog up */
  top_level_dlg = gimp_dialog_new (_("Gfig"), "gfig",
                                   NULL, 0,
                                   gimp_standard_help_func, HELP_ID,

                                   GTK_STOCK_UNDO,   RESPONSE_UNDO,
                                   GTK_STOCK_CLEAR,  RESPONSE_CLEAR,
                                   GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                   GTK_STOCK_CLOSE,  GTK_RESPONSE_OK,
                                   _("Paint"),       RESPONSE_PAINT,

                                   NULL);

  g_signal_connect (top_level_dlg, "response",
                    G_CALLBACK (gfig_response),
                    top_level_dlg);
  g_signal_connect (top_level_dlg, "destroy",
                    G_CALLBACK (gtk_main_quit),
                    NULL);

  gtk_dialog_set_response_sensitive (GTK_DIALOG (top_level_dlg),
                                     RESPONSE_UNDO, undo_water_mark >= 0);

  /* build the menu */
  menubar = gtk_menu_bar_new ();
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (top_level_dlg)->vbox),
                      menubar, FALSE, FALSE, 0);
  gtk_widget_show (menubar);

  /* File menu */
  menuitem = gtk_menu_item_new_with_mnemonic ("_File");
  gtk_menu_shell_append (GTK_MENU_SHELL (menubar), menuitem);
  gtk_widget_show (menuitem);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), menu);
  gtk_widget_show (menu);

  menuitem = gtk_menu_item_new_with_mnemonic ("_Open");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (G_OBJECT (menuitem), "activate",
                    G_CALLBACK (load_button_callback),
                    NULL);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_mnemonic ("_Import");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (G_OBJECT (menuitem), "activate",
                    G_CALLBACK (merge_button_callback),
                    NULL);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_mnemonic ("_Save");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (G_OBJECT (menuitem), "activate",
                    G_CALLBACK (gfig_save_menu_callback),
                    NULL);
  gtk_widget_show (menuitem);

  /* Grid menu */
  menuitem = gtk_menu_item_new_with_mnemonic ("_Edit");
  gtk_menu_shell_append (GTK_MENU_SHELL (menubar), menuitem);
  gtk_widget_show (menuitem);

  menu = gtk_menu_new ();
  gtk_menu_item_set_submenu (GTK_MENU_ITEM (menuitem), menu);
  gtk_widget_show (menu);

  menuitem = gtk_menu_item_new_with_mnemonic ("_Grid");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (G_OBJECT (menuitem), "activate",
                    G_CALLBACK (adjust_grid_callback),
                    NULL);
  gtk_widget_show (menuitem);

  menuitem = gtk_menu_item_new_with_mnemonic ("_Options");
  gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
  g_signal_connect (G_OBJECT (menuitem), "activate",
                    G_CALLBACK (options_dialog_callback),
                    NULL);
  gtk_widget_show (menuitem);


  main_hbox = gtk_hbox_new (FALSE, 12);
  gtk_container_set_border_width (GTK_CONTAINER (main_hbox), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (top_level_dlg)->vbox), main_hbox,
                      TRUE, TRUE, 0);

  /* Add buttons beside the preview frame */
  vbox = gtk_vbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (main_hbox), vbox, FALSE, FALSE, 0);
  gtk_widget_show (vbox);

  gtk_box_pack_start (GTK_BOX (vbox),
                      draw_buttons (top_level_dlg), FALSE, FALSE, 0);

  /* Preview itself */
  gtk_box_pack_start (GTK_BOX (main_hbox), make_preview (), FALSE, FALSE, 0);

  gtk_widget_show (gfig_context->preview);

  /* Style frame on right side */
  frame = gimp_frame_new ("Style");
  gtk_box_pack_start (GTK_BOX (main_hbox), frame, FALSE, FALSE, 0);
  gtk_widget_show (frame);

  vbox = gtk_vbox_new (FALSE, 3);
  gtk_container_add (GTK_CONTAINER (frame), vbox);
  gtk_widget_show (vbox);

  /* foreground color button in Style frame*/
  gfig_context->fg_color = (GimpRGB*)g_malloc (sizeof (GimpRGB));
  gfig_context->fg_color_button = gimp_color_button_new ("Foreground",
                                                    SEL_BUTTON_WIDTH,
                                                    SEL_BUTTON_HEIGHT,
                                                    gfig_context->fg_color,
                                                    GIMP_COLOR_AREA_SMALL_CHECKS);
  g_signal_connect (gfig_context->fg_color_button, "color-changed",
                    G_CALLBACK (set_foreground_callback),
                    gfig_context->fg_color);
  gimp_color_button_set_color (GIMP_COLOR_BUTTON (gfig_context->fg_color_button),
                               &gfig_context->current_style->foreground);
  gtk_box_pack_start (GTK_BOX (vbox), gfig_context->fg_color_button,
                      FALSE, FALSE, 0);
  gtk_widget_show (gfig_context->fg_color_button);

  /* background color button in Style frame */
  gfig_context->bg_color = (GimpRGB*)g_malloc (sizeof (GimpRGB));
  gfig_context->bg_color_button = gimp_color_button_new ("Background",
                                           SEL_BUTTON_WIDTH, SEL_BUTTON_HEIGHT,
                                           gfig_context->bg_color,
                                           GIMP_COLOR_AREA_SMALL_CHECKS);
  g_signal_connect (gfig_context->bg_color_button, "color-changed",
                    G_CALLBACK (set_background_callback),
                    gfig_context->bg_color);
  gimp_color_button_set_color (GIMP_COLOR_BUTTON (gfig_context->bg_color_button),
                               &gfig_context->current_style->background);
  gtk_box_pack_start (GTK_BOX (vbox), gfig_context->bg_color_button, FALSE, FALSE, 0);
  gtk_widget_show (gfig_context->bg_color_button);

  /* brush selector in Style frame */
  gfig_context->brush_select
    = gimp_brush_select_widget_new ("Brush", gfig_context->current_style->brush_name,
                                    -1, -1, -1,
                                    gfig_brush_changed_callback,
                                    NULL);
  gtk_box_pack_start (GTK_BOX (vbox), gfig_context->brush_select,
                      FALSE, FALSE, 0);
  gtk_widget_show (gfig_context->brush_select);

  /* pattern selector in Style frame */
  gfig_context->pattern_select
    = gimp_pattern_select_widget_new ("Pattern", gfig_context->current_style->pattern,
                                      gfig_pattern_changed_callback,
                                      NULL);
  gtk_box_pack_start (GTK_BOX (vbox), gfig_context->pattern_select,
                      FALSE, FALSE, 0);
  gtk_widget_show (gfig_context->pattern_select);

  /* gradient selector in Style frame */
  gfig_context->gradient_select
    = gimp_gradient_select_widget_new ("Gradient", gfig_context->current_style->gradient,
                                       gfig_gradient_changed_callback,
                                       NULL);
  gtk_box_pack_start (GTK_BOX (vbox), gfig_context->gradient_select,
                      FALSE, FALSE, 0);
  gtk_widget_show (gfig_context->gradient_select);

  /* fill style combo box in Style frame  */
  gfig_context->fillstyle_combo = combo
    = gimp_int_combo_box_new (_("Pattern"),    FILL_PATTERN,
                              _("Foreground"), FILL_FOREGROUND,
                              _("Background"), FILL_BACKGROUND,
                              NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), 0);
  g_signal_connect (combo, "changed",
                    G_CALLBACK (select_combo_callback),
                    GINT_TO_POINTER (SELECT_TYPE_MENU_FILL));
  gtk_box_pack_start (GTK_BOX (vbox), combo, FALSE, FALSE, 0);
  gtk_widget_show (combo);


  /* Load saved objects */
  gfig_list_load_all (gfig_path);

  /* Setup initial brush settings */
  gfig_context->bdesc.name = mygimp_brush_get ();
  mygimp_brush_info (&gfig_context->bdesc.width, &gfig_context->bdesc.height);

  gtk_widget_show (main_hbox);

  gtk_widget_show (top_level_dlg);

  dialog_update_preview (gfig_drawable);
  gfig_new_gc (); /* Need this for drawing */

  gfig = gfig_load_from_parasite ();
  if (gfig)
    {
      gfig_list_insert (gfig);
      new_obj_2edit (gfig);
      gfig_style_set_context_from_style (&gfig_context->default_style);
      gfig_style_apply (&gfig_context->default_style);
    }

  gfig_context->enable_repaint = TRUE;
  gfig_paint_callback ();

  gtk_main ();

  /* FIXME */
  return TRUE;
}


static void
gfig_response (GtkWidget *widget,
               gint       response_id,
               gpointer   data)
{
  switch (response_id)
    {
    case RESPONSE_UNDO:
      if (undo_water_mark >= 0)
        {
          /* Free current objects an reinstate previous */
          free_all_objs (gfig_context->current_obj->obj_list);
          gfig_context->current_obj->obj_list = NULL;
          tmp_bezier = tmp_line = obj_creating = NULL;
          gfig_context->current_obj->obj_list = undo_table[undo_water_mark];
          undo_water_mark--;
          /* Update the screen */
          gtk_widget_queue_draw (gfig_context->preview);
          /* And preview */
          list_button_update (gfig_context->current_obj);
          gfig_context->current_obj->obj_status |= GFIG_MODIFIED;
        }

      gtk_dialog_set_response_sensitive (GTK_DIALOG (widget),
                                         RESPONSE_UNDO, undo_water_mark >= 0);
      gfig_paint_callback ();
      break;

    case RESPONSE_CLEAR:
      /* Make sure we can get back - if we have some objects to get back to */
      if (!gfig_context->current_obj->obj_list)
        return;

      setup_undo ();
      /* Free all objects */
      free_all_objs (gfig_context->current_obj->obj_list);
      gfig_context->current_obj->obj_list = NULL;
      obj_creating = NULL;
      tmp_line = NULL;
      tmp_bezier = NULL;
      gtk_widget_queue_draw (gfig_context->preview);
      /* And preview */
      list_button_update (gfig_context->current_obj);
      gfig_paint_callback ();
      break;

    case RESPONSE_SAVE:
      gfig_save (widget);  /* Save current object */
      break;

    case GTK_RESPONSE_OK:  /* Close button */
      gfig_style_copy (&gfig_context->default_style, gfig_context->current_style, "object");
      gfig_save_as_parasite ();
      gtk_widget_destroy (widget);
      break;

    case RESPONSE_PAINT:
      gfig_paint_callback ();
      break;

    default:
      gtk_widget_destroy (widget);
      break;
    }
}

static void
load_button_callback (GtkWidget *widget,
                      gpointer   data)
{
  static GtkWidget *window = NULL;

  /* Load a single object */
  window = gtk_file_chooser_dialog_new (_("Load Gfig object collection"),
                                        GTK_WINDOW (gtk_widget_get_toplevel (widget)),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,

                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OPEN,   GTK_RESPONSE_OK,

                                        NULL);

  gtk_dialog_set_default_response (GTK_DIALOG (window), GTK_RESPONSE_OK);

  g_signal_connect (window, "destroy",
                    G_CALLBACK (gtk_widget_destroyed),
                    &window);
  g_signal_connect (window, "response",
                    G_CALLBACK (gfig_load_file_chooser_response),
                    window);

  gtk_widget_show (window);
}

static void
merge_button_callback (GtkWidget *widget,
                       gpointer   data)
{
  GList     *sellist;
  GFigObj   *sel_obj;
  DAllObjs  *obj_copies;
  GtkWidget *list = (GtkWidget *) data;

  /* Must update which object we are editing */
  /* Get the list and which item is selected */
  /* Only allow single selections */

  sellist = GTK_LIST (list)->selection;

  sel_obj = (GFigObj *) g_object_get_data (G_OBJECT (sellist->data),
                                           "user_data");

  if (sel_obj && sel_obj->obj_list && sel_obj != gfig_context->current_obj)
    {
      /* Copy list tag onto current & redraw */
      obj_copies = copy_all_objs (sel_obj->obj_list);
      prepend_to_all_obj (gfig_context->current_obj, obj_copies);

      /* redraw all */
      gtk_widget_queue_draw (gfig_context->preview);
      /* And preview */
      list_button_update (gfig_context->current_obj);
    }
}

static void
gfig_new_gc (void)
{
  GdkColor fg, bg;

  /*  create a new graphics context  */
  gfig_gc = gdk_gc_new (gfig_context->preview->window);

  gdk_gc_set_function (gfig_gc, GDK_INVERT);

  fg.pixel = 0xFFFFFFFF;
  bg.pixel = 0x00000000;
  gdk_gc_set_foreground (gfig_gc, &fg);
  gdk_gc_set_background (gfig_gc, &bg);

  gdk_gc_set_line_attributes (gfig_gc, 1,
                              GDK_LINE_SOLID, GDK_CAP_BUTT, GDK_JOIN_MITER);
}


static void
gfig_save_menu_callback (GtkWidget *widget,
                         gpointer   data)
{
  create_save_file_chooser (gfig_context->current_obj, NULL, GTK_WIDGET (data));
}

/* Given a point x, y draw a circle */
void
draw_circle (GdkPoint *p)
{
  if (!selvals.opts.showcontrol)
    return;

  gdk_draw_arc (gfig_context->preview->window,
                gfig_gc,
                0,
                p->x - SQ_SIZE/2,
                p->y - SQ_SIZE/2,
                SQ_SIZE,
                SQ_SIZE,
                0,
                360*64);
}

static void
gfig_list_load_all (const gchar *path)
{
  /*  Make sure to clear any existing gfigs  */
  gfig_context->current_obj = pic_obj = NULL;
  gfig_list_free_all ();


  if (! gfig_list)
    {
      GFigObj *gfig;

      /* lets have at least one! */
      gfig = gfig_new ();
      gfig->draw_name = g_strdup (_("First Gfig"));
      gfig_list_insert (gfig);
    }

  pic_obj = gfig_context->current_obj = gfig_list->data;  /* set to first entry */
}

static void
gfig_save (GtkWidget *parent)
{
  create_save_file_chooser (gfig_context->current_obj, NULL, parent);
}

static void
gfig_list_free_all (void)
{
  g_list_foreach (gfig_list, (GFunc) gfig_free, NULL);
  g_list_free (gfig_list);
  gfig_list = NULL;
}

static void
create_save_file_chooser (GFigObj   *obj,
                          gchar     *tpath,
                          GtkWidget *parent)
{
  static GtkWidget *window = NULL;

  if (! window)
    {
      window =
        gtk_file_chooser_dialog_new (_("Save Gfig Drawing"),
                                     GTK_WINDOW (parent),
                                     GTK_FILE_CHOOSER_ACTION_SAVE,

                                     GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                     GTK_STOCK_SAVE,   GTK_RESPONSE_OK,

                                     NULL);

      g_signal_connect (window, "destroy",
                        G_CALLBACK (gtk_widget_destroyed),
                        &window);
      g_signal_connect (window, "response",
                        G_CALLBACK (file_chooser_response),
                        obj);
    }

  if (tpath)
    {
      gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (window), tpath);
    }
  else if (gfig_path)
    {
      GList *list;
      gchar *dir;

      list = gimp_path_parse (gfig_path, 16, FALSE, 0);
      dir = gimp_path_get_user_writable_dir (list);
      gimp_path_free (list);

      if (! dir)
        dir = g_strdup (gimp_directory ());

      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (window), dir);

      g_free (dir);
    }
  else
    {
      const gchar *tmp = g_get_tmp_dir ();

      gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (window), tmp);
    }



  fprintf (stderr, "Got here.\n");
  gtk_window_present (GTK_WINDOW (window));
}

#define ADVANCE   ++col; if (col == ncol) { ++row; col = 0; }

#define SKIP_ROW   if (col != 0) {++row; col = 0;} \
                   gtk_table_set_row_spacing (GTK_TABLE (table), row, 12); ++row;

static GtkWidget *
draw_buttons (GtkWidget *ww)
{
  GtkWidget *button;
  GtkWidget *frame;
  GSList    *group = NULL;
  GtkWidget *table;
  gint       col, row;
  gint       ncol;


  /* Create group */
  frame = gimp_frame_new ("Toolbox");

  table = gtk_table_new (9, 4, FALSE);
  gtk_container_add (GTK_CONTAINER (frame), table);
  gtk_widget_show (table);

  ncol = 4;
  row = col = 0;

  /* Put buttons in */
  button = but_with_pix (GFIG_STOCK_LINE, &group, LINE);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create line"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_CIRCLE, &group, CIRCLE);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create circle"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_ELLIPSE, &group, ELLIPSE);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create ellipse"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_CURVE, &group, ARC);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Create arch"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_POLYGON, &group, POLY);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);

  g_signal_connect (button, "button_press_event",
                    G_CALLBACK (poly_button_press),
                    NULL);
  gimp_help_set_help_data (button, _("Create reg polygon"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_STAR, &group, STAR);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  g_signal_connect (button, "button_press_event",
                    G_CALLBACK (star_button_press),
                    NULL);
  gimp_help_set_help_data (button, _("Create star"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_SPIRAL, &group, SPIRAL);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);

  g_signal_connect (button, "button_press_event",
                    G_CALLBACK (spiral_button_press),
                    NULL);
  gimp_help_set_help_data (button, _("Create spiral"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_BEZIER, &group, BEZIER);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  g_signal_connect (button, "button_press_event",
                    G_CALLBACK (bezier_button_press),
                    NULL);

  gimp_help_set_help_data (button,
                        _("Create bezier curve. "
                          "Shift + Button ends object creation."), NULL);

  SKIP_ROW

  button = but_with_pix (GFIG_STOCK_MOVE_OBJECT, &group, MOVE_OBJ);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Move an object"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_MOVE_POINT, &group, MOVE_POINT);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Move a single point"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_COPY_OBJECT, &group, COPY_OBJ);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Copy an object"), NULL);

  ADVANCE

  button = but_with_pix (GFIG_STOCK_DELETE_OBJECT, &group, DEL_OBJ);
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+1, row, row+1);
  gtk_widget_show (button);
  gimp_help_set_help_data (button, _("Delete an object"), NULL);

  SKIP_ROW

  button = obj_select_buttons ();
  gtk_table_attach_defaults (GTK_TABLE (table), button, col, col+2, row, row+2);
  gtk_widget_show (button);

  gtk_widget_show (frame);

  return frame;
}


static void
select_combo_callback (GtkWidget *widget,
                       gpointer   data)
{
  gint mtype = GPOINTER_TO_INT (data);
  gint value;

  gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (widget), &value);

  switch (mtype)
    {
    case SELECT_TYPE_MENU:
      selopt.type = (SelectionType) value;
      break;
    case SELECT_ARCTYPE_MENU:
      selopt.as_pie = (ArcType) value;
      break;
    case SELECT_TYPE_MENU_FILL:
      selopt.fill_type = (FillType) value;
      break;
    case SELECT_TYPE_MENU_WHEN:
      selopt.fill_when = (FillWhen) value;
      break;
    default:
      g_return_if_reached ();
      break;
    }

  gfig_paint_callback ();
}

static void
options_dialog_callback (GtkWidget *widget,
                         gpointer   data)
{
  GtkWidget *options_dlg;
  GtkWidget *table;
  GtkWidget *toggle;
  GtkWidget *vbox;
  GtkObject *size_data;
  GtkWidget *scale;
  GtkObject *scale_data;

  options_dlg = gimp_dialog_new (_("Options"), "gfig",
                              NULL, 0,
                              gimp_standard_help_func, HELP_ID,

                              GTK_STOCK_CLOSE,  GTK_RESPONSE_OK,

                              NULL);

  vbox = GTK_DIALOG (options_dlg)->vbox;

  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);


  /* Put buttons in */
  toggle = gtk_check_button_new_with_label (_("Show image"));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle),
                                gfig_context->show_background);
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 6);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &gfig_context->show_background);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gfig_preview_expose),
                    NULL);
  gtk_widget_show (toggle);

  toggle = gtk_check_button_new_with_label (_("Show position"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 6);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selvals.showpos);
  g_signal_connect_after (toggle, "toggled",
                          G_CALLBACK (gfig_pos_enable),
                          NULL);
  gtk_widget_show (toggle);

  toggle = gtk_check_button_new_with_label (_("Hide control points"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 6);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selvals.opts.showcontrol);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (toggle_show_image),
                    NULL);
  gtk_widget_show (toggle);
  gfig_opt_widget.showcontrol = toggle;

  toggle = gtk_check_button_new_with_label (_("Antialiasing"));
  gtk_box_pack_start (GTK_BOX (vbox), toggle, FALSE, FALSE, 6);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selopt.antia);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gfig_paint_callback),
                    NULL);
  gtk_widget_show (toggle);

  table = gtk_table_new (4, 4, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 6);
  gtk_widget_show (table);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                                    _("Max undo:"), 100, 50,
                                    selvals.maxundo, MIN_UNDO, MAX_UNDO, 1, 2, 0,
                                    TRUE, 0, 0,
                                    NULL, NULL);
  g_signal_connect (size_data, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &selvals.maxundo);

  page_menu_bg = gimp_int_combo_box_new (_("Transparent"), LAYER_TRANS_BG,
                                         _("Background"),  LAYER_BG_BG,
                                         _("Foreground"),  LAYER_FG_BG,
                                         _("White"),       LAYER_WHITE_BG,
                                         _("Copy"),        LAYER_COPY_BG,
                                         NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (page_menu_bg), 0);

  g_signal_connect (page_menu_bg, "changed",
                    G_CALLBACK (paint_combo_callback),
                    GINT_TO_POINTER (PAINT_BGS_MENU));

  gimp_help_set_help_data (page_menu_bg,
                           _("Layer background type. Copy causes the previous "
                             "layer to be copied before the draw is performed."),
                           NULL);

  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
                             _("Background:"), 0.0, 0.5,
                             page_menu_bg, 2, FALSE);


  toggle = gtk_check_button_new_with_label (_("Feather"));
  gtk_table_attach (GTK_TABLE (table), toggle, 2, 3, 2, 3,
                    GTK_FILL, GTK_FILL, 0, 0);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selopt.feather);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gfig_paint_callback),
                    NULL);
  gtk_widget_show (toggle);

  scale_data =
    gtk_adjustment_new (selopt.feather_radius, 0.0, 100.0, 1.0, 1.0, 0.0);
  scale = gtk_hscale_new (GTK_ADJUSTMENT (scale_data));
  gtk_scale_set_value_pos (GTK_SCALE (scale), GTK_POS_TOP);
  gtk_range_set_update_policy (GTK_RANGE (scale), GTK_UPDATE_DELAYED);
  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (gimp_double_adjustment_update),
                    &selopt.feather_radius);
  g_signal_connect (scale_data, "value_changed",
                    G_CALLBACK (gfig_paint_callback),
                    NULL);
  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
                             _("Radius:"), 0.5, 0.5,
                             scale, 1, FALSE);

  gimp_dialog_run (GIMP_DIALOG (options_dlg));
  gtk_widget_destroy (options_dlg);
}

static void
adjust_grid_callback (GtkWidget *widget,
                      gpointer   data)
{
  GtkWidget *grid_dlg;
  GtkWidget *vbox;
  GtkWidget *hbox;
  GtkWidget *table;
  GtkWidget *toggle;
  GtkObject *size_data;
  GtkWidget *combo;

  grid_dlg = gimp_dialog_new (_("Grid"), "gfig",
                              NULL, 0,
                              gimp_standard_help_func, HELP_ID,

                              GTK_STOCK_CLOSE,  GTK_RESPONSE_OK,

                              NULL);

  vbox = GTK_DIALOG (grid_dlg)->vbox;

  hbox = gtk_hbox_new (FALSE, 6);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 6);
  gtk_widget_show (hbox);

  toggle = gtk_check_button_new_with_label (_("Show grid"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selvals.opts.drawgrid);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (draw_grid_clear),
                    NULL);
  gtk_widget_show (toggle);
  gfig_opt_widget.drawgrid = toggle;

  toggle = gtk_check_button_new_with_label (_("Snap to grid"));
  gtk_box_pack_start (GTK_BOX (hbox), toggle, FALSE, FALSE, 0);
  g_signal_connect (toggle, "toggled",
                    G_CALLBACK (gimp_toggle_button_update),
                    &selvals.opts.snap2grid);
  gtk_widget_show (toggle);
  gfig_opt_widget.snap2grid = toggle;

  table = gtk_table_new (3, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_box_pack_start (GTK_BOX (vbox), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                                    _("Grid spacing:"), 100, 50,
                                    selvals.opts.gridspacing,
                                    MIN_GRID, MAX_GRID, 1, 10, 0,
                                    TRUE, 0, 0,
                                    NULL, NULL);
  g_signal_connect (size_data, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    &selvals.opts.gridspacing);
  g_signal_connect (size_data, "value_changed",
                    G_CALLBACK (draw_grid_clear),
                    NULL);
  gfig_opt_widget.gridspacing = size_data;

  combo = gimp_int_combo_box_new (_("Rectangle"), RECT_GRID,
                                  _("Polar"),     POLAR_GRID,
                                  _("Isometric"), ISO_GRID,
                                  NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), 0);

  g_signal_connect (combo, "changed",
                    G_CALLBACK (gridtype_combo_callback),
                    GINT_TO_POINTER (GRID_TYPE_MENU));

  gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
                             _("Grid type:"), 0.0, 0.5,
                             combo, 2, FALSE);

  gfig_opt_widget.gridtypemenu = combo;

  combo = gimp_int_combo_box_new (_("Normal"),    GTK_STATE_NORMAL,
                                  _("Black"),     GFIG_BLACK_GC,
                                  _("White"),     GFIG_WHITE_GC,
                                  _("Grey"),      GFIG_GREY_GC,
                                  _("Darker"),    GTK_STATE_ACTIVE,
                                  _("Lighter"),   GTK_STATE_PRELIGHT,
                                  _("Very dark"), GTK_STATE_SELECTED,
                                  NULL);
  gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), 0);

  g_signal_connect (combo, "changed",
                    G_CALLBACK (gridtype_combo_callback),
                    GINT_TO_POINTER (GRID_RENDER_MENU));

  gimp_table_attach_aligned (GTK_TABLE (table), 0, 2,
                             _("Grid color:"), 0.0, 0.5,
                             combo, 2, FALSE);

  gimp_dialog_run (GIMP_DIALOG (grid_dlg));
  gtk_widget_destroy (grid_dlg);
}

void
update_options (GFigObj *old_obj)
{
  /* Save old vals */
  if (selvals.opts.gridspacing != old_obj->opts.gridspacing)
    {
      old_obj->opts.gridspacing = selvals.opts.gridspacing;
    }
  if (selvals.opts.gridtype != old_obj->opts.gridtype)
    {
      old_obj->opts.gridtype = selvals.opts.gridtype;
    }
  if (selvals.opts.drawgrid != old_obj->opts.drawgrid)
    {
      old_obj->opts.drawgrid = selvals.opts.drawgrid;
    }
  if (selvals.opts.snap2grid != old_obj->opts.snap2grid)
    {
      old_obj->opts.snap2grid = selvals.opts.snap2grid;
    }
  if (selvals.opts.lockongrid != old_obj->opts.lockongrid)
    {
      old_obj->opts.lockongrid = selvals.opts.lockongrid;
    }
  if (selvals.opts.showcontrol != old_obj->opts.showcontrol)
    {
      old_obj->opts.showcontrol = selvals.opts.showcontrol;
    }

  /* New vals */
  if (selvals.opts.gridspacing != gfig_context->current_obj->opts.gridspacing)
    {
      gtk_adjustment_set_value (GTK_ADJUSTMENT (gfig_opt_widget.gridspacing),
                                gfig_context->current_obj->opts.gridspacing);
    }
  if (selvals.opts.drawgrid != gfig_context->current_obj->opts.drawgrid)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gfig_opt_widget.drawgrid),
                                    gfig_context->current_obj->opts.drawgrid);
    }
  if (selvals.opts.snap2grid != gfig_context->current_obj->opts.snap2grid)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gfig_opt_widget.snap2grid),
                                    gfig_context->current_obj->opts.snap2grid);
    }
  if (selvals.opts.lockongrid != gfig_context->current_obj->opts.lockongrid)
    {
#if 0
      /* Maurits: code not implemented */
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gfig_opt_widget.lockongrid),
                                    gfig_context->current_obj->opts.lockongrid);
#endif
    }
  if (selvals.opts.showcontrol != gfig_context->current_obj->opts.showcontrol)
    {
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (gfig_opt_widget.showcontrol),
                                    gfig_context->current_obj->opts.showcontrol);
    }
  if (selvals.opts.gridtype != gfig_context->current_obj->opts.gridtype)
    {
      gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (gfig_opt_widget.gridtypemenu),
                                     gfig_context->current_obj->opts.gridtype);

#ifdef DEBUG
      printf ("Gridtype set in options to ");
      if (gfig_context->current_obj->opts.gridtype == RECT_GRID)
        printf ("RECT_GRID\n");
      else if (gfig_context->current_obj->opts.gridtype == POLAR_GRID)
        printf ("POLAR_GRID\n");
      else if (gfig_context->current_obj->opts.gridtype == ISO_GRID)
        printf ("ISO_GRID\n");
      else printf ("NONE\n");
#endif /* DEBUG */
    }
}

static void
file_chooser_response (GtkFileChooser *chooser,
                       gint            response_id,
                       GFigObj        *obj)
{
  if (response_id == GTK_RESPONSE_OK)
    {
      gchar   *filename;
      GFigObj *real_current;

      filename = gtk_file_chooser_get_filename (chooser);

      obj->filename = filename;

      real_current = gfig_context->current_obj;
      gfig_context->current_obj = obj;
      gfig_save_callbk ();
      gfig_context->current_obj = gfig_context->current_obj;
    }

  gtk_widget_destroy (GTK_WIDGET (chooser));
}

static Dobject *
gfig_select_obj_by_number (gint count)
{
  DAllObjs *objs = gfig_context->current_obj->obj_list;
  gint      k = 0;

  gfig_context->selected_obj = NULL;

  while (objs)
    {
      if (k == obj_show_single)
        {
          gfig_context->selected_obj = objs->obj;
          gfig_context->current_style = &objs->obj->style;
          gfig_style_set_context_from_style (&objs->obj->style);
          break;
        }

      objs = objs->next;

      k++;
    }

  return objs->obj;
}

static void
select_button_clicked (GtkWidget *widget,
                       gpointer   data)
{
  gint      type  = GPOINTER_TO_INT (data);
  gint      count = 0;
  DAllObjs *objs;

  if (gfig_context->current_obj)
    {
      for (objs = gfig_context->current_obj->obj_list; objs; objs = objs->next)
        count++;
    }

  switch (type)
    {
    case OBJ_SELECT_LT:
      obj_show_single--;
      if (obj_show_single < 0)
        obj_show_single = count - 1;
      break;

    case OBJ_SELECT_GT:
      obj_show_single++;
      if (obj_show_single >= count)
        obj_show_single = 0;
      break;

    case OBJ_SELECT_EQ:
      obj_show_single = -1; /* Reset to show all */
      break;

    default:
      break;
    }

  objs = gfig_context->current_obj->obj_list;

  if (obj_show_single >= 0)
    gfig_select_obj_by_number (obj_show_single);

  draw_grid_clear ();
}

static GtkWidget *
obj_select_buttons (void)
{
  GtkWidget *button;
  GtkWidget *image;
  GtkWidget *hbox, *vbox;

  vbox = gtk_vbox_new (TRUE, 0);
  gtk_widget_show (vbox);

  hbox = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  button = gtk_button_new ();
  gimp_help_set_help_data (button, _("Show previous object"), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (select_button_clicked),
                    GINT_TO_POINTER (OBJ_SELECT_LT));
  gtk_widget_show (button);

  image = gtk_image_new_from_stock (GTK_STOCK_GO_BACK,
                                    GTK_ICON_SIZE_BUTTON);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_widget_show (image);

  button = gtk_button_new ();
  gimp_help_set_help_data (button, _("Show next object"), NULL);
  gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (select_button_clicked),
                    GINT_TO_POINTER (OBJ_SELECT_GT));
  gtk_widget_show (button);

  image = gtk_image_new_from_stock (GTK_STOCK_GO_FORWARD,
                                    GTK_ICON_SIZE_BUTTON);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_widget_show (image);

  button = gtk_button_new_with_label (_("All"));
  gimp_help_set_help_data (button, _("Show all objects"), NULL);
  gtk_box_pack_start (GTK_BOX (vbox), button, TRUE, TRUE, 0);
  g_signal_connect (button, "clicked",
                    G_CALLBACK (select_button_clicked),
                    GINT_TO_POINTER (OBJ_SELECT_EQ));
  gtk_widget_show (button);

  return vbox;
}

static GtkWidget *
but_with_pix (const gchar  *stock_id,
              GSList      **group,
              gint          baction)
{
  GtkWidget *button;

  button = gtk_radio_button_new_with_label (*group, stock_id);
  gtk_button_set_use_stock (GTK_BUTTON (button), TRUE);
  gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);
  g_signal_connect (button, "toggled",
                    G_CALLBACK (toggle_obj_type),
                    GINT_TO_POINTER (baction));
  gtk_widget_show (button);

  *group = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));

  return button;
}


/* Special case for now - options on poly/star/spiral button */

void
num_sides_dialog (gchar *d_title,
                  gint  *num_sides,
                  gint  *which_way,
                  gint   adj_min,
                  gint   adj_max)
{
  GtkWidget *window;
  GtkWidget *table;
  GtkObject *size_data;

  window = gimp_dialog_new (d_title, "gfig",
                            NULL, 0,
                            gimp_standard_help_func, HELP_ID,

                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,

                            NULL);

  g_signal_connect (window, "response",
                    G_CALLBACK (gtk_widget_destroy),
                    NULL);

  table = gtk_table_new (which_way ? 2 : 1, 3, FALSE);
  gtk_table_set_col_spacings (GTK_TABLE (table), 6);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_container_set_border_width (GTK_CONTAINER (table), 12);
  gtk_box_pack_start (GTK_BOX (GTK_DIALOG (window)->vbox), table,
                      FALSE, FALSE, 0);
  gtk_widget_show (table);

  size_data = gimp_scale_entry_new (GTK_TABLE (table), 0, 0,
                                    _("Number of Sides/Points/Turns:"), 0, 0,
                                    *num_sides, adj_min, adj_max, 1, 10, 0,
                                    TRUE, 0, 0,
                                    NULL, NULL);
  g_signal_connect (size_data, "value_changed",
                    G_CALLBACK (gimp_int_adjustment_update),
                    num_sides);

  if (which_way)
    {
      GtkWidget *combo = gimp_int_combo_box_new (_("Clockwise"),      0,
                                                 _("Anti-Clockwise"), 1,
                                                 NULL);

      gimp_int_combo_box_set_active (GIMP_INT_COMBO_BOX (combo), *which_way);

      g_signal_connect (combo, "changed",
                        G_CALLBACK (gimp_int_combo_box_get_active),
                        which_way);

      gimp_table_attach_aligned (GTK_TABLE (table), 0, 1,
                                 _("Orientation:"), 0.0, 0.5,
                                 combo, 1, FALSE);
    }

  gtk_widget_show (window);
}

static gint
bezier_button_press (GtkWidget      *widget,
                     GdkEventButton *event,
                     gpointer        data)
{
  if ((event->type == GDK_2BUTTON_PRESS) &&
      (event->button == 1))
    bezier_dialog ();
  return FALSE;
}

void
gfig_paint (BrushType brush_type,
            gint32    drawable_ID,
            gint      seg_count,
            gdouble   line_pnts[])
{
  switch (brush_type)
    {
    case BRUSH_BRUSH_TYPE:
      gimp_paintbrush (drawable_ID,
                       selvals.brushfade,
                       seg_count, line_pnts,
                       GIMP_PAINT_CONSTANT,
                       selvals.brushgradient);
      break;

    case BRUSH_PENCIL_TYPE:
      gimp_pencil (drawable_ID,
                   seg_count, line_pnts);
      break;

    case BRUSH_AIRBRUSH_TYPE:
      gimp_airbrush (drawable_ID,
                     selvals.airbrushpressure,
                     seg_count, line_pnts);
      break;

    case BRUSH_PATTERN_TYPE:
      gimp_clone (drawable_ID,
                  drawable_ID,
                  GIMP_PATTERN_CLONE,
                  0.0, 0.0,
                  seg_count, line_pnts);
      break;
    }
}


static void
paint_combo_callback (GtkWidget *widget,
                      gpointer   data)
{
  gint mtype = GPOINTER_TO_INT (data);
  gint value;

  gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (widget), &value);

  switch (mtype)
    {
    case PAINT_LAYERS_MENU:
      selvals.onlayers = (DrawonLayers) value;

    case PAINT_BGS_MENU:
      selvals.onlayerbg = (LayersBGType) value;

#ifdef DEBUG
      printf ("BG type = %d\n", selvals.onlayerbg);
#endif /* DEBUG */
      break;

    case PAINT_TYPE_MENU:
      selvals.painttype = (PaintType) value;

#ifdef DEBUG
      printf ("Got type menu = %d\n", selvals.painttype);
#endif /* DEBUG */

    default:
      g_return_if_reached ();
      break;
    }

  gfig_paint_callback ();
}


static void
gridtype_combo_callback (GtkWidget *widget,
                         gpointer   data)
{
  gint mtype = GPOINTER_TO_INT (data);
  gint value;

  gimp_int_combo_box_get_active (GIMP_INT_COMBO_BOX (widget), &value);

  switch (mtype)
    {
    case GRID_TYPE_MENU:
      selvals.opts.gridtype = value;
      break;
    case GRID_RENDER_MENU:
      grid_gc_type = value;
      break;
    default:
      g_return_if_reached ();
      break;
    }

  draw_grid_clear ();
}



/*
 *  The edit gfig name attributes dialog
 *  Modified from Gimp source - layer edit.
 */

typedef struct _GfigListOptions
{
  GtkWidget *query_box;
  GtkWidget *name_entry;
  GtkWidget *list_entry;
  GFigObj   *obj;
  gboolean   created;
} GfigListOptions;

void
list_button_update (GFigObj *obj)
{
  g_return_if_fail (obj != NULL);

  pic_obj = (GFigObj *) obj;

}


static void
gfig_load_file_chooser_response (GtkFileChooser *chooser,
                                 gint            response_id,
                                 gpointer        data)
{
  if (response_id == GTK_RESPONSE_OK)
    {
      gchar   *filename;
      GFigObj *gfig;
      GFigObj *current_saved;

      filename = gtk_file_chooser_get_filename (chooser);

      if (g_file_test (filename, G_FILE_TEST_IS_REGULAR))
        {
          /* Hack - current object MUST be NULL to prevent setup_undo ()
           * from kicking in.
           */
          current_saved = gfig_context->current_obj;
          gfig_context->current_obj = NULL;
          gfig = gfig_load (filename, filename);
          gfig_context->current_obj = current_saved;

          if (gfig)
            {
              /* Read only ?*/
              if (access (filename, W_OK))
                gfig->obj_status |= GFIG_READONLY;

              gfig_list_insert (gfig);
              new_obj_2edit (gfig);
            }
        }

      g_free (filename);
    }

  gtk_widget_destroy (GTK_WIDGET (chooser));
  gfig_paint_callback ();
}

static void
paint_layer_fill (void)
{
  gimp_edit_bucket_fill (gfig_context->drawable_id,
                         selopt.fill_type,    /* Fill mode */
                         GIMP_NORMAL_MODE,
                         selopt.fill_opacity, /* Fill opacity */
                         0.0,                 /* threshold - ignored */
                         FALSE,               /* Sample merged - ignored */
                         0.0,                 /* x - ignored */
                         0.0);                /* y - ignored */
}

void
gfig_paint_callback (void)
{
  DAllObjs  *objs;
  gint       layer_count = 0;
  gchar      buf[128];
  gint       count;
  gint       ccount = 0;

  if (!gfig_context->enable_repaint || !gfig_context->current_obj)
    return;

  objs = gfig_context->current_obj->obj_list;

  count = gfig_obj_counts (objs);

  gimp_drawable_fill (gfig_context->drawable_id, GIMP_TRANSPARENT_FILL);

  while (objs)
    {
      if (ccount == obj_show_single || obj_show_single == -1)
        {
          sprintf (buf, _("Gfig layer %d"), layer_count++);

          gfig_style_apply (&objs->obj->style);

          objs->obj->class->paintfunc (objs->obj);

          /* Fill layer if required */
          if (selvals.painttype == PAINT_SELECTION_FILL_TYPE
              && selopt.fill_when == FILL_EACH)
            paint_layer_fill ();
        }

      objs = objs->next;

      ccount++;
    }

  /* Fill layer if required */
  if (selvals.painttype == PAINT_SELECTION_FILL_TYPE
      && selopt.fill_when == FILL_AFTER)
    paint_layer_fill ();

/*   gfig_style_apply (&gfig_context->gimp_style); */

  gimp_displays_flush ();

  if (back_pixbuf)
    {
      g_object_unref (back_pixbuf);
      back_pixbuf = NULL;
    }

  gfig_preview_expose (gfig_context->preview, NULL);
}

/* Given a point x, y draw a square around it */
void
draw_sqr (GdkPoint *p)
{
  if (!selvals.opts.showcontrol)
    return;

  gdk_draw_rectangle (gfig_context->preview->window,
                      gfig_gc,
                      0,
                      gfig_scale_x (p->x) - SQ_SIZE/2,
                      gfig_scale_y (p->y) - SQ_SIZE/2,
                      SQ_SIZE,
                      SQ_SIZE);
}

/* Draw the grid on the screen
 */

void
draw_grid_clear ()
{
  /* wipe slate and start again */
  dialog_update_preview (gfig_drawable);
  gtk_widget_queue_draw (gfig_context->preview);
}

static void
toggle_show_image (void)
{
  /* wipe slate and start again */
  draw_grid_clear ();
}

void
toggle_obj_type (GtkWidget *widget,
                 gpointer   data)
{
  static GdkCursor *p_cursors[DEL_OBJ + 1];
  GdkCursorType     ctype = GDK_LAST_CURSOR;

  if (selvals.otype != (DobjType) GPOINTER_TO_INT (data))
    {
      /* Mem leak */
      obj_creating = NULL;
      tmp_line = NULL;
      tmp_bezier = NULL;

      if ((DobjType)data < MOVE_OBJ)
        {
          obj_show_single = -1; /* Cancel select preview */
        }
      /* Update draw areas */
      gtk_widget_queue_draw (gfig_context->preview);
      /* And preview */
      list_button_update (gfig_context->current_obj);
    }

  selvals.otype = (DobjType) GPOINTER_TO_INT (data);

  switch (selvals.otype)
    {
    case LINE:
    case CIRCLE:
    case ELLIPSE:
    case ARC:
    case POLY:
    case STAR:
    case SPIRAL:
    case BEZIER:
    default:
      ctype = GDK_CROSSHAIR;
      break;
    case MOVE_OBJ:
    case MOVE_POINT:
    case COPY_OBJ:
    case MOVE_COPY_OBJ:
      ctype = GDK_DIAMOND_CROSS;
      break;
    case DEL_OBJ:
      ctype = GDK_PIRATE;
      break;
    }

  if (!p_cursors[selvals.otype])
    {
      GdkDisplay *display = gtk_widget_get_display (gfig_context->preview);

      p_cursors[selvals.otype] = gdk_cursor_new_for_display (display, ctype);
    }

  gdk_window_set_cursor (gfig_context->preview->window, p_cursors[selvals.otype]);
}

/* This could belong in a separate file ... but makes it easier to lump into
 * one when compiling the plugin.
 */

/* Given a number of float co-ords adjust for scaling back to org size */
/* Size is number of PAIRS of points */
/* FP + int varients */

static void
scale_to_orginal_x (gdouble *list)
{
  *list *= scale_x_factor;
}

static gint
gfig_scale_x (gint x)
{
  if (!selvals.scaletoimage)
    return (gint) (x * (1 / scale_x_factor));
  else
    return x;
}

static void
scale_to_orginal_y (gdouble *list)
{
  *list *= scale_y_factor;
}

static gint
gfig_scale_y (gint y)
{
  if (!selvals.scaletoimage)
    return (gint) (y * (1 / scale_y_factor));
  else
    return y;
}

/* Pairs x followed by y */
void
scale_to_original_xy (gdouble *list,
                      gint     size)
{
  gint i;

  for (i = 0; i < size * 2; i += 2)
    {
      scale_to_orginal_x (&list[i]);
      scale_to_orginal_y (&list[i + 1]);
    }
}

/* Pairs x followed by y */
void
scale_to_xy (gdouble *list,
             gint     size)
{
  gint i;

  for (i = 0; i < size * 2; i += 2)
    {
      list[i] *= (org_scale_x_factor / scale_x_factor);
      list[i + 1] *= (org_scale_y_factor / scale_y_factor);
    }
}

/* Given an list of PAIRS of doubles reverse the list */
/* Size is number of pairs to swap */
void
reverse_pairs_list (gdouble *list,
                    gint     size)
{
  gint i;

  struct cs
  {
    gdouble i1;
    gdouble i2;
  } copyit, *orglist;

  orglist = (struct cs *) list;

  /* Uses struct copies */
  for (i = 0; i < size / 2; i++)
    {
      copyit = orglist[i];
      orglist[i] = orglist[size - 1 - i];
      orglist[size - 1 - i] = copyit;
    }
}

void
gfig_draw_arc (gint x, gint y, gint width, gint height, gint angle1,
               gint angle2)
{
  gdk_draw_arc (gfig_context->preview->window,
                gfig_gc,
                FALSE,
                gfig_scale_x (x - width),
                gfig_scale_y (y - height),
                gfig_scale_x (2 * width),
                gfig_scale_y (2 * height),
                angle1 * 64,
                angle2 * 64);
}

void
gfig_draw_line (gint x0, gint y0, gint x1, gint y1)
{
  gdk_draw_line (gfig_context->preview->window,
                 gfig_gc,
                 gfig_scale_x (x0),
                 gfig_scale_y (y0),
                 gfig_scale_x (x1),
                 gfig_scale_y (y1));
}