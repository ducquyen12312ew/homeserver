#define _POSIX_C_SOURCE 200809L

#include <gtk/gtk.h>
#include <stdio.h>
#include <string.h>

#include "message_builder.h"
#include "network_helper.h"

#define SERVER_PORT 6666

typedef struct {
    GtkWidget *window;
    GtkWidget *server_entry;
    GtkWidget *pass_entry;
    GtkWidget *status_label;
    GtkWidget *device_list;
    GtkWidget *device_combo;
    GtkWidget *control_label;
    GtkWidget *old_pass_entry;
    GtkWidget *new_pass_entry;
    NetContext *net;
    gboolean logged_in;
} AppData;

void show_error(GtkWidget *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(
        GTK_WINDOW(parent),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "%s",
        msg
    );
    gtk_dialog_run(GTK_DIALOG(d));
    gtk_widget_destroy(d);
}

void show_info(GtkWidget *parent, const char *msg) {
    GtkWidget *d = gtk_message_dialog_new(
        GTK_WINDOW(parent),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE,
        "%s",
        msg
    );
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
        snprintf(e, sizeof(e), "%s: %s", prefix, 
                 msg ? msg : "Unknown error");
        show_error(app->window, e);
        response_free(rp);
        return NULL;
    }
    
    if (!response_is_success(rp)) {
        char e[256];
        snprintf(e, sizeof(e), "%s: %.200s", prefix, 
                 rp->error_msg ? rp->error_msg : "Failed");
        show_error(app->window, e);
        response_free(rp);
        return NULL;
    }
    
    return rp;
}

void set_logged_in(AppData *app, gboolean v) {
    app->logged_in = v;
    gtk_widget_set_sensitive(app->device_combo, v);
    
    if (!v) {
        gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(app->device_combo));
        gtk_label_set_text(GTK_LABEL(app->device_list), "No devices");
        gtk_label_set_text(GTK_LABEL(app->control_label), "State: unknown");
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
    if (app->net) {
        net_context_free(app->net);
    }
    gtk_main_quit();
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    AppData app = {0};
    app.net = net_context_create("gtk_client");
    app.logged_in = FALSE;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Smart Home");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 420, 360);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 10);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), &app);

    GtkWidget *vb = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(app.window), vb);

    GtkWidget *h1 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vb), h1, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(h1), gtk_label_new("Server:"), FALSE, FALSE, 0);
    
    app.server_entry = gtk_entry_new();
    gtk_box_pack_start(GTK_BOX(h1), app.server_entry, TRUE, TRUE, 0);

    app.pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.pass_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(h1), app.pass_entry, TRUE, TRUE, 0);

    GtkWidget *cb = gtk_button_new_with_label("Connect");
    g_signal_connect(cb, "clicked", G_CALLBACK(on_connect_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h1), cb, FALSE, FALSE, 0);

    app.status_label = gtk_label_new("Disconnected");
    gtk_box_pack_start(GTK_BOX(vb), app.status_label, FALSE, FALSE, 0);

    GtkWidget *hp = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vb), hp, FALSE, FALSE, 0);

    app.old_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.old_pass_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(hp), app.old_pass_entry, TRUE, TRUE, 0);

    app.new_pass_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.new_pass_entry), FALSE);
    gtk_box_pack_start(GTK_BOX(hp), app.new_pass_entry, TRUE, TRUE, 0);

    GtkWidget *cp = gtk_button_new_with_label("Change Pass");
    g_signal_connect(cp, "clicked", G_CALLBACK(on_change_password_clicked), &app);
    gtk_box_pack_start(GTK_BOX(hp), cp, FALSE, FALSE, 0);

    GtkWidget *scan = gtk_button_new_with_label("Scan");
    g_signal_connect(scan, "clicked", G_CALLBACK(on_scan_clicked), &app);
    gtk_box_pack_start(GTK_BOX(vb), scan, FALSE, FALSE, 0);

    app.device_list = gtk_label_new("No devices");
    gtk_box_pack_start(GTK_BOX(vb), app.device_list, FALSE, FALSE, 0);

    app.device_combo = gtk_combo_box_text_new();
    gtk_widget_set_sensitive(app.device_combo, FALSE);
    gtk_box_pack_start(GTK_BOX(vb), app.device_combo, FALSE, FALSE, 0);

    GtkWidget *h2 = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(vb), h2, FALSE, FALSE, 0);

    GtkWidget *onb = gtk_button_new_with_label("Turn ON");
    g_signal_connect(onb, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), onb, TRUE, TRUE, 0);

    GtkWidget *offb = gtk_button_new_with_label("Turn OFF");
    g_signal_connect(offb, "clicked", G_CALLBACK(on_control_clicked), &app);
    gtk_box_pack_start(GTK_BOX(h2), offb, TRUE, TRUE, 0);

    app.control_label = gtk_label_new("State: unknown");
    gtk_box_pack_start(GTK_BOX(vb), app.control_label, FALSE, FALSE, 0);

    gtk_widget_show_all(app.window);
    gtk_main();
    return 0;
}
