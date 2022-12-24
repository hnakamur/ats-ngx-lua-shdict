INCS = -I src -I/usr/include/luajit-2.1

CC =	cc
CFLAGS = -fPIC $(INCS) -pipe -O2 -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g
LINK =	$(CC)

NGX_CFLAGS = -DMPS_NGX $(CFLAGS)

MPS_DEPS = src/mps_core.h \
           src/mps_log.h \
           src/mps_queue.h \
           src/mps_rbtree.h \
           src/mps_shdict.h \
           src/mps_slab.h \
           src/ngx_array.h \
           src/ngx_atomic.h \
           src/ngx_auto_config.h \
           src/ngx_auto_headers.h \
           src/ngx_config.h \
           src/ngx_core.h \
           src/ngx_cycle.h \
           src/ngx_errno.h \
           src/ngx_linux_config.h \
           src/ngx_log.h \
           src/ngx_murmurhash.h \
           src/ngx_queue.h \
           src/ngx_string.h \
           src/tslog.h

MPS_ATS_OBJS = objs/ngx_murmurhash.o \
               objs/mps_rbtree.o \
               objs/mps_shdict.o \
               objs/mps_slab.o \
               objs/ngx_string.o

MPS_NGX_OBJS = objs/ngx/mps_rbtree.o \
               objs/ngx/mps_shdict.o \
               objs/ngx/mps_slab.o

SHLIBS = objs/libmps_ats_shdict.so \
         objs/libmps_ngx_shdict.so

build: $(SHLIBS)

install: $(SHLIBS)
	sudo install $(SHLIBS) /usr/lib/x86_64-linux-gnu/
	sudo install mps_ats_shdict.lua mps_ngx_shdict.lua /usr/local/share/lua/5.1/

# build SHLIBS

objs/libmps_ats_shdict.so: $(MPS_ATS_OBJS)
	$(LINK) -o $@ $^ -shared

objs/libmps_ngx_shdict.so: $(MPS_NGX_OBJS)
	$(LINK) -o $@ $^ -shared

# build MPS_OBJS

objs/ngx_murmurhash.o:	src/ngx_murmurhash.c $(MPS_DEPS)
	@mkdir -p objs
	$(CC) -c $(CFLAGS) -o $@ $<

objs/mps_rbtree.o:	src/mps_rbtree.c $(MPS_DEPS)	
	@mkdir -p objs
	$(CC) -c $(CFLAGS) -o $@ $<

objs/mps_shdict.o:	src/mps_shdict.c $(MPS_DEPS)
	@mkdir -p objs
	$(CC) -c $(CFLAGS) -o $@ $<

objs/mps_slab.o:	src/mps_slab.c $(MPS_DEPS)
		@mkdir -p objs
	$(CC) -c $(CFLAGS) -o $@ $<

objs/ngx_string.o:	src/ngx_string.c $(MPS_DEPS)
	@mkdir -p objs
	$(CC) -c $(CFLAGS) -o $@ $<

# build MPS_NGX_OBJS

objs/ngx/mps_rbtree.o:	src/mps_rbtree.c $(MPS_DEPS)	
	@mkdir -p objs/ngx
	$(CC) -c $(NGX_CFLAGS) -o $@ $<

objs/ngx/mps_shdict.o:	src/mps_shdict.c $(MPS_DEPS)	
	@mkdir -p objs/ngx
	$(CC) -c $(NGX_CFLAGS) -o $@ $<

objs/ngx/mps_slab.o:	src/mps_slab.c $(MPS_DEPS)	
	@mkdir -p objs/ngx
	$(CC) -c $(NGX_CFLAGS) -o $@ $<

clean:
	@rm -r objs
