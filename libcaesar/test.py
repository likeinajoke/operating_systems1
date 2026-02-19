import ctypes

# загрузка библиотеки
lib = ctypes.CDLL("./libcaesar.dll")

# описание функций
lib.set_key.argtypes = [ctypes.c_char]
lib.set_key.restype = None

lib.caesar.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_int]
lib.caesar.restype = None

# исходные данные
data = bytearray(b"hello world")

print("Original:", data)

# установить ключ
lib.set_key(b'K')

# создать буферы ctypes
src = (ctypes.c_ubyte * len(data)).from_buffer(data)
encrypted = (ctypes.c_ubyte * len(data))()

# шифрование
lib.caesar(src, encrypted, len(data))

print("Encrypted:", bytes(encrypted))

# дешифрование
decrypted = (ctypes.c_ubyte * len(data))()

lib.caesar(encrypted, decrypted, len(data))

print("Decrypted:", bytes(decrypted))
