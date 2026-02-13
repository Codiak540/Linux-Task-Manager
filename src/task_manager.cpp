#include "task_manager.h"
#include "debug.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <sys/resource.h>
#include <unistd.h>

#define MAX_NET 100.0

const char* headers[] = {"PID", "Name", "CPU%", "Mem%", "Memory (MB)", "Threads", "User", "State"};
const char* headers2[] = {"Name", "Description", "State", "Active", "PID"};
const char* headers3[] = {"Name", "Enabled", "Source", "Path"};

TaskManager::TaskManager() : running(true), paused(false) {
    last_refresh_time = std::chrono::steady_clock::now();
    last_network_time = std::chrono::steady_clock::now();
    last_network_stats = get_network_stats();
}

TaskManager::~TaskManager() {
    running = false;
    if (refresh_thread.joinable()) {
        refresh_thread.join();
    }
}

void TaskManager::run() {
    int argc = 0;
    char** argv = nullptr;
    gtk_init(&argc, &argv);

    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Task Manager");
    gtk_window_set_default_size(GTK_WINDOW(window), 1000, 600);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(toolbar), 5);

    GtkWidget* search_entry = gtk_search_entry_new();
    gtk_widget_set_size_request(search_entry, 250, -1);
    gtk_box_pack_start(GTK_BOX(toolbar), search_entry, FALSE, FALSE, 0);

    GtkWidget* pause_button = gtk_toggle_button_new_with_label("Pause");
    gtk_box_pack_start(GTK_BOX(toolbar), pause_button, FALSE, FALSE, 0);

    GtkWidget* end_process_btn = gtk_button_new_with_label("End Task");
    gtk_box_pack_start(GTK_BOX(toolbar), end_process_btn, FALSE, FALSE, 0);

    GtkWidget* restart_btn = gtk_button_new_with_label("Restart");
    gtk_box_pack_start(GTK_BOX(toolbar), restart_btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), toolbar, FALSE, FALSE, 0);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(window), vbox);

    setup_processes_tab();
    setup_services_tab();
    setup_startup_tab();
    setup_performance_tab();

    g_signal_connect(window, "delete-event", G_CALLBACK(on_delete_event), this);
    g_signal_connect(end_process_btn, "clicked", G_CALLBACK(on_end_process), this);
    g_signal_connect(pause_button, "toggled", G_CALLBACK(on_pause_toggled), this);
    g_signal_connect(search_entry, "search-changed", G_CALLBACK(on_search_changed), this);

    gtk_widget_show_all(window);

    refresh_thread = std::thread([this]() {
        while (running) {
            if (!paused) {
                g_idle_add(refresh_data, this);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });

    gtk_main();
}

void TaskManager::setup_processes_tab() {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    processes_tab.store = gtk_list_store_new(8,
        G_TYPE_INT,      // PID
        G_TYPE_STRING,   // Name
        G_TYPE_DOUBLE,   // CPU%
        G_TYPE_DOUBLE,   // Memory%
        G_TYPE_UINT64,   // Memory (MB)
        G_TYPE_INT,      // Threads
        G_TYPE_STRING,   // User
        G_TYPE_STRING    // State
    );

    processes_tab.filter = GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(processes_tab.store), nullptr));
    gtk_tree_model_filter_set_visible_func(processes_tab.filter,
        [](GtkTreeModel* model, GtkTreeIter* iter, gpointer data) -> gboolean {
            auto* self = static_cast<TaskManager*>(data);
            if (self->current_search_query.empty()) return TRUE;

            gchar* name = nullptr;
            gtk_tree_model_get(model, iter, 1, &name, -1);
            if (!name) return FALSE;

            bool visible = strcasestr(name, self->current_search_query.c_str()) != nullptr;
            g_free(name);
            return visible;
        }, this, nullptr);

    processes_tab.treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(processes_tab.filter));
    g_object_unref(processes_tab.store);
    g_object_unref(processes_tab.filter);

    for (int i = 0; i < 8; i++) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            headers[i], renderer, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(processes_tab.treeview), column);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), processes_tab.treeview);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Processes"));
}

