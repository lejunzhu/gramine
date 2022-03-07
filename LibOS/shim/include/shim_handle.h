/* SPDX-License-Identifier: LGPL-3.0-or-later */
/* Copyright (C) 2014 Stony Brook University */

/*
 * Definitions of types and functions for file/handle bookkeeping.
 */

#ifndef _SHIM_HANDLE_H_
#define _SHIM_HANDLE_H_

#include <asm/fcntl.h>
#include <asm/resource.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

#include "atomic.h"  // TODO: migrate to stdatomic.h
#include "linux_socket.h"
#include "list.h"
#include "pal.h"
#include "shim_defs.h"
#include "shim_fs_mem.h"
#include "shim_lock.h"
#include "shim_pollable_event.h"
#include "shim_sync.h"
#include "shim_types.h"

/* Handle types. Many of these are used by a single filesystem. */
enum shim_handle_type {
    /* Files: */
    TYPE_CHROOT,     /* host files, used by `chroot` filesystem */
    TYPE_CHROOT_ENCRYPTED,
                     /* encrypted host files, used by `chroot_encrypted` filesystem */
    TYPE_DEV,        /* emulated devices, used by `dev` filesystem */
    TYPE_STR,        /* string-based files (with data inside handle), handled by `pseudo_*`
                      * functions */
    TYPE_PSEUDO,     /* pseudo nodes (currently directories), handled by `pseudo_*` functions */
    TYPE_TMPFS,      /* string-based files (with data inside dentry), used by `tmpfs` filesystem */
    TYPE_SYNTHETIC,  /* synthetic files, used by `synthetic` filesystem */

    /* Pipes and sockets: */
    TYPE_PIPE,       /* pipes, used by `pipe` filesystem */
    TYPE_SOCK,       /* sockets, used by `socket` filesystem */

    /* Special handles: */
    TYPE_EPOLL,      /* epoll handles, see `shim_epoll.c` */
    TYPE_EVENTFD,    /* eventfd handles, used by `eventfd` filesystem */
};

struct shim_handle;
struct shim_thread;
struct shim_vma;

enum shim_file_type {
    FILE_UNKNOWN,
    FILE_REGULAR,
    FILE_DIR,
    FILE_DEV,
    FILE_TTY,
};

#define FILE_HANDLE_DATA(hdl)  ((hdl)->info.file.data)
#define FILE_DENTRY_DATA(dent) ((struct shim_file_data*)(dent)->data)

struct shim_pipe_handle {
    bool ready_for_ops; /* true for pipes, false for FIFOs that were mknod'ed but not open'ed */
    char name[PIPE_URI_SIZE];
};

enum shim_sock_state {
    SOCK_NEW,
    SOCK_BOUND,
    SOCK_CONNECTED,
    SOCK_LISTENING,
};

/*
 * Accecss to `state`, `remote_addr`, `remote_addrlen`, `local_addr`, `local_addrlen, `last_error`,
 * `sendtimeout_us`, `receivetimeout_us`, `read_shutdown`, `write_shutdown` is protected by `lock`.
 * `ops`, `domain`, `type` and `protocol` are read-only and do not need any locking.
 * Access to `peek` struct is protected by `recv_lock`. This lock also ensures proper ordering of
 * stream reads (see the comment in `do_recvmsg` in "LibOS/shim/src/sys/shim_socket.c").
 * `pal_handle` should be accessed using atomic operations. It can be NULL. Once it's set, it cannot
 * change anymore.
 */
struct shim_sock_handle {
    struct shim_lock lock;
    struct shim_sock_ops* ops;
    PAL_HANDLE pal_handle;
    int domain;
    int type;
    int protocol;
    enum shim_sock_state state;
    struct sockaddr_storage remote_addr;
    size_t remote_addrlen;
    struct sockaddr_storage local_addr;
    size_t local_addrlen;
    struct {
        char* buf;
        size_t buf_size;
        size_t data_size;
    } peek;
    struct shim_lock recv_lock;
    unsigned int last_error;
    uint64_t sendtimeout_us;
    uint64_t receivetimeout_us;
    bool was_bound;
    bool read_shutdown;
    bool write_shutdown;
};

struct shim_dir_handle {
    /* The first two dentries are always "." and ".." */
    struct shim_dentry** dents;
    size_t count;
};

struct shim_str_handle {
    struct shim_mem_file mem;
};

DEFINE_LISTP(shim_epoll_item);
DEFINE_LISTP(shim_epoll_waiter);
struct shim_epoll_handle {
    /* For details about these fields see `shim_epoll.c`. */
    struct shim_lock lock;
    LISTP_TYPE(shim_epoll_waiter) waiters;
    LISTP_TYPE(shim_epoll_item) items;
    size_t items_count;
    size_t last_returned_index;
};

struct shim_fs;
struct shim_dentry;

struct shim_handle {
    enum shim_handle_type type;
    bool is_dir;

    REFTYPE ref_count;

    struct shim_fs* fs;
    struct shim_dentry* dentry;

    /*
     * Inode associated with this handle. Currently optional, and only for the use of underlying
     * filesystem (see `shim_inode` in `shim_fs.h`). Eventually, should replace `dentry` fields.
     *
     * This field does not change, so reading it does not require holding `lock`.
     *
     * When taking locks for both handle and inode (`hdl->lock` and `hdl->inode->lock`), you should
     * lock the *inode* first.
     */
    struct shim_inode* inode;

