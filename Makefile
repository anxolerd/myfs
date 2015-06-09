FLAGS = -Wall `pkg-config fuse --cflags --libs`

all: test bin/myfs

clean:
	rm -rf bin test

bin:
	mkdir -p bin/

bin/myfs: bin src/myfs.c
	$(CC) $(FLAGS) -g src/myfs.c -o bin/myfs

test:
	mkdir -p test/mnt

