#pragma once

#include <gtk/gtk.h>

void pumpkin_refresh_button_setup(GtkButton *button);
void pumpkin_refresh_button_set_label(GtkButton *button, const char *label);
gboolean pumpkin_refresh_button_begin(GtkButton *button);
gboolean pumpkin_refresh_button_is_animating(GtkButton *button);
