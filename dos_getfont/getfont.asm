; Программа для DOS (NASM)
; Сохраняет текущий шрифт VGA (8x16) в файл dosfont.fnt

bits 16
org 0x100

section .data
filename db 'dosfont.fnt', 0

section .bss
font_buffer resb 4096

section .text
start:
    call get_vga_font

    ; Создаём файл
    mov ah, 0x3C
    xor cx, cx
    mov dx, filename
    int 0x21
    jc error_exit
    mov bx, ax

    ; Записываем буфер в файл
    mov ah, 0x40
    mov cx, 4096
    mov dx, font_buffer
    int 0x21
    jc error_exit

    ; Закрываем файл
    mov ah, 0x3E
    int 0x21

    mov ax, 0x4C00
    int 0x21

error_exit:
    mov ax, 0x4C01
    int 0x21

get_vga_font:
    push es
    push di
    push si
    push cx
    push ds

    ; Получаем указатель на шрифт (ES:BP)
    mov ah, 0x11
    mov al, 0x30
    mov bh, 0x06
    int 0x10

    ; Меняем местами: источник = ES:BP, назначение = CS:font_buffer
    push es
    pop ds          ; DS = ES (источник)
    mov si, bp      ; SI = BP

    push cs
    pop es          ; ES = CS (назначение)
    mov di, font_buffer

    ; Копируем 4096 байт
    mov cx, 4096
    cld
    rep movsb

    pop ds
    pop cx
    pop si
    pop di
    pop es
    ret
