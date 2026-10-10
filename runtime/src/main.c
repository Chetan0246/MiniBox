#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "minibox.h"
#include "process.h"
#include "workload.h"
#include "lifecycle.h"
#include "isolation.h"
#include "resources.h"
#include "monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void print_usage(const char *progname) {
    fprintf(stderr, "MiniBox TUI Core Runtime (v%s)\n\n", MINIBOX_VERSION);
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s capabilities [--json]                                  Report detected Linux capabilities\n", progname);
    fprintf(stderr, "  %s run [options] <executable> [args...]                 Run workload synchronously in foreground\n", progname);
    fprintf(stderr, "  %s start [options] <executable> [args...]               Start workload asynchronously in background\n", progname);
    fprintf(stderr, "  %s list [--json]                                         List all workloads\n", progname);
    fprintf(stderr, "  %s inspect [--json] <workload_id>                        Inspect a workload\n", progname);
    fprintf(stderr, "  %s metrics [--json] [--sample-ms <ms>] [<workload_id>]    Collect resource metrics (alias: stats)\n", progname);
    fprintf(stderr, "  %s stop [--json] [--force] [--signal <num>] <id>         Stop a running workload\n", progname);
    fprintf(stderr, "  %s logs <workload_id>                                    View workload stdout/stderr log\n", progname);
    fprintf(stderr, "  %s delete [--json] [--force] <workload_id>               Delete a workload record\n", progname);
    fprintf(stderr, "  %s cleanup [--json]                                      Remove all completed/terminated workloads\n", progname);
    fprintf(stderr, "  %s version                                                Show version information\n", progname);
    fprintf(stderr, "  %s help                                                   Show this help message\n", progname);

    fprintf(stderr, "\nOptions for 'run' and 'start':\n");
    fprintf(stderr, "  --id <id>          Explicit workload identifier (must not already exist)\n");
    fprintf(stderr, "  --name <name>      Friendly name for the workload\n");
    fprintf(stderr, "  --workdir <dir>    Working directory for the process\n");
    fprintf(stderr, "  --cpu <pct>        CPU quota percentage of 1 CPU (e.g. 50, 100)\n");
    fprintf(stderr, "  --memory <size>    Memory limit with units (e.g. 64M, 256M, 1G)\n");
    fprintf(stderr, "  --pids <max>       Maximum number of processes (e.g. 10)\n");
    fprintf(stderr, "  --isolate          Enable full isolation (UTS, PID, and Mount namespaces)\n");
    fprintf(stderr, "  --uts              Enable UTS namespace isolation (isolated hostname)\n");
    fprintf(stderr, "  --hostname <name>  Custom hostname for container (enables UTS namespace)\n");
    fprintf(stderr, "  --pid-ns           Enable PID namespace isolation (process sees PID 1)\n");
    fprintf(stderr, "  --mount-ns         Enable mount namespace isolation\n");
    fprintf(stderr, "  --json             Output results in JSON format (run/start only)\n");
    fprintf(stderr, "  --                 Stop option parsing; everything after is the command\n");

    fprintf(stderr, "\nOptions for 'metrics':\n");
    fprintf(stderr, "  --json             Output results in JSON format\n");
    fprintf(stderr, "  --sample-ms <ms>   CPU sampling window for a single workload, 0-%d ms (default: 100)\n",
            MINIBOX_MAX_SAMPLE_MS);

    fprintf(stderr, "\nOptions for 'stop':\n");
    fprintf(stderr, "  --json             Output results in JSON format\n");
    fprintf(stderr, "  --force            Escalate to SIGKILL if the process does not exit in time\n");
    fprintf(stderr, "  --signal <num>     Signal number to send (default: 15 / SIGTERM)\n");

    fprintf(stderr, "\nOptions for 'delete':\n");
    fprintf(stderr, "  --json             Output results in JSON format\n");
    fprintf(stderr, "  --force            Delete even while the workload is running\n");
}

static void print_version(void) {
    printf("%s version %s (Phase 8: Final Integration & Educational Deliverables)\n", MINIBOX_PROGNAME, MINIBOX_VERSION);
}

static int cmd_capabilities(int argc, char *argv[]) {
    bool json_output = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        }
    }

    minibox_capabilities_t caps;
    minibox_detect_capabilities(&caps);

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": true,\n");
        printf("  \"user_namespaces\": %s,\n", caps.user_ns_supported ? "true" : "false");
        printf("  \"uts_namespace\": %s,\n", caps.uts_ns_supported ? "true" : "false");
        printf("  \"pid_namespace\": %s,\n", caps.pid_ns_supported ? "true" : "false");
        printf("  \"mount_namespace\": %s,\n", caps.mount_ns_supported ? "true" : "false");
        printf("  \"cgroups_v2\": %s,\n", caps.cgroups_v2_supported ? "true" : "false");
        printf("  \"granular\": {\n");
        printf("    \"kernel_user_ns\": %s,\n", caps.kernel_user_ns ? "true" : "false");
        printf("    \"unprivileged_user_ns\": %s,\n", caps.unprivileged_user_ns ? "true" : "false");
        printf("    \"pid_ns_child_tested\": %s,\n", caps.pid_ns_child_tested ? "true" : "false");
        printf("    \"cgroup_v2_kernel\": %s,\n", caps.cgroup_v2_kernel ? "true" : "false");
        printf("    \"cgroup_v2_delegated\": %s,\n", caps.cgroup_v2_delegated ? "true" : "false");
        printf("    \"cgroup_cpu_controller\": %s,\n", caps.cgroup_cpu_controller ? "true" : "false");
        printf("    \"cgroup_cpu_writable\": %s,\n", caps.cgroup_cpu_writable ? "true" : "false");
        printf("    \"cgroup_memory_controller\": %s,\n", caps.cgroup_memory_controller ? "true" : "false");
        printf("    \"cgroup_memory_writable\": %s,\n", caps.cgroup_memory_writable ? "true" : "false");
        printf("    \"cgroup_pids_controller\": %s,\n", caps.cgroup_pids_controller ? "true" : "false");
        printf("    \"cgroup_pids_writable\": %s\n", caps.cgroup_pids_writable ? "true" : "false");
        printf("  },\n");
        printf("  \"reason\": \"%s\"\n", caps.reason);
        printf("}\n");
    } else {
        printf("MiniBox Linux Capability Report:\n");
        printf("  User Namespaces:           %s (kernel: %s, unprivileged: %s)\n",
               caps.user_ns_supported ? "AVAILABLE" : "UNAVAILABLE",
               caps.kernel_user_ns ? "yes" : "no",
               caps.unprivileged_user_ns ? "yes" : "no");
        printf("  UTS Namespace:             %s\n", caps.uts_ns_supported ? "AVAILABLE" : "UNAVAILABLE");
        printf("  PID Namespace:             %s (child PID 1 tested: %s)\n",
               caps.pid_ns_supported ? "AVAILABLE" : "UNAVAILABLE",
               caps.pid_ns_child_tested ? "yes" : "no");
        printf("  Mount Namespace:           %s\n", caps.mount_ns_supported ? "AVAILABLE" : "UNAVAILABLE");
        printf("  cgroups v2:                %s (kernel: %s, delegated candidate: %s)\n",
               caps.cgroups_v2_supported ? "AVAILABLE" : "UNAVAILABLE",
               caps.cgroup_v2_kernel ? "yes" : "no",
               caps.cgroup_v2_delegated ? "yes" : "no");
        printf("  cgroup Controllers (Candidate Delegation):\n");
        printf("    CPU:                     %s (candidate writable: %s)\n",
               caps.cgroup_cpu_controller ? "AVAILABLE" : "UNAVAILABLE",
               caps.cgroup_cpu_writable ? "yes" : "no");
        printf("    Memory:                  %s (candidate writable: %s)\n",
               caps.cgroup_memory_controller ? "AVAILABLE" : "UNAVAILABLE",
               caps.cgroup_memory_writable ? "yes" : "no");
        printf("    PIDs:                    %s (candidate writable: %s)\n",
               caps.cgroup_pids_controller ? "AVAILABLE" : "UNAVAILABLE",
               caps.cgroup_pids_writable ? "yes" : "no");
        printf("  Summary:                   %s\n", caps.reason);
        printf("  Note: Static probe inspects candidate permissions; dynamic enforcement is verified at container launch.\n");
    }
    return 0;
}

