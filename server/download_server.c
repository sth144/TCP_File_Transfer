/********************************************************************************************
 * Title: TCP download server implementation
 * Author: Sean Hinds
 * Date: 25 Nov, 2018
 * Description: Implementation for TCP download server. Server listens for connections on a
 * 		specified port. When a connection is received, a new thread is spawned to 
 * 		manage that connection. Handles LIST (-l) and GET (-g FILENAME) commands from
 * 		client. When a valid command is recieved, a separate data connection is 
 * 		requested by the server to transfer the file or directory contents.
 * *****************************************************************************************/

#include "download_server.h"

/************************************* Server setup ****************************************/

/* called to initialize the server */
void initializeServer(char* port_str)
{
    SERVER_DISCONNECT = 0;

    /* initialize mutexes and allocate buffers*/
    for (int i = 0; i < NUM_BUFFERS; i++) {
        pthread_mutex_init(&io_mutexes[i], NULL);
        in_buffers[i] = (unsigned char*) malloc(IN_BUFFER_SIZE * sizeof(char));
        out_buffers[i] = (unsigned char*) malloc(OUT_BUFFER_SIZE * sizeof(char));
    } 

    sockets = malloc(SOCKETS_ALLOWED * sizeof(int));

    for (int i = 0; i < SOCKETS_ALLOWED; i++) {
        sockets[i] = -1;
    }
    int server_welcome_fd = createWelcomeSocket(port_str);

    startServer(server_welcome_fd, port_str);
}

/* parse command line arguments to ensure a valid port number selected */
char* getValidPort(int argc, char** argv)
{   
    if (argc < 2) {
        fprintf(stderr, "Usage: $ ./server {PORT}\n");
        exit(1);
    }
    int valid = 0;
    char* port_str = argv[1];
    int port = atoi(port_str);
    if (port_str[0] == '0' || port != 0) {
        return port_str;
    } else {
        fprintf(stderr, "Please enter a valid port number\n");
        exit(1);
    }
}

/************************************* Server startup, runs in main thread ************************************/

/* initialize the welcome socket for incoming control connections */
int createWelcomeSocket(char* port_str)
{ 
    struct addrinfo hints, *server_addrinfo, *ptr_addrinfo;
    int return_value;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((return_value = getaddrinfo(NULL, port_str, &hints, &server_addrinfo)) != 0) {
        fprintf(stderr, "Error getting address info\n");
        exit(1);
    }
    
    int server_welcome_fd = bindWelcomeSocket(&hints, &server_addrinfo, &ptr_addrinfo); 
    return server_welcome_fd;
}

