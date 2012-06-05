/*
 * connection-manager.c - proxy for a Telepathy connection manager
 *
 * Copyright (C) 2007-2009 Collabora Ltd. <http://www.collabora.co.uk/>
 * Copyright (C) 2007-2009 Nokia Corporation
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

#include "config.h"

#include "telepathy-glib/connection-manager.h"

#include <string.h>

#include <telepathy-glib/cli-misc.h>
#include "telepathy-glib/defs.h"
#include "telepathy-glib/enums.h"
#include "telepathy-glib/errors.h"
#include "telepathy-glib/gtypes.h"
#include <telepathy-glib/interfaces.h>
#include <telepathy-glib/proxy-internal.h>
#include <telepathy-glib/proxy-subclass.h>
#include "telepathy-glib/util.h"

#define DEBUG_FLAG TP_DEBUG_MANAGER
#include "telepathy-glib/debug-internal.h"
#include "telepathy-glib/protocol-internal.h"
#include "telepathy-glib/util-internal.h"

/**
 * SECTION:connection-manager
 * @title: TpConnectionManager
 * @short_description: proxy object for a Telepathy connection manager
 * @see_also: #TpConnection
 *
 * #TpConnectionManager objects represent Telepathy connection managers. They
 * can be used to open connections.
 *
 * Since: 0.7.1
 */

/**
 * TP_CONNECTION_MANAGER_FEATURE_CORE:
 *
 * Expands to a call to a function that returns a quark for the "core" feature
 * on a #TpConnectionManager.
 *
 * After this feature is prepared, basic information about the connection
 * manager's protocols (tp_connection_manager_dup_protocols()), and their
 * available parameters, will have been retrieved, either by activating the
 * connection manager over D-Bus or by reading the .manager file in which
 * that information is cached.
 *
 * Since 0.11.11, this feature also finds any extra interfaces that
 * this connection manager has, and adds them to #TpProxy:interfaces (where
 * they can be queried with tp_proxy_has_interface()).
 *
 * One can ask for a feature to be prepared using the
 * tp_proxy_prepare_async() function, and waiting for it to callback.
 *
 * Since: 0.11.3
 */

GQuark
tp_connection_manager_get_feature_quark_core (void)
{
  return g_quark_from_static_string ("tp-connection-manager-feature-core");
}

/**
 * TpCMInfoSource:
 * @TP_CM_INFO_SOURCE_NONE: no information available
 * @TP_CM_INFO_SOURCE_FILE: information came from a .manager file
 * @TP_CM_INFO_SOURCE_LIVE: information came from the connection manager
 *
 * Describes possible sources of information on connection managers'
 * supported protocols.
 *
 * Since 0.11.5, there is a corresponding #GEnumClass type,
 * %TP_TYPE_CM_INFO_SOURCE.
 *
 * Since: 0.7.1
 */

/**
 * TP_TYPE_CM_INFO_SOURCE:
 *
 * The #GEnumClass type of a #TpCMInfoSource.
 *
 * Since: 0.11.5
 */

/**
 * TpConnectionManagerClass:
 *
 * The class of a #TpConnectionManager.
 *
 * Since: 0.7.1
 */

enum
{
  SIGNAL_ACTIVATED,
  SIGNAL_GOT_INFO,
  SIGNAL_EXITED,
  N_SIGNALS
};

static guint signals[N_SIGNALS] = {0};

enum
{
  PROP_INFO_SOURCE = 1,
  PROP_MANAGER_FILE,
  PROP_ALWAYS_INTROSPECT,
  PROP_CONNECTION_MANAGER,
  N_PROPS
};

/**
 * TpConnectionManager:
 *
 * A proxy object for a Telepathy connection manager.
 *
 * This might represent a connection manager which is currently running
 * (in which case it can be introspected) or not (in which case its
 * capabilities can be read from .manager files in the filesystem).
 * Accordingly, this object never emits #TpProxy::invalidated unless all
 * references to it are discarded.
 *
 * Various fields and methods on this object do not work until
 * %TP_CONNECTION_MANAGER_FEATURE_CORE is prepared. Use
 * tp_proxy_prepare_async() to wait for this to happen.
 *
 * Since 0.19.1, accessing the fields of this struct is deprecated,
 * and they are no longer documented here.
 * Use the accessors tp_connection_manager_get_name(),
 * tp_connection_manager_is_running(),
 * tp_connection_manager_dup_protocols(),
 * tp_connection_manager_get_info_source()
 * and the #TpConnectionManager:always-introspect property instead.
 *
 * Since: 0.7.1
 */

/**
 * TpConnectionManagerParam:
 *
 * Structure representing a connection manager parameter.
 *
 * Since 0.19.1, accessing the fields of this struct is deprecated,
 * and they are no longer documented here.
 * Use the accessors tp_connection_manager_param_get_name(),
 * tp_connection_manager_param_get_dbus_signature(),
 * tp_connection_manager_param_is_required(),
 * tp_connection_manager_param_is_required_for_registration(),
 * tp_connection_manager_param_is_secret(),
 * tp_connection_manager_param_is_dbus_property(),
 * tp_connection_manager_param_get_default(),
 * tp_connection_manager_param_dup_default_variant() instead.
 *
 * Since: 0.7.1
 */

typedef enum {
    INTROSPECT_IDLE,
    INTROSPECT_GETTING_PROPERTIES,
    INTROSPECT_LISTING_PROTOCOLS,
    INTROSPECT_GETTING_PARAMETERS
} IntrospectionStep;

struct _TpConnectionManagerPrivate {
    /* absolute path to .manager file */
    gchar *manager_file;

    /* source ID for reading the manager file later */
    guint manager_file_read_idle_id;

    /* source ID for introspecting later */
    guint introspect_idle_id;

    /* TRUE if dispose() has run already */
    unsigned disposed:1;

    /* dup'd name => referenced TpProtocol */
    GHashTable *protocol_objects;

    /* dup'd name => referenced TpProtocol
     *
     * If we're waiting for a GetParameters, protocols we found so far for
     * the introspection that is in progress (will replace protocol_objects
     * when finished). Otherwise NULL */
    GHashTable *found_protocols;

    /* list of WhenReadyContext */
    GList *waiting_for_ready;

    /* things we introspected so far */
    IntrospectionStep introspection_step;

    /* the method call currently pending, or NULL if none. */
    TpProxyPendingCall *introspection_call;

    /* FALSE if initial name-owner (if any) hasn't been found yet */
    gboolean name_known;
    /* TRUE if someone asked us to activate but we're putting it off until
     * name_known */
    gboolean want_activation;
};

G_DEFINE_TYPE (TpConnectionManager,
    tp_connection_manager,
    TP_TYPE_PROXY)


