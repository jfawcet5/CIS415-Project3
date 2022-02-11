#include <stdio.h>
#include <stdlib.h>
#include "topicStore.h"
#include <string.h>
#include <unistd.h>
#include <pthread.h>

float DELTA;

pthread_mutex_t mutex[MAXTOPICS]; 
pthread_t publishers[NUMPROXIES];
pthread_t subscribers[NUMPROXIES];

void initialize(int index, int length, int ID, char *name) {
	// Initialize topicQueue member variables
	strcpy(topicStore[index].name, name);
	topicStore[index].size = 0;
	topicStore[index].head = 0;
	topicStore[index].tail = 0;
	topicStore[index].length = length;
	topicStore[index].entryCounter = 0;
	topicStore[index].ID = ID;
	pthread_mutex_init(&topicStore[index].mutex, NULL);

	// Initialize topicEntries with default values. These will be overwritten by enqueue()
	for (int i = 0; i < MAXENTRIES; i++) {
		topicStore[index].entries[i].entryNum = -1;
		topicStore[index].entries[i].pubID = -1;
		strcpy(topicStore[index].entries[i].photoURL, "EMPTY");
		strcpy(topicStore[index].entries[i].photoCaption, "EMPTY");
	}
}

typedef struct PubThread {
	int id, alive, available, hasWork, kill;
	pthread_t tid;
	char **qNames; // List of names of topicQueues to publish to 
	struct topicEntry *pubList; // List of topicEntry struct to be published
	char *filename[NUMPROXIES]; // Name of file to read from for part 3
	int numPubs;
} pubThread;

typedef struct SubThread {
	int id, alive, available, hasWork, kill;
	pthread_t tid;
	char **qNames;
	int lastEntries[MAXTOPICS];
	struct topicEntry temp;
	char *filename[NUMPROXIES];
	int numSubs;
} subThread;

typedef struct CleanupThread {
	pthread_t tid;
	int kill;
} cThread;