void TaskManager::setup_services_tab() {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    services_tab.store = gtk_list_store_new(5,
        G_TYPE_STRING,   // Name
        G_TYPE_STRING,   // Description
        G_TYPE_STRING,   // State
        G_TYPE_STRING,   // Active
        G_TYPE_INT       // PID
    );

    services_tab.treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(services_tab.store));
    g_object_unref(services_tab.store);

    for (int i = 0; i < 5; i++) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            headers2[i], renderer, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(services_tab.treeview), column);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), services_tab.treeview);

    // Add right-click menu support
    g_signal_connect(services_tab.treeview, "button-press-event",
                     G_CALLBACK(on_services_button_press), this);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Services"));
}

void TaskManager::setup_startup_tab() {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    startup_tab.store = gtk_list_store_new(4,
        G_TYPE_STRING,   // Name
        G_TYPE_BOOLEAN,  // Enabled
        G_TYPE_STRING,   // Source
        G_TYPE_STRING    // Path
    );

    startup_tab.treeview = gtk_tree_view_new_with_model(GTK_TREE_MODEL(startup_tab.store));
    g_object_unref(startup_tab.store);

    for (int i = 0; i < 4; i++) {
        GtkCellRenderer* renderer = gtk_cell_renderer_text_new();
        GtkTreeViewColumn* column = gtk_tree_view_column_new_with_attributes(
            headers3[i], renderer, "text", i, nullptr);
        gtk_tree_view_column_set_resizable(column, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(startup_tab.treeview), column);
    }

    gtk_container_add(GTK_CONTAINER(scrolled), startup_tab.treeview);

    // Add right-click menu support
    g_signal_connect(startup_tab.treeview, "button-press-event",
                     G_CALLBACK(on_startup_button_press), this);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Startup"));
}

void TaskManager::setup_performance_tab() {
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
        GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);

    GtkWidget* cpu_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(cpu_title), "<b>CPU Usage</b>");
    gtk_box_pack_start(GTK_BOX(vbox), cpu_title, FALSE, FALSE, 0);

    cpu_drawing_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(cpu_drawing_area), -1, 80);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(cpu_drawing_area), FALSE, FALSE, 0);
    g_signal_connect(cpu_drawing_area, "draw", G_CALLBACK(on_perf_draw), this);

    cpu_label = GTK_LABEL(gtk_label_new("CPU: 0.0%"));
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(cpu_label), FALSE, FALSE, 0);

    GtkWidget* mem_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(mem_title), "<b>Memory Usage</b>");
    gtk_box_pack_start(GTK_BOX(vbox), mem_title, FALSE, FALSE, 0);

    mem_drawing_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(mem_drawing_area), -1, 80);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(mem_drawing_area), FALSE, FALSE, 0);
    g_signal_connect(mem_drawing_area, "draw", G_CALLBACK(on_mem_draw), this);

    mem_label = GTK_LABEL(gtk_label_new("Memory: 0 MB / 0 MB (0.0%)"));
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(mem_label), FALSE, FALSE, 0);

    GtkWidget* net_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(net_title), "<b>Network I/O</b>");
    gtk_box_pack_start(GTK_BOX(vbox), net_title, FALSE, FALSE, 0);

    net_drawing_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(net_drawing_area), -1, 80);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(net_drawing_area), FALSE, FALSE, 0);
    g_signal_connect(net_drawing_area, "draw", G_CALLBACK(on_net_draw), this);

    net_label = GTK_LABEL(gtk_label_new("Network: 0 Mbps"));
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(net_label), FALSE, FALSE, 0);

    GtkWidget* gpu_title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(gpu_title), "<b>GPU Usage</b>");
    gtk_box_pack_start(GTK_BOX(vbox), gpu_title, FALSE, FALSE, 0);

    gpu_drawing_area = GTK_DRAWING_AREA(gtk_drawing_area_new());
    gtk_widget_set_size_request(GTK_WIDGET(gpu_drawing_area), -1, 80);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(gpu_drawing_area), FALSE, FALSE, 0);
    g_signal_connect(gpu_drawing_area, "draw", G_CALLBACK(on_gpu_draw), this);

    gpu_label = GTK_LABEL(gtk_label_new("GPU: 0.0%"));
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(gpu_label), FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), gtk_label_new(nullptr), TRUE, TRUE, 0);

    gtk_container_add(GTK_CONTAINER(scrolled), vbox);
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), scrolled, gtk_label_new("Performance"));
}