static void
_tp_connection_manager_param_copy_contents (
    const TpConnectionManagerParam *in,
    TpConnectionManagerParam *out)
{
  out->name = g_strdup (in->name);
  out->dbus_signature = g_strdup (in->dbus_signature);
  out->flags = in->flags;

  if (G_IS_VALUE (&in->default_value))
    {
      g_value_init (&out->default_value, G_VALUE_TYPE (&in->default_value));
      g_value_copy (&in->default_value, &out->default_value);
    }
}


/**
 * tp_connection_manager_param_copy:
 * @in: the #TpConnectionManagerParam to copy
 *
 * <!-- Returns: says it all -->
 *
 * Returns: a newly (slice) allocated #TpConnectionManagerParam, free with
 *  tp_connection_manager_param_free()
 *
 * Since: 0.11.3
 */
TpConnectionManagerParam *
tp_connection_manager_param_copy (const TpConnectionManagerParam *in)
{
  TpConnectionManagerParam *out = g_slice_new0 (TpConnectionManagerParam);

  _tp_connection_manager_param_copy_contents (in, out);

  return out;
}


/**
 * tp_connection_manager_param_free:
 * @param: the #TpConnectionManagerParam to free
 *
 * Frees @param, which was copied with tp_connection_manager_param_copy().
 *
 * Since: 0.11.3
 */
void
tp_connection_manager_param_free (TpConnectionManagerParam *param)
{
  _tp_connection_manager_param_free_contents (param);

  g_slice_free (TpConnectionManagerParam, param);
}

/**
 * TP_TYPE_CONNECTION_MANAGER_PARAM:
 *
 * The boxed type of a #TpConnectionManagerParam.
 *
 * Since: 0.11.3
 */

G_DEFINE_BOXED_TYPE (TpConnectionManagerParam, tp_connection_manager_param,
    tp_connection_manager_param_copy, tp_connection_manager_param_free)

static void
tp_connection_manager_ready_or_failed (TpConnectionManager *self,
                                       const GError *error)
{
  if (self->info_source > TP_CM_INFO_SOURCE_NONE)
    {
      /* we have info already, so suppress any error and return the old info */
      error = NULL;
    }
  else
    {
      g_assert (error != NULL);
    }

  if (error == NULL)
    {
      _tp_proxy_set_feature_prepared ((TpProxy *) self,
          TP_CONNECTION_MANAGER_FEATURE_CORE, TRUE);
    }
  else
    {
      _tp_proxy_set_features_failed ((TpProxy *) self, error);
    }
}

static void tp_connection_manager_continue_introspection
    (TpConnectionManager *self);

static void tp_connection_manager_ready_or_failed (TpConnectionManager *self,
                                       const GError *error);

static void
tp_connection_manager_end_introspection (TpConnectionManager *self,
                                         const GError *error)
{
  self->priv->introspection_step = INTROSPECT_IDLE;

  if (self->priv->introspection_call != NULL)
    {
      tp_proxy_pending_call_cancel (self->priv->introspection_call);
      self->priv->introspection_call = NULL;
    }

  if (self->priv->found_protocols != NULL)
    {
      g_hash_table_unref (self->priv->found_protocols);
      self->priv->found_protocols = NULL;
    }

  DEBUG ("End of introspection, info source %u", self->info_source);
  g_signal_emit (self, signals[SIGNAL_GOT_INFO], 0, self->info_source);
  tp_connection_manager_ready_or_failed (self, error);
}

static void
tp_connection_manager_get_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer nil G_GNUC_UNUSED,
    GObject *object G_GNUC_UNUSED)
{
  TpConnectionManager *self = (TpConnectionManager *) proxy;

  g_assert (TP_IS_CONNECTION_MANAGER (self));
  g_assert (self->priv->introspection_step == INTROSPECT_GETTING_PROPERTIES);
  g_assert (self->priv->introspection_call != NULL);
  self->priv->introspection_call = NULL;

  if (error == NULL)
    {
      GHashTable *protocols;

      tp_proxy_add_interfaces (proxy,
          tp_asv_get_strv (properties, "Interfaces"));

      protocols = tp_asv_get_boxed (properties, "Protocols",
          TP_HASH_TYPE_PROTOCOL_PROPERTIES_MAP);

      if (protocols != NULL)
        {
          GHashTableIter iter;
          gpointer k, v;

          DEBUG ("%u Protocols from D-Bus", g_hash_table_size (protocols));

          g_assert (self->priv->found_protocols == NULL);
          self->priv->found_protocols = g_hash_table_new_full (g_str_hash,
              g_str_equal, g_free, g_object_unref);

          g_hash_table_iter_init (&iter, protocols);

          while (g_hash_table_iter_next (&iter, &k, &v))
            {
              const gchar *name = k;
              GHashTable *protocol_properties = v;

              if (tp_connection_manager_check_valid_protocol_name (name, NULL))
                {
                  TpProtocol *proto_object = tp_protocol_new (
                      tp_proxy_get_dbus_daemon (self), self->name, name,
                      protocol_properties, NULL);

                  /* tp_protocol_new can currently only fail because of
                   * malformed names, and we already checked for that */
                  g_assert (proto_object != NULL);

                  g_hash_table_insert (self->priv->found_protocols,
                      g_strdup (name), proto_object);
                }
              else
                {
                  INFO ("ignoring invalid Protocol name %s from %s",
                      name, tp_proxy_get_object_path (self));
                }
            }
        }

      tp_connection_manager_continue_introspection (self);
    }
  else
    {
      DEBUG ("Error getting ConnectionManager properties: %s %d: %s",
          g_quark_to_string (error->domain), error->code, error->message);

      if (!self->running)
        {
          /* GetAll failed to start it - we assume this is because
           * activation failed. */
          g_signal_emit (self, signals[SIGNAL_EXITED], 0);
        }

      tp_connection_manager_end_introspection (self, error);
    }
}

static void
tp_connection_manager_continue_introspection (TpConnectionManager *self)
{
  GHashTable *tmp;
  guint old;

  if (self->priv->introspection_step == INTROSPECT_IDLE)
    {
      DEBUG ("calling GetAll on CM");
      self->priv->introspection_step = INTROSPECT_GETTING_PROPERTIES;
      self->priv->introspection_call = tp_cli_dbus_properties_call_get_all (
          self, -1, TP_IFACE_CONNECTION_MANAGER,
          tp_connection_manager_get_all_cb, NULL, NULL, NULL);
      return;
    }


  /* swap found_protocols and protocol_objects, so we'll free the old
   * protocol_objects as part of end_introspection */
  tmp = self->priv->protocol_objects;
  self->priv->protocol_objects = self->priv->found_protocols;
  self->priv->found_protocols = tmp;

  old = self->info_source;
  self->info_source = TP_CM_INFO_SOURCE_LIVE;

  if (old != TP_CM_INFO_SOURCE_LIVE)
    g_object_notify ((GObject *) self, "info-source");

  tp_connection_manager_end_introspection (self, NULL);

  g_assert (self->priv->introspection_step == INTROSPECT_IDLE);
}

