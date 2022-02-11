OBJECTS=quacker.o topicStore.o part4.o part3.o part2.o part1.o
PROGRAMS=server part4 part3 part2 part1

server: topicStore.o quacker.o
	gcc -o server $^ -lpthread
part4: topicStore.o part4.o
	gcc -o part4 $^ -lpthread
part3: topicStore.o part3.o
	gcc -o part3 $^ -lpthread
part2: topicStore.o part2.o
	gcc -o part2 $^ -lpthread
part1: topicStore.o part1.o
	gcc -o part1 $^ -lpthread

quacker.o: quacker.c
topicStore.o: topicStore.c topicStore.h
part4.o: part4.c
part3.o: part3.c
part2.o: part2.c
part1.o: part1.c

clean:
	rm -f $(OBJECTS) $(PROGRAMS)
