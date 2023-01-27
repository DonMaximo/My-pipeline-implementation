CC=gcc
CFLAGS=-Wall -std=c99

runpipeline: runpipeline.c
	$(CC) $(CFLAGS) -o runpipeline runpipeline.c

clean:
  rm -rf *.o runpipeline *~