static int parse_strict_positive_int(const char *str, int min_val, int max_val, int *out) {
    if (!str || !*str || !out) return -1;
    if (*str < '0' || *str > '9') return -1;
    char *endptr = NULL;
    errno = 0;
    long val = strtol(str, &endptr, 10);
    if (errno != 0 || endptr == str || *endptr != '\0') return -1;
    if (val < min_val || val > max_val) return -1;
    *out = (int)val;
    return 0;
}

/*
 * Extract the single positional argument expected by the read-only commands.
 * Options listed in `value_opts` consume the following argument, which must therefore
 * not be mistaken for the positional. Returns 0 and sets *out on success, 1 when no
 * positional was supplied, and -1 when more than one was supplied.
 */
static int collect_positional_arg(int argc, char *argv[],
                                  const char *const *value_opts,
                                  const char **out) {
    const char *found = NULL;
    for (int i = 2; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (value_opts) {
                bool takes_value = false;
                for (const char *const *o = value_opts; *o; o++) {
                    if (strcmp(argv[i], *o) == 0) { takes_value = true; break; }
                }
                if (takes_value) i++;
            }
            continue;
        }
        if (found) {
            fprintf(stderr, "Error: Unexpected extra argument '%s' (expected a single workload id)\n", argv[i]);
            return -1;
        }
        found = argv[i];
    }
    if (!found) return 1;
    *out = found;
    return 0;
}

static void print_json_string_field(const char *prefix, const char *key, const char *val, bool trailing_comma) {
    printf("%s\"%s\": \"", prefix ? prefix : "", key);
    if (val) {
        for (const char *p = val; *p; p++) {
            switch (*p) {
                case '\"': fputs("\\\"", stdout); break;
                case '\\': fputs("\\\\", stdout); break;
                case '\b': fputs("\\b", stdout); break;
                case '\f': fputs("\\f", stdout); break;
                case '\n': fputs("\\n", stdout); break;
                case '\r': fputs("\\r", stdout); break;
                case '\t': fputs("\\t", stdout); break;
                default:
                    if ((unsigned char)*p < 0x20) {
                        printf("\\u%04x", (unsigned char)*p);
                    } else {
                        putchar(*p);
                    }
                    break;
            }
        }
    }
    printf("\"%s\n", trailing_comma ? "," : "");
}