/* Bind welcome socket for incoming control connections to port */
int bindWelcomeSocket(struct addrinfo* hints, struct addrinfo** server_addrinfo, struct addrinfo** ptr_addrinfo)
{
    int server_welcome_fd, optval = 1;
    for (ptr_addrinfo = server_addrinfo; 
            ptr_addrinfo != NULL; 
            *ptr_addrinfo = (*ptr_addrinfo)->ai_next) {
        if ((server_welcome_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
            continue;
        } 
        if (setsockopt(server_welcome_fd, 
                        SOL_SOCKET, 
                        SO_REUSEADDR, 	// free port immediately on close()
                        &optval, sizeof(int)) == -1) {
            fprintf(stderr, "setsockopt() failed\n");
            exit(1);
        }
        if (bind(server_welcome_fd, 
                    (*ptr_addrinfo)->ai_addr, 
                    (*ptr_addrinfo)->ai_addrlen) == -1) {
            close(server_welcome_fd);
            fprintf(stderr, "Failure to bind, try another port\n");  
            exit(1); 
            continue;
        }
        break;
    }
    freeaddrinfo(*server_addrinfo);
    if (ptr_addrinfo == NULL) {
        fprintf(stderr, "Socket failed\n");
        exit(1);
    }
    return server_welcome_fd;
}

/* begin listening for control connection requests from client */
void startServer(int server_welcome_fd, char* port_str)
{ 
    if (listen(server_welcome_fd, CONNECTION_BACKLOG) == -1) {
        fprintf(stderr, "Socket failed to listen\n");
        exit(1);
    }

    establishCommandConnection(server_welcome_fd, port_str);
}

/* worker thread main function. Services a single client */
void* worker_thread(void* arg)
{
    pthread_detach(pthread_self());
    int worker_cmd_fd = *((int*) arg);
    
    /* get data socket info */
    char** client_data_socket_info = malloc(2 * sizeof(char*));
    client_data_socket_info[0] = malloc(ADDRESS_LENGTH * sizeof(char));
    client_data_socket_info[1] = malloc(PORT_LENGTH * sizeof(char));
    memset(client_data_socket_info[0], 0, ADDRESS_LENGTH);
    memset(client_data_socket_info[1], 0, PORT_LENGTH); 
    int result = getClientDataSocketInfo(worker_cmd_fd, arg, client_data_socket_info);

    if (result == -1) {
        printf("Failed to get client data socket info\n");
        workerThreadComplete(worker_cmd_fd, NULL, arg);
    }

    char* client_data_addr = client_data_socket_info[0];
    char* client_data_port = client_data_socket_info[1];

    printf("Worker thread connected to client %s data port: %s\n", client_data_addr, client_data_port);

    /* launch control loop to process commands */
    ctrlLoop(worker_cmd_fd, client_data_socket_info, arg); 

    /* complete execution */
    workerThreadComplete(worker_cmd_fd, client_data_socket_info, arg);
    return (void*) 0;
}

/*************************** Command connection handling in worker thread *********************************/

/* establish the command connection */
void establishCommandConnection(int server_welcome_fd, char* port_str)
{
    socklen_t sin_size;
    struct sockaddr_storage client_addr;
    int server_cmd_fd;
    pthread_t worker;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
 
    fd_set read_fds;

    int printPrompt;

    while(!SERVER_DISCONNECT) {

        if(printPrompt) {
            printf("Server listening on %s\n", port_str);
            printPrompt = 0;
        }

        FD_ZERO(&read_fds);
        FD_SET(server_welcome_fd, &read_fds);
        int connections_requested = select(server_welcome_fd+1, &read_fds, NULL, NULL, &tv);

        if (FD_ISSET(server_welcome_fd, &read_fds) && connections_requested > 0) {
            sin_size = sizeof(client_addr);
            server_cmd_fd = accept(server_welcome_fd, (struct sockaddr *) &client_addr, &sin_size);
            if (server_cmd_fd == -1) {
                continue;
            }

            /* spawn pthread for new client */
            int* worker_cmd_fd_ptr = malloc(sizeof(server_cmd_fd));
            *worker_cmd_fd_ptr = server_cmd_fd;
            if (pthread_create(&worker, NULL, worker_thread, worker_cmd_fd_ptr) == 0) {
                //printf("Worker thread spawned\n");
                printPrompt = 1;
            } else {
                fprintf(stderr, "Failed to create worker thread\n");
            }
        }
    }
    serverTearDown();
} 

/* Called within worker thread to begin processing incoming commands */
void ctrlLoop(int worker_cmd_fd, char** client_data_socket_info, void* arg)
{
    int connection_status = 0;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
 
    fd_set read_fds;

    while (connection_status != CLIENT_DISCONNECTED && !SERVER_DISCONNECT)
    {
        FD_ZERO(&read_fds);
        FD_SET(worker_cmd_fd, &read_fds);
        select(worker_cmd_fd+1, &read_fds, NULL, NULL, &tv);
        if (FD_ISSET(worker_cmd_fd, &read_fds)) {		// if a command has been received
            connection_status = handleClientCmd(worker_cmd_fd, client_data_socket_info);
        } 
    }
    if (connection_status != CLIENT_DISCONNECTED)
    {
        //printf("Worker thread sending kill message to client\n");
        sendKillToClient(worker_cmd_fd);
    }
}

/* called by ctrlLoop() (worker thread) to handle a single command */
int handleClientCmd(int worker_cmd_fd, char** client_data_socket_info)
{ 
    /* acquire a free io mutex */
    int mutex_idx = acquireFreeIOMutex(); 

    if (mutex_idx != -1) {
        /* already memset to 0 by acquireFreeIOMutex */
        unsigned char* in_buffer = in_buffers[mutex_idx];
        unsigned char* out_buffer = out_buffers[mutex_idx];

        int retval;

        recv(worker_cmd_fd, in_buffer, IN_BUFFER_SIZE, 0);
        
        if (strcmp((char*) in_buffer, "\0") == 0) {
            handleClientDisconnect(worker_cmd_fd);
            retval = -1;
        } else if (validCommand(in_buffer)) {
	    printf("Command received: %s\n", in_buffer);
            int worker_data_fd = establishDataConnection(client_data_socket_info); 
            if (worker_data_fd != -1) {
                /* parse args  */
                if (strncmp((char*) in_buffer, GET_MESSAGE, 2)   == 0)       
                    handleGetCmd(worker_data_fd, worker_cmd_fd, in_buffer, out_buffer);
                else if (strncmp((char*) in_buffer, LIST_MESSAGE, 2) == 0)  
		    printf("handling list\n");
                    handleListCmd(worker_data_fd, worker_cmd_fd, in_buffer, out_buffer);
            }
            close(worker_data_fd);
            retval = 0;
        } else {
	    /* invalid command received */
            handleInvalidCmd(worker_cmd_fd, in_buffer, out_buffer);
            retval = 1;
        }
        pthread_mutex_unlock(&io_mutexes[mutex_idx]);
        //printf("Worker unlocked mutex %d\n", mutex_idx);
        return retval;
    } else {
        fprintf(stderr, "No IO mutex available\n");
        return 1;
    }
} 

/* returns 0 if an invalid command received, 1 if a valid command received */
int validCommand(unsigned char* command)
{
    if (strncmp((char*) command, GET_MESSAGE, 2) == 0 ||
        strncmp((char*) command, LIST_MESSAGE, 2) == 0) {
            return 1; 
    } else {
        return 0;
    }
} 

/**************************** Data connection handling in worker thread **************************************/

int getClientDataSocketInfo(int worker_cmd_fd, void* arg, char** dest) 
{
    int mutex_idx = acquireFreeIOMutex();
    unsigned char* in_buffer = in_buffers[mutex_idx];
    if (mutex_idx != -1) {
	
	/* get data address and port from client */
        if (recv(worker_cmd_fd, in_buffer, IN_BUFFER_SIZE, 0) == -1) return -1;
        char* client_data_addr = malloc(strlen((char*) in_buffer));
        memset(client_data_addr, 0, strlen((char*) in_buffer));
        strcpy(client_data_addr, (char*) in_buffer);
        memset(in_buffer, 0, IN_BUFFER_SIZE);

        if (send(worker_cmd_fd, ACK_ADDR, strlen(ACK_ADDR), 0) == -1) return -1;

        if (recv(worker_cmd_fd, in_buffer, IN_BUFFER_SIZE, 0) == -1) return -1;
        char* client_data_port = malloc(strlen((char*) in_buffer));
        memset(client_data_port, 0, strlen((char*) in_buffer));
        strcpy(client_data_port, (char*) in_buffer);

        if (send(worker_cmd_fd, ACK_PORT, strlen(ACK_PORT), 0) == -1) return -1;

        //printf("Received data socket info from client: %s:%s\n", client_data_addr, client_data_port);

        pthread_mutex_unlock(&io_mutexes[mutex_idx]);

	/* store results in dest */
        dest[0] = client_data_addr;
	dest[1] = client_data_port;
		
        return 0;	// success
    } else {
        fprintf(stderr, "Couldn't get client data socket info\n");
        return -1;
    }
}

/* establish the data connection to transfer directory contents or file to client */
int establishDataConnection(char** client_data_socket_info) 
{ 
    int server_data_fd = 0;

    int rv;
    struct addrinfo hints, *server_info, *address_ptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    //printf("Getting dynamic IP address info for data socket %s:%s\n", client_data_socket_info[0], client_data_socket_info[1]);

    if ((rv = getaddrinfo(client_data_socket_info[0], 
                            client_data_socket_info[1], 
                            &hints, &server_info)) != 0) {
        fprintf(stderr, "Failed to get resolve dynamic IP address\n");
        return -1;
    }

    for (address_ptr = server_info; address_ptr != NULL; address_ptr = address_ptr->ai_next) {
        if ((server_data_fd = socket(address_ptr->ai_family, address_ptr->ai_socktype, address_ptr->ai_protocol)) == -1) {
            continue;
        }
        if (connect(server_data_fd, address_ptr->ai_addr, address_ptr->ai_addrlen) == -1) {
            close(server_data_fd);
            continue;
        }
        break;
    }

    if (address_ptr == NULL) {
        fprintf(stderr, "Failed to connect to client data socket\n");
        return -1;
    }

    char conn_ack[28];
    recv(server_data_fd, conn_ack, 28, 0);
    //printf("%s\n", conn_ack);

    return server_data_fd;
}

/* send a signal to client that all data has been sent */
void sendEndData(int worker_data_fd)
{
    //printf("Sending END_DATA_MESSAGE\n");
    send(worker_data_fd, END_DATA_MESSAGE, strlen(END_DATA_MESSAGE), 0);
}
 
/* Get command handling */
void handleGetCmd(int worker_data_fd, int worker_cmd_fd, unsigned char* arg, unsigned char* output_buffer)
{   
    char* file_name = (char*) (arg + 3);
    DIR* _dir = getDirectoryContents("."); 		// open directory
    if (directoryContains(_dir, file_name)) {		// verify directory contains file
        printf("Sending file %s to client\n", arg);
        FILE *fp = fopen(file_name, "r");
        do {
			memset(output_buffer, 0, OUT_BUFFER_SIZE);
			fread(output_buffer, OUT_BUFFER_SIZE, 1, fp);
			//printf("Buffer fill: %d / %d\n", strlen(output_buffer), OUT_BUFFER_SIZE);
			send(worker_data_fd, output_buffer, OUT_BUFFER_SIZE, 0);
		} while (strlen(output_buffer) == OUT_BUFFER_SIZE);
        fclose(fp);
    } else {
        fprintf(stderr, "File not Found\n");
        sendError(worker_cmd_fd, ERROR_BAD_FILENAME, output_buffer);
        send(worker_cmd_fd, END_DATA_MESSAGE, strlen(END_DATA_MESSAGE), 0);

    }
    sendDataDisconnectToClient(worker_data_fd);
    closedir(_dir);
}

/* returns 0 if directory does not contain file, 1 if directory does contain file */
int directoryContains(DIR* _dir, char* file_name)
{
    struct dirent* _dirent;
    while((_dirent = readdir(_dir)) != NULL)
        if (strcmp(_dirent->d_name, file_name) == 0)
            return 1;
    return 0;
}

/* List command handling */
void handleListCmd(int worker_data_fd, int worker_cmd_fd, unsigned char* arg, unsigned char* output_buffer)
{   
    struct dirent* _dirent;
    DIR* _dir = getDirectoryContents(".");
    if (_dir != NULL) { 
	printf("Sending directory contents to client\n"); 
        while ((_dirent = readdir(_dir)) != NULL) {
	    /* send each file or sub-directory name, followed by a newline */
            send(worker_data_fd, (char*) _dirent->d_name, strlen((char*) _dirent->d_name), 0);
            send(worker_data_fd, "\n", 1, 0);
        }
        send(worker_data_fd, END_DATA_MESSAGE, strlen(END_DATA_MESSAGE), 0);	// done sending 
        closedir(_dir);
        char fin_ack[strlen(END_DATA_MESSAGE)];
        recv(worker_data_fd, fin_ack, strlen(END_DATA_MESSAGE), 0);		// receive FIN ACK
    }
}

/* returns a pointer to DIR for the current working directory */
DIR* getDirectoryContents(char* dir_name)
{
    DIR* _dir = opendir(dir_name);
    if (_dir == NULL) {
        fprintf(stderr, "Could not open directory\n");
        return NULL;
    } else return _dir;
}

/* print the contents of DIR pointed to by argument */
void printDirectory(DIR* _dir)
{   
    struct dirent* _dirent;
    while((_dirent = readdir(_dir)) != NULL)
        printf("%s\n", _dirent->d_name);
}

/**************************************** Error handling ****************************************/

/* send an error message to client */
void sendError(int worker_cmd_fd, char* error, unsigned char* out_buffer)
{
    memset(out_buffer, 0, OUT_BUFFER_SIZE);
    memcpy(out_buffer, error, strlen(error));
    send(worker_cmd_fd, out_buffer, strlen((char*) out_buffer), 0);
}

/* handle invalid command (not used currently, handled by client) */
void handleInvalidCmd(int worker_cmd_fd, 
                        unsigned char* command, 
                        unsigned char* output_buffer) 
{
    fprintf(stderr, "Invalid command\n"); 
    sendError(worker_cmd_fd, ERROR_INVALID_COMMAND, output_buffer);
}

/* handle bad filename (file does not exist in servers current working directory) */ 
void handleBadFilename(int worker_cmd_fd,
                        char* file_name,
                        unsigned char* output_buffer)
{
    fprintf(stderr, "Invalid filename\n");
    sendError(worker_cmd_fd, ERROR_BAD_FILENAME, output_buffer);
}

/********************************** Connection termination **********************************************/

/* client has disconnected */
void handleClientDisconnect(int worker_cmd_fd)
{
    fprintf(stderr, "Client disconnected\n");
}

/* tell client to disconnect */
void sendDataDisconnectToClient(int worker_data_fd)
{
    //printf("Sending %s\n", END_DATA_MESSAGE);
    sendEndData(worker_data_fd);
    char fin_ack[strlen(END_DATA_MESSAGE)];
    recv(worker_data_fd, fin_ack, strlen(END_DATA_MESSAGE), 0);
} 

/*************************************** Shutdown handling ***********************************************/

/* terminate a worker thread, called from within the worker thread itself */
void workerThreadComplete(int worker_cmd_fd, char** client_data_socket_info, void* arg)
{
    //printf("Worker thread complete\n");
    close(worker_cmd_fd);
    if (client_data_socket_info[0]) free(client_data_socket_info[0]);
    if (client_data_socket_info[1]) free(client_data_socket_info[1]);
    if (client_data_socket_info) free(client_data_socket_info);
    if (arg) free(arg);
    //printf("Worker thread exiting\n");
    pthread_exit(NULL);					// worker thread exits, rather than being joined
										// main thread does not need to keep track of threads
}

/* send a message to client indicating that client should shut down */
void sendKillToClient(int worker_cmd_fd)
{
    //printf("Sending kill message %s to client\n", SERVER_KILL_MESSAGE);
    send(worker_cmd_fd, SERVER_KILL_MESSAGE, strlen(SERVER_KILL_MESSAGE), 0);
}

/* deallocate heap memory */
void serverTearDown()
{
    for (int i = 0; i < NUM_BUFFERS; i++) {
        if (in_buffers[i]) free(in_buffers[i]);
        if (out_buffers[i]) free(out_buffers[i]);
    }
    for (int i = 0; i < socket_count; i++) {
        int try = send(sockets[i], "TEST", 4, 0);
        if (try != -1) {
            close(sockets[i]);
        }
    }
    free(sockets);
    printf("Server teardown complete, exiting\n");
    exit(1); 
}

/****************************** IO resource sharing among threads *************************/

/* returns a mutex which can be used to access corresponding thread-safe buffers to store
 * strings to send to client or strings received to client */
int acquireFreeIOMutex() {
    int acquired = -1, idx = 0;
    while (idx < NUM_BUFFERS && acquired == -1) {
        if (pthread_mutex_trylock(&io_mutexes[idx]) == 0) {
            acquired = 1;
            //printf("Mutex %d acquired\n", idx);
        } else {
            idx++;
        }
    }
    if (acquired > 0) {
        memset(in_buffers[idx], 0, IN_BUFFER_SIZE);
        memset(out_buffers[idx], 0, OUT_BUFFER_SIZE);
        return idx;
    } else {
        return acquired;
    }
}

/*************************************** Signal handling **********************************/

void sigint_intercept(int signal_number)
{
    printf("SIGINT received, terminating server...\n");
    SERVER_DISCONNECT = 1;
}
