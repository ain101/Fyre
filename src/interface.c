/*
 * interface.c - Implements the GTK+ user interface. Code in this module isn't
 *               used when rendering from the command line.
 *
 * de Jong Explorer - interactive exploration of the Peter de Jong attractor
 * Copyright (C) 2004 David Trowbridge and Micah Dowty
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#include <gtk/gtk.h>
#include <glade/glade.h>
#include "main.h"
#include "color-button.h"

struct {
  GladeXML *xml;
  GtkWidget *window;

  GtkWidget *drawing_area;
  GdkGC *gc;

  GtkStatusbar *statusbar;
  guint render_status_message_id;
  guint render_status_context;

  guint idler;
} gui;

static int limit_update_rate(float max_rate);
static int auto_limit_update_rate(void);
static void update_gui();
static void update_drawing_area();
static int interactive_idle_handler(gpointer user_data);
static float generate_random_param();

gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data);
gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data);
void on_start_clicked(GtkWidget *widget, gpointer user_data);
void on_stop_clicked(GtkWidget *widget, gpointer user_data);
void on_param_spinner_changed(GtkWidget *widget, gpointer user_data);
void on_render_spinner_changed(GtkWidget *widget, gpointer user_data);
void on_color_changed(GtkWidget *widget, gpointer user_data);
void on_random_clicked(GtkWidget *widget, gpointer user_data);
void on_save_clicked(GtkWidget *widget, gpointer user_data);
GtkWidget *custom_color_button_new(gchar *widget_name, gchar *string1, gchar *string2, gint int1, gint int2);


void interactive_main(int argc, char **argv) {
  /* After common initialization code needed whether or not we're running
   * interactively, this takes over to provide the gtk UI for playing with
   * the de jong attractor in mostly-real-time. Yay.
   */
  gtk_init(&argc, &argv);
  glade_init();

  gui.xml = glade_xml_new("data/explorer-ui.glade", NULL, NULL);
  glade_xml_signal_autoconnect(gui.xml);

  gui.window = glade_xml_get_widget(gui.xml, "explorer_window");

  gui.drawing_area = glade_xml_get_widget(gui.xml, "main_drawingarea");
  gui.gc = gdk_gc_new(gui.drawing_area->window);

  gui.statusbar = GTK_STATUSBAR(glade_xml_get_widget(gui.xml, "statusbar"));
  gui.render_status_context = gtk_statusbar_get_context_id(gui.statusbar, "Rendering status");

  gui.idler = g_idle_add(interactive_idle_handler, NULL);
  gtk_main();
}

static int limit_update_rate(float max_rate) {
  /* Limit the frame rate to the given value. This should be called once per
   * frame, and will return 0 if it's alright to render another frame, or 1
   * otherwise.
   */
  static GTimeVal last_update;
  GTimeVal now;
  gulong diff;

  /* Figure out how much time has passed, in milliseconds */
  g_get_current_time(&now);
  diff = ((now.tv_usec - last_update.tv_usec) / 1000 +
	  (now.tv_sec  - last_update.tv_sec ) * 1000);

  if (diff < (1000 / max_rate)) {
    return 1;
  }
  else {
    last_update = now;
    return 0;
  }
}

static int auto_limit_update_rate(void) {
  /* Automatically determine a good maximum frame rate based on the current
   * number of iterations, and use limit_update_rate() to limit us to that.
   * Returns 1 if a frame should not be rendered.
   *
   * When we just start rendering an image, we want a quite high frame rate
   * (but not high enough we bog down the GUI) so the user can interactively
   * set parameters. After the rendering has been running for a while though,
   * the image changes much less and a very slow frame rate will leave more
   * CPU for calculations.
   */
  return limit_update_rate(200 / (1 + (log(render.iterations) - 9.21) * 5));
}

static void update_gui() {
  /* If the GUI needs updating, update it. This includes limiting the maximum
   * update rate, updating the iteration count display, and actually rendering
   * frames to the drawing area.
   */
  GtkWidget *statusbar;
  gchar *iters;

  /* Skip frame rate limiting and updating the iteration counter if we're in
   * a hurry to show the user the result of a modified rendering parameter.
   */
  if (!render.dirty_flag) {

    if (auto_limit_update_rate())
      return;

    /* Update the iteration counter, removing the previous one if it existed */
    iters = g_strdup_printf("Iterations:    %.3e        Peak density:    %d", render.iterations, render.current_density);
    if (gui.render_status_message_id)
      gtk_statusbar_remove(gui.statusbar, gui.render_status_context, gui.render_status_message_id);
    gui.render_status_message_id = gtk_statusbar_push(gui.statusbar, gui.render_status_context, iters);
    g_free(iters);
  }

  update_pixels();
  update_drawing_area();
}

static void update_drawing_area() {
  /* Update our drawing area */
  gdk_draw_rgb_32_image(gui.drawing_area->window, gui.gc,
			0, 0, render.width, render.height, GDK_RGB_DITHER_NORMAL,
			(guchar*) render.pixels, render.width * 4);
}

static int interactive_idle_handler(gpointer user_data) {
  /* An idle handler used for interactively rendering. This runs a relatively
   * small number of iterations, then calls update_gui() to update our visible image.
   */
  run_iterations(10000);
  update_gui();
  return 1;
}

