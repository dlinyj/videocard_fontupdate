#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// Функция для отображения символа DOS-шрифта в консоли
void display_char(uint8_t *char_data) {
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 8; x++) {
            if (char_data[y] & (0x80 >> x)) {
                printf("\u2588"); // Полный блок Unicode
            } else {
                printf(" "); // Пробел для пустых пикселей
            }
        }
        printf("\n");
    }
}

// Функция для сохранения символа в текстовый файл как ASCII-арт
void save_char_as_text(uint8_t *char_data, const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Не удалось создать файл");
        return;
    }

    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 8; x++) {
            if (char_data[y] & (0x80 >> x)) {
                fprintf(f, "#"); // Используем # для заполненных пикселей
            } else {
                fprintf(f, "."); // Используем . для пустых пикселей
            }
        }
        fprintf(f, "\n");
    }

    fclose(f);
    printf("Символ сохранен как ASCII-арт в файл: %s\n", filename);
}

// Функция для сохранения символа в бинарном формате (как есть)
void save_char_as_binary(uint8_t *char_data, const char *filename) {
    FILE *f = fopen(filename, "wb");
    if (!f) {
        perror("Не удалось создать файл");
        return;
    }

    fwrite(char_data, 1, 16, f);
    fclose(f);
    printf("Символ сохранен в бинарном формате в файл: %s\n", filename);
}

// Функция для сохранения символа в формате C-массива
void save_char_as_c_array(uint8_t *char_data, const char *filename, int char_index) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Не удалось создать файл");
        return;
    }

    fprintf(f, "// DOS font character %d (0x%02X)\n", char_index, char_index);
    fprintf(f, "const uint8_t char_%d[16] = {\n", char_index);
    
    for (int y = 0; y < 16; y++) {
        fprintf(f, "    0x%02X", char_data[y]);
        if (y < 15) fprintf(f, ",");
        
        // Добавляем комментарий с визуальным представлением строки
        fprintf(f, " // ");
        for (int x = 0; x < 8; x++) {
            fprintf(f, "%c", (char_data[y] & (0x80 >> x)) ? '#' : '.');
        }
        fprintf(f, "\n");
    }
    
    fprintf(f, "};\n");
    fclose(f);
    printf("Символ сохранен как C-массив в файл: %s\n", filename);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Использование: %s <файл_шрифта> <номер_символа> [save] [format]\n", argv[0]);
        printf("Опции:\n");
        printf("  save   - сохранить символ в файл\n");
        printf("  format - формат сохранения (txt, bin, c) - по умолчанию txt\n");
        return 1;
    }

    // Получаем имя файла и номер символа из аргументов
    char *font_file = argv[1];
    int char_index = atoi(argv[2]);
    int save_mode = 0;
    char save_format[10] = "txt"; // По умолчанию текстовый формат

    // Проверяем аргументы для сохранения
    if (argc > 3 && strcmp(argv[3], "save") == 0) {
        save_mode = 1;
        if (argc > 4) {
            strncpy(save_format, argv[4], 9);
            save_format[9] = '\0';
        }
    }

    // Проверяем диапазон символа
    if (char_index < 0 || char_index > 255) {
        printf("Номер символа должен быть от 0 до 255\n");
        return 1;
    }

    // Открываем файл шрифта
    FILE *f = fopen(font_file, "rb");
    if (!f) {
        perror("Не удалось открыть файл шрифта");
        return 1;
    }

    // Перемещаемся к нужному символу (каждый символ занимает 16 байт)
    if (fseek(f, char_index * 16, SEEK_SET) != 0) {
        perror("Ошибка при позиционировании в файле");
        fclose(f);
        return 1;
    }

    // Считываем данные символа (16 байт)
    uint8_t char_data[16];
    if (fread(char_data, 1, 16, f) != 16) {
        printf("Ошибка при чтении данных символа\n");
        fclose(f);
        return 1;
    }

    fclose(f);

    // Выводим информацию о символе
    printf("Символ: %d (0x%02X)\n", char_index, char_index);
    
    // Отображаем символ
    display_char(char_data);

    // Если нужно сохранить символ
    if (save_mode) {
        char filename[100];
        
        if (strcmp(save_format, "txt") == 0) {
            sprintf(filename, "char_%d.txt", char_index);
            save_char_as_text(char_data, filename);
        } 
        else if (strcmp(save_format, "bin") == 0) {
            sprintf(filename, "char_%d.bin", char_index);
            save_char_as_binary(char_data, filename);
        } 
        else if (strcmp(save_format, "c") == 0) {
            sprintf(filename, "char_%d.c", char_index);
            save_char_as_c_array(char_data, filename, char_index);
        } 
        else {
            printf("Неизвестный формат сохранения: %s\n", save_format);
            printf("Поддерживаемые форматы: txt, bin, c\n");
        }
    }

    return 0;
}
