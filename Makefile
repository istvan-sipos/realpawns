CC      := i686-w64-mingw32-gcc
WINDRES := i686-w64-mingw32-windres
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -static-libgcc -std=gnu99 -fno-omit-frame-pointer
LDFLAGS := -shared -Wl,--kill-at

VERSION := $(shell cat VERSION)
comma   := ,
VER_NUM := $(subst .,$(comma),$(VERSION)),0
# windres re-invokes gcc -E -xc on the .rc, so the inner quotes need to
# survive a second shell-quoting pass.
WINDRESFLAGS := -DREALPAWNS_VER_NUM=$(VER_NUM) '-DREALPAWNS_VER_STR=\"$(VERSION)\"' -O coff

SRC_DIR := src
TARGET  := realpawns.dll
SRC     := $(addprefix $(SRC_DIR)/,realpawns.c patch.c savehook.c pawnxml.c store.c rift.c discipline.c skilltabs.c innate.c vocation.c inclination.c)
RES     := realpawns.res.o

# realpawns.dll sits next to DDDA.exe / dinput8.dll and is loaded via
# dinput8's [main] addons= line.
DDDA_DIR := /home/istvan/.wine/drive_c/DDDA
DEPLOY   := $(DDDA_DIR)/$(TARGET)

.PHONY: all clean deploy deploy-ini

all: $(TARGET)

$(RES): $(SRC_DIR)/realpawns.rc VERSION
	$(WINDRES) $(WINDRESFLAGS) -i $(SRC_DIR)/realpawns.rc -o $@

$(TARGET): $(SRC) $(wildcard $(SRC_DIR)/*.h) $(RES) VERSION
	$(CC) $(CFLAGS) -DREALPAWNS_VERSION='"$(VERSION)"' $(LDFLAGS) -o $@ $(SRC) $(RES) -luser32 -lkernel32 -lversion -Wl,-Bstatic -lz -Wl,-Bdynamic

deploy: $(TARGET)
	cp $(TARGET) "$(DEPLOY)"
	@echo "== deploy hash check =="
	@md5sum $(TARGET) "$(DEPLOY)"

# Copy the default realpawns.ini next to the DLL.  Separate from `deploy` so
# a customised ini isn't clobbered on every build.
deploy-ini: $(SRC_DIR)/realpawns.ini
	cp $(SRC_DIR)/realpawns.ini "$(DDDA_DIR)/realpawns.ini"
	@echo "== ini deployed to $(DDDA_DIR)/realpawns.ini =="

clean:
	rm -f $(TARGET) $(RES)
