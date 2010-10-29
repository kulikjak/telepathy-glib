/*
 * text-channel.h - high level API for Text channels
 *
 * Copyright (C) 2010 Collabora Ltd. <http://www.collabora.co.uk/>
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

/**
 * SECTION:text-channel
 * @title: TpTextChannel
 * @short_description: proxy object for a text channel
 *
 * #TpTextChannel is a sub-class of #TpChannel providing convenient API
 * to send and receive #TpMessage.
 */

/**
 * TpTextChannel:
 *
 * Data structure representing a #TpTextChannel.
 *
 * Since: 0.13.UNRELEASED
 */

/**
 * TpTextChannelClass:
 *
 * The class of a #TpTextChannel.
 *
 * Since: 0.13.UNRELEASED
 */

#include <config.h>

#include "telepathy-glib/text-channel.h"

#include <telepathy-glib/contact.h>
#include <telepathy-glib/dbus.h>
#include <telepathy-glib/enums.h>
#include <telepathy-glib/gnio-util.h>
#include <telepathy-glib/gtypes.h>
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/message-internal.h>
#include <telepathy-glib/proxy-internal.h>
#include <telepathy-glib/proxy-subclass.h>
#include <telepathy-glib/signalled-message-internal.h>
#include <telepathy-glib/util-internal.h>
#include <telepathy-glib/util.h>

#define DEBUG_FLAG TP_DEBUG_CHANNEL
#include "telepathy-glib/debug-internal.h"

#include "_gen/signals-marshal.h"

#include <stdio.h>
#include <glib/gstdio.h>

G_DEFINE_TYPE (TpTextChannel, tp_text_channel, TP_TYPE_CHANNEL)

struct _TpTextChannelPrivate
{
  GStrv supported_content_types;
  TpMessagePartSupportFlags message_part_support_flags;
  TpDeliveryReportingSupportFlags delivery_reporting_support;

  /* list of owned TpSignalledMessage */
  GList *pending_messages;
  gboolean retrieving_pending;
};

enum
{
  PROP_SUPPORTED_CONTENT_TYPES = 1,
  PROP_MESSAGE_PART_SUPPORT_FLAGS,
  PROP_DELIVERY_REPORTING_SUPPORT
};

enum /* signals */
{
  SIG_MESSAGE_RECEIVED,
  SIG_PENDING_MESSAGES_REMOVED,
  SIG_MESSAGE_SENT,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0, };

static void
tp_text_channel_dispose (GObject *obj)
{
  TpTextChannel *self = (TpTextChannel *) obj;

  tp_clear_pointer (&self->priv->supported_content_types, g_strfreev);

  g_list_foreach (self->priv->pending_messages, (GFunc) g_object_unref, NULL);
  tp_clear_pointer (&self->priv->pending_messages, g_list_free);

  G_OBJECT_CLASS (tp_text_channel_parent_class)->dispose (obj);
}

static void
tp_text_channel_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  TpTextChannel *self = (TpTextChannel *) object;

  switch (property_id)
    {
      case PROP_SUPPORTED_CONTENT_TYPES:
        g_value_set_boxed (value,
            tp_text_channel_get_supported_content_types (self));
        break;

      case PROP_MESSAGE_PART_SUPPORT_FLAGS:
        g_value_set_uint (value,
            tp_text_channel_get_message_part_support_flags (self));
        break;

      case PROP_DELIVERY_REPORTING_SUPPORT:
        g_value_set_uint (value,
            tp_text_channel_get_delivery_reporting_support (self));
        break;

      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
        break;
    }
}

static void
message_sent_cb (TpChannel *channel,
    const GPtrArray *content,
    guint flags,
    const gchar *token,
    gpointer user_data,
    GObject *weak_object)
{
  TpMessage *msg;

  msg = _tp_signalled_message_new (content);

  g_signal_emit (channel, signals[SIG_MESSAGE_SENT], 0, msg, flags,
      tp_str_empty (token) ? NULL : token);

  g_object_unref (msg);
}

