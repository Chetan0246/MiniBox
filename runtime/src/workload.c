#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "workload.h"
#include "lifecycle.h"
#include "resources.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static char g_state_dir[512] = {0};

/*
 * Upper bound accepted when reading a state record. The serialized record can be
 * several kilobytes (args alone are 2 KB and there are three 1 KB path fields), so
 * this must stay comfortably above the largest producible document.
 */
#define MINIBOX_STATE_FILE_MAX (1024 * 1024)

/*
 * Read an entire file into a freshly allocated NUL-terminated buffer.
 * Returns the number of bytes read, or -1 with errno set on failure.
 * The buffer size grows to fit the file, so callers never observe a truncated record.
 */
static ssize_t read_whole_file(const char *path, char **out_buf) {
    *out_buf = NULL;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    struct stat st;
    size_t cap = 4096;
    if (fstat(fd, &st) == 0 && st.st_size > 0) {
        if ((uint64_t)st.st_size > MINIBOX_STATE_FILE_MAX) {
            close(fd);
            errno = EFBIG;
            return -1;
        }
        cap = (size_t)st.st_size + 1;
    }

    char *buf = malloc(cap);
    if (!buf) {
        close(fd);
        errno = ENOMEM;
        return -1;
    }

    size_t total = 0;
    while (total + 1 < cap) {
        ssize_t n = read(fd, buf + total, cap - total - 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            int saved = errno;
            free(buf);
            close(fd);
            errno = saved;
            return -1;
        }
        if (n == 0) break;
        total += (size_t)n;
    }
    buf[total] = 0;
    close(fd);

    *out_buf = buf;
    return (ssize_t)total;
}

uint64_t minibox_current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}

static int mkdir_p(const char *path, mode_t mode) {
    char tmp[MINIBOX_MAX_PATH_LEN];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len == 0) return -1;
    if (tmp[len - 1] == '/') tmp[len - 1] = 0;

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

const char *minibox_get_state_dir(void) {
    if (g_state_dir[0] != 0) {
        return g_state_dir;
    }

    const char *env_dir = getenv("MINIBOX_STATE_DIR");
    if (env_dir && strlen(env_dir) > 0) {
        snprintf(g_state_dir, sizeof(g_state_dir), "%s", env_dir);
    } else {
        snprintf(g_state_dir, sizeof(g_state_dir), "/tmp/minibox-%u/workloads", (unsigned int)getuid());
    }

    mkdir_p(g_state_dir, 0700);
    return g_state_dir;
}

int minibox_get_state_dir_buf(char *out, size_t out_size) {
    if (!out || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    const char *dir = minibox_get_state_dir();
    size_t len = strlen(dir);
    if (len + 1 > out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, dir, len + 1);
    return 0;
}

static bool is_safe_workload_id(const char *id) {
    if (!id || *id == 0 || strlen(id) > 64) return false;
    if (strcmp(id, ".") == 0 || strcmp(id, "..") == 0) return false;
    if (strstr(id, "..") != NULL || strchr(id, '/') != NULL || strchr(id, '\\') != NULL) return false;
    for (const char *p = id; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '-' && *p != '_' && *p != '.') {
            return false;
        }
    }
    return true;
}

/*
 * Index of the "starttime" field within the /proc/<pid>/stat fields that follow the
 * closing parenthesis of comm. Field 1 is pid and field 2 is comm, so starttime
 * (field 22) sits at index 22 - 3 = 19. Index 0 holds the single-character state.
 */
#define PROC_STAT_STATE_INDEX   0
#define PROC_STAT_STARTTIME_IDX 19

/*
 * Read the state character and start time of a PID from /proc/<pid>/stat.
 * Returns false if the process does not exist or the record is malformed.
 */
static bool read_proc_pid_identity(pid_t pid, char *out_state, uint64_t *out_starttime) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);

    FILE *f = fopen(path, "r");
    if (!f) return false;

    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;

    /* comm is wrapped in parentheses and may itself contain spaces or parens. */
    char *after_comm = strrchr(buf, ')');
    if (!after_comm) return false;
    after_comm++;

    int index = 0;
    const char *p = after_comm;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0 || *p == '\n') break;

        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        size_t len = (size_t)(p - tok);

        if (index == PROC_STAT_STATE_INDEX) {
            if (len != 1 || !isalpha((unsigned char)tok[0])) return false;
            if (out_state) *out_state = tok[0];
        } else if (index == PROC_STAT_STARTTIME_IDX) {
            if (len == 0 || len > 20) return false;
            char tmp[24];
            memcpy(tmp, tok, len);
            tmp[len] = 0;
            errno = 0;
            char *endptr = NULL;
            unsigned long long v = strtoull(tmp, &endptr, 10);
            if (errno != 0 || endptr == tmp || *endptr != 0) return false;
            if (out_starttime) *out_starttime = (uint64_t)v;
            return true;
        }
        index++;
    }
    return false;
}

