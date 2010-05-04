/* Tests of TpBaseClient
 *
 * Copyright © 2010 Collabora Ltd. <http://www.collabora.co.uk/>
 *
 * Copying and distribution of this file, with or without modification,
 * are permitted in any medium without royalty provided the copyright
 * notice and this notice are preserved.
 */

#include <telepathy-glib/base-client.h>
#include <telepathy-glib/client.h>
#include <telepathy-glib/debug.h>
#include <telepathy-glib/defs.h>
#include <telepathy-glib/proxy-subclass.h>

#include "tests/lib/util.h"
#include "tests/lib/simple-account.h"
#include "tests/lib/simple-client.h"
#include "tests/lib/simple-conn.h"
#include "tests/lib/textchan-null.h"

typedef struct {
    GMainLoop *mainloop;
    TpDBusDaemon *dbus;

    /* Service side objects */
    TpBaseClient *base_client;
    SimpleClient *simple_client;
    TpBaseConnection *base_connection;
    SimpleAccount *account_service;
    TestTextChannelNull *text_chan_service;

    /* Client side objects */
    TpClient *client;
    TpConnection *connection;
    TpAccount *account;
    TpChannel *text_chan;

    GError *error /* initialized where needed */;
    GStrv interfaces;
} Test;

#define ACCOUNT_PATH TP_ACCOUNT_OBJECT_PATH_BASE "what/ev/er"

static void
setup (Test *test,
       gconstpointer data)
{
  gchar *chan_path;
  TpHandle handle;
  TpHandleRepoIface *contact_repo;

  test->mainloop = g_main_loop_new (NULL, FALSE);
  test->dbus = test_dbus_daemon_dup_or_die ();

  test->error = NULL;
  test->interfaces = NULL;

  /* Claim AccountManager bus-name (needed as we're going to export an Account
   * object). */
  tp_dbus_daemon_request_name (test->dbus,
          TP_ACCOUNT_MANAGER_BUS_NAME, FALSE, &test->error);
  g_assert_no_error (test->error);

  /* Create service-side Client object */
  test->simple_client = simple_client_new (test->dbus, "Test", FALSE);
  g_assert (test->simple_client != NULL);
  test->base_client = TP_BASE_CLIENT (test->simple_client);

  /* Create service-side Account object */
  test->account_service = test_object_new_static_class (SIMPLE_TYPE_ACCOUNT,
      NULL);
  tp_dbus_daemon_register_object (test->dbus, ACCOUNT_PATH,
      test->account_service);

  /* Create client-side Client object */
  test->client = test_object_new_static_class (TP_TYPE_CLIENT,
          "dbus-daemon", test->dbus,
          "bus-name", tp_base_client_get_bus_name (test->base_client),
          "object-path", tp_base_client_get_object_path (test->base_client),
          NULL);

  g_assert (test->client != NULL);

  /* Create client-side Account object */
  test->account = tp_account_new (test->dbus, ACCOUNT_PATH, NULL);
  g_assert (test->account != NULL);

  /* Create (service and client sides) connection objects */
  test_create_and_connect_conn (SIMPLE_TYPE_CONNECTION, "me@test.com",
      &test->base_connection, &test->connection);

  /* Create service-side text channel object */
  chan_path = g_strdup_printf ("%s/Channel",
      tp_proxy_get_object_path (test->connection));

  contact_repo = tp_base_connection_get_handles (test->base_connection,
      TP_HANDLE_TYPE_CONTACT);
  g_assert (contact_repo != NULL);

  handle = tp_handle_ensure (contact_repo, "bob", NULL, &test->error);
  g_assert_no_error (test->error);

  test->text_chan_service = TEST_TEXT_CHANNEL_NULL (
      test_object_new_static_class (
        TEST_TYPE_TEXT_CHANNEL_NULL,
        "connection", test->base_connection,
        "object-path", chan_path,
        "handle", handle,
        NULL));

  /* Create client-side text channel object */
  test->text_chan = tp_channel_new (test->connection, chan_path, NULL,
      TP_HANDLE_TYPE_CONTACT, handle, &test->error);
  g_assert_no_error (test->error);

  tp_handle_unref (contact_repo, handle);

  g_free (chan_path);
}