static int cmd_run(int argc, char *argv[]) {
    bool json_output = false;
    const char *work_dir = NULL;
    const char *req_id = NULL;
    const char *name = NULL;
    minibox_isolation_config_t iso = {0};
    minibox_resource_limits_t limits = {0};
    int exec_index = 2;

    while (exec_index < argc) {
        if (strcmp(argv[exec_index], "--json") == 0) {
            json_output = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--workdir") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --workdir requires an argument\n");
                return 1;
            }
            work_dir = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--id") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --id requires an argument\n");
                return 1;
            }
            req_id = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--name") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --name requires an argument\n");
                return 1;
            }
            name = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--cpu") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --cpu requires a percentage argument\n");
                return 1;
            }
            int cpu_pct = 0;
            if (parse_strict_positive_int(argv[exec_index + 1], 1, 10000, &cpu_pct) != 0) {
                fprintf(stderr, "Error: Invalid CPU percentage '%s' (must be an integer between 1 and 10000)\n", argv[exec_index + 1]);
                return 1;
            }
            limits.cpu_percent = cpu_pct;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--memory") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --memory requires a size argument\n");
                return 1;
            }
            uint64_t mem_bytes = 0;
            if (minibox_parse_memory_size(argv[exec_index + 1], &mem_bytes) != 0 || mem_bytes == 0) {
                fprintf(stderr, "Error: Invalid memory size '%s'\n", argv[exec_index + 1]);
                return 1;
            }
            limits.memory_max_bytes = mem_bytes;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--pids") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --pids requires a max pids argument\n");
                return 1;
            }
            int pids_max = 0;
            if (parse_strict_positive_int(argv[exec_index + 1], 1, 4194304, &pids_max) != 0) {
                fprintf(stderr, "Error: Invalid max pids '%s' (must be an integer between 1 and 4194304)\n", argv[exec_index + 1]);
                return 1;
            }
            limits.pids_max = pids_max;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--isolate") == 0) {
            iso.enable_uts = true;
            iso.enable_pid = true;
            iso.enable_mount = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--uts") == 0) {
            iso.enable_uts = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--hostname") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --hostname requires an argument\n");
                return 1;
            }
            iso.enable_uts = true;
            snprintf(iso.hostname, sizeof(iso.hostname), "%s", argv[exec_index + 1]);
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--pid-ns") == 0) {
            iso.enable_pid = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--mount-ns") == 0) {
            iso.enable_mount = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--") == 0) {
            exec_index++;
            break;
        } else if (argv[exec_index][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[exec_index]);
            print_usage(argv[0]);
            return 1;
        } else {
            break;
        }
    }

    if (exec_index >= argc) {
        fprintf(stderr, "Error: No executable specified to run\n");
        return 1;
    }

    minibox_workload_t wl;
    if (minibox_workload_init(&wl, req_id, name, argv[exec_index], &argv[exec_index], work_dir) != 0) {
        int init_errno = errno;
        if (init_errno == EEXIST) {
            fprintf(stderr, "Error: Workload id '%s' already exists\n", req_id);
        } else if (init_errno == EINVAL) {
            fprintf(stderr, "Error: Invalid workload id '%s'\n", req_id);
        } else {
            fprintf(stderr, "Error: Failed to initialize workload record: %s\n",
                    strerror(init_errno != 0 ? init_errno : EINVAL));
        }
        return 1;
    }
    wl.limits = limits;

    if (!json_output) {
        printf("[minibox] Workload '%s' (%s) starting: %s\n", wl.name, wl.id, wl.executable);
        fflush(stdout);
    }

    int rc = minibox_run_workload_foreground(&wl, &argv[exec_index], json_output, &iso);
    (void)rc;

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": %s,\n", (wl.state == MINIBOX_STATE_COMPLETED || wl.state == MINIBOX_STATE_TERMINATED) ? "true" : "false");
        print_json_string_field("  ", "workload_id", wl.id, true);
        print_json_string_field("  ", "name", wl.name, true);
        printf("  \"pid\": %d,\n", wl.pid);
        printf("  \"state\": \"%s\",\n", minibox_state_to_string(wl.state));
        printf("  \"status\": \"%s\",\n", minibox_state_to_string(wl.state));
        printf("  \"exit_code\": %d,\n", wl.exit_code);
        printf("  \"term_signal\": %d,\n", wl.term_signal);
        printf("  \"errno\": %d,\n", wl.exec_errno);
        printf("  \"exec_errno\": %d,\n", wl.exec_errno);
        printf("  \"iso_uts\": %s,\n", wl.iso_uts ? "true" : "false");
        print_json_string_field("  ", "iso_hostname", wl.iso_hostname, true);
        printf("  \"iso_pid\": %s,\n", wl.iso_pid ? "true" : "false");
        printf("  \"iso_mount\": %s,\n", wl.iso_mount ? "true" : "false");
        printf("  \"limits\": {\n");
        printf("    \"cpu_percent\": %d,\n", wl.limits.cpu_percent);
        printf("    \"memory_max_bytes\": %lu,\n", (unsigned long)wl.limits.memory_max_bytes);
        printf("    \"pids_max\": %d\n", wl.limits.pids_max);
        printf("  },\n");
        print_json_string_field("  ", "cgroup_path", wl.cgroup_path, true);
        printf("  \"cgroup_active\": %s,\n", wl.cgroup_active ? "true" : "false");
        printf("  \"oom_killed\": %s,\n", wl.oom_killed ? "true" : "false");
        if (wl.error_msg[0]) {
            print_json_string_field("  ", "error", wl.error_msg, false);
        } else {
            printf("  \"error\": null\n");
        }
        printf("}\n");
    } else {
        if (wl.state == MINIBOX_STATE_FAILED) {
            fprintf(stderr, "[minibox] Execution failed: %s (errno: %d)\n",
                    wl.error_msg[0] ? wl.error_msg : strerror(wl.exec_errno), wl.exec_errno);
        } else if (wl.state == MINIBOX_STATE_COMPLETED) {
            printf("[minibox] Workload '%s' finished with exit code: %d\n", wl.id, wl.exit_code);
        } else if (wl.state == MINIBOX_STATE_TERMINATED) {
            printf("[minibox] Workload '%s' Terminated by signal: %d\n", wl.id, wl.term_signal);
        }
    }

    if (wl.state == MINIBOX_STATE_FAILED) {
        return 127;
    }
    if (wl.state == MINIBOX_STATE_TERMINATED) {
        return 128 + wl.term_signal;
    }
    return wl.exit_code;
}

