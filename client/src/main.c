#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "message_builder.h"
#include "network_helper.h"

#define SERVER_PORT 6666

typedef struct {
    GtkWidget *window;
    GtkWidget *notebook;
    GtkWidget *server_entry;
    GtkWidget *pass_entry;
    GtkWidget *status_label;
    GtkWidget *device_list;
    GtkWidget *device_combo;
    GtkWidget *control_label;
    GtkWidget *old_pass_entry;
    GtkWidget *new_pass_entry;
    GtkWidget *timer_list_view;
    GtkListStore *timer_store;
    GtkWidget *log_list_view;
    GtkListStore *log_store;
    NetContext *net;
    gboolean logged_in;
} AppData;

void show_error(GtkWidget *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

void show_info(GtkWidget *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(parent), GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", msg);
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

ResponseParser* send_request(AppData *app, MessageBuilder *mb, const char *prefix) {
    char *resp = net_send_receive(app->net, mb);
    if (!resp) {
        char e[256];
        snprintf(e, sizeof(e), "%s: No response", prefix);
        show_error(app->window, e);
        return NULL;
    }
    
    ResponseParser *rp = response_parse(resp);
    free(resp);
    
    if (!rp) {
        char e[256];
        snprintf(e, sizeof(e), "%s: Invalid response", prefix);
        show_error(app->window, e);
        return NULL;
    }
    
    const char *status = response_get_string(rp, "status");
    if (status && strcmp(status, "error") == 0) {
        const char *msg = response_get_string(rp, "message");
        char e[256];
        snprintf(e, sizeof(e), "%s: %s", prefix, msg ? msg : "Unknown error");
        show_error(app->window, e);
        response_free(rp);
        return NULL;
    }
    
    if (!response_is_success(rp)) {
        char e[256];
        snprintf(e, sizeof(e), "%s: %.200s", prefix, rp->error_msg ? rp->error_msg : "Failed");
        show_error(app->window, e);
        response_free(rp);
        return NULL;
    }
    
    return rp;
}

void on_refresh_timers_clicked(GtkWidget *w, gpointer d);
void on_refresh_logs_clicked(GtkWidget *w, gpointer d);

void set_logged_in(AppData *app, gboolean v) {
    app->logged_in = v;
    gtk_widget_set_sensitive(app->device_combo, v);
    
    if (!v) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->device_combo));
        gtk_label_set_text(GTK_LABEL(app->device_list), "No devices");
        gtk_label_set_text(GTK_LABEL(app->control_label), "State: unknown");
        gtk_list_store_clear(app->timer_store);
        gtk_list_store_clear(app->log_store);
    }
}

void on_connect_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    const char *ip = gtk_entry_get_text(GTK_ENTRY(app->server_entry));
    const char *pw = gtk_entry_get_text(GTK_ENTRY(app->pass_entry));

    if (!strlen(ip) || !strlen(pw)) {
        show_error(app->window, "Enter server IP and password");
        return;
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), "Connecting...");

    if (net_connect(app->net, ip, SERVER_PORT) < 0) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Connection failed");
        show_error(app->window, "Cannot connect to server");
        return;
    }

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "login");
    msg_builder_add_string(mb, "username", "admin");
    msg_builder_add_string(mb, "password", pw);
    ResponseParser *rp = send_request(app, mb, "Login failed");
    msg_builder_free(mb);

    if (!rp) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Login failed");
        set_logged_in(app, FALSE);
        return;
    }

    const char *status = response_get_string(rp, "status");
    if (!status || strcmp(status, "success") != 0) {
        gtk_label_set_text(GTK_LABEL(app->status_label), "Auth failed");
        show_error(app->window, "Wrong password");
        set_logged_in(app, FALSE);
        response_free(rp);
        return;
    }

    gtk_label_set_text(GTK_LABEL(app->status_label), "Connected");
    set_logged_in(app, TRUE);
    response_free(rp);
    on_refresh_logs_clicked(NULL, app);
}