gboolean TaskManager::refresh_data(gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    self->refresh_processes();
    self->refresh_services();
    self->refresh_startup();

    static int perf_counter = 0;
    if (perf_counter++ % 2 == 0) {
        self->refresh_performance();
    }
    return FALSE;
}

void TaskManager::refresh_processes() {
    try {
        auto new_procs = ProcParser::get_all_processes();

        auto now = std::chrono::steady_clock::now();
        double time_diff_secs = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_refresh_time).count();
        last_refresh_time = now;

        long ticks_per_second = sysconf(_SC_CLK_TCK);
        int num_cpus = sysconf(_SC_NPROCESSORS_ONLN);
        if (num_cpus <= 0) num_cpus = 1;

        SystemStats stats = ProcParser::get_system_stats();

        // FIXED: Use total_memory instead of available_memory for correct percentage
        uint64_t total_mem = stats.total_memory;
        if (total_mem == 0) total_mem = 1; // Prevent division by zero

        for (auto& proc : new_procs) {
            // CPU usage calculation: (cpu_time_delta / ticks_per_sec / time_delta) * 100
            // This gives total CPU usage across all cores (can exceed 100% on multi-core systems)
            if (last_cpu_times.count(proc.pid) && time_diff_secs > 0.001) {
                long cpu_diff = proc.cpu_time - last_cpu_times[proc.pid];
                if (cpu_diff > 0) {
                    // Convert jiffies to seconds, then to percentage
                    double cpu_seconds = static_cast<double>(cpu_diff) / static_cast<double>(ticks_per_second);
                    proc.cpu_usage = (cpu_seconds / time_diff_secs) * 100.0;
                } else {
                    proc.cpu_usage = 0.0;
                }
            } else {
                proc.cpu_usage = 0.0;
            }
            last_cpu_times[proc.pid] = proc.cpu_time;

            // FIXED: Correct memory percentage calculation
            proc.memory_usage = (static_cast<double>(proc.memory_rss) / static_cast<double>(total_mem)) * 100.0;
        }

        processes_tab.processes = std::move(new_procs);

        // Build map of old PIDs
        std::map<pid_t, GtkTreeIter> old_pids;
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(processes_tab.store), &iter)) {
            do {
                gint pid;
                gtk_tree_model_get(GTK_TREE_MODEL(processes_tab.store), &iter, 0, &pid, -1);
                old_pids[pid] = iter;
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(processes_tab.store), &iter));
        }

        // Build set of new PIDs
        std::set<pid_t> new_pids;
        for (const auto& proc : processes_tab.processes) {
            new_pids.insert(proc.pid);
        }

        // Remove dead processes
        for (auto it = old_pids.begin(); it != old_pids.end(); ) {
            if (new_pids.find(it->first) == new_pids.end()) {
                gtk_list_store_remove(processes_tab.store, &it->second);
                it = old_pids.erase(it);
            } else {
                ++it;
            }
        }

        // Create map of new procs by PID for O(1) lookup
        std::map<pid_t, const ProcessInfo*> new_proc_map;
        for (const auto& proc : processes_tab.processes) {
            new_proc_map[proc.pid] = &proc;
        }

        // Update existing processes
        for (auto& pair : old_pids) {
            pid_t pid = pair.first;
            if (new_proc_map.count(pid)) {
                const ProcessInfo* proc = new_proc_map[pid];
                gtk_list_store_set(processes_tab.store, &pair.second,
                    0, proc->pid,
                    1, proc->name.c_str(),
                    2, proc->cpu_usage,
                    3, proc->memory_usage,
                    4, proc->memory_rss / (1024 * 1024),
                    5, proc->thread_count,
                    6, proc->user.c_str(),
                    7, proc->state.c_str(),
                    -1);
            }
        }

        // Add new processes
        for (const auto& proc : processes_tab.processes) {
            if (!old_pids.count(proc.pid)) {
                GtkTreeIter new_iter;
                gtk_list_store_append(processes_tab.store, &new_iter);
                gtk_list_store_set(processes_tab.store, &new_iter,
                    0, proc.pid,
                    1, proc.name.c_str(),
                    2, proc.cpu_usage,
                    3, proc.memory_usage,
                    4, proc.memory_rss / (1024 * 1024),
                    5, proc.thread_count,
                    6, proc.user.c_str(),
                    7, proc.state.c_str(),
                    -1);
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "Error refreshing processes: " << e.what() << std::endl;
    }
}

