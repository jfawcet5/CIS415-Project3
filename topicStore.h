/* Header file for the topic store
 */

#ifndef TOPIC_STORE_H
#define TOPIC_STORE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <pthread.h>
// include time.h

#define MAXNAME 20
#define MAXENTRIES 11
#define MAXTOPICS 7
#define URLSIZE 30
#define CAPSIZE 30
#define NUMPROXIES 5

struct topicEntry {
	int entryNum;                // entry number
	struct timeval timeStamp;    // Time stamp
	int pubID;                   // Publisher ID ?
	char photoURL[URLSIZE];      // Photo URL
	char photoCaption[CAPSIZE];  // Photo Caption
};

struct topicQueue {
	struct topicEntry entries[MAXENTRIES];          // circular buffer of topicEntrys
	char name[MAXNAME];                          // Name of topic queue
	int size, head, tail, entryCounter, length, ID;  // size of topicQueue, head pointer, tail pointer, topicEntry counter (monotonically increasing)
	pthread_mutex_t mutex;
};

struct topicQueue topicStore[MAXTOPICS];

int enqueue(int topID, struct topicEntry entry, int pubID);

int dequeue(int index, struct topicEntry *e, float DELTA);

int getEntry(int index, int lastEntry, struct topicEntry *t);
#endif
