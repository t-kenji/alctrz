/** @file       alctrz.c
 *  @brief      Chroot jail 環境を提供する.
 *
 *  @author     t-kenji <protect.2501@gmail.com>
 *  @date       2018-04-30 新規作成.
 *  @copyright  Copyright © 2018 t-kenji
 *
 *  This code is licensed under the MIT License.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for strdup, getopt */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <pty.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <linux/capability.h>
#include <linux/securebits.h>

#include <jansson.h>

#include "debug.h"
#include "collections.h"

/**
 *  バージョン情報.
 */
#ifndef MODULE_VERSION
#define MODULE_VERSION "unknown"
#endif

/**
 *  標準のディレクトリアクセス権限.
 */
#define DIR_PERM_DEF (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH | S_IXGRP | S_IXOTH)

/**
 *  標準のファイルアクセス権限.
 */
#define FILE_PERM_DEF (S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IROTH)

/**
 *  コンテキスト構造体.
 */
struct alctrz {
    /**
     *  閉じ込めるプログラムの情報.
     */
    struct prisoner {
        /**
         *  実行ユーザ / グループの情報.
         */
        struct user {
            gid_t gid;                 /**< グループ ID. */
            uid_t uid;                 /**< ユーザ ID. */
            char name[LOGIN_NAME_MAX]; /**< ユーザ名. */
        } user;

        char home_path[PATH_MAX];  /**< ホームディレクトリのパス. */
        char term[PATH_MAX];       /**< ターミナル名. */
        char shell_path[PATH_MAX]; /**< シェルのパス. */

        /**
         *  標準入出力の情報.
         */
        struct stdio {
            char path[PATH_MAX];
        } stdio;

        int argc;           /**< コマンドライン引数の数. */
        char * const *argv; /**< コマンドライン引数の文字列配列. */
        pid_t pid;          /**< プロセス ID. */
    } prisoner;

    /**
     *  jail の情報.
     */
    struct jail {
        json_t *env;                /**< jail の rootfs 構成情報. */
        char mount_point[PATH_MAX]; /**< jail を作成するパス. */
    } jail;

    bool do_attach;
    bool show_help;    /**< ヘルプを表示する. */
    bool show_version; /**< バージョンを表示する. */

    LIST bind_entries; /**< バインド登録情報. */
};

/**
 *  コンテキスト構造体の初期化子.
 */
#define ALCTRZ_INITIALIZER                       \
    (struct alctrz){                             \
        .prisoner = {                            \
            .user = {                            \
                .gid = getgid(),                 \
                .uid = getuid(),                 \
                .name = {0},                     \
            },                                   \
            .home_path = "/",                    \
            .term = {0},                         \
            .shell_path = "/bin/sh",             \
            .stdio = {                           \
                .path = {0},                     \
            },                                   \
            .argc = 0,                           \
            .argv = NULL,                        \
        },                                       \
        .jail = {                                \
            .env = NULL,                         \
            .mount_point = "/tmp/chroot-XXXXXX", \
        },                                       \
        .do_attach = false,                      \
        .show_help = false,                      \
        .show_version = false,                   \
        .bind_entries = NULL,                    \
    }

/**
 *  配列の長さを返す.
 */
#define lengthof(array) (sizeof(array)/sizeof(array[0]))

/**
 *  小さい方を返す.
 */
#define min(a, b) (((a) > (b)) ? (b) : (a))

/**
 *  標準入力のターミナル情報.
 */
static struct termios saved_term;

/**
 *  ターミナルのウィンドウサイズ.
 */
static struct winsize winsz;

/**
 *  ラムダ式マクロ.
 */
#define lambda(ret_type, ...)        \
    __extension__                    \
    ({                               \
        ret_type __fn__ __VA_ARGS__; \
        __fn__;                      \
    })

/**
 *  使用方法を表示する.
 *
 *  @param  [in]    name    モジュール名.
 */
static void print_usage(const char *name)
{
    printf("usage: %s [-hv] -c <conf-file> -u <user> [-g <group>] -- <program-path> [<program-args>]\n"
           "  -c    Specify the json format setting file.\n"
           "  -u    Specify the user-id for <program> execution.\n"
           "  -g    Specify the group-id for <program> execution.\n"
           "  -h    Only show help.\n"
           "  -v    Only show version.\n"
           "  <program-path> must be absolute path.\n",
           name);
}

/**
 *  バージョンを表示する.
 */
static void print_version(void)
{
    printf("v%s\n", MODULE_VERSION);
}

/**
 *  指定のファイル記述子に指定書式で文字列を出力する.
 *
 *  @param  [in]    fd      ファイル記述子.
 *  @param  [in]    format  文字列書式.
 *  @param  [in]    ...     書式パラメータ.
 *  @return 成功の場合は書き込んだ文字列の長さが返り,
 *          失敗の場合は -1 が返る.
 */
__attribute__((format (printf, 2, 3)))
static int fdprintf(int fd, const char *format, ...)
{
    int length = -1;
    char *buf = malloc(BUFSIZ);
    if (buf != NULL) {
        va_list ap;
        va_start(ap, format);
        length = vsnprintf(buf, BUFSIZ, format, ap);
        va_end(ap);
        length = write(fd, buf, min(length, BUFSIZ));
    }
    free(buf);
    return length;
}

/**
 *  capability を文字列から数値に変換する.
 *
 *  @param  [in]    name    capability 名称.
 *  @return 変換出来る場合は capability の数値が返り,
 *          失敗の場合は -1 が返る.
 */
