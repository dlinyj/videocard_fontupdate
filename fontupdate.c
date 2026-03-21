#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>
#include "fnt_def.h"

#define DEFAULT_OUTPUT "upd.rom"

// Размеры шрифтов в байтах
#define FONT_8X8_SIZE    2048
#define FONT_8X14_SIZE   3584
#define FONT_8X16_SIZE   4096
#define CHAR_SIZE_8X16   16    // размер одного символа 8x16

// Магические сигнатуры для поиска шрифтов
const uint8_t FONT_8X8_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};
const uint8_t FONT_8X14_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};
const uint8_t FONT_8X16_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};

// Структура для хранения опций командной строки
typedef struct {
    char *input_rom;
    char *font_8x8;
    char *font_8x14;
    char *font_8x16;
    char *dosfont_8x16;    // новая опция
    char *output_rom;
    char *save_pattern;
    int is_normal;
    int output_normal;
    int default_fnt;
} options_t;

#ifdef __DEBUG__
int save_tmp_debfile(char *filename, int filesize, uint8_t *data) {
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening output file");
        return 1;
    }
    if (write(fd, data, filesize) != filesize) {
        perror("Error writing output file");
        close(fd);
        return 1;
    }
    close(fd);
    return 0;
}
#endif //__DEBUG__

// Функция для нормализации данных ROM
void odd_even_to_linear(uint8_t *in, uint8_t *out, int size) {
    for (int i = 0; i < size; i++) {
        if (i % 2) {
            out[i] = in[i/2 + 0x4000];
        } else {
            out[i] = in[i/2];
        }
    }
}

// Функция для обратного преобразования
void linear_to_odd_even(uint8_t *in, uint8_t *out, int size) {
    for (int i = 0; i < size; i++) {
        if (i % 2) {
            out[i/2 + 0x4000] = in[i];
        } else {
            out[i/2] = in[i];
        }
    }
}

// Функция для поиска подпоследовательности в массиве
int find_signature(uint8_t *data, int data_len,
                   const uint8_t *signature, int sig_len, int search_start_pos) {
    for (int i = search_start_pos; i <= data_len - sig_len; i++) {
        int found = 1;
        for (int j = 0; j < sig_len; j++) {
            if (data[i + j] != signature[j]) {
                found = 0;
                break;
            }
        }
        if (found) {
            return i;
        }
    }
    return -1;
}

// Функция для загрузки файла шрифта
uint8_t *load_font_file(const char *filename, int *size) {
    struct stat st;
    int fd;
    uint8_t *data;

    if (stat(filename, &st) != 0) {
        perror("Error getting font file size");
        return NULL;
    }

    *size = st.st_size;
    fd = open(filename, O_RDONLY);
    if (fd == -1) {
        perror("Error opening font file");
        return NULL;
    }

    data = malloc(*size);
    if (data == NULL) {
        perror("Memory allocation failed");
        close(fd);
        return NULL;
    }

    if (read(fd, data, *size) != *size) {
        perror("Error reading font file");
        free(data);
        close(fd);
        return NULL;
    }

    close(fd);
    return data;
}

static int save_font(uint8_t *font_data, size_t font_size, const char *pattern, const char *size_suffix) {
    char filename[256];

    if (pattern && *pattern) {
        snprintf(filename, sizeof(filename), "%s%s.fnt", pattern, size_suffix);
    } else {
        snprintf(filename, sizeof(filename), "%s.fnt", size_suffix);
    }

    FILE *f = fopen(filename, "wb");
    if (!f) {
        fprintf(stderr, "Error: Unable to create file %s\n", filename);
        return -1;
    }

    size_t written = fwrite(font_data, 1, font_size, f);
    fclose(f);

    if (written != font_size) {
        fprintf(stderr, "Error: Failed to write all data to %s\n", filename);
        return -1;
    }

    printf("Original font saved to %s\n", filename);
    return 0;
}

// Функция для обновления контрольной суммы ROM
void update_checksum(uint8_t *data, int size) {
    uint8_t sum = 0;

    for (int i = 0; i < size - 1; i++) {
        sum += data[i];
    }

    data[size - 1] = (uint8_t)(0x100 - sum);
    printf("Updated checksum to: 0x%02X\n", data[size - 1]);
}