static gboolean
introspection_in_progress (TpConnectionManager *self)
{
  return (self->priv->introspection_call != NULL ||
      self->priv->found_protocols != NULL);
}

static gboolean
tp_connection_manager_idle_introspect (gpointer data)
{
  TpConnectionManager *self = data;

  /* Start introspecting if we want to and we're not already */
  if (!introspection_in_progress (self) &&
      (self->always_introspect ||
       self->info_source == TP_CM_INFO_SOURCE_NONE))
    {
      tp_connection_manager_continue_introspection (self);
    }

  self->priv->introspect_idle_id = 0;

  return FALSE;
}

static gboolean tp_connection_manager_idle_read_manager_file (gpointer data);

static void
tp_connection_manager_name_owner_changed_cb (TpDBusDaemon *bus,
                                             const gchar *name,
                                             const gchar *new_owner,
                                             gpointer user_data)
{
  TpConnectionManager *self = user_data;

  /* make sure self exists for the duration of this callback */
  g_object_ref (self);

  if (new_owner[0] == '\0')
    {
      GError e = { TP_DBUS_ERRORS, TP_DBUS_ERROR_NAME_OWNER_LOST,
          "Connection manager process exited during introspection" };

      self->running = FALSE;

      /* cancel pending introspection, if any */
      if (introspection_in_progress (self))
        tp_connection_manager_end_introspection (self, &e);

      /* If our name wasn't known already, a change to "" is just the initial
       * state, so we didn't *exit* as such. */
      if (self->priv->name_known)
        {
          g_signal_emit (self, signals[SIGNAL_EXITED], 0);
        }
    }
  else
    {
      /* represent an atomic change of ownership as if it was an exit and
       * restart */
      if (self->running)
        tp_connection_manager_name_owner_changed_cb (bus, name, "", self);

      self->running = TRUE;
      g_signal_emit (self, signals[SIGNAL_ACTIVATED], 0);

      if (self->priv->introspect_idle_id == 0)
        self->priv->introspect_idle_id = g_idle_add (
            tp_connection_manager_idle_introspect, self);
    }

  /* if we haven't started introspecting yet, now would be a good time */
  if (!self->priv->name_known)
    {
      g_assert (self->priv->manager_file_read_idle_id == 0);

      /* now we know whether we're running or not, we can try reading the
       * .manager file... */
      self->priv->manager_file_read_idle_id = g_idle_add (
          tp_connection_manager_idle_read_manager_file, self);

      if (self->priv->want_activation && self->priv->introspect_idle_id == 0)
        {
          /* ... but if activation was requested, we should also do that */
          self->priv->introspect_idle_id = g_idle_add (
              tp_connection_manager_idle_introspect, self);
        }

      /* Unfreeze automatic reading of .manager file if manager-file changes */
      self->priv->name_known = TRUE;
    }

  g_object_unref (self);
}

static gboolean
tp_connection_manager_read_file (TpDBusDaemon *dbus_daemon,
    const gchar *cm_name,
    const gchar *filename,
    GHashTable **protocols_out,
    GStrv *interfaces_out,
    GError **error)
{
  GKeyFile *file;
  gchar **groups = NULL;
  gchar **group;
  TpProtocol *proto_object;
  GHashTable *protocols = NULL;
  GStrv interfaces = NULL;

  file = g_key_file_new ();

  if (!g_key_file_load_from_file (file, filename, G_KEY_FILE_NONE, error))
    return FALSE;

  /* if missing, it's not an error, so ignore @error */
  interfaces = g_key_file_get_string_list (file, "ConnectionManager",
      "Interfaces", NULL, NULL);

  protocols = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);

  groups = g_key_file_get_groups (file, NULL);

  if (groups == NULL)
    goto success;

  for (group = groups; *group != NULL; group++)
    {
      gchar *name;
      GHashTable *immutables;

      immutables = _tp_protocol_parse_manager_file (file, cm_name, *group,
          &name);

      if (immutables == NULL)
        continue;

      proto_object = tp_protocol_new (dbus_daemon, cm_name, name,
          immutables, NULL);
      g_assert (proto_object != NULL);

      /* steals @name */
      g_hash_table_insert (protocols, name, proto_object);

      g_hash_table_unref (immutables);
    }

success:
  g_strfreev (groups);
  g_key_file_free (file);

  if (protocols_out != NULL)
    *protocols_out = protocols;
  else
    g_hash_table_unref (protocols);

  if (interfaces_out != NULL)
    *interfaces_out = interfaces;
  else
    g_strfreev (interfaces);

  return TRUE;
}

static gboolean
tp_connection_manager_idle_read_manager_file (gpointer data)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (data);

  self->priv->manager_file_read_idle_id = 0;

  if (self->priv->protocol_objects == NULL)
    {
      if (self->priv->manager_file != NULL &&
          self->priv->manager_file[0] != '\0')
        {
          GError *error = NULL;
          GHashTable *protocols;
          GStrv interfaces = NULL;

          DEBUG ("Reading %s", self->priv->manager_file);

          if (!tp_connection_manager_read_file (
              tp_proxy_get_dbus_daemon (self),
              self->name, self->priv->manager_file, &protocols, &interfaces,
              &error))
            {
              DEBUG ("Failed to load %s: %s", self->priv->manager_file,
                  error->message);
              g_error_free (error);
              error = NULL;
            }
          else
            {
              tp_proxy_add_interfaces ((TpProxy *) self,
                  (const gchar * const *) interfaces);
              g_strfreev (interfaces);

              self->priv->protocol_objects = protocols;

              DEBUG ("Got info from file");
              /* previously it must have been NONE */
              self->info_source = TP_CM_INFO_SOURCE_FILE;

              g_object_ref (self);
              g_object_notify ((GObject *) self, "info-source");

              g_signal_emit (self, signals[SIGNAL_GOT_INFO], 0,
                  self->info_source);
              tp_connection_manager_ready_or_failed (self, NULL);
              g_object_unref (self);

              goto out;
            }
        }

      if (self->priv->introspect_idle_id == 0)
        {
          DEBUG ("no .manager file or failed to parse it, trying to activate "
              "CM instead");
          tp_connection_manager_idle_introspect (self);
        }
      else
        {
          DEBUG ("no .manager file, but will activate CM soon anyway");
        }
      /* else we're going to introspect soon anyway */
    }

out:
  return FALSE;
}

