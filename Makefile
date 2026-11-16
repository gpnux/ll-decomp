PYTHON ?= $(if $(wildcard .venv/bin/python),.venv/bin/python,python3)
CONFIG ?= config/splat.ll.yaml
ASM_DIR ?= asm
ARM_PREFIX ?= arm-none-eabi-
RAM_LDSCRIPT ?= config/ram.ll.ld

AS := $(ARM_PREFIX)as
CPP := $(ARM_PREFIX)cpp
LD := $(ARM_PREFIX)ld
OBJCOPY := $(ARM_PREFIX)objcopy
CC1 := tools/agbcc/bin/agbcc
PREPROC := tools/preproc/preproc
TEXT_CHARMAP_DIR := build/charmaps/jp
TEXT_CHARMAP_SOURCES := charmap.txt charmap_script.txt charmap_title.txt charmap_menu_tiles.txt
TEXT_CHARMAPS := $(addprefix $(TEXT_CHARMAP_DIR)/,menu.txt words.txt title.txt menu_tiles.txt)
PREPROCFLAGS := -m words=$(TEXT_CHARMAP_DIR)/words.txt -m title=$(TEXT_CHARMAP_DIR)/title.txt -m menu_tiles=$(TEXT_CHARMAP_DIR)/menu_tiles.txt

ASFLAGS := -mcpu=arm7tdmi -mthumb-interwork -I$(ASM_DIR)
CPPFLAGS := -nostdinc -Itools/agbcc/include -iquote include
CC1FLAGS := -mthumb-interwork -Wimplicit -Wparentheses -O2 -g -fhex-asm -fprologue-bugfix

SHELL := bash
.SHELLFLAGS := -eu -o pipefail -c
.SUFFIXES:
.DELETE_ON_ERROR:
.DEFAULT_GOAL := all
.PHONY: all check split clean objdiff objdiff-config compdb

# Split is explicit: ordinary make and objdiff never run the splitter.
-include $(ASM_DIR)/units.mk
ifeq ($(wildcard $(ASM_DIR)/units.mk),)
ifeq ($(filter split check clean,$(MAKECMDGOALS)),)
$(error Run 'make split' first)
endif
endif

ROM_NAME ?= ll
BASEROM ?= baserom.gba
ROM := $(ROM_NAME).gba
ELF := build/$(ROM_NAME).elf
MAP := build/$(ROM_NAME).map
BASE_OBJS := $(addprefix build/,$(addsuffix .o,$(CODE_UNITS)))
C_UNITS := $(foreach unit,$(CODE_UNITS),$(if $(wildcard src/$(unit).c),$(unit)))
C_OBJS := $(addprefix build/,$(addsuffix .o,$(C_UNITS)))
ASM_OBJS := $(filter-out $(C_OBJS),$(BASE_OBJS))
EXPECTED_OBJS := $(addprefix expected/,$(addsuffix .o,$(CODE_UNITS)))
DATA_OBJS := $(addprefix build/data/,$(addsuffix .o,$(DATA_UNITS)))
ROM_OBJS := $(BASE_OBJS) $(DATA_OBJS)

all: $(ROM)

# check only hashes the existing ROM; all hashes it after incremental compilation.
all check:
	@test -f "$(ROM)" || { echo "$(ROM) is missing; run make first" >&2; exit 1; }
	@base_hash=$$(sha1sum "$(BASEROM)"); base_hash=$${base_hash%% *}; \
	rom_hash=$$(sha1sum "$(ROM)"); rom_hash=$${rom_hash%% *}; \
	if [ "$$base_hash" = "$$rom_hash" ]; then \
	  echo "$(ROM): SHA1 $$rom_hash (matches $(BASEROM))"; \
	else \
	  echo "$(ROM): SHA1 $$rom_hash" >&2; \
	  echo "$(BASEROM): SHA1 $$base_hash" >&2; exit 1; \
	fi

split: tools/gbadisasm/gbadisasm
	$(PYTHON) tools/gba_split.py "$(CONFIG)" --output "$(ASM_DIR)"
	$(PYTHON) tools/gba_objdiff.py "$(CONFIG)" --output "$(ASM_DIR)"