static void
tp_text_channel_constructed (GObject *obj)
{
  TpTextChannel *self = (TpTextChannel *) obj;
  void (*chain_up) (GObject *) =
    ((GObjectClass *) tp_text_channel_parent_class)->constructed;
  TpChannel *chan = (TpChannel *) obj;
  GHashTable *props;
  gboolean valid;
  GError *err = NULL;

  if (chain_up != NULL)
    chain_up (obj);

  if (tp_channel_get_channel_type_id (chan) !=
      TP_IFACE_QUARK_CHANNEL_TYPE_TEXT)
    {
      GError error = { TP_DBUS_ERRORS, TP_DBUS_ERROR_INCONSISTENT,
          "Channel is not of type Text" };

      DEBUG ("Channel is not of type Text: %s", tp_channel_get_channel_type (
            chan));

      tp_proxy_invalidate (TP_PROXY (self), &error);
      return;
    }

  if (!tp_proxy_has_interface_by_id (self,
        TP_IFACE_QUARK_CHANNEL_INTERFACE_MESSAGES))
    {
      GError error = { TP_DBUS_ERRORS, TP_DBUS_ERROR_INCONSISTENT,
          "Channel does not implement the Messages interface" };

      DEBUG ("Channel does not implement the Messages interface");

      tp_proxy_invalidate (TP_PROXY (self), &error);
      return;

    }

  props = tp_channel_borrow_immutable_properties (TP_CHANNEL (self));

  self->priv->supported_content_types = (GStrv) tp_asv_get_strv (props,
      TP_PROP_CHANNEL_INTERFACE_MESSAGES_SUPPORTED_CONTENT_TYPES);
  if (self->priv->supported_content_types == NULL)
    {
      DEBUG ("Channel doesn't have Messages.SupportedContentTypes in its "
          "immutable properties");
    }
  else
    {
      self->priv->supported_content_types = g_strdupv (
          self->priv->supported_content_types);
    }

  self->priv->message_part_support_flags = tp_asv_get_uint32 (props,
      TP_PROP_CHANNEL_INTERFACE_MESSAGES_MESSAGE_PART_SUPPORT_FLAGS, &valid);
  if (!valid)
    {
      DEBUG ("Channel doesn't have Messages.MessagePartSupportFlags in its "
          "immutable properties");
    }

  self->priv->delivery_reporting_support = tp_asv_get_uint32 (props,
      TP_PROP_CHANNEL_INTERFACE_MESSAGES_DELIVERY_REPORTING_SUPPORT, &valid);
  if (!valid)
    {
      DEBUG ("Channel doesn't have Messages.DeliveryReportingSupport in its "
          "immutable properties");
    }

  tp_cli_channel_interface_messages_connect_to_message_sent (chan,
      message_sent_cb, NULL, NULL, NULL, &err);
  if (err != NULL)
    {
      DEBUG ("Failed to connect to MessageSent: %s", err->message);
      g_error_free (err);
    }
}

static void
add_message_received (TpTextChannel *self,
    TpMessage *msg)
{
  self->priv->pending_messages = g_list_append (
      self->priv->pending_messages, msg);

  g_signal_emit (self, signals[SIG_MESSAGE_RECEIVED], 0, msg);
}

static void
got_sender_contact_cb (TpConnection *connection,
    guint n_contacts,
    TpContact * const *contacts,
    guint n_failed,
    const TpHandle *failed,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpTextChannel *self = (TpTextChannel *) weak_object;
  TpMessage *msg = user_data;
  TpContact *contact;

  if (error != NULL)
    {
      DEBUG ("Failed to prepare TpContact: %s", error->message);
      goto out;
    }

  if (n_failed > 0)
    {
      DEBUG ("Failed to prepare TpContact (InvalidHandle)");
      goto out;
    }

  contact = contacts[0];

  _tp_signalled_message_set_sender (msg, contact);

out:
  add_message_received (self, msg);
}

static void
message_received_cb (TpChannel *proxy,
    const GPtrArray *message,
    gpointer user_data,
    GObject *weak_object)
{
  TpTextChannel *self = user_data;
  TpMessage *msg;
  const GHashTable *header;
  TpHandle sender;
  TpConnection *conn;

  /* If we are still retrieving pending messages, no need to add the message,
   * it will be in the initial set of messages retrieved. */
  if (self->priv->retrieving_pending)
    return;

  DEBUG ("New message received");

  msg = _tp_signalled_message_new (message);

  header = tp_message_peek (msg, 0);
  sender = tp_asv_get_uint32 (header, "message-sender", NULL);

  if (sender == 0)
    {
      DEBUG ("Message doesn't have a sender");

      add_message_received (self, msg);
      return;
    }

  conn = tp_channel_borrow_connection (proxy);

  tp_connection_get_contacts_by_handle (conn, 1, &sender,
      0, NULL, got_sender_contact_cb, msg, NULL, G_OBJECT (self));
}

