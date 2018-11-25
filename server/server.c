/***********************************************************************
 * Title: Download Server Initializater
 * Author: Sean Hinds
 * Date: 25 Nov, 2018
 * Description: Initializes a download server on a user specified port
 * Compile:	$ make
 * Usage: 	$ ./server {PORT_NUMBER}
 * ********************************************************************/

#include <signal.h>
#include "./download_server.h"

int main(int argc, char** argv)
{

    /* Register SIGINT handler */
    signal(SIGINT, sigint_intercept);

    /* Validate server port number */
    char* port_str = getValidPort(argc, argv); 

    /* Initialize the server and listen for clients */
    initializeServer(port_str);

    return 0;
}
