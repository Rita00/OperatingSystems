projeto: main.o listas_voos.o
	gcc -Wall -Wextra -pthread -D_REENTRANT -o projeto main.o listas_voos.o

main.o: main.c structs.h headers.h
	gcc -Wall -Wextra -pthread -D_REENTRANT -c main.c -o main.o

listas_voos.o: listas_voos.c structs.h headers.h
	gcc -Wall -Wextra -pthread -D_REENTRANT -c listas_voos.c -o listas_voos.o