uint64_t minibox_pid_start_time(pid_t pid) {
    if (pid <= 0) return 0;
    uint64_t starttime = 0;
    if (!read_proc_pid_identity(pid, NULL, &starttime)) return 0;
    return starttime;
}

bool minibox_pid_is_live(pid_t pid) {
    if (pid <= 1) return false;
    char state = 0;
    if (!read_proc_pid_identity(pid, &state, NULL)) return false;
    /* A reaped-but-not-yet-waited process is not doing any work. */
    return state != 'Z' && state != 'X' && state != 'x';
}

bool minibox_pid_is_gone(pid_t pid) {
    if (pid <= 1) return true;
    /*
     * A zombie still exists as a /proc entry, so this stays false until the owning
     * parent has reaped it. Callers that must not outrun the parent which is about
     * to publish a result need this stricter test rather than minibox_pid_is_live().
     */
    if (kill(pid, 0) == 0) return false;
    return errno == ESRCH;
}

/*
 * Confirm that `pid` is still the very process MiniBox started, by comparing the
 * kernel's process start time against the value captured at launch. A recycled PID
 * that now belongs to an unrelated process fails this check, so callers must never
 * signal it.
 */
bool minibox_pid_matches(pid_t pid, uint64_t recorded_start_time) {
    if (pid <= 1) return false;
    uint64_t current = minibox_pid_start_time(pid);
    if (current == 0) return false;
    if (recorded_start_time == 0) {
        /* No recorded start time (legacy record): fall back to liveness only. */
        return minibox_pid_is_live(pid);
    }
    return current == recorded_start_time;
}

void minibox_generate_workload_id(char *out_id, size_t max_len) {
    if (!out_id || max_len < 32) return;
    const char *base = minibox_get_state_dir();

    unsigned int pid_seed = 0;
    int seed_fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (seed_fd >= 0) {
        (void)read(seed_fd, &pid_seed, sizeof(pid_seed));
        close(seed_fd);
    }
    /* Mix in wall-clock and PID entropy so the rand() fallback below is not
     * reproducible across identically-timed processes in the same second. */
    srand((unsigned int)(minibox_current_time_ms() ^ ((uint64_t)getpid() << 16) ^ pid_seed));

    for (int attempts = 0; attempts < 100; attempts++) {
        uint64_t rnd64 = 0;
        int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            if (read(fd, &rnd64, sizeof(rnd64)) != (ssize_t)sizeof(rnd64)) {
                rnd64 = 0;
            }
            close(fd);
        }
        if (rnd64 == 0) {
            /* Deterministic fallback only used when /dev/urandom is unavailable. */
            rnd64 = ((uint64_t)rand() << 32) ^ (uint64_t)rand() ^ (uint64_t)getpid();
        }

        uint64_t now = minibox_current_time_ms();
        static unsigned int counter = 0;
        counter++;

        snprintf(out_id, max_len, "wl-%lx%04x%016llx",
                 (unsigned long)(now & 0xFFFFFFFF),
                 (counter + (unsigned int)getpid()) & 0xFFFF,
                 (unsigned long long)rnd64);

        char check_path[MINIBOX_WL_DIR_MAX];
        snprintf(check_path, sizeof(check_path), "%s/%s", base, out_id);
        struct stat st;
        if (stat(check_path, &st) != 0 && errno == ENOENT) {
            return;
        }
    }

    /* Exhausted every attempt: fall back to a name that is extremely unlikely to collide. */
    snprintf(out_id, max_len, "wl-%lx%016llx",
             (unsigned long)(minibox_current_time_ms() & 0xFFFFFFFF),
             (unsigned long long)(((uint64_t)getpid() << 32) ^ (uint64_t)time(NULL)));
}