void on_scan_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "list_devices");
    ResponseParser *rp = send_request(app, mb, "Scan failed");
    msg_builder_free(mb);
    if (!rp) return;

    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->device_combo));

    struct json_object *devs;
    if (json_object_object_get_ex(rp->data, "devices", &devs)) {
        size_t n = json_object_array_length(devs);
        char txt[64];
        if (n == 0) {
            snprintf(txt, sizeof(txt), "No devices found");
        } else {
            snprintf(txt, sizeof(txt), "Found %zu device(s)", n);
        }
        gtk_label_set_text(GTK_LABEL(app->device_list), txt);
        
        for (size_t i = 0; i < n; i++) {
            struct json_object *d = json_object_array_get_idx(devs, i);
            struct json_object *id, *type;
            if (json_object_object_get_ex(d, "id", &id) &&
                json_object_object_get_ex(d, "type", &type)) {
                char item[128];
                snprintf(item, sizeof(item), "%s (%s)",
                         json_object_get_string(id),
                         json_object_get_string(type));
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(app->device_combo), item);
            }
        }
        if (n > 0) gtk_combo_box_set_active(GTK_COMBO_BOX(app->device_combo), 0);
    }
    response_free(rp);
    on_refresh_logs_clicked(NULL, app);
}

void on_control_clicked(GtkWidget *w, gpointer d) {
    AppData *app = d;
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->device_combo));
    if (!sel) {
        show_error(app->window, "Select a device");
        return;
    }

    char did[64];
    sscanf(sel, "%63s", did);
    g_free(sel);
    gboolean on = strcmp(gtk_button_get_label(GTK_BUTTON(w)), "Turn ON") == 0;

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", did, "control");
    msg_builder_add_string(mb, "device_type", "light");
    msg_builder_add_bool(mb, "state", on);
    ResponseParser *rp = send_request(app, mb, "Control failed");
    msg_builder_free(mb);
    if (!rp) return;

    const char *st = response_get_string(rp, "state");
    int pwr = response_get_int(rp, "power");
    if (st) {
        char txt[128];
        snprintf(txt, sizeof(txt), "State: %s | Power: %dW", st, pwr);
        gtk_label_set_text(GTK_LABEL(app->control_label), txt);
    }
    response_free(rp);
    on_refresh_logs_clicked(NULL, app);
}

void on_fan_speed_clicked(GtkWidget *w, gpointer d) {
    AppData *app = d;
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->device_combo));
    if (!sel) {
        show_error(app->window, "Select a device");
        return;
    }

    char did[64];
    sscanf(sel, "%63s", did);
    g_free(sel);

    int speed = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "speed"));

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", did, "control");
    msg_builder_add_bool(mb, "state", TRUE);
    msg_builder_add_int(mb, "speed", speed);

    ResponseParser *rp = send_request(app, mb, "Set fan speed failed");
    msg_builder_free(mb);
    if (!rp) return;

    response_free(rp);
    on_refresh_logs_clicked(NULL, app);
}

void quick_set_timer(AppData *app, int delay_seconds, gboolean state) {
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    gchar *sel = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(app->device_combo));
    if (!sel) {
        show_error(app->window, "Select a device first");
        return;
    }

    char did[64];
    sscanf(sel, "%63s", did);
    g_free(sel);

    char label[64];
    snprintf(label, sizeof(label), "%s after %ds", state ? "Turn ON" : "Turn OFF", delay_seconds);

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "set_timer");
    msg_builder_add_string(mb, "device_id", did);
    msg_builder_add_int(mb, "delay_seconds", delay_seconds);
    msg_builder_add_bool(mb, "state", state);
    msg_builder_add_string(mb, "label", label);
    ResponseParser *rp = send_request(app, mb, "Set timer failed");
    msg_builder_free(mb);
    
    if (rp) {
        char success_msg[128];
        snprintf(success_msg, sizeof(success_msg), "Timer set: %s in %ds", state ? "ON" : "OFF", delay_seconds);
        show_info(app->window, success_msg);
        response_free(rp);
        on_refresh_timers_clicked(NULL, app);
        on_refresh_logs_clicked(NULL, app);
    }
}

void on_timer_10s_on(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 10, TRUE); }
void on_timer_10s_off(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 10, FALSE); }
void on_timer_30s_on(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 30, TRUE); }
void on_timer_30s_off(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 30, FALSE); }
void on_timer_60s_on(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 60, TRUE); }
void on_timer_60s_off(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 60, FALSE); }
void on_timer_120s_on(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 120, TRUE); }
void on_timer_120s_off(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 120, FALSE); }
void on_timer_300s_on(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 300, TRUE); }
void on_timer_300s_off(GtkWidget *w, gpointer d) { (void)w; quick_set_timer((AppData*)d, 300, FALSE); }