static gchar *
tp_connection_manager_find_manager_file (const gchar *name)
{
  gchar *filename;
  const gchar * const * data_dirs;

  g_assert (name != NULL);

  filename = g_strdup_printf ("%s/telepathy/managers/%s.manager",
      g_get_user_data_dir (), name);

  DEBUG ("in XDG_DATA_HOME: trying %s", filename);

  if (g_file_test (filename, G_FILE_TEST_EXISTS))
    return filename;

  g_free (filename);

  for (data_dirs = g_get_system_data_dirs ();
       *data_dirs != NULL;
       data_dirs++)
    {
      filename = g_strdup_printf ("%s/telepathy/managers/%s.manager",
          *data_dirs, name);

      DEBUG ("in XDG_DATA_DIRS: trying %s", filename);

      if (g_file_test (filename, G_FILE_TEST_EXISTS))
        return filename;

      g_free (filename);
    }

  return NULL;
}

static GObject *
tp_connection_manager_constructor (GType type,
                                   guint n_params,
                                   GObjectConstructParam *params)
{
  GObjectClass *object_class =
      (GObjectClass *) tp_connection_manager_parent_class;
  TpConnectionManager *self =
      TP_CONNECTION_MANAGER (object_class->constructor (type, n_params,
            params));
  TpProxy *as_proxy = (TpProxy *) self;
  const gchar *object_path = as_proxy->object_path;
  const gchar *bus_name = as_proxy->bus_name;

  g_return_val_if_fail (object_path != NULL, NULL);
  g_return_val_if_fail (bus_name != NULL, NULL);

  /* Watch my D-Bus name */
  tp_dbus_daemon_watch_name_owner (as_proxy->dbus_daemon,
      as_proxy->bus_name, tp_connection_manager_name_owner_changed_cb, self,
      NULL);

  self->name = strrchr (object_path, '/') + 1;
  g_assert (self->name != NULL);

  if (self->priv->manager_file == NULL)
    {
      self->priv->manager_file =
          tp_connection_manager_find_manager_file (self->name);
    }

  return (GObject *) self;
}

static void
tp_connection_manager_init (TpConnectionManager *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, TP_TYPE_CONNECTION_MANAGER,
      TpConnectionManagerPrivate);
}

static void
tp_connection_manager_dispose (GObject *object)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);
  TpProxy *as_proxy = (TpProxy *) self;

  if (self->priv->disposed)
    goto finally;

  self->priv->disposed = TRUE;

  tp_dbus_daemon_cancel_name_owner_watch (as_proxy->dbus_daemon,
      as_proxy->bus_name, tp_connection_manager_name_owner_changed_cb,
      object);

  if (self->priv->protocol_objects != NULL)
    {
      g_hash_table_unref (self->priv->protocol_objects);
      self->priv->protocol_objects = NULL;
    }

  if (self->priv->found_protocols != NULL)
    {
      g_hash_table_unref (self->priv->found_protocols);
      self->priv->found_protocols = NULL;
    }

finally:
  G_OBJECT_CLASS (tp_connection_manager_parent_class)->dispose (object);
}

static void
tp_connection_manager_finalize (GObject *object)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);

  g_free (self->priv->manager_file);

  if (self->priv->manager_file_read_idle_id != 0)
    g_source_remove (self->priv->manager_file_read_idle_id);

  if (self->priv->introspect_idle_id != 0)
    g_source_remove (self->priv->introspect_idle_id);

  G_OBJECT_CLASS (tp_connection_manager_parent_class)->finalize (object);
}