/* Move this as TpMessage (or TpSignalledMessage?) API ? */
static guint
get_pending_message_id (TpMessage *msg,
    gboolean *valid)
{
  const GHashTable *part0;

  part0 = tp_message_peek (msg, 0);
  if (part0 == NULL)
    {
      *valid = FALSE;
      return 0;
    }

  return tp_asv_get_uint32 (part0, "pending-message-id", valid);
}

static void
pending_messages_removed_cb (TpChannel *proxy,
    const GArray *ids,
    gpointer user_data,
    GObject *weak_object)
{
  TpTextChannel *self = (TpTextChannel *) proxy;
  GList *l;
  guint i;

  for (i = 0; i < ids->len; i++)
    {
      guint id = g_array_index (ids, guint, i);

      for (l = self->priv->pending_messages; l != NULL; l = g_list_next (l))
        {
          TpMessage *msg = l->data;
          guint msg_id;
          gboolean valid;

          msg_id = get_pending_message_id (msg, &valid);
          if (!valid)
            continue;

          if (msg_id == id)
            {
              self->priv->pending_messages = g_list_delete_link (
                  self->priv->pending_messages, l);

              g_signal_emit (self, signals[SIG_PENDING_MESSAGES_REMOVED],
                  0, msg);

              g_object_unref (msg);
            }
        }
    }
}

static void
get_pending_messages_cb (TpProxy *proxy,
    const GValue *value,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  TpTextChannel *self = user_data;
  guint i;
  GPtrArray *messages;

  self->priv->retrieving_pending = FALSE;

  if (error != NULL)
    {
      DEBUG ("Failed to get PendingMessages property: %s", error->message);

      _tp_proxy_set_feature_prepared (proxy,
          TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES, FALSE);
      return;
    }

  messages = g_value_get_boxed (value);
  for (i = 0; i < messages->len; i++)
    {
      GPtrArray *parts = g_ptr_array_index (messages, i);
      TpMessage *msg;

      msg = _tp_signalled_message_new (parts);

      self->priv->pending_messages = g_list_append (
          self->priv->pending_messages, msg);
    }

  _tp_proxy_set_feature_prepared (proxy,
      TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES, TRUE);
}

static void
tp_text_channel_prepare_pending_messages (TpProxy *proxy)
{
  TpTextChannel *self = (TpTextChannel *) proxy;
  TpChannel *channel = (TpChannel *) proxy;
  GError *error = NULL;

  tp_cli_channel_interface_messages_connect_to_message_received (channel,
      message_received_cb, proxy, NULL, G_OBJECT (proxy), &error);
  if (error != NULL)
    {
      DEBUG ("Failed to connect to MessageReceived signal: %s", error->message);
      goto fail;
    }

  tp_cli_channel_interface_messages_connect_to_pending_messages_removed (
      channel, pending_messages_removed_cb, proxy, NULL, G_OBJECT (proxy),
      &error);
  if (error != NULL)
    {
      DEBUG ("Failed to connect to PendingMessagesRemoved signal: %s",
          error->message);
      goto fail;
    }

  self->priv->retrieving_pending = TRUE;

  tp_cli_dbus_properties_call_get (proxy, -1,
      TP_IFACE_CHANNEL_INTERFACE_MESSAGES, "PendingMessages",
      get_pending_messages_cb, proxy, NULL, G_OBJECT (proxy));

  return;

fail:
  g_error_free (error);

  _tp_proxy_set_feature_prepared (proxy,
      TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES, TRUE);
}

enum {
    FEAT_PENDING_MESSAGES,
    N_FEAT
};

