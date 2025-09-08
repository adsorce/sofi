#include <dirent.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "files.h"
#include "drun.h"
#include "desktop_vec.h"
#include "log.h"
#include "mkdirp.h"
#include "xmalloc.h"

static const char *default_cache_dir = ".cache";
static const char *cache_basename = "sorce-files";

/* Directories to exclude from search */
static const char *exclude_dirs[] = {
    "/proc", "/sys", "/dev", "/run", "/tmp",
    "/var/lib/docker", "/snap", "/mnt", "/media",
    "/.git", "/node_modules", "/.cache", "/lost+found",
    NULL
};

static bool should_exclude(const char *path) {
    for (int i = 0; exclude_dirs[i] != NULL; i++) {
        if (strstr(path, exclude_dirs[i]) != NULL) {
            return true;
        }
    }
    return false;
}

static char *get_cache_path() {
    char *cache_name = NULL;
    const char *cache_path = getenv("XDG_CACHE_HOME");
    if (cache_path == NULL) {
        const char *home = getenv("HOME");
        if (home == NULL) {
            log_error("Couldn't retrieve HOME from environment.\n");
            return NULL;
        }
        size_t len = strlen(home) + 1
            + strlen(default_cache_dir) + 1
            + strlen(cache_basename) + 1;
        cache_name = xmalloc(len);
        snprintf(cache_name, len, "%s/%s/%s", home, default_cache_dir, cache_basename);
    } else {
        size_t len = strlen(cache_path) + 1 + strlen(cache_basename) + 1;
        cache_name = xmalloc(len);
        snprintf(cache_name, len, "%s/%s", cache_path, cache_basename);
    }
    return cache_name;
}

static void scan_directory_to_buffer(const char *dir_path, FILE *output, int depth, int *count) {
    if (depth > 3) return;
    if (should_exclude(dir_path)) return;
    if (*count > 5000) return;  /* Limit total files */
    
    DIR *dir = opendir(dir_path);
    if (dir == NULL) return;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (*count > 5000) break;
        
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        struct stat st;
        if (stat(full_path, &st) == -1) continue;
        
        if (S_ISREG(st.st_mode)) {
            /* Store as: basename|||full_path for special parsing */
            const char *basename = strrchr(full_path, '/');
            if (basename) {
                basename++; /* Skip the '/' */
                fprintf(output, "%s|||%s\n", basename, full_path);
            } else {
                fprintf(output, "%s|||%s\n", entry->d_name, full_path);
            }
            (*count)++;
        } else if (S_ISDIR(st.st_mode)) {
            scan_directory_to_buffer(full_path, output, depth + 1, count);
        }
    }
    
    closedir(dir);
}

static int cached_app_count = -1;

static char *generate_file_list() {
    /* Create temporary file for building the list */
    char tmp_path[] = "/tmp/sorce-files-XXXXXX";
    int fd = mkstemp(tmp_path);
    if (fd == -1) {
        log_error("Failed to create temp file.\n");
        return xstrdup("");
    }
    
    FILE *tmp = fdopen(fd, "w+");
    if (tmp == NULL) {
        close(fd);
        unlink(tmp_path);
        return xstrdup("");
    }
    
    /* First, add all desktop apps */
    log_debug("Adding apps to unified list.\n");
    struct desktop_vec apps = drun_generate_cached();
    cached_app_count = 0;
    for (size_t i = 0; i < apps.count; i++) {
        fprintf(tmp, "%s\n", apps.buf[i].name);
        cached_app_count++;
    }
    desktop_vec_destroy(&apps);
    
    /* Add separator line between apps and files */
    if (cached_app_count > 0) {
        fprintf(tmp, "\n");
        cached_app_count++;
    }
    
    int count = 0;
    const char *home = getenv("HOME");
    
    if (home != NULL) {
        char path[PATH_MAX];
        
        /* Scan priority directories */
        snprintf(path, sizeof(path), "%s/Documents", home);
        scan_directory_to_buffer(path, tmp, 0, &count);
        
        snprintf(path, sizeof(path), "%s/Downloads", home);
        scan_directory_to_buffer(path, tmp, 0, &count);
        
        snprintf(path, sizeof(path), "%s/Desktop", home);
        scan_directory_to_buffer(path, tmp, 0, &count);
        
        snprintf(path, sizeof(path), "%s/Pictures", home);
        scan_directory_to_buffer(path, tmp, 0, &count);
        
        snprintf(path, sizeof(path), "%s/Videos", home);
        scan_directory_to_buffer(path, tmp, 0, &count);
        
        /* Scan home directory */
        if (count < 4000) {
            scan_directory_to_buffer(home, tmp, 0, &count);
        }
    }
    
    /* Get file size */
    fseek(tmp, 0, SEEK_END);
    long size = ftell(tmp);
    fseek(tmp, 0, SEEK_SET);
    
    /* Read into buffer */
    char *buffer = xmalloc(size + 1);
    fread(buffer, 1, size, tmp);
    buffer[size] = '\0';
    
    fclose(tmp);
    unlink(tmp_path);
    
    log_debug("Generated %d files.\n", count);
    return buffer;
}