void on_refresh_timers_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (!app->logged_in) return;

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "list_timers");
    ResponseParser *rp = send_request(app, mb, "List timers failed");
    msg_builder_free(mb);
    if (!rp) return;

    gtk_list_store_clear(app->timer_store);

    struct json_object *timers;
    if (json_object_object_get_ex(rp->data, "timers", &timers)) {
        size_t n = json_object_array_length(timers);
        for (size_t i = 0; i < n; i++) {
            struct json_object *t = json_object_array_get_idx(timers, i);
            struct json_object *tid, *dev, *exec_time, *st, *lbl;
            
            if (json_object_object_get_ex(t, "timer_id", &tid) &&
                json_object_object_get_ex(t, "device_id", &dev) &&
                json_object_object_get_ex(t, "execute_time", &exec_time) &&
                json_object_object_get_ex(t, "state", &st) &&
                json_object_object_get_ex(t, "label", &lbl)) {
                
                GtkTreeIter iter;
                gtk_list_store_append(app->timer_store, &iter);
                
                time_t exec = json_object_get_int64(exec_time);
                time_t now = time(NULL);
                int remaining = (int)(exec - now);
                
                char time_str[64];
                if (remaining > 0) {
                    int mins = remaining / 60;
                    int secs = remaining % 60;
                    if (mins > 0) {
                        snprintf(time_str, sizeof(time_str), "%dm %ds", mins, secs);
                    } else {
                        snprintf(time_str, sizeof(time_str), "%ds", secs);
                    }
                } else {
                    strcpy(time_str, "Executing...");
                }
                
                gtk_list_store_set(app->timer_store, &iter,
                    0, json_object_get_int(tid),
                    1, json_object_get_string(dev),
                    2, json_object_get_boolean(st) ? "ON" : "OFF",
                    3, time_str,
                    4, json_object_get_string(lbl),
                    -1);
            }
        }
    }
    response_free(rp);
}

void on_delete_timer_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(app->timer_list_view));
    GtkTreeIter iter;
    GtkTreeModel *model;

    if (!gtk_tree_selection_get_selected(sel, &model, &iter)) {
        show_error(app->window, "Select a timer to delete");
        return;
    }

    int timer_id;
    gtk_tree_model_get(model, &iter, 0, &timer_id, -1);

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "delete_timer");
    msg_builder_add_int(mb, "timer_id", timer_id);
    ResponseParser *rp = send_request(app, mb, "Delete timer failed");
    msg_builder_free(mb);
    
    if (rp) {
        show_info(app->window, "Timer deleted!");
        on_refresh_timers_clicked(NULL, app);
        on_refresh_logs_clicked(NULL, app);
        response_free(rp);
    }
}

void on_refresh_logs_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (!app->logged_in) return;

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "get_logs");
    ResponseParser *rp = send_request(app, mb, "Get logs failed");
    msg_builder_free(mb);
    if (!rp) return;

    gtk_list_store_clear(app->log_store);

    struct json_object *logs;
    if (json_object_object_get_ex(rp->data, "logs", &logs)) {
        size_t n = json_object_array_length(logs);
        for (size_t i = 0; i < n; i++) {
            struct json_object *log = json_object_array_get_idx(logs, i);
            struct json_object *lid, *ts, *actor, *action, *target, *details, *success;
            
            if (json_object_object_get_ex(log, "log_id", &lid) &&
                json_object_object_get_ex(log, "timestamp", &ts) &&
                json_object_object_get_ex(log, "actor", &actor) &&
                json_object_object_get_ex(log, "action", &action) &&
                json_object_object_get_ex(log, "target", &target) &&
                json_object_object_get_ex(log, "details", &details) &&
                json_object_object_get_ex(log, "success", &success)) {
                
                GtkTreeIter iter;
                gtk_list_store_append(app->log_store, &iter);
                
                time_t timestamp = json_object_get_int64(ts);
                char time_str[64];
                struct tm *tm_info = localtime(&timestamp);
                strftime(time_str, sizeof(time_str), "%H:%M:%S", tm_info);
                
                gtk_list_store_set(app->log_store, &iter,
                    0, json_object_get_int(lid),
                    1, time_str,
                    2, json_object_get_string(actor),
                    3, json_object_get_string(action),
                    4, json_object_get_string(target),
                    5, json_object_get_string(details),
                    6, json_object_get_boolean(success) ? "OK" : "FAIL",
                    -1);
            }
        }
    }
    response_free(rp);
}

