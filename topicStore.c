#include "topicStore.h"

int enqueue(int index, struct topicEntry entry, int pubID) {

	if (topicStore[index].size >= topicStore[index].length) {
		return 0;
	}

	// Update the topicEntry at the tail of the topicQueue
	int tail = topicStore[index].tail;
	gettimeofday(&topicStore[index].entries[tail].timeStamp, NULL);
	topicStore[index].entries[tail].entryNum = ++topicStore[index].entryCounter;
	topicStore[index].entries[tail].pubID = pubID;
	strcpy(topicStore[index].entries[tail].photoURL, entry.photoURL);
	strcpy(topicStore[index].entries[tail].photoCaption, entry.photoCaption);

	// increment tail, and update size
	tail = (tail + 1) % MAXENTRIES;
	topicStore[index].tail = tail;
	topicStore[index].size++;
	return 1;
}

int dequeue(int index, struct topicEntry *e, float DELTA) {
	// Goes through each topicEntry in the queue and removes any that have aged beyond DELTA

	// Queue is empty
	if (topicStore[index].size == 0) {
		return 0;
	}
	int lastEntry = (topicStore[index].tail + (MAXENTRIES - 1)) % MAXENTRIES; // index of last (newest) topicEntry in the queue
	int i = topicStore[index].head;	                              // current index into the queue
	int head = topicStore[index].head;
	struct timeval eTime, curTime;                                // timeval structs used to calculate age of current topicEntry
	gettimeofday(&curTime, NULL);
	int condition = 1;  	                                      // Condition for exiting the do-while loop
	// loop through each topicEntry
	do {
		// Calculate the age of the current topicEntry
		eTime = topicStore[index].entries[i].timeStamp;
		double age = (double)(curTime.tv_sec - eTime.tv_sec) + (double)(curTime.tv_usec - eTime.tv_usec) / 1000000;

		// If entry has aged beyond DELTA
		if (age >= DELTA) {
			struct topicEntry temp = topicStore[index].entries[head];
			printf("Topic Cleanup - Dequeueing Entry: %d from topicQueue: %d\n", temp.entryNum, topicStore[index].ID); 
			// Since head points to the oldest entry in the queue, this if statement
			// will only be executed if the current entry is the head, and will update
			// the head to the next oldest entry in the queue
			*e = topicStore[index].entries[head];
			head = (head + 1) % MAXENTRIES;
			topicStore[index].head = head;
			topicStore[index].size--;
		}
		// Make sure we stay within the bounds of head and tail
		if (i == lastEntry) {
			condition = 0;
		}
		// increment i for next iteration
		i = (i + 1) % MAXENTRIES;
	} while (condition);
	return 1;
}

int getEntry(int index, int lastEntry, struct topicEntry *t) {
	// Case 1. Topic queue is empty -> return 0
	
	if (topicStore[index].size == 0) {
		return 0;
	}
	// Check for case 2 or case 3
	int inQueue = -1;
	for (int i = topicStore[index].head, j = 0; j < topicStore[index].size; i = (i + 1) % MAXENTRIES, j++) {
		if (topicStore[index].entries[i].entryNum == lastEntry+1) {
			inQueue = i;
			break;
		}
	}

	// Case 2 - Topic Queue is not empty and lastEntry+1 is in the queue
	if (inQueue >= 0) {
		*t = topicStore[index].entries[inQueue];
		return 1;
	}
	// Case 3 - Topic Queue is not empty and lastEntry+1 is not in the queue
	else {
		for (int i = topicStore[index].head, j = 0; j != topicStore[index].size; i = (i + 1) % MAXENTRIES, j++) {
			if (topicStore[index].entries[i].entryNum > (lastEntry + 1)) {
				// Case 3(ii) - There exists an entry greater than lastEntry+1
				*t = topicStore[index].entries[i];
				return topicStore[index].entries[i].entryNum;
			}
		}
	}
	// Case 3(i) - All entries are less than lastEntry+1
	return 0;
}