void *publisher(void *pub) {
	// Variables used for tokenization
	char *buf = NULL, *str, *token, *saveptr;
	ssize_t n = 0;

	int pubIndex = 0;

	while (!((pubThread *)pub)->kill) {
		// Wait to be assigned work
		if (((pubThread *)pub)->hasWork) {
			((pubThread *)pub)->available = 0;

			// Open the given publisher file
			FILE *fp = fopen(((pubThread *)pub)->filename[pubIndex], "r");
			// If unable to open file then there are no commands to be executed so 
			// proxy thread must wait to be reassigned work
			if (fp == NULL) {
				printf("Error! No such file or directory: %s\n", ((pubThread *)pub)->filename[pubIndex]);
				((pubThread *)pub)->numPubs--;
				if (((pubThread *)pub)->numPubs <= 0) {
					pthread_exit(0);
				}
				else {
					continue;
				}
			}

			// Read each line of the file using getline()
			while (getline(&buf, &n, fp) != -1) {

				// Remove newline
				int newline = strcspn(buf, "\n");
				buf[newline] = '\0';

				str = buf;
				// Read the first word in the line. This will be the command to be executed
				token = strtok_r(str, " ", &saveptr);

				if (!strcmp(token, "put")) { // If the command is 'put'
					// Read the second word in the line. This will be the topic ID to enqueue to.
					// then convert to an integer and store in variable 'ID'
					char *topID = strtok_r(NULL, " ", &saveptr);
					char *temp;
					int ID = strtol(topID, &temp, 10);

					// Read the URL and Caption to enqueue
					char *URL = strtok_r(NULL, " ", &saveptr);
					char *CAP = strtok_r(NULL, " ", &saveptr);

					// Create a temporary topicEntry struct for enqueueing
					struct topicEntry entry = {.pubID = ((pubThread *)pub)->id};
					strcpy(entry.photoURL, URL);
					strcpy(entry.photoCaption, CAP);

					// Find the index of the topicQueue specified by ID
					int index = -1;
					for (int i = 0; i < MAXTOPICS; i++) {
						if (ID == topicStore[i].ID) {
							index = i;
							break;
						}
					}
					// If topicQueue was not found then we continue to the next instruction
					if (index == -1) {
						continue;
					}


					int res = 0;

					// Attempt to enqueue. If enqueue returns 0 then the topicQueue is full. We will continue
					// to try to enqueue until the topic_cleanup thread has made space in the appropriate topicQueue
					while (!res) {
						pthread_mutex_lock(&topicStore[index].mutex);
						res = enqueue(index, entry, ((pubThread *)pub)->id);
						pthread_mutex_unlock(&topicStore[index].mutex);
						if (!res) {
							sched_yield();
						}
					}
					printf("Proxy thread %d - type: Publisher  - Executed command: ", ((pubThread *)pub)->id);
					printf("%s %d %s %s\n", token, ID, URL, CAP);
					
				}
				else if (!strcmp(token, "sleep")) { // If command is 'sleep'
					// Read the next word in the line. This will be the number of milliseconds to sleep for
					char *sec = strtok_r(NULL, " ", &saveptr);
					
					// Convert string to integer
					char *ptr;
					int milisec = (unsigned long int)strtol(sec, &ptr, 10);
					
					// Convert the milliseconds into seconds and nanoseconds for the nanosleep() function
					int seconds = milisec / 1000;
					int nsec;

					if (milisec >= 1000) {
						nsec = (milisec - (seconds * 1000)) * 1000000;
					}
					else {
						nsec = milisec * 1000000;
					}
					struct timespec t1, t2;
					t1.tv_sec = seconds;
					t1.tv_nsec = nsec;
					nanosleep(&t1, &t2);
					printf("Proxy thread %d - type: Publisher  - Executed command: %s %s\n", ((pubThread *)pub)->id, token, sec);
				}
				else if (!strcmp(token, "stop")) { // If command is 'stop'
					// Close the file and free the memory allocated for the file name. Then the proxy thread either
					// moves on to the next filename or is finished executing
					fclose(fp);
					free( ((pubThread *)pub)->filename[pubIndex] );

					if (pubIndex == ( ((pubThread *)pub)->numPubs - 1 )) {
						((pubThread *)pub)->hasWork = 0;
						((pubThread *)pub)->available = 1;
						((pubThread *)pub)->kill = 1;
						printf("Proxy thread %d - type: Publisher  - Executed command: %s\n", ((pubThread *)pub)->id, token);
						break;
					}
					else {
						pubIndex = (pubIndex + 1) % NUMPROXIES;
						printf("Proxy thread %d - type: Publisher  - Executed command: %s\n", ((pubThread *)pub)->id, token);
						continue;
					}
				}
			}
		}
	}
	// Free memory allocated by getline()
	free(buf);
}