tools/gbadisasm/gbadisasm: tools/gbadisasm/main.c tools/gbadisasm/disasm.c tools/gbadisasm/gbadisasm.h tools/gbadisasm/Makefile
	$(MAKE) -C tools/gbadisasm

$(PREPROC): $(wildcard tools/preproc/*.cpp tools/preproc/*.h) tools/preproc/Makefile
	$(MAKE) -C tools/preproc

$(TEXT_CHARMAPS) &: $(TEXT_CHARMAP_SOURCES) tools/charmap_for_preproc.py
	$(PYTHON) tools/charmap_for_preproc.py --all "$(TEXT_CHARMAP_DIR)"

$(ELF): $(ROM_OBJS) $(RAM_LDSCRIPT) $(ASM_DIR)/linker.ld Makefile
	@mkdir -p "$(@D)"
	$(LD) -T "$(RAM_LDSCRIPT)" -T "$(ASM_DIR)/linker.ld" -Map "$(MAP)" -o "$@"

$(ROM): $(ELF)
	$(OBJCOPY) -O binary "$<" "$@"

# Objdiff invokes concrete object targets; their only work is compilation.
define CODE_RULES
expected/$(1).o: $(ASM_DIR)/obj/$(1).s $(ASM_DIR)/macro.inc Makefile
	@mkdir -p "$$(@D)"
	$$(AS) $$(ASFLAGS) --MD "$$@.d" "$$<" -o "$$@"

ifneq ($$(wildcard src/$(1).c),)
build/$(1).o: src/$(1).c Makefile $$(PREPROC) $$(TEXT_CHARMAPS) $$(wildcard tools/agbcc/bin/*agbcc)
	@mkdir -p "$$(@D)"
	$$(CPP) $$(CPPFLAGS) -MMD -MP -MT "$$@" -MF "$$@.cpp.d" "$$<" -o "$$@.i"
	$$(PREPROC) $$(PREPROCFLAGS) "$$@.i" "$$(TEXT_CHARMAP_DIR)/menu.txt" | $$(CC1) $$(CC1FLAGS) -o "$$@.s"
	$$(AS) $$(ASFLAGS) --MD "$$@.asm.d" "$$@.s" -o "$$@"
else
build/$(1).o: $$(firstword $$(wildcard src/$(1).s) $(ASM_DIR)/obj/$(1).s) $(ASM_DIR)/macro.inc Makefile
	@mkdir -p "$$(@D)"
	$$(AS) $$(ASFLAGS) --MD "$$@.d" "$$<" -o "$$@"
endif
endef
$(foreach unit,$(CODE_UNITS),$(eval $(call CODE_RULES,$(unit))))

build/sdk/m4a.o build/sdk/sram.o: CC1 := tools/agbcc/bin/old_agbcc
build/sdk/m4a.o: CC1FLAGS := -mthumb-interwork -Wimplicit -Wparentheses -O2 -g
build/sdk/sram.o: CC1FLAGS := -mthumb-interwork -Wimplicit -Wparentheses -O1 -g

build/data/%.o: $(ASM_DIR)/data/%.s $(ASM_DIR)/macro.inc Makefile
	@mkdir -p "$(@D)"
	$(AS) $(ASFLAGS) --MD "$@.d" "$<" -o "$@"

objdiff: $(BASE_OBJS) $(EXPECTED_OBJS)

objdiff-config:
	$(PYTHON) tools/gba_objdiff.py "$(CONFIG)" --output "$(ASM_DIR)"

# clangd uses the same live C units and include paths as the ROM build.
compdb:
	$(PYTHON) tools/gba_compdb.py --cppflags="$(CPPFLAGS)" $(addprefix src/,$(addsuffix .c,$(C_UNITS)))

clean:
	$(RM) -r build expected "$(ROM)"

# A renamed generated file may remain in an old GAS dependency file until its
# object is rebuilt. Let that rebuild proceed; GAS still checks live includes.
$(ASM_DIR)/obj/%.s $(ASM_DIR)/nonmatchings/%.s $(ASM_DIR)/matchings/%.s $(ASM_DIR)/data/%.s $(ASM_DIR)/assets/%: ;

-include $(addsuffix .d,$(ASM_OBJS) $(DATA_OBJS) $(EXPECTED_OBJS)) $(addsuffix .asm.d,$(C_OBJS)) $(addsuffix .cpp.d,$(C_OBJS))