static void
teardown (Test *test,
          gconstpointer data)
{
  g_clear_error (&test->error);

  g_strfreev (test->interfaces);

  g_object_unref (test->base_client);
  g_object_unref (test->client);

  tp_dbus_daemon_unregister_object (test->dbus, test->account_service);
  g_object_unref (test->account_service);

  tp_dbus_daemon_release_name (test->dbus, TP_ACCOUNT_MANAGER_BUS_NAME,
      &test->error);
  g_assert_no_error (test->error);

  g_object_unref (test->dbus);
  test->dbus = NULL;
  g_main_loop_unref (test->mainloop);
  test->mainloop = NULL;

  g_object_unref (test->account);

  g_object_unref (test->text_chan_service);
  g_object_unref (test->text_chan);

  tp_cli_connection_run_disconnect (test->connection, -1, &test->error, NULL);
  g_assert_no_error (test->error);

  g_object_unref (test->connection);
  g_object_unref (test->base_connection);
}

/* Test Basis */

static void
test_basics (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  TpDBusDaemon *dbus;
  gchar *name;
  gboolean unique;

  g_object_get (test->base_client,
      "dbus-daemon", &dbus,
      "name", &name,
      "uniquify-name", &unique,
      NULL);

  g_assert (test->dbus == dbus);
  g_assert_cmpstr ("Test", ==, name);
  g_assert (!unique);

  g_object_unref (dbus);
  g_free (name);
}

/* Test register */

static void
get_client_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 1);

  test->interfaces = g_strdupv ((GStrv) tp_asv_get_strv (
        properties, "Interfaces"));

out:
  g_main_loop_quit (test->mainloop);
}

/* Test Observer */
static void
check_filters (GPtrArray *filters)
{
  GHashTable *filter;

  g_assert (filters != NULL);
  g_assert_cmpuint (filters->len, ==, 2);

  filter = g_ptr_array_index (filters, 0);
  g_assert_cmpuint (g_hash_table_size (filter), ==, 1);
  g_assert_cmpstr (tp_asv_get_string (filter, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_TEXT);

  filter = g_ptr_array_index (filters, 1);
  g_assert_cmpuint (g_hash_table_size (filter), ==, 2);
  g_assert_cmpstr (tp_asv_get_string (filter, TP_PROP_CHANNEL_CHANNEL_TYPE), ==,
      TP_IFACE_CHANNEL_TYPE_STREAM_TUBE);
  g_assert_cmpuint (tp_asv_get_uint32 (filter,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, NULL), ==, TP_HANDLE_TYPE_CONTACT);
}

static void
get_observer_prop_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;
  GPtrArray *filters;
  gboolean recover;
  gboolean valid;

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

  g_assert_cmpint (g_hash_table_size (properties), == , 2);

  filters = tp_asv_get_boxed (properties, "ObserverChannelFilter",
      TP_ARRAY_TYPE_CHANNEL_CLASS_LIST);
  check_filters (filters);

  recover = tp_asv_get_boolean (properties, "Recover", &valid);
  g_assert (valid);
  g_assert (recover);

out:
  g_main_loop_quit (test->mainloop);
}

static void
no_return_cb (TpClient *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  Test *test = user_data;

  g_clear_error (&test->error);

  if (error != NULL)
    {
      test->error = g_error_copy (error);
      goto out;
    }

out:
  g_main_loop_quit (test->mainloop);
}

static void
add_channel_to_ptr_array (GPtrArray *arr,
    TpChannel *channel)
{
  GValueArray *tmp;

  g_assert (arr != NULL);
  g_assert (channel != NULL);

  tmp = tp_value_array_build (2,
      DBUS_TYPE_G_OBJECT_PATH, tp_proxy_get_object_path (channel),
      TP_HASH_TYPE_STRING_VARIANT_MAP, tp_channel_borrow_immutable_properties (
        channel),
      G_TYPE_INVALID);

  g_ptr_array_add (arr, tmp);
}

