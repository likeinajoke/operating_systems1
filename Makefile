CC = gcc

CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -Llibcaesar -lcaesar

TARGET = secure_copy.exe
OBJ = secure_copy.o

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $(TARGET)
	cp libcaesar/libcaesar.dll .

secure_copy.o: secure_copy.c buffer.h libcaesar/caesar.h
	$(CC) $(CFLAGS) -c secure_copy.c -o secure_copy.o

clean:
	rm -f *.o *.exe *.dll output.txt