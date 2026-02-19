CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

LIBNAME = libcaesar.dll

SRC = caesar.c
OBJ = caesar.o

all: $(LIBNAME)

$(LIBNAME): $(OBJ)
	$(CC) $(LDFLAGS) -o $(LIBNAME) $(OBJ)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC)

install:
	cp $(LIBNAME) /mingw64/bin/

test:
	python test.py

clean:
	rm -f *.o *.dll