void TaskManager::refresh_services() const {
    try {
        auto new_services = SystemdManager::get_all_services();

        DEBUG_ACTION(std::cerr << "DEBUG: Found " << new_services.size() << " services" << std::endl);

        std::map<std::string, GtkTreeIter> old_services;
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(services_tab.store), &iter)) {
            do {
                gchar* name = nullptr;
                gtk_tree_model_get(GTK_TREE_MODEL(services_tab.store), &iter, 0, &name, -1);
                if (name) {
                    old_services[std::string(name)] = iter;
                    g_free(name);
                }
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(services_tab.store), &iter));
        }

        std::set<std::string> new_service_names;
        for (const auto& svc : new_services) {
            new_service_names.insert(svc.name);
        }

        for (auto it = old_services.begin(); it != old_services.end(); ) {
            if (!new_service_names.count(it->first)) {
                gtk_list_store_remove(services_tab.store, &it->second);
                it = old_services.erase(it);
            } else {
                ++it;
            }
        }

        for (const auto& svc : new_services) {
            auto it = old_services.find(svc.name);
            if (it != old_services.end()) {
                gtk_list_store_set(services_tab.store, &it->second,
                    0, svc.name.c_str(),
                    1, svc.description.c_str(),
                    2, svc.state.c_str(),
                    3, svc.active.c_str(),
                    4, static_cast<gint>(svc.main_pid),
                    -1);
            } else {
                GtkTreeIter new_iter;
                gtk_list_store_append(services_tab.store, &new_iter);
                gtk_list_store_set(services_tab.store, &new_iter,
                    0, svc.name.c_str(),
                    1, svc.description.c_str(),
                    2, svc.state.c_str(),
                    3, svc.active.c_str(),
                    4, static_cast<gint>(svc.main_pid),
                    -1);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error refreshing services: " << e.what() << std::endl;
    }
}

void TaskManager::refresh_startup() const {
    try {
        auto new_startups = SystemdManager::get_startup_entries();

        std::map<std::string, GtkTreeIter> old_startups;
        GtkTreeIter iter;
        if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(startup_tab.store), &iter)) {
            do {
                gchar* path = nullptr;
                gtk_tree_model_get(GTK_TREE_MODEL(startup_tab.store), &iter, 3, &path, -1);
                if (path) {
                    old_startups[std::string(path)] = iter;
                    g_free(path);
                }
            } while (gtk_tree_model_iter_next(GTK_TREE_MODEL(startup_tab.store), &iter));
        }

        std::set<std::string> new_startup_paths;
        for (const auto& entry : new_startups) {
            new_startup_paths.insert(entry.path);
        }

        for (auto it = old_startups.begin(); it != old_startups.end(); ) {
            if (!new_startup_paths.count(it->first)) {
                gtk_list_store_remove(startup_tab.store, &it->second);
                it = old_startups.erase(it);
            } else {
                ++it;
            }
        }

        for (const auto& entry : new_startups) {
            auto it = old_startups.find(entry.path);
            if (it != old_startups.end()) {
                gtk_list_store_set(startup_tab.store, &it->second,
                    0, entry.name.c_str(),
                    1, entry.enabled,
                    2, entry.source.c_str(),
                    3, entry.path.c_str(),
                    -1);
            } else {
                GtkTreeIter new_iter;
                gtk_list_store_append(startup_tab.store, &new_iter);
                gtk_list_store_set(startup_tab.store, &new_iter,
                    0, entry.name.c_str(),
                    1, entry.enabled,
                    2, entry.source.c_str(),
                    3, entry.path.c_str(),
                    -1);
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error refreshing startup: " << e.what() << std::endl;
    }
}

