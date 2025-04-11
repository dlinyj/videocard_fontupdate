#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <stdint.h>

#define DEFAULT_OUTPUT "upd.rom"

// Размеры шрифтов в байтах
#define FONT_8X8_SIZE    2048
#define FONT_8X14_SIZE   3584
#define FONT_8X16_SIZE   4096

// Магические сигнатуры для поиска шрифтов
const unsigned char FONT_8X8_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};
const unsigned char FONT_8X14_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};
const unsigned char FONT_8X16_SIGNATURE[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7E, 0x81, 0xA5, 0x81
};

// Структура для хранения опций командной строки
typedef struct {
    char *input_rom;
    char *font_8x8;
    char *font_8x14;
    char *font_8x16;
    char *output_rom;
    char *save_pattern;
    int fix_duplicates;  // Флаг для включения функции поиска дубликатов
    char *duplicate_chars; // Строка с кодами символов, например "140,141,240-255"
} options_t;

#ifdef __DEBUG__
int save_tmp_debfile(char * filename, int filesize, unsigned char *data){
    // Записываем результат
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
// (из специфичного формата в последовательный)
void normalize(unsigned char *in, unsigned char *out, int size) {
    for (int i = 0; i < size; i++) {
        if (i % 2) {
            out[i] = in[i/2 + 0x4000];
        } else {
            out[i] = in[i/2];
        }
    }
}

// Функция для обратного преобразования
// (из последовательного формата в специфичный для ПЗУ)
void mix(unsigned char *in, unsigned char *out, int size) {
    for (int i = 0; i < size; i++) {
        if (i % 2) {
            out[i/2 + 0x4000] = in[i];
        } else {
            out[i/2] = in[i];
        }
    }
}

// Функция для поиска подпоследовательности в массиве
int find_signature(unsigned char *data, int data_len,
                   const unsigned char *signature, int sig_len, int search_start_pos) {
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
unsigned char *load_font_file(const char *filename, int *size) {
    struct stat st;
    int fd;
    unsigned char *data;
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
    // Формируем имя файла
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
void update_checksum(unsigned char *data, int size) {
    // Расчет контрольной суммы BIOS
    // Обычно контрольная сумма находится в последнем байте и должна
    // делать сумму всех байтов кратной 256 (т.е. сумма mod 256 = 0)
    unsigned char sum = 0;
    // Суммируем все байты, кроме последнего
    for (int i = 0; i < size - 1; i++) {
        sum += data[i];
    }
    // Устанавливаем последний байт так, чтобы сумма была равна 0
    data[size - 1] = (unsigned char)(0x100 - sum);
    printf("Updated checksum to: 0x%02X\n", data[size - 1]);
}

// Функция для вывода справки
void print_help() {
    printf("Usage: fontupdate [OPTIONS]\n");
    printf("Update fonts in VGA BIOS ROM files.\n\n");
    printf("Options:\n");
    printf("  -i, --input <file>   Input ROM file (required)\n");
    printf("  -8, --f8 <file>      8x8 font file\n");
    printf("  -4, --f14 <file>     8x14 font file\n");
    printf("  -6, --f16 <file>     8x16 font file\n");
    printf("  -o, --output <file>  Output ROM file (default: %s)\n", DEFAULT_OUTPUT);
    printf("  -s, --save[=pattern] Save original fonts with optional name pattern\n");
    printf("  -d, --fix-duplicates Search and fix duplicate character patterns\n");
    printf("  -c, --duplicate-chars <s> Specify character codes to check (default: 140,141,240-255)\n");
    printf("  -h, --help           Display this help message\n\n");
    printf("If any font file is not specified, that font will not be replaced.\n");
    exit(0);
}

// Функция для разбора параметров командной строки
options_t parse_options(int argc, char *argv[]) {
    options_t opts = {
        .input_rom = NULL,
        .font_8x8 = NULL,
        .font_8x14 = NULL,
        .font_8x16 = NULL,
        .output_rom = DEFAULT_OUTPUT
    };
    struct option long_options[] = {
        {"input",   required_argument, 0, 'i'},
        {"f8",      required_argument, 0, '8'},
        {"f14",     required_argument, 0, '4'},
        {"f16",     required_argument, 0, '6'},
        {"output",  required_argument, 0, 'o'},
        {"save",    optional_argument, 0, 's'},
        {"fix-duplicates", no_argument, 0, 'd'},
        {"duplicate-chars", required_argument, 0, 'c'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "i:o:8:4:6:s:dc:h",
                              long_options, &option_index)) != -1) {
        switch (opt) {
            case 'i':
                opts.input_rom = optarg;
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
            case 'o':
                opts.output_rom = optarg;
                break;
            case 's':
                opts.save_pattern = optarg ? optarg : "";
                break;
            case 'd':
                opts.fix_duplicates = 1;
                break;
            case 'c':
                opts.duplicate_chars = optarg;
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

static int *parse_char_codes(const char *chars_str, int *count) {
    if (!chars_str || !count) {
        fprintf(stderr, "Invalid parameters for parse_char_codes\n");
        return NULL;
    }

    char *str_copy = strdup(chars_str);
    if (!str_copy) {
        fprintf(stderr, "Memory allocation error in parse_char_codes\n");
        return NULL;
    }

    char *token, *subtoken;
    char *saveptr1 = NULL, *saveptr2 = NULL;
    int *codes = NULL;
    int capacity = 0;
    *count = 0;

    // Разбиваем строку по запятым
    for (token = strtok_r(str_copy, ",", &saveptr1); token; token = strtok_r(NULL, ",", &saveptr1)) {
        // Проверяем, есть ли диапазон (символ '-')
        if (strchr(token, '-')) {
            int start, end;
            subtoken = strtok_r(token, "-", &saveptr2);
            if (!subtoken) continue;
            start = atoi(subtoken);

            subtoken = strtok_r(NULL, "-", &saveptr2);
            if (!subtoken) continue;
            end = atoi(subtoken);

            if (start < 0) start = 0;
            if (end > 255) end = 255;
            if (start > end) continue;

            // Добавляем все символы из диапазона
            for (int i = start; i <= end; i++) {
                if (*count >= capacity) {
                    capacity = capacity ? capacity * 2 : 16;
                    int *new_codes = realloc(codes, capacity * sizeof(int));
                    if (!new_codes) {
                        fprintf(stderr, "Memory allocation error in parse_char_codes\n");
                        free(codes);
                        free(str_copy);
                        return NULL;
                    }
                    codes = new_codes;
                }
                codes[(*count)++] = i;
            }
        } else {
            // Добавляем один символ
            int code = atoi(token);
            if (code < 0 || code > 255) continue;

            if (*count >= capacity) {
                capacity = capacity ? capacity * 2 : 16;
                int *new_codes = realloc(codes, capacity * sizeof(int));
                if (!new_codes) {
                    fprintf(stderr, "Memory allocation error in parse_char_codes\n");
                    free(codes);
                    free(str_copy);
                    return NULL;
                }
                codes = new_codes;
            }
            codes[(*count)++] = code;
        }
    }

    free(str_copy);

    // Если не нашли ни одного кода, возвращаем NULL
    if (*count == 0) {
        free(codes);
        return NULL;
    }

    return codes;
}


static int fix_duplicate_chars(uint8_t *rom_data, size_t rom_size,
                             uint8_t *font_data, size_t font_offset,
                             int *char_codes, int code_count) {
    int replacements = 0;

    if (!rom_data || !font_data || !char_codes || rom_size < font_offset + 256 * 16) {
        fprintf(stderr, "Invalid parameters for fix_duplicate_chars\n");
        return -1;
    }

    // Для каждого символа из списка
    for (int i = 0; i < code_count; i++) {
        int char_code = char_codes[i];
        if (char_code < 0 || char_code > 255) {
            fprintf(stderr, "Invalid character code: %d\n", char_code);
            continue;
        }

        // Получаем указатель на патерн символа в основном шрифте
        uint8_t *pattern = &font_data[font_offset + char_code * 16];

        printf("Searching for duplicates of char %d (0x%02X)...\n", char_code, char_code);

        // Ищем этот патерн по всему образу ПЗУ, исключая основную таблицу шрифтов
        for (size_t offset = 0; offset <= rom_size - 16; offset++) {
            // Пропускаем основную таблицу шрифтов
            if (offset >= font_offset && offset < font_offset + 256 * 16) {
                continue;
            }

            // Сравниваем патерн
            if (memcmp(&rom_data[offset], pattern, 16) == 0) {
                // Нашли дублирующийся патерн, заменяем его
                memcpy(&rom_data[offset], pattern, 16);
                printf("  Found duplicate at offset 0x%zX, replaced\n", offset);
                replacements++;
            }
        }
    }

    return replacements;
}



int main(int argc, char *argv[]) {
    options_t opts = parse_options(argc, argv);
    struct stat st;
    int filesize;
    unsigned char *rom_data, *normalized_data;
    int font_8x8_offset = -1, font_8x14_offset = -1, font_8x16_offset = -1;
    // Получаем размер входного файла
    if (stat(opts.input_rom, &st) != 0) {
        perror("Error getting input file size");
        return 1;
    }
    filesize = st.st_size;
    printf("Input ROM: %s (size: %d bytes)\n", opts.input_rom, filesize);
    // Выделяем память для данных
    rom_data = malloc(filesize);
    normalized_data = malloc(filesize);
    if (!rom_data || !normalized_data) {
        perror("Memory allocation failed");
        free(rom_data);
        free(normalized_data);
        return 1;
    }
    // Читаем входной файл
    int fd = open(opts.input_rom, O_RDONLY);
    if (fd == -1) {
        perror("Error opening input file");
        free(rom_data);
        free(normalized_data);
        return 1;
    }
    if (read(fd, rom_data, filesize) != filesize) {
        perror("Error reading input file");
        close(fd);
        free(rom_data);
        free(normalized_data);
        return 1;
    }
    close(fd);
    // Нормализуем данные
    normalize(rom_data, normalized_data, filesize); //работает корректно
    #ifdef __DEBUG__
    save_tmp_debfile("normalize.dat", filesize, normalized_data);
    #endif
    int search_start_offset = 0;
    // Ищем шрифты по сигнатурам
    font_8x8_offset = find_signature(normalized_data, filesize,
                                      FONT_8X8_SIGNATURE,
                                      sizeof(FONT_8X8_SIGNATURE), search_start_offset);
    if (font_8x8_offset) {
        search_start_offset = font_8x8_offset + FONT_8X8_SIZE;
    }
    font_8x14_offset = find_signature(normalized_data, filesize,
                                       FONT_8X14_SIGNATURE,
                                       sizeof(FONT_8X14_SIGNATURE), search_start_offset);
    if (font_8x14_offset) {
        search_start_offset = font_8x14_offset + FONT_8X14_SIZE;
    }
    font_8x16_offset = find_signature(normalized_data, filesize,
                                       FONT_8X16_SIGNATURE,
                                       sizeof(FONT_8X16_SIGNATURE), search_start_offset);

    printf("Font positions found:\n");
    printf("  8x8:  %s (0x%X)\n",
           font_8x8_offset >= 0 ? "Found" : "Not found",
           font_8x8_offset);
    printf("  8x14: %s (0x%X)\n",
           font_8x14_offset >= 0 ? "Found" : "Not found",
           font_8x14_offset);
    printf("  8x16: %s (0x%X)\n",
           font_8x16_offset >= 0 ? "Found" : "Not found",
           font_8x16_offset);

    if ((font_8x8_offset >= 0) && (opts.save_pattern != NULL))  {
        save_font(normalized_data + font_8x8_offset, 256 * 8, opts.save_pattern, "8x8");
    }

    if ((font_8x14_offset >= 0) && (opts.save_pattern != NULL)) {
        save_font(normalized_data + font_8x14_offset, 256 * 14, opts.save_pattern, "8x14");
    }

    if  ((font_8x16_offset >= 0) && (opts.save_pattern != NULL)) {
        save_font(normalized_data + font_8x16_offset, 256 * 16, opts.save_pattern, "8x16");
    }


    // Заменяем шрифты, если указаны
    if (opts.font_8x8 && font_8x8_offset >= 0) {
        int font_size;

        unsigned char *font_data = load_font_file(opts.font_8x8, &font_size);
            if (font_data) {
            printf("Replacing 8x8 font from %s\n", opts.font_8x8);

            // Проверяем размер шрифта
            if (font_size != FONT_8X8_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X8_SIZE);
            }

            // Копируем шрифт в ROM
            int copy_size = (font_size < FONT_8X8_SIZE) ? font_size : FONT_8X8_SIZE;
            memcpy(normalized_data + font_8x8_offset, font_data, copy_size);

            free(font_data);
        }
    }
    if (opts.font_8x14 && font_8x14_offset >= 0) {
        int font_size;
        unsigned char *font_data = load_font_file(opts.font_8x14, &font_size);
            if (font_data) {
            printf("Replacing 8x14 font from %s\n", opts.font_8x14);
                    // Проверяем размер шрифта
            if (font_size != FONT_8X14_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X14_SIZE);
            }
                    // Копируем шрифт в ROM
            int copy_size = (font_size < FONT_8X14_SIZE) ? font_size : FONT_8X14_SIZE;
            memcpy(normalized_data + font_8x14_offset, font_data, copy_size);

            free(font_data);
        }
    }
    if (opts.font_8x16 && font_8x16_offset >= 0) {
        int font_size;
        unsigned char *font_data = load_font_file(opts.font_8x16, &font_size);
            if (font_data) {
            printf("Replacing 8x16 font from %s\n", opts.font_8x16);
                    // Проверяем размер шрифта
            if (font_size != FONT_8X16_SIZE) {
                printf("Warning: Font file size (%d) doesn't match expected size (%d)\n",
                       font_size, FONT_8X16_SIZE);
            }

            // Копируем шрифт в ROM
            int copy_size = (font_size < FONT_8X16_SIZE) ? font_size : FONT_8X16_SIZE;
            memcpy(normalized_data + font_8x16_offset, font_data, copy_size);


        }
            // Если включена опция поиска дубликатов и шрифт был заменен
            if (opts.fix_duplicates) {
                int count;
                int *char_codes = parse_char_codes(opts.duplicate_chars, &count);
                if (char_codes) {
                    printf("Searching for duplicate 8x16 characters...\n");
                    int fixed = fix_duplicate_chars(normalized_data, filesize,
                                                  font_data, font_8x16_offset,
                                                  char_codes, count);
                    printf("Fixed %d duplicate character patterns\n", fixed);
                    free(char_codes);
                }
            }
        free(font_data);
    }
    #ifdef __DEBUG__
    save_tmp_debfile("fnt_updated.dat", filesize, normalized_data);
    #endif

    // Обновляем контрольную сумму
    update_checksum(normalized_data, filesize);

    // Преобразуем обратно в формат для ПЗУ
    mix(normalized_data, rom_data, filesize);

    // Записываем результат
    fd = open(opts.output_rom, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        perror("Error opening output file");
        free(rom_data);
        free(normalized_data);
        return 1;
    }

    if (write(fd, rom_data, filesize) != filesize) {
        perror("Error writing output file");
        close(fd);
        free(rom_data);
        free(normalized_data);
        return 1;
    }

    close(fd);

    printf("ROM updated successfully. Output written to %s\n", opts.output_rom);

    free(rom_data);
    free(normalized_data);

    return 0;
}
