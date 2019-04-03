#CFLAGS += -g -std=c11 -fsanitize=address,leak,undefined -Wall -Wextra
CFLAGS += -g -std=c11 -Wall -Wextra -Wno-unused-function
PROG := cmp
OBJ := main.o sema.o parser.o lexer.o

$(PROG): $(OBJ)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJ)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

main.o: sema.h parser.h lexer.h
sema.o: sema.h parser.h lexer.h
parser.o: parser.h lexer.h
lexer.o: lexer.h

.PHONY: clean
clean:
	rm -f *.o $(PROG)
