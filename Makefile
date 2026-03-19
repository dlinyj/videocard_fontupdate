# Makefile для проекта работы с ROM BIOS VGA-карт ISA

CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = 

# Основные программы в корне
MAIN_TARGETS = fontupdate

# Утилиты в папке utils
UTILS_TARGETS = encode addchecksum pattern_replace dos_font_viewer

# Правило по умолчанию
all: $(MAIN_TARGETS) utils

# Правила для основных программ в корне

fontupdate: fontupdate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

fontupdate_debug: fontupdate.c
	$(CC) $(CFLAGS) -D__DEBUG__ -o fontupdate $< $(LDFLAGS)

# Правило для компиляции утилит в папке utils
utils/%: utils/%.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Цель для компиляции всех утилит
utils: $(addprefix utils/, $(UTILS_TARGETS))

# Отладочная сборка
debug: fontupdate_debug utils

# Очистка проекта
clean:
	rm -f $(MAIN_TARGETS) $(addprefix utils/, $(UTILS_TARGETS)) *.o *~ core

# Цель для создания архива проекта
dist: clean
	mkdir -p vga-rom-tools
	cp *.c Makefile README* vga-rom-tools/
	cp utils/*.c utils/*.py utils/*.json vga-rom-tools/utils/
	cp -r firmware fnt img update vga-rom-tools/
	tar -czf vga-rom-tools.tar.gz vga-rom-tools
	rm -rf vga-rom-tools

# Объявляем фиктивные цели
.PHONY: all clean dist debug utils fontupdate_debug
