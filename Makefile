build:
	gcc nextpuzzle.c -o nextpuzzle -lsqlite3 -lm

clean:
	rm nextpuzzle
