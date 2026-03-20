; Программа для DOS (COM, NASM)
; Сохраняет текущий шрифт VGA (8x16) в файл
; Использование: savefont.com [filename]
; По умолчанию: dosfont.fnt

bits 16
org 0x100

start:
    ; Парсим командную строку
    mov si, 0x81            ; Начало командной строки (после пробела)
    call skip_spaces        ; Пропускаем начальные пробелы

    cmp byte [si], 0Dh      ; Проверяем конец строки (Enter)
    je  use_default         ; Если пусто — используем имя по умолчанию

    ; Копируем имя файла из командной строки
    mov di, filename
.copy_loop:
    lodsb
    cmp al, 0Dh             ; Конец строки?
    je  .done_copy
    cmp al, ' '             ; Пробел (конец аргумента)?
    je  .done_copy
    stosb
    jmp .copy_loop
.done_copy:
    mov byte [di], 0        ; Завершающий нуль
    jmp do_work

use_default:
    ; Копируем имя по умолчанию
    mov si, default_name
    mov di, filename
.copy_default:
    lodsb
    stosb
    test al, al
    jnz .copy_default

do_work:
    call get_vga_font

    ; Создаём файл
    mov ah, 0x3C
    xor cx, cx              ; Атрибуты файла — обычный
    mov dx, filename
    int 0x21
    jc error_exit
    mov bx, ax              ; Сохраняем handle

    ; Записываем буфер в файл
    mov ah, 0x40
    mov cx, 4096
    mov dx, font_buffer
    int 0x21
    jc error_exit

    ; Закрываем файл
    mov ah, 0x3E
    int 0x21

    ; Успешный выход
    mov ax, 0x4C00
    int 0x21

error_exit:
    mov ax, 0x4C01
    int 0x21

; Пропускает пробелы в начале командной строки
skip_spaces:
    lodsb
    cmp al, ' '
    je skip_spaces
    dec si                  ; Возвращаемся на не-пробел
    ret

get_vga_font:
    push di
    push si
    push cx
    push ds
    push es

    ; Получаем указатель на шрифт (ES:BP)
    mov ah, 0x11
    mov al, 0x30
    mov bh, 0x06            ; 8x16 font
    int 0x10

    ; В COM-программе CS=DS=ES=SS, но после int 10h ES указывает на шрифт
    ; Нужно скопировать из ES:BP в наш буфер (CS/DS)

    push es
    pop ds                  ; DS = ES (источник шрифта)
    mov si, bp              ; SI = BP

    push cs
    pop es                  ; ES = CS (наш сегмент с буфером)
    mov di, font_buffer     ; DI = адрес буфера

    ; Копируем 4096 байт (256 символов × 16 байт)
    mov cx, 4096
    cld
    rep movsb

    ; Восстанавливаем DS = CS для нормальной работы программы
    push cs
    pop ds

    pop es
    pop ds
    pop cx
    pop si
    pop di
    ret

; Данные
default_name:   db 'dosfont.fnt', 0

; Неинициализированные данные (BSS-стиль в COM)
; Размещаем в конце, после кода

font_buffer:    times 4096 db 0
filename:       times 128 db 0