static int capability_to_int(const char *name)
{
    static const struct {
        const char *name;
        int value;
    } conv_table[] = {
#define CAP_CONV_TABLE_SETTER(name) {#name, name}
        CAP_CONV_TABLE_SETTER(CAP_CHOWN),
        CAP_CONV_TABLE_SETTER(CAP_DAC_OVERRIDE),
        CAP_CONV_TABLE_SETTER(CAP_DAC_READ_SEARCH),
        CAP_CONV_TABLE_SETTER(CAP_FOWNER),
        CAP_CONV_TABLE_SETTER(CAP_FSETID),
        CAP_CONV_TABLE_SETTER(CAP_KILL),
        CAP_CONV_TABLE_SETTER(CAP_SETGID),
        CAP_CONV_TABLE_SETTER(CAP_SETUID),
        CAP_CONV_TABLE_SETTER(CAP_SETPCAP),
        CAP_CONV_TABLE_SETTER(CAP_LINUX_IMMUTABLE),
        CAP_CONV_TABLE_SETTER(CAP_NET_BIND_SERVICE),
        CAP_CONV_TABLE_SETTER(CAP_NET_BROADCAST),
        CAP_CONV_TABLE_SETTER(CAP_NET_ADMIN),
        CAP_CONV_TABLE_SETTER(CAP_NET_RAW),
        CAP_CONV_TABLE_SETTER(CAP_IPC_LOCK),
        CAP_CONV_TABLE_SETTER(CAP_IPC_OWNER),
        CAP_CONV_TABLE_SETTER(CAP_SYS_MODULE),
        CAP_CONV_TABLE_SETTER(CAP_SYS_RAWIO),
        CAP_CONV_TABLE_SETTER(CAP_SYS_CHROOT),
        CAP_CONV_TABLE_SETTER(CAP_SYS_PTRACE),
        CAP_CONV_TABLE_SETTER(CAP_SYS_PACCT),
        CAP_CONV_TABLE_SETTER(CAP_SYS_ADMIN),
        CAP_CONV_TABLE_SETTER(CAP_SYS_BOOT),
        CAP_CONV_TABLE_SETTER(CAP_SYS_NICE),
        CAP_CONV_TABLE_SETTER(CAP_SYS_RESOURCE),
        CAP_CONV_TABLE_SETTER(CAP_SYS_TIME),
        CAP_CONV_TABLE_SETTER(CAP_SYS_TTY_CONFIG),
        CAP_CONV_TABLE_SETTER(CAP_MKNOD),
        CAP_CONV_TABLE_SETTER(CAP_LEASE),
        CAP_CONV_TABLE_SETTER(CAP_AUDIT_WRITE),
        CAP_CONV_TABLE_SETTER(CAP_AUDIT_CONTROL),
        CAP_CONV_TABLE_SETTER(CAP_SETFCAP),
        CAP_CONV_TABLE_SETTER(CAP_MAC_OVERRIDE),
        CAP_CONV_TABLE_SETTER(CAP_MAC_ADMIN),
        CAP_CONV_TABLE_SETTER(CAP_SYSLOG),
        CAP_CONV_TABLE_SETTER(CAP_WAKE_ALARM)
#undef CAP_CONV_TABLE_SETTER
    };

    for (size_t i = 0; i < lengthof(conv_table); ++i) {
        if (strcmp(conv_table[i].name, name) == 0) {
            return conv_table[i].value;
        }
    }

    return -1;
}

/**
 *  ファイル記述子のブロッキングを設定する.
 *
 *  @param  [in]    fd      ファイル記述子.
 *  @param  [in]    enabled ブロッキングするか.
 *  @return 成功時は 0 が返り, 失敗時は -1 が返る.
 */