static const TpProxyFeature *
tp_text_channel_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
  static TpProxyFeature features[N_FEAT + 1] = { { 0 } };

  if (G_LIKELY (features[0].name != 0))
    return features;

  features[FEAT_PENDING_MESSAGES].name =
    TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES;
  features[FEAT_PENDING_MESSAGES].start_preparing =
    tp_text_channel_prepare_pending_messages;

  /* assert that the terminator at the end is there */
  g_assert (features[N_FEAT].name == 0);

  return features;
}

static void
tp_text_channel_class_init (TpTextChannelClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GParamSpec *param_spec;

  gobject_class->constructed = tp_text_channel_constructed;
  gobject_class->get_property = tp_text_channel_get_property;
  gobject_class->dispose = tp_text_channel_dispose;

  proxy_class->list_features = tp_text_channel_list_features;

  /**
   * TpTextChannel:supported-content-types:
   *
   * A #GStrv containing the MIME types supported by this channel, with more
   * preferred MIME types appearing earlier in the array.
   *
   * Since: 0.13.UNRELEASED
   */
  param_spec = g_param_spec_boxed ("supported-content-types",
      "SupportedContentTypes",
      "The Messages.SupportedContentTypes property of the channel",
      G_TYPE_STRV,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_SUPPORTED_CONTENT_TYPES,
      param_spec);

  /**
   * TpTextChannel:message-part-support-flags:
   *
   * A #TpMessagePartSupportFlags indicating the level of support for
   * message parts on this channel.
   *
   * Since: 0.13.UNRELEASED
   */
  param_spec = g_param_spec_uint ("message-part-support-flags",
      "MessagePartSupportFlags",
      "The Messages.MessagePartSupportFlags property of the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class,
      PROP_MESSAGE_PART_SUPPORT_FLAGS, param_spec);

  /**
   * TpTextChannel:delivery-reporting-support:
   *
   * A #TpDeliveryReportingSupportFlags indicating features supported
   * by this channel.
   *
   * Since: 0.13.UNRELEASED
   */
  param_spec = g_param_spec_uint ("delivery-reporting-support",
      "DeliveryReportingSupport",
      "The Messages.DeliveryReportingSupport property of the channel",
      0, G_MAXUINT32, 0,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class,
      PROP_DELIVERY_REPORTING_SUPPORT, param_spec);

  /**
   * TpTextChannel::message-received
   * @self: the #TpTextChannel
   * @message: a #TpSignalledMessage
   *
   * The ::message-received signal is emitted when a new message has been
   * received on @self.
   *
   * Note that this signal is only fired once the
   * #TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES has been prepared.
   *
   * Since: 0.13.UNRELEASED
   */
  signals[SIG_MESSAGE_RECEIVED] = g_signal_new ("message-received",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, TP_TYPE_SIGNALLED_MESSAGE);

  /**
   * TpTextChannel::pending-message-removed
   * @self: the #TpTextChannel
   * @message: a #TpSignalledMessage
   *
   * The ::pending-message-removed signal is emitted when @message
   * has been acked and so removed from the pending messages list.
   *
   * Note that this signal is only fired once the
   * #TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES has been prepared.
   *
   * Since: 0.13.UNRELEASED
   */
  signals[SIG_PENDING_MESSAGES_REMOVED] = g_signal_new (
      "pending-message-removed",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, TP_TYPE_SIGNALLED_MESSAGE);

  /**
   * TpTextChannel::message-sent
   * @self: the #TpTextChannel
   * @message: a #TpSignalledMessage
   * @flags: the #TpMessageSendingFlags affecting how the message was sent
   * @token: an opaque token used to match any incoming delivery or failure
   * reports against this message, or %NULL if the message is not
   * readily identifiable.
   *
   * The ::message-sent signal is emitted when @message
   * has been submitted for sending.
   *
   * Since: 0.13.UNRELEASED
   */
  signals[SIG_MESSAGE_SENT] = g_signal_new (
      "message-sent",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST,
      0, NULL, NULL,
      _tp_marshal_VOID__OBJECT_UINT_STRING,
      G_TYPE_NONE,
      3, TP_TYPE_SIGNALLED_MESSAGE, G_TYPE_UINT, G_TYPE_STRING);

  g_type_class_add_private (gobject_class, sizeof (TpTextChannelPrivate));
}

