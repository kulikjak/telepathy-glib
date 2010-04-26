/*
 * Context objects for TpBaseClient calls (internal)
 *
 * Copyright (C) 2009 Collabora Ltd. <http://www.collabora.co.uk/>
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

#ifndef __TP_OBSERVE_CHANNELS_CONTEXT_INTERNAL_H__
#define __TP_OBSERVE_CHANNELS_CONTEXT_INTERNAL_H__

#include <dbus/dbus-glib.h>

#include <telepathy-glib/account.h>
#include <telepathy-glib/observe-channels-context.h>
#include <telepathy-glib/channel-dispatch-operation.h>

G_BEGIN_DECLS

struct _TpObserveChannelsContext {
  /*<private>*/
  GObject parent;
  TpObserveChannelsContextPrivate *priv;

  TpAccount *account;
  TpConnection *connection;
  /* array of reffed TpChannel */
  GPtrArray *channels;
  /* List of reffed TpChannelDispatchOperation */
  TpChannelDispatchOperation *dispatch_operation;
  GPtrArray *requests;
  GHashTable *observer_info;
  DBusGMethodInvocation *dbus_context;
};

TpObserveChannelsContext * _tp_observe_channels_context_new (
    TpAccount *account,
    TpConnection *connection,
    GPtrArray *channels,
    TpChannelDispatchOperation *dispatch_operation,
    GPtrArray *requests,
    GHashTable *observer_info,
    DBusGMethodInvocation *dbus_context);

TpObserveChannelsContextState _tp_observe_channels_context_get_state (
    TpObserveChannelsContext *self);

G_END_DECLS

#endif