int minibox_workload_init(minibox_workload_t *wl,
                          const char *requested_id,
                          const char *name,
                          const char *executable,
                          char *const *argv,
                          const char *work_dir) {
    if (!wl || !executable) {
        return -1;
    }
    memset(wl, 0, sizeof(*wl));

    if (requested_id && strlen(requested_id) > 0) {
        if (!is_safe_workload_id(requested_id)) {
            errno = EINVAL;
            return -1;
        }
        /*
         * Refuse to reuse an existing identifier. Silently adopting a live record
         * would destroy the previous workload's metadata, hijack its cgroup and
         * orphan its log file.
         */
        char existing_base[MINIBOX_STATE_DIR_MAX];
        if (minibox_get_state_dir_buf(existing_base, sizeof(existing_base)) != 0) return -1;
        char existing_dir[MINIBOX_WL_DIR_MAX];
        snprintf(existing_dir, sizeof(existing_dir), "%s/%s", existing_base, requested_id);
        struct stat existing_st;
        if (stat(existing_dir, &existing_st) == 0) {
            errno = EEXIST;
            return -1;
        }
        if (errno != ENOENT) {
            return -1;
        }
        snprintf(wl->id, sizeof(wl->id), "%s", requested_id);
    } else {
        minibox_generate_workload_id(wl->id, sizeof(wl->id));
    }

    if (name && strlen(name) > 0) {
        snprintf(wl->name, sizeof(wl->name), "%s", name);
    } else {
        snprintf(wl->name, sizeof(wl->name), "%s", wl->id);
    }

    snprintf(wl->executable, sizeof(wl->executable), "%s", executable);

    if (work_dir && strlen(work_dir) > 0) {
        snprintf(wl->work_dir, sizeof(wl->work_dir), "%s", work_dir);
    } else {
        if (!getcwd(wl->work_dir, sizeof(wl->work_dir))) {
            wl->work_dir[0] = '.';
            wl->work_dir[1] = 0;
        }
    }

    /* Join argv into string */
    wl->args[0] = 0;
    if (argv) {
        size_t offset = 0;
        for (int i = 0; argv[i] != NULL; i++) {
            int written = snprintf(wl->args + offset, sizeof(wl->args) - offset,
                                   "%s%s", (i > 0 ? " " : ""), argv[i]);
            if (written < 0 || (size_t)written >= sizeof(wl->args) - offset) {
                break;
            }
            offset += (size_t)written;
        }
    }

    wl->state = MINIBOX_STATE_CREATED;
    wl->created_at_ms = minibox_current_time_ms();

    char base[MINIBOX_STATE_DIR_MAX];
    if (minibox_get_state_dir_buf(base, sizeof(base)) != 0) return -1;
    char wl_dir[MINIBOX_WL_DIR_MAX];
    snprintf(wl_dir, sizeof(wl_dir), "%s/%s", base, wl->id);
    mkdir_p(wl_dir, 0700);

    snprintf(wl->log_file, sizeof(wl->log_file), "%s/output.log", wl_dir);

    return 0;
}

static void write_json_escaped_string(FILE *f, const char *key, const char *val, bool trailing_comma) {
    fprintf(f, "  \"%s\": \"", key);
    if (val) {
        for (const char *p = val; *p; p++) {
            switch (*p) {
                case '\"': fputs("\\\"", f); break;
                case '\\': fputs("\\\\", f); break;
                case '\b': fputs("\\b", f); break;
                case '\f': fputs("\\f", f); break;
                case '\n': fputs("\\n", f); break;
                case '\r': fputs("\\r", f); break;
                case '\t': fputs("\\t", f); break;
                default:
                    if ((unsigned char)*p < 0x20) {
                        fprintf(f, "\\u%04x", (unsigned char)*p);
                    } else {
                        fputc(*p, f);
                    }
                    break;
            }
        }
    }
    fprintf(f, "\"%s\n", trailing_comma ? "," : "");
}

