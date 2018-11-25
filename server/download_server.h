/***************************************************************************************
 * Title: TCP Download Server Specification
 * Author: Sean Hinds
 * Date: 25 Nov, 2018
 * Description: Specification for a set of functions which impelement a TCP download 
 * 		server, which allows a client to connect via TCP and send LIST and GET 
 * 		commands. Server initializes a separate TCP connection to send data back 
 * 		to the client.
 * ************************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <dirent.h>

/* Global constants */
#define IN_BUFFER_SIZE      128
#define OUT_BUFFER_SIZE     4096
#define NUM_BUFFERS         5

#define CONNECTION_BACKLOG  10
#define SOCKETS_ALLOWED     24

#define ADDRESS_LENGTH      15
#define PORT_LENGTH         5

/* Message definitions */
#define GET_MESSAGE         "-g"
#define LIST_MESSAGE        "-l"

#define ERROR_INVALID_COMMAND   "@@ERROR_INVALID_COMMAND"
#define ERROR_BAD_FILENAME      "@@ERROR_BAD_FILENAME"
#define SERVER_KILL_MESSAGE     "@@SERVER_KILL"

#define ACK_ADDR            "@@ACK_ADDR"
#define ACK_PORT            "@@ACK_PORT"

#define GET_RES_SENTINEL    "@@GET"
#define LIST_RES_SENTINEL   "@@LIST"
#define END_DATA_MESSAGE    "@@END_DATA"

/* Global flags */
int SERVER_DISCONNECT;

/* Types */
enum client_status {
    CLIENT_DISCONNECTED = -1,
    CLIENT_VALID_CONNECTED,
    CLIENT_INVALID_CONNECTED
};

/* Global variables */
static pthread_mutex_t io_mutexes[NUM_BUFFERS];

unsigned char* in_buffers[NUM_BUFFERS];
unsigned char* out_buffers[NUM_BUFFERS];
int* sockets;
int socket_count;

/* Server setup */
void initializeServer(char*);
char* getValidPort(int, char**);

/* Server startup, runs in main thread */
int createWelcomeSocket(char*);
int bindWelcomeSocket(struct addrinfo*, struct addrinfo**, struct addrinfo**);
void startServer(int, char*);

/* worker thread main function */
void* worker_thread(void*);

/* Command connection handling in worker thread */
void establishCommandConnection(int, char*);
void ctrlLoop(int, char**, void*);
int handleClientCmd(int, char**);
void displayMessage(unsigned char*);
int validCommand(unsigned char*);

/* Data connection handling in worker thread */
int getClientDataSocketInfo(int, void*, char**);
int establishDataConnection(char**);
void sendEndData(int);

/* Get command handling */
void handleGetCmd(int, int, unsigned char*, unsigned char*);
int directoryContains(DIR*, char*);

/* List command handling */
void handleListCmd(int, int, unsigned char*, unsigned char*);
DIR* getDirectoryContents(char*);
void printDirectory(DIR*);

/* Error handling */
void sendError(int, char*, unsigned char*);
void handleInvalidCmd(int, unsigned char*, unsigned char*);
void handleBadFilename(int, char*, unsigned char*);

/* Connection termination */
void handleClientDisconnect(int);
void sendDataDisconnectToClient(int);

/* Shutdown handling */
void workerThreadComplete(int, char**, void*);
void sendKillToClient(int);
void serverTearDown();

/* IO resource sharing among thread */
int acquireFreeIOMutex();

/* Signal handling */
void sigint_intercept();
