#ifndef CONFIG_H
#define CONFIG_H

#include <sys/types.h>

#define MAX_PATH_LEN 512
#define MAX_NAME_LEN 64

/* Container configuration 
 * 
 * Image lifecycle:
 * 1. Images are stored as compressed tarballs: /var/sandbox/basefs/<name>.tar.gz
 * 2. On first use, images are extracted to: /var/sandbox/cache/<name>/
 * 3. Cached extractions are reused across all containers
 * 4. base_root points to the cached extraction path
 */
typedef struct {
    char base_root[MAX_PATH_LEN];	/* lowerdir (cached extracted image) */
    char container_base[MAX_PATH_LEN];	/* per-container dirs */
    char cgroup_base[MAX_PATH_LEN];	/* cgroup path */
    char shell[MAX_PATH_LEN];	/* shell to execute */
    char name[MAX_NAME_LEN];	/* container name */
    pid_t pid;			/* container PID (set at runtime) */

    time_t created_at;
    char status[32]; // "running", "stopped", "exited"
    int exit_code;
    
    // Resource limits
    struct {
        size_t memory_bytes;
        int cpu_quota;
        int pids_max;
    } limits;

    char container_dir[MAX_PATH_LEN];
    char config_file[MAX_NAME_LEN];
} container_config_t;

/* Initialize config with defaults */
void config_init(container_config_t * cfg);

/* Set image (validates existence) */
int config_set_image(container_config_t * cfg, const char *image_name);

/* List available images */
void config_list_images(const char *images_dir);

/* Save to config json */
int config_save_to_file(const container_config_t *cfg);

/* Load from config json */
int config_load_from_file(const char *config_path, container_config_t *cfg);

/* Update config json */
int config_update_status(const container_config_t *cfg, const char *status);

/* List container configs */
int config_list_containers(int show_all);  // 0=running only, 1=all

#endif				/* CONFIG_H */
