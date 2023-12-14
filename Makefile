include Makefile.config

LDFLAGS=-lsspfd -lssmem -lrt -m64 -lm  -pthread
CFLAGS=-Iinclude/ -g -L/usr/local/lib -Llib
INCLUDE=-I/usr/local/include
LIB=/usr/local/lib/libpapi.a
export LD_LIBRARY_PATH=lib:$LD_LIBRARY_PATH
TARGETS=$(addprefix lib, $(ALGORITHMS))
DIR=$(addprefix obj/, $(ALGORITHMS))
BINDIR=bin
SOS=$(TARGETS:=.so)
SHS=$(TARGETS:=.sh)
export COND_VAR=0

.PRECIOUS: %.o
.SECONDARY: $(OBJS)
.PHONY: all clean format

BIN=  bench_block  htll_bench_block 

BINPATH=$(addprefix $(BINDIR)/, $(BIN))

all: $(BINDIR) $(DIR) include/topology.h $(SOS) $(SHS) $(BINPATH)

no_cond_var: COND_VAR=0
no_cond_var: all


%.so: obj/
	mkdir -p lib/
	echo $@
	$(MAKE) -C src/ ../lib/$@

obj/:
	mkdir -p $@

$(DIR):
	mkdir -p $@

$(BINDIR):
	mkdir -p $@

$(SHS): src/liblock.in
	cat $< | sed -e "s/@abs_top_srcdir@/$$(echo $$(cd .; pwd) | sed -e 's/\([\/&]\)/\\\1/g')/g" > $@
	sed -i "s/@lib@/$$(basename $@ .sh).so/g" $@
	chmod a+x $@

include/topology.h: include/topology.in
	cat $< | sed -e "s/@nodes@/$$(numactl -H | head -1 | cut -f 2 -d' ')/g" > $@
	sed -i "s/@cpus@/$$(nproc)/g" $@
	sed -i "s/@cachelinesize@/128/g" $@  
	sed -i "s/@pagesize@/$$(getconf PAGESIZE)/g" $@
	sed -i 's#@cpufreq@#'$$(cat /proc/cpuinfo | grep MHz | head -1 | awk '{ x = $$4/1000; printf("%0.2g", x); }')'#g' $@
	chmod a+x $@

# micro-benchmarks
$(BINDIR)/bench: bench/bench.c $(DIR) $(SOS)
	gcc  bench/bench.c -lpapi -pthread -O3 -Iinclude/ -L./lib  -g  -o $(BINDIR)/bench

$(BINDIR)/bench_block: bench/bench_block.c $(DIR) $(SOS)
	gcc  bench/bench_block.c -lpapi -pthread -O3 -Iinclude/ -L./lib  -g  -o $(BINDIR)/bench_block

$(BINDIR)/htll_bench_block: bench/bench_block.c $(DIR) $(SOS)
	gcc  bench/bench_block.c -lpapi -pthread -O3 -Iinclude/ -L./lib  -DLIBHTLL_INTERFACE -g  -lhtll_original -o $(BINDIR)/htll_bench_block


$(BINDIR)/check: bench/check.c $(DIR) $(SOS)
	gcc bench/check.c -lpapi -pthread -O3 -Iinclude/  -L./lib  -g -o  $(BINDIR)/check

clean:
	rm -rf lib/ obj/ $(SHS) $(BINPATH) tail_*

