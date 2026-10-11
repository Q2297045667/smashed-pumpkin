#include "refresh-button.h"

#define REFRESH_DURATION_MSEC 500

typedef struct {
  GtkWidget parent_instance;
  GtkWidget *image;
  guint tick;
  gint64 started;
  float angle;
} PumpkinRefreshIcon;

typedef GtkWidgetClass PumpkinRefreshIconClass;
G_DEFINE_TYPE(PumpkinRefreshIcon, pumpkin_refresh_icon, GTK_TYPE_WIDGET)

static void
icon_measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
             int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
  PumpkinRefreshIcon *icon = (PumpkinRefreshIcon *)widget;
  gtk_widget_measure(icon->image, orientation, for_size,
                     minimum, natural, minimum_baseline, natural_baseline);
}

static void
icon_allocate(GtkWidget *widget, int width, int height, int baseline)
{
  PumpkinRefreshIcon *icon = (PumpkinRefreshIcon *)widget;
  gtk_widget_allocate(icon->image, width, height, baseline, NULL);
}

static void
icon_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
  PumpkinRefreshIcon *icon = (PumpkinRefreshIcon *)widget;
  graphene_point_t center = GRAPHENE_POINT_INIT(gtk_widget_get_width(widget) / 2.0f,
                                               gtk_widget_get_height(widget) / 2.0f);
  graphene_point_t origin = GRAPHENE_POINT_INIT(-center.x, -center.y);
  gtk_snapshot_save(snapshot);
  gtk_snapshot_translate(snapshot, &center);
  gtk_snapshot_rotate(snapshot, icon->angle);
  gtk_snapshot_translate(snapshot, &origin);
  gtk_widget_snapshot_child(widget, icon->image, snapshot);
  gtk_snapshot_restore(snapshot);
}

static void
icon_dispose(GObject *object)
{
  PumpkinRefreshIcon *icon = (PumpkinRefreshIcon *)object;
  if (icon->tick != 0) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(icon), icon->tick);
    icon->tick = 0;
  }
  if (icon->image != NULL) {
    gtk_widget_unparent(icon->image);
    icon->image = NULL;
  }
  G_OBJECT_CLASS(pumpkin_refresh_icon_parent_class)->dispose(object);
}

static void
pumpkin_refresh_icon_class_init(PumpkinRefreshIconClass *klass)
{
  G_OBJECT_CLASS(klass)->dispose = icon_dispose;
  klass->measure = icon_measure;
  klass->size_allocate = icon_allocate;
  klass->snapshot = icon_snapshot;
  gtk_widget_class_set_css_name(klass, "refresh-icon");
  gtk_widget_class_set_accessible_role(klass, GTK_ACCESSIBLE_ROLE_PRESENTATION);
}

static void
pumpkin_refresh_icon_init(PumpkinRefreshIcon *icon)
{
  gtk_widget_set_can_target(GTK_WIDGET(icon), FALSE);
  icon->image = gtk_image_new_from_icon_name("view-refresh-symbolic");
  gtk_widget_set_can_target(icon->image, FALSE);
  gtk_image_set_pixel_size(GTK_IMAGE(icon->image), 16);
  gtk_widget_set_parent(icon->image, GTK_WIDGET(icon));
}

static gboolean
rotate_icon(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
  (void)data;
  PumpkinRefreshIcon *icon = (PumpkinRefreshIcon *)widget;
  double progress = CLAMP((gdk_frame_clock_get_frame_time(clock) - icon->started) /
                          (REFRESH_DURATION_MSEC * 1000.0), 0.0, 1.0);
  double eased = progress * progress * (3.0 - 2.0 * progress);
  icon->angle = (float)(360.0 * eased);
  gtk_widget_queue_draw(widget);
  return G_SOURCE_CONTINUE;
}

static PumpkinRefreshIcon *find_icon(GtkWidget *widget)
{
  if (widget == NULL) return NULL;
  if (G_TYPE_CHECK_INSTANCE_TYPE(widget, pumpkin_refresh_icon_get_type())) return (PumpkinRefreshIcon *)widget;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    PumpkinRefreshIcon *icon = find_icon(child);
    if (icon != NULL) return icon;
  }
  return NULL;
}

static gboolean
finish_refresh(gpointer data)
{
  GtkButton *button = GTK_BUTTON(data);
  PumpkinRefreshIcon *icon = find_icon(gtk_button_get_child(button));
  if (icon != NULL) {
    GtkWidget *child = GTK_WIDGET(icon);
    if (icon->tick != 0) gtk_widget_remove_tick_callback(child, icon->tick);
    icon->tick = 0;
    icon->angle = 0;
    gtk_widget_queue_draw(child);
  }
  gboolean was_targetable = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "refresh-was-targetable"));
  g_object_set_data(G_OBJECT(button), "refresh-animating", NULL);
  gtk_widget_set_can_target(GTK_WIDGET(button), was_targetable);
  return G_SOURCE_REMOVE;
}

void
pumpkin_refresh_button_setup(GtkButton *button)
{
  GtkWidget *child = gtk_button_get_child(button);
  if (find_icon(child) != NULL) return;
  g_autofree char *label = g_strdup(gtk_button_get_label(button));
  GtkWidget *icon = g_object_new(pumpkin_refresh_icon_get_type(), NULL);
  if (label == NULL) gtk_button_set_child(button, icon);
  else {
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_can_target(content, FALSE);
    gtk_widget_set_halign(content, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(content), icon);
    GtkWidget *text = gtk_label_new(label);
    gtk_widget_set_can_target(text, FALSE);
    gtk_box_append(GTK_BOX(content), text);
    gtk_button_set_child(button, content);
    gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
  }
}

gboolean
pumpkin_refresh_button_is_animating(GtkButton *button)
{
  return g_object_get_data(G_OBJECT(button), "refresh-animating") != NULL;
}

void
pumpkin_refresh_button_set_label(GtkButton *button, const char *label)
{
  pumpkin_refresh_button_setup(button);
  GtkWidget *box = gtk_button_get_child(button);
  for (GtkWidget *child = gtk_widget_get_first_child(box); child != NULL;
       child = gtk_widget_get_next_sibling(child)) {
    if (GTK_IS_LABEL(child)) {
      gtk_label_set_text(GTK_LABEL(child), label);
      gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
      return;
    }
  }
}

gboolean
pumpkin_refresh_button_begin(GtkButton *button)
{
  if (pumpkin_refresh_button_is_animating(button)) return FALSE;
  pumpkin_refresh_button_setup(button);
  PumpkinRefreshIcon *icon = find_icon(gtk_button_get_child(button));
  gboolean animations = TRUE;
  g_object_get(gtk_widget_get_settings(GTK_WIDGET(button)), "gtk-enable-animations", &animations, NULL);
  icon->started = g_get_monotonic_time();
  if (animations) {
    icon->tick = gtk_widget_add_tick_callback(GTK_WIDGET(icon), rotate_icon, NULL, NULL);
  }
  g_object_set_data(G_OBJECT(button), "refresh-animating", GINT_TO_POINTER(1));
  g_object_set_data(G_OBJECT(button), "refresh-was-targetable",
    GINT_TO_POINTER(gtk_widget_get_can_target(GTK_WIDGET(button))));
  /* Preserve normal styling. Callback guards also reject keyboard activation. */
  gtk_widget_set_can_target(GTK_WIDGET(button), FALSE);
  g_timeout_add_full(G_PRIORITY_DEFAULT, REFRESH_DURATION_MSEC, finish_refresh,
                     g_object_ref(button), g_object_unref);
  return TRUE;
}
