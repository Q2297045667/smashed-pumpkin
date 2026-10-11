#include "window-startup.h"
#include "config.h"
#include "platform.h"

#ifdef HAVE_STARTUP_X11
#include <gdk/x11/gdkx.h>
#include <X11/Xlib.h>
#endif
#ifdef G_OS_WIN32
#include <gdk/win32/gdkwin32.h>
#include <windows.h>
#endif

char *
pumpkin_monitor_id(GdkMonitor *monitor)
{
  const char *connector = gdk_monitor_get_connector(monitor);
  if (connector != NULL && *connector != '\0') {
    return g_strdup_printf("connector:%s", connector);
  }
  GdkRectangle rect;
  gdk_monitor_get_geometry(monitor, &rect);
  return g_strdup_printf("display:%s:%s:%d:%d:%d:%d",
                          gdk_monitor_get_manufacturer(monitor) != NULL ? gdk_monitor_get_manufacturer(monitor) : "",
                          gdk_monitor_get_model(monitor) != NULL ? gdk_monitor_get_model(monitor) : "",
                          rect.x, rect.y, rect.width, rect.height);
}

gboolean
pumpkin_startup_can_place_maximized(GdkDisplay *display)
{
  (void)display;
#ifdef HAVE_STARTUP_X11
  if (GDK_IS_X11_DISPLAY(display)) {
    return TRUE;
  }
#endif
#ifdef G_OS_WIN32
  if (GDK_IS_WIN32_DISPLAY(display)) {
    return TRUE;
  }
#endif
  return FALSE;
}

static GdkMonitor *
find_monitor(GdkDisplay *display, const char *id)
{
  GListModel *monitors = gdk_display_get_monitors(display);
  for (guint i = 0; id != NULL && *id != '\0' && i < g_list_model_get_n_items(monitors); i++) {
    GdkMonitor *monitor = g_list_model_get_item(monitors, i);
    g_autofree char *candidate = pumpkin_monitor_id(monitor);
    if (gdk_monitor_is_valid(monitor) && g_strcmp0(id, candidate) == 0) {
      return monitor;
    }
    g_object_unref(monitor);
  }
  return NULL;
}

#ifdef G_OS_WIN32
typedef struct {
  GdkRectangle geometry;
  int scale;
  HWND window;
} MonitorPlacement;

static BOOL CALLBACK
place_on_windows_monitor(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM user_data)
{
  (void)dc;
  (void)rect;
  MonitorPlacement *placement = (MonitorPlacement *)user_data;
  MONITORINFO info = {0};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    return TRUE;
  }
  /* GDK's Win32 geometry divides native monitor coordinates by its scale. */
  int scale = placement->scale;
  GdkRectangle *geometry = &placement->geometry;
  if (info.rcMonitor.left / scale != geometry->x || info.rcMonitor.top / scale != geometry->y ||
      (info.rcMonitor.right - info.rcMonitor.left) / scale != geometry->width ||
      (info.rcMonitor.bottom - info.rcMonitor.top) / scale != geometry->height) {
    return TRUE;
  }
  RECT window_rect;
  if (GetWindowRect(placement->window, &window_rect)) {
    int x = info.rcWork.left + MAX(0, (info.rcWork.right - info.rcWork.left -
                                     (window_rect.right - window_rect.left)) / 2);
    int y = info.rcWork.top + MAX(0, (info.rcWork.bottom - info.rcWork.top -
                                    (window_rect.bottom - window_rect.top)) / 2);
    SetWindowPos(placement->window, NULL, x, y, 0, 0,
                  SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  return FALSE;
}
#endif

static void
place_before_maximizing(GtkWindow *window, GdkMonitor *monitor)
{
  (void)window;
  (void)monitor;
#ifdef HAVE_STARTUP_X11
  if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(GTK_WIDGET(window)))) {
    gtk_widget_realize(GTK_WIDGET(window));
    GdkSurface *surface = gtk_native_get_surface(GTK_NATIVE(window));
    GdkRectangle rect;
    gdk_monitor_get_geometry(monitor, &rect);
    int width, height;
    gtk_window_get_default_size(window, &width, &height);
    int scale = gdk_monitor_get_scale_factor(monitor);
    /* GTK deprecated its X11 backend, but supported X11 builds still need
     * native placement: GTK has no portable maximized-on-monitor API. */
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    Display *display = gdk_x11_display_get_xdisplay(gdk_surface_get_display(surface));
    XEvent event = {0};
    event.xclient.type = ClientMessage;
    event.xclient.window = gdk_x11_surface_get_xid(surface);
    event.xclient.message_type = XInternAtom(display, "_NET_MOVERESIZE_WINDOW", False);
    event.xclient.format = 32;
    /* Static gravity, x/y present, source = application (EWMH 4.2). */
    event.xclient.data.l[0] = StaticGravity | (1 << 8) | (1 << 9) | (1 << 12);
    event.xclient.data.l[1] = (rect.x + MAX(0, (rect.width - width) / 2)) * scale;
    event.xclient.data.l[2] = (rect.y + MAX(0, (rect.height - height) / 2)) * scale;
    XSendEvent(display, DefaultRootWindow(display), False,
                SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XFlush(display);
    G_GNUC_END_IGNORE_DEPRECATIONS
  }
#endif
#ifdef G_OS_WIN32
  if (GDK_IS_WIN32_DISPLAY(gtk_widget_get_display(GTK_WIDGET(window)))) {
    gtk_widget_realize(GTK_WIDGET(window));
    MonitorPlacement placement = {0};
    gdk_monitor_get_geometry(monitor, &placement.geometry);
    placement.scale = gdk_monitor_get_scale_factor(monitor);
    placement.window = gdk_win32_surface_get_handle(gtk_native_get_surface(GTK_NATIVE(window)));
    EnumDisplayMonitors(NULL, NULL, place_on_windows_monitor, (LPARAM)&placement);
  }
#endif
}

