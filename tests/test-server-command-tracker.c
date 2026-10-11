#include "server-command-tracker.h"

static void
test_server_isolation(void)
{
  g_autoptr(GObject) first = g_object_new(G_TYPE_OBJECT, NULL);
  g_autoptr(GObject) second = g_object_new(G_TYPE_OBJECT, NULL);
  for (guint i = 0; i < 4; i++) {
    pumpkin_command_track(first, "list uuids", TRUE, 1000);
  }
  g_assert_false(pumpkin_command_poll_ready(first, "list uuids", 2000));
  g_assert_true(pumpkin_command_poll_ready(second, "list uuids", 2000));
  g_assert_false(pumpkin_command_consume_reply(second, "list"));
  for (guint i = 0; i < 4; i++) {
    g_assert_true(pumpkin_command_consume_reply(first, "list"));
  }
  g_assert_true(pumpkin_command_poll_ready(first, "list uuids", 2000));
  g_assert_false(pumpkin_command_consume_reply(first, "list"));
}

static void
test_manual_order(void)
{
  g_autoptr(GObject) server = g_object_new(G_TYPE_OBJECT, NULL);
  pumpkin_command_track(server, "list uuids", TRUE, 1000);
  pumpkin_command_track(server, "list", FALSE, 2000);
  pumpkin_command_track(server, "tps", FALSE, 2000);
  g_assert_false(pumpkin_command_poll_ready(server, "list", 3000));
  g_assert_true(pumpkin_command_consume_reply(server, "list"));
  g_assert_false(pumpkin_command_poll_ready(server, "list", 3000));
  g_assert_false(pumpkin_command_consume_reply(server, "list"));
  g_assert_false(pumpkin_command_consume_reply(server, "tps"));
  g_assert_true(pumpkin_command_poll_ready(server, "list", 3000));
  pumpkin_command_track(server, "list", FALSE, 3000);
  pumpkin_command_track(server, "list uuids", TRUE, 3000);
  g_assert_false(pumpkin_command_consume_reply(server, "list"));
  g_assert_true(pumpkin_command_consume_reply(server, "list"));
}

static void
test_timeout_and_session(void)
{
  g_autoptr(GObject) server = g_object_new(G_TYPE_OBJECT, NULL);
  pumpkin_command_track(server, "tps", TRUE, 1000);
  g_assert_false(pumpkin_command_poll_ready(server, "tps", 1000 + 29 * G_USEC_PER_SEC));
  g_assert_true(pumpkin_command_poll_ready(server, "tps", 1000 + 30 * G_USEC_PER_SEC));
  pumpkin_command_track(server, "list uuids", TRUE, 1000);
  pumpkin_command_tracking_reset(server);
  g_assert_false(pumpkin_command_consume_reply(server, "list"));
  g_assert_false(pumpkin_command_poll_ready(server, "say list", 1000));
  pumpkin_command_track(server, "say tps", TRUE, 1000);
  g_assert_false(pumpkin_command_consume_reply(server, "tps"));
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/commands/server-isolation", test_server_isolation);
  g_test_add_func("/commands/manual-order", test_manual_order);
  g_test_add_func("/commands/timeout-and-session", test_timeout_and_session);
  return g_test_run();
}
