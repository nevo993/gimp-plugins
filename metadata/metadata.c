/* metadata.c - main() for the metadata editor
 *
 * Copyright (C) 2004-2005, Raphaël Quinet <raphael@gimp.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "config.h"

#include <string.h>
#include <stdio.h>

#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "libgimp/stdplugins-intl.h"

#include "interface.h"
#include "xmp-encode.h"
/* FIXME: uncomment when these are working
#include "exif-decode.h"
#include "exif-encode.h"
#include "iptc-parse.h"
*/

#define METADATA_PARASITE   "gimp-metadata"
#define METADATA_MARKER     "GIMP_XMP_1"
#define METADATA_MARKER_LEN (sizeof (METADATA_MARKER) - 1)

#define HELP_ID             "plug-in-metadata"


/* prototypes of local functions */
static void      query           (void);
static void      run             (const gchar      *name,
				  gint              nparams,
				  const GimpParam  *param,
				  gint             *nreturn_vals,
				  GimpParam       **return_vals);

/* local variables */
GimpPlugInInfo PLUG_IN_INFO =
{
  NULL,  /* init_proc  */
  NULL,  /* quit_proc  */
  query, /* query_proc */
  run,   /* run_proc   */
};

/* local functions */
MAIN ()

static void
query (void)
{
  static GimpParamDef editor_args[] =
  {
    { GIMP_PDB_INT32,       "run_mode",  "Interactive, non-interactive" },
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_DRAWABLE,    "drawable",  "Input drawable (unused)"      }
  };

  static GimpParamDef decode_xmp_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "xmp",       "XMP packet"                   },
  };

  static GimpParamDef encode_xmp_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
  };
  static GimpParamDef encode_xmp_return_vals[] =
  {
    { GIMP_PDB_STRING,      "xmp",       "XMP packet"                   },
  };

/* FIXME: uncomment when these are working
  static GimpParamDef decode_exif_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_INT32,       "exif_size", "size of the EXIF block"       },
    { GIMP_PDB_INT8ARRAY,   "exif",      "EXIF block"                   },
  };

  static GimpParamDef encode_exif_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
  };
  static GimpParamDef encode_exif_return_vals[] =
  {
    { GIMP_PDB_INT32,       "exif_size", "size of the EXIF block"       },
    { GIMP_PDB_INT8ARRAY,   "exif",      "EXIF block"                   },
  };
*/

  static GimpParamDef get_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "schema",    "XMP schema prefix or URI"     },
    { GIMP_PDB_STRING,      "property",  "XMP property name"            },
  };
  static GimpParamDef get_return_vals[] =
  {
    { GIMP_PDB_INT32,       "type",      "XMP property type"            },
    { GIMP_PDB_INT32,       "num_vals",  "number of values"             },
    { GIMP_PDB_STRINGARRAY, "vals",      "XMP property values"          },
  };

  static GimpParamDef set_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "schema",    "XMP schema prefix or URI"     },
    { GIMP_PDB_STRING,      "property",  "XMP property name"            },
    { GIMP_PDB_INT32,       "type",      "XMP property type"            },
    { GIMP_PDB_INT32,       "num_vals",  "number of values"             },
    { GIMP_PDB_STRINGARRAY, "vals",      "XMP property values"          },
  };

  static GimpParamDef get_simple_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "schema",    "XMP schema prefix or URI"     },
    { GIMP_PDB_STRING,      "property",  "XMP property name"            },
  };
  static GimpParamDef get_simple_return_vals[] =
  {
    { GIMP_PDB_STRING,      "value",     "XMP property value"           },
  };

  static GimpParamDef set_simple_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "schema",    "XMP schema prefix or URI"     },
    { GIMP_PDB_STRING,      "property",  "XMP property name"            },
    { GIMP_PDB_STRING,      "value",     "XMP property value"           },
  };

