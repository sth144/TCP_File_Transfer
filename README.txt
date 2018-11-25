***************************************************************************************************

			File Download: C Server and Python Client
			    A Network Application built on TCP
					Sean T. Hinds
					25 Nov, 2018


***************************************************************************************************

					Overview

This pair of programs use the TCP transport layer protocol to implement a file transfer network
application. The server is written in C (-std=gnu11) and the client is written in Python (3.7). 
The server starts and listens on a user specified port X on host A. The client starts on host B,
initiating a control connection with the server on host A, port X. The client also begins listening
for data connection requests on host B, port Y. The client sends a command (-l for LIST, -g FILENAME
for GET) to the server, and the server requests a data connection from the client on host B, port Y.
Once the data connection is established, the server sends either the contents of its current working
directory (for a LIST) or the contents of the requested file (for a GET) over the data socket. The
client prints the data received over the data socket (for a LIST) or writes the data to a file (for
a GET). The server then closes the data connection. The client may run in either single command or
shell mode. If the client is running in single command mode, the client will close its command 
connection and shut down after one command. If the client is running in shell mode, the user will
be prompted for another command. The client and server should be run in separate directories, and
may be run on separate hosts. 

Client will check for requests of files which already exist in its directory, as well as invalid
commands. Server will handle file-not-found errors.

Both the client and server are multithreaded. The client utilizes separate threads to manage the 
control and data connections. The server uses one main thread to handle incoming control connection 
requests and separate worker threads to manage control and data connections for each connecting 
client.

These programs were tested on the Oregon State Flip Linux servers. Server was run on host flip2, 
and the client on host flip3.

					Instructions

Setup:
1. Place client.py and download_client.py in a directory called client. Place makefile, server.c
download_server.h and download_server.c in a directory called server. Server directory must
contain text files that the server may serve to the client. test_data.txt and long_data.txt have
been supplied for this purpose.
2. Open separate terminals within the client and server directories. These terminals/directories
may exist on separate hosts. Flip2 will be the example host for the server below, and flip3 for 
the client

Build:
3. run make in the server directory to build the server executable.
	flip2:server $ make

Start Server:
4. Start the server on your port of choice with the following command:
	flip2:server $ ./server {SERVER_PORT}

Client:
5. run:
	chmod +x client.py
6. To view the contents of the server directory, run the following in the client directory:
	flip3:client $ ./client.py flip2 {SERVER_PORT} {CLIENT_DATA_PORT} -l
This should cause the server to send its directory contents, and the client to display them.
7. To request a file from the server directory, run:
	flip3:client $ ./client.py flip2 {SERVER_PORT} {CLIENT_DATA_PORT} -g {FILE_NAME}
8. To enter shell mode, simply start the client with no command arguments:
	flip3:client $ ./client.py flip2 {SERVER_PORT} {CLIENT_DATA_PORT}

					Extra Credit Features Implemented

1. Make the server multi-threaded

					Sources

The following sources were referenced during development (all code is original):
Accessing directory in C: 
	https://www.geeksforgeeks.org/c-program-list-files-sub-directories-directory/
TCP socket programming in C: Beej's Guide to Network Programming
	https://beej.us/guide/bgnet/html/multi/index.html 
Multithreaded programming in C: pthread docs
	http://pubs.opengroup.org/onlinepubs/7908799/xsh/pthread.h.html
Python find local external IP address: Jamieson Becker's post from 9 Mar, 2015
	https://stackoverflow.com/questions/166506/finding-local-ip-addresses-using-pythons-stdlib
Python sockets: Python socket Module Documentation
	https://docs.python.org/3/library/socket.html
Python threading: Python threading Module Documentation
	https://docs.python.org/3.7/library/threading.html
Python signal handling with threads: George Notora's blog post on G-Loaded Journal from 24 Nov, 2016
	https://www.g-loaded.eu/2016/11/24/how-to-terminate-running-python-threads-using-signals/

