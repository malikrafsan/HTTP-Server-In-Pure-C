Port = 8080
Backlog = 10
BufferSize = 1024

build:
	gcc *.c -o main

run:
	./main $(Port) $(Backlog) $(BufferSize)

