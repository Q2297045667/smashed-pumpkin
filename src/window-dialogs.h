#pragma once

#include <adwaita.h>

void pumpkin_window_enable_dialog_dismissal(GtkWindow *window);
gboolean pumpkin_window_dismiss_dialog_at(GtkWindow *window, double x, double y);
