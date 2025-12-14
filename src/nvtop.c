/*
 * Copyright (C) 2017-2021 Maxime Schmitt <maxime.schmitt91@gmail.com>
 *
 * This file is part of Nvtop.
 * License: GPLv3
 */

#include "nvtop/extract_gpuinfo.h"
#include "nvtop/info_messages.h"
#include "nvtop/interface.h"
#include "nvtop/interface_common.h"
#include "nvtop/interface_options.h"
#include "nvtop/time.h"
#include "nvtop/version.h"

#include <getopt.h>
#include <ncurses.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>      // Added for sleep
#include <locale.h>

// Added for manual GPU field validation
#include "nvtop/extract_gpuinfo_common.h"

static volatile sig_atomic_t signal_exit = 0;
static volatile sig_atomic_t signal_resize_win = 0;
static volatile sig_atomic_t signal_cont_received = 0;

static void exit_handler(int signum) {
  (void)signum;
  signal_exit = 1;
}

static void resize_handler(int signum) {
  (void)signum;
  signal_resize_win = 1;
}

static void cont_handler(int signum) {
  (void)signum;
  signal_cont_received = 1;
}

static const char helpstring[] = "Available options:\n"
"  -d --delay        : Select the refresh rate (1 == 0.1s)\n"
"  -v --version      : Print the version and exit\n"
"  -c --config-file  : Provide a custom config file location to load/save "
"preferences\n"
"  -p --no-plot      : Disable bar plot\n"
"  -P --no-processes : Disable process list\n"
"  -r --reverse-abs  : Reverse abscissa: plot the recent data left and "
"older on the right\n"
"  -C --no-color     : No colors\n"
"line information\n"
"  -f --freedom-unit : Use fahrenheit\n"
"  -i --gpu-info     : Show bar with additional GPU parametres\n"
"  -E --encode-hide  : Set encode/decode auto hide time in seconds "
"(default 30s, negative = always on screen)\n"
"  -h --help         : Print help and exit\n"
"  -s --snapshot     : Output the current gpu stats without ncurses"
"(useful for scripting)\n";

static const char versionString[] = "nvtop version " NVTOP_VERSION_STRING;

static const struct option long_opts[] = {
  {.name = "delay", .has_arg = required_argument, .flag = NULL, .val = 'd'},
  {.name = "version", .has_arg = no_argument, .flag = NULL, .val = 'v'},
  {.name = "help", .has_arg = no_argument, .flag = NULL, .val = 'h'},
  {.name = "config-file", .has_arg = required_argument, .flag = NULL, .val = 'c'},
  {.name = "no-color", .has_arg = no_argument, .flag = NULL, .val = 'C'},
  {.name = "no-colour", .has_arg = no_argument, .flag = NULL, .val = 'C'},
  {.name = "freedom-unit", .has_arg = no_argument, .flag = NULL, .val = 'f'},
  {.name = "gpu-info", .has_arg = no_argument, .flag = NULL, .val = 'i'},
  {.name = "encode-hide", .has_arg = required_argument, .flag = NULL, .val = 'E'},
  {.name = "no-plot", .has_arg = no_argument, .flag = NULL, .val = 'p'},
  {.name = "no-processes", .has_arg = no_argument, .flag = NULL, .val = 'P'},
  {.name = "reverse-abs", .has_arg = no_argument, .flag = NULL, .val = 'r'},
  {.name = "snapshot", .has_arg = no_argument, .flag = NULL, .val = 's'},
  {0, 0, 0, 0},
};

static const char opts[] = "hvd:c:CfE:pPris";