void TaskManager::refresh_performance() {
    SystemStats stats = ProcParser::get_system_stats();

    perf_data.current_cpu = stats.total_cpu_usage;
    uint64_t used_mem = stats.total_memory - stats.available_memory;
    perf_data.current_mem = (used_mem * 100.0) / stats.total_memory;

    // Calculate network usage
    auto now = std::chrono::steady_clock::now();
    double time_diff = std::chrono::duration_cast<std::chrono::duration<double>>(now - last_network_time).count();

    if (time_diff > 0.1) {  // Only calculate if enough time has passed
        NetworkStats current_net = get_network_stats();

        uint64_t bytes_diff = (current_net.bytes_recv + current_net.bytes_sent) -
                              (last_network_stats.bytes_recv + last_network_stats.bytes_sent);

        // Convert to Mbps
        perf_data.current_net = (bytes_diff * 8.0) / (time_diff * 1000000.0);

        last_network_stats = current_net;
        last_network_time = now;
    }

    // GPU is placeholder for now - could be expanded with nvidia-smi or similar
    perf_data.current_gpu = 0.0;

    if (perf_data.cpu_history.size() >= perf_data.max_history) {
        perf_data.cpu_history.erase(perf_data.cpu_history.begin());
    }
    perf_data.cpu_history.push_back(stats.total_cpu_usage);

    if (perf_data.mem_history.size() >= perf_data.max_history) {
        perf_data.mem_history.erase(perf_data.mem_history.begin());
    }
    perf_data.mem_history.push_back(perf_data.current_mem);

    if (perf_data.net_history.size() >= perf_data.max_history) {
        perf_data.net_history.erase(perf_data.net_history.begin());
    }
    perf_data.net_history.push_back(perf_data.current_net);

    if (perf_data.gpu_history.size() >= perf_data.max_history) {
        perf_data.gpu_history.erase(perf_data.gpu_history.begin());
    }
    perf_data.gpu_history.push_back(perf_data.current_gpu);

    gchar* cpu_text = g_strdup_printf("CPU: %.1f%%", perf_data.current_cpu);
    gtk_label_set_text(cpu_label, cpu_text);
    g_free(cpu_text);

    gchar* mem_text = g_strdup_printf("Memory: %lu MB / %lu MB (%.1f%%)",
        used_mem / (1024 * 1024),
        stats.total_memory / (1024 * 1024),
        perf_data.current_mem);
    gtk_label_set_text(mem_label, mem_text);
    g_free(mem_text);

    gchar* net_text = g_strdup_printf("Network: %.1f Mbps", perf_data.current_net);
    gtk_label_set_text(net_label, net_text);
    g_free(net_text);

    gchar* gpu_text = g_strdup_printf("GPU: %.1f%%", perf_data.current_gpu);
    gtk_label_set_text(gpu_label, gpu_text);
    g_free(gpu_text);

    if (cpu_drawing_area) gtk_widget_queue_draw(GTK_WIDGET(cpu_drawing_area));
    if (mem_drawing_area) gtk_widget_queue_draw(GTK_WIDGET(mem_drawing_area));
    if (net_drawing_area) gtk_widget_queue_draw(GTK_WIDGET(net_drawing_area));
    if (gpu_drawing_area) gtk_widget_queue_draw(GTK_WIDGET(gpu_drawing_area));
}

static inline void draw_graph(GtkWidget* widget, cairo_t* cr, const std::vector<double>& history,
                               double max_val, double r, double g, double b) {
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);

    if (alloc.width <= 1 || alloc.height <= 1) return;

    cairo_set_source_rgb(cr, 0.15, 0.15, 0.15);
    cairo_rectangle(cr, 0, 0, alloc.width, alloc.height);
    cairo_fill(cr);

    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    cairo_set_line_width(cr, 1.0);
    for (int i = 0; i <= 4; i++) {
        double y = static_cast<double>(alloc.height / 4) * i;
        cairo_move_to(cr, 0, y);
        cairo_line_to(cr, alloc.width, y);
    }
    cairo_stroke(cr);

    if (history.size() > 1) {
        cairo_set_source_rgb(cr, r, g, b);
        cairo_set_line_width(cr, 2.0);
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);

        double x_step = static_cast<double>(alloc.width) / (history.size() - 1);

        for (size_t i = 0; i < history.size(); i++) {
            double x = i * x_step;
            double val = history[i];
            if (val > max_val) val = max_val;
            double y = alloc.height - (val / max_val * alloc.height);

            if (i == 0) {
                cairo_move_to(cr, x, y);
            } else {
                cairo_line_to(cr, x, y);
            }
        }
        cairo_stroke(cr);
    }
}

