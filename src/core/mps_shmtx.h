
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _MPS_SHMTX_H_INCLUDED_
#define _MPS_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    ngx_atomic_t   lock;
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t   wait;
#endif
} mps_shmtx_sh_t;


typedef struct {
#if (NGX_HAVE_ATOMIC_OPS)
    ngx_atomic_t  *lock;
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t  *wait;
    ngx_uint_t     semaphore;
    sem_t          sem;
#endif
#else
    ngx_fd_t       fd;
    u_char        *name;
#endif
    ngx_uint_t     spin;
} mps_shmtx_t;


ngx_int_t mps_shmtx_create(mps_shmtx_t *mtx, mps_shmtx_sh_t *addr,
    u_char *name);
void mps_shmtx_destroy(mps_shmtx_t *mtx);
ngx_uint_t mps_shmtx_trylock(mps_shmtx_t *mtx);
void mps_shmtx_lock(mps_shmtx_t *mtx);
void mps_shmtx_unlock(mps_shmtx_t *mtx);
ngx_uint_t mps_shmtx_force_unlock(mps_shmtx_t *mtx, ngx_pid_t pid);


#endif /* _MPS_SHMTX_H_INCLUDED_ */