int main(int argc, char **argv) {
  (void)setlocale(LC_CTYPE, "");

  opterr = 0;
  bool update_interval_option_set = false;
  int update_interval_option;
  bool no_color_option = false;
  bool use_fahrenheit_option = false;
  bool hide_plot_option = false;
  bool hide_processes_option = false;
  bool reverse_plot_direction_option = false;
  bool encode_decode_timer_option_set = false;
  bool show_gpu_info_bar = false;
  bool show_snapshot = false;
  double encode_decode_hide_time = -1.;
  char *custom_config_file_path = NULL;
  while (true) {
    int optchar = getopt_long(argc, argv, opts, long_opts, NULL);
    if (optchar == -1)
      break;
    switch (optchar) {
      case 'd': {
        char *endptr = NULL;
        long int delay_val = strtol(optarg, &endptr, 0);
        if (endptr == optarg) {
          fprintf(stderr, "Error: The delay must be a positive value "
          "representing tenths of seconds\n");
          exit(EXIT_FAILURE);
        }
        if (delay_val < 0) {
          fprintf(stderr, "Error: A negative delay requires a time machine!\n");
          exit(EXIT_FAILURE);
        }
        update_interval_option_set = true;
        update_interval_option = (int)delay_val * 100u;
        if (update_interval_option > 99900)
          update_interval_option = 99900;
        if (update_interval_option < 100)
          update_interval_option = 100;
      } break;
      case 'v':
        printf("%s\n", versionString);
        exit(EXIT_SUCCESS);
      case 'h':
        printf("%s\n%s", versionString, helpstring);
        exit(EXIT_SUCCESS);
      case 'c':
        custom_config_file_path = optarg;
        break;
      case 'C':
        no_color_option = true;
        break;
      case 'f':
        use_fahrenheit_option = true;
        break;
      case 'i':
        show_gpu_info_bar = true;
        break;
      case 'E': {
        if (sscanf(optarg, "%lf", &encode_decode_hide_time) == EOF) {
          fprintf(stderr, "Invalid format for encode/decode hide time: %s\n", optarg);
          exit(EXIT_FAILURE);
        }
        encode_decode_timer_option_set = true;
      } break;
      case 'p':
        hide_plot_option = true;
        break;
      case 'P':
        hide_processes_option = true;
        break;
      case 'r':
        reverse_plot_direction_option = true;
        break;
      case 's':
        show_snapshot = true;
        break;
      case ':':
      case '?':
        switch (optopt) {
          case 'd':
            fprintf(stderr, "Error: The delay option takes a positive value "
            "representing tenths of seconds\n");
            break;
          default:
            fprintf(stderr, "Unhandled error in getopt missing argument\n");
            exit(EXIT_FAILURE);
            break;
        }
        exit(EXIT_FAILURE);
    }
  }

  setenv("ESCDELAY", "10", 1);

  struct sigaction siga;
  siga.sa_flags = 0;
  sigemptyset(&siga.sa_mask);
  siga.sa_handler = exit_handler;

  if (sigaction(SIGINT, &siga, NULL) != 0) {
    perror("Impossible to set signal handler for SIGINT: ");
    exit(EXIT_FAILURE);
  }
  if (sigaction(SIGQUIT, &siga, NULL) != 0) {
    perror("Impossible to set signal handler for SIGQUIT: ");
    exit(EXIT_FAILURE);
  }
  siga.sa_handler = resize_handler;
  if (sigaction(SIGWINCH, &siga, NULL) != 0) {
    perror("Impossible to set signal handler for SIGWINCH: ");
    exit(EXIT_FAILURE);
  }
  siga.sa_handler = cont_handler;
  if (sigaction(SIGCONT, &siga, NULL) != 0) {
    perror("Impossible to set signal handler for SIGCONT: ");
    exit(EXIT_FAILURE);
  }

  unsigned allDevCount = 0;
  LIST_HEAD(monitoredGpus);
  LIST_HEAD(nonMonitoredGpus);
  if (!gpuinfo_init_info_extraction(&allDevCount, &monitoredGpus))
    return EXIT_FAILURE;
  if (allDevCount == 0) {
    fprintf(stdout, "No GPU to monitor.\n");
    return EXIT_SUCCESS;
  }

  unsigned numWarningMessages = 0;
  const char **warningMessages;
  get_info_messages(&monitoredGpus, &numWarningMessages, &warningMessages);

  nvtop_interface_option allDevicesOptions;
  alloc_interface_options_internals(custom_config_file_path, allDevCount, &monitoredGpus, &allDevicesOptions);
  load_interface_options_from_config_file(allDevCount, &allDevicesOptions);
  for (unsigned i = 0; i < allDevCount; ++i) {
    if (!plot_isset_draw_info(plot_information_count, allDevicesOptions.gpu_specific_opts[i].to_draw)) {
      allDevicesOptions.gpu_specific_opts[i].to_draw = plot_default_draw_info();
    } else {
      allDevicesOptions.gpu_specific_opts[i].to_draw =
      plot_remove_draw_info(plot_information_count, allDevicesOptions.gpu_specific_opts[i].to_draw);
    }
  }
  if (!process_is_field_displayed(process_field_count, allDevicesOptions.process_fields_displayed)) {
    allDevicesOptions.process_fields_displayed = process_default_displayed_field();
  } else {
    allDevicesOptions.process_fields_displayed =
    process_remove_field_to_display(process_field_count, allDevicesOptions.process_fields_displayed);
  }
  if (no_color_option)
    allDevicesOptions.use_color = false;
  if (hide_plot_option) {
    for (unsigned i = 0; i < allDevCount; ++i) {
      allDevicesOptions.gpu_specific_opts[i].to_draw = 0;
    }
  }
  allDevicesOptions.hide_processes_list = hide_processes_option;
  if (encode_decode_timer_option_set) {
    allDevicesOptions.encode_decode_hiding_timer = encode_decode_hide_time;
    if (allDevicesOptions.encode_decode_hiding_timer < 0.)
      allDevicesOptions.encode_decode_hiding_timer = 0.;
  }
  if (reverse_plot_direction_option)
    allDevicesOptions.plot_left_to_right = true;
  if (use_fahrenheit_option)
    allDevicesOptions.temperature_in_fahrenheit = true;
  if (update_interval_option_set)
    allDevicesOptions.update_interval = update_interval_option;
  allDevicesOptions.has_gpu_info_bar = allDevicesOptions.has_gpu_info_bar || show_gpu_info_bar;

  gpuinfo_populate_static_infos(&monitoredGpus);
  unsigned numMonitoredGpus =
  interface_check_and_fix_monitored_gpus(allDevCount, &monitoredGpus, &nonMonitoredGpus, &allDevicesOptions);

  if (allDevicesOptions.show_startup_messages) {
    bool dont_show_again = show_information_messages(numWarningMessages, warningMessages);
    if (dont_show_again) {
      allDevicesOptions.show_startup_messages = false;
      save_interface_options_to_config_file(allDevCount, &allDevicesOptions);
    }
  }

  // ====================================================================================
  // CUSTOM FIX: Manual JSON Snapshot Printer
  // ====================================================================================
  if (show_snapshot) {
    // 1. Warm-up Pass
    gpuinfo_refresh_dynamic_info(&monitoredGpus);
    gpuinfo_refresh_processes(&monitoredGpus);
    usleep(250000);

    // 2. Start Pass
    gpuinfo_refresh_dynamic_info(&monitoredGpus);
    gpuinfo_refresh_processes(&monitoredGpus);

    // 3. Work Interval (1 Second)
    usleep(1000000);

    // 4. Finish Pass
    gpuinfo_refresh_dynamic_info(&monitoredGpus);
    gpuinfo_refresh_processes(&monitoredGpus);

    // 5. Calculate Rates (Standard)
    gpuinfo_utilisation_rate(&monitoredGpus);

    // 6. MANUAL JSON OUTPUT
    printf("[\n");

    struct list_head *ptr;
    int is_first = 1;
    list_for_each(ptr, &monitoredGpus) {
      struct gpu_info *device = list_entry(ptr, struct gpu_info, list);

      if (!is_first) printf(",\n");
      is_first = 0;

      // --- Calculate True Usage (Summing Processes) ---
      unsigned total_usage = 0;
      for (unsigned i = 0; i < device->processes_count; ++i) {
        if (GPUINFO_PROCESS_FIELD_VALID(&device->processes[i], gpu_usage)) {
          total_usage += device->processes[i].gpu_usage;
        }
      }
      if (total_usage > 100) total_usage = 100;

      // --- Print JSON ---
      printf("  {\n");
      printf("   \"device_name\": \"%s\",\n", device->static_info.device_name);

      // Clock
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_clock_speed))
        printf("   \"gpu_clock\": \"%uMHz\",\n", device->dynamic_info.gpu_clock_speed);
      else
        printf("   \"gpu_clock\": null,\n");

      // Temp
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, gpu_temp))
        printf("   \"temp\": \"%uC\",\n", device->dynamic_info.gpu_temp);
      else
        printf("   \"temp\": null,\n");

      // Fan
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, fan_rpm))
        printf("   \"fan_speed\": \"%uRPM\",\n", device->dynamic_info.fan_rpm);
      else
        printf("   \"fan_speed\": null,\n");

      // Power
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, power_draw))
        // CHANGE THIS LINE BELOW: Add " / 1000"
        printf("   \"power_draw\": \"%uW\",\n", device->dynamic_info.power_draw / 1000);
      else
        printf("   \"power_draw\": null,\n");

      // GPU Util (THE FIXED VALUE)
      printf("   \"gpu_util\": \"%u%%\",\n", total_usage);

      // Mem Util
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, mem_util_rate))
        printf("   \"mem_util\": \"%u%%\",\n", device->dynamic_info.mem_util_rate);
      else
        printf("   \"mem_util\": null,\n");

      // Memory Details
      if (GPUINFO_DYNAMIC_FIELD_VALID(&device->dynamic_info, total_memory)) {
        printf("   \"mem_total\": \"%lu\",\n", device->dynamic_info.total_memory);
        printf("   \"mem_used\": \"%lu\",\n", device->dynamic_info.used_memory);
        printf("   \"mem_free\": \"%lu\"\n", device->dynamic_info.free_memory);
      } else {
        printf("   \"mem_total\": null\n");
      }

      printf("  }");
    }
    printf("\n]\n");

    gpuinfo_shutdown_info_extraction(&monitoredGpus);
    return EXIT_SUCCESS;
  }
  // ====================================================================================

  struct nvtop_interface *interface =
  initialize_curses(allDevCount, numMonitoredGpus, interface_largest_gpu_name(&monitoredGpus), allDevicesOptions);
  timeout(interface_update_interval(interface));

  double time_slept = interface_update_interval(interface);
  while (!signal_exit) {
    if (signal_resize_win) {
      signal_resize_win = 0;
      update_window_size_to_terminal_size(interface);
    }
    if (signal_cont_received) {
      signal_cont_received = 0;
      update_window_size_to_terminal_size(interface);
    }
    interface_check_monitored_gpu_change(&interface, allDevCount, &numMonitoredGpus, &monitoredGpus, &nonMonitoredGpus);
    if (time_slept >= interface_update_interval(interface)) {
      gpuinfo_refresh_dynamic_info(&monitoredGpus);
      if (!interface_freeze_processes(interface)) {
        gpuinfo_refresh_processes(&monitoredGpus);
        gpuinfo_utilisation_rate(&monitoredGpus);
        gpuinfo_fix_dynamic_info_from_process_info(&monitoredGpus);
      }
      save_current_data_to_ring(&monitoredGpus, interface);
      timeout(interface_update_interval(interface));
      time_slept = 0.;
    } else {
      int next_sleep = interface_update_interval(interface) - (int)time_slept;
      timeout(next_sleep);
    }
    draw_gpu_info_ncurses(numMonitoredGpus, &monitoredGpus, interface);

    nvtop_time time_before_sleep, time_after_sleep;
    nvtop_get_current_time(&time_before_sleep);
    int input_char = getch();
    nvtop_get_current_time(&time_after_sleep);
    time_slept += nvtop_difftime(time_before_sleep, time_after_sleep) * 1000;
    switch (input_char) {
      case 27: // ESC
      {
        timeout(0);
        int in = getch();
        if (in == ERR) { // ESC alone
          if (is_escape_for_quit(interface))
            signal_exit = 1;
          else
            interface_key(27, interface);
        }
      } break;
      case KEY_F(10):
        if (is_escape_for_quit(interface))
          signal_exit = 1;
      break;
      case 'q':
        signal_exit = 1;
        break;
      case KEY_RESIZE:
        update_window_size_to_terminal_size(interface);
        break;
      case KEY_F(2):
      case KEY_F(5):
      case KEY_F(9):
      case KEY_F(6):
      case KEY_F(12):
      case '+':
      case '-':
      case 12: // Ctrl+L
        interface_key(input_char, interface);
        break;
      case 'k':
      case KEY_UP:
      case 'j':
      case KEY_DOWN:
      case 'h':
      case KEY_LEFT:
      case 'l':
      case KEY_RIGHT:
      case KEY_ENTER:
      case '\n':
        interface_key(input_char, interface);
        break;
      case ERR:
      default:
        break;
    }
  }

  clean_ncurses(interface);
  gpuinfo_shutdown_info_extraction(&monitoredGpus);

  return EXIT_SUCCESS;
}
