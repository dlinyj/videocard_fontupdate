#!/bin/bash

# Проверяем, передан ли файл шрифта
if [ $# -eq 0 ]; then
    echo "Использование: $0 <файл_шрифта.fnt>"
    echo "Пример: $0 BAD.FNT"
    exit 1
fi

FONT_FILE="$1"

# Массив с номерами символов для сохранения
SYMBOLS=(145 155 157 158 171 172)

# Сохраняем каждый символ
for symbol in "${SYMBOLS[@]}"; do
    echo "Сохраняю символ $symbol..."
    ../dos_font_viewer "$FONT_FILE" "$symbol" save bin
    
    # Проверяем, создался ли файл
    if [ -f "char_${symbol}.bin" ]; then
        echo "  ✓ Создан файл: char_${symbol}.bin"
    else
        echo "  ✗ Ошибка: файл char_${symbol}.bin не создан"
    fi
done

echo "Готово! Сохранено ${#SYMBOLS[@]} символов."
