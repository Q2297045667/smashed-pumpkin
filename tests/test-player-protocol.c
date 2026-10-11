#include "player-protocol.h"
#include "window-protocol.h"

static void
test_headers(void)
{
  const char *lines[] = {
    "[08.10.2026 13:00:00] [INFO] [pumpkin::world] Rotstein joined the game",
    "2026-10-08T11:00:00.123Z INFO pumpkin::world: Rotstein joined the game",
    "\033[32m[INFO]\033[0m [pumpkin::world] Rotstein joined the game",
    "\033[2m19:06:40\033[0m \033[32m INFO\033[0m Rotstein joined the game",
    "19:06:40.123 INFO pumpkin::world: Rotstein joined the game",
    "Rotstein joined the game"
  };
  for (guint i = 0; i < G_N_ELEMENTS(lines); i++) {
    g_autofree char *message = pumpkin_player_log_message(lines[i]);
    g_assert_cmpstr(message, ==, "Rotstein joined the game");
    g_autofree char *name = pumpkin_player_name_before_suffix(message, " joined the game");
    g_assert_cmpstr(name, ==, "Rotstein");
  }
}

static void
test_names(void)
{
  g_autofree char *name = pumpkin_player_name_before_suffix("Some Player left the game", " left the game");
  g_assert_cmpstr(name, ==, "Some Player");
  g_autofree char *renamed = pumpkin_player_name_before_suffix(
    "NewName (formerly known as OldName) joined the game", " joined the game");
  g_assert_cmpstr(renamed, ==, "NewName");
  g_assert_null(pumpkin_player_name_before_suffix("<Someone> Fake joined the game", " joined the game"));
  g_assert_null(pumpkin_player_name_before_suffix("world] Fake joined the game", " joined the game"));
  g_assert_null(pumpkin_player_name_before_suffix("Someone joined the game then failed", " joined the game"));
  g_assert_true(pumpkin_player_name_is_valid("JavaBuilder"));
  g_assert_true(pumpkin_player_name_is_valid(".Some_Player"));
  g_assert_true(pumpkin_player_name_is_valid("Some Player"));
  g_assert_false(pumpkin_player_name_is_valid("bad\nname"));
}

static void
test_list_entries(void)
{
  g_autofree char *name = NULL;
  g_autofree char *uuid = NULL;
  g_assert_true(pumpkin_player_parse_list_entry(
    " Some Player (069A79F4-44E9-4726-A5BE-FCA90E38AAF5) ", &name, &uuid));
  g_assert_cmpstr(name, ==, "Some Player");
  g_assert_cmpstr(uuid, ==, "069a79f4-44e9-4726-a5be-fca90e38aaf5");
  g_clear_pointer(&name, g_free);
  g_clear_pointer(&uuid, g_free);
  g_assert_true(pumpkin_player_parse_list_entry("PlainName", &name, &uuid));
  g_assert_null(uuid);
  g_clear_pointer(&name, g_free);
  g_assert_false(pumpkin_player_parse_list_entry("BadName (not-a-uuid)", &name, &uuid));
  g_assert_null(name);
  g_assert_null(uuid);
  g_assert_false(pumpkin_player_parse_list_entry("", &name, &uuid));
}

static void
test_snapshots(void)
{
  int count = -1;
  g_autofree char *names = NULL;
  g_assert_true(pumpkin_parse_player_list_snapshot_line(
    "There are 1 of a max of 20 players online: Some Player (069a79f4-44e9-4726-a5be-fca90e38aaf5)",
    &count, &names));
  g_assert_cmpint(count, ==, 1);
  g_assert_nonnull(names);
  g_clear_pointer(&names, g_free);
  g_assert_true(pumpkin_parse_player_list_snapshot_line(
    "There are 0 of a max of 20 players online:", &count, &names));
  g_assert_cmpint(count, ==, 0);
  g_assert_null(names);
  g_assert_false(pumpkin_parse_player_list_snapshot_line(
    "<Someone> There are 0 of a max of 20 players online:", &count, &names));
  g_assert_false(pumpkin_parse_player_list_snapshot_line(
    "There are 99999999999999999999 of a max of 20 players online:", &count, &names));
  g_assert_false(pumpkin_parse_player_list_snapshot_line(
    "There are 99999999999999999999 of a max of 20 players online:", NULL, NULL));
}

int
main(int argc, char **argv)
{
  g_test_init(&argc, &argv, NULL);
  g_test_add_func("/players/log-headers", test_headers);
  g_test_add_func("/players/names-and-chat", test_names);
  g_test_add_func("/players/list-identities", test_list_entries);
  g_test_add_func("/players/list-snapshots", test_snapshots);
  return g_test_run();
}
