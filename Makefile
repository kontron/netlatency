TOPDIR = $(shell pwd)

VERSION := 0.4.2

CROSS_COMPILE ?=
O ?= $(TOPDIR)

o := $(O)/

CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
INSTALL ?= install
PKG_CONFIG ?= pkg-config

# install directories
PREFIX ?= /usr
BINDIR ?= $(PREFIX)/bin
SBINDIR ?= $(PREFIX)/sbin
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib
MAN1DIR ?= $(PREFIX)/share/man/man1

ALL_TARGETS :=
CLEAN_TARGETS :=

IS_GIT := $(shell if [ -d .git ] ; then echo yes ; else echo no; fi)
ifeq ($(IS_GIT),yes)
VERSION := $(shell git describe --abbrev=8 --dirty --always --tags --long)
endif

cflags_for_lib = $(shell $(PKG_CONFIG) --cflags $(1))
ldflags_for_lib = $(shell $(PKG_CONFIG) --libs $(1))

# let the user override the default CFLAGS/LDFLAGS
CFLAGS ?= -O2 -pthread
LDFLAGS ?= -pthread

MY_CFLAGS := $(CFLAGS)
MY_LDFLAGS := $(LDFLAGS)

MY_CFLAGS += $(call cflags_for_lib,glib-2.0)
LIBS += $(call ldflags_for_lib,glib-2.0)
LIBS += $(call ldflags_for_lib,jansson)
LIBS += -lrt
LIBS += -lm

MY_CFLAGS += -DVERSION=\"$(VERSION)\"

define compile_tgt
	@mkdir -p $(dir $@)
	$(CC) -MD -MT $@ -MF $(@:.o=.d) $(CPPFLAGS) $($(1)_CPPFLAGS) $(MY_CFLAGS) $($(1)_CFLAGS) -c -o $@ $<
endef

define link_tgt
	$(CC) $(MY_LDFLAGS) $($(1)_LDFLAGS) -o $@ $(filter-out %.a,$^) -L/ $(addprefix -l:,$(filter %.a,$^)) $(LIBS) $($(1)_LIBS)
endef

MY_CFLAGS += -Wall -W -Werror

ifeq ($(DEBUG),1)
	MY_CFLAGS += -g -O0
endif

ifeq ($(COVERAGE),1)
	MY_CFLAGS += --coverage
	MY_LDFLAGS += --coverage
endif

DEPS := $(shell find $(o) -name '*.d')

all: real-all

include tests/tests.mk

ALL_TARGETS += $(o)nl-rx $(o)nl-tx

real-all: $(ALL_TARGETS)

CLEAN_TARGETS += clean-rx
CLEAN_TARGETS += clean-tx
INSTALL_TARGETS += install-rx
INSTALL_TARGETS += install-tx
INSTALL_TARGETS += install-scripts
INSTALL_TARGETS += install-manpages

nl-rx_SOURCES := rx.c json.c timer.c
nl-rx_OBJECTS := $(addprefix $(o),$(nl-rx_SOURCES:.c=.o))
nl-tx_SOURCES := tx.c timer.c
nl-tx_OBJECTS := $(addprefix $(o),$(nl-tx_SOURCES:.c=.o))


HELPER_SCRIPTS := nl-report nl-calc nl-trace nl-xlat-ts
MAN1_PAGES := nl-calc.1 nl-report.1 nl-rx.1 nl-trace.1 nl-tx.1 \
nl-xlat-ts.1

$(o)%.o: %.c
	$(call compile_tgt,netlatency)


$(o)nl-rx: $(nl-rx_OBJECTS)
	$(call link_tgt,nl-rx)

clean-nl-rx:
	rm -f $(nl-rx_OBJECTS) $(o)nl-rx

install-rx: $(o)nl-rx
	$(INSTALL) -d -m 0755 $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 $(o)nl-rx $(DESTDIR)$(SBINDIR)/

$(o)nl-tx: $(nl-tx_OBJECTS)
	$(call link_tgt,nl-tx)

clean-tx:
	rm -f $(tx_OBJECTS) $(o)tx

install-tx: $(o)nl-tx
	$(INSTALL) -d -m 0755 $(DESTDIR)$(SBINDIR)
	$(INSTALL) -m 0755 $(o)nl-tx $(DESTDIR)$(SBINDIR)/

install-scripts: $(HELPER_SCRIPTS)
	$(INSTALL) -d -m 0755 $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $^ $(DESTDIR)$(BINDIR)/

install-manpages: $(MAN1_PAGES)
	$(INSTALL) -d -m 0755 $(DESTDIR)$(MAN1DIR)
	$(INSTALL) -m 0755 $^ $(DESTDIR)$(MAN1DIR)/

.PHONY: $(CLEAN_TARGETS) clean
clean: $(CLEAN_TARGETS)
	rm -f $(DEPS)

.PHONY: $(INSTALL_TARGETS) install
install: $(INSTALL_TARGETS)

-include $(DEPS)