int minibox_workload_save(const minibox_workload_t *wl) {
    if (!wl || wl->id[0] == 0) return -1;

    char base[MINIBOX_STATE_DIR_MAX];
    if (minibox_get_state_dir_buf(base, sizeof(base)) != 0) return -1;
    char dir[MINIBOX_WL_DIR_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", base, wl->id);
    mkdir_p(dir, 0700);

    char target_file[MINIBOX_WL_DIR_MAX + 32];
    snprintf(target_file, sizeof(target_file), "%s/state.json", dir);

    /*
     * Use a unique temporary name per writer. A fixed "state.json.tmp" would let a
     * supervisor and a concurrent reader interleave writes into the same inode,
     * after which either rename() could publish the other's partial bytes.
     */
    char tmp_file[MINIBOX_WL_DIR_MAX + 32];
    snprintf(tmp_file, sizeof(tmp_file), "%s/state.json.XXXXXX", dir);

    int fd = mkstemp(tmp_file);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved = errno;
        close(fd);
        unlink(tmp_file);
        errno = saved;
        return -1;
    }

    FILE *f = fdopen(fd, "w");
    if (!f) {
        int saved = errno;
        close(fd);
        unlink(tmp_file);
        errno = saved;
        return -1;
    }

    fprintf(f, "{\n");
    write_json_escaped_string(f, "id", wl->id, true);
    write_json_escaped_string(f, "name", wl->name, true);
    write_json_escaped_string(f, "state", minibox_state_to_string(wl->state), true);
    fprintf(f, "  \"pid\": %d,\n", wl->pid);
    fprintf(f, "  \"supervisor_pid\": %d,\n", wl->supervisor_pid);
    write_json_escaped_string(f, "executable", wl->executable, true);
    write_json_escaped_string(f, "args", wl->args, true);
    write_json_escaped_string(f, "work_dir", wl->work_dir, true);
    fprintf(f, "  \"created_at_ms\": %lu,\n", (unsigned long)wl->created_at_ms);
    fprintf(f, "  \"started_at_ms\": %lu,\n", (unsigned long)wl->started_at_ms);
    fprintf(f, "  \"finished_at_ms\": %lu,\n", (unsigned long)wl->finished_at_ms);
    fprintf(f, "  \"exit_code\": %d,\n", wl->exit_code);
    fprintf(f, "  \"term_signal\": %d,\n", wl->term_signal);
    fprintf(f, "  \"exec_errno\": %d,\n", wl->exec_errno);
    write_json_escaped_string(f, "error_msg", wl->error_msg, true);
    write_json_escaped_string(f, "log_file", wl->log_file, true);
    fprintf(f, "  \"iso_uts\": %s,\n", wl->iso_uts ? "true" : "false");
    write_json_escaped_string(f, "iso_hostname", wl->iso_hostname, true);
    fprintf(f, "  \"iso_pid\": %s,\n", wl->iso_pid ? "true" : "false");
    fprintf(f, "  \"iso_mount\": %s,\n", wl->iso_mount ? "true" : "false");
    fprintf(f, "  \"cpu_percent\": %d,\n", wl->limits.cpu_percent);
    fprintf(f, "  \"memory_max_bytes\": %lu,\n", (unsigned long)wl->limits.memory_max_bytes);
    fprintf(f, "  \"pids_max\": %d,\n", wl->limits.pids_max);
    write_json_escaped_string(f, "cgroup_path", wl->cgroup_path, true);
    fprintf(f, "  \"cgroup_active\": %s,\n", wl->cgroup_active ? "true" : "false");
    fprintf(f, "  \"pid_start_time\": %lu,\n", (unsigned long)wl->pid_start_time);
    fprintf(f, "  \"oom_killed\": %s\n", wl->oom_killed ? "true" : "false");
    fprintf(f, "}\n");

    int write_errno = 0;
    if (fflush(f) != 0) write_errno = errno;
    if (fsync(fileno(f)) != 0 && write_errno == 0) write_errno = errno;
    if (fclose(f) != 0 && write_errno == 0) write_errno = errno;
    if (write_errno != 0) {
        unlink(tmp_file);
        errno = write_errno;
        return -1;
    }

    if (rename(tmp_file, target_file) != 0) {
        int saved = errno;
        unlink(tmp_file);
        errno = saved;
        return -1;
    }
    return 0;
}

