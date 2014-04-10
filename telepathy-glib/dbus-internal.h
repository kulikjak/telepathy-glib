/*<private_header>*/
/*
 * internal-dbus-glib.h - private header for dbus-glib glue
 *
 * Copyright (C) 2007 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007 Nokia Corporation
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __TP_INTERNAL_DBUS_GLIB_H__
#define __TP_INTERNAL_DBUS_GLIB_H__

G_BEGIN_DECLS

gboolean _tp_dbus_connection_get_name_owner (GDBusConnection *dbus_connection,
    gint timeout_ms, const gchar *well_known_name, gchar **unique_name,
    GError **error);

GDBusConnection *_tp_dbus_object_get_connection (gpointer object);
const gchar *_tp_dbus_object_get_object_path (gpointer object);

GStrv _tp_g_dbus_object_dup_interface_names (GDBusObject *obj,
    const gchar *skip_class,
    const gchar *skip_type);

G_END_DECLS

#endif /* __TP_INTERNAL_DBUS_GLIB_H__ */