void on_change_password_clicked(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (!app->logged_in) {
        show_error(app->window, "Login first");
        return;
    }

    const char *oldpw = gtk_entry_get_text(GTK_ENTRY(app->old_pass_entry));
    const char *newpw = gtk_entry_get_text(GTK_ENTRY(app->new_pass_entry));

    if (!strlen(oldpw) || !strlen(newpw)) {
        show_error(app->window, "Enter both passwords");
        return;
    }

    if (strlen(newpw) < 4) {
        show_error(app->window, "New password too short");
        return;
    }

    MessageBuilder *mb = msg_builder_create("request", "gtk_client", "server", "change_password");
    msg_builder_add_string(mb, "old_password", oldpw);
    msg_builder_add_string(mb, "new_password", newpw);
    ResponseParser *rp = send_request(app, mb, "Change password failed");
    msg_builder_free(mb);
    if (!rp) return;

    show_info(app->window, "Password changed. Re-login required.");
    gtk_entry_set_text(GTK_ENTRY(app->old_pass_entry), "");
    gtk_entry_set_text(GTK_ENTRY(app->new_pass_entry), "");
    gtk_entry_set_text(GTK_ENTRY(app->pass_entry), "");
    set_logged_in(app, FALSE);
    gtk_label_set_text(GTK_LABEL(app->status_label), "Disconnected");
    response_free(rp);
}