static void
tp_connection_manager_get_property (GObject *object,
                                    guint property_id,
                                    GValue *value,
                                    GParamSpec *pspec)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);

  switch (property_id)
    {
    case PROP_CONNECTION_MANAGER:
      g_value_set_string (value, self->name);
      break;

    case PROP_INFO_SOURCE:
      g_value_set_uint (value, self->info_source);
      break;

    case PROP_MANAGER_FILE:
      g_value_set_string (value, self->priv->manager_file);
      break;

    case PROP_ALWAYS_INTROSPECT:
      g_value_set_boolean (value, self->always_introspect);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
tp_connection_manager_set_property (GObject *object,
                                    guint property_id,
                                    const GValue *value,
                                    GParamSpec *pspec)
{
  TpConnectionManager *self = TP_CONNECTION_MANAGER (object);

  switch (property_id)
    {
    case PROP_MANAGER_FILE:
      g_free (self->priv->manager_file);

      /* If initial code has already run, change the definition of where
       * we expect to find the .manager file and trigger re-introspection.
       * Otherwise, just take the value - when name_known becomes TRUE we
       * queue the first-time manager file lookup anyway.
       */
      if (self->priv->name_known)
        {
          const gchar *tmp = g_value_get_string (value);

          if (tmp == NULL)
            {
              self->priv->manager_file =
                  tp_connection_manager_find_manager_file (self->name);
            }
          else
            {
              self->priv->manager_file = g_strdup (tmp);
            }

          if (self->priv->manager_file_read_idle_id == 0)
            self->priv->manager_file_read_idle_id = g_idle_add (
                tp_connection_manager_idle_read_manager_file, self);
        }
      else
        {
          self->priv->manager_file = g_value_dup_string (value);
        }

      break;

    case PROP_ALWAYS_INTROSPECT:
        {
          gboolean old = self->always_introspect;

          self->always_introspect = g_value_get_boolean (value);

          if (self->running && !old && self->always_introspect)
            {
              /* It's running, we weren't previously auto-introspecting,
               * but we are now. Try it when idle
               */
              if (self->priv->introspect_idle_id == 0)
                self->priv->introspect_idle_id = g_idle_add (
                    tp_connection_manager_idle_introspect, self);
            }
        }
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

/**
 * tp_connection_manager_init_known_interfaces:
 *
 * Ensure that the known interfaces for TpConnectionManager have been set up.
 * This is done automatically when necessary, but for correct
 * overriding of library interfaces by local extensions, you should
 * call this function before calling
 * tp_proxy_or_subclass_hook_on_interface_add() with first argument
 * %TP_TYPE_CONNECTION_MANAGER.
 *
 * Since: 0.7.32
 */
void
tp_connection_manager_init_known_interfaces (void)
{
  static gsize once = 0;

  if (g_once_init_enter (&once))
    {
      GType tp_type = TP_TYPE_CONNECTION_MANAGER;

      tp_proxy_init_known_interfaces ();
      tp_proxy_or_subclass_hook_on_interface_add (tp_type,
          tp_cli_connection_manager_add_signals);
      tp_proxy_subclass_add_error_mapping (tp_type,
          TP_ERROR_PREFIX, TP_ERROR, TP_TYPE_ERROR);

      g_once_init_leave (&once, 1);
    }
}

enum {
    FEAT_CORE,
    N_FEAT
};

static const TpProxyFeature *
tp_connection_manager_list_features (TpProxyClass *cls G_GNUC_UNUSED)
{
  static TpProxyFeature features[N_FEAT + 1] = { { 0 } };

  if (G_LIKELY (features[0].name != 0))
    return features;

  features[FEAT_CORE].name = TP_CONNECTION_MANAGER_FEATURE_CORE;
  features[FEAT_CORE].core = TRUE;

  /* assert that the terminator at the end is there */
  g_assert (features[N_FEAT].name == 0);

  return features;
}

static void
tp_connection_manager_class_init (TpConnectionManagerClass *klass)
{
  TpProxyClass *proxy_class = (TpProxyClass *) klass;
  GObjectClass *object_class = (GObjectClass *) klass;
  GParamSpec *param_spec;

  tp_connection_manager_init_known_interfaces ();

  g_type_class_add_private (klass, sizeof (TpConnectionManagerPrivate));

  object_class->constructor = tp_connection_manager_constructor;
  object_class->get_property = tp_connection_manager_get_property;
  object_class->set_property = tp_connection_manager_set_property;
  object_class->dispose = tp_connection_manager_dispose;
  object_class->finalize = tp_connection_manager_finalize;

  proxy_class->interface = TP_IFACE_QUARK_CONNECTION_MANAGER;
  proxy_class->list_features = tp_connection_manager_list_features;

  /**
   * TpConnectionManager:info-source:
   *
   * Where we got the current information on supported protocols
   * (a #TpCMInfoSource).
   *
   * Since 0.7.26, the #GObject::notify signal is emitted for this
   * property.
   *
   * (Note that this is of type %G_TYPE_UINT, not %TP_TYPE_CM_INFO_SOURCE,
   * for historical reasons.)
   */
  param_spec = g_param_spec_uint ("info-source", "CM info source",
      "Where we got the current information on supported protocols",
      TP_CM_INFO_SOURCE_NONE, TP_CM_INFO_SOURCE_LIVE, TP_CM_INFO_SOURCE_NONE,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INFO_SOURCE,
      param_spec);

  /**
   * TpConnectionManager:connection-manager:
   *
   * The name of the connection manager, e.g. "gabble" (read-only).
   */
  param_spec = g_param_spec_string ("connection-manager", "CM name",
      "The name of the connection manager, e.g. \"gabble\" (read-only)",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONNECTION_MANAGER,
      param_spec);

  /**
   * TpConnectionManager:manager-file:
   *
   * The absolute path of the .manager file. If set to %NULL (the default),
   * the XDG data directories will be searched for a .manager file of the
   * correct name.
   *
   * If set to the empty string, no .manager file will be read.
   */
  param_spec = g_param_spec_string ("manager-file", ".manager filename",
      "The .manager filename",
      NULL,
      G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_MANAGER_FILE,
      param_spec);

  /**
   * TpConnectionManager:always-introspect:
   *
   * If %TRUE, always introspect the connection manager as it comes online,
   * even if we already have its info from a .manager file. Default %FALSE.
   */
  param_spec = g_param_spec_boolean ("always-introspect", "Always introspect?",
      "Opportunistically introspect the CM when it's run", FALSE,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_ALWAYS_INTROSPECT,
      param_spec);

  /**
   * TpConnectionManager::activated:
   * @self: the connection manager proxy
   *
   * Emitted when the connection manager's well-known name appears on the bus.
   */
  signals[SIGNAL_ACTIVATED] = g_signal_new ("activated",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  /**
   * TpConnectionManager::exited:
   * @self: the connection manager proxy
   *
   * Emitted when the connection manager's well-known name disappears from
   * the bus or when activation fails.
   */
  signals[SIGNAL_EXITED] = g_signal_new ("exited",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL, NULL,
      G_TYPE_NONE, 0);

  /**
   * TpConnectionManager::got-info:
   * @self: the connection manager proxy
   * @source: a #TpCMInfoSource
   *
   * Emitted when the connection manager's capabilities have been discovered.
   *
   * This signal is not very helpful. Using
   * tp_proxy_prepare_async() instead is recommended.
   */
  signals[SIGNAL_GOT_INFO] = g_signal_new ("got-info",
      G_OBJECT_CLASS_TYPE (klass),
      G_SIGNAL_RUN_LAST | G_SIGNAL_DETAILED,
      0,
      NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_UINT);
}

/**
 * tp_connection_manager_new:
 * @dbus: Proxy for the D-Bus daemon
 * @name: The connection manager name (such as "gabble")
 * @manager_filename: (allow-none): The #TpConnectionManager:manager-file
 *  property, which may (and generally should) be %NULL.
 * @error: used to return an error if %NULL is returned
 *
 * Convenience function to create a new connection manager proxy. If
 * its protocol and parameter information are required, you should call
 * tp_proxy_prepare_async() on the result.
 *
 * Returns: a new reference to a connection manager proxy, or %NULL if @error
 *          is set.
 */
TpConnectionManager *
tp_connection_manager_new (TpDBusDaemon *dbus,
                           const gchar *name,
                           const gchar *manager_filename,
                           GError **error)
{
  TpConnectionManager *cm;
  gchar *object_path, *bus_name;

  g_return_val_if_fail (dbus != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  if (!tp_connection_manager_check_valid_name (name, error))
    return NULL;

  object_path = g_strdup_printf ("%s%s", TP_CM_OBJECT_PATH_BASE, name);
  bus_name = g_strdup_printf ("%s%s", TP_CM_BUS_NAME_BASE, name);

  cm = TP_CONNECTION_MANAGER (g_object_new (TP_TYPE_CONNECTION_MANAGER,
        "dbus-daemon", dbus,
        "dbus-connection", ((TpProxy *) dbus)->dbus_connection,
        "bus-name", bus_name,
        "object-path", object_path,
        "manager-file", manager_filename,
        NULL));

  g_free (object_path);
  g_free (bus_name);

  return cm;
}

/**
 * tp_connection_manager_activate: (skip)
 * @self: a connection manager proxy
 *
 * Attempt to run and introspect the connection manager, asynchronously.
 * Since 0.7.26 this function is not generally very useful, since
 * the connection manager will now be activated automatically if necessary.
 *
 * If the CM was already running, do nothing and return %FALSE.
 *
 * On success, emit #TpConnectionManager::activated when the CM appears
 * on the bus, and #TpConnectionManager::got-info when its capabilities
 * have been (re-)discovered.
 *
 * On failure, emit #TpConnectionManager::exited without first emitting
 * activated.
 *
 * Returns: %TRUE if activation was needed and is now in progress, %FALSE
 *  if the connection manager was already running and no additional signals
 *  will be emitted.
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_activate (TpConnectionManager *self)
{
  if (self->priv->name_known)
    {
      if (self->running)
        {
          DEBUG ("already running");
          return FALSE;
        }

      if (self->priv->introspect_idle_id == 0)
        self->priv->introspect_idle_id = g_idle_add (
            tp_connection_manager_idle_introspect, self);
    }
  else
    {
      /* we'll activate later, when we know properly whether we're running */
      DEBUG ("queueing activation for when we know what's going on");
      self->priv->want_activation = TRUE;
    }

  return TRUE;
}

static gboolean
steal_into_ptr_array (gpointer key,
                      gpointer value,
                      gpointer user_data)
{
  if (value != NULL)
    g_ptr_array_add (user_data, value);

  g_free (key);

  return TRUE;
}

typedef void (*TpConnectionManagerListCb) (TpConnectionManager * const *cms,
    gsize n_cms,
    const GError *error,
    gpointer user_data,
    GObject *weak_object);

typedef struct
{
  GHashTable *table;
  GPtrArray *arr;
  GSimpleAsyncResult *result;
  TpConnectionManagerListCb callback;
  gpointer user_data;
  GDestroyNotify destroy;
  gpointer weak_object;
  TpProxyPendingCall *pending_call;
  size_t base_len;
  gsize refcount;
  gsize cms_to_ready;
  unsigned getting_names:1;
  unsigned had_weak_object:1;
} _ListContext;

static void
list_context_unref (_ListContext *list_context)
{
  guint i;

  if (--list_context->refcount > 0)
    return;

  if (list_context->weak_object != NULL)
    g_object_remove_weak_pointer (list_context->weak_object,
        &list_context->weak_object);

  if (list_context->destroy != NULL)
    list_context->destroy (list_context->user_data);

  if (list_context->arr != NULL)
    {
      for (i = 0; i < list_context->arr->len; i++)
        {
          TpConnectionManager *cm = g_ptr_array_index (list_context->arr, i);

          if (cm != NULL)
            g_object_unref (cm);
        }

      g_ptr_array_unref (list_context->arr);
    }

  g_hash_table_unref (list_context->table);
  g_slice_free (_ListContext, list_context);
}

static void
tp_list_connection_managers_cm_prepared (GObject *source G_GNUC_UNUSED,
    GAsyncResult *result G_GNUC_UNUSED,
    gpointer user_data)
{
  _ListContext *list_context = user_data;

  /* ignore the result here - all we guarantee is that the CM is ready
   * *if possible* */

  if ((--list_context->cms_to_ready) == 0)
    {
      TpConnectionManager **cms;
      guint n_cms = list_context->arr->len;

      g_assert (list_context->callback != NULL);

      g_ptr_array_add (list_context->arr, NULL);
      cms = (TpConnectionManager **) list_context->arr->pdata;

      /* If we never had a weak object anyway, call the callback.
       * If we had a weak object when we started, only call the callback
       * if it hasn't died yet. */
      if (!list_context->had_weak_object || list_context->weak_object != NULL)
        {
          list_context->callback (cms, n_cms, NULL, list_context->user_data,
              list_context->weak_object);
        }

      list_context->callback = NULL;
    }

  list_context_unref (list_context);
}

static void
tp_list_connection_managers_got_names (TpDBusDaemon *bus_daemon,
                                       const gchar * const *names,
                                       const GError *error,
                                       gpointer user_data,
                                       GObject *weak_object)
{
  _ListContext *list_context = user_data;
  const gchar * const *name_iter;

  /* The TpProxy APIs we use guarantee this */
  g_assert (weak_object != NULL || !list_context->had_weak_object);

  if (error != NULL)
    {
      list_context->callback (NULL, 0, error, list_context->user_data,
          weak_object);
      return;
    }

  for (name_iter = names; name_iter != NULL && *name_iter != NULL; name_iter++)
    {
      const gchar *name;
      TpConnectionManager *cm;

      if (strncmp (TP_CM_BUS_NAME_BASE, *name_iter, list_context->base_len)
          != 0)
        continue;

      name = *name_iter + list_context->base_len;

      if (g_hash_table_lookup (list_context->table, name) == NULL)
        {
          /* just ignore connection managers with bad names */
          cm = tp_connection_manager_new (bus_daemon, name, NULL, NULL);
          if (cm != NULL)
            g_hash_table_insert (list_context->table, g_strdup (name), cm);
        }
    }

  if (list_context->getting_names)
    {
      /* now that we have all the CMs, wait for them all to be ready */
      guint i;

      list_context->arr = g_ptr_array_sized_new (g_hash_table_size
              (list_context->table));

      g_hash_table_foreach_steal (list_context->table, steal_into_ptr_array,
          list_context->arr);

      list_context->cms_to_ready = list_context->arr->len;
      list_context->refcount += list_context->cms_to_ready;

      for (i = 0; i < list_context->cms_to_ready; i++)
        {
          TpConnectionManager *cm = g_ptr_array_index (list_context->arr, i);

          tp_proxy_prepare_async (cm, NULL,
              tp_list_connection_managers_cm_prepared, list_context);
        }
    }
  else
    {
      list_context->getting_names = TRUE;
      list_context->refcount++;
      tp_dbus_daemon_list_names (bus_daemon, 2000,
          tp_list_connection_managers_got_names, list_context,
          (GDestroyNotify) list_context_unref, weak_object);
    }
}

static void
tp_list_connection_managers (TpDBusDaemon *bus_daemon,
                             TpConnectionManagerListCb callback,
                             gpointer user_data,
                             GDestroyNotify destroy,
                             GObject *weak_object)
{
  _ListContext *list_context = g_slice_new0 (_ListContext);

  list_context->base_len = strlen (TP_CM_BUS_NAME_BASE);
  list_context->callback = callback;
  list_context->user_data = user_data;
  list_context->destroy = destroy;

  list_context->getting_names = FALSE;
  list_context->refcount = 1;
  list_context->table = g_hash_table_new_full (g_str_hash, g_str_equal, g_free,
      g_object_unref);
  list_context->arr = NULL;
  list_context->cms_to_ready = 0;

  if (weak_object != NULL)
    {
      list_context->weak_object = weak_object;
      list_context->had_weak_object = TRUE;
      g_object_add_weak_pointer (weak_object, &list_context->weak_object);
    }

  tp_dbus_daemon_list_activatable_names (bus_daemon, 2000,
      tp_list_connection_managers_got_names, list_context,
      (GDestroyNotify) list_context_unref, weak_object);
}

static void
list_connection_managers_async_cb (TpConnectionManager * const *cms,
    gsize n_cms,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *result = user_data;

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (result, error);
    }
  else
    {
      GList *l = NULL;
      gsize i;

      for (i = 0; i < n_cms; i++)
          l = g_list_prepend (l, g_object_ref (cms[i]));

      l = g_list_reverse (l);

      g_simple_async_result_set_op_res_gpointer (result, l,
          (GDestroyNotify) _tp_object_list_free);
    }

  g_simple_async_result_complete_in_idle (result);

  /* result is unreffed by GDestroyNotify */
}

/**
 * tp_list_connection_managers_async:
 * @dbus_daemon: (allow-none): a #TpDBusDaemon, or %NULL to use
 *  tp_dbus_daemon_dup()
 * @callback: a callback to call with a list of CMs
 * @user_data: data to pass to @callback
 *
 * List the available (running or installed) connection managers,
 * asynchronously, and wait for their %TP_CONNECTION_MANAGER_FEATURE_CORE
 * feature to be ready.
 *
 * Since: 0.17.6
 */
void
tp_list_connection_managers_async (TpDBusDaemon *dbus_daemon,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *result;
  GError *error = NULL;

  if (dbus_daemon == NULL)
    dbus_daemon = tp_dbus_daemon_dup (&error);
  else
    g_object_ref (dbus_daemon);

  result = g_simple_async_result_new (NULL, callback, user_data,
      tp_list_connection_managers_async);

  if (dbus_daemon == NULL)
    {
      g_simple_async_result_take_error (result, error);
      g_simple_async_result_complete_in_idle (result);
      g_object_unref (result);
    }
  else
    {
      tp_list_connection_managers (dbus_daemon,
          list_connection_managers_async_cb, result, g_object_unref, NULL);
      g_object_unref (dbus_daemon);
    }
}

/**
 * tp_list_connection_managers_finish:
 * @result: the result of tp_list_connection_managers_async()
 * @error: used to raise an error if the operation failed
 *
 * Finish listing the available connection managers.
 *
 * Free the list after use, for instance with
 * <literal>g_list_free_full (list, g_object_unref)</literal>.
 *
 * Returns: (transfer full) (element-type TelepathyGLib.ConnectionManager): a
 *  newly allocated list of references to #TpConnectionManager objects
 * Since: 0.17.6
 */
GList *
tp_list_connection_managers_finish (GAsyncResult *result,
    GError **error)
{
  _tp_implement_finish_return_copy_pointer (NULL,
      tp_list_connection_managers_async,
      _tp_object_list_copy);
}

/**
 * tp_connection_manager_check_valid_name:
 * @name: a possible connection manager name
 * @error: used to raise %TP_ERROR_INVALID_ARGUMENT if %FALSE is returned
 *
 * Check that the given string is a valid connection manager name, i.e. that
 * it consists entirely of ASCII letters, digits and underscores, and starts
 * with a letter.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_check_valid_name (const gchar *name,
                                        GError **error)
{
  const gchar *name_char;

  if (tp_str_empty (name))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "The empty string is not a valid connection manager name");
      return FALSE;
    }

  if (!g_ascii_isalpha (name[0]))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Not a valid connection manager name because first character "
          "is not an ASCII letter: %s", name);
      return FALSE;
    }

  for (name_char = name; *name_char != '\0'; name_char++)
    {
      if (!g_ascii_isalnum (*name_char) && *name_char != '_')
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Not a valid connection manager name because character '%c' "
              "is not an ASCII letter, digit or underscore: %s",
              *name_char, name);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * tp_connection_manager_check_valid_protocol_name:
 * @name: a possible protocol name
 * @error: used to raise %TP_ERROR_INVALID_ARGUMENT if %FALSE is returned
 *
 * Check that the given string is a valid protocol name, i.e. that
 * it consists entirely of ASCII letters, digits and hyphen/minus, and starts
 * with a letter.
 *
 * Returns: %TRUE if @name is valid
 *
 * Since: 0.7.1
 */
gboolean
tp_connection_manager_check_valid_protocol_name (const gchar *name,
                                                 GError **error)
{
  const gchar *name_char;

  if (name == NULL || name[0] == '\0')
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "The empty string is not a valid protocol name");
      return FALSE;
    }

  if (!g_ascii_isalpha (name[0]))
    {
      g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
          "Not a valid protocol name because first character "
          "is not an ASCII letter: %s", name);
      return FALSE;
    }

  for (name_char = name; *name_char != '\0'; name_char++)
    {
      if (!g_ascii_isalnum (*name_char) && *name_char != '-')
        {
          g_set_error (error, TP_ERROR, TP_ERROR_INVALID_ARGUMENT,
              "Not a valid protocol name because character '%c' "
              "is not an ASCII letter, digit or hyphen/minus: %s",
              *name_char, name);
          return FALSE;
        }
    }

  return TRUE;
}

