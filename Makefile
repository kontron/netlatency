TOPDIR = $(shell pwd)

VERSION := 0.0

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
INCLUDEDIR ?= $(PREFIX)/include
LIBDIR ?= $(PREFIX)/lib

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

ALL_TARGETS += $(o)rx $(o)tx $(o)reader

real-all: $(ALL_TARGETS)




CLEAN_TARGETS += clean-rx
CLEAN_TARGETS += clean-tx
CLEAN_TARGETS += clean-reader
INSTALL_TARGETS += install-rx

rx_SOURCES := rx.c domain_socket.c timer.c
rx_OBJECTS := $(addprefix $(o),$(rx_SOURCES:.c=.o))

$(o)%.o: %.c
	$(call compile_tgt,rx)


$(o)rx: $(rx_OBJECTS)
	$(call link_tgt,rx)

clean-rx:
	rm -f $(rx_OBJECTS) $(o)rx

install-rx: $(o)rx
	$(INSTALL) -d -m 0755 $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 0755 $(o)rx $(DESTDIR)$(BINDIR)/


tx_SOURCES := tx.c timer.c domain_socket.c config_control.c
tx_OBJECTS := $(addprefix $(o),$(tx_SOURCES:.c=.o))

$(o)tx: $(tx_OBJECTS)
	$(call link_tgt,tx)

clean-tx:
	rm -f $(tx_OBJECTS) $(o)tx

reader_SOURCES := reader.c domain_socket.c
reader_OBJECTS := $(addprefix $(o),$(reader_SOURCES:.c=.o))

$(o)reader: $(reader_OBJECTS)
	$(call link_tgt,reader)

clean-reader:
	rm -f $(reader_OBJECTS) $(o)reader


.PHONY: $(CLEAN_TARGETS) clean
clean: $(CLEAN_TARGETS)
	rm -f $(DEPS)

.PHONY: $(INSTALL_TARGETS) install
install: $(INSTALL_TARGETS)

-include $(DEPS)
