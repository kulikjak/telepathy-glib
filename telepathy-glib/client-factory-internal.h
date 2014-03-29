/*<private_header>*/
/*
 * Internal methods of TpClientFactory
 *
 * Copyright © 2011 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef __TP_CLIENT_FACTORY_INTERNAL_H__
#define __TP_CLIENT_FACTORY_INTERNAL_H__

#include <telepathy-glib/client-factory.h>

G_BEGIN_DECLS

void _tp_client_factory_insert_proxy (TpClientFactory *self,
    gpointer proxy);

TpChannelRequest *_tp_client_factory_ensure_channel_request (
    TpClientFactory *self,
    const gchar *object_path,
    GHashTable *immutable_properties,
    GError **error);

TpChannelDispatchOperation *
_tp_client_factory_ensure_channel_dispatch_operation (TpClientFactory *self,
    const gchar *object_path,
    GHashTable *immutable_properties,
    GError **error);

TpAccount *_tp_account_new_with_factory (TpClientFactory *factory,
    TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GError **error);

TpConnection *_tp_connection_new_with_factory (TpClientFactory *factory,
    TpDBusDaemon *dbus,
    const gchar *bus_name,
    const gchar *object_path,
    GError **error);

TpChannel *_tp_channel_new_with_factory (TpClientFactory *factory,
    TpConnection *conn,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error);

TpChannelRequest *_tp_channel_request_new_with_factory (
    TpClientFactory *factory,
    TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GVariant *immutable_properties,
    GError **error);

TpChannelDispatchOperation *_tp_channel_dispatch_operation_new_with_factory (
    TpClientFactory *factory,
    TpDBusDaemon *bus_daemon,
    const gchar *object_path,
    GHashTable *immutable_properties,
    GError **error);

TpTLSCertificate *_tp_tls_certificate_new (TpProxy *conn_or_chan,
    const gchar *object_path,
    GError **error);

TpAccountManager *_tp_account_manager_new (TpClientFactory *factory);

TpChannelDispatcher *_tp_channel_dispatcher_new (TpClientFactory *factory);

TpDebugClient *_tp_debug_client_new (TpClientFactory *factory,
    const gchar *unique_name,
    GError **error);

G_END_DECLS

#endif
