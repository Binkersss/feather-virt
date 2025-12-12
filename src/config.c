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
#include <json-c/json.h>

#define DEFAULT_IMAGES_DIR "/var/sandbox/basefs"
#define DEFAULT_CONTAINER_BASE "/var/sandbox/containers"
#define DEFAULT_CGROUP_BASE "/sys/fs/cgroup/sandbox"
#define DEFAULT_SHELL "/bin/sh"
#define CACHE_DIR "/var/sandbox/cache"

int config_save_to_file(const container_config_t *cfg)
{
    json_object *root = json_object_new_object();
    if (!root) {
	fprintf(stderr, "Failed to create JSON object\n");
	return -1;
    }

    /* Add basic fields */
    json_object_object_add(root, "pid", json_object_new_int(cfg->pid));
    json_object_object_add(root, "name",
			   json_object_new_string(cfg->name));

    /* Extract image name from full path for cleaner display */
    const char *image_name = strrchr(cfg->base_root, '/');
    image_name = image_name ? image_name + 1 : cfg->base_root;
    json_object_object_add(root, "image",
			   json_object_new_string(image_name));

    /* Store full path for internal use */
    json_object_object_add(root, "base_root",
			   json_object_new_string(cfg->base_root));

    /* Format timestamp as ISO 8601 */
    char time_buf[64];
    struct tm *tm_info = gmtime(&cfg->created_at);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    json_object_object_add(root, "created_at",
			   json_object_new_string(time_buf));

    /* Add command as array */
    json_object *cmd_array = json_object_new_array();
    json_object_array_add(cmd_array, json_object_new_string(cfg->shell));
    json_object_object_add(root, "command", cmd_array);

    /* Add resource limits */
    json_object *limits = json_object_new_object();
    json_object_object_add(limits, "memory_bytes",
			   json_object_new_int64(cfg->
						 limits.memory_bytes));
    json_object_object_add(limits, "cpu_quota",
			   json_object_new_int(cfg->limits.cpu_quota));
    json_object_object_add(limits, "pids_max",
			   json_object_new_int(cfg->limits.pids_max));
    json_object_object_add(root, "limits", limits);

    /* Add status */
    json_object_object_add(root, "status",
			   json_object_new_string(cfg->status));

    /* Add exit code if container has exited */
    if (strcmp(cfg->status, "exited") == 0
	|| strcmp(cfg->status, "killed") == 0) {
	json_object_object_add(root, "exit_code",
			       json_object_new_int(cfg->exit_code));
    }

    /* Write to file with pretty printing */
    const char *json_str = json_object_to_json_string_ext(root,
							  JSON_C_TO_STRING_PRETTY);
    if (!json_str) {
	fprintf(stderr, "Failed to serialize JSON\n");
	json_object_put(root);
	return -1;
    }

    FILE *fp = fopen(cfg->config_file, "w");
    if (!fp) {
	perror("Failed to open config file for writing");
	json_object_put(root);
	return -1;
    }

    fprintf(fp, "%s\n", json_str);
    fclose(fp);

    json_object_put(root);
    return 0;
}

int config_load_from_file(const char *config_path, container_config_t *cfg)
{
    json_object *root = json_object_from_file(config_path);
    if (!root) {
	fprintf(stderr, "Failed to parse JSON from %s\n", config_path);
	return -1;
    }

    json_object *obj;

    /* Extract PID */
    if (json_object_object_get_ex(root, "pid", &obj)) {
	cfg->pid = json_object_get_int(obj);
    }

    /* Extract name */
    if (json_object_object_get_ex(root, "name", &obj)) {
	strncpy(cfg->name, json_object_get_string(obj), MAX_NAME_LEN - 1);
    }

    /* Extract full base_root path (preferred) or fall back to image name */
    if (json_object_object_get_ex(root, "base_root", &obj)) {
	strncpy(cfg->base_root, json_object_get_string(obj),
		MAX_PATH_LEN - 1);
    } else if (json_object_object_get_ex(root, "image", &obj)) {
	/* Fallback: construct path from image name */
	const char *image = json_object_get_string(obj);
	snprintf(cfg->base_root, MAX_PATH_LEN, "%s/%s", CACHE_DIR, image);
    }

    /* Extract created_at */
    if (json_object_object_get_ex(root, "created_at", &obj)) {
	const char *time_str = json_object_get_string(obj);
	struct tm tm = { 0 };
	strptime(time_str, "%Y-%m-%dT%H:%M:%SZ", &tm);
	cfg->created_at = timegm(&tm);
    }

    /* Extract command (first element) */
    if (json_object_object_get_ex(root, "command", &obj)) {
	if (json_object_is_type(obj, json_type_array) &&
	    json_object_array_length(obj) > 0) {
	    json_object *cmd = json_object_array_get_idx(obj, 0);
	    strncpy(cfg->shell, json_object_get_string(cmd),
		    MAX_PATH_LEN - 1);
	}
    }

    /* Extract limits */
    if (json_object_object_get_ex(root, "limits", &obj)) {
	json_object *limit_obj;

	if (json_object_object_get_ex(obj, "memory_bytes", &limit_obj)) {
	    cfg->limits.memory_bytes = json_object_get_int64(limit_obj);
	}
	if (json_object_object_get_ex(obj, "cpu_quota", &limit_obj)) {
	    cfg->limits.cpu_quota = json_object_get_int(limit_obj);
	}
	if (json_object_object_get_ex(obj, "pids_max", &limit_obj)) {
	    cfg->limits.pids_max = json_object_get_int(limit_obj);
	}
    }

    /* Extract status */
    if (json_object_object_get_ex(root, "status", &obj)) {
	strncpy(cfg->status, json_object_get_string(obj),
		sizeof(cfg->status) - 1);
    }

    /* Extract exit code if present */
    if (json_object_object_get_ex(root, "exit_code", &obj)) {
	cfg->exit_code = json_object_get_int(obj);
    }

    /* Set container paths based on PID */
    snprintf(cfg->container_dir, MAX_PATH_LEN, "%s/%d",
	     DEFAULT_CONTAINER_BASE, cfg->pid);
    snprintf(cfg->config_file, MAX_PATH_LEN, "%s/config.json",
	     cfg->container_dir);

    json_object_put(root);
    return 0;
}

