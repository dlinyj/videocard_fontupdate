#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import sys
import json

cp866_map = {
    'А': b'\x80', 'Б': b'\x81', 'В': b'\x82', 'Г': b'\x83', 'Д': b'\x84',
    'Е': b'\x85', 'Ё': b'\xf0', 'Ж': b'\x86', 'З': b'\x87', 'И': b'\x88',
    'Й': b'\x89', 'К': b'\x8a', 'Л': b'\x8b', 'М': b'\x8c', 'Н': b'\x8d',
    'О': b'\x8e', 'П': b'\x8f', 'Р': b'\x90', 'С': b'\x91', 'Т': b'\x92',
    'У': b'\x93', 'Ф': b'\x94', 'Х': b'\x95', 'Ц': b'\x96', 'Ч': b'\x97',
    'Ш': b'\x98', 'Щ': b'\x99', 'Ъ': b'\x9a', 'Ы': b'\x9b', 'Ь': b'\x9c',
    'Э': b'\x9d', 'Ю': b'\x9e', 'Я': b'\x9f',
    'а': b'\xa0', 'б': b'\xa1', 'в': b'\xa2', 'г': b'\xa3', 'д': b'\xa4',
    'е': b'\xa5', 'ё': b'\xf1', 'ж': b'\xa6', 'з': b'\xa7', 'и': b'\xa8',
    'й': b'\xa9', 'к': b'\xaa', 'л': b'\xab', 'м': b'\xac', 'н': b'\xad',
    'о': b'\xae', 'п': b'\xaf', 'р': b'\xe0', 'с': b'\xe1', 'т': b'\xe2',
    'у': b'\xe3', 'ф': b'\xe4', 'х': b'\xe5', 'ц': b'\xe6', 'ч': b'\xe7',
    'ш': b'\xe8', 'щ': b'\xe9', 'ъ': b'\xea', 'ы': b'\xeb', 'ь': b'\xec',
    'э': b'\xed', 'ю': b'\xee', 'я': b'\xef',
}

def rus2cp866(text):
    result = b''
    for ch in text:
        if ch in cp866_map:
            result += cp866_map[ch]
        else:
            result += ch.encode('ascii')
    return result

def bytes_from_escaped(text):
    r"""Превращает строку с \xNN в байты (только для английских строк)"""
    result = bytearray()
    i = 0
    while i < len(text):
        if text[i:i+2] == '\\x' and i+4 <= len(text):
            hex_str = text[i+2:i+4]
            try:
                result.append(int(hex_str, 16))
                i += 4
                continue
            except ValueError:
                pass
        result.append(ord(text[i]))
        i += 1
    return bytes(result)

def load_patterns(json_file):
    with open(json_file, 'r', encoding='utf-8') as f:
        data = json.load(f)
    
    patterns = []
    for item in data['patterns']:
        # Английская строка - конвертируем \xNN в байты
        eng_bytes = bytes_from_escaped(item[0])
        
        # Русская строка - просто переводим в CP866 (без обработки escape)
        # Если в русской строке есть \xNN, они будут восприняты как обычный текст
        rus_bytes = rus2cp866(item[1])
        
        patterns.append((eng_bytes, rus_bytes, item[0], item[1]))  # сохраняем оригиналы для вывода
    
    return patterns

def calculate_vga_checksum(data):
    """Вычисляет контрольную сумму как в addchecksum.c"""
    checksum = 0
    # Суммируем все байты, кроме последнего
    for i in range(len(data) - 1):
        checksum = (checksum + data[i]) & 0xFF
    # Инвертируем для получения нулевой суммы
    checksum = (-checksum) & 0xFF
    return checksum

def patch_bios(rom_file, output_file, patterns):
    with open(rom_file, 'rb') as f:
        data = bytearray(f.read())
    
    print(f"Размер файла: {len(data)} байт")
    
    # Запоминаем оригинальную контрольную сумму для отчёта
    original_checksum = data[-1] if len(data) > 0 else 0
    print(f"Оригинальная контрольная сумма (последний байт): 0x{original_checksum:02X}")
    
    for eng_bytes, rus_bytes, eng_str, rus_str in patterns:
        if len(eng_bytes) != len(rus_bytes):
            print(f"\nПРЕДУПРЕЖДЕНИЕ: длины не совпадают!")
            print(f"  Англ ({len(eng_bytes)}): {eng_str}")
            print(f"  Рус  ({len(rus_bytes)}): {rus_str}")
            print(f"  Пропускаем...")
            continue
        
        print(f"\nИщем: {eng_str} ({len(eng_bytes)} байт)")
        print(f"Заменим на: {rus_str}")
        
        found = 0
        pos = 0
        while True:
            pos = data.find(eng_bytes, pos)
            if pos == -1:
                break
            
            print(f"  Найдено по адресу {pos} (0x{pos:x})")
            for i in range(len(eng_bytes)):
                data[pos + i] = rus_bytes[i]
            found += 1
            pos += len(eng_bytes)
        
        print(f"  Заменено {found} раз")
    
    # Вычисляем новую контрольную сумму (алгоритм из addchecksum.c)
    new_checksum = calculate_vga_checksum(data)
    
    # Записываем контрольную сумму в последний байт
    if len(data) > 0:
        data[-1] = new_checksum
        print(f"\nНовая контрольная сумма (последний байт): 0x{new_checksum:02X}")
        print(f"Изменение: 0x{original_checksum:02X} -> 0x{new_checksum:02X}")
    else:
        print("\nОШИБКА: Файл пустой!")
    
    with open(output_file, 'wb') as f:
        f.write(data)
    print(f"\nСохранено в {output_file}")
    
    # Проверяем, что сумма всех байтов (с новой контрольной) даёт 0
    verify_sum = 0
    for b in data:
        verify_sum = (verify_sum + b) & 0xFF
    print(f"Проверка: сумма всех байт = 0x{verify_sum:02X} (должна быть 0x00)")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Использование: python3 patch_bios.py <файл_биоса.bin> <файл_паттернов.json>")
        sys.exit(1)
    
    patterns = load_patterns(sys.argv[2])
    #patch_bios(sys.argv[1], "bios_ru.bin", patterns)cp
    patch_bios(sys.argv[1], "ru_" + sys.argv[1], patterns)