static int set_blocking(int fd, bool enabled)
{
    int mode = fcntl(fd, F_GETFL);
    if (mode == -1) {
        DEBUG("fcntl: %s", strerror(errno));
        return -1;
    }
    if (enabled) {
        mode &= ~O_NONBLOCK;
    } else {
        mode |= O_NONBLOCK;
    }
    if (fcntl(fd, F_SETFL, mode) != 0) {
        DEBUG("fcntl: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 *  epoll 監視の監視対象に指定のファイル記述子を追加する.
 *
 *  @param  [in]    epfd    epoll ファイル記述子.
 *  @param  [in]    events  監視対象のイベント.
 *  @param  [in]    fd      監視対象のファイル記述子.
 *  @returm 成功時は 0 が返り, 失敗時は -1 が返る.
 */
static int epoll_add_fd(int epfd, uint32_t events, int fd)
{
    struct epoll_event ev;
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        return -1;
    }
    return 0;
}

/**
 *  指定のファイル記述子に対するイベントを待つ.
 *
 *  イベント発生時は対応するイベントハンドラを呼び出す.
 *
 *  @param  [in]    fds         ファイル記述子配列.
 *  @param  [in]    handlers    イベントハンドラ配列.
 *  @param  [in]    count       配列長.
 *  @param  [in]    events      監視対象のイベント.
 *  @return 成功時は 0 が返り, 失敗時は -1 が返る.
 */
static int wait_for_event(int fds[],
                          bool (*handlers[])(uint32_t),
                          int count,
                          uint32_t events)
{
    sigset_t mask, saved_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, &saved_mask);
    int sigfd = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (sigfd < 0) {
        DEBUG("signalfd: %s", strerror(errno));
        return -1;
    }

    int epfd = epoll_create1(0);
    if (epfd < 0) {
        DEBUG("epoll_create1: %s", strerror(errno));
        close(epfd);
        return -1;
    }
    if (epoll_add_fd(epfd, EPOLLIN, sigfd) != 0) {
        DEBUG("epoll_add_fd: %s", strerror(errno));
        close(epfd);
        close(sigfd);
        return -1;
    }
    for (int i = 0; i < count; ++i) {
        if (epoll_add_fd(epfd, events, fds[i]) != 0) {
            DEBUG("epoll_add_fd: %s", strerror(errno));
            close(epfd);
            close(sigfd);
            return -1;
        }
    }

    bool be_cont = true;
    do {
        struct epoll_event evs[10];
        int nevs = epoll_wait(epfd, evs, lengthof(evs), -1);
        if (nevs < 0) {
            DEBUG("epoll_wait: %s", strerror(errno));
            be_cont = false;
        } else {
            for (int i = 0; i < nevs; ++i) {
                if (evs[i].data.fd == sigfd) {
                    if (evs[i].events & EPOLLIN) {
                        struct signalfd_siginfo siginfo = {0};
                        if (read(sigfd, &siginfo, sizeof(siginfo)) > 0) {
                            switch (siginfo.ssi_signo) {
                            case SIGCHLD:
                                be_cont = false;
                                break;
                            default:
                                break;
                            }
                        }
                    }
                } else {
                    for (int j = 0; j < count; ++j) {
                        if (fds[j] == evs[i].data.fd) {
                            be_cont = handlers[j](evs[i].events);
                            break;
                        }
                    }
                }
            }
        }
    } while (be_cont);

    close(epfd);
    close(sigfd);

    return 0;
}

/**
 *  所有者を指定してディレクトリを作成する.
 */
static int mkdir_with_owner(const char *pathname,
                            mode_t mode,
                            uid_t owner,
                            gid_t group)
{
    if (mkdir(pathname, mode) != 0) {
        return -1;
    }

    if (chown(pathname, owner, group) != 0) {
        return -1;
    }

    return 0;
}

/**
 *  再帰的にディレクトリを作成する.
 *
 *  @c path_only が指定された場合は, ベースディレクトリの作成まで行う.
 */
static int recursive_mkdir(const char *pathname,
                           mode_t mode,
                           uid_t owner,
                           gid_t group,
                           bool path_only)
{
    const size_t length = strlen(pathname);
    char path[PATH_MAX] = {0};

    if (length > sizeof(path) - 1) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(path, pathname, sizeof(path) - 1);

    for (char *sep = path + 1; *sep != '\0'; ++sep) {
        if (*sep == '/') {
            if (*(sep + 1) == '\0') {
                break;
            }
            *sep = '\0';
            if (mkdir_with_owner(path, DIR_PERM_DEF, owner, group) != 0) {
                if (errno != EEXIST) {
                    return -1;
                }
            }
            *sep = '/';
        }
    }

    if (!path_only && (mkdir_with_owner(path, mode, owner, group) != 0)) {
        if (errno != EEXIST) {
            return -1;
        }
    }

    return 0;
}

/**
 *  パスを生成した上で空ファイルを作成する.
 */
static int touch_with_mkpath(const char *pathname, uid_t owner, gid_t group)
{
    recursive_mkdir(pathname, DIR_PERM_DEF, owner, group, true);

    int fd = open(pathname, O_WRONLY | O_CREAT, FILE_PERM_DEF);
    if (fd < 0) {
        DEBUG("open: %s (%s)", strerror(errno), pathname);
        return -1;
    }
    close(fd);

    if (chown(pathname, owner, group) != 0) {
        DEBUG("chown: %s (%s)", strerror(errno), pathname);
        return -1;
    }

    return 0;
}

/**
 *  jail の rootfs に, 指定のデバイスファイルを作成する内部処理.
 */
static int create_rootfs_device_inner(struct alctrz *self,
                                      const char *pathname,
                                      const char *type,
                                      int major,
                                      int minor,
                                      const char *perm)
{
    if ((pathname == NULL) || (type == NULL) || (major == 0)) {
        errno = EINVAL;
        return -1;
    }

    mode_t mode = (perm != NULL) ? strtol(perm, NULL, 8) : 0666;
    if (strncmp(type, "char", 5) == 0) {
        mode |= S_IFCHR;
    } else {
        mode |= S_IFREG;
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, pathname);
    recursive_mkdir(path,
                    DIR_PERM_DEF,
                    self->prisoner.user.uid,
                    self->prisoner.user.gid,
                    true);

    int ret;
    ret = mknod(path, mode, makedev(major, minor));
    if (ret != 0) {
        DEBUG("mknod: %s (%s)", strerror(errno), path);
        return -1;
    }
    ret = chown(path, self->prisoner.user.uid, self->prisoner.user.gid);
    if (ret != 0) {
        DEBUG("chown: %s (%s)", strerror(errno), path);
        return -1;
    }

    return 0;
}

/**
 *  jail の rootfs に文字列で指定したデバイスファイルを作成する.
 */
static int create_rootfs_device_by_string(struct alctrz *self, const char *data)
{
    char *options = strdup(data);
    char *pathname = options;

    char *perm = strrchr(options, ',');
    if (perm != NULL) {
        *perm = '\0';
        ++perm;
    }

    char *minor = strrchr(options, ',');
    if (minor != NULL) {
        *minor = '\0';
        ++minor;
    }

    char *major = strrchr(options, ',');
    if (major != NULL) {
        *major = '\0';
        ++major;
    }

    char *type = strrchr(options, ',');
    if (type != NULL) {
        *type = '\0';
        ++type;
    }

    if (create_rootfs_device_inner(self,
                                   pathname,
                                   type,
                                   (major != NULL) ? atoi(major) : 0,
                                   (minor != NULL) ? atoi(minor) : 0,
                                   perm) != 0) {

        free(options);
        return -1;
    }

    free(options);

    return 0;
}

/**
 *  jail の rootfs にオブジェクトで指定したデバイスファイルを作成する.
 */
static int create_rootfs_device_by_object(struct alctrz *self, json_t *data)
{
    json_t *pathname = json_object_get(data, "pathname"),
           *type = json_object_get(data, "type"),
           *major = json_object_get(data, "major"),
           *minor = json_object_get(data, "minor"),
           *perm = json_object_get(data, "perm");

    if (!json_is_string(pathname)) {
        DEBUG("json: %s is not a string", "pathname");
        return -1;
    }
    if (!json_is_string(type)) {
        DEBUG("json: %s is not a string", "type");
        return -1;
    }
    if (!json_is_integer(major)) {
        DEBUG("json: %s is not a string", "major");
        return -1;
    }
    if (!json_is_integer(minor)) {
        DEBUG("json: %s is not a string", "minor");
        return -1;
    }
    if (!json_is_string(perm)) {
        DEBUG("json: %s is not a string", "perm");
        return -1;
    }

    if (create_rootfs_device_inner(self,
                                   json_string_value(pathname),
                                   json_string_value(type),
                                   json_integer_value(major),
                                   json_integer_value(minor),
                                   json_string_value(perm)) != 0) {

        return -1;
    }

    return 0;
}

/**
 *  jail の rootfs に, 指定のバインドを行う内部処理.
 */
static int create_rootfs_bind_inner(struct alctrz *self,
                                    const char *source,
                                    const char *target,
                                    const char *mode)
{
    char path[PATH_MAX] = {0};
    const char *target_ = (target) ?: source;
    const char *mode_ = (mode) ?: "ro";
    u_long mountflags = MS_BIND;
    struct stat status;
    int ret;

    if (strncmp(mode_, "ro", 3) == 0) {
        mountflags |= MS_RDONLY;
    }

    snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, target_);
    DEBUG("mount: %s to %s (%s)", source, path, mode_);

    ret = lstat(source, &status);
    if (ret != 0) {
        DEBUG("lstat: %s", strerror(errno));
        return -1;
    }
    if (S_ISDIR(status.st_mode)) {
        recursive_mkdir(path,
                        DIR_PERM_DEF,
                        self->prisoner.user.uid,
                        self->prisoner.user.gid,
                        false);
    } else {
        ret = touch_with_mkpath(path, self->prisoner.user.uid, self->prisoner.user.gid);
        if (ret != 0) {
            return -1;
        }
    }

    ret = mount(source, path, NULL, mountflags, NULL);
    if (ret != 0) {
        DEBUG("mount: %s", strerror(errno));
        return -1;
    }
    list_add(self->bind_entries, path);

    return 0;
}