/**
 * tp_connection_manager_get_name:
 * @self: a connection manager
 *
 * Return the internal name of this connection manager in the Telepathy
 * D-Bus API, e.g. "gabble" or "haze". This is often the name of the binary
 * without the "telepathy-" prefix.
 *
 * The returned string is valid as long as @self is. Copy it with g_strdup()
 * if a longer lifetime is required.
 *
 * Returns: the #TpConnectionManager:connection-manager property
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_get_name (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);
  return self->name;
}

/**
 * tp_connection_manager_is_running:
 * @self: a connection manager
 *
 * Return %TRUE if this connection manager currently appears to be running.
 * This may change at any time that the main loop is running; the
 * #TpConnectionManager::activated and #TpConnectionManager::exited signals
 * are emitted.
 *
 * Returns: whether the connection manager is currently running
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_is_running (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), FALSE);
  return self->running;
}

/**
 * tp_connection_manager_get_info_source:
 * @self: a connection manager
 *
 * If protocol and parameter information has been obtained from the connection
 * manager, return %TP_CM_INFO_SOURCE_LIVE; if it has been obtained from the
 * cache in the .manager file, return %TP_CM_INFO_SOURCE_FILE. If this
 * information has not yet been obtained, or obtaining it failed, return
 * %TP_CM_INFO_SOURCE_NONE.
 *
 * This may increase at any time that the main loop is running; the
 * #GObject::notify signal is emitted.
 *
 * Returns: the value of the #TpConnectionManager:info-source property
 * Since: 0.7.26
 */
