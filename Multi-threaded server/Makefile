GCC = gcc -g -Wall -Wextra -Wpedantic -Wshadow -pthread -O2
TARGET = httpserver

all: ${TARGET}

${TARGET}: ${TARGET}.o
	${GCC} -o ${TARGET} ${TARGET}.o

${TARGET}.o: ${TARGET}.c
	${GCC} -c ${TARGET}.c

clean:
	rm -f ${TARGET}.o

spotless:
	rm -f ${TARGET} ${TARGET}.o