gboolean TaskManager::on_perf_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    draw_graph(widget, cr, self->perf_data.cpu_history, 100.0, 0.0, 1.0, 0.0);
    return FALSE;
}

gboolean TaskManager::on_mem_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    draw_graph(widget, cr, self->perf_data.mem_history, 100.0, 0.2, 0.8, 1.0);
    return FALSE;
}

gboolean TaskManager::on_net_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    draw_graph(widget, cr, self->perf_data.net_history, MAX_NET, 1.0, 1.0, 0.0);
    return FALSE;
}

gboolean TaskManager::on_gpu_draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    draw_graph(widget, cr, self->perf_data.gpu_history, 100.0, 1.0, 0.5, 0.0);
    return FALSE;
}

gboolean TaskManager::on_delete_event(GtkWidget*, GdkEvent*, gpointer) {
    gtk_main_quit();
    return FALSE;
}

void TaskManager::on_end_process(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->processes_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gint pid;
        gtk_tree_model_get(model, &iter, 0, &pid, -1);

        if (ProcParser::terminate_process(pid, false)) {
            std::cerr << "Terminated process " << pid << std::endl;
        } else {
            std::cerr << "Failed to terminate process " << pid << std::endl;
        }
    }
}

void TaskManager::on_kill_process(GtkWidget*, gpointer data) {
    (void)data;
}

void TaskManager::on_suspend_process(GtkWidget*, gpointer data) {
    (void)data;
}

void TaskManager::on_priority_changed(GtkWidget*, gpointer data) {
    (void)data;
}

void TaskManager::on_column_clicked(GtkTreeViewColumn*, gpointer data) {
    (void)data;
}

void TaskManager::on_search_changed(GtkSearchEntry* entry, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    const char* query = gtk_entry_get_text(GTK_ENTRY(entry));
    self->current_search_query = (query ? query : "");

    gtk_tree_model_filter_refilter(self->processes_tab.filter);
}

void TaskManager::on_pause_toggled(GtkToggleButton* button, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);
    self->paused = gtk_toggle_button_get_active(button);
}

void TaskManager::update_treeview(const TabState& tab) {
    (void)tab;
}

void TaskManager::apply_search_filter(const std::string& query, const TabState& tab) {
    (void)query;
    (void)tab;
}

NetworkStats TaskManager::get_network_stats() {
    NetworkStats stats;
    stats.bytes_recv = 0;
    stats.bytes_sent = 0;

    std::ifstream net_dev("/proc/net/dev");
    if (!net_dev) return stats;

    std::string line;
    // Skip first two header lines
    std::getline(net_dev, line);
    std::getline(net_dev, line);

    while (std::getline(net_dev, line)) {
        std::istringstream iss(line);
        std::string interface;
        iss >> interface;

        // Skip loopback interface
        if (interface.find("lo:") != std::string::npos) continue;

        uint64_t bytes_recv, packets_recv, errs_recv, drop_recv, fifo_recv, frame_recv, compressed_recv, multicast_recv;
        uint64_t bytes_sent, packets_sent, errs_sent, drop_sent, fifo_sent, colls_sent, carrier_sent, compressed_sent;

        iss >> bytes_recv >> packets_recv >> errs_recv >> drop_recv >> fifo_recv >> frame_recv >> compressed_recv >> multicast_recv;
        iss >> bytes_sent >> packets_sent >> errs_sent >> drop_sent >> fifo_sent >> colls_sent >> carrier_sent >> compressed_sent;

        stats.bytes_recv += bytes_recv;
        stats.bytes_sent += bytes_sent;
    }

    return stats;
}