static int cmd_start(int argc, char *argv[]) {
    bool json_output = false;
    const char *work_dir = NULL;
    const char *req_id = NULL;
    const char *name = NULL;
    minibox_isolation_config_t iso = {0};
    minibox_resource_limits_t limits = {0};
    int exec_index = 2;

    while (exec_index < argc) {
        if (strcmp(argv[exec_index], "--json") == 0) {
            json_output = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--workdir") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --workdir requires an argument\n");
                return 1;
            }
            work_dir = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--id") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --id requires an argument\n");
                return 1;
            }
            req_id = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--name") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --name requires an argument\n");
                return 1;
            }
            name = argv[exec_index + 1];
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--cpu") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --cpu requires a percentage argument\n");
                return 1;
            }
            int cpu_pct = 0;
            if (parse_strict_positive_int(argv[exec_index + 1], 1, 10000, &cpu_pct) != 0) {
                fprintf(stderr, "Error: Invalid CPU percentage '%s' (must be an integer between 1 and 10000)\n", argv[exec_index + 1]);
                return 1;
            }
            limits.cpu_percent = cpu_pct;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--memory") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --memory requires a size argument\n");
                return 1;
            }
            uint64_t mem_bytes = 0;
            if (minibox_parse_memory_size(argv[exec_index + 1], &mem_bytes) != 0 || mem_bytes == 0) {
                fprintf(stderr, "Error: Invalid memory size '%s'\n", argv[exec_index + 1]);
                return 1;
            }
            limits.memory_max_bytes = mem_bytes;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--pids") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --pids requires a max pids argument\n");
                return 1;
            }
            int pids_max = 0;
            if (parse_strict_positive_int(argv[exec_index + 1], 1, 4194304, &pids_max) != 0) {
                fprintf(stderr, "Error: Invalid max pids '%s' (must be an integer between 1 and 4194304)\n", argv[exec_index + 1]);
                return 1;
            }
            limits.pids_max = pids_max;
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--isolate") == 0) {
            iso.enable_uts = true;
            iso.enable_pid = true;
            iso.enable_mount = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--uts") == 0) {
            iso.enable_uts = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--hostname") == 0) {
            if (exec_index + 1 >= argc) {
                fprintf(stderr, "Error: --hostname requires an argument\n");
                return 1;
            }
            iso.enable_uts = true;
            snprintf(iso.hostname, sizeof(iso.hostname), "%s", argv[exec_index + 1]);
            exec_index += 2;
        } else if (strcmp(argv[exec_index], "--pid-ns") == 0) {
            iso.enable_pid = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--mount-ns") == 0) {
            iso.enable_mount = true;
            exec_index++;
        } else if (strcmp(argv[exec_index], "--") == 0) {
            exec_index++;
            break;
        } else if (argv[exec_index][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[exec_index]);
            print_usage(argv[0]);
            return 1;
        } else {
            break;
        }
    }

    if (exec_index >= argc) {
        fprintf(stderr, "Error: No executable specified to start\n");
        return 1;
    }

    minibox_workload_t wl;
    if (minibox_workload_init(&wl, req_id, name, argv[exec_index], &argv[exec_index], work_dir) != 0) {
        int init_errno = errno;
        if (init_errno == EEXIST) {
            fprintf(stderr, "Error: Workload id '%s' already exists\n", req_id);
        } else if (init_errno == EINVAL) {
            fprintf(stderr, "Error: Invalid workload id '%s'\n", req_id);
        } else {
            fprintf(stderr, "Error: Failed to initialize workload record: %s\n",
                    strerror(init_errno != 0 ? init_errno : EINVAL));
        }
        return 1;
    }
    wl.limits = limits;

    int rc = minibox_start_workload_background(&wl, &argv[exec_index], &iso);

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": %s,\n", (rc == 0 && wl.state == MINIBOX_STATE_RUNNING) ? "true" : "false");
        print_json_string_field("  ", "workload_id", wl.id, true);
        print_json_string_field("  ", "name", wl.name, true);
        printf("  \"pid\": %d,\n", wl.pid);
        printf("  \"supervisor_pid\": %d,\n", wl.supervisor_pid);
        printf("  \"state\": \"%s\",\n", minibox_state_to_string(wl.state));
        printf("  \"iso_uts\": %s,\n", wl.iso_uts ? "true" : "false");
        print_json_string_field("  ", "iso_hostname", wl.iso_hostname, true);
        printf("  \"iso_pid\": %s,\n", wl.iso_pid ? "true" : "false");
        printf("  \"iso_mount\": %s,\n", wl.iso_mount ? "true" : "false");
        printf("  \"limits\": {\n");
        printf("    \"cpu_percent\": %d,\n", wl.limits.cpu_percent);
        printf("    \"memory_max_bytes\": %lu,\n", (unsigned long)wl.limits.memory_max_bytes);
        printf("    \"pids_max\": %d\n", wl.limits.pids_max);
        printf("  },\n");
        print_json_string_field("  ", "cgroup_path", wl.cgroup_path, true);
        printf("  \"cgroup_active\": %s,\n", wl.cgroup_active ? "true" : "false");
        print_json_string_field("  ", "log_file", wl.log_file, true);
        if (wl.state == MINIBOX_STATE_FAILED) {
            print_json_string_field("  ", "error", wl.error_msg[0] ? wl.error_msg : strerror(wl.exec_errno), true);
            printf("  \"errno\": %d\n", wl.exec_errno);
        } else {
            printf("  \"error\": null\n");
        }
        printf("}\n");
    } else {
        if (rc == 0 && wl.state == MINIBOX_STATE_RUNNING) {
            printf("[minibox] Workload '%s' (%s) started in background.\n", wl.name, wl.id);
            printf("  PID:        %d\n", wl.pid);
            printf("  State:      %s\n", minibox_state_to_string(wl.state));
            if (wl.iso_uts) {
                printf("  Hostname:   %s\n", wl.iso_hostname);
            }
            if (wl.iso_pid) {
                printf("  PID ns:     ENABLED\n");
            }
            if (wl.cgroup_active) {
                printf("  cgroup:     %s\n", wl.cgroup_path);
            }
            printf("  Logs:       %s\n", wl.log_file);
        } else {
            fprintf(stderr, "[minibox] Failed to start workload '%s': %s (errno: %d)\n",
                    wl.id, wl.error_msg[0] ? wl.error_msg : strerror(wl.exec_errno), wl.exec_errno);
        }
    }

    return (rc == 0) ? 0 : (wl.exec_errno == ENOENT ? 127 : 1);
}

static int cmd_list(int argc, char *argv[]) {
    bool json_output = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        }
    }

    minibox_workload_t *workloads = NULL;
    size_t count = 0;
    if (minibox_workload_list(&workloads, &count) != 0) {
        fprintf(stderr, "Error: Failed to list workloads\n");
        return 1;
    }

    if (json_output) {
        printf("[\n");
        for (size_t i = 0; i < count; i++) {
            printf("  {\n");
            print_json_string_field("    ", "id", workloads[i].id, true);
            print_json_string_field("    ", "name", workloads[i].name, true);
            printf("    \"state\": \"%s\",\n", minibox_state_to_string(workloads[i].state));
            printf("    \"pid\": %d,\n", workloads[i].pid);
            printf("    \"supervisor_pid\": %d,\n", workloads[i].supervisor_pid);
            print_json_string_field("    ", "executable", workloads[i].executable, true);
            printf("    \"exit_code\": %d,\n", workloads[i].exit_code);
            printf("    \"term_signal\": %d,\n", workloads[i].term_signal);
            printf("    \"iso_uts\": %s,\n", workloads[i].iso_uts ? "true" : "false");
            printf("    \"iso_pid\": %s,\n", workloads[i].iso_pid ? "true" : "false");
            printf("    \"started_at_ms\": %lu,\n", (unsigned long)workloads[i].started_at_ms);
            printf("    \"finished_at_ms\": %lu\n", (unsigned long)workloads[i].finished_at_ms);
            printf("  }%s\n", (i + 1 < count) ? "," : "");
        }
        printf("]\n");
    } else {
        if (count == 0) {
            printf("No workloads found.\n");
        } else {
            printf("%-18s %-16s %-8s %-12s %-6s %s\n", "WORKLOAD ID", "NAME", "PID", "STATE", "EXIT", "COMMAND");
            printf("--------------------------------------------------------------------------------\n");
            for (size_t i = 0; i < count; i++) {
                char exit_str[16];
                if (workloads[i].state == MINIBOX_STATE_COMPLETED) {
                    snprintf(exit_str, sizeof(exit_str), "%d", workloads[i].exit_code);
                } else if (workloads[i].state == MINIBOX_STATE_TERMINATED) {
                    snprintf(exit_str, sizeof(exit_str), "sig:%d", workloads[i].term_signal);
                } else {
                    snprintf(exit_str, sizeof(exit_str), "-");
                }
                printf("%-18s %-16s %-8d %-12s %-6s %s\n",
                       workloads[i].id,
                       workloads[i].name,
                       workloads[i].pid,
                       minibox_state_to_string(workloads[i].state),
                       exit_str,
                       workloads[i].executable);
            }
        }
    }

    minibox_workload_free_list(workloads);
    return 0;
}