// Новая функция для поиска и замены паттернов DOS-шрифта
void find_and_replace_patterns(uint8_t *rom_data, int rom_size,
                               uint8_t *fontrom, int fontrom_offset,
                               uint8_t *dosfont, uint8_t *newfont,
                               int font_size) {
    int patterns_found = 0;
    int patterns_replaced = 0;
    int max_chars = font_size / CHAR_SIZE_8X16;

    if (max_chars > 256) max_chars = 256; // ограничиваем 256 символами

    printf("\nSearching for DOS font patterns...\n");

    // Для каждого символа
    for (int char_idx = 0; char_idx < max_chars; char_idx++) {
        uint8_t *fontrom_char = fontrom + (char_idx * CHAR_SIZE_8X16);
        uint8_t *dosfont_char = dosfont + (char_idx * CHAR_SIZE_8X16);

        // Проверяем, не выходим ли за границы
        if ((char_idx * CHAR_SIZE_8X16 + CHAR_SIZE_8X16) > font_size) {
            break;
        }

        // Сравниваем паттерны символа
        if (memcmp(fontrom_char, dosfont_char, CHAR_SIZE_8X16) != 0) {
            patterns_found++;

            // Ищем паттерн во всем ROM, кроме области основного шрифта
            uint8_t *search_ptr = rom_data;
            uint8_t *end_ptr = rom_data + rom_size - CHAR_SIZE_8X16;
            uint8_t *fontrom_start = rom_data + fontrom_offset;
            uint8_t *fontrom_end = fontrom_start + font_size;

            while (search_ptr <= end_ptr) {
                // Пропускаем область основного шрифта
                if (search_ptr >= fontrom_start && search_ptr < fontrom_end) {
                    search_ptr = fontrom_end;
                    continue;
                }

                if (memcmp(search_ptr, dosfont_char, CHAR_SIZE_8X16) == 0) {
                    // Нашли паттерн - заменяем на символ из нового шрифта
                    uint8_t *newfont_char = newfont + (char_idx * CHAR_SIZE_8X16);

                    // Проверяем, не выходит ли новый символ за границы
                    if ((char_idx * CHAR_SIZE_8X16 + CHAR_SIZE_8X16) <= font_size) {
                        memcpy(search_ptr, newfont_char, CHAR_SIZE_8X16);
                        patterns_replaced++;
                    }

                    search_ptr += CHAR_SIZE_8X16; // Переходим к следующему блоку
                } else {
                    search_ptr++;
                }
            }
        }
    }

    printf("  Characters compared: %d\n", max_chars);
    printf("  Non-matching patterns found: %d\n", patterns_found);
    printf("  Patterns replaced in ROM: %d\n", patterns_replaced);
}

// Функция для вывода справки
void print_help() {
    printf("Usage: fontupdate [OPTIONS]\n");
    printf("Update fonts in VGA BIOS ROM files.\n\n");
    printf("Options:\n");
    printf("  -i, --input <file>   Input ROM file (required)\n");
    printf("  -d, --default        Use default fonts. Font files are ignored\n");
    printf("  -8, --f8 <file>      8x8 font file\n");
    printf("  -4, --f14 <file>     8x14 font file\n");
    printf("  -6, --f16 <file>     8x16 font file\n");
    printf("  -f, --fontdos <file> DOS 8x16 font file for pattern matching\n");
    printf("  -o, --output <file>  Output ROM file (default: %s)\n", DEFAULT_OUTPUT);
    printf("  -s, --save[=pattern] Save original fonts with optional name pattern\n");
    printf("  -n, --normal         The input ROM image has a linear byte arrangement\n");
    printf("  -m, --mix            The output ROM image will have the following order:\n\t\todd at the beginning, even in the middle\n");
    printf("  -h, --help           Display this help message\n\n");
    printf("If any font file is not specified, that font will not be replaced.\n");
    printf("By default, even and odd (shuffled) data is expected to be interleaved in ROM.\n");
    printf("The --dosfont option enables pattern matching: finds characters that\n");
    printf("differ between ROM and DOS font and replaces their occurrences elsewhere\n");
    printf("in the ROM before updating the main font.\n");
    exit(0);
}

// Функция для разбора параметров командной строки
options_t parse_options(int argc, char *argv[]) {
    options_t opts = {
        .input_rom = NULL,
        .font_8x8 = NULL,
        .font_8x14 = NULL,
        .font_8x16 = NULL,
        .dosfont_8x16 = NULL,
        .output_rom = DEFAULT_OUTPUT,
        .save_pattern = NULL,
        .is_normal = 0,
        .output_normal = 1,
        .default_fnt = 0
    };

    struct option long_options[] = {
        {"input",   required_argument, 0, 'i'},
        {"default", no_argument,       0, 'd'},
        {"f8",      required_argument, 0, '8'},
        {"f14",     required_argument, 0, '4'},
        {"f16",     required_argument, 0, '6'},
        {"fontdos", required_argument, 0, 'f'},
        {"output",  required_argument, 0, 'o'},
        {"save",    optional_argument, 0, 's'},
        {"normal",  no_argument,       0, 'n'},
        {"mix",     no_argument,       0, 'm'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "i:do:8:4:6:f:s::nmh",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                opts.input_rom = optarg;
                break;
            case 'd':
                opts.default_fnt = 1;
                break;
            case '8':
                opts.font_8x8 = optarg;
                break;
            case '4':
                opts.font_8x14 = optarg;
                break;
            case '6':
                opts.font_8x16 = optarg;
                break;
            case 'f':
                opts.dosfont_8x16 = optarg;
                break;
            case 'o':
                opts.output_rom = optarg;
                break;
            case 's':
                opts.save_pattern = optarg ? optarg : "";
                break;
            case 'n':
                opts.is_normal = 1;
                break;
            case 'm':
                opts.output_normal = 0;
                break;
            case 'h':
            default:
                print_help();
                break;
        }
    }

    if (opts.input_rom == NULL) {
        fprintf(stderr, "Error: Input ROM file is required\n");
        print_help();
    }

    return opts;
}