TpCMInfoSource
tp_connection_manager_get_info_source (TpConnectionManager *self)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self),
      TP_CM_INFO_SOURCE_NONE);
  return self->info_source;
}

/**
 * tp_connection_manager_dup_protocol_names:
 * @self: a connection manager
 *
 * Returns a list of protocol names supported by this connection manager.
 * These are the internal protocol names used by the Telepathy specification
 * (e.g. "jabber" and "msn"), rather than user-visible names in any particular
 * locale.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %NULL. Use
 * tp_proxy_prepare_async() to wait for this.
 *
 * The result is copied and must be freed by the caller, but it is not
 * necessarily still true after the main loop is re-entered.
 *
 * Returns: (array zero-terminated=1) (transfer full): a #GStrv of protocol names
 * Since: 0.7.26
 */
gchar **
tp_connection_manager_dup_protocol_names (TpConnectionManager *self)
{
  GPtrArray *ret;
  GHashTableIter iter;
  gpointer key;

  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);

  if (self->info_source == TP_CM_INFO_SOURCE_NONE)
    return NULL;

  g_assert (self->priv->protocol_objects != NULL);

  ret = g_ptr_array_sized_new (
      g_hash_table_size (self->priv->protocol_objects));

  g_hash_table_iter_init (&iter, self->priv->protocol_objects);
  while (g_hash_table_iter_next (&iter, &key, NULL))
    g_ptr_array_add (ret, g_strdup (key));

  g_ptr_array_add (ret, NULL);

  return (gchar **) g_ptr_array_free (ret, FALSE);
}

