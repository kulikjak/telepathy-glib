/*
 * A factory for TpContacts and plain subclasses of TpProxy
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

#ifndef __TP_SIMPLE_CLIENT_FACTORY_H__
#define __TP_SIMPLE_CLIENT_FACTORY_H__

#include <telepathy-glib/account.h>
#include <telepathy-glib/channel.h>
#include <telepathy-glib/channel-dispatch-operation.h>
#include <telepathy-glib/channel-request.h>
#include <telepathy-glib/connection.h>
#include <telepathy-glib/contact.h>
#include <telepathy-glib/dbus-daemon.h>

G_BEGIN_DECLS

/* TpSimpleClientFactory is typedef'd in proxy.h */
typedef struct _TpSimpleClientFactoryPrivate TpSimpleClientFactoryPrivate;
typedef struct _TpSimpleClientFactoryClass TpSimpleClientFactoryClass;

struct _TpSimpleClientFactoryClass {
    /*<public>*/
    GObjectClass parent_class;

    /* TpAccount */
    TpAccount * (*create_account) (TpSimpleClientFactory *self,
        const gchar *object_path,
        const GHashTable *immutable_properties,
        GError **error);
    GArray * (*dup_account_features) (TpSimpleClientFactory *self,
        TpAccount *account);

    /* TpConnection */
    TpConnection * (*create_connection) (TpSimpleClientFactory *self,
        const gchar *object_path,
        const GHashTable *immutable_properties,
        GError **error);
    GArray * (*dup_connection_features) (TpSimpleClientFactory *self,
        TpConnection *connection);

    /* TpChannel */
    TpChannel * (*create_channel) (TpSimpleClientFactory *self,
        TpConnection *conn,
        const gchar *object_path,
        const GHashTable *immutable_properties,
        GError **error);
    GArray * (*dup_channel_features) (TpSimpleClientFactory *self,
        TpChannel *channel);

    /* TpContact */
    TpContact * (*create_contact) (TpSimpleClientFactory *self,
        TpConnection *connection,
        TpHandle handle,
        const gchar *identifier);
    GArray * (*dup_contact_features) (TpSimpleClientFactory *self,
        TpConnection *connection);

    /*<private>*/
    GCallback padding[20];
};

struct _TpSimpleClientFactory {
    /*<private>*/
    GObject parent;
    TpSimpleClientFactoryPrivate *priv;
};

GType tp_simple_client_factory_get_type (void);

#define TP_TYPE_SIMPLE_CLIENT_FACTORY \
  (tp_simple_client_factory_get_type ())
#define TP_SIMPLE_CLIENT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), TP_TYPE_SIMPLE_CLIENT_FACTORY, \
                               TpSimpleClientFactory))
#define TP_SIMPLE_CLIENT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), TP_TYPE_SIMPLE_CLIENT_FACTORY, \
                            TpSimpleClientFactoryClass))
#define TP_IS_SIMPLE_CLIENT_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TP_TYPE_SIMPLE_CLIENT_FACTORY))
#define TP_IS_SIMPLE_CLIENT_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), TP_TYPE_SIMPLE_CLIENT_FACTORY))
#define TP_SIMPLE_CLIENT_FACTORY_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), TP_TYPE_SIMPLE_CLIENT_FACTORY, \
                              TpSimpleClientFactoryClass))

TpSimpleClientFactory * tp_simple_client_factory_new (TpDBusDaemon *dbus);

TpDBusDaemon *tp_simple_client_factory_get_dbus_daemon (
    TpSimpleClientFactory *self);

/* TpAccount */
TpAccount *tp_simple_client_factory_ensure_account (TpSimpleClientFactory *self,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error);
GArray *tp_simple_client_factory_dup_account_features (
    TpSimpleClientFactory *self,
    TpAccount *account);
void tp_simple_client_factory_add_account_features (TpSimpleClientFactory *self,
    const GQuark *features);
void tp_simple_client_factory_add_account_features_varargs (
    TpSimpleClientFactory *self,
    GQuark feature,
    ...);

/* TpConnection */
TpConnection *tp_simple_client_factory_ensure_connection (
    TpSimpleClientFactory *self,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error);
GArray *tp_simple_client_factory_dup_connection_features (
    TpSimpleClientFactory *self,
    TpConnection *connection);
void tp_simple_client_factory_add_connection_features (
    TpSimpleClientFactory *self,
    const GQuark *features);
void tp_simple_client_factory_add_connection_features_varargs (
    TpSimpleClientFactory *self,
    GQuark feature,
    ...);

/* TpChannel */
TpChannel *tp_simple_client_factory_ensure_channel (TpSimpleClientFactory *self,
    TpConnection *connection,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error);
GArray *tp_simple_client_factory_dup_channel_features (
    TpSimpleClientFactory *self,
    TpChannel *channel);
void tp_simple_client_factory_add_channel_features (TpSimpleClientFactory *self,
    const GQuark *features);
void tp_simple_client_factory_add_channel_features_varargs (
    TpSimpleClientFactory *self,
    GQuark feature,
    ...);

/* TpContact */
TpContact *tp_simple_client_factory_ensure_contact (TpSimpleClientFactory *self,
    TpConnection *connection,
    TpHandle handle,
    const gchar *identifier);
GArray *tp_simple_client_factory_dup_contact_features (
    TpSimpleClientFactory *self,
    TpConnection *connection);
void tp_simple_client_factory_add_contact_features (TpSimpleClientFactory *self,
    const GQuark *features);
void tp_simple_client_factory_add_contact_features_varargs (
    TpSimpleClientFactory *self,
    GQuark feature,
    ...);

G_END_DECLS

#endif