/* FIXME: uncomment when these are working
  static GimpParamDef delete_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "schema",    "XMP schema prefix or URI"     },
    { GIMP_PDB_STRING,      "property",  "XMP property name"            },
  };

  static GimpParamDef add_schema_args[] =
  {
    { GIMP_PDB_IMAGE,       "image",     "Input image"                  },
    { GIMP_PDB_STRING,      "prefix",    "XMP schema prefix"            },
    { GIMP_PDB_STRING,      "uri",       "XMP schema URI"               },
  };
*/

  gimp_install_procedure ("plug_in_metadata_editor",
			  "View and edit metadata (EXIF, IPTC, XMP)",
                          "View and edit metadata information attached to the "
                          "current image.  This can include EXIF, IPTC and/or "
                          "XMP information.  Some or all of this metadata "
                          "will be saved in the file, depending on the output "
                          "file format.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2004-2005",
                          N_("Propert_ies"),
                          "RGB*, INDEXED*, GRAY*",
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (editor_args), 0,
                          editor_args, NULL);
  gimp_plugin_menu_register ("plug_in_metadata_editor",
                             N_("<Image>/File/Info"));
  gimp_plugin_icon_register ("plug_in_metadata_editor",
                             GIMP_ICON_TYPE_STOCK_ID, GTK_STOCK_PROPERTIES);
  /* FIXME: The GNOME HIG recommends using the accel Alt+Return for this */

  gimp_install_procedure ("plug_in_metadata_decode_xmp",
			  "Decode an XMP packet",
                          "Parse an XMP packet and merge the results with "
                          "any metadata already attached to the image.  This "
                          "should be used when an XMP packet is read from an "
                          "image file.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (decode_xmp_args), 0,
                          decode_xmp_args, NULL);

  gimp_install_procedure ("plug_in_metadata_encode_xmp",
			  "Encode metadata into an XMP packet",
                          "Generate an XMP packet from the metadata "
                          "information attached to the image.  The new XMP "
                          "packet can then be saved into a file.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (encode_xmp_args),
                          G_N_ELEMENTS (encode_xmp_return_vals),
                          encode_xmp_args, encode_xmp_return_vals);

/* FIXME: uncomment when these are working
  gimp_install_procedure ("plug_in_metadata_decode_exif",
			  "Decode an EXIF block",
                          "Parse an EXIF block and merge the results with "
                          "any metadata already attached to the image.  This "
                          "should be used when an EXIF block is read from an "
                          "image file.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (decode_exif_args), 0,
                          decode_exif_args, NULL);

  gimp_install_procedure ("plug_in_metadata_encode_exif",
			  "Encode metadata into an EXIF block",
                          "Generate an EXIF block from the metadata "
                          "information attached to the image.  The new EXIF "
                          "block can then be saved into a file.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (encode_exif_args),
                          G_N_ELEMENTS (encode_exif_return_vals),
                          encode_exif_args, encode_exif_return_vals);
*/

  gimp_install_procedure ("plug_in_metadata_get",
			  "Retrieve the values of an XMP property",
                          "Retrieve the list of values associated with "
                          "an XMP property.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (get_args),
                          G_N_ELEMENTS (get_return_vals),
                          get_args, get_return_vals);

  gimp_install_procedure ("plug_in_metadata_set",
			  "Set the values of an XMP property",
                          "Set the list of values associated with "
                          "an XMP property.  If a property with the same "
                          "name already exists, it will be replaced.",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (set_args), 0,
                          set_args, NULL);

  gimp_install_procedure ("plug_in_metadata_get_simple",
			  "Retrieve the value of an XMP property",
                          "Retrieve value associated with a scalar XMP "
                          "property.  This can only be done for simple "
                          "property types such as text or integers.  "
                          "Structured types must be retrieved with "
                          "plug_in_metadata_get().",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (get_simple_args),
                          G_N_ELEMENTS (get_simple_return_vals),
                          get_simple_args, get_simple_return_vals);

  gimp_install_procedure ("plug_in_metadata_set_simple",
			  "Set the value of an XMP property",
                          "Set the value of a scalar XMP property.  This "
                          "can only be done for simple property types such "
                          "as text or integers.  Structured types need to "
                          "be set with plug_in_metadata_set().",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "Raphaël Quinet <raphael@gimp.org>",
			  "2005",
                          NULL,
                          NULL,
                          GIMP_PLUGIN,
                          G_N_ELEMENTS (set_simple_args), 0,
                          set_simple_args, NULL);


}

