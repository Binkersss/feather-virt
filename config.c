#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>

#define DEFAULT_IMAGES_DIR "/var/sandbox/basefs"
#define DEFAULT_CONTAINER_BASE "/var/sandbox/containers"
#define DEFAULT_CGROUP_BASE "/sys/fs/cgroup/sandbox"
#define DEFAULT_SHELL "/bin/sh"
#define CACHE_DIR "/var/sandbox/cache"

int config_save_to_file(const container_config_t *cfg) 
{
    FILE *fp = fopen(cfg->config_file, "w");
    if (!fp) {
        perror("Failed to open config file");
        return -1;
    }
    
    char time_buf[64];
    struct tm *tm_info = gmtime(&cfg->created_at);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    
    fprintf(fp, "{\n");
    fprintf(fp, "  \"pid\": %d,\n", cfg->pid);
    fprintf(fp, "  \"name\": \"%s\",\n", cfg->name);
    fprintf(fp, "  \"image\": \"%s\",\n", cfg->base_root);
    fprintf(fp, "  \"created_at\": \"%s\",\n", time_buf);
    fprintf(fp, "  \"command\": [\"%s\"],\n", cfg->shell);
    fprintf(fp, "  \"limits\": {\n");
    fprintf(fp, "    \"memory_bytes\": %zu,\n", cfg->limits.memory_bytes);
    fprintf(fp, "    \"cpu_quota\": %d,\n", cfg->limits.cpu_quota);
    fprintf(fp, "    \"pids_max\": %d\n", cfg->limits.pids_max);
    fprintf(fp, "  },\n");
    fprintf(fp, "  \"status\": \"%s\"", cfg->status);
    if (strcmp(cfg->status, "exited") == 0) {
        fprintf(fp, ",\n  \"exit_code\": %d\n", cfg->exit_code);
    } else {
        fprintf(fp, "\n");
    }
    fprintf(fp, "}\n");
    
    fclose(fp);
    return 0;
}

int config_load_from_file(const char *config_path, container_config_t *cfg) {
    FILE *fp = fopen(config_path, "r");
    if (!fp) return -1;
    
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        char *key, *value;
        
        // Very basic parsing - you may want to use a proper JSON library
        if (sscanf(line, "  \"pid\": %d", &cfg->pid) == 1) continue;
        if (sscanf(line, "  \"name\": \"%127[^\"]\"", cfg->name) == 1) continue;
        if (sscanf(line, "  \"status\": \"%31[^\"]\"", cfg->status) == 1) continue;
        // ... parse other fields ...
    }
    
    fclose(fp);
    return 0;
}

int config_update_status(const container_config_t *cfg, const char *status) {
    container_config_t tmp = *cfg;
    strncpy(tmp.status, status, sizeof(tmp.status) - 1);
    return config_save_to_file(&tmp);
}

int config_list_containers(int show_all) {
    DIR *dir = opendir("/var/sandbox/containers");
    if (!dir) {
        perror("Failed to open containers directory");
        return -1;
    }
    
    printf("%-10s %-20s %-20s %-15s %-10s\n",
           "PID", "NAME", "IMAGE", "STATUS", "CREATED");
    printf("%s\n", "-------------------------------------------------------------------");
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        
        char config_path[MAX_PATH_LEN];
        snprintf(config_path, sizeof(config_path),
                 "/var/sandbox/containers/%s/config.json", entry->d_name);
        
        container_config_t cfg;
        if (config_load_from_file(config_path, &cfg) != 0) {
            continue;
        }
        
        // Check if process is still running
        int is_running = (kill(cfg.pid, 0) == 0);
        if (!is_running && strcmp(cfg.status, "running") == 0) {
            strncpy(cfg.status, "stopped", sizeof(cfg.status) - 1);
            config_save_to_file(&cfg);
        }
        
        if (!show_all && strcmp(cfg.status, "running") != 0) {
            continue;
        }
        
        char time_str[32];
        struct tm *tm_info = localtime(&cfg.created_at);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M", tm_info);
        
        // Extract just the image name from full path
        const char *image_name = strrchr(cfg.base_root, '/');
        image_name = image_name ? image_name + 1 : cfg.base_root;
        
        printf("%-10d %-20s %-20s %-15s %-10s\n",
               cfg.pid, cfg.name, image_name, cfg.status, time_str);
    }
    
    closedir(dir);
    return 0;
}

