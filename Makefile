CC =	cc
CFLAGS = -fPIC -I/usr/include/luajit-2.1  -pipe  -O -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g  -DNDK_SET_VAR -DNDK_SET_VAR -DNDK_UPSTREAM_LIST
LINK =	$(CC)


CORE_DEPS = src/core/nginx.h \
	src/core/ngx_config.h \
	src/core/ngx_core.h \
	src/core/ngx_log.h \
	src/core/ngx_palloc.h \
	src/core/ngx_array.h \
	src/core/ngx_list.h \
	src/core/ngx_queue.h \
	src/core/ngx_string.h \
	src/core/ngx_crc32.h \
	src/core/ngx_rbtree.h \
	src/core/ngx_slab.h \
	src/core/ngx_times.h \
	src/core/ngx_shmtx.h \
	src/core/ngx_cycle.h \
	src/os/unix/ngx_time.h \
	src/os/unix/ngx_errno.h \
	src/os/unix/ngx_alloc.h \
	src/os/unix/ngx_shmem.h \
	src/os/unix/ngx_process.h \
	src/os/unix/ngx_setaffinity.h \
	src/os/unix/ngx_setproctitle.h \
	src/os/unix/ngx_atomic.h \
	src/os/unix/ngx_thread.h \
	src/os/unix/ngx_socket.h \
	src/os/unix/ngx_os.h \
	src/os/unix/ngx_user.h \
	src/os/unix/ngx_linux_config.h \
	src/os/unix/ngx_linux.h \
	src/core/ngx_auto_config.h


CORE_INCS = -I src/core \
            -I src/os/unix


ALL_INCS = $(CORE_INCS) \
           -I src/api


OBJS = objs/src/ngx_http_lua_shdict.o \
       objs/src/core/ngx_array.o \
       objs/src/core/ngx_crc32.o \
       objs/src/core/ngx_list.o \
       objs/src/core/ngx_palloc.o \
       objs/src/core/ngx_queue.o \
       objs/src/core/ngx_rbtree.o \
       objs/src/core/ngx_slab.o \
       objs/src/core/ngx_times.o \
       objs/src/core/ngx_shmtx.o \
       objs/src/core/ngx_string.o \
       objs/src/os/unix/ngx_time.o \
       objs/src/os/unix/ngx_errno.o \
       objs/src/os/unix/ngx_alloc.o \
       objs/src/os/unix/ngx_shmem.o \
       objs/src/os/unix/ngx_stubs.o \
       objs/src/os/unix/ngx_global_vars.o

MPS_OBJS = objs/src/core/mps_slab.o

objs/libmps_slab.so: $(MPS_OBJS)
	$(LINK) -o objs/libmps_slab.so \
	$(MPS_OBJS) \
	-L/usr/lib/x86_64-linux-gnu \
	-shared

objs/src/core/mps_slab.o:	$(CORE_DEPS) \
	src/core/mps_slab.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/mps_slab.o \
		src/core/mps_slab.c


all: objs/libats_ngx_http_lua_shdict.so objs/ats_ngx_http_lua_shdict.so

objs/libats_ngx_http_lua_shdict.so: $(OBJS)
	$(LINK) -o objs/libats_ngx_http_lua_shdict.so \
	$(OBJS) \
	-L/usr/lib/x86_64-linux-gnu \
	-shared


objs/ats_ngx_http_lua_shdict.so: $(OBJS)
	$(LINK) -o objs/ats_ngx_http_lua_shdict.so \
	$(OBJS) \
	-L/usr/lib/x86_64-linux-gnu \
	-shared


objs/src/ngx_http_lua_shdict.o:	$(CORE_DEPS) \
	src/ngx_http_lua_shdict.c
	$(CC) -c $(CFLAGS) $(ALL_INCS) \
		-o objs/src/ngx_http_lua_shdict.o \
		src/ngx_http_lua_shdict.c


objs/src/core/ngx_array.o:	$(CORE_DEPS) \
	src/core/ngx_array.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_array.o \
		src/core/ngx_array.c


objs/src/core/ngx_crc32.o:	$(CORE_DEPS) \
	src/core/ngx_crc32.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_crc32.o \
		src/core/ngx_crc32.c


objs/src/core/ngx_list.o:	$(CORE_DEPS) \
	src/core/ngx_list.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_list.o \
		src/core/ngx_list.c


objs/src/core/ngx_palloc.o:	$(CORE_DEPS) \
	src/core/ngx_palloc.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_palloc.o \
		src/core/ngx_palloc.c


objs/src/core/ngx_queue.o:	$(CORE_DEPS) \
	src/core/ngx_queue.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_queue.o \
		src/core/ngx_queue.c


objs/src/core/ngx_rbtree.o:	$(CORE_DEPS) \
	src/core/ngx_rbtree.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_rbtree.o \
		src/core/ngx_rbtree.c


objs/src/core/ngx_slab.o:	$(CORE_DEPS) \
	src/core/ngx_slab.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_slab.o \
		src/core/ngx_slab.c


objs/src/core/ngx_times.o:	$(CORE_DEPS) \
	src/core/ngx_times.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_times.o \
		src/core/ngx_times.c


objs/src/core/ngx_shmtx.o:	$(CORE_DEPS) \
	src/core/ngx_shmtx.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_shmtx.o \
		src/core/ngx_shmtx.c


objs/src/core/ngx_string.o:	$(CORE_DEPS) \
	src/core/ngx_string.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_string.o \
		src/core/ngx_string.c


objs/src/os/unix/ngx_time.o:	$(CORE_DEPS) \
	src/os/unix/ngx_time.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_time.o \
		src/os/unix/ngx_time.c


objs/src/os/unix/ngx_errno.o:	$(CORE_DEPS) \
	src/os/unix/ngx_errno.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_errno.o \
		src/os/unix/ngx_errno.c


objs/src/os/unix/ngx_alloc.o:	$(CORE_DEPS) \
	src/os/unix/ngx_alloc.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_alloc.o \
		src/os/unix/ngx_alloc.c


objs/src/os/unix/ngx_shmem.o:	$(CORE_DEPS) \
	src/os/unix/ngx_shmem.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_shmem.o \
		src/os/unix/ngx_shmem.c


objs/src/os/unix/ngx_stubs.o:	$(CORE_DEPS) \
	src/os/unix/ngx_stubs.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_stubs.o \
		src/os/unix/ngx_stubs.c


objs/src/os/unix/ngx_global_vars.o:	$(CORE_DEPS) \
	src/os/unix/ngx_global_vars.c
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_global_vars.o \
		src/os/unix/ngx_global_vars.c


clean:
	@rm -f $(OBJS)