    /* Offset in file. Protected by `pos_lock`. */
    file_off_t pos;

    /* This list contains `shim_epoll_item` objects this handle is part of. All accesses should be
     * protected by `handle->lock`. */
    LISTP_TYPE(shim_epoll_item) epoll_items;
    size_t epoll_items_count;
    /* Only meaningful if the handle is registered in some epoll instance with `EPOLLET` semantics.
     * `false` if it already triggered an `EPOLLIN` event for the current portion of data otherwise
     * `true` and the next `epoll_wait` will consider this handle and report events for it. */
    bool needs_et_poll_in;
    /* Same as above but for `EPOLLOUT` events. */
    bool needs_et_poll_out;

    char* uri; /* PAL URI for this handle (if any). Does not change. */

    PAL_HANDLE pal_handle;

    /* Type-specific fields: when accessing, ensure that `type` field is appropriate first (at least
     * by using assert()) */
    union {
        /* (no data) */                         /* TYPE_CHROOT */
        /* (no data) */                         /* TYPE_CHROOT_ENCRYPTED */
        /* (no data) */                         /* TYPE_DEV */
        struct shim_str_handle str;             /* TYPE_STR */
        /* (no data) */                         /* TYPE_PSEUDO */
        /* (no data) */                         /* TYPE_TMPFS */
        /* (no data) */                         /* TYPE_SYNTHETIC */

        struct shim_pipe_handle pipe;           /* TYPE_PIPE */
        struct shim_sock_handle sock;           /* TYPE_SOCK */

        struct shim_epoll_handle epoll;         /* TYPE_EPOLL */
        struct { bool is_semaphore; } eventfd;  /* TYPE_EVENTFD */
    } info;

    struct shim_dir_handle dir_info;

    /* TODO: the `flags` and `acc_mode` fields contain duplicate information (the access mode).
     * Instead of `flags`, we should have a field with different name (such as `options`) that
     * contain the open flags without access mode (i.e. set it to `flags & ~O_ACCMODE`). */
    int flags; /* Linux' O_* flags */
    int acc_mode;
    struct shim_lock lock;

    /* Lock for handle position (`pos`). Intended for operations that change the position (e.g.
     * `read`, `seek` but not `pread`). This lock should be taken *before* `shim_handle.lock` and
     * `shim_inode.lock`. */
    struct shim_lock pos_lock;
};

/* allocating / manage handle */
struct shim_handle* get_new_handle(void);
void get_handle(struct shim_handle* hdl);
void put_handle(struct shim_handle* hdl);

/* Set handle to non-blocking or blocking mode. */
int set_handle_nonblocking(struct shim_handle* hdl, bool on);

/* file descriptor table */
struct shim_fd_handle {
    uint32_t vfd; /* virtual file descriptor */
    int flags;    /* file descriptor flags, only FD_CLOEXEC */

    struct shim_handle* handle;
};

struct shim_handle_map {
    /* the top of created file descriptors */
    uint32_t fd_size;
    uint32_t fd_top;

    /* refrence count and lock */
    REFTYPE ref_count;
    struct shim_lock lock;

    /* An array of file descriptor belong to this mapping */
    struct shim_fd_handle** map;
};

/* allocating file descriptors */
#define FD_NULL                     UINT32_MAX
#define HANDLE_ALLOCATED(fd_handle) ((fd_handle) && (fd_handle)->vfd != FD_NULL)

struct shim_handle* __get_fd_handle(uint32_t fd, int* flags, struct shim_handle_map* map);
struct shim_handle* get_fd_handle(uint32_t fd, int* flags, struct shim_handle_map* map);

/*!
 * \brief Assign new fd to a handle.
 *
 * \param hdl         A handle to be mapped to the new fd.
 * \param flags       Flags assigned to new shim_fd_handle.
 * \param handle_map  Handle map to be used. If NULL is passed, current thread's handle map is used.
 *
 * Creates mapping for the given handle to a new file descriptor which is then returned.
 * Uses the lowest, non-negative available number for the new fd.
 */
int set_new_fd_handle(struct shim_handle* hdl, int fd_flags, struct shim_handle_map* map);
int set_new_fd_handle_by_fd(uint32_t fd, struct shim_handle* hdl, int fd_flags,
                            struct shim_handle_map* map);
int set_new_fd_handle_above_fd(uint32_t fd, struct shim_handle* hdl, int fd_flags,
                               struct shim_handle_map* map);
struct shim_handle* __detach_fd_handle(struct shim_fd_handle* fd, int* flags,
                                       struct shim_handle_map* map);
struct shim_handle* detach_fd_handle(uint32_t fd, int* flags, struct shim_handle_map* map);

/* manage handle mapping */
int dup_handle_map(struct shim_handle_map** new_map, struct shim_handle_map* old_map);
void get_handle_map(struct shim_handle_map* map);
void put_handle_map(struct shim_handle_map* map);
int walk_handle_map(int (*callback)(struct shim_fd_handle*, struct shim_handle_map*),
                    struct shim_handle_map* map);

int init_handle(void);
int init_important_handles(void);

int open_executable(struct shim_handle* hdl, const char* path);

int get_file_size(struct shim_handle* file, uint64_t* size);

ssize_t do_handle_read(struct shim_handle* hdl, void* buf, size_t count);
ssize_t do_handle_write(struct shim_handle* hdl, const void* buf, size_t count);

#endif /* _SHIM_HANDLE_H_ */