void on_destroy(GtkWidget *w, gpointer d) {
    (void)w;
    AppData *app = d;
    if (app->net) net_context_free(app->net);
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppData app = {0};
    app.net = net_context_create("gtk_client");
    app.logged_in = FALSE;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Smart Home Control");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 900, 650);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 10);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), &app);

    GtkWidget *vbox_main = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app.window), vbox_main);

    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox_main), h1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h1), gtk_label_new("Server:"), FALSE, FALSE, 0);
    
    app.server_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.server_entry), "192.168.1.100");
    gtk_box_pack_start(GTK_BOX(h1), app.server_entry, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(h1), gtk_label_new("Password:"), FALSE, FALSE, 0);
    app.pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.pass_entry), FALSE);
    gtk_entry_set_placeholder_text(GTK_ENTRY(app.pass_entry), "admin");
    gtk_box_pack_start(GTK_BOX(h1), app.pass_entry, TRUE, TRUE, 0);

    GtkWidget *cb = gtk_button_new_with_label("Connect");
    g_signal_connect(cb, "clicked", G_CALLBACK(on_connect_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h1), cb, FALSE, FALSE, 0);

    app.status_label = gtk_label_new("Disconnected");
    gtk_box_pack_start(GTK_BOX(vbox_main), app.status_label, FALSE, FALSE, 0);

    GtkWidget *hp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vbox_main), hp, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(hp), gtk_label_new("Old Pass:"), FALSE, FALSE, 0);
    app.old_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.old_pass_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(hp), app.old_pass_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(hp), gtk_label_new("New Pass:"), FALSE, FALSE, 0);
    app.new_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.new_pass_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(hp), app.new_pass_entry, TRUE, TRUE, 0);
    GtkWidget *cp = gtk_button_new_with_label("Change Password");
    g_signal_connect(cp, "clicked", G_CALLBACK(on_change_password_clicked), &app);
    gtk_box_pack_start(GTK_BOX(hp), cp, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox_main), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    app.notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox_main), app.notebook, TRUE, TRUE, 0);

    GtkWidget *page1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(page1), 10);
    gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), page1, gtk_label_new("Control"));

    GtkWidget *scan = gtk_button_new_with_label("Scan Devices");
    g_signal_connect(scan, "clicked", G_CALLBACK(on_scan_clicked), &app);
    gtk_box_pack_start(GTK_BOX(page1), scan, FALSE, FALSE, 0);

    app.device_list = gtk_label_new("No devices");
    gtk_box_pack_start(GTK_BOX(page1), app.device_list, FALSE, FALSE, 0);

    app.device_combo = gtk_combo_box_text_new();
    gtk_widget_set_sensitive(app.device_combo, FALSE);
    gtk_box_pack_start(GTK_BOX(page1), app.device_combo, FALSE, FALSE, 0);

    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(page1), h2, FALSE, FALSE, 0);
    GtkWidget *onb = gtk_button_new_with_label("Turn ON");
    g_signal_connect(onb, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), onb, TRUE, TRUE, 0);
    GtkWidget *offb = gtk_button_new_with_label("Turn OFF");
    g_signal_connect(offb, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), offb, TRUE, TRUE, 0);

    app.control_label = gtk_label_new("State: unknown");
    gtk_box_pack_start(GTK_BOX(page1), app.control_label, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(page1), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    gtk_box_pack_start(GTK_BOX(page1), gtk_label_new("Fan Speed:"), FALSE, FALSE, 0);

    GtkWidget *speed_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(page1), speed_box, FALSE, FALSE, 0);

    GtkWidget *btn_s1 = gtk_button_new_with_label("Speed 1");
    GtkWidget *btn_s2 = gtk_button_new_with_label("Speed 2");
    GtkWidget *btn_s3 = gtk_button_new_with_label("Speed 3");

    g_object_set_data(G_OBJECT(btn_s1), "speed", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(btn_s2), "speed", GINT_TO_POINTER(2));
    g_object_set_data(G_OBJECT(btn_s3), "speed", GINT_TO_POINTER(3));

    g_signal_connect(btn_s1, "clicked", G_CALLBACK(on_fan_speed_clicked), &app);
    g_signal_connect(btn_s2, "clicked", G_CALLBACK(on_fan_speed_clicked), &app);
    g_signal_connect(btn_s3, "clicked", G_CALLBACK(on_fan_speed_clicked), &app);

    gtk_box_pack_start(GTK_BOX(speed_box), btn_s1, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(speed_box), btn_s2, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(speed_box), btn_s3, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(page1), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 5);

    GtkWidget *timer_header = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(timer_header), "<b>QUICK TIMER</b>");
    gtk_box_pack_start(GTK_BOX(page1), timer_header, FALSE, FALSE, 0);

    GtkWidget *timer_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(timer_grid), 5);
    gtk_grid_set_column_spacing(GTK_GRID(timer_grid), 5);
    gtk_box_pack_start(GTK_BOX(page1), timer_grid, FALSE, FALSE, 0);

    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("Delay"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("Turn ON"), 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("Turn OFF"), 2, 0, 1, 1);

    GtkWidget *btn_10s_on = gtk_button_new_with_label("ON 10s");
    GtkWidget *btn_10s_off = gtk_button_new_with_label("OFF 10s");
    g_signal_connect(btn_10s_on, "clicked", G_CALLBACK(on_timer_10s_on), &app);
    g_signal_connect(btn_10s_off, "clicked", G_CALLBACK(on_timer_10s_off), &app);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("10s:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_10s_on, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_10s_off, 2, 1, 1, 1);

    GtkWidget *btn_30s_on = gtk_button_new_with_label("ON 30s");
    GtkWidget *btn_30s_off = gtk_button_new_with_label("OFF 30s");
    g_signal_connect(btn_30s_on, "clicked", G_CALLBACK(on_timer_30s_on), &app);
    g_signal_connect(btn_30s_off, "clicked", G_CALLBACK(on_timer_30s_off), &app);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("30s:"), 0, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_30s_on, 1, 2, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_30s_off, 2, 2, 1, 1);

    GtkWidget *btn_60s_on = gtk_button_new_with_label("ON 60s");
    GtkWidget *btn_60s_off = gtk_button_new_with_label("OFF 60s");
    g_signal_connect(btn_60s_on, "clicked", G_CALLBACK(on_timer_60s_on), &app);
    g_signal_connect(btn_60s_off, "clicked", G_CALLBACK(on_timer_60s_off), &app);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("60s:"), 0, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_60s_on, 1, 3, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_60s_off, 2, 3, 1, 1);

    GtkWidget *btn_120s_on = gtk_button_new_with_label("ON 120s");
    GtkWidget *btn_120s_off = gtk_button_new_with_label("OFF 120s");
    g_signal_connect(btn_120s_on, "clicked", G_CALLBACK(on_timer_120s_on), &app);
    g_signal_connect(btn_120s_off, "clicked", G_CALLBACK(on_timer_120s_off), &app);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("120s:"), 0, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_120s_on, 1, 4, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_120s_off, 2, 4, 1, 1);

    GtkWidget *btn_300s_on = gtk_button_new_with_label("ON 300s");
    GtkWidget *btn_300s_off = gtk_button_new_with_label("OFF 300s");
    g_signal_connect(btn_300s_on, "clicked", G_CALLBACK(on_timer_300s_on), &app);
    g_signal_connect(btn_300s_off, "clicked", G_CALLBACK(on_timer_300s_off), &app);
    gtk_grid_attach(GTK_GRID(timer_grid), gtk_label_new("300s:"), 0, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_300s_on, 1, 5, 1, 1);
    gtk_grid_attach(GTK_GRID(timer_grid), btn_300s_off, 2, 5, 1, 1);