static void parse_json_string(const char *json, const char *key, char *out, size_t max_len) {
    if (!out || max_len == 0) return;
    out[0] = 0;
    char search[128];
    snprintf(search, sizeof(search), "\"%s\": \"", key);
    const char *p = strstr(json, search);
    if (!p) {
        return;
    }
    p += strlen(search);
    size_t i = 0;
    while (*p && *p != '\"' && i + 1 < max_len) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
                case '\"': out[i++] = '\"'; p++; break;
                case '\\': out[i++] = '\\'; p++; break;
                case '/':  out[i++] = '/'; p++; break;
                case 'b':  out[i++] = '\b'; p++; break;
                case 'f':  out[i++] = '\f'; p++; break;
                case 'n':  out[i++] = '\n'; p++; break;
                case 'r':  out[i++] = '\r'; p++; break;
                case 't':  out[i++] = '\t'; p++; break;
                case 'u': {
                    if (isxdigit((unsigned char)*(p + 1)) &&
                        isxdigit((unsigned char)*(p + 2)) &&
                        isxdigit((unsigned char)*(p + 3)) &&
                        isxdigit((unsigned char)*(p + 4))) {
                        char hex[5] = { *(p + 1), *(p + 2), *(p + 3), *(p + 4), 0 };
                        unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
                        p += 5;

                        /* Check for UTF-16 surrogate pairs */
                        if (cp >= 0xD800 && cp <= 0xDBFF && *p == '\\' && *(p + 1) == 'u') {
                            if (isxdigit((unsigned char)*(p + 2)) &&
                                isxdigit((unsigned char)*(p + 3)) &&
                                isxdigit((unsigned char)*(p + 4)) &&
                                isxdigit((unsigned char)*(p + 5))) {
                                char hex2[5] = { *(p + 2), *(p + 3), *(p + 4), *(p + 5), 0 };
                                unsigned int cp2 = (unsigned int)strtoul(hex2, NULL, 16);
                                if (cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                                    cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                                    p += 6;
                                }
                            }
                        }

                        /* Encode into UTF-8 */
                        if (cp <= 0x7F) {
                            if (i + 1 < max_len) out[i++] = (char)cp;
                        } else if (cp <= 0x7FF) {
                            if (i + 2 < max_len) {
                                out[i++] = (char)(0xC0 | (cp >> 6));
                                out[i++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else if (cp <= 0xFFFF) {
                            if (i + 3 < max_len) {
                                out[i++] = (char)(0xE0 | (cp >> 12));
                                out[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[i++] = (char)(0x80 | (cp & 0x3F));
                            }
                        } else if (cp <= 0x10FFFF) {
                            if (i + 4 < max_len) {
                                out[i++] = (char)(0xF0 | (cp >> 18));
                                out[i++] = (char)(0x80 | ((cp >> 12) & 0x3F));
                                out[i++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                                out[i++] = (char)(0x80 | (cp & 0x3F));
                            }
                        }
                    } else {
                        out[i++] = 'u';
                        p++;
                    }
                    break;
                }
                default:
                    out[i++] = *p++;
                    break;
            }
        } else {
            out[i++] = *p++;
        }
    }
    out[i] = 0;
}

static long parse_json_int(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\": ", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    return strtol(p, NULL, 10);
}

static uint64_t parse_json_uint64(const char *json, const char *key) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\": ", key);
    const char *p = strstr(json, search);
    if (!p) return 0;
    p += strlen(search);
    return (uint64_t)strtoull(p, NULL, 10);
}