gboolean TaskManager::on_services_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {  // Right click
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
        GtkTreePath* path;

        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                          static_cast<gint>(event->x),
                                          static_cast<gint>(event->y),
                                          &path, nullptr, nullptr, nullptr)) {
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);

            GtkWidget* menu = gtk_menu_new();

            GtkWidget* start_item = gtk_menu_item_new_with_label("Start");
            GtkWidget* stop_item = gtk_menu_item_new_with_label("Stop");
            GtkWidget* restart_item = gtk_menu_item_new_with_label("Restart");

            g_signal_connect(start_item, "activate", G_CALLBACK(on_service_start), self);
            g_signal_connect(stop_item, "activate", G_CALLBACK(on_service_stop), self);
            g_signal_connect(restart_item, "activate", G_CALLBACK(on_service_restart), self);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), start_item);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), stop_item);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), restart_item);

            gtk_widget_show_all(menu);
            gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));

            return TRUE;
        }
    }

    return FALSE;
}

gboolean TaskManager::on_startup_button_press(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    if (event->type == GDK_BUTTON_PRESS && event->button == 3) {  // Right click
        GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(widget));
        GtkTreePath* path;

        if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),
                                          static_cast<gint>(event->x),
                                          static_cast<gint>(event->y),
                                          &path, nullptr, nullptr, nullptr)) {
            gtk_tree_selection_select_path(selection, path);
            gtk_tree_path_free(path);

            GtkWidget* menu = gtk_menu_new();

            GtkWidget* enable_item = gtk_menu_item_new_with_label("Enable");
            GtkWidget* disable_item = gtk_menu_item_new_with_label("Disable");

            g_signal_connect(enable_item, "activate", G_CALLBACK(on_startup_enable), self);
            g_signal_connect(disable_item, "activate", G_CALLBACK(on_startup_disable), self);

            gtk_menu_shell_append(GTK_MENU_SHELL(menu), enable_item);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), disable_item);

            gtk_widget_show_all(menu);
            gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));

            return TRUE;
        }
    }

    return FALSE;
}

void TaskManager::on_service_start(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->services_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* name = nullptr;
        gtk_tree_model_get(model, &iter, 0, &name, -1);

        if (name) {
            if (SystemdManager::start_service(name)) {
                std::cout << "Started service: " << name << std::endl;
            } else {
                std::cerr << "Failed to start service: " << name << std::endl;
            }
            g_free(name);
        }
    }
}

void TaskManager::on_service_stop(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->services_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* name = nullptr;
        gtk_tree_model_get(model, &iter, 0, &name, -1);

        if (name) {
            if (SystemdManager::stop_service(name)) {
                std::cout << "Stopped service: " << name << std::endl;
            } else {
                std::cerr << "Failed to stop service: " << name << std::endl;
            }
            g_free(name);
        }
    }
}

void TaskManager::on_service_restart(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->services_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* name = nullptr;
        gtk_tree_model_get(model, &iter, 0, &name, -1);

        if (name) {
            if (SystemdManager::restart_service(name)) {
                std::cout << "Restarted service: " << name << std::endl;
            } else {
                std::cerr << "Failed to restart service: " << name << std::endl;
            }
            g_free(name);
        }
    }
}

void TaskManager::on_startup_enable(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->startup_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* path = nullptr;
        gtk_tree_model_get(model, &iter, 3, &path, -1);

        if (path) {
            if (SystemdManager::enable_startup(path)) {
                std::cout << "Enabled startup entry: " << path << std::endl;
            } else {
                std::cerr << "Failed to enable startup entry: " << path << std::endl;
            }
            g_free(path);
        }
    }
}

void TaskManager::on_startup_disable(GtkWidget*, gpointer data) {
    auto* self = static_cast<TaskManager*>(data);

    GtkTreeSelection* selection = gtk_tree_view_get_selection(
        GTK_TREE_VIEW(self->startup_tab.treeview));
    GtkTreeIter iter;
    GtkTreeModel* model;

    if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
        gchar* path = nullptr;
        gtk_tree_model_get(model, &iter, 3, &path, -1);

        if (path) {
            if (SystemdManager::disable_startup(path)) {
                std::cout << "Disabled startup entry: " << path << std::endl;
            } else {
                std::cerr << "Failed to disable startup entry: " << path << std::endl;
            }
            g_free(path);
        }
    }
}