void *subscriber(void *sub) {
	// Variables used for tokenization
	char *buf = NULL, *token, *str, *saveptr;
	ssize_t n = 0;

	int subIndex = 0;

	while (!((subThread *)sub)->kill) {
		// Wait to be assigned work
		if (((subThread *)sub)->hasWork) {
			((subThread *)sub)->available = 0;

			// Open the given subscriber file
			FILE *fp = fopen(((subThread *)sub)->filename[subIndex], "r");
			// If we are unable to open the file then we go back to waiting for work
			if (fp == NULL) {
				printf("Error! No such file or directory: %s\n", ((subThread *)sub)->filename[subIndex]);
				((subThread *)sub)->numSubs--;
				if (((subThread *)sub)->numSubs <= 0) {
					((subThread *)sub)->kill = 1;
					pthread_exit(0);
				}
				else {
					continue;
				}
			}

			// Read each line of the file
			while (getline(&buf, &n, fp) != -1) {

				// remove newline from current line
				int newline = strcspn(buf, "\n");
				buf[newline] = '\0';

				str = buf;

				// Read the first word in the line. This will be the command to execute
				token = strtok_r(str, " ", &saveptr);

				if (!strcmp(token, "get")) { // If command is 'get'
					
					// Read the next word which is the topicQueue to read from 
					char *ptr;
					char *temp = strtok_r(NULL, " ", &saveptr);
					// Convert string to integer
					int ID = strtol(temp, &ptr, 10);

					// Find the index of the topicQueue specified by ID
					int index = -1;
					for (int i = 0; i < MAXTOPICS; i++) {
						if (ID == topicStore[i].ID) {
							index = i;
							break;
						}
					}
					// If topicQueue with topicQueue.ID == ID was not found then we move on to next command
					if (index == -1) {
						continue;
					}

					// Attempt to get entry 1000 times before giving up
					int fail = 0;
					int ret = getEntry(index, ((subThread *)sub)->lastEntries[index], &((subThread *)sub)->temp);

					while (ret == 0) {
						if (++fail >= 1000) {
							printf("Proxy thread %d - type: Subscriber - Failed to execute command: ", ((subThread *)sub)->id);
							printf("%s %s - No new entries to read\n", token, temp);
							break;
						}
						pthread_mutex_lock(&topicStore[index].mutex);
						ret = getEntry(index, ((subThread *)sub)->lastEntries[index], &((subThread *)sub)->temp);
						pthread_mutex_unlock(&topicStore[index].mutex);
						sched_yield();
					}
					if (ret != 0) { // If return value from getEntry != 0 then we had a successful read
						struct topicEntry t = ((subThread *)sub)->temp;
						// Print appropriate message
						printf("Proxy thread %d - type: Subscriber - Executed command: %s %s ", ((subThread *)sub)->id, token, temp);
						printf("- Result: entryNum: %d, URL: %s, Caption: %s\n", t.entryNum, t.photoURL, t.photoCaption);
						// update the appropriate lastEntry for the next time we want to read from the topicQueue 
						// specified by the current ID
						if (ret == 1) {
							((subThread *)sub)->lastEntries[index]++;
						}
						else {
							((subThread *)sub)->lastEntries[index] = ret;
						}
					}
				}
				else if (!strcmp(token, "sleep")) { // If command is 'sleep'

					// Read the number of milliseconds to sleep for
					char *sec = strtok_r(NULL, " ", &saveptr);

					char *ptr;
					// Convert string to integer
					int milisec = (unsigned long int)strtol(sec, &ptr, 10);
					
					// Convert milliseconds into seconds and nanoseconds for the nanosleep() function
					int seconds = milisec / 1000;
					int nsec;
					if (milisec >= 1000) {
						nsec = (milisec - (seconds * 1000)) * 1000000;
					}
					else {
						nsec = milisec * 1000000;
					}
					struct timespec t1, t2;
					t1.tv_sec = seconds;
					t1.tv_nsec = nsec;
					nanosleep(&t1, &t2);
					printf("Proxy thread %d - type: Subscriber - Executed command: %s %s\n", ((subThread *)sub)->id, token, sec);
				}
				else if (!strcmp(token, "stop")) { // If command is 'stop'
					// Close the file and free the memory allocated for the file name. Then the proxy thread either
					// moves on to the next filename or is finished executing
					fclose(fp);
					free( ((subThread *)sub)->filename[subIndex] );

					if (subIndex == ( ((subThread *)sub)->numSubs - 1 )) {
						
						((subThread *)sub)->hasWork = 0;
						((subThread *)sub)->available = 1;
						((subThread *)sub)->kill = 1;
						printf("Proxy thread %d - type: Subscriber - Executed command: %s\n", ((subThread *)sub)->id, token);
						break;
					}
					else {
						subIndex = (subIndex + 1) % NUMPROXIES;
						printf("Proxy thread %d - type: Subscriber - Executed command: %s\n", ((subThread *)sub)->id, token);
						continue;
					}
				}
			}
		}
	}
	// Free memory allocated by getline()
	free(buf);
}

void *topic_cleanup(void *cthread) {
	printf("Topic Cleanup thread startup\n");
	struct topicEntry temp;
	int index = 0;
	// While thread is alive we are going to iterate through the topicStore and dequeue entries
	// from the topicQueues that have aged beyond DELTA
	while (!((cThread *)cthread)->kill) {
		pthread_mutex_lock(&topicStore[index].mutex);
		dequeue(index, &temp, DELTA);
		pthread_mutex_unlock(&topicStore[index].mutex);
		index = (index + 1) % MAXTOPICS;
		sched_yield();
	}
}

int match(char *string) {
	// Used in main to match 'string' to a command for a switch statement
	char *commands[] = {"create", "query", "add", "delta", "start"};
	for (int i = 0; i < 5; i++) {
		if (!strcmp(string, commands[i])) {
			return i;
		}
	}
	return -1;
}