void config_init(container_config_t * cfg)
{
    memset(cfg, 0, sizeof(container_config_t));
    strncpy(cfg->container_base, DEFAULT_CONTAINER_BASE, MAX_PATH_LEN - 1);
    strncpy(cfg->cgroup_base, DEFAULT_CGROUP_BASE, MAX_PATH_LEN - 1);
    strncpy(cfg->shell, DEFAULT_SHELL, MAX_PATH_LEN - 1);
    strncpy(cfg->name, "unnamed", MAX_NAME_LEN - 1);
}

int config_set_image(container_config_t * cfg, const char *image_name)
{
    struct stat st;
    char tarball_path[MAX_PATH_LEN];
    char extracted_path[MAX_PATH_LEN];

    /* Build path to compressed tarball */
    snprintf(tarball_path, MAX_PATH_LEN, "%s/%s.tar.gz",
	     DEFAULT_IMAGES_DIR, image_name);

    /* Validate tarball exists */
    if (stat(tarball_path, &st) != 0) {
	fprintf(stderr, "Error: Image '%s' not found at %s\n", image_name,
		tarball_path);
	fprintf(stderr,
		"Hint: Run 'scripts/build_rootfs.sh build <distro> <version>' to build images\n");
	return -1;
    }

    if (!S_ISREG(st.st_mode)) {
	fprintf(stderr, "Error: %s is not a regular file\n", tarball_path);
	return -1;
    }

    /* Check if already extracted in cache */
    snprintf(extracted_path, MAX_PATH_LEN, "%s/%s", CACHE_DIR, image_name);

    if (stat(extracted_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
	/* Need to extract the tarball */
	fprintf(stderr, "Extracting image '%s' to cache...\n", image_name);

	/* Create cache directory */
	char mkdir_cmd[MAX_PATH_LEN + 32];
	snprintf(mkdir_cmd, sizeof(mkdir_cmd), "mkdir -p %s", CACHE_DIR);
	if (system(mkdir_cmd) != 0) {
	    fprintf(stderr, "Error: Failed to create cache directory\n");
	    return -1;
	}

	/* Extract tarball to cache */
	char extract_cmd[MAX_PATH_LEN * 2];
	snprintf(extract_cmd, sizeof(extract_cmd),
		 "tar -xzf %s -C %s 2>/dev/null", tarball_path, CACHE_DIR);

	if (system(extract_cmd) != 0) {
	    fprintf(stderr, "Error: Failed to extract tarball %s\n",
		    tarball_path);
	    return -1;
	}

	fprintf(stderr, "Image extracted successfully\n");
    }

    /* Use extracted directory as base_root */
    strncpy(cfg->base_root, extracted_path, MAX_PATH_LEN - 1);

    return 0;
}

void config_list_images(const char *images_dir)
{
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char path[MAX_PATH_LEN];
    int found = 0;

    if (images_dir == NULL) {
	images_dir = DEFAULT_IMAGES_DIR;
    }

    dir = opendir(images_dir);
    if (!dir) {
	fprintf(stderr, "Error: Cannot open images directory %s\n",
		images_dir);
	fprintf(stderr,
		"Hint: Run 'scripts/build_rootfs.sh build-all' to create images\n");
	return;
    }

    printf("Available base images in %s:\n", images_dir);
    printf("----------------------------------------\n");

    while ((entry = readdir(dir)) != NULL) {
	/* Skip . and .. */
	if (strcmp(entry->d_name, ".") == 0
	    || strcmp(entry->d_name, "..") == 0)
	    continue;

	/* Look for .tar.gz files */
	size_t len = strlen(entry->d_name);
	if (len > 7 && strcmp(entry->d_name + len - 7, ".tar.gz") == 0) {
	    snprintf(path, sizeof(path), "%s/%s", images_dir,
		     entry->d_name);
	    if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
		/* Remove .tar.gz extension for display */
		char display_name[256];
		strncpy(display_name, entry->d_name,
			sizeof(display_name) - 1);
		display_name[len - 7] = '\0';

		/* Show file size */
		double size_mb = (double) st.st_size / (1024.0 * 1024.0);
		printf("  - %-30s (%.2f MB)\n", display_name, size_mb);
		found = 1;
	    }
	}
    }

    if (!found) {
	printf("  (no images found)\n");
	printf("\nTo build images, run:\n");
	printf("  sudo scripts/build_rootfs.sh build alpine 3.20.2\n");
	printf("  sudo scripts/build_rootfs.sh build-all\n");
    }

    closedir(dir);
}
