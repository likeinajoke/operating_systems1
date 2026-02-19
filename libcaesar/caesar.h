#ifndef CAESAR_H
#define CAESAR_H

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

// установить ключ
EXPORT void set_key(char key);

// XOR шифрование / дешифрование
EXPORT void caesar(void* src, void* dst, int len);

#endif