int main(int argc, char *argv[]) {

	// Initialize the topicStore with default values
	for (int i = 0; i < MAXTOPICS; i++) {
		initialize(i, 0, -1, "EMPTY");
	}

	// Initialize delta with a default value
	DELTA = 5;

	// Create publisher thread pool
	printf("Creating publisher thread pool\n");
	pubThread pubs[NUMPROXIES];

	for (int i = 0; i < NUMPROXIES; i++) {
		pubs[i].id = 2*i;
		pubs[i].alive = pubs[i].available = 1;
		pubs[i].hasWork = pubs[i].kill = 0;
		//strcpy(pubs[i].filename, "EMPTY");
		pubs[i].pubList = NULL;
		pubs[i].qNames = NULL;
		pubs[i].numPubs = 0;

		pthread_create(&pubs[i].tid, NULL, publisher, (void *) &pubs[i]);
	}

	// Create subscriber thread pool
	printf("\nCreating subscriber thread pool\n");
	subThread subs[NUMPROXIES];

	for (int i = 0; i < NUMPROXIES; i++) {
		subs[i].id = 2*i + 1;
		subs[i].alive = subs[i].available = 1;
		subs[i].hasWork = subs[i].kill = 0;
		//strcpy(subs[i].filename, "EMPTY");
		subs[i].qNames = NULL;
		subs[i].numSubs = 0;

		for (int j = 0; j < MAXTOPICS; j++) {
			subs[i].lastEntries[j] = 0;
		}

		pthread_create(&subs[i].tid, NULL, subscriber, (void *) &subs[i]);
	}

	// If file is not specified then program will default to stdin
	FILE *stream = stdin;

	// Check if input file was provided
	if (argc > 1) {
		stream = fopen(argv[1], "r");
		// If the given file could not be opened
		if (stream == NULL) {
			printf("Error! No such file or directory - %s\n", argv[1]);
			return 0;
		}
	}

	int start = 0; // Used to exit the while loop if the start command is specified in standard input
	int topicCtr = 0; // used as index into topicStore and counter of topics added to topicStore
	int pubCtr = 0; // used as index into publisher thread pool and counter of allocated publisher threads
	int subCtr = 0; // used as index into subscriber thread pool and counter of allocated subscriber threads
	char *buf = NULL, *str, *token, *saveptr, *saveptr2; // used for reading and tokenizing standard input
	ssize_t n = 0;

	// Iterate through input file or stdin
	while (getline(&buf, &n, stream) > 1) {
		int newline = strcspn(buf, "\n");
		buf[newline] = '\0';
		
		str = buf;

		// Read the type of command (create, add, query, etc...)
		token = strtok_r(str, " ", &saveptr);

		// Interpret the command
		int c = match(token);

		// Execute the command
		switch (c) {
			// No match
			case -1:
				printf("Error! Unknown method: %s\n", token);
				return 1;
			// Create: create topic <topic ID> '<topic name>' <queue length>
			case 0:
				// Skip the word 'topic'
				strtok_r(NULL, " ", &saveptr);

				// store <topic ID> in int topicID
				char *ptr;
				char *temp1 = strtok_r(NULL, " ", &saveptr);
				int topicID = strtol(temp1, &ptr, 10);

				printf("\nCreating topicQueue: %d\n", topicID);
				// Remove quotations from '<topic name>' and store in char *topicName
				char *tname = strtok_r(NULL, " ", &saveptr);
				char *topicName = strtok_r(tname, "\"", &saveptr2);
				
				// Store <queue length> in int length
				temp1 = strtok_r(NULL, " ", &saveptr);
				int length = strtol(temp1, &ptr, 10);

				//printf("topicID: %d, topicName: %s, length: %d\n", topicID, topicName, length);
				// Create the topic
				topicStore[topicCtr].length = length;
				topicStore[topicCtr].ID = topicID;
				strcpy(topicStore[topicCtr].name, topicName);
				topicCtr++;
				break;
			// Query: query <topics/publishers/subscribers>
			case 1: ;
				// Name of what we are going to query
				char *query = strtok_r(NULL, " ", &saveptr);
				printf("\nQuery %s:\n", query);

				if (!strcmp(query, "topics")) {
					// print all topicIDs and lengths
					for (int i = 0; i < topicCtr; i++) {
						printf("Topic: %d - Length: %d\n", topicStore[i].ID, topicStore[i].length);
					}
				}
				else if (!strcmp(query, "publishers")) {
					// pruntf out all current publisher and command file names
					for (int i = 0; i < NUMPROXIES; i++) {
						int numPubs = pubs[i].numPubs;
						if (numPubs > 0) {
							for (int j = 0; j < numPubs; j++) {
								printf("Publisher: %d - Proxy thread ID: %d, Command file: %s\n", i, pubs[i].id, pubs[i].filename[j]);
							}
						}
					}
				}
				else if (!strcmp(query, "subscribers")) {
					// printf out all current subscribers and their command file names
					for (int i = 0; i < NUMPROXIES; i++) {
						int numSubs = subs[i].numSubs;
						if (numSubs > 0) {
							for (int j = 0; j < numSubs; j++) {
								printf("Subscriber: %d - Proxy thread ID: %d, Command file: %s\n", i, subs[i].id, subs[i].filename[j]);
							}
						}
					}
				}
				else {
					// Print error message
				}
				break;
			// Add: add <publisher/subscriber> '<sub/pub command file>'
			case 2: ;
				// store the name of type of thread in char *thread
				char *thread = strtok_r(NULL, " ", &saveptr);

				// Remove quotations from '<command file>' and
				// store in char *filename
				char *f1 = strtok_r(NULL, " ", &saveptr);
				char *f2 = strtok_r(f1, "\"", &saveptr2);

				int len = strlen(f2) + 1;
				char *filename = malloc(sizeof(char) * len);
				strcpy(filename, f2);
				
				printf("\nAdding %s: %s\n", thread, filename);

				if (!strcmp(thread, "publisher")) {
					// Assign publisher thread
					int findex = pubs[pubCtr].numPubs++;
					pubs[pubCtr].available = 0;
					//strcpy(pubs[pubCtr].filename[findex], filename);
					pubs[pubCtr].filename[findex] = filename;
					pubCtr = (pubCtr + 1) % NUMPROXIES;
				}
				else if (!strcmp(thread, "subscriber")) {
					// Assign subscriber thread
					int findex = subs[subCtr].numSubs++;
					subs[subCtr].available = 0;
					//strcpy(subs[subCtr].filename[findex], filename);
					subs[subCtr].filename[findex] = filename;
					subCtr = (subCtr + 1) % NUMPROXIES;
				}
				else {
					// Print error message
				}
				break;
			// Delta: delta <DELTA>
			case 3: ;
				// store value of DELTA in temp2
				char *temp2 = strtok_r(NULL, " ", &saveptr);
				
				printf("\nSetting delta = %s\n", temp2);
				// Convert to float type
				char *ptr1;
				float newDelta = strtof(temp2, &ptr);

				// Assign new DELTA
				DELTA = newDelta;
				break;
			// Start: start
			case 4:
				printf("\nStarting all publishers and subscribers\n\n");
				start = 1;
				break;
		}
		// Exit the while loop and start all of the publishers and subscribers
		if (start) {
			break;
		}

	}

	// Start the topic Cleanup thread
	cThread tc = {.kill = 0};
	pthread_create(&tc.tid, NULL, topic_cleanup, (void *) &tc);


	// Start the publisher threads
	for (int i = 0; i < pubCtr; i++) {
		pubs[i].hasWork = 1;
	}
	
	// Start the subscriber threads
	for (int i = 0; i < subCtr; i++) {
		subs[i].hasWork = 1;
	}

	// Wait for threads to finish execution
	for (int i = 0; i < NUMPROXIES; i++) {
		if (i >= pubCtr) {
			pubs[i].kill = 1;
		}
		pthread_join(pubs[i].tid, NULL);
	}

	for (int i = 0; i < NUMPROXIES; i++) {
		if (i >= subCtr) {
			subs[i].kill = 1;
		}
		pthread_join(subs[i].tid, NULL);
	}

	// Stop the topic_cleanup thread
	tc.kill = 1;
	pthread_join(tc.tid, NULL);

	// Free memory
	if (argc > 1) {
		fclose(stream);
	}
	free(buf);

	return 0;
}

