/*
 * This is a plug-in for the GIMP.
 *
 * Generates clickable image maps.
 *
 * Copyright (C) 1998-2005 Maurits Rijk  m.rijk@chello.nl
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
 *
 */

#include "config.h"

#include <gtk/gtk.h>

#include "imap_commands.h"
#include "imap_main.h"
#include "imap_object_popup.h"

#include "libgimp/stdplugins-intl.h"


/* Current object with popup menu */
static Object_t *_current_obj;

Object_t*
get_popup_object(void)
{
   return _current_obj;
}

extern GtkUIManager *ui_manager;

ObjectPopup_t*
make_object_popup(void)
{
   ObjectPopup_t *popup = g_new (ObjectPopup_t, 1);
   popup->menu = gtk_ui_manager_get_widget (ui_manager, "/ObjectPopupMenu");
   return popup;
}

GtkWidget*
object_popup_prepend_menu(ObjectPopup_t *popup, gchar *label,
			  MenuCallback activate, gpointer data)
{
   return prepend_item_with_label(popup->menu, label, activate, data);
}

void
object_handle_popup(ObjectPopup_t *popup, Object_t *obj, GdkEventButton *event)
{
  /* int position = object_get_position_in_list(obj) + 1; */

   _current_obj = popup->obj = obj;
#ifdef _TEMP_
   gtk_widget_set_sensitive(popup->up, (position > 1) ? TRUE : FALSE);
   gtk_widget_set_sensitive(popup->down,
			    (position < g_list_length(obj->list->list))
			    ? TRUE : FALSE);
#endif
   gtk_menu_popup(GTK_MENU(popup->menu), NULL, NULL, NULL, NULL,
		  event->button, event->time);
}

void
object_do_popup(Object_t *obj, GdkEventButton *event)
{
   static ObjectPopup_t *popup;

   if (!popup)
      popup = make_object_popup();
   object_handle_popup(popup, obj, event);
}