/**
 * tp_connection_manager_get_protocol:
 * @self: a connection manager
 * @protocol: the name of a protocol as defined in the Telepathy D-Bus API,
 *            e.g. "jabber" or "msn"
 *
 * Returns an object representing a protocol, or %NULL if this connection
 * manager does not support the specified protocol.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %NULL. Use tp_proxy_prepare_async()
 * to wait for this.
 *
 * The result should be referenced with g_object_ref() if it will be kept.
 *
 * Returns: (transfer none): an object representing the protocol, or %NULL
 *
 * Since: 0.11.11
 */
TpProtocol *
tp_connection_manager_get_protocol (TpConnectionManager *self,
    const gchar *protocol)
{
  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);
  g_return_val_if_fail (protocol != NULL, NULL);

  if (self->priv->protocol_objects == NULL)
    return NULL;

  return g_hash_table_lookup (self->priv->protocol_objects, protocol);
}

/* FIXME: in Telepathy 1.0, rename to get_protocols */
/**
 * tp_connection_manager_dup_protocols:
 * @self: a connection manager
 *
 * Return objects representing all protocols supported by this connection
 * manager.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %NULL. Use tp_proxy_prepare_async()
 * to wait for this.
 *
 * The caller must free the list, for instance with
 * <literal>g_list_free_full (l, g_object_unref)</literal>.
 *
 * Returns: (transfer full) (element-type TelepathyGLib.Protocol): a list
 *  of #TpProtocol objects representing the protocols supported by @self,
 *  owned by the caller
 *
 * Since: 0.17.6
 */
GList *
tp_connection_manager_dup_protocols (TpConnectionManager *self)
{
  GList *l;

  g_return_val_if_fail (TP_IS_CONNECTION_MANAGER (self), NULL);

  if (self->priv->protocol_objects == NULL)
    return NULL;

  l = g_hash_table_get_values (self->priv->protocol_objects);

  g_list_foreach (l, (GFunc) g_object_ref, NULL);
  return l;
}

/**
 * tp_connection_manager_has_protocol:
 * @self: a connection manager
 * @protocol: the name of a protocol as defined in the Telepathy D-Bus API,
 *            e.g. "jabber" or "msn"
 *
 * Return whether @protocol is supported by this connection manager.
 *
 * If this function is called before the connection manager information has
 * been obtained, the result is always %FALSE. Use tp_proxy_prepare_async()
 * to wait for this.
 *
 * Returns: %TRUE if this connection manager supports @protocol
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_has_protocol (TpConnectionManager *self,
                                    const gchar *protocol)
{
  return (tp_connection_manager_get_protocol (self, protocol) != NULL);
}

/**
 * tp_connection_manager_param_get_name:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: the name of the parameter
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_param_get_name (const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, NULL);

  return param->name;
}

/**
 * tp_connection_manager_param_get_dbus_signature:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: the D-Bus signature of the parameter
 * Since: 0.7.26
 */
const gchar *
tp_connection_manager_param_get_dbus_signature (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, NULL);

  return param->dbus_signature;
}

/**
 * tp_connection_manager_param_is_required:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter is normally required
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_required (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_REQUIRED) != 0;
}

/**
 * tp_connection_manager_param_is_required_for_registration:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter is required when registering a new account
 *          (by setting the special "register" parameter to %TRUE)
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_required_for_registration (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_REGISTER) != 0;
}

/**
 * tp_connection_manager_param_is_secret:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter's value is a password or other secret
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_secret (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_SECRET) != 0;
}

/**
 * tp_connection_manager_param_is_dbus_property:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * <!-- -->
 *
 * Returns: %TRUE if the parameter represents a D-Bus property of the same name
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_is_dbus_property (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, FALSE);

  return (param->flags & TP_CONN_MGR_PARAM_FLAG_DBUS_PROPERTY) != 0;
}

/**
 * tp_connection_manager_param_get_default:
 * @param: a parameter supported by a #TpConnectionManager
 * @value: pointer to an unset (all zeroes) #GValue into which the default's
 *         type and value are written
 *
 * Get the default value for this parameter, if there is one. If %FALSE is
 * returned, @value is left uninitialized.
 *
 * Returns: %TRUE if there is a default value
 * Since: 0.7.26
 */
gboolean
tp_connection_manager_param_get_default (
    const TpConnectionManagerParam *param,
    GValue *value)
{
  g_return_val_if_fail (param != NULL, FALSE);
  g_return_val_if_fail (value != NULL, FALSE);
  g_return_val_if_fail (!G_IS_VALUE (value), FALSE);

  if ((param->flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT) == 0
      || !G_IS_VALUE (&param->default_value))
    return FALSE;

  g_value_init (value, G_VALUE_TYPE (&param->default_value));
  g_value_copy (&param->default_value, value);

  return TRUE;
}

/**
 * tp_connection_manager_param_dup_default_variant:
 * @param: a parameter supported by a #TpConnectionManager
 *
 * Get the default value for this parameter.
 *
 * Use g_variant_get_type() to check that the type is what you expect.
 * For instance, a string parameter should have type
 * %G_VARIANT_TYPE_STRING.
 *
 * Returns: the default value, or %NULL if there is no default
 * Since: 0.19.0
 */
GVariant *
tp_connection_manager_param_dup_default_variant (
    const TpConnectionManagerParam *param)
{
  g_return_val_if_fail (param != NULL, NULL);

  if ((param->flags & TP_CONN_MGR_PARAM_FLAG_HAS_DEFAULT) == 0
      || !G_IS_VALUE (&param->default_value))
    return NULL;

  return g_variant_ref_sink (dbus_g_value_build_g_variant (
        &param->default_value));
}