gboolean on_expose(GtkWidget *widget, GdkEventExpose *event, gpointer user_data) {
  update_drawing_area();
  return TRUE;
}

gboolean on_window_delete(GtkWidget *widget, GdkEvent *event, gpointer user_data) {
  g_source_remove(gui.idler);
  gtk_main_quit();
}

void on_start_clicked(GtkWidget *widget, gpointer user_data) {
  clear();

  /*
  gtk_widget_set_sensitive(gui.stop, TRUE);
  gtk_widget_set_sensitive(gui.start, FALSE);
  params.a = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.a));
  params.b = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.b));
  params.c = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.c));
  params.d = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.d));
  params.zoom = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.zoom));
  params.xoffset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.xoffset));
  params.yoffset = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.yoffset));
  params.rotation = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.rotation));
  params.blur_radius = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.blur_radius));
  params.blur_ratio = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.blur_ratio));
  */
  gui.idler = g_idle_add(interactive_idle_handler, NULL);
}

void on_stop_clicked(GtkWidget *widget, gpointer user_data) {
  /*
  gtk_widget_set_sensitive(gui.stop, FALSE);
  gtk_widget_set_sensitive(gui.start, TRUE);
  */
  g_source_remove(gui.idler);
}

void on_param_spinner_changed(GtkWidget *widget, gpointer user_data) {
  on_stop_clicked(widget, user_data);
  on_start_clicked(widget, user_data);
}

void on_render_spinner_changed(GtkWidget *widget, gpointer user_data) {
  /*
  render.exposure = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.exposure));
  render.gamma = gtk_spin_button_get_value(GTK_SPIN_BUTTON(gui.gamma));
  */
  render.dirty_flag = TRUE;
}

void on_color_changed(GtkWidget *widget, gpointer user_data) {
  /* The simple method of just setting dirty_flag works well when the spin
   * button values change, but the color picker sucks up too much event loop
   * time for that to work nicely for colors. This is a bit of a hack that
   * makes color picking run much more smoothly.
   */

  /*
  color_button_get_color(COLOR_BUTTON(gui.fgcolor), &render.fgcolor);
  color_button_get_color(COLOR_BUTTON(gui.bgcolor), &render.bgcolor);
  */
  render.dirty_flag = TRUE;

  gtk_main_iteration();
  update_gui();
}

static float generate_random_param() {
  return uniform_variate() * 12 - 6;
}

void on_random_clicked(GtkWidget *widget, gpointer user_data) {
  /*
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui.a), generate_random_param());
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui.b), generate_random_param());
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui.c), generate_random_param());
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(gui.d), generate_random_param());
  */

  on_stop_clicked(widget, user_data);
  on_start_clicked(widget, user_data);
}

GtkWidget *custom_color_button_new(gchar *widget_name, gchar *string1,
				   gchar *string2, gint int1, gint int2) {
  GdkColor foo;
  return color_button_new("Boing", &foo);
}

/* File picker for GTK 2.3 and above */
#if (GTK_MAJOR_VERSION > 2) || (GTK_MAJOR_VERSION == 2 && GTK_MINOR_VERSION >= 3)

void update_save_preview(GtkFileChooser *chooser, gpointer data) {
  GtkWidget *preview;
  char *filename;
  GdkPixbuf *pixbuf;
  gboolean have_preview;

  preview = GTK_WIDGET(data);
  filename = gtk_file_chooser_get_preview_filename(chooser);

  pixbuf = gdk_pixbuf_new_from_file_at_size(filename, 128, 128, NULL);
  have_preview = (pixbuf != NULL);
  g_free(filename);

  gtk_image_set_from_pixbuf(GTK_IMAGE(preview), pixbuf);
  if(pixbuf)
    gdk_pixbuf_unref(pixbuf);
  gtk_file_chooser_set_preview_widget_active(chooser, have_preview);
}

void on_save_clicked(GtkWidget *widget, gpointer user_data) {
  GtkWidget *dialog, *preview;

  dialog = gtk_file_chooser_dialog_new("Save", GTK_WINDOW(gui.window), GTK_FILE_CHOOSER_ACTION_SAVE,
  				       GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
				       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				       NULL);
  GtkFileFilter *filter = gtk_file_filter_new();
  gtk_file_filter_add_pattern(filter, "*.png");
  gtk_file_filter_set_name(filter, "PNG Image");
  gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);

  preview = gtk_image_new();
  gtk_file_chooser_set_preview_widget(GTK_FILE_CHOOSER(dialog), preview);
  g_signal_connect(dialog, "update-preview", G_CALLBACK(update_save_preview), preview);
  if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    char *filename;
    filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    save_to_file(filename);
    g_free(filename);
  }
  g_object_unref(filter);
  gtk_widget_destroy(dialog);
}

#else /* GTK < 2.3 */

/* File picker for GTK versions before 2.3 */
void on_save_clicked(GtkWidget *widget, gpointer user_data) {
  GtkWidget *dialog;

  dialog = gtk_file_selection_new("Save");

  if(gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
    const gchar *filename;
    filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(dialog));
    save_to_file(filename);
  }
  gtk_widget_destroy(dialog);
}

#endif /* GTK version */

/* The End */