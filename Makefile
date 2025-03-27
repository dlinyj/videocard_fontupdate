# Makefile для проекта работы с ROM BIOS VGA-карт ISA

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = 

# Целевые исполняемые файлы
TARGETS = encode addchecksum fontupdate

# Правило по умолчанию
all: $(TARGETS)

# Правило для компиляции encode
encode: encode.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Правило для компиляции addchecksum
addchecksum: addchecksum.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Правило для компиляции fontupdate
fontupdate: fontupdate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Правило для компиляции fontupdate в отладочном режиме
fontupdate_debug: fontupdate.c
	$(CC) $(CFLAGS) -D__DEBUG__ -o fontupdate $< $(LDFLAGS)

# Цель для отладочной сборки
debug: encode addchecksum fontupdate_debug

# Очистка проекта
clean:
	rm -f $(TARGETS) *.o *~ core

# Цель для создания архива проекта
dist: clean
	mkdir -p vga-rom-tools
	cp *.c Makefile README* vga-rom-tools/
	tar -czf vga-rom-tools.tar.gz vga-rom-tools
	rm -rf vga-rom-tools

# Объявляем фиктивные цели
.PHONY: all clean dist debug fontupdate_debug