static void
tp_text_channel_init (TpTextChannel *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE ((self), TP_TYPE_TEXT_CHANNEL,
      TpTextChannelPrivate);
}


/**
 * tp_text_channel_new:
 * @conn: a #TpConnection; may not be %NULL
 * @object_path: the object path of the channel; may not be %NULL
 * @immutable_properties: (transfer none) (element-type utf8 GObject.Value):
 *  the immutable properties of the channel,
 *  as signalled by the NewChannel D-Bus signal or returned by the
 *  CreateChannel and EnsureChannel D-Bus methods: a mapping from
 *  strings (D-Bus interface name + "." + property name) to #GValue instances
 * @error: used to indicate the error if %NULL is returned
 *
 * Convenient function to create a new #TpTextChannel
 *
 * Returns: (transfer full): a newly created #TpTextChannel
 *
 * Since: 0.13.UNRELEASED
 */
TpTextChannel *
tp_text_channel_new (TpConnection *conn,
    const gchar *object_path,
    const GHashTable *immutable_properties,
    GError **error)
{
  TpProxy *conn_proxy = (TpProxy *) conn;

  g_return_val_if_fail (TP_IS_CONNECTION (conn), NULL);
  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (immutable_properties != NULL, NULL);

  if (!tp_dbus_check_valid_object_path (object_path, error))
    return NULL;

  return g_object_new (TP_TYPE_TEXT_CHANNEL,
      "connection", conn,
       "dbus-daemon", conn_proxy->dbus_daemon,
       "bus-name", conn_proxy->bus_name,
       "object-path", object_path,
       "handle-type", (guint) TP_UNKNOWN_HANDLE_TYPE,
       "channel-properties", immutable_properties,
       NULL);
}

/**
 * tp_text_channel_get_supported_content_types: (skip)
 * @self: a #TpTextChannel
 *
 * Return the #TpTextChannel:supported-content-types property
 *
 * Returns: (transfer none) :
 * the value of #TpTextChannel:supported-content-types
 *
 * Since: 0.13.UNRELEASED
 */
GStrv
tp_text_channel_get_supported_content_types (TpTextChannel *self)
{
  return self->priv->supported_content_types;
}

/**
 * tp_text_channel_get_message_part_support_flags: (skip)
 * @self: a #TpTextChannel
 *
 * Return the #TpTextChannel:message-part-support-flags property
 *
 * Returns: the value of #TpTextChannel:message-part-support-flags
 *
 * Since: 0.13.UNRELEASED
 */
TpMessagePartSupportFlags
tp_text_channel_get_message_part_support_flags (
    TpTextChannel *self)
{
  return self->priv->message_part_support_flags;
}

/**
 * tp_text_channel_get_delivery_reporting_support: (skip)
 * @self: a #TpTextChannel
 *
 * Return the #TpTextChannel:delivery-reporting-support property
 *
 * Returns: the value of #TpTextChannel:delivery-reporting-support property
 *
 * Since: 0.13.UNRELEASED
 */
TpDeliveryReportingSupportFlags
tp_text_channel_get_delivery_reporting_support (
    TpTextChannel *self)
{
  return self->priv->delivery_reporting_support;
}

/**
 * TP_TEXT_CHANNEL_FEATURE_PENDING_MESSAGES:
 *
 * Expands to a call to a function that returns a quark representing the
 * Pending Messages features of a #TpTextChannel.
 *
 * When this feature is prepared, the initial value of the
 * #TpTextChannel:pending-messages property has been fetched
 * and change notification has been set up.
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.13.UNRELEASED
 */
GQuark
tp_text_channel_get_feature_quark_pending_messages (void)
{
  return g_quark_from_static_string (
      "tp-text-channel-feature-pending-messages");
}

/**
 * tp_text_channel_get_pending_messages:
 * @self: a #TpTextChannel
 *
 * Return the a newly allocated list of not acked #TpSignalledMessage.
 *
 * Returns: (transfer container): a #GList of borrowed #TpSignalledMessage
 *
 * Since: 0.13.UNRELEASED
 */
GList *
tp_text_channel_get_pending_messages (TpTextChannel *self)
{
  return g_list_copy (self->priv->pending_messages);
}