static int cmd_inspect(int argc, char *argv[]) {
    bool json_output = false;
    const char *id = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        }
    }

    int pos = collect_positional_arg(argc, argv, NULL, &id);
    if (pos < 0) return 1;
    if (pos > 0 || !id) {
        fprintf(stderr, "Error: Workload ID required for inspect\n");
        return 1;
    }

    minibox_workload_t wl;
    if (minibox_workload_load(id, &wl) != 0) {
        if (json_output) {
            printf("{\"ok\": false, \"error\": \"Workload not found\"}\n");
        } else {
            fprintf(stderr, "Error: Workload '%s' not found\n", id);
        }
        return 1;
    }
    minibox_workload_refresh_state(&wl);

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": true,\n");
        print_json_string_field("  ", "id", wl.id, true);
        print_json_string_field("  ", "name", wl.name, true);
        printf("  \"state\": \"%s\",\n", minibox_state_to_string(wl.state));
        printf("  \"pid\": %d,\n", wl.pid);
        printf("  \"supervisor_pid\": %d,\n", wl.supervisor_pid);
        print_json_string_field("  ", "executable", wl.executable, true);
        print_json_string_field("  ", "args", wl.args, true);
        print_json_string_field("  ", "work_dir", wl.work_dir, true);
        printf("  \"created_at_ms\": %lu,\n", (unsigned long)wl.created_at_ms);
        printf("  \"started_at_ms\": %lu,\n", (unsigned long)wl.started_at_ms);
        printf("  \"finished_at_ms\": %lu,\n", (unsigned long)wl.finished_at_ms);
        printf("  \"exit_code\": %d,\n", wl.exit_code);
        printf("  \"term_signal\": %d,\n", wl.term_signal);
        printf("  \"exec_errno\": %d,\n", wl.exec_errno);
        print_json_string_field("  ", "error_msg", wl.error_msg, true);
        print_json_string_field("  ", "log_file", wl.log_file, true);
        printf("  \"iso_uts\": %s,\n", wl.iso_uts ? "true" : "false");
        print_json_string_field("  ", "iso_hostname", wl.iso_hostname, true);
        printf("  \"iso_pid\": %s,\n", wl.iso_pid ? "true" : "false");
        printf("  \"iso_mount\": %s,\n", wl.iso_mount ? "true" : "false");
        printf("  \"limits\": {\n");
        printf("    \"cpu_percent\": %d,\n", wl.limits.cpu_percent);
        printf("    \"memory_max_bytes\": %lu,\n", (unsigned long)wl.limits.memory_max_bytes);
        printf("    \"pids_max\": %d\n", wl.limits.pids_max);
        printf("  },\n");
        print_json_string_field("  ", "cgroup_path", wl.cgroup_path, true);
        printf("  \"cgroup_active\": %s,\n", wl.cgroup_active ? "true" : "false");
        printf("  \"oom_killed\": %s\n", wl.oom_killed ? "true" : "false");
        printf("}\n");
    } else {
        printf("Workload Inspection: %s\n", wl.id);
        printf("  Name:           %s\n", wl.name);
        printf("  State:          %s\n", minibox_state_to_string(wl.state));
        printf("  PID:            %d\n", wl.pid);
        printf("  Supervisor PID: %d\n", wl.supervisor_pid);
        printf("  Executable:     %s\n", wl.executable);
        printf("  Args:           %s\n", wl.args);
        printf("  Work Directory: %s\n", wl.work_dir);
        printf("  Log File:       %s\n", wl.log_file);
        printf("  Isolation UTS:  %s (hostname: %s)\n",
               wl.iso_uts ? "ENABLED" : "DISABLED",
               wl.iso_hostname[0] ? wl.iso_hostname : "default");
        printf("  Isolation PID:  %s\n", wl.iso_pid ? "ENABLED" : "DISABLED");
        printf("  Isolation MNT:  %s\n", wl.iso_mount ? "ENABLED" : "DISABLED");
        if (wl.limits.cpu_percent > 0) {
            printf("  Limit CPU:      %d%%\n", wl.limits.cpu_percent);
        } else {
            printf("  Limit CPU:      unlimited\n");
        }
        if (wl.limits.memory_max_bytes > 0) {
            printf("  Limit Memory:   %lu bytes\n", (unsigned long)wl.limits.memory_max_bytes);
        } else {
            printf("  Limit Memory:   unlimited\n");
        }
        if (wl.limits.pids_max > 0) {
            printf("  Limit PIDs:     %d\n", wl.limits.pids_max);
        } else {
            printf("  Limit PIDs:     unlimited\n");
        }
        if (wl.cgroup_path[0]) {
            printf("  cgroup Path:    %s\n", wl.cgroup_path);
        }
        if (wl.state == MINIBOX_STATE_COMPLETED) {
            printf("  Exit Code:      %d\n", wl.exit_code);
        } else if (wl.state == MINIBOX_STATE_TERMINATED) {
            printf("  Term Signal:    %d\n", wl.term_signal);
        } else if (wl.state == MINIBOX_STATE_FAILED) {
            printf("  Error:          %s\n", wl.error_msg);
        }
    }
    return 0;
}

