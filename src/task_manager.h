#pragma once

#include <gtk/gtk.h>
#include <vector>
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <map>
#include <set>
#include <chrono>
#include "proc_parser.h"
#include "systemd_manager.h"

struct TabState {
    GtkWidget* treeview = nullptr;
    GtkListStore* store = nullptr;
    GtkTreeModelFilter* filter = nullptr;
    std::vector<ProcessInfo> processes;
    GtkTreeViewColumn* sort_column = nullptr;
    GtkSortType sort_order = GTK_SORT_ASCENDING;
};

struct PerformanceData {
    std::vector<double> cpu_history;
    std::vector<double> mem_history;
    std::vector<double> net_history;
    std::vector<double> gpu_history;
    double current_cpu = 0;
    double current_mem = 0;
    double current_net = 0;
    double current_gpu = 0;
    const size_t max_history = 60;
};

class TaskManager {
public:
    TaskManager();
    ~TaskManager();

    void run();

private:
    GtkWidget* window = nullptr;
    GtkWidget* notebook = nullptr;

    // Scrolled windows for preserving scroll position
    GtkScrolledWindow* processes_scrolled = nullptr;
    GtkScrolledWindow* services_scrolled = nullptr;
    GtkScrolledWindow* startup_scrolled = nullptr;

    TabState processes_tab;
    TabState services_tab;
    TabState startup_tab;
    PerformanceData perf_data;
    GtkDrawingArea* cpu_drawing_area = nullptr;
    GtkDrawingArea* mem_drawing_area = nullptr;
    GtkDrawingArea* net_drawing_area = nullptr;
    GtkDrawingArea* gpu_drawing_area = nullptr;
    GtkLabel* cpu_label = nullptr;
    GtkLabel* mem_label = nullptr;
    GtkLabel* net_label = nullptr;
    GtkLabel* gpu_label = nullptr;

    std::thread refresh_thread;
    std::atomic<bool> running;
    std::atomic<bool> paused;

    ProcParser proc_parser;
    SystemdManager systemd_mgr;
    std::string current_search_query;
    std::map<pid_t, long> last_cpu_times;
    std::chrono::steady_clock::time_point last_refresh_time;

    // UI Callbacks
    static gboolean on_delete_event(GtkWidget* widget, GdkEvent* event, gpointer data);
    static void on_end_process(GtkWidget* widget, gpointer data);
    static void on_kill_process(GtkWidget* widget, gpointer data);
    static void on_suspend_process(GtkWidget* widget, gpointer data);
    static void on_priority_changed(GtkWidget* widget, gpointer data);
    static gboolean refresh_data(gpointer data);
    static void on_column_clicked(GtkTreeViewColumn* col, gpointer data);
    static void on_search_changed(GtkSearchEntry* entry, gpointer data);
    static void on_pause_toggled(GtkToggleButton* button, gpointer data);
    static gboolean on_perf_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean on_mem_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean on_net_draw(GtkWidget* widget, cairo_t* cr, gpointer data);
    static gboolean on_gpu_draw(GtkWidget* widget, cairo_t* cr, gpointer data);

    // Internal methods
    void setup_processes_tab();
    void setup_services_tab();
    void setup_startup_tab();
    void setup_performance_tab();
    void refresh_processes();
    void refresh_services() const;
    void refresh_startup() const;
    void refresh_performance();

    // Helper methods for scroll preservation
    void save_scroll_position(GtkScrolledWindow* scrolled, double& v_pos, double& h_pos);
    void restore_scroll_position(GtkScrolledWindow* scrolled, double v_pos, double h_pos);

    static void update_treeview(const TabState& tab);
    static void apply_search_filter(const std::string& query, const TabState& tab);
};