static void
send_message_cb (TpChannel *proxy,
    const gchar *token,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("Failed to send message: %s", error->message);

      g_simple_async_result_set_from_error (result, error);
    }

  g_simple_async_result_set_op_res_gpointer (result, g_strdup (token),
      g_free);

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_text_channel_send_message_async:
 * @self: a #TpTextChannel
 * @message: a #TpClientMessage
 * @flags: flags affecting how the message is sent
 * @callback: a callback to call when the message has been submitted to the
 * server
 * @user_data: data to pass to @callback
 *
 * Submit a message to the server for sending. Once the message has been
 * submitted to the sever, @callback will be called. You can then call
 * tp_text_channel_send_message_finish() to get the result of the operation.
 *
 * Since: 0.13.UNRELEASED
 */
void
tp_text_channel_send_message_async (TpTextChannel *self,
    TpMessage *message,
    TpMessageSendingFlags flags,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  g_return_if_fail (TP_IS_TEXT_CHANNEL (self));
  g_return_if_fail (TP_IS_CLIENT_MESSAGE (message));

  result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, tp_text_channel_send_message_async);

  tp_cli_channel_interface_messages_call_send_message (TP_CHANNEL (self),
    -1, message->parts, flags, send_message_cb, result, NULL, NULL);
}

/**
 * tp_text_channel_send_message_finish:
 * @self: a #TpTextChannel
 * @result: a #GAsyncResult
 * @token: (out) (transfer full): if not %NULL, used to return the
 * token of the sent message
 * @error: a #GError to fill
 *
 * Finishes to send a message.
 *
 * @token can be used to match any incoming delivery or failure reports
 * against the sent message. If the returned token is %NULL the
 * message is not readily identifiable.
 *
 * Returns: %TRUE if the message has been submitted to the server, %FALSE
 * otherwise.
 *
 * Since: 0.13.UNRELEASED
 */
gboolean
tp_text_channel_send_message_finish (TpTextChannel *self,
    GAsyncResult *result,
    gchar **token,
    GError **error)
{
  _tp_implement_finish_copy_pointer (self, tp_text_channel_send_message_async,
      g_strdup, token);
}

static void
acknowledge_pending_messages_cb (TpChannel *channel,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("Failed to ack messages: %s", error->message);

      g_simple_async_result_set_from_error (result, error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_text_channel_ack_messages_async:
 * @self: a #TpTextChannel
 * @messages: a #Glist of #TpSignalledMessage
 * @callback: a callback to call when the message have been acked
 * @user_data: data to pass to @callback
 *
 * Ack all the messages in @messages.
 * Once the messages have been acked, @callback will be called.
 * You can then call tp_text_channel_ack_messages_finish() to get the
 * result of the operation.
 *
 * Since: 0.13.UNRELEASED
 */
void
tp_text_channel_ack_messages_async (TpTextChannel *self,
    const GList *messages,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpChannel *chan = (TpChannel *) self;
  GArray *ids;
  GList *l;
  GSimpleAsyncResult *result;

  g_return_if_fail (TP_IS_TEXT_CHANNEL (self));

  result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, tp_text_channel_ack_messages_async);

  if (messages == NULL)
    {
      /* Nothing to ack, succeed immediately */
      g_simple_async_result_complete_in_idle (result);

      g_object_unref (result);
      return;
    }

  ids = g_array_sized_new (FALSE, FALSE, sizeof (guint),
      g_list_length ((GList *) messages));

  for (l = (GList *) messages; l != NULL; l = g_list_next (l))
    {
      TpMessage *msg = l->data;
      guint id;
      gboolean valid;

      g_return_if_fail (TP_IS_SIGNALLED_MESSAGE (msg));

      id = get_pending_message_id (msg, &valid);
      if (!valid)
        {
          DEBUG ("Message doesn't have pending-message-id ?!");
          continue;
        }

      g_array_append_val (ids, id);
    }

  tp_cli_channel_type_text_call_acknowledge_pending_messages (chan, -1, ids,
      acknowledge_pending_messages_cb, result, NULL, G_OBJECT (self));

  g_array_free (ids, TRUE);
}

/**
 * tp_text_channel_ack_messages_finish:
 * @self: a #TpTextChannel
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes to ack a list of messages.
 *
 * Returns: %TRUE if the messages have been acked, %FALSE otherwise.
 *
 * Since: 0.13.UNRELEASED
 */
gboolean
tp_text_channel_ack_messages_finish (TpTextChannel *self,
    GAsyncResult *result,
    GError **error)
{
  _tp_implement_finish_void (self, tp_text_channel_ack_messages_async)
}

/**
 * tp_text_channel_ack_message_async:
 * @self: a #TpTextChannel
 * @message: a #TpSignalledMessage
 * @callback: a callback to call when the message have been acked
 * @user_data: data to pass to @callback
 *
 * Ack @message. Once the message has been acked, @callback will be called.
 * You can then call tp_text_channel_ack_message_finish() to get the
 * result of the operation.
 *
 * Since: 0.13.UNRELEASED
 */
void
tp_text_channel_ack_message_async (TpTextChannel *self,
    TpMessage *message,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpChannel *chan = (TpChannel *) self;
  GSimpleAsyncResult *result;
  GArray *ids;
  guint id;
  gboolean valid;

  g_return_if_fail (TP_IS_TEXT_CHANNEL (self));
  g_return_if_fail (TP_IS_SIGNALLED_MESSAGE (message));

  id = get_pending_message_id (message, &valid);
  if (!valid)
    {
      g_simple_async_report_error_in_idle (G_OBJECT (self), callback, user_data,
          TP_ERRORS, TP_ERROR_INVALID_ARGUMENT,
          "Message doesn't have a pending-message-id");

      return;
    }

  result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, tp_text_channel_ack_message_async);

  ids = g_array_sized_new (FALSE, FALSE, sizeof (guint), 1);
  g_array_append_val (ids, id);

  tp_cli_channel_type_text_call_acknowledge_pending_messages (chan, -1, ids,
      acknowledge_pending_messages_cb, result, NULL, G_OBJECT (self));

  g_array_free (ids, TRUE);
}