int minibox_workload_load(const char *id, minibox_workload_t *wl) {
    if (!id || id[0] == 0 || !wl) {
        errno = EINVAL;
        return -1;
    }

    if (!is_safe_workload_id(id)) {
        errno = EINVAL;
        return -1;
    }

    char base[MINIBOX_STATE_DIR_MAX];
    if (minibox_get_state_dir_buf(base, sizeof(base)) != 0) return -1;
    char filepath[MINIBOX_WL_DIR_MAX + 32];
    snprintf(filepath, sizeof(filepath), "%s/%s/state.json", base, id);

    char *buffer = NULL;
    ssize_t n = read_whole_file(filepath, &buffer);
    if (n < 0) return -1;
    if (n == 0) {
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    /* Validate basic JSON envelope */
    const char *start = buffer;
    while (*start && isspace((unsigned char)*start)) start++;
    if (*start != '{') {
        free(buffer);
        errno = EINVAL;
        return -1;
    }
    const char *end = buffer + n;
    while (end > start && isspace((unsigned char)end[-1])) end--;
    if (end <= start || end[-1] != '}') {
        free(buffer);
        errno = EINVAL;
        return -1;
    }

    memset(wl, 0, sizeof(*wl));
    parse_json_string(buffer, "id", wl->id, sizeof(wl->id));
    if (wl->id[0] == 0 || strcmp(wl->id, id) != 0) {
        free(buffer);
        errno = EINVAL;
        return -1;
    }
    parse_json_string(buffer, "name", wl->name, sizeof(wl->name));

    char state_str[32] = {0};
    parse_json_string(buffer, "state", state_str, sizeof(state_str));
    wl->state = minibox_state_from_string(state_str);

    wl->pid = (pid_t)parse_json_int(buffer, "pid");
    wl->supervisor_pid = (pid_t)parse_json_int(buffer, "supervisor_pid");
    parse_json_string(buffer, "executable", wl->executable, sizeof(wl->executable));
    parse_json_string(buffer, "args", wl->args, sizeof(wl->args));
    parse_json_string(buffer, "work_dir", wl->work_dir, sizeof(wl->work_dir));

    wl->created_at_ms = parse_json_uint64(buffer, "created_at_ms");
    wl->started_at_ms = parse_json_uint64(buffer, "started_at_ms");
    wl->finished_at_ms = parse_json_uint64(buffer, "finished_at_ms");

    wl->exit_code = (int)parse_json_int(buffer, "exit_code");
    wl->term_signal = (int)parse_json_int(buffer, "term_signal");
    wl->exec_errno = (int)parse_json_int(buffer, "exec_errno");
    parse_json_string(buffer, "error_msg", wl->error_msg, sizeof(wl->error_msg));
    parse_json_string(buffer, "log_file", wl->log_file, sizeof(wl->log_file));
    wl->iso_uts = (strstr(buffer, "\"iso_uts\": true") != NULL);
    parse_json_string(buffer, "iso_hostname", wl->iso_hostname, sizeof(wl->iso_hostname));
    wl->iso_pid = (strstr(buffer, "\"iso_pid\": true") != NULL);
    wl->iso_mount = (strstr(buffer, "\"iso_mount\": true") != NULL);
    wl->limits.cpu_percent = (int)parse_json_int(buffer, "cpu_percent");
    wl->limits.memory_max_bytes = parse_json_uint64(buffer, "memory_max_bytes");
    wl->limits.pids_max = (int)parse_json_int(buffer, "pids_max");
    parse_json_string(buffer, "cgroup_path", wl->cgroup_path, sizeof(wl->cgroup_path));
    wl->cgroup_active = (strstr(buffer, "\"cgroup_active\": true") != NULL);
    wl->pid_start_time = parse_json_uint64(buffer, "pid_start_time");
    wl->oom_killed = (strstr(buffer, "\"oom_killed\": true") != NULL);

    free(buffer);
    return 0;
}

int minibox_workload_refresh_state(minibox_workload_t *wl) {
    if (!wl || wl->id[0] == 0) return -1;

    /* Reload from disk in case supervisor wrote an update */
    minibox_workload_t disk_wl;
    if (minibox_workload_load(wl->id, &disk_wl) == 0) {
        *wl = disk_wl;
    }

    if (wl->state == MINIBOX_STATE_RUNNING || wl->state == MINIBOX_STATE_STOPPING) {
        /* Only act once the kernel has fully reaped the process. Testing for a
         * zombie here would race the parent that is about to publish the real
         * exit status. */
        if (wl->pid > 1 && minibox_pid_is_gone(wl->pid)) {
            /*
             * Re-read first: the supervisor may have published the authoritative
             * terminal record while we were checking.
             */
            minibox_workload_t latest;
            if (minibox_workload_load(wl->id, &latest) == 0) {
                *wl = latest;
            }
            if (minibox_state_is_terminal(wl->state)) {
                return 0;
            }
            /*
             * A supervisor that is still running owns the terminal record; leave the
             * state alone rather than writing a guess over its result.
             */
            if (wl->supervisor_pid > 1 && minibox_pid_is_live(wl->supervisor_pid)) {
                return 0;
            }
            /* No supervisor will ever publish a result, so record the outcome. */
            wl->state = MINIBOX_STATE_TERMINATED;
            if (wl->finished_at_ms == 0) {
                wl->finished_at_ms = minibox_current_time_ms();
            }
            minibox_workload_save(wl);
        }
    }
    return 0;
}

int minibox_workload_list(minibox_workload_t **out_workloads, size_t *out_count) {
    if (!out_workloads || !out_count) return -1;

    *out_workloads = NULL;
    *out_count = 0;

    char base[MINIBOX_STATE_DIR_MAX];
    if (minibox_get_state_dir_buf(base, sizeof(base)) != 0) return -1;
    DIR *d = opendir(base);
    if (!d) return -1;

    size_t capacity = 16;
    size_t count = 0;
    minibox_workload_t *list = malloc(capacity * sizeof(minibox_workload_t));
    if (!list) {
        closedir(d);
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        minibox_workload_t wl;
        if (minibox_workload_load(entry->d_name, &wl) == 0) {
            minibox_workload_refresh_state(&wl);

            if (count >= capacity) {
                capacity *= 2;
                minibox_workload_t *new_list = realloc(list, capacity * sizeof(minibox_workload_t));
                if (!new_list) {
                    free(list);
                    closedir(d);
                    return -1;
                }
                list = new_list;
            }
            list[count++] = wl;
        }
    }
    closedir(d);

    *out_workloads = list;
    *out_count = count;
    return 0;
}

void minibox_workload_free_list(minibox_workload_t *workloads) {
    free(workloads);
}

static int remove_directory_recursive(const char *path) {
    DIR *d = opendir(path);
    if (!d) return (errno == ENOENT) ? 0 : -1;

    struct dirent *entry;
    char subpath[MINIBOX_FULL_PATH_MAX];
    int rc = 0;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(subpath, &st) != 0) {
            rc = -1;
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            if (remove_directory_recursive(subpath) != 0) rc = -1;
        } else if (unlink(subpath) != 0 && errno != ENOENT) {
            rc = -1;
        }
    }
    closedir(d);

    if (rmdir(path) != 0 && errno != ENOENT) return -1;
    return rc;
}