char *files_generate_cached() {
    char *cache_path = get_cache_path();
    
    if (cache_path == NULL) {
        log_error("Failed to get cache path.\n");
        return generate_file_list();
    }
    
    /* Try to read from cache */
    FILE *cache = fopen(cache_path, "r");
    if (cache != NULL) {
        fseek(cache, 0, SEEK_END);
        long size = ftell(cache);
        fseek(cache, 0, SEEK_SET);
        
        char *buffer = xmalloc(size + 1);
        fread(buffer, 1, size, cache);
        buffer[size] = '\0';
        
        fclose(cache);
        
        /* Count apps in cached data */
        struct desktop_vec apps = drun_generate_cached();
        cached_app_count = apps.count;
        desktop_vec_destroy(&apps);
        
        free(cache_path);
        
        log_debug("Loaded files from cache with %d apps.\n", cached_app_count);
        return buffer;
    }
    
    /* Generate new list */
    char *buffer = generate_file_list();
    
    /* Save to cache */
    /* Create cache directory if needed */
    char *dir = strdup(cache_path);
    char *last_slash = strrchr(dir, '/');
    if (last_slash != NULL) {
        *last_slash = '\0';
        mkdirp(dir);
    }
    free(dir);
    
    cache = fopen(cache_path, "w");
    if (cache != NULL) {
        fputs(buffer, cache);
        fclose(cache);
        log_debug("Saved files to cache.\n");
    }
    
    free(cache_path);
    return buffer;
}

static int get_item_index(const char *item) {
    /* Read the cache to find item's position */
    char *cache_path = get_cache_path();
    if (cache_path == NULL) return -1;
    
    FILE *cache = fopen(cache_path, "r");
    if (cache == NULL) {
        free(cache_path);
        return -1;
    }
    
    char line[PATH_MAX * 2];  /* Increased size for basename|||path format */
    int index = 0;
    while (fgets(line, sizeof(line), cache) != NULL) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
        }
        if (strcmp(line, item) == 0) {
            fclose(cache);
            free(cache_path);
            return index;
        }
        index++;
    }
    
    fclose(cache);
    free(cache_path);
    return -1;
}

void files_launch(const char *path) {
    /* Check if it's a file with our special format */
    const char *separator = strstr(path, "|||");
    if (separator) {
        /* It's a file - extract the actual path after ||| */
        const char *actual_path = separator + 3;
        log_debug("Attempting to launch file: %s\n", actual_path);
        
        /* Fork and exec to properly launch the file */
        pid_t pid = fork();
        if (pid == 0) {
            /* Child process */
            /* Close file descriptors to detach from parent */
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            
            /* Create new session */
            setsid();
            
            /* Try gio open first (more reliable), fall back to xdg-open */
            execlp("gio", "gio", "open", actual_path, NULL);
            /* If gio fails, try xdg-open */
            execlp("xdg-open", "xdg-open", actual_path, NULL);
            /* If both fail, exit */
            exit(1);
        } else if (pid > 0) {
            /* Parent process - fork another process to focus Firefox after a delay */
            log_debug("Launched file opener with PID: %d\n", pid);
            
            /* Fork another process to handle window focusing */
            pid_t focus_pid = fork();
            if (focus_pid == 0) {
                /* Child process for focusing */
                /* Redirect to log file for debugging */
                FILE *log = fopen("/tmp/sorce-focus.log", "w");
                if (log) {
                    dup2(fileno(log), STDERR_FILENO);
                    dup2(fileno(log), STDOUT_FILENO);
                }
                setsid();
                
                /* Wait a bit for the file to open */
                usleep(20000); /* 20ms */
                
                /* Try to focus Firefox window using niri - write to log */
                fprintf(stderr, "Attempting to focus Firefox window...\n");
                int ret = system("niri msg windows | grep 'App ID: \"firefox\"' -B2 | grep 'Window ID' | head -1 | awk '{print $3}' | sed 's/://' | tee /tmp/firefox-id.txt | xargs -I {} sh -c 'echo \"Focusing window ID: {}\" && niri msg action focus-window --id {}'");
                fprintf(stderr, "Focus command returned: %d\n", ret);
                
                if (log) fclose(log);
                exit(0);
            }
        } else {
            log_error("Failed to fork: %s\n", strerror(errno));
        }
        return;
    }
    
    /* Check if it's just an empty line (separator) */
    if (strlen(path) == 0) {
        return;
    }
    
    /* Otherwise it's an app - find and launch it */
    struct desktop_vec apps = drun_generate_cached();
    for (size_t i = 0; i < apps.count; i++) {
        if (strcmp(apps.buf[i].name, path) == 0) {
            drun_launch(apps.buf[i].path);
            desktop_vec_destroy(&apps);
            return;
        }
    }
    desktop_vec_destroy(&apps);
    log_error("Failed to find app: %s\n", path);
}