static void
run (const gchar      *name,
     gint              nparams,
     const GimpParam  *param,
     gint             *nreturn_vals,
     GimpParam       **return_vals)
{
  static GimpParam   values[4];
  gint32             image_ID;
  XMPModel          *xmp_model;
  GimpPDBStatusType  status = GIMP_PDB_SUCCESS;
  GimpParasite      *parasite = NULL;

  *nreturn_vals = 1;
  *return_vals  = values;

  values[0].type          = GIMP_PDB_STATUS;
  values[0].data.d_status = GIMP_PDB_EXECUTION_ERROR;

  INIT_I18N();

  if (! strcmp (name, "plug_in_metadata_editor"))
    image_ID = param[1].data.d_image;
  else
    image_ID = param[0].data.d_image;

  xmp_model = xmp_model_new ();

  /* if there is already a metadata parasite, load it */
  parasite = gimp_image_parasite_find (image_ID, METADATA_PARASITE);
  if (parasite)
    {
      GError *error = NULL;

      if (!! strncmp (gimp_parasite_data (parasite),
                      METADATA_MARKER, METADATA_MARKER_LEN)
          || ! xmp_model_parse_buffer (xmp_model,
                                       gimp_parasite_data (parasite)
                                       + METADATA_MARKER_LEN,
                                       gimp_parasite_data_size (parasite)
                                       - METADATA_MARKER_LEN,
                                       FALSE, &error))
        {
          g_warning ("Metadata parasite seems to be corrupt");
          /* continue anyway, we will attach a clean parasite later */
        }
      gimp_parasite_free (parasite);
    }

  /* If we have no metadata yet, try to find some XMP in the file (but
   * ignore errors if nothing is found).  FIXME: This is a workaround
   * until all file plug-ins do the right thing when loading their
   * files.
   */
  if (xmp_model_is_empty (xmp_model))
    {
      const gchar *filename;
      GError      *error = NULL;

      filename = gimp_image_get_filename (image_ID);
      if (filename != NULL)
        if (xmp_model_parse_file (xmp_model, filename, &error))
          /* g_message ("XMP loaded from file '%s'\n", filename) */;
    }

  /* Now check what we are supposed to do */
  if (! strcmp (name, "plug_in_metadata_editor"))
    {
      GimpRunMode run_mode;

      run_mode = param[0].data.d_int32;
      if (run_mode == GIMP_RUN_INTERACTIVE)
        {
          /* Hello, user! */
          if (! metadata_dialog (image_ID, xmp_model))
            status = GIMP_PDB_CANCEL;
        }
    }
  else if (! strcmp (name, "plug_in_metadata_decode_xmp"))
    {
      const gchar *buffer;
      GError      *error = NULL;

      buffer = param[1].data.d_string;
      if (! xmp_model_parse_buffer (xmp_model, buffer, strlen (buffer),
                                    FALSE, &error))
        status = GIMP_PDB_EXECUTION_ERROR;
    }
  else if (! strcmp (name, "plug_in_metadata_encode_xmp"))
    {
      /* done below together with the parasite */
    }
  else if (! strcmp (name, "plug_in_metadata_get"))
    {
      g_warning ("Not implemented yet\n"); /* FIXME */
    }
  else if (! strcmp (name, "plug_in_metadata_set"))
    {
      g_warning ("Not implemented yet\n"); /* FIXME */
    }
  else if (! strcmp (name, "plug_in_metadata_get_simple"))
    {
      const gchar *schema_name;
      const gchar *property_name;
      const gchar *value;

      schema_name = param[1].data.d_string;
      property_name = param[2].data.d_string;
      value = xmp_model_get_scalar_property (xmp_model, schema_name,
                                             property_name);
      if (value)
        {
          *nreturn_vals = 2;
          values[1].type = GIMP_PDB_STRING;
          values[1].data.d_string = g_strdup (value);
        }
      else
        status = GIMP_PDB_EXECUTION_ERROR;
    }
  else if (! strcmp (name, "plug_in_metadata_set_simple"))
    {
      const gchar *schema_name;
      const gchar *property_name;
      const gchar *property_value;

      schema_name = param[1].data.d_string;
      property_name = param[2].data.d_string;
      property_value = param[3].data.d_string;
      if (! xmp_model_set_scalar_property (xmp_model, schema_name,
                                           property_name, property_value))
        status = GIMP_PDB_EXECUTION_ERROR;
    }
  else
    {
      status = GIMP_PDB_CALLING_ERROR;
    }

  if (status == GIMP_PDB_SUCCESS)
    {
      gchar   *buffer;
      gssize   buffer_size;
      gssize   used_size;

      /* Generate the updated parasite and attach it to the image */
      buffer_size = xmp_estimate_size (xmp_model);
      buffer = g_new (gchar, buffer_size + METADATA_MARKER_LEN);
      strcpy (buffer, METADATA_MARKER);
      used_size = xmp_generate_block (xmp_model,
                                      buffer + METADATA_MARKER_LEN,
                                      buffer_size);
      parasite = gimp_parasite_new (METADATA_PARASITE,
                                    GIMP_PARASITE_PERSISTENT,
                                    used_size + METADATA_MARKER_LEN,
                                    (gpointer) buffer);
      gimp_image_parasite_attach (image_ID, parasite);
      if (! strcmp (name, "plug_in_metadata_encode_xmp"))
        {
          *nreturn_vals = 2;
          values[1].type = GIMP_PDB_STRING;
          values[1].data.d_string = g_strdup (buffer + METADATA_MARKER_LEN);
        }
      g_free (buffer);
      xmp_model_free (xmp_model);
    }

  values[0].data.d_status = status;
}