int minibox_workload_delete(const char *id, bool force) {
    if (!id || !is_safe_workload_id(id)) {
        errno = EINVAL;
        return -1;
    }

    minibox_workload_t wl;
    if (minibox_workload_load(id, &wl) != 0) {
        return -1;
    }
    minibox_workload_refresh_state(&wl);

    if (wl.state == MINIBOX_STATE_RUNNING || wl.state == MINIBOX_STATE_STOPPING) {
        if (!force) {
            errno = EBUSY;
            return -1;
        }
        /* Force-terminate, but only if the PID is still the process we launched.
         * Signalling a recycled PID would kill an unrelated process. */
        if (wl.pid > 1 && minibox_pid_matches(wl.pid, wl.pid_start_time)) {
            kill(wl.pid, SIGKILL);
        }
        /* Wait briefly for the process to actually disappear before removing the cgroup. */
        for (int i = 0; i < 50; i++) {
            if (wl.pid <= 1 || !minibox_pid_is_live(wl.pid)) break;
            usleep(20000);
        }
    }

    int cg_rc = 0;
    if (wl.cgroup_path[0] != 0) {
        /* Strictly validate cgroup path before cleanup: must be inside /sys/fs/cgroup/ and contain 'minibox' */
        if (strncmp(wl.cgroup_path, "/sys/fs/cgroup/", 15) == 0 &&
            strstr(wl.cgroup_path, "minibox") != NULL &&
            strstr(wl.cgroup_path, "..") == NULL) {
            cg_rc = minibox_cgroup_cleanup(wl.cgroup_path);
        } else {
            errno = EINVAL;
            return -1;
        }
    }

    char base[MINIBOX_STATE_DIR_MAX];
    if (minibox_get_state_dir_buf(base, sizeof(base)) != 0) return -1;
    char dir[MINIBOX_WL_DIR_MAX];
    snprintf(dir, sizeof(dir), "%s/%s", base, id);

    /* Verify that dir resolves within base */
    char resolved_base[PATH_MAX] = {0};
    char resolved_dir[PATH_MAX] = {0};
    if (realpath(base, resolved_base) && realpath(dir, resolved_dir)) {
        size_t base_len = strlen(resolved_base);
        if (strncmp(resolved_dir, resolved_base, base_len) != 0 ||
            resolved_dir[base_len] != '/') {
            errno = EACCES;
            return -1;
        }
    } else {
        return -1;
    }

    int dir_rc = remove_directory_recursive(dir);

    if (cg_rc != 0 && errno != ENOENT) {
        return -1;
    }
    return dir_rc;
}

int minibox_workload_cleanup_completed(size_t *removed_count) {
    minibox_workload_t *list = NULL;
    size_t count = 0;
    if (minibox_workload_list(&list, &count) != 0) {
        return -1;
    }

    size_t removed = 0;
    for (size_t i = 0; i < count; i++) {
        if (minibox_state_is_terminal(list[i].state)) {
            if (minibox_workload_delete(list[i].id, true) == 0) {
                removed++;
            }
        }
    }
    minibox_workload_free_list(list);
    if (removed_count) *removed_count = removed;
    return 0;
}