static void
free_channel_details (gpointer data,
    gpointer user_data)
{
  g_boxed_free (TP_STRUCT_TYPE_CHANNEL_DETAILS, data);
}

static void
test_observer (Test *test,
    gconstpointer data G_GNUC_UNUSED)
{
  GHashTable *filter;
  GPtrArray *channels, *requests_satisified;
  GHashTable *info;

  filter = tp_asv_new (
      TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING, TP_IFACE_CHANNEL_TYPE_TEXT,
      NULL);

  tp_base_client_add_observer_filter (test->base_client, filter);
  g_hash_table_unref (filter);

  tp_base_client_take_observer_filter (test->base_client, tp_asv_new (
        TP_PROP_CHANNEL_CHANNEL_TYPE, G_TYPE_STRING,
          TP_IFACE_CHANNEL_TYPE_STREAM_TUBE,
        TP_PROP_CHANNEL_TARGET_HANDLE_TYPE, G_TYPE_UINT,
          TP_HANDLE_TYPE_CONTACT,
        NULL));

  tp_base_client_set_observer_recover (test->base_client, TRUE);

  tp_base_client_register (test->base_client, &test->error);
  g_assert_no_error (test->error);

  /* Check Client properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT, get_client_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);
  g_assert_cmpint (g_strv_length (test->interfaces), ==, 1);
  g_assert (tp_strv_contains ((const gchar * const *) test->interfaces,
        TP_IFACE_CLIENT_OBSERVER));

  /* Check Observer properties */
  tp_cli_dbus_properties_call_get_all (test->client, -1,
      TP_IFACE_CLIENT_OBSERVER, get_observer_prop_cb, test, NULL, NULL);
  g_main_loop_run (test->mainloop);

  g_assert_no_error (test->error);

  /* Call ObserveChannels */
  channels = g_ptr_array_sized_new (1);
  add_channel_to_ptr_array (channels, test->text_chan);

  requests_satisified = g_ptr_array_sized_new (0);
  info = tp_asv_new (
      "recovering", G_TYPE_BOOLEAN, TRUE,
      NULL);

  tp_proxy_add_interface_by_id (TP_PROXY (test->client),
      TP_IFACE_QUARK_CLIENT_OBSERVER);

  tp_cli_client_observer_call_observe_channels (test->client, -1,
      tp_proxy_get_object_path (test->account),
      tp_proxy_get_object_path (test->connection),
      channels, "/", requests_satisified, info,
      no_return_cb, test, NULL, NULL);

  g_main_loop_run (test->mainloop);
  g_assert_no_error (test->error);

  g_assert (test->simple_client->observe_ctx != NULL);
  g_assert (tp_observe_channels_context_is_recovering (
        test->simple_client->observe_ctx));

  /* Now call it with an invalid argument */
  tp_asv_set_boolean (info, "FAIL", TRUE);

  tp_cli_client_observer_call_observe_channels (test->client, -1,
      tp_proxy_get_object_path (test->account),
      tp_proxy_get_object_path (test->connection),
      channels, "/", requests_satisified, info,
      no_return_cb, test, NULL, NULL);

  g_main_loop_run (test->mainloop);
  g_assert_error (test->error, TP_ERRORS, TP_ERROR_INVALID_ARGUMENT);

  g_ptr_array_foreach (channels, free_channel_details, NULL);
  g_ptr_array_free (channels, TRUE);
  g_ptr_array_free (requests_satisified, TRUE);
  g_hash_table_unref (info);
}

int
main (int argc,
      char **argv)
{
  g_type_init ();
  tp_debug_set_flags ("all");

  g_test_init (&argc, &argv, NULL);
  g_test_bug_base ("http://bugs.freedesktop.org/show_bug.cgi?id=");

  g_test_add ("/base-client/basics", Test, NULL, setup, test_basics, teardown);
  g_test_add ("/base-client/observer", Test, NULL, setup, test_observer,
      teardown);

  return g_test_run ();
}