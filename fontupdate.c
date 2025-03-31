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
    printf("  -i, --input <file>  Input ROM file (required)\n");
    printf("  -8, --f8 <file>     8x8 font file\n");
    printf("  -4, --f14 <file>    8x14 font file\n");
    printf("  -6, --f16 <file>    8x16 font file\n");
    printf("  -o, --output <file> Output ROM file (default: %s)\n", DEFAULT_OUTPUT);
    printf("  -h, --help          Display this help message\n\n");
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
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };
    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "i:8:4:6:o:h",
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


int main(int argc, char *argv[]) {
    options_t opts = parse_options(argc, argv);
    struct stat st;
    int filesize;
    unsigned char *rom_data, *normalized_data;
    int font_8x8_pos = -1, font_8x14_pos = -1, font_8x16_pos = -1;
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
    int search_start_pos = 0;
    // Ищем шрифты по сигнатурам
    font_8x8_pos = find_signature(normalized_data, filesize,
                                      FONT_8X8_SIGNATURE,
                                      sizeof(FONT_8X8_SIGNATURE), search_start_pos);
    if (font_8x8_pos) {
        search_start_pos = font_8x8_pos + FONT_8X8_SIZE;
    }
    font_8x14_pos = find_signature(normalized_data, filesize,
                                       FONT_8X14_SIGNATURE,
                                       sizeof(FONT_8X14_SIGNATURE), search_start_pos);
    if (font_8x14_pos) {
        search_start_pos = font_8x14_pos + FONT_8X14_SIZE;
    }
    font_8x16_pos = find_signature(normalized_data, filesize,
                                       FONT_8X16_SIGNATURE,
                                       sizeof(FONT_8X16_SIGNATURE), search_start_pos);

    printf("Font positions found:\n");
    printf("  8x8:  %s (0x%X)\n",
           font_8x8_pos >= 0 ? "Found" : "Not found",
           font_8x8_pos);
    printf("  8x14: %s (0x%X)\n",
           font_8x14_pos >= 0 ? "Found" : "Not found",
           font_8x14_pos);
    printf("  8x16: %s (0x%X)\n",
           font_8x16_pos >= 0 ? "Found" : "Not found",
           font_8x16_pos);

    // Заменяем шрифты, если указаны
    if (opts.font_8x8 && font_8x8_pos >= 0) {
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
            memcpy(normalized_data + font_8x8_pos, font_data, copy_size);

            free(font_data);
        }
    }
    if (opts.font_8x14 && font_8x14_pos >= 0) {
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
            memcpy(normalized_data + font_8x14_pos, font_data, copy_size);

            free(font_data);
        }
    }
    if (opts.font_8x16 && font_8x16_pos >= 0) {
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
            memcpy(normalized_data + font_8x16_pos, font_data, copy_size);

            free(font_data);
        }
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
