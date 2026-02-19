#include "caesar.h"
#include <stdint.h>

// статическая переменная для хранения ключа
static unsigned char current_key = 0;

// установка ключа
EXPORT void set_key(char key)
{
    current_key = (unsigned char)key;
}

// XOR функция
EXPORT void caesar(void* src, void* dst, int len)
{
    unsigned char* s = (unsigned char*)src;
    unsigned char* d = (unsigned char*)dst;

    for (int i = 0; i < len; i++)
    {
        d[i] = s[i] ^ current_key;
    }
}