#ifdef HAVE_STARTUP_X11
static void
release_monitor(gpointer data, GClosure *closure)
{
  (void)closure;
  g_object_unref(data);
}

static void
maximize_after_map(GtkWindow *window, GdkMonitor *monitor)
{
  g_autoptr(GdkMonitor) retained = g_object_ref(monitor);
  g_signal_handlers_disconnect_by_func(window, maximize_after_map, monitor);
  if (gdk_monitor_is_valid(retained)) {
    place_before_maximizing(window, retained);
  }
  gtk_window_maximize(window);
}
#endif

static gboolean
leave_fullscreen(GtkWidget *widget, GVariant *args, gpointer user_data)
{
  (void)args;
  (void)user_data;
  if (!gtk_window_is_fullscreen(GTK_WINDOW(widget))) {
    return FALSE;
  }
  gtk_window_unfullscreen(GTK_WINDOW(widget));
  return TRUE;
}

void
pumpkin_window_apply_startup(GtkWindow *window, PumpkinConfig *config)
{
  if (!pumpkin_platform_supports_desktop_integrations() ||
      !pumpkin_config_should_expand_on_startup(config)) {
    return;
  }
  g_autoptr(GdkMonitor) monitor = find_monitor(gtk_widget_get_display(GTK_WIDGET(window)),
                                              pumpkin_config_get_start_monitor(config));
  if (pumpkin_config_get_start_mode(config) == PUMPKIN_START_FULLSCREEN) {
    if (monitor != NULL) {
      gtk_window_fullscreen_on_monitor(window, monitor);
    } else {
      gtk_window_fullscreen(window);
    }
    GtkEventController *shortcuts = gtk_shortcut_controller_new();
    gtk_shortcut_controller_set_scope(GTK_SHORTCUT_CONTROLLER(shortcuts), GTK_SHORTCUT_SCOPE_GLOBAL);
    gtk_shortcut_controller_add_shortcut(GTK_SHORTCUT_CONTROLLER(shortcuts),
      gtk_shortcut_new(gtk_keyval_trigger_new(GDK_KEY_F11, 0),
                       gtk_callback_action_new(leave_fullscreen, NULL, NULL)));
    gtk_widget_add_controller(GTK_WIDGET(window), shortcuts);
  } else {
    if (monitor != NULL && pumpkin_startup_can_place_maximized(gtk_widget_get_display(GTK_WIDGET(window)))) {
#ifdef HAVE_STARTUP_X11
      if (GDK_IS_X11_DISPLAY(gtk_widget_get_display(GTK_WIDGET(window))) &&
          !gtk_widget_get_mapped(GTK_WIDGET(window))) {
        /* A window manager can overwrite pre-map placement. Request movement
         * once it manages the window, before requesting maximization. */
        g_signal_connect_data(window, "map", G_CALLBACK(maximize_after_map),
                               g_object_ref(monitor), release_monitor, G_CONNECT_AFTER);
        return;
      }
#endif
      place_before_maximizing(window, monitor);
    }
    gtk_window_maximize(window);
  }
}
