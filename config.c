#define _GNU_SOURCE
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#define DEFAULT_IMAGES_DIR "/var/sandbox/basefs"
#define DEFAULT_CONTAINER_BASE "/var/sandbox/containers"
#define DEFAULT_CGROUP_BASE "/sys/fs/cgroup/sandbox"
#define DEFAULT_SHELL "/bin/sh"
#define CACHE_DIR "/var/sandbox/cache"

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
