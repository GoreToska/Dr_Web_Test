#include "is_non_zero.h"

// Исходный код
//
//   push ebp
//   mov  ebp, esp
//   mov  eax, [ebp+8]  ; eax = value - первый аргумент
//   neg  eax           ; CF = (value != 0)
//   sbb  eax, eax      ; eax = -CF: 0xFFFFFFFF, если value != 0, иначе 0
//   neg  eax           ; eax = CF: 1 или 0
//   pop  ebp
//   ret

int IsNonZero(int value)
{
    return value != 0;
}
