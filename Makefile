src_files = ./src/main.c
include_files = ./include/main.h

build:
	mkdir bin
	gcc $(include_files) $(src_files) -Wall -pthread -o ./bin/server

clean:
	rm -rf ./bin/ 

run:
	./bin/server 8080