/**
 * tp_text_channel_ack_message_finish:
 * @self: a #TpTextChannel
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes to ack a message.
 *
 * Returns: %TRUE if the message has been acked, %FALSE otherwise.
 *
 * Since: 0.13.UNRELEASED
 */
gboolean
tp_text_channel_ack_message_finish (TpTextChannel *self,
    GAsyncResult *result,
    GError **error)
{
  _tp_implement_finish_void (self, tp_text_channel_ack_message_async)
}

static void
set_chat_state_cb (TpChannel *proxy,
      const GError *error,
      gpointer user_data,
      GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      DEBUG ("SetChatState failed: %s", error->message);

      g_simple_async_result_set_from_error (result, error);
    }

  g_simple_async_result_complete (result);
  g_object_unref (result);
}

/**
 * tp_text_channel_set_chat_state_async:
 * @self: a #TpTextChannel
 * @state: a #TpChannelChatState to set
 * @callback: a callback to call when the chat state has been set
 * @user_data: data to pass to @callback
 *
 * Set the local state on channel @self to @state.
 * Once the state has been set, @callback will be called.
 * You can then call tp_text_channel_set_chat_state_finish() to get the
 * result of the operation.
 *
 * Since: 0.13.UNRELEASED
 */
void
tp_text_channel_set_chat_state_async (TpTextChannel *self,
    TpChannelChatState state,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;

  result = g_simple_async_result_new (G_OBJECT (self), callback,
      user_data, tp_text_channel_set_chat_state_async);

  tp_cli_channel_interface_chat_state_call_set_chat_state (TP_CHANNEL (self),
      -1, state, set_chat_state_cb, result, NULL, G_OBJECT (self));
}

/**
 * tp_text_channel_set_chat_state_finish:
 * @self: a #TpTextChannel
 * @result: a #GAsyncResult
 * @error: a #GError to fill
 *
 * Finishes to set chat state.
 *
 * Returns: %TRUE if the chat state has been changed, %FALSE otherwise.
 *
 * Since: 0.13.UNRELEASED
 */
gboolean
tp_text_channel_set_chat_state_finish (TpTextChannel *self,
    GAsyncResult *result,
    GError **error)
{
  _tp_implement_finish_void (self, tp_text_channel_set_chat_state_finish)
}