#ifndef BBOX_H_INCLUDED
#define BBOX_H_INCLUDED

#define BBOX_ERR_INVOCATION 254
#define BBOX_ERR_RUNTIME    255

/* Config */

#define BBOX_DO_MOUNT_DEV  0x01
#define BBOX_DO_MOUNT_PROC 0x02
#define BBOX_DO_MOUNT_SYS  0x04
#define BBOX_DO_MOUNT_HOME 0x08
#define BBOX_DO_MOUNT_ALL  0x0F

#define BBOX_DO_COPY_FILES 0x10
#define BBOX_DO_ISOLATE    0x20

#define BBOX_GROUP_NAME "build-box"
#define BBOX_VAR_LIB "/var/lib/build-box"
#define BBOX_USER_DIR_TEMPLATE BBOX_VAR_LIB"/users/%lu"

#include <sys/types.h>

typedef struct {
    char *target_dir;
    char *home_dir;
    char *chroot_home_dir;
    char *user_name;
    unsigned int config_bits;
} bbox_conf_t;

bbox_conf_t *bbox_config_new();

int bbox_config_set_target_dir(bbox_conf_t *conf, const char *path);
char *bbox_config_get_target_dir(const bbox_conf_t *conf);

int bbox_config_set_home_dir(bbox_conf_t *conf, const char *path);
char *bbox_config_get_home_dir(const bbox_conf_t *conf);

char *bbox_config_get_chroot_home_dir(const bbox_conf_t *conf);
char *bbox_config_get_user_name(const bbox_conf_t *conf);

void bbox_config_clear_mount(bbox_conf_t *conf);

void bbox_config_set_mount_all(bbox_conf_t *conf);
void bbox_config_set_mount_dev(bbox_conf_t *conf);
void bbox_config_set_mount_proc(bbox_conf_t *conf);
void bbox_config_set_mount_sys(bbox_conf_t *conf);
void bbox_config_set_mount_home(bbox_conf_t *conf);

void bbox_config_unset_mount_dev(bbox_conf_t *conf);
void bbox_config_unset_mount_proc(bbox_conf_t *conf);
void bbox_config_unset_mount_sys(bbox_conf_t *conf);
void bbox_config_unset_mount_home(bbox_conf_t *conf);

unsigned int bbox_config_get_mount_any(const bbox_conf_t *conf);
unsigned int bbox_config_get_mount_dev(const bbox_conf_t *conf);
unsigned int bbox_config_get_mount_proc(const bbox_conf_t *conf);
unsigned int bbox_config_get_mount_sys(const bbox_conf_t *conf);
unsigned int bbox_config_get_mount_home(const bbox_conf_t *conf);

void bbox_config_disable_file_updates(bbox_conf_t *conf);
void bbox_config_enable_file_updates(bbox_conf_t *conf);

void bbox_config_set_isolation(bbox_conf_t *conf);
void bbox_config_unset_isolation(bbox_conf_t *conf);
unsigned int bbox_config_get_isolation(const bbox_conf_t *conf);

unsigned int bbox_config_do_file_updates(const bbox_conf_t *conf);
void bbox_config_free(bbox_conf_t *conf);

/* Option parsing */

/*
 * Which kind a command's parser is decides its optstring. A dispatching
 * parser hands the rest of the line to something else -- "run" hands it
 * to a shell -- so it must stop at the first non-option, or it reads
 * options that were never its to read. A leaf parser owns the whole line
 * and permutes, so an option may follow an operand: "mount t -m dev".
 * Every parser starts with bbox_getopt_begin(), which is where the two
 * libcs are brought into line.
 */
#define BBOX_OPTS_DISPATCH(s) "+" s
#define BBOX_OPTS_LEAF(s) s

void bbox_getopt_begin();

/* Utilities */

void bbox_path_join(char **buf_ptr, const char *base, const char *sub,
        size_t *n_ptr);
int bbox_copy_file(const char *src, const char *dst);
void bbox_perror(const char *lead, const char *msg, ...);
void bbox_pwarning(const char *lead, const char *msg, ...);
int bbox_login_sh_chrooted(char *sys_root, char *home_dir);
int bbox_runas_user_chrooted(const char *sys_root, int argc,
        char * const argv[], const bbox_conf_t *conf);
void bbox_update_chroot_dynamic_config(const char *sys_root,
        const bbox_conf_t *conf);
void bbox_sanitize_environment();

int bbox_lower_privileges();
int bbox_raise_privileges();
int bbox_drop_privileges();
int bbox_no_new_privs();
int bbox_reset_supplementary_groups();

int bbox_check_user_in_group_build_box();
int bbox_isdir_and_owned_by(const char *module, const char *dir, uid_t uid);
int bbox_open_dir_owned_by(const char *module, const char *dir, uid_t uid);
int bbox_lock_dir(const char *module, const char *dir);
long bbox_fd_mount_id(const char *module, int fd);
int bbox_is_mount_point_at(const char *module, int dir_fd, const char *name);
int bbox_validate_entry_name(const char *module, const char *name);
int bbox_mkdir_p(const char *module, const char *path);
int bbox_sysroot_mkdir_p(const char *module, const char *sysroot,
        const char *path);
int bbox_try_fix_pkg_cache_symlink(const char *module,
        const char *chroot_home);

char *bbox_get_user_dir(uid_t uid, size_t *n_ptr);
int validate_target_name(const char *module, const char *target_name);

/* Mounting */

int bbox_mount_any(const bbox_conf_t *conf, const char *sys_root);
int bbox_mount_bind(const char *sys_root, const char *source,
        const char *parent_relpath, const char *name, int recursive,
        unsigned long remount_flags);
int bbox_mount_special(const char *sys_root, const char *parent_relpath,
        const char *name, const char *filesystemtype);
int bbox_umount_unbind(const char *sys_root, const char *parent_relpath,
        const char *name);

/* Setup */

int bbox_init_user_directory();

/* Commands */

int bbox_init(int argc, char * const argv[]);
int bbox_login(int argc, char * const argv[]);
int bbox_run(int argc, char * const argv[]);
int bbox_run_command_index(int argc, char * const argv[], int index);
int bbox_mount(int argc, char * const argv[]);
int bbox_umount(int argc, char * const argv[]);

#endif