static void print_workload_metrics_json(const minibox_workload_metrics_t *m, bool include_host) {
    printf("{\n");
    printf("  \"ok\": true,\n");
    print_json_string_field("  ", "workload_id", m->workload_id, true);
    print_json_string_field("  ", "name", m->name, true);
    printf("  \"pid\": %d,\n", m->pid);
    printf("  \"supervisor_pid\": %d,\n", m->supervisor_pid);
    printf("  \"state\": \"%s\",\n", minibox_state_to_string(m->state));
    printf("  \"active\": %s,\n", m->active ? "true" : "false");
    printf("  \"elapsed_ms\": %lu,\n", (unsigned long)m->elapsed_ms);
    printf("  \"exit_code\": %d,\n", m->exit_code);
    printf("  \"term_signal\": %d,\n", m->term_signal);
    printf("  \"cpu\": {\n");
    printf("    \"valid\": %s,\n", m->cpu.valid ? "true" : "false");
    printf("    \"usage_percent\": %.2f,\n", m->cpu.usage_percent);
    printf("    \"usage_usec\": %lu,\n", (unsigned long)m->cpu.usage_usec);
    printf("    \"user_usec\": %lu,\n", (unsigned long)m->cpu.user_usec);
    printf("    \"system_usec\": %lu,\n", (unsigned long)m->cpu.system_usec);
    printf("    \"nr_periods\": %lu,\n", (unsigned long)m->cpu.nr_periods);
    printf("    \"nr_throttled\": %lu,\n", (unsigned long)m->cpu.nr_throttled);
    printf("    \"throttled_usec\": %lu\n", (unsigned long)m->cpu.throttled_usec);
    printf("  },\n");
    printf("  \"memory\": {\n");
    printf("    \"valid\": %s,\n", m->memory.valid ? "true" : "false");
    printf("    \"current_bytes\": %lu,\n", (unsigned long)m->memory.current_bytes);
    printf("    \"peak_bytes\": %lu,\n", (unsigned long)m->memory.peak_bytes);
    printf("    \"limit_bytes\": %lu,\n", (unsigned long)m->memory.limit_bytes);
    printf("    \"rss_bytes\": %lu\n", (unsigned long)m->memory.rss_bytes);
    printf("  },\n");
    printf("  \"pids\": {\n");
    printf("    \"valid\": %s,\n", m->pids.valid ? "true" : "false");
    printf("    \"current\": %d,\n", m->pids.current_pids);
    printf("    \"limit\": %d\n", m->pids.max_pids);
    printf("  }");
    if (include_host) {
        printf(",\n  \"host\": {\n");
        printf("    \"valid\": %s,\n", m->host.valid ? "true" : "false");
        printf("    \"cpu_percent\": %.2f,\n", m->host.cpu_percent);
        printf("    \"mem_total_bytes\": %lu,\n", (unsigned long)m->host.mem_total_bytes);
        printf("    \"mem_avail_bytes\": %lu,\n", (unsigned long)m->host.mem_avail_bytes);
        printf("    \"mem_used_bytes\": %lu,\n", (unsigned long)m->host.mem_used_bytes);
        printf("    \"load_1m\": %.2f,\n", m->host.load_1m);
        printf("    \"load_5m\": %.2f,\n", m->host.load_5m);
        printf("    \"load_15m\": %.2f\n", m->host.load_15m);
        printf("  }\n");
    } else {
        printf("\n");
    }
    printf("}\n");
}

static void print_workload_metrics_human(const minibox_workload_metrics_t *m) {
    printf("Workload Metrics: %s (%s)\n", m->name, m->workload_id);
    printf("  State:          %s (%s)\n", minibox_state_to_string(m->state),
           m->active ? "ACTIVE" : "INACTIVE");
    printf("  PID:            %d (supervisor: %d)\n", m->pid, m->supervisor_pid);
    printf("  Elapsed Time:   %.2fs\n", (double)m->elapsed_ms / 1000.0);
    if (m->cpu.valid) {
        printf("  CPU Usage:      %.1f%%\n", m->cpu.usage_percent);
        printf("  CPU Time:       %.3fs (user: %.3fs, sys: %.3fs)\n",
               (double)m->cpu.usage_usec / 1e6,
               (double)m->cpu.user_usec / 1e6,
               (double)m->cpu.system_usec / 1e6);
        if (m->cpu.nr_periods > 0) {
            printf("  CFS Throttle:   %lu / %lu periods (%lu us throttled)\n",
                   (unsigned long)m->cpu.nr_throttled,
                   (unsigned long)m->cpu.nr_periods,
                   (unsigned long)m->cpu.throttled_usec);
        }
    } else {
        printf("  CPU Metrics:    unavailable (workload exited)\n");
    }
    if (m->memory.valid) {
        printf("  Memory Current: %.2f MiB (%lu bytes)\n",
               (double)m->memory.current_bytes / (1024.0 * 1024.0),
               (unsigned long)m->memory.current_bytes);
        printf("  Memory Peak:    %.2f MiB (%lu bytes)\n",
               (double)m->memory.peak_bytes / (1024.0 * 1024.0),
               (unsigned long)m->memory.peak_bytes);
        if (m->memory.limit_bytes > 0) {
            printf("  Memory Limit:   %.2f MiB (%lu bytes)\n",
                   (double)m->memory.limit_bytes / (1024.0 * 1024.0),
                   (unsigned long)m->memory.limit_bytes);
        } else {
            printf("  Memory Limit:   unlimited\n");
        }
    } else {
        printf("  Memory Metrics: unavailable (workload exited)\n");
    }
    if (m->pids.valid) {
        printf("  Active PIDs:    %d", m->pids.current_pids);
        if (m->pids.max_pids > 0) {
            printf(" (limit: %d)\n", m->pids.max_pids);
        } else {
            printf(" (limit: unlimited)\n");
        }
    }
    printf("\nHost Resource Overview:\n");
    printf("  Host CPU:       %.1f%%\n", m->host.cpu_percent);
    printf("  Host RAM Used:  %.2f / %.2f GiB (available: %.2f GiB)\n",
           (double)m->host.mem_used_bytes / (1024.0 * 1024.0 * 1024.0),
           (double)m->host.mem_total_bytes / (1024.0 * 1024.0 * 1024.0),
           (double)m->host.mem_avail_bytes / (1024.0 * 1024.0 * 1024.0));
    printf("  Load Averages:  %.2f, %.2f, %.2f\n",
           m->host.load_1m, m->host.load_5m, m->host.load_15m);
}

