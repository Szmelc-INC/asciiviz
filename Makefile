# Makefile — builds asciiviz and bakes presets + palettes
APP       := asciiviz
SRC       := main.c util.c terminal.c
PRESETS_H := baked_presets.h
PALETTES_H:= baked_palettes.h

CC        ?= gcc
CFLAGS    ?= -O2 -std=c99 -Wall -Wextra
LDFLAGS   ?= -lm

PREFIX    ?= /usr/local
BINDIR    ?= $(PREFIX)/bin

FUNCDIR   := functions
CFGFILES  := $(wildcard $(FUNCDIR)/*.cfg)

CHARDIR   := palettes/char
COLDIR    := palettes/col
CHARFILES := $(wildcard $(CHARDIR)/*.cfg)
COLFILES  := $(wildcard $(COLDIR)/*.cfg)

HAVE_CFG  := $(strip $(CFGFILES))
HAVE_CHAR := $(strip $(CHARFILES))
HAVE_COL  := $(strip $(COLFILES))

.PHONY: all run clean install uninstall nobake

all: $(APP)

$(APP): $(SRC) $(PRESETS_H) $(PALETTES_H)
	$(CC) $(CFLAGS) -DBAKE_PRESETS -DBAKE_PALETTES -o $@ $(SRC) $(LDFLAGS)

# --------- baked presets ----------
ifeq ($(HAVE_CFG),)
$(PRESETS_H):
	@echo "/* auto-generated empty header (no cfg) */" > $@
	@echo "#ifndef BAKED_PRESETS_H" >> $@
	@echo "#define BAKED_PRESETS_H" >> $@
	@echo "#include <stddef.h>" >> $@
	@echo "static const struct { const char *name; const char *ini; } g_baked_presets[] = {};" >> $@
	@echo "static const size_t g_baked_presets_count = 0;" >> $@
	@echo "#endif" >> $@
	@echo "Generated $@ (empty)."
else
$(PRESETS_H): $(CFGFILES)
	@echo "// auto-generated from $(FUNCDIR)/*.cfg — do not edit" > $@
	@echo "#ifndef BAKED_PRESETS_H" >> $@
	@echo "#define BAKED_PRESETS_H" >> $@
	@echo "#include <stddef.h>" >> $@
	@for f in $(CFGFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "static const char preset_$$sym[] =" >> $@; \
		sed 's/\\/\\\\/g; s/\"/\\\"/g; s/\r//g; s/^/\"/; s/$$/\\n\"/' $$f >> $@; \
		echo ";" >> $@; \
	done
	@echo "static const struct { const char *name; const char *ini; } g_baked_presets[] = {" >> $@
	@for f in $(CFGFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "  {\"$$n\", preset_$$sym}," >> $@; \
	done
	@echo "};" >> $@
	@echo "static const size_t g_baked_presets_count = sizeof(g_baked_presets)/sizeof(g_baked_presets[0]);" >> $@
	@echo "#endif" >> $@
	@echo "Generated $@ from $(words $(CFGFILES)) cfg(s)."
endif

# --------- baked palettes ----------
$(PALETTES_H):
	@echo "// auto-generated palettes header" > $@
	@echo "#ifndef BAKED_PALETTES_H" >> $@
	@echo "#define BAKED_PALETTES_H" >> $@
	@echo "#include <stddef.h>" >> $@

ifneq ($(HAVE_CHAR),)
	@for f in $(CHARFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "static const char charpal_$$sym[] =" >> $@; \
		sed 's/\\/\\\\/g; s/\"/\\\"/g; s/\r//g; s/^/\"/; s/$$/\\n\"/' $$f >> $@; \
		echo ";" >> $@; \
	done
	@echo "static const struct { const char *name; const char *text; } g_char_pals[] = {" >> $@
	@for f in $(CHARFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "  {\"$$n\", charpal_$$sym}," >> $@; \
	done
	@echo "};" >> $@
	@echo "static const size_t g_char_pals_count = sizeof(g_char_pals)/sizeof(g_char_pals[0]);" >> $@
else
	@echo "static const struct { const char *name; const char *text; } g_char_pals[] = {};" >> $@
	@echo "static const size_t g_char_pals_count = 0;" >> $@
endif

ifneq ($(HAVE_COL),)
	@for f in $(COLFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "static const char colpal_$$sym[] =" >> $@; \
		sed 's/\\/\\\\/g; s/\"/\\\"/g; s/\r//g; s/^/\"/; s/$$/\\n\"/' $$f >> $@; \
		echo ";" >> $@; \
	done
	@echo "static const struct { const char *name; const char *text; } g_color_pals[] = {" >> $@
	@for f in $(COLFILES); do \
		n=$$(basename $$f .cfg); \
		sym=$$(echo $$n | sed 's/[^A-Za-z0-9_]/_/g'); \
		echo "  {\"$$n\", colpal_$$sym}," >> $@; \
	done
	@echo "};" >> $@
	@echo "static const size_t g_color_pals_count = sizeof(g_color_pals)/sizeof(g_color_pals[0]);" >> $@
else
	@echo "static const struct { const char *name; const char *text; } g_color_pals[] = {};" >> $@
	@echo "static const size_t g_color_pals_count = 0;" >> $@
endif

	@echo "#endif /* BAKED_PALETTES_H */" >> $@
	@echo "Generated $@."

run: $(APP)
	./$(APP)

nobake:
	$(CC) $(CFLAGS) -o $(APP) $(SRC) $(LDFLAGS)

install: $(APP)
	install -Dm755 $(APP) "$(DESTDIR)$(BINDIR)/$(APP)"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/$(APP)"

clean:
	rm -f $(APP) $(PRESETS_H) $(PALETTES_H)
