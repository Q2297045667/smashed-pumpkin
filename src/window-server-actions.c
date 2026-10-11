#include "window-server-actions.h"
#include "platform.h"

static void
set_action(GtkButton *button, gboolean visible, gboolean enabled)
{
  gtk_widget_set_visible(GTK_WIDGET(button), visible);
  gtk_widget_set_sensitive(GTK_WIDGET(button), enabled);
}

void
pumpkin_window_update_server_actions(PumpkinWindow *self, gboolean installed,
                                     gboolean update_available, gboolean busy,
                                     gboolean running)
{
  gboolean available = self->current != NULL && pumpkin_platform_supports_local_hosting();
  busy = busy || (available && pumpkin_server_get_maintenance(self->current));
  gboolean stopping = available && pumpkin_server_get_stopping(self->current);
  gboolean force_available = stopping && pumpkin_server_get_stop_timed_out(self->current);
  set_action(self->btn_details_install, available && !installed, !busy && !running);
  set_action(self->btn_details_update, available && installed && update_available, !busy && !stopping);
  set_action(self->btn_details_start, available && installed && !running,
             !busy && !stopping);
  set_action(self->btn_details_stop, available && running,
             force_available || (!stopping && self->ui_state == UI_STATE_RUNNING));
  set_action(self->btn_details_restart, available && running && !stopping,
             !busy && self->restart_delay_id == 0);
  gtk_button_set_label(self->btn_details_stop,
                        force_available ? "Force Stop..." : (stopping ? "Stopping..." : "Stop"));
  if (force_available) {
    gtk_widget_add_css_class(GTK_WIDGET(self->btn_details_stop), "destructive-action");
  } else {
    gtk_widget_remove_css_class(GTK_WIDGET(self->btn_details_stop), "destructive-action");
  }
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->btn_details_stop),
    force_available ? "Force termination can lose unsaved data" : "Save data and stop the server");
  gtk_widget_set_tooltip_text(GTK_WIDGET(self->btn_details_update),
    running ? "Download first, then safely stop, install and restart" : "Update the installed server");
  gtk_widget_set_sensitive(GTK_WIDGET(self->btn_remove_server), !running && !busy);
}
