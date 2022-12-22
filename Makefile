CC =	cc
CFLAGS = -fPIC -I/usr/include/luajit-2.1  -pipe  -O -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g  -DNDK_SET_VAR -DNDK_SET_VAR -DNDK_UPSTREAM_LIST
LINK =	$(CC)


MPS_DEPS = src/core/ngx_config.h \
	       src/core/ngx_core.h \
	       src/core/ngx_log.h \
	       src/core/ngx_string.h \
	       src/core/ngx_crc32.h \
	       src/os/unix/ngx_linux_config.h \
	       src/core/ngx_auto_config.h \
	       src/core/mps_shdict.h \
	       src/core/mps_rbtree.h \
	       src/core/mps_queue.h \
	       src/core/mps_slab.h


CORE_INCS = -I src/core \
            -I src/os/unix


ALL_INCS = $(CORE_INCS) \
           -I src/api


MPS_OBJS = objs/src/core/mps_shdict.o \
           objs/src/core/mps_rbtree.o \
		   objs/src/core/mps_slab.o \
           objs/src/core/ngx_crc32.o \
           objs/src/core/ngx_string.o \
           objs/src/os/unix/ngx_global_vars.o


dll: objs/libmps_shdict.so


test: dll
	env LD_LIBRARY_PATH=objs luajit mps_shdict_ex.lua


objs/libmps_shdict.so: $(MPS_OBJS)
	$(LINK) -o objs/libmps_shdict.so \
	$(MPS_OBJS) \
	-L/usr/lib/x86_64-linux-gnu \
	-shared


objs/src/core/mps_shdict.o:	$(MPS_DEPS) \
	src/core/mps_shdict.c
	@mkdir -p objs/src/core
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/mps_shdict.o \
		src/core/mps_shdict.c


objs/src/core/mps_slab.o:	$(MPS_DEPS) \
	src/core/mps_slab.c
	@mkdir -p objs/src/core
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/mps_slab.o \
		src/core/mps_slab.c


objs/src/core/mps_rbtree.o:	$(MPS_DEPS) \
	src/core/mps_rbtree.c
	@mkdir -p objs/src/core
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/mps_rbtree.o \
		src/core/mps_rbtree.c


objs/src/core/ngx_crc32.o:	$(MPS_DEPS) \
	src/core/ngx_crc32.c
	@mkdir -p objs/src/core
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_crc32.o \
		src/core/ngx_crc32.c


objs/src/core/ngx_string.o:	$(MPS_DEPS) \
	src/core/ngx_string.c
	@mkdir -p objs/src/core
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/core/ngx_string.o \
		src/core/ngx_string.c


objs/src/os/unix/ngx_global_vars.o:	$(MPS_DEPS) \
	src/os/unix/ngx_global_vars.c
	@mkdir -p objs/src/os/unix
	$(CC) -c $(CFLAGS) $(CORE_INCS) \
		-o objs/src/os/unix/ngx_global_vars.o \
		src/os/unix/ngx_global_vars.c

clean:
	@rm -r objs
