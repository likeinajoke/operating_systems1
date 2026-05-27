CC = gcc
CFLAGS = -Wall -Wextra -pedantic -fPIC
LDFLAGS = -shared

LIBNAME = libcaesar.so

SRC = caesar.c
OBJ = caesar.o

all: $(LIBNAME)

$(LIBNAME): $(OBJ)
	$(CC) $(LDFLAGS) -o $(LIBNAME) $(OBJ)

$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $(SRC)

install:
	cp $(LIBNAME) /usr/local/lib/

test:
	python test.py

clean:
	rm -f *.o *.dll
