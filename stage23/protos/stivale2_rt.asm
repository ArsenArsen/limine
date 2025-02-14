section .bss

user_stack:
    resq 1

user_cs: resq 1
user_ds: resq 1
user_es: resq 1
user_ss: resq 1

section .text

extern term_write
extern stivale2_rt_stack
extern stivale2_term_callback_ptr

global stivale2_term_callback
stivale2_term_callback:
bits 32
    push ebp
    mov ebp, esp

    push ebx
    push esi
    push edi

    ; Go 64
    push 0x28
    push .mode64
    retfd
bits 64
  .mode64:
    mov eax, 0x30
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rdi, [rbp + 8]
    mov rsi, [rbp + 16]
    mov rdx, [rbp + 24]
    mov rcx, [rbp + 32]

    mov rbx, rsp
    mov rsp, [user_stack]
    call [stivale2_term_callback_ptr]
    mov rsp, rbx

    ; Go 32
    push 0x18
    push .mode32
    retfq
bits 32
  .mode32:
    mov eax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    pop edi
    pop esi
    pop ebx
    pop ebp

    ret

global stivale2_term_write_entry
stivale2_term_write_entry:
bits 64
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15

    mov [user_stack], rsp
    mov esp, [stivale2_rt_stack]

    mov word [user_cs], cs
    mov word [user_ds], ds
    mov word [user_es], es
    mov word [user_ss], ss

    push rsi
    push rdi

    push 0x18
    push .mode32
    retfq
bits 32
  .mode32:
    mov eax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    call term_write
    add esp, 16

    push dword [user_cs]
    push .mode64
    retfd
bits 64
  .mode64:
    mov ds, word [user_ds]
    mov es, word [user_es]
    mov ss, word [user_ss]
    mov rsp, [user_stack]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx

    ret
