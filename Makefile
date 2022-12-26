
CC =	   clang
LINK =	   $(CC)
COV =      llvm-cov
PROFDATA = llvm-profdata

INCS = -Isrc -I/usr/include/luajit-2.1
WARNING_FLAGS = -Wall -Wno-unused-value -Wno-unused-function -Wno-nullability-completeness -Wno-expansion-to-defined -Werror=implicit-function-declaration -Werror=incompatible-pointer-types
COMMON_CFLAGS = $(INCS) -pipe $(WARNING_FLAGS)
COV_FLAGS = -fprofile-instr-generate -fcoverage-mapping

ATS_CFLAGS = -DMPS_ATS -O2 -fPIC $(COMMON_CFLAGS)

NGX_CFLAGS = -DMPS_NGX -O2 -fPIC $(COMMON_CFLAGS)

TEST_CFLAGS = -DMPS_LOG_STDERR -O0 -g3 -Itest/unity $(COV_FLAGS) $(COMMON_CFLAGS)

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
           src/tslog.h \
           src/tslog_ngx.h \
           src/tslog_stderr.h

SRCS = src/mps_rbtree.c \
       src/mps_shdict.c \
       src/mps_slab.c \
       src/ngx_murmurhash.c \
       src/ngx_string.c \
       src/tslog_stderr.c

UNITY_DEPS = test/unity/unity.h \
             test/unity/unity_internals.h

MPS_ATS_OBJS = objs/ats/ngx_murmurhash.o \
               objs/ats/mps_rbtree.o \
               objs/ats/mps_shdict.o \
               objs/ats/mps_slab.o \
               objs/ats/ngx_string.o

MPS_NGX_OBJS = objs/ngx/mps_rbtree.o \
               objs/ngx/mps_shdict.o \
               objs/ngx/mps_slab.o

MPS_TEST_OBJS = objs/test/ngx_murmurhash.o \
                objs/test/mps_rbtree.o \
                objs/test/mps_shdict.o \
                objs/test/mps_slab.o \
                objs/test/ngx_string.o \
                objs/test/tslog_stderr.o \
				objs/test/unity.o

SHLIBS = objs/libmps_ats_shdict.so \
         objs/libmps_ngx_shdict.so

build: $(SHLIBS)

install: $(SHLIBS)
	sudo install $(SHLIBS) /usr/lib/x86_64-linux-gnu/
	sudo install mps_ats_shdict.lua mps_ngx_shdict.lua /usr/local/share/lua/5.1/

example: objs/libmps_test_shdict.so
	LD_LIBRARY_PATH=objs luajit mps_test_shdict_ex.lua

test: objs/shdict_test
	objs/shdict_test

cov: objs/shdict_test
	LLVM_PROFILE_FILE=objs/shdict_test.profraw objs/shdict_test
	$(PROFDATA) merge -sparse objs/shdict_test.profraw -o objs/shdict_test.profdata
	$(COV) show objs/shdict_test -instr-profile=objs/shdict_test.profdata $(SRCS)

objs/shdict_test: test/main.c $(MPS_TEST_OBJS)
	$(CC) -o $@ $(TEST_CFLAGS) $^

format:
	ls src/*.[ch] test/*.[ch] | xargs clang-format -i -style=file

# build SHLIBS

objs/libmps_ats_shdict.so: $(MPS_ATS_OBJS)
	$(LINK) -o $@ $^ -shared

objs/libmps_ngx_shdict.so: $(MPS_NGX_OBJS)
	$(LINK) -o $@ $^ -shared

objs/libmps_test_shdict.so: $(MPS_TEST_OBJS)
	$(LINK) -o $@ $^ -shared

# build MPS_ATS_OBJS

objs/ats/ngx_murmurhash.o:	src/ngx_murmurhash.c $(MPS_DEPS)
	@mkdir -p objs/ats
	$(CC) -c $(ATS_CFLAGS) -o $@ $<

objs/ats/mps_rbtree.o:	src/mps_rbtree.c $(MPS_DEPS)	
	@mkdir -p objs/ats
	$(CC) -c $(ATS_CFLAGS) -o $@ $<

objs/ats/mps_shdict.o:	src/mps_shdict.c $(MPS_DEPS)
	@mkdir -p objs/ats
	$(CC) -c $(ATS_CFLAGS) -o $@ $<

objs/ats/mps_slab.o:	src/mps_slab.c $(MPS_DEPS)
	@mkdir -p objs/ats
	$(CC) -c $(ATS_CFLAGS) -o $@ $<

objs/ats/ngx_string.o:	src/ngx_string.c $(MPS_DEPS)
	@mkdir -p objs/ats
	$(CC) -c $(ATS_CFLAGS) -o $@ $<

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

# build MPS_TEST_OBJS

objs/test/ngx_murmurhash.o:	src/ngx_murmurhash.c $(MPS_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/mps_rbtree.o:	src/mps_rbtree.c $(MPS_DEPS)	
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/mps_shdict.o:	src/mps_shdict.c $(MPS_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/mps_slab.o:	src/mps_slab.c $(MPS_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/ngx_string.o:	src/ngx_string.c $(MPS_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/tslog_stderr.o:	src/tslog_stderr.c $(MPS_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

objs/test/unity.o:	test/unity/unity.c $(UNITY_DEPS)
	@mkdir -p objs/test
	$(CC) -c $(TEST_CFLAGS) -o $@ $<

clean:
	@rm -r objs