static int cmd_metrics(int argc, char *argv[]) {
    bool json_output = false;
    int sample_ms = 100;
    const char *id = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--sample-ms") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --sample-ms requires an argument (0-%d ms)\n", MINIBOX_MAX_SAMPLE_MS);
                return 1;
            }
            if (parse_strict_positive_int(argv[i + 1], 0, MINIBOX_MAX_SAMPLE_MS, &sample_ms) != 0) {
                fprintf(stderr, "Error: Invalid sample-ms '%s' (must be an integer between 0 and %d)\n",
                        argv[i + 1], MINIBOX_MAX_SAMPLE_MS);
                return 1;
            }
            i++;
        }
    }

    static const char *const metrics_value_opts[] = { "--sample-ms", NULL };
    int pos = collect_positional_arg(argc, argv, metrics_value_opts, &id);
    if (pos < 0) return 1;

    if (id) {
        minibox_workload_metrics_t m;
        if (minibox_collect_workload_metrics(id, sample_ms, &m) != 0) {
            if (json_output) {
                printf("{\"ok\": false, \"error\": \"Workload not found\"}\n");
            } else {
                fprintf(stderr, "Error: Workload '%s' not found\n", id);
            }
            return 1;
        }
        if (json_output) {
            print_workload_metrics_json(&m, true);
        } else {
            print_workload_metrics_human(&m);
        }
        return 0;
    }

    minibox_host_metrics_t host;
    minibox_collect_host_metrics(&host);

    minibox_workload_t *workloads = NULL;
    size_t count = 0;
    if (minibox_workload_list(&workloads, &count) != 0) {
        if (json_output) {
            printf("{\"ok\": false, \"error\": \"Failed to list workloads\"}\n");
        } else {
            fprintf(stderr, "Error: Failed to list workloads\n");
        }
        return 1;
    }

    /*
     * Every per-workload snapshot reuses the single host measurement taken above.
     * Re-measuring host CPU here used to cost a fixed sleep per workload.
     */
    minibox_workload_metrics_t *wms = NULL;
    if (count > 0) {
        wms = calloc(count, sizeof(*wms));
        if (!wms) {
            minibox_workload_free_list(workloads);
            fprintf(stderr, "Error: Out of memory collecting metrics\n");
            return 1;
        }
        for (size_t i = 0; i < count; i++) {
            if (minibox_collect_workload_metrics_with_host(workloads[i].id, 0, &host, &wms[i]) != 0) {
                /* Fall back to a summary derived from the listed record. */
                memset(&wms[i], 0, sizeof(wms[i]));
                snprintf(wms[i].workload_id, sizeof(wms[i].workload_id), "%s", workloads[i].id);
                snprintf(wms[i].name, sizeof(wms[i].name), "%s", workloads[i].name);
                wms[i].pid = workloads[i].pid;
                wms[i].supervisor_pid = workloads[i].supervisor_pid;
                wms[i].state = workloads[i].state;
                wms[i].exit_code = workloads[i].exit_code;
                wms[i].term_signal = workloads[i].term_signal;
                wms[i].host = host;
            }
        }
    }

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": true,\n");
        printf("  \"host\": {\n");
        printf("    \"valid\": %s,\n", host.valid ? "true" : "false");
        printf("    \"cpu_percent\": %.2f,\n", host.cpu_percent);
        printf("    \"mem_total_bytes\": %lu,\n", (unsigned long)host.mem_total_bytes);
        printf("    \"mem_avail_bytes\": %lu,\n", (unsigned long)host.mem_avail_bytes);
        printf("    \"mem_used_bytes\": %lu,\n", (unsigned long)host.mem_used_bytes);
        printf("    \"load_1m\": %.2f,\n", host.load_1m);
        printf("    \"load_5m\": %.2f,\n", host.load_5m);
        printf("    \"load_15m\": %.2f\n", host.load_15m);
        printf("  },\n");
        printf("  \"workloads\": [\n");
        for (size_t i = 0; i < count; i++) {
            const minibox_workload_metrics_t *wm = &wms[i];
            printf("    {\n");
            print_json_string_field("      ", "id", wm->workload_id, true);
            print_json_string_field("      ", "name", wm->name, true);
            printf("      \"state\": \"%s\",\n", minibox_state_to_string(wm->state));
            printf("      \"active\": %s,\n", wm->active ? "true" : "false");
            printf("      \"pid\": %d,\n", wm->pid);
            printf("      \"cpu_percent\": %.2f,\n", wm->cpu.usage_percent);
            printf("      \"memory_bytes\": %lu,\n", (unsigned long)wm->memory.current_bytes);
            printf("      \"pids_count\": %d\n", wm->pids.current_pids);
            printf("    }%s\n", (i + 1 < count) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
    } else {
        printf("Host Resource Overview:\n");
        printf("  CPU Usage:      %.1f%%\n", host.cpu_percent);
        printf("  Memory Used:    %.2f / %.2f GiB (available: %.2f GiB)\n",
               (double)host.mem_used_bytes / (1024.0 * 1024.0 * 1024.0),
               (double)host.mem_total_bytes / (1024.0 * 1024.0 * 1024.0),
               (double)host.mem_avail_bytes / (1024.0 * 1024.0 * 1024.0));
        printf("  Load Averages:  %.2f, %.2f, %.2f\n\n",
               host.load_1m, host.load_5m, host.load_15m);

        if (count == 0) {
            printf("No active workloads.\n");
        } else {
            printf("%-18s %-16s %-8s %-12s %-10s %-12s %-6s\n",
                   "WORKLOAD ID", "NAME", "PID", "STATE", "CPU %", "MEMORY", "PIDS");
            printf("--------------------------------------------------------------------------------\n");
            for (size_t i = 0; i < count; i++) {
                const minibox_workload_metrics_t *wm = &wms[i];
                char cpu_str[16];
                char mem_str[16];
                char pids_str[16];
                if (wm->active && wm->cpu.valid) {
                    snprintf(cpu_str, sizeof(cpu_str), "%.1f%%", wm->cpu.usage_percent);
                } else {
                    snprintf(cpu_str, sizeof(cpu_str), "-");
                }
                if (wm->active && wm->memory.valid) {
                    snprintf(mem_str, sizeof(mem_str), "%.1f MB", (double)wm->memory.current_bytes / (1024.0 * 1024.0));
                } else {
                    snprintf(mem_str, sizeof(mem_str), "-");
                }
                if (wm->active && wm->pids.valid) {
                    snprintf(pids_str, sizeof(pids_str), "%d", wm->pids.current_pids);
                } else {
                    snprintf(pids_str, sizeof(pids_str), "-");
                }
                printf("%-18s %-16s %-8d %-12s %-10s %-12s %-6s\n",
                       wm->workload_id, wm->name, wm->pid,
                       minibox_state_to_string(wm->state),
                       cpu_str, mem_str, pids_str);
            }
        }
    }

    free(wms);
    minibox_workload_free_list(workloads);
    return 0;
}

