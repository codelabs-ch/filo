#
# Copyright (C) 2008 by coresystems GmbH 
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#

EBCHECKSUM = ebchecksum.o ipchecksum.o checksum_elf32le.o checksum_elf32be.o checksum_elf64le.o checksum_elf64be.o
EBCHECKSUM_OBJS     := $(patsubst %,$(obj)/util/%,$(EBCHECKSUM))

$(obj)/util/ebchecksum: $(EBCHECKSUM_OBJS)
	@echo "  HOSTCC  $(subst $(shell pwd)/,,$(@))"
	@$(HOSTCC) $(HOSTCFLAGS) $(EBCHECKSUM_OBJS) -o $@

$(obj)/util/%.o: $(src)/util/%.c
	@echo "  HOSTCC  $(subst $(shell pwd)/,,$(<))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -c $< -o $@

$(obj)/util/ipchecksum.o: $(src)/main/ipchecksum.c
	@echo "  HOSTCC  $(subst $(shell pwd)/,,$(<))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -c $< -o $@

$(obj)/util/checksum_elf32le.o: util/checksum_elf.c
	@echo "  HOSTCC  $< -> $(subst $(shell pwd)/,,$(@))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -DTARGET_CLASS=ELFCLASS32 -DTARGET_DATA=ELFDATA2LSB -c $< -o $@
$(obj)/util/checksum_elf32be.o: util/checksum_elf.c
	@echo "  HOSTCC  $< -> $(subst $(shell pwd)/,,$(@))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -DTARGET_CLASS=ELFCLASS32 -DTARGET_DATA=ELFDATA2MSB -c $< -o $@
$(obj)/util/checksum_elf64le.o: util/checksum_elf.c
	@echo "  HOSTCC  $< -> $(subst $(shell pwd)/,,$(@))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -DTARGET_CLASS=ELFCLASS64 -DTARGET_DATA=ELFDATA2LSB -c $< -o $@
$(obj)/util/checksum_elf64be.o: util/checksum_elf.c
	@echo "  HOSTCC  $< -> $(subst $(shell pwd)/,,$(@))"
	@$(HOSTCC) $(HOSTCFLAGS) -Iinclude -DTARGET_CLASS=ELFCLASS64 -DTARGET_DATA=ELFDATA2MSB -c $< -o $@

$(obj)/util/vmser: util/vmser.c
	@echo "  HOSTCC  $(subst $(shell pwd)/,,$(<))"
	@$(HOSTCC) $(HOSTCFLAGS) $< -lcurses -o $@

vmser: $(obj)/util/vmser

.PHONY : vmser
