#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include "message_builder.h"
#include "network_helper.h"
#define SERVER_PORT 6666
typedef struct { GtkWidget *window, *server_entry, *status_label, *device_list, *device_combo, *control_label; NetContext *net; } AppData;
void show_error(GtkWidget *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d)); gtk_widget_destroy(d);
}
ResponseParser* send_request(AppData *app, MessageBuilder *mb, const char *prefix) {
    char *resp_str = net_send_receive(app->net, mb);
    if (!resp_str) { char e[256]; snprintf(e, 256, "%s: No response", prefix); show_error(app->window, e); return NULL; }
    ResponseParser *rp = response_parse(resp_str); free(resp_str);
    if (!response_is_success(rp)) { char e[256]; snprintf(e, 256, "%s: %s", prefix, rp->error_msg); show_error(app->window, e); response_free(rp); return NULL; }
    return rp;
}
void on_connect_clicked(GtkWidget *w, gpointer d) { (void)w; AppData *app = d; const char *ip = gtk_entry_get_text(GTK_ENTRY(app->server_entry));
    if (!strlen(ip)) { show_error(app->window, "Enter server IP"); return; }
    gtk_label_set_text(GTK_LABEL(app->status_label), "Connecting...");
    if (net_connect(app->net, ip, SERVER_PORT) < 0) { gtk_label_set_text(GTK_LABEL(app->status_label), "Disconnected"); show_error(app->window, "Connection failed"); return; }
    MessageBuilder *mb = msg_builder_create(
    "request", 
    "gtk_client", 
    "server",
    "login");
    msg_builder_add_string(mb, "username", "admin"); msg_builder_add_string(mb, "password", "admin");
    ResponseParser *rp = send_request(app, mb, "Login failed"); msg_builder_free(mb);
    if (rp) { gtk_label_set_text(GTK_LABEL(app->status_label), "Connected"); response_free(rp); }
    else { gtk_label_set_text(GTK_LABEL(app->status_label), "Disconnected"); }
}
void on_scan_clicked(GtkWidget *w, gpointer d) { (void)w; AppData *app = d;
    if (app->net->sock < 0) { show_error(app->window, "Not connected"); return; }
    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "list_devices");
    ResponseParser *rp = send_request(app, mb, "Scan failed"); msg_builder_free(mb);
    if (!rp) return;
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->device_combo));
    struct json_object *data = rp->data, *devs;
    if (json_object_object_get_ex(data, "devices", &devs)) {
        size_t cnt = json_object_array_length(devs); char txt[256]; snprintf(txt, 256, "Found %zu device(s)", cnt);
        gtk_label_set_text(GTK_LABEL(app->device_list), txt);
        for (size_t i = 0; i < cnt; i++) {
            struct json_object *dev = json_object_array_get_idx(devs, i), *id, *type;
            if (json_object_object_get_ex(dev, "id", &id) && json_object_object_get_ex(dev, "type", &type)) {
                char item[128]; snprintf(item, 128, "%s (%s)", json_object_get_string(id), json_object_get_string(type));
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->device_combo), item);
            }
        }
        if (cnt > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(app->device_combo), 0);
    }
    response_free(rp);
}
void on_control_clicked(GtkWidget *w, gpointer d) { AppData *app = d;
    if (app->net->sock < 0) { show_error(app->window, "Not connected"); return; }
    gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->device_combo));
    if (!sel) { show_error(app->window, "No device selected"); return; }
    char did[64]; sscanf(sel, "%s", did); g_free(sel);
    bool on = (strcmp(gtk_button_get_label(GTK_BUTTON(w)), "Turn ON") == 0);
    MessageBuilder *mb = msg_builder_create("request", "gtk_client", did, "control");
    msg_builder_add_string(mb, "device_type", "light"); msg_builder_add_bool(mb, "state", on);
    ResponseParser *rp = send_request(app, mb, "Control failed"); msg_builder_free(mb);
    if (!rp) return;
    const char *st = response_get_string(rp, "state"); int pwr = response_get_int(rp, "power");
    if (st) { char txt[128]; snprintf(txt, 128, "State: %s | Power: %dW", st, pwr); gtk_label_set_text(GTK_LABEL(app->control_label), txt); }
    response_free(rp);
}


void on_destroy(GtkWidget *w, gpointer d) { (void)w; AppData *app = d; net_context_free(app->net); gtk_main_quit(); }
int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv); AppData app = {0}; app.net = net_context_create("gtk_client");
    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Smart Home");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 400, 300);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), &app);
    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5); gtk_container_add(GTK_CONTAINER(app.window), vb);
    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); gtk_box_pack_start(GTK_BOX(vb), h1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h1), gtk_label_new("Server IP:"), FALSE, FALSE, 0);
    app.server_entry = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(app.server_entry), "192.168.1.65");
    gtk_box_pack_start(GTK_BOX(h1), app.server_entry, TRUE, TRUE, 0);
    GtkWidget *cb = gtk_button_new_with_label("Connect"); g_signal_connect(cb, "clicked", G_CALLBACK(on_connect_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h1), cb, FALSE, FALSE, 0);
    app.status_label = gtk_label_new("Disconnected"); gtk_box_pack_start(GTK_BOX(vb), app.status_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    GtkWidget *sb = gtk_button_new_with_label("Scan"); g_signal_connect(sb, "clicked", G_CALLBACK(on_scan_clicked), &app);
    gtk_box_pack_start(GTK_BOX(vb), sb, FALSE, FALSE, 0);
    app.device_list = gtk_label_new("No devices"); gtk_box_pack_start(GTK_BOX(vb), app.device_list, FALSE, FALSE, 0);
    app.device_combo = gtk_combo_box_text_new(); gtk_box_pack_start(GTK_BOX(vb), app.device_combo, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vb), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);
    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5); gtk_box_pack_start(GTK_BOX(vb), h2, FALSE, FALSE, 0);
    GtkWidget *on = gtk_button_new_with_label("Turn ON"); g_signal_connect(on, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), on, TRUE, TRUE, 0);
    GtkWidget *off = gtk_button_new_with_label("Turn OFF"); g_signal_connect(off, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), off, TRUE, TRUE, 0);
    app.control_label = gtk_label_new("State: unknown"); gtk_box_pack_start(GTK_BOX(vb), app.control_label, FALSE, FALSE, 0);
    gtk_widget_show_all(app.window); gtk_main(); return 0;
}