static int cmd_stop(int argc, char *argv[]) {
    bool json_output = false;
    bool force = false;
    int sig = 15; /* SIGTERM */
    const char *id = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force = true;
        } else if (strcmp(argv[i], "--signal") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --signal requires a signal number\n");
                return 1;
            }
            if (parse_strict_positive_int(argv[i + 1], 1, 64, &sig) != 0) {
                fprintf(stderr, "Error: Invalid signal number '%s' (must be an integer between 1 and 64)\n",
                        argv[i + 1]);
                return 1;
            }
            i++;
        }
    }

    static const char *const stop_value_opts[] = { "--signal", NULL };
    int pos = collect_positional_arg(argc, argv, stop_value_opts, &id);
    if (pos < 0) return 1;
    if (pos > 0 || !id) {
        fprintf(stderr, "Error: Workload ID required for stop\n");
        return 1;
    }

    errno = 0;
    int rc = minibox_stop_workload(id, sig, force);
    int stop_errno = errno;

    minibox_workload_t wl;
    if (minibox_workload_load(id, &wl) == 0) {
        minibox_workload_refresh_state(&wl);
    } else {
        memset(&wl, 0, sizeof(wl));
    }

    if (json_output) {
        printf("{\n");
        printf("  \"ok\": %s,\n", (rc == 0) ? "true" : "false");
        printf("  \"workload_id\": \"%s\",\n", id);
        printf("  \"state\": \"%s\"\n", minibox_state_to_string(wl.state));
        printf("}\n");
    } else {
        if (rc == 0) {
            printf("[minibox] Workload '%s' stopped (state: %s).\n", id, minibox_state_to_string(wl.state));
        } else {
            fprintf(stderr, "[minibox] Failed to stop workload '%s': %s\n", id,
                    strerror(stop_errno != 0 ? stop_errno : EINVAL));
        }
    }
    return (rc == 0) ? 0 : 1;
}

static int cmd_logs(int argc, char *argv[]) {
    const char *id = NULL;
    int pos = collect_positional_arg(argc, argv, NULL, &id);
    if (pos < 0) return 1;
    if (pos > 0 || !id) {
        fprintf(stderr, "Error: Workload ID required for logs\n");
        return 1;
    }

    minibox_workload_t wl;
    if (minibox_workload_load(id, &wl) != 0) {
        fprintf(stderr, "Error: Workload '%s' not found\n", id);
        return 1;
    }

    FILE *f = fopen(wl.log_file, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open log file '%s': %s\n", wl.log_file, strerror(errno));
        return 1;
    }

    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        fwrite(buf, 1, n, stdout);
    }
    fclose(f);
    return 0;
}

static int cmd_delete(int argc, char *argv[]) {
    bool json_output = false;
    bool force = false;
    const char *id = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force = true;
        }
    }

    int pos = collect_positional_arg(argc, argv, NULL, &id);
    if (pos < 0) return 1;
    if (pos > 0 || !id) {
        fprintf(stderr, "Error: Workload ID required for delete\n");
        return 1;
    }

    errno = 0;
    int rc = minibox_workload_delete(id, force);
    int delete_errno = errno;

    if (json_output) {
        printf("{\"ok\": %s, \"workload_id\": \"%s\"}\n", rc == 0 ? "true" : "false", id);
    } else {
        if (rc == 0) {
            printf("[minibox] Workload '%s' deleted.\n", id);
        } else {
            fprintf(stderr, "[minibox] Failed to delete workload '%s': %s\n", id,
                    strerror(delete_errno != 0 ? delete_errno : EINVAL));
        }
    }
    return (rc == 0) ? 0 : 1;
}

static int cmd_cleanup(int argc, char *argv[]) {
    bool json_output = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--json") == 0) {
            json_output = true;
        }
    }

    size_t removed = 0;
    errno = 0;
    int rc = minibox_workload_cleanup_completed(&removed);
    int cleanup_errno = errno;

    if (json_output) {
        printf("{\"ok\": %s, \"removed_count\": %zu}\n", rc == 0 ? "true" : "false", removed);
    } else {
        if (rc == 0) {
            printf("[minibox] Cleaned up %zu completed/terminated workload(s).\n", removed);
        } else {
            fprintf(stderr, "[minibox] Cleanup failed: %s\n",
                    strerror(cleanup_errno != 0 ? cleanup_errno : EINVAL));
        }
    }
    return (rc == 0) ? 0 : 1;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "version") == 0 || strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0) {
        print_version();
        return 0;
    }

    if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "capabilities") == 0) {
        return cmd_capabilities(argc, argv);
    }
    if (strcmp(argv[1], "run") == 0) {
        return cmd_run(argc, argv);
    }
    if (strcmp(argv[1], "start") == 0) {
        return cmd_start(argc, argv);
    }
    if (strcmp(argv[1], "list") == 0) {
        return cmd_list(argc, argv);
    }
    if (strcmp(argv[1], "inspect") == 0) {
        return cmd_inspect(argc, argv);
    }
    if (strcmp(argv[1], "metrics") == 0 || strcmp(argv[1], "stats") == 0) {
        return cmd_metrics(argc, argv);
    }
    if (strcmp(argv[1], "stop") == 0) {
        return cmd_stop(argc, argv);
    }
    if (strcmp(argv[1], "logs") == 0) {
        return cmd_logs(argc, argv);
    }
    if (strcmp(argv[1], "delete") == 0) {
        return cmd_delete(argc, argv);
    }
    if (strcmp(argv[1], "cleanup") == 0) {
        return cmd_cleanup(argc, argv);
    }

    fprintf(stderr, "Error: Unknown command '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 1;
}
