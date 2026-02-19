# Makefile — Distributed Hashcash PoW System
# CS740 Assignment — vamsikunal/join_my_ass-ignment
#
# Usage:
#   make          — build coordinator and worker (Linux, generic)
#   make fast     — build with native CPU optimizations
#   make clean    — remove binaries and object files

CC       = gcc
# -D_GNU_SOURCE is required for usleep() on glibc with strict POSIX source
CFLAGS   = -O3 -funroll-loops -pthread -D_GNU_SOURCE -DREGEXP_POSIX -DMONOLITHIC \
           -Wall -Wno-unused-result -Wno-format-truncation -Wno-stringop-truncation
CFLAGS_FAST = -O3 -funroll-loops -march=native -pthread -D_GNU_SOURCE \
              -DREGEXP_POSIX -DMONOLITHIC -Wall -Wno-unused-result \
              -Wno-format-truncation -Wno-stringop-truncation

BINDIR   = bin

HC_DIR   = hashcash/c

# ── Hashcash library object files ─────────────────────────────
HC_OBJS  = \
	$(HC_DIR)/libhc.o              \
	$(HC_DIR)/libsha1.o            \
	$(HC_DIR)/utct.o               \
	$(HC_DIR)/sdb.o                \
	$(HC_DIR)/lock.o               \
	$(HC_DIR)/sstring.o            \
	$(HC_DIR)/random.o             \
	$(HC_DIR)/array.o              \
	$(HC_DIR)/getopt.o             \
	$(HC_DIR)/libfastmint.o        \
	$(HC_DIR)/fastmint_library.o   \
	$(HC_DIR)/fastmint_ansi_standard_1.o  \
	$(HC_DIR)/fastmint_ansi_compact_1.o   \
	$(HC_DIR)/fastmint_ansi_standard_2.o  \
	$(HC_DIR)/fastmint_ansi_compact_2.o   \
	$(HC_DIR)/fastmint_ansi_ultracompact_1.o \
	$(HC_DIR)/fastmint_mmx_standard_1.o  \
	$(HC_DIR)/fastmint_mmx_compact_1.o

# ── Common object files ────────────────────────────────────────
# stubs.o provides die_msg + AltiVec stubs needed by hashcash library
COMMON_OBJS = common/logger.o common/stubs.o

# ── Targets ───────────────────────────────────────────────────
.PHONY: all fast clean

all: $(BINDIR)/coordinator $(BINDIR)/worker

fast: CFLAGS = $(CFLAGS_FAST)
fast: $(BINDIR)/coordinator $(BINDIR)/worker

# ── Build hashcash library objects ────────────────────────────
# Compile each .c in hashcash/c/ as a standalone object.
# We suppress strict aliasing warnings from the original code.
$(HC_DIR)/%.o: $(HC_DIR)/%.c
	$(CC) $(CFLAGS) -Wno-strict-aliasing -I$(HC_DIR) -c $< -o $@

# ── MMX minters need -mmmx flag (x86 SSE/MMX intrinsics) ────────
$(HC_DIR)/fastmint_mmx_standard_1.o: $(HC_DIR)/fastmint_mmx_standard_1.c
	$(CC) $(CFLAGS) -Wno-strict-aliasing -mmmx -I$(HC_DIR) -c $< -o $@

$(HC_DIR)/fastmint_mmx_compact_1.o: $(HC_DIR)/fastmint_mmx_compact_1.c
	$(CC) $(CFLAGS) -Wno-strict-aliasing -mmmx -I$(HC_DIR) -c $< -o $@

# ── Build common objects ───────────────────────────────────────
common/%.o: common/%.c
	$(CC) $(CFLAGS) -I$(HC_DIR) -Icommon -c $< -o $@

# ── Coordinator binary ─────────────────────────────────────────
$(BINDIR)/coordinator: coordinator/coordinator.c $(COMMON_OBJS) $(HC_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -I$(HC_DIR) -Icommon \
		coordinator/coordinator.c \
		$(COMMON_OBJS) $(HC_OBJS) \
		-o $@ -lpthread

# ── Worker binary ──────────────────────────────────────────────
$(BINDIR)/worker: worker/worker.c $(COMMON_OBJS) $(HC_OBJS) | $(BINDIR)
	$(CC) $(CFLAGS) -I$(HC_DIR) -Icommon \
		worker/worker.c \
		$(COMMON_OBJS) $(HC_OBJS) \
		-o $@ -lpthread

# ── Create bin directory ───────────────────────────────────────
$(BINDIR):
	mkdir -p $(BINDIR)

# ── Clean ──────────────────────────────────────────────────────
clean:
	rm -rf $(BINDIR)
	rm -f common/*.o
	rm -f $(HC_DIR)/*.o