GtkWidget *page2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
gtk_container_set_border_width(GTK_CONTAINER(page2), 10);
gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), page2, gtk_label_new("Timers"));

GtkWidget *timer_buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
gtk_box_pack_start(GTK_BOX(page2), timer_buttons, FALSE, FALSE, 0);

GtkWidget *refresh_timer_btn = gtk_button_new_with_label("Refresh");
g_signal_connect(refresh_timer_btn, "clicked", G_CALLBACK(on_refresh_timers_clicked), &app);
gtk_box_pack_start(GTK_BOX(timer_buttons), refresh_timer_btn, TRUE, TRUE, 0);

GtkWidget *delete_timer_btn = gtk_button_new_with_label("Delete Selected");
g_signal_connect(delete_timer_btn, "clicked", G_CALLBACK(on_delete_timer_clicked), &app);
gtk_box_pack_start(GTK_BOX(timer_buttons), delete_timer_btn, TRUE, TRUE, 0);

GtkWidget *scroll_timer = gtk_scrolled_window_new(NULL, NULL);
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_timer), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
gtk_box_pack_start(GTK_BOX(page2), scroll_timer, TRUE, TRUE, 0);

app.timer_store = gtk_list_store_new(5, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
app.timer_list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.timer_store));

GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.timer_list_view), -1, "ID", renderer, "text", 0, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.timer_list_view), -1, "Device", renderer, "text", 1, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.timer_list_view), -1, "Action", renderer, "text", 2, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.timer_list_view), -1, "Time Left", renderer, "text", 3, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.timer_list_view), -1, "Label", renderer, "text", 4, NULL);

gtk_container_add(GTK_CONTAINER(scroll_timer), app.timer_list_view);

GtkWidget *page3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
gtk_container_set_border_width(GTK_CONTAINER(page3), 10);
gtk_notebook_append_page(GTK_NOTEBOOK(app.notebook), page3, gtk_label_new("Activity Logs"));

GtkWidget *refresh_logs_btn = gtk_button_new_with_label("Refresh Logs");
g_signal_connect(refresh_logs_btn, "clicked", G_CALLBACK(on_refresh_logs_clicked), &app);
gtk_box_pack_start(GTK_BOX(page3), refresh_logs_btn, FALSE, FALSE, 0);

GtkWidget *scroll_log = gtk_scrolled_window_new(NULL, NULL);
gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll_log), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
gtk_box_pack_start(GTK_BOX(page3), scroll_log, TRUE, TRUE, 0);

app.log_store = gtk_list_store_new(7, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
app.log_list_view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(app.log_store));

GtkCellRenderer *log_renderer = gtk_cell_renderer_text_new();
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "ID", log_renderer, "text", 0, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Time", log_renderer, "text", 1, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Actor", log_renderer, "text", 2, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Action", log_renderer, "text", 3, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Target", log_renderer, "text", 4, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Details", log_renderer, "text", 5, NULL);
gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(app.log_list_view), -1, "Status", log_renderer, "text", 6, NULL);

gtk_container_add(GTK_CONTAINER(scroll_log), app.log_list_view);

gtk_widget_show_all(app.window);
gtk_main();
return 0;
}