int config_update_status(const container_config_t *cfg, const char *status,
			 int exit_code)
{
    container_config_t tmp = *cfg;
    strncpy(tmp.status, status, sizeof(tmp.status) - 1);
    tmp.exit_code = exit_code;
    return config_save_to_file(&tmp);
}

int config_list_containers(int show_all)
{
    DIR *dir = opendir(DEFAULT_CONTAINER_BASE);
    if (!dir) {
	/* Try to create directory if it doesn't exist */
	if (mkdir(DEFAULT_CONTAINER_BASE, 0755) == 0) {
	    printf("No containers found (created directory %s)\n",
		   DEFAULT_CONTAINER_BASE);
	} else {
	    perror("Failed to open containers directory");
	}
	return -1;
    }

    printf("%-10s %-20s %-20s %-15s %-19s\n",
	   "PID", "NAME", "IMAGE", "STATUS", "CREATED");
    printf
	("--------------------------------------------------------------------------------\n");

    struct dirent *entry;
    int count = 0;

    while ((entry = readdir(dir)) != NULL) {
	if (entry->d_name[0] == '.')
	    continue;

	char config_path[MAX_PATH_LEN];
	snprintf(config_path, sizeof(config_path),
		 "%s/%s/config.json", DEFAULT_CONTAINER_BASE,
		 entry->d_name);

	container_config_t cfg;
	if (config_load_from_file(config_path, &cfg) != 0) {
	    continue;
	}

	/* Check if process is still running */
	int is_running = (kill(cfg.pid, 0) == 0);
	if (!is_running && strcmp(cfg.status, "running") == 0) {
	    strncpy(cfg.status, "stopped", sizeof(cfg.status) - 1);
	    config_save_to_file(&cfg);
	}

	/* Filter by show_all flag */
	if (!show_all && strcmp(cfg.status, "running") != 0) {
	    continue;
	}

	/* Format timestamp */
	char time_str[32];
	struct tm *tm_info = localtime(&cfg.created_at);
	strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);

	/* Extract just the image name */
	const char *image_name = strrchr(cfg.base_root, '/');
	image_name = image_name ? image_name + 1 : cfg.base_root;

	printf("%-10d %-20s %-20s %-15s %-19s\n",
	       cfg.pid, cfg.name, image_name, cfg.status, time_str);

	count++;
    }

    closedir(dir);

    if (count == 0) {
	printf("No containers found.\n");
    }

    return 0;
}

void config_init(container_config_t *cfg)
{
    memset(cfg, 0, sizeof(container_config_t));
    strncpy(cfg->container_base, DEFAULT_CONTAINER_BASE, MAX_PATH_LEN - 1);
    strncpy(cfg->cgroup_base, DEFAULT_CGROUP_BASE, MAX_PATH_LEN - 1);
    strncpy(cfg->shell, DEFAULT_SHELL, MAX_PATH_LEN - 1);
    strncpy(cfg->name, "unnamed", MAX_NAME_LEN - 1);
    strncpy(cfg->status, "init", sizeof(cfg->status) - 1);

    /* Set default resource limits */
    cfg->limits.memory_bytes = 128 * 1024 * 1024;	/* 128MB */
    cfg->limits.cpu_quota = 50000;	/* 50% CPU */
    cfg->limits.pids_max = 10;

    cfg->created_at = time(NULL);
    cfg->exit_code = -1;
}

int config_set_image(container_config_t *cfg, const char *image_name)
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