/**
 *  jail の rootfs に文字列で指定したバインドを行う.
 */
static int create_rootfs_bind_by_string(struct alctrz *self, const char *data)
{
    char *options = strdup(data);

    char *mode = strrchr(options, ',');
    if (mode != NULL) {
        *mode = '\0';
        ++mode;
    }

    char *target = strchr(options, ':');
    if (target != NULL) {
        *target = '\0';
        ++target;
    }

    char *source = options;
    if (create_rootfs_bind_inner(self, source, target, mode) != 0) {
        free(options);
        return -1;
    }

    free(options);

    return 0;
}

/**
 *  jail の rootfs にオブジェクトで指定したバインドを行う.
 */
static int create_rootfs_bind_by_object(struct alctrz *self, json_t *data)
{
    json_t *source = json_object_get(data, "source"),
           *target = json_object_get(data, "target"),
           *mode = json_object_get(data, "mode");

    if (!json_is_string(source)) {
        DEBUG("json: %s is not a string", "source");
        return -1;
    }
    if (!json_is_string(target)) {
        DEBUG("json: %s is not a string", "target");
        return -1;
    }
    if (!json_is_string(mode)) {
        DEBUG("json: %s is not a string", "mode");
        return -1;
    }

    if (create_rootfs_bind_inner(self,
                                 json_string_value(source),
                                 json_string_value(target),
                                 json_string_value(mode)) != 0) {

        return -1;
    }

    return 0;
}

/**
 *  jail の rootfs に, 指定のパスを作成する.
 */
static int create_rootfs_path(struct alctrz *self, json_t *data)
{
    if (!json_is_string(data)) {
        return -1;
    }

    char path[PATH_MAX];
    const char *pathname = json_string_value(data);
    uid_t uid = self->prisoner.user.uid;
    gid_t gid = self->prisoner.user.gid;
    snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, pathname);
    recursive_mkdir(path, DIR_PERM_DEF, uid, gid, false);

    return 0;
}

