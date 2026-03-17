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
    char *output_rom;
    char *save_pattern;
    int is_normal;  // новая опция
} options_t;

#ifdef __DEBUG__
int save_tmp_debfile(char * filename, int filesize, uint8_t *data){
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
// (из последовательного формата в специфичный для ПЗУ)
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
void update_checksum(uint8_t *data, int size) {
    // Расчет контрольной суммы BIOS
    // Обычно контрольная сумма находится в последнем байте и должна
    // делать сумму всех байтов кратной 256 (т.е. сумма mod 256 = 0)
    uint8_t sum = 0;
    // Суммируем все байты, кроме последнего
    for (int i = 0; i < size - 1; i++) {
        sum += data[i];
    }
    // Устанавливаем последний байт так, чтобы сумма была равна 0
    data[size - 1] = (uint8_t)(0x100 - sum);
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
    printf("  -n, --normal         Input ROM has normal (linear) font layout\n");
    printf("  -h, --help           Display this help message\n\n");
    printf("If any font file is not specified, that font will not be replaced.\n");
    printf("By default, odd/even (interleaved) font layout is expected.\n");
    exit(0);
}

// Функция для разбора параметров командной строки
options_t parse_options(int argc, char *argv[]) {
    options_t opts = {
        .input_rom = NULL,
        .font_8x8 = NULL,
        .font_8x14 = NULL,
        .font_8x16 = NULL,
        .output_rom = DEFAULT_OUTPUT,
        .save_pattern = NULL,
        .is_normal = 0
    };

    struct option long_options[] = {
        {"input",   required_argument, 0, 'i'},
        {"f8",      required_argument, 0, '8'},
        {"f14",     required_argument, 0, '4'},
        {"f16",     required_argument, 0, '6'},
        {"output",  required_argument, 0, 'o'},
        {"save",    optional_argument, 0, 's'},
        {"normal",  no_argument,       0, 'n'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    int option_index = 0;

    while ((opt = getopt_long(argc, argv, "i:o:8:4:6:s::nh",
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
            case 'n':
                opts.is_normal = 1;
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
        // Для нормального ROM работаем напрямую с оригинальными данными
        working_data = rom_data;
        printf("Using normal (linear) font layout\n");
    } else {
        // Для odd/even ROM нормализуем в отдельный буфер
        working_data = malloc(filesize);
        if (!working_data) {
            perror("Memory allocation failed");
            free(rom_data);
            exit(-1);
        }
        odd_even_to_linear(rom_data, working_data, filesize);
        printf("Converting from odd/even to linear layout\n");
    }

    #ifdef __DEBUG__
    save_tmp_debfile("normalize.dat", filesize, working_data);
    #endif

    // Ищем шрифты по сигнатурам
    int search_start_offset = 0;

    font_8x8_offset = find_signature(working_data, filesize,
                                     FONT_8X8_SIGNATURE,
                                     sizeof(FONT_8X8_SIGNATURE), search_start_offset);
    if (font_8x8_offset > 0) {
        search_start_offset = font_8x8_offset + FONT_8X8_SIZE;
    }

    font_8x14_offset = find_signature(working_data, filesize,
                                      FONT_8X14_SIGNATURE,
                                      sizeof(FONT_8X14_SIGNATURE), search_start_offset);
    if (font_8x14_offset > 0) {
        search_start_offset = font_8x14_offset + FONT_8X14_SIZE;
    }

    font_8x16_offset = find_signature(working_data, filesize,
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

    // Сохраняем оригинальные шрифты если нужно
    if (opts.save_pattern != NULL) {
        if (font_8x8_offset >= 0) {
            save_font(working_data + font_8x8_offset, 256 * 8, opts.save_pattern, "8x8");
        }
        if (font_8x14_offset >= 0) {
            save_font(working_data + font_8x14_offset, 256 * 14, opts.save_pattern, "8x14");
        }
        if (font_8x16_offset >= 0) {
            save_font(working_data + font_8x16_offset, 256 * 16, opts.save_pattern, "8x16");
        }
    }

    // Заменяем шрифты, если указаны
    if (opts.font_8x8 && font_8x8_offset >= 0) {
        int font_size;
        uint8_t *font_data = load_font_file(opts.font_8x8, &font_size);
        if (font_data) {
            printf("Replacing 8x8 font from %s\n", opts.font_8x8);
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
            printf("Replacing 8x14 font from %s\n", opts.font_8x14);
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
            printf("Replacing 8x16 font from %s\n", opts.font_8x16);
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

    // Подготавливаем выходные данные в зависимости от типа ROM
    if (opts.is_normal) {
        // Для нормального ROM выходные данные = working_data
        output_data = working_data;
    } else {
        // Для odd/even ROM преобразуем обратно в rom_data
        linear_to_odd_even(working_data, rom_data, filesize);
        output_data = rom_data;
        free(working_data);  // working_data больше не нужен
    }

    // Записываем результат
    write_rom_file(opts.output_rom, output_data, filesize);

    printf("ROM updated successfully. Output written to %s\n", opts.output_rom);

    // Очистка
    if (opts.is_normal) {
        // Для normal режима rom_data = working_data = output_data
        free(rom_data);
    } else {
        // Для odd/even режима rom_data использован как output_data
        free(rom_data);  // rom_data = output_data
        // working_data уже освобожден выше
    }

    return 0;
}