int get_filesize(char *filename) {
    struct stat st;
    if (stat(filename, &st) != 0) {
        perror("Error getting input file size");
        exit(-1);
    }
    return st.st_size;
}

uint8_t *read_rom_file(char *input_file, int filesize) {
    printf("Input ROM: %s (size: %d bytes)\n", input_file, filesize);

    uint8_t *rom_data = malloc(filesize);
    if (!rom_data) {
        perror("Memory allocation failed");
        exit(-1);
    }

    int fd = open(input_file, O_RDONLY);
    if (fd == -1) {
        perror("Error opening input file");
        free(rom_data);
        exit(-1);
    }

    if (read(fd, rom_data, filesize) != filesize) {
        perror("Error reading input file");
        close(fd);
        free(rom_data);
        exit(-1);
    }

    close(fd);
    return rom_data;
}

void write_rom_file(char *output_file, uint8_t *output_data, int filesize) {
    int fd = open(output_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening output file");
        free(output_data);
        exit(-1);
    }

    if (write(fd, output_data, filesize) != filesize) {
        perror("Error writing output file");
        close(fd);
        free(output_data);
        exit(-1);
    }

    close(fd);
}

int main(int argc, char *argv[]) {
    options_t opts = parse_options(argc, argv);

    int font_8x8_offset = -1, font_8x14_offset = -1, font_8x16_offset = -1;
    int filesize = get_filesize(opts.input_rom);

    // Читаем ROM
    uint8_t *rom_data = read_rom_file(opts.input_rom, filesize);
    uint8_t *working_data = NULL;
    uint8_t *output_data = NULL;

    // Подготавливаем working_data в зависимости от типа ROM
    if (opts.is_normal) {
        working_data = rom_data;
        printf("Using normal (linear) font layout\n");
    } else {
        working_data = malloc(filesize);
        if (!working_data) {
            perror("Memory allocation failed");
            free(rom_data);
            exit(-1);
        }
        odd_even_to_linear(rom_data, working_data, filesize);
        printf("Converting from odd/even to linear layout\n");
    }

    if ((0x55 != working_data[0]) || (0xAA != working_data[1])) {
        printf("\nWarning! The image is not a BIOS ROM\n");
        printf("Check the correctness of the selection of alternation of even and odd data in ROM.\n");
        if (working_data != rom_data) {
            free(working_data);
        }
        free(rom_data);
        exit(-1);
    }

    #ifdef __DEBUG__
    save_tmp_debfile("normalize.dat", filesize, working_data);
    #endif

    // Ищем шрифты по сигнатурам
    int search_start_offset = 0;

    font_8x8_offset = find_signature(working_data, filesize,
                                     FONT_8X8_SIGNATURE,
                                     sizeof(FONT_8X8_SIGNATURE), search_start_offset);
    if (font_8x8_offset >= 0) {
        search_start_offset = font_8x8_offset + FONT_8X8_SIZE;
    }

    font_8x14_offset = find_signature(working_data, filesize,
                                      FONT_8X14_SIGNATURE,
                                      sizeof(FONT_8X14_SIGNATURE), search_start_offset);
    if (font_8x14_offset >= 0) {
        search_start_offset = font_8x14_offset + FONT_8X14_SIZE;
    }

    font_8x16_offset = find_signature(working_data, filesize,
                                      FONT_8X16_SIGNATURE,
                                      sizeof(FONT_8X16_SIGNATURE), search_start_offset);

    printf("\nFont positions found:\n");
    printf("  8x8:  %s (0x%X)\n",
           font_8x8_offset >= 0 ? "Found" : "Not found",
           font_8x8_offset);
    printf("  8x14: %s (0x%X)\n",
           font_8x14_offset >= 0 ? "Found" : "Not found",
           font_8x14_offset);
    printf("  8x16: %s (0x%X)\n",
           font_8x16_offset >= 0 ? "Found" : "Not found",
           font_8x16_offset);

    // Сохраняем оригинальные шрифты если нужно
    if (opts.save_pattern != NULL) {
        if (font_8x8_offset >= 0) {
            save_font(working_data + font_8x8_offset, FONT_8X8_SIZE,
                     opts.save_pattern, "8x8");
        }
        if (font_8x14_offset >= 0) {
            save_font(working_data + font_8x14_offset, FONT_8X14_SIZE,
                     opts.save_pattern, "8x14");
        }
        if (font_8x16_offset >= 0) {
            save_font(working_data + font_8x16_offset, FONT_8X16_SIZE,
                     opts.save_pattern, "8x16");
        }
    }

    // Обработка DOS-шрифта (поиск и замена паттернов ДО замены основного шрифта)
    if (opts.dosfont_8x16 && font_8x16_offset >= 0 && opts.font_8x16) {
        int dosfont_size;
        uint8_t *dosfont_data = load_font_file(opts.dosfont_8x16, &dosfont_size);

        if (dosfont_data) {
            if (dosfont_size < FONT_8X16_SIZE) {
                printf("Warning: DOS font file size (%d) is smaller than expected (%d)\n",
                       dosfont_size, FONT_8X16_SIZE);
            }

            // Загружаем новый шрифт
            int newfont_size;
            uint8_t *newfont_data = load_font_file(opts.font_8x16, &newfont_size);

            if (newfont_data) {
                if (newfont_size < FONT_8X16_SIZE) {
                    printf("Warning: New font file size (%d) is smaller than expected (%d)\n",
                           newfont_size, FONT_8X16_SIZE);
                }

                // Указатель на область шрифта в working_data
                uint8_t *fontrom_ptr = working_data + font_8x16_offset;

                // Ищем и заменяем паттерны
                find_and_replace_patterns(working_data, filesize,
                                        fontrom_ptr, font_8x16_offset,
                                        dosfont_data, newfont_data,
                                        FONT_8X16_SIZE);

                free(newfont_data);
            }

            free(dosfont_data);
        }
    }

    // Заменяем шрифты
    if (opts.font_8x8 && font_8x8_offset >= 0) {
        int font_size;
        uint8_t *font_data = load_font_file(opts.font_8x8, &font_size);
        if (font_data) {
            printf("\nReplacing 8x8 font from %s\n", opts.font_8x8);
            if (font_size != FONT_8X8_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X8_SIZE);
            }
            int copy_size = (font_size < FONT_8X8_SIZE) ? font_size : FONT_8X8_SIZE;
            memcpy(working_data + font_8x8_offset, font_data, copy_size);
            free(font_data);
        }
    }

    if (opts.font_8x14 && font_8x14_offset >= 0) {
        int font_size;
        uint8_t *font_data = load_font_file(opts.font_8x14, &font_size);
        if (font_data) {
            printf("\nReplacing 8x14 font from %s\n", opts.font_8x14);
            if (font_size != FONT_8X14_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X14_SIZE);
            }
            int copy_size = (font_size < FONT_8X14_SIZE) ? font_size : FONT_8X14_SIZE;
            memcpy(working_data + font_8x14_offset, font_data, copy_size);
            free(font_data);
        }
    }

    if (opts.font_8x16 && font_8x16_offset >= 0) {
        int font_size;
        uint8_t *font_data = load_font_file(opts.font_8x16, &font_size);
        if (font_data) {
            printf("\nReplacing 8x16 font from %s\n", opts.font_8x16);
            if (font_size != FONT_8X16_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X16_SIZE);
            }
            int copy_size = (font_size < FONT_8X16_SIZE) ? font_size : FONT_8X16_SIZE;
            memcpy(working_data + font_8x16_offset, font_data, copy_size);
            free(font_data);
        }
    }

    #ifdef __DEBUG__
    save_tmp_debfile("fnt_updated.dat", filesize, working_data);
    #endif

    // Обновляем контрольную сумму
    update_checksum(working_data, filesize);

    // Подготавливаем выходные данные
    if (opts.output_normal) {
        output_data = working_data;
    } else {
        linear_to_odd_even(working_data, rom_data, filesize);
        output_data = rom_data;
    }

    // Записываем результат
    write_rom_file(opts.output_rom, output_data, filesize);

    printf("\nROM updated successfully. Output written to %s\n", opts.output_rom);

    if (NULL != rom_data) {
        free(rom_data);
    }
    if (output_data != NULL && output_data != working_data) {
        free(output_data);
    }
    if (working_data != NULL && working_data != rom_data) {
        free(working_data);
    }
    return 0;
}