static int create_rootfs_device(struct alctrz *self, json_t *data)
{
    if (json_is_string(data)) {
        if (create_rootfs_device_by_string(self, json_string_value(data)) != 0) {
            return -1;
        }
    } else if (json_is_object(data)) {
        if (create_rootfs_device_by_object(self, data) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

static int create_rootfs_bind(struct alctrz *self, json_t *data)
{
    if (json_is_string(data)) {
        if (create_rootfs_bind_by_string(self, json_string_value(data)) != 0) {
            return -1;
        }
    } else if (json_is_object(data)) {
        if (create_rootfs_bind_by_object(self, data) != 0) {
            return -1;
        }
    } else {
        return -1;
    }

    return 0;
}

static int try_json_object(struct alctrz *self,
                           json_t *data,
                           const char *name,
                           int (*func)(struct alctrz *self, json_t *))
{
    json_t *obj = json_object_get(data, name);
    if (!json_is_object(obj)) {
        DEBUG("json: '%s' is not an object", name);
        return -1;
    }
    if (func(self, obj) != 0) {
        DEBUG("json: failed to '%s'", name);
        return -1;
    }
    return 0;
}

static int try_json_array(struct alctrz *self,
                          json_t *data,
                          const char *name,
                          int (*func)(struct alctrz *self, json_t *))
{
    json_t *obj = json_object_get(data, name);
    if (obj != NULL) {
        if (!json_is_array(obj)) {
            DEBUG("json: '%s' is not an array", name);
            return -1;
        }
        if (func(self, obj) != 0) {
            DEBUG("json: failed to '%s'", name);
            return -1;
        }
    }
    return 0;
}

static bool try_json_boolean(struct alctrz *self,
                             json_t *data,
                             const char *name)
{
    json_t *obj = json_object_get(data, name);
    if (!json_is_boolean(obj)) {
        DEBUG("json: %s is not a boolean", name);
        return false;
    }
    return json_boolean_value(obj) == 1;
}

/**
 *  jail の rootfs に, kernel 関連の filesystem を作成する.
 */
static int build_rootfs_kernelfs(struct alctrz *self, json_t *data)
{
    bool devfs_enable = try_json_boolean(self, data, "devtmpfs"),
         procfs_enable = try_json_boolean(self, data, "procfs"),
         sysfs_enable = try_json_boolean(self, data, "sysfs");
    uid_t uid = self->prisoner.user.uid;
    gid_t gid = self->prisoner.user.gid;
    char path[PATH_MAX];

    if (devfs_enable) {
        snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, "/dev");
        recursive_mkdir(path, DIR_PERM_DEF, uid, gid, false);
        if (mount("none", path, "devtmpfs", 0, NULL) != 0) {
            DEBUG("mount: %s (%s)", strerror(errno), path);
            return -1;
        }
    }
    if (procfs_enable) {
        snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, "/proc");
        recursive_mkdir(path, DIR_PERM_DEF, uid, gid, false);
        if (mount("none", path, "proc", 0, NULL) != 0) {
            DEBUG("mount: %s (%s)", strerror(errno), path);
            return -1;
        }
    }
    if (sysfs_enable) {
        snprintf(path, sizeof(path), "%s%s", self->jail.mount_point, "/sys");
        recursive_mkdir(path, DIR_PERM_DEF, uid, gid, false);
        if (mount("none", path, "sysfs", 0, NULL) != 0) {
            DEBUG("mount: %s (%s)", strerror(errno), path);
            return -1;
        }
    }

    return 0;
}

static int build_rootfs_directory(struct alctrz *self, json_t *data)
{
    for (size_t i = 0, length = json_array_size(data); i < length; ++i) {
        json_t *item = json_array_get(data, i);

        if (create_rootfs_path(self, item) != 0) {
            DEBUG("json: failed to 'directory' %zu", i + 1);
        }
    }

    return 0;
}

static int build_rootfs_device(struct alctrz *self, json_t *data)
{
    for (size_t i = 0, length = json_array_size(data); i < length; ++i) {
        json_t *item = json_array_get(data, i);

        if (create_rootfs_device(self, item) != 0) {
            DEBUG("json: failed to 'device' %zu", i + 1);
        }
    }

    return 0;
}

static int build_rootfs_bind(struct alctrz *self, json_t *data)
{
    for (size_t i = 0, length = json_array_size(data); i < length; ++i) {
        json_t *item = json_array_get(data, i);

        if (create_rootfs_bind(self, item) != 0) {
            DEBUG("json: failed to 'bind' %zu", i + 1);
        }
    }

    return 0;
}

/**
 *  指定の設定で, jail 向けの rootfs を作成する.
 */
static int build_rootfs(struct alctrz *self)
{
    json_t *root = self->jail.env;

    if (!json_is_object(root)) {
        DEBUG("json: root is not an object");
        return -1;
    }

    if (try_json_object(self, root, "filesystem", build_rootfs_kernelfs) != 0) {
        return -1;
    }
    if (try_json_array(self, root, "directory", build_rootfs_directory) != 0) {
        return -1;
    }
    if (try_json_array(self, root, "device", build_rootfs_device) != 0) {
        return -1;
    }
    if (try_json_array(self, root, "bind", build_rootfs_bind) != 0) {
        return -1;
    }

    return 0;
}

/**
 *  指定の json ファイルを読み込む.
 */
static json_t *json_load_from_file(const char *pathname)
{
    char buf[BUFSIZ] = {0};
    int fd;
    struct stat file_stat;
    json_t *root;
    json_error_t err;
    ssize_t read_bytes;
    int ret;

    fd = open(pathname, O_RDONLY);
    if (fd < 0) {
        DEBUG("open: %s", strerror(errno));
        return NULL;
    }
    ret = fstat(fd, &file_stat);
    if (ret != 0) {
        DEBUG("fstat: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    if (file_stat.st_size > sizeof(buf) - 1) {
        errno = EFBIG;
        DEBUG("%s: %s", __func__, strerror(errno));
        close(fd);
        return NULL;
    }
    read_bytes = read(fd, buf, sizeof(buf) - 1);
    if (read_bytes < 0) {
        DEBUG("read: %s", strerror(errno));
        close(fd);
        return NULL;
    }
    close(fd);

    root = json_loads(buf, 0, &err);
    if (root == NULL) {
        DEBUG("json error on line: %d: %s", err.line, err.text);
    }

    return root;
}

/**
 *  jail の設置場所を生成する.
 */
static int create_jail(struct alctrz *self)
{
    if (mkdtemp(self->jail.mount_point) == NULL) {
        DEBUG("mkdtemp: %s", strerror(errno));
        return -1;
    }

    int ret;
    char options[64];
    snprintf(options, sizeof(options),
             "size=96m,uid=%d,gid=%d,mode=700",
             self->prisoner.user.uid, self->prisoner.user.gid);
    ret = mount("none", self->jail.mount_point, "tmpfs", 0, options);
    if (ret != 0) {
        DEBUG("mount: %s", strerror(errno));
        return -1;
    }

    return 0;
}

static int create_stdio_for_prisoner(struct alctrz *self)
{
    json_t *value = json_object_get(self->jail.env, "stdio");
    if (!json_is_string(value)) {
        DEBUG("json: 'stdio' is not an string");
        return -1;
    }

    char *uri = strdup(json_string_value(value));
    char *proto = uri;
    char *format = strstr(uri, "://");
    if (format == NULL) {
        DEBUG("json: 'stdio' is wrong format");
        free(uri);
        return -1;
    }
    *format = '\0';
    format += 3;
    if (strcmp(proto, "fifo") == 0) {
        strncpy(self->prisoner.stdio.path, format, sizeof(self->prisoner.stdio.path));
        char path[PATH_MAX];
        snprintf(path, sizeof(path), self->prisoner.stdio.path, STDIN_FILENO);
        int ret;
        ret = mkfifo(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        if ((ret != 0) && (errno != EEXIST)) {
            DEBUG("mkfifo: %s (%s)", strerror(errno), path);
            free(uri);
            return -1;
        }
        if (chown(path, self->prisoner.user.uid, self->prisoner.user.gid) != 0) {
            DEBUG("chown: %s (%s)", strerror(errno), path);
            free(uri);
            return -1;
        }
        snprintf(path, sizeof(path), self->prisoner.stdio.path, STDOUT_FILENO);
        ret = mkfifo(path, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
        if ((ret != 0) && (errno != EEXIST)) {
            DEBUG("mkfifo: %s (%s)", strerror(errno), path);
            free(uri);
            return -1;
        }
        if (chown(path, self->prisoner.user.uid, self->prisoner.user.gid) != 0) {
            DEBUG("chown: %s (%s)", strerror(errno), path);
            free(uri);
            return -1;
        }
    } else {
        DEBUG("json: 'stdio' is unknown protocol");
        free(uri);
        return -1;
    }
    free(uri);

    return 0;
}

/**
 *  指定の capability を残し, その他を落とす.
 */
static int drop_capabilities(struct alctrz *self)
{
    json_t *caps = json_object_get(self->jail.env, "keep_capability");
    if (!json_is_array(caps)) {
        DEBUG("json: keep_capability is not an array");
        return -1;
    }

    uint64_t keep_caps_bits = 0;
    for (size_t i = 0, length = json_array_size(caps); i < length; ++i) {
        json_t *name = json_array_get(caps, i);
        if (!json_is_string(name)) {
            DEBUG("json: keep_capability %zu is not a string", i + 1);
            return -1;
        }

        int capability = capability_to_int(json_string_value(name));
        if (capability < 0) {
            DEBUG("json: %s is not a capability name", json_string_value(name));
            return -1;
        }
        keep_caps_bits |= 1 << capability;
    }

    struct __user_cap_header_struct hdr = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0
    };
    struct __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3] = {{0}};
    int ret;

    /* 継承のベースにするために root の capability を取得する. */
    ret = syscall(SYS_capget, &hdr, data);
    if (ret != 0) {
        DEBUG("capget: %s", strerror(errno));
        return -1;
    }
    DEBUG("effective: %08x %08x", data[1].effective, data[0].effective);
    DEBUG("permitted: %08x %08x", data[1].permitted, data[0].permitted);
    DEBUG("inheritable: %08x %08x", data[1].inheritable, data[0].inheritable);

    /* capability bounding set から不要な権限を落とす. */
    for (int i = 0; ; ++i) {
        if ((keep_caps_bits & (1 << i)) == 0) {
            /* CAP_LAST_CAP は 35 だが, 実際には 37 まで設定されているため,
             * prctl の結果で有効な capability を判断する.
             */
            ret = prctl(PR_CAPBSET_READ, i);
            if (ret < 0) {
                break;
            }
            ret = prctl(PR_CAPBSET_DROP, i);
            if (ret != 0) {
                DEBUG("prctl: %s (%d)", strerror(errno), i);
            }

            /* ついでに file capability からも落とす. */
            data[CAP_TO_INDEX(i)].permitted &= ~CAP_TO_MASK(i);
        } else {
            ret = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, i, 0, 0);
            if (ret != 0) {
                DEBUG("prctl: %s (%d)", strerror(errno), i);
            }
        }
        data[CAP_TO_INDEX(i)].inheritable |= CAP_TO_MASK(i);
    }

    /* setuid で capability を継承させる. */
    u_long secbits = SECBIT_KEEP_CAPS | SECBIT_KEEP_CAPS_LOCKED
                   | SECBIT_NO_SETUID_FIXUP | SECBIT_NO_SETUID_FIXUP_LOCKED;
    ret = prctl(PR_SET_SECUREBITS, secbits);
    if (ret != 0) {
        DEBUG("prctl: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 *  環境変数をリセットする.
 */
static int reset_environment(struct alctrz *self)
{
    /* 全ての環境変数を削除する. */
    clearenv();

    /* 標準として HOME, SHELL, USER, TERM を環境変数に設定する. */
    setenv("HOME", self->prisoner.home_path, 0);
    setenv("SHELL", self->prisoner.shell_path, 0);
    setenv("USER", self->prisoner.user.name, 0);
    setenv("TERM", self->prisoner.term, 0);

    json_t *envs = json_object_get(self->jail.env, "environment");
    if (envs == NULL) {
        return 0;
    } else if (!json_is_object(envs)) {
        DEBUG("json: environment is not an object");
        return -1;
    }

    /* 指定された環境変数を設定する. (存在する場合は上書き) */
    const char *name;
    json_t *value;
    json_object_foreach(envs, name, value) {
        if (!json_is_string(value)) {
            DEBUG("json: environment %s is not an string", name);
            return -1;
        }
        setenv(name, json_string_value(value), 1);
    }

    /* 上書きされた可能性があるため, 標準環境変数を読み出す. */
    strncpy(self->prisoner.home_path, getenv("HOME"), sizeof(self->prisoner.home_path));
    strncpy(self->prisoner.shell_path, getenv("SHELL"), sizeof(self->prisoner.shell_path));
    strncpy(self->prisoner.user.name, getenv("USER"), sizeof(self->prisoner.user.name));
    strncpy(self->prisoner.term, getenv("TERM"), sizeof(self->prisoner.term));

    return 0;
}

/**
 *  指定名称のユーザ情報を取得する.
 *
 *  @todo エラーチェックの方法など, 今一度精査する. (セキュアコーディング観点)
 */
static int get_user_info(struct alctrz *self, const char *name)
{
    struct passwd *pw = getpwnam(name);
    if (pw == NULL) {
        DEBUG("user: failed to get user information for user %s: %s",
              name, strerror(errno));
        return -1;
    }
    if ((pw->pw_name == NULL) || (pw->pw_name[0] == '\0')) {
        DEBUG("user: got an empty username for user %s", name);
        return -1;
    }
    if (strncmp(pw->pw_name, name, LOGIN_NAME_MAX) != 0) {
        DEBUG("user: asked for user %s, got user info for %s", name, pw->pw_name);
        return -1;
    }

    self->prisoner.user.uid = pw->pw_uid;
    self->prisoner.user.gid = pw->pw_gid;
    strncpy(self->prisoner.user.name, pw->pw_name, sizeof(self->prisoner.user.name));
    strncpy(self->prisoner.home_path, pw->pw_dir, sizeof(self->prisoner.home_path));
    strncpy(self->prisoner.shell_path, pw->pw_shell, sizeof(self->prisoner.shell_path));
    strncpy(self->prisoner.term, getenv("TERM"), sizeof(self->prisoner.term));

    return 0;
}

/**
 *  指定名称のグループ ID を取得する.
 *
 *  @todo エラーチェックの方法など, 今一度精査する. (セキュアコーディング観点)
 */
static gid_t get_group_id(const char *name)
{
    struct group *gr = getgrnam(name);
    if (gr == NULL) {
        DEBUG("group: failed to get group information for group %s: %s",
              name, strerror(errno));
        return (gid_t)-1;
    }
    if ((gr->gr_name == NULL) || (gr->gr_name[0] == '\0')) {
        DEBUG("group: got an empty groupname for group %s", name);
        return (gid_t)-1;
    }
    if (strncmp(gr->gr_name, name, LOGIN_NAME_MAX) != 0) {
        DEBUG("group: asked for user %s, got group info for %s", name, gr->gr_name);
        return (gid_t)-1;
    }

    return gr->gr_gid;
}

/**
 *  引数を解析してコンテキストに設定する.
 *
 *  @remarks    `-g` が指定された場合は, 指定ユーザの所属するグループ ID に
 *              上書きする.
 */
static int parse_arguments(struct alctrz *self, int argc, char * const *argv)
{
    int opt;
    gid_t group = (gid_t)-1;
    int ret;

    while ((opt = getopt(argc, argv, "c:u:g:ahv")) != -1) {
        switch (opt) {
        case 'c':
            /** @todo パスは正規化したほうが良い. (セキュアコーディング観点) */
            self->jail.env= json_load_from_file(optarg);
            if (self->jail.env == NULL) {
                errno = EINVAL;
                return -1;
            }
            break;
        case 'u':
            ret = get_user_info(self, optarg);
            if (ret != 0) {
                errno = EINVAL;
                return -1;
            }
            break;
        case 'g':
            group = get_group_id(optarg);
            if (group == (gid_t)-1) {
                errno = EINVAL;
                return -1;
            }
            break;
        case 'a':
            self->do_attach = true;
            break;
        case 'h':
            self->show_help = true;
            return 0;
        case 'v':
            self->show_version = true;
            return 0;
        default:
            errno = EINVAL;
            return -1;
        }
    }

    if (!self->do_attach) {
        if (argc == optind) {
            errno = EINVAL;
            return -1;
        }

        self->prisoner.argc = argc - optind;
        self->prisoner.argv = &argv[optind];
        if (self->prisoner.argv[0][0] != '/') {
            errno = EINVAL;
            return -1;
        }

        if (group != (gid_t)-1) {
            self->prisoner.user.gid = group;
        }
    }

    return 0;
}

/**
 *  Alcatraz コア機能.
 *
 *  @param  [in]    self    コンテキスト.
 *  @return 正常終了の場合は, 0 が返る.
 *          エラーが発生した場合は, -1 が返る.
 */
static int alctrz(struct alctrz *self)
{
    int ret;

    setsid();

    ret = create_jail(self);
    if (ret != 0) {
        return -1;
    }

    ret = build_rootfs(self);
    if (ret != 0) {
        return -1;
    }

    int master_fd;
    self->prisoner.pid = forkpty(&master_fd, NULL, &saved_term, &winsz);
    if (self->prisoner.pid < 0) {
        return -1;
    } else if (self->prisoner.pid == 0) {
        ret = chroot(self->jail.mount_point);
        if (ret != 0) {
            DEBUG("chroot: %s (%s)", strerror(errno), self->jail.mount_point);
            exit(2);
        }
        ret = reset_environment(self);
        if (ret != 0) {
            exit(2);
        }
        ret = chdir("/");
        if (ret != 0) {
            DEBUG("chdir: %s, (/)", strerror(errno));
            exit(2);
        }
        recursive_mkdir(self->prisoner.home_path,
                        DIR_PERM_DEF,
                        self->prisoner.user.uid,
                        self->prisoner.user.gid,
                        false);

        ret = drop_capabilities(self);
        if (ret != 0) {
            exit(2);
        }

        gid_t gid = self->prisoner.user.gid;
        uid_t uid = self->prisoner.user.uid;
        const gid_t aux_gids[] = {
            gid,
        };
        ret = setgid(gid);
        if (ret != 0) {
            DEBUG("setgid: %s", strerror(errno));
            exit(2);
        }
        ret = setgroups(lengthof(aux_gids), aux_gids);
        if (ret != 0) {
            DEBUG("setgroups: %s", strerror(errno));
            exit(2);
        }
        ret = setuid(uid);
        if (ret != 0) {
            DEBUG("setuid: %s", strerror(errno));
            exit(2);
        }
        ret = chdir(self->prisoner.home_path);
        if (ret != 0) {
            DEBUG("chdir: %s (%s)", strerror(errno), self->prisoner.home_path);
            exit(2);
        }
        json_decref(self->jail.env);
        execvp(self->prisoner.argv[0], self->prisoner.argv);
        DEBUG("%s: %s", self->prisoner.argv[0], strerror(errno));
    }

    char path[PATH_MAX];
    snprintf(path, sizeof(path), self->prisoner.stdio.path, STDOUT_FILENO);
    int stdout_fd = open(path, O_WRONLY);
    if (stdout_fd < 0) {
        kill(self->prisoner.pid, SIGTERM);
        close(master_fd);
        return -1;
    }

    set_blocking(master_fd, false);

    ret = -1;
    do {
        snprintf(path, sizeof(path), self->prisoner.stdio.path, STDIN_FILENO);
        int stdin_fd = open(path, O_RDONLY | O_NONBLOCK);
        if (stdin_fd < 0) {
            fdprintf(stdout_fd, "open: %s (%s)\r\n", strerror(errno), path);
            break;
        }

        int fds[] = {
            stdin_fd,
            master_fd,
        };
        ssize_t read_len, written_len;
        char buf[BUFSIZ];
        ret = wait_for_event(
            fds,
            (bool (*[])(uint32_t)){
                lambda(bool, (uint32_t events) {
                    if (events & EPOLLIN) {
                        read_len = read(stdin_fd, buf, sizeof(buf));
                        if (read_len < 0) {
                            fdprintf(stdout_fd, "read: %s\r\n", strerror(errno));
                            return false;
                        }
                        written_len = write(master_fd, buf, read_len);
                        if (written_len < 0) {
                            fdprintf(stdout_fd, "write: %s\r\n", strerror(errno));
                            return false;
                        }
                    }
                    return true;
                }),
                lambda(bool, (uint32_t events) {
                    if (events & EPOLLIN) {
                        read_len = read(master_fd, buf, sizeof(buf));
                        if (read_len < 0) {
                            fdprintf(stdout_fd, "read: %s\r\n", strerror(errno));
                            return false;
                        }
                        written_len = write(stdout_fd, buf, read_len);
                        if (written_len < 0) {
                            fdprintf(stdout_fd, "write: %s\r\n", strerror(errno));
                            return false;
                        }
                    }
                    return true;
                }),
            },
            lengthof(fds),
            EPOLLIN | EPOLLET);

        close(stdin_fd);
    } while (0);

    kill(self->prisoner.pid, SIGTERM);
    close(master_fd);

    int status;
    ret = waitpid(self->prisoner.pid, &status, 0);
    if (ret != self->prisoner.pid) {
        fdprintf(stdout_fd, "waitpid: %s (%d)\r\n",
                 strerror(errno), self->prisoner.pid);
    } else {
        if (WIFEXITED(status)) {
            fdprintf(stdout_fd, "child %d exited with %d\r\n",
                     self->prisoner.pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fdprintf(stdout_fd, "child %d signaled by %d\r\n",
                     self->prisoner.pid, WTERMSIG(status));
        } else {
            fdprintf(stdout_fd, "child %d exited with %#x\r\n",
                     self->prisoner.pid, status);
        }
    }

    close(stdout_fd);

    return 0;
}

static int visitation(struct alctrz *self)
{
    char path[PATH_MAX];

    snprintf(path, sizeof(path), self->prisoner.stdio.path, STDIN_FILENO);
    int stdin_fd = open(path, O_RDWR);
    if (stdin_fd < 0) {
        DEBUG("open: %s (%s)", strerror(errno), path);
        return -1;
    }
    snprintf(path, sizeof(path), self->prisoner.stdio.path, STDOUT_FILENO);
    int stdout_fd = open(path, O_RDONLY | O_NONBLOCK);
    if (stdout_fd < 0) {
        DEBUG("open: %s (%s)", strerror(errno), path);
        close(stdin_fd);
        return -1;
    }

    int fds[] = {
        STDIN_FILENO,
        stdout_fd,
    };
    ssize_t read_len, written_len;
    char buf[BUFSIZ];
    int ret = wait_for_event(
        fds,
        (bool (*[])(uint32_t)){
            lambda(bool, (uint32_t events) {
                if (events & EPOLLIN) {
                    read_len = read(STDIN_FILENO, buf, sizeof(buf));
                    if (read_len < 0) {
                        DEBUG("read: %s", strerror(errno));
                        return false;
                    }
                    if (buf[0] == 0x04) {
                        fputs("^D (detached)\r\n", stdout);
                        return false;
                    }
                    written_len = write(stdin_fd, buf, read_len);
                    if (written_len < 0) {
                        DEBUG("write: %s", strerror(errno));
                        return false;
                    }
                    return true;
                }
                return false;
            }),
            lambda(bool, (uint32_t events) {
                if (events & EPOLLIN) {
                    read_len = read(stdout_fd, buf, sizeof(buf));
                    if (read_len < 0) {
                        DEBUG("read: %s", strerror(errno));
                        return false;
                    }
                    written_len = write(STDOUT_FILENO, buf, read_len);
                    if (written_len < 0) {
                        DEBUG("write: %s", strerror(errno));
                        return false;
                    }
                    return true;
                }
                return false;
            }),
        },
        lengthof(fds),
        EPOLLIN | EPOLLET);

    close(stdin_fd);
    close(stdout_fd);

    return ret;
}

static void cleanup(struct alctrz *self)
{
    int ret;

    for (ITER iter = list_iter(self->bind_entries);
         iter != NULL;
         iter = iter_next(iter)) {

        ret = umount2(iter_get_payload(iter), MNT_DETACH);
        if (ret != 0) {
            DEBUG("umount2: %s (%s)", strerror(errno), (char *)iter_get_payload(iter));
        }
    }
    ret = umount2(self->jail.mount_point, MNT_DETACH);
    if (ret != 0) {
        DEBUG("umount2: %s (%s)", strerror(errno), self->jail.mount_point);
    }
    ret = rmdir(self->jail.mount_point);
    if (ret != 0) {
        DEBUG("rmdir: %s (%s)", strerror(errno), self->jail.mount_point);
    }
    char path[PATH_MAX];
    snprintf(path, sizeof(path), self->prisoner.stdio.path, STDIN_FILENO);
    ret = unlink(path);
    if (ret != 0) {
        DEBUG("unlink: %s (%s)", strerror(errno), path);
    }
    snprintf(path, sizeof(path), self->prisoner.stdio.path, STDOUT_FILENO);
    ret = unlink(path);
    if (ret != 0) {
        DEBUG("unlink: %s (%s)", strerror(errno), path);
    }
}

/**
 *  imprisonment desc.
 *
 *  @param  [in]    self    Self context.
 *  @return Returns zero on success.
 *          On error, -1 is returned.
 */
static int imprisonment(struct alctrz *self)
{
    pid_t pid = fork();
    if (pid < 0) {
        ERROR("fork: %s", strerror(errno));
        return -1;
    } else if (pid > 0) {
        /* TODO: jail が完成するのを待ったほうが良い. */
        return 0;
    }

#if 1
    int null_fd = open("/dev/null", O_RDWR);
    if ((dup2(null_fd, STDIN_FILENO) != STDIN_FILENO)
        || (dup2(null_fd, STDOUT_FILENO) != STDOUT_FILENO)
        || (dup2(null_fd, STDERR_FILENO) != STDERR_FILENO)) {

        return -1;
    }
    close(null_fd);
#endif

    int status = alctrz(self);
    /* cleanup() の呼び出しは親のみ. */
    json_decref(self->jail.env);
    free(self);

    exit(status);
}

/**
 *  スタートアップ.
 *
 *  @param  [in]    argc    引数の数.
 *  @param  [in]    argv    引数文字列のポインタ配列.
 *  @return 正常終了の場合は, 0 が返る.
 *          エラーが発生した場合は, 1 が返る.
 */
int main(int argc, char **argv)
{
    int ret;

    struct alctrz *self = malloc(sizeof(*self));
    if (self == NULL) {
        ERROR("Internal error: %s", strerror(errno));
        exit(1);
    }

    *self = ALCTRZ_INITIALIZER;
    ret = parse_arguments(self, argc, argv);
    if (ret != 0) {
        print_usage(argv[0]);
        exit(1);
    }
    if (self->show_help) {
        print_usage(argv[0]);
        exit(0);
    }
    if (self->show_version) {
        print_version();
        exit(0);
    }

    ret = create_stdio_for_prisoner(self);
    if (ret != 0) {
        /* FIXME: リソース解放漏れ？ */
        exit(1);
    }
    tcgetattr(STDIN_FILENO, &saved_term);
    ioctl(STDIN_FILENO, TIOCGWINSZ, &winsz);

    if (!self->do_attach) {
        ret = imprisonment(self);
        if (ret != 0) {
            /* FIXME: リソース解放漏れ？ */
            exit(1);
        }
    }

    set_blocking(STDIN_FILENO, false);

    struct termios term = saved_term;
    cfmakeraw(&term);
    term.c_cc[VMIN]  = 1;
    term.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &term);

    ret = visitation(self);

    tcsetattr(STDIN_FILENO, TCSANOW, &saved_term);
    set_blocking(STDIN_FILENO, true);

    cleanup(self);
    json_decref(self->jail.env);
    free(self);

    return (ret == 0) ? 0 : 1;
}
