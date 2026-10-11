#include "window-dialogs.h"

gboolean
pumpkin_window_dismiss_dialog_at(GtkWindow *window, double x, double y)
{
  AdwDialog *dialog = NULL;
  if (ADW_IS_APPLICATION_WINDOW(window))
    dialog = adw_application_window_get_visible_dialog(ADW_APPLICATION_WINDOW(window));
  else if (ADW_IS_WINDOW(window))
    dialog = adw_window_get_visible_dialog(ADW_WINDOW(window));
  if (dialog == NULL || !adw_dialog_get_can_close(dialog)) return FALSE;
  GtkWidget *child = adw_dialog_get_child(dialog);
  graphene_rect_t bounds;
  if (child == NULL || !gtk_widget_compute_bounds(child, GTK_WIDGET(window), &bounds)) return FALSE;
  graphene_point_t point = GRAPHENE_POINT_INIT((float)x, (float)y);
  if (graphene_rect_contains_point(&bounds, &point)) return FALSE;
  /* Menus and entry context menus may extend beyond the dialog's rectangle. */
  GtkWidget *picked = gtk_widget_pick(GTK_WIDGET(window), x, y, GTK_PICK_DEFAULT);
  for (GtkWidget *w = picked; w != NULL; w = gtk_widget_get_parent(w))
    if (GTK_IS_POPOVER(w)) return FALSE;
  return adw_dialog_close(dialog);
}

static void
on_window_pressed(GtkGestureClick *gesture, int presses, double x, double y, GtkWindow *window)
{
  (void)presses;
  if (pumpkin_window_dismiss_dialog_at(window, x, y))
    gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

void
pumpkin_window_enable_dialog_dismissal(GtkWindow *window)
{
  GtkGesture *click = gtk_gesture_click_new();
  gtk_event_controller_set_name(GTK_EVENT_CONTROLLER(click), "smashed-dialog-backdrop");
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), GDK_BUTTON_PRIMARY);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
  /* AdwDialog sets GtkWidget:limit-events on recent GTK. Without NONE the
   * window controller never sees clicks on the modal backdrop. */
  gtk_event_controller_set_propagation_limit(GTK_EVENT_CONTROLLER(click), GTK_LIMIT_NONE);
  g_signal_connect(click, "pressed", G_CALLBACK(on_window_pressed), window);
  gtk_widget_add_controller(GTK_WIDGET(window), GTK_EVENT_CONTROLLER(click));
}
