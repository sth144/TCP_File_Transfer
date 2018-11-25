#
#	Title: Python File Download Client Class Implementation
#	Author: Sean Hinds
#	Date: 24 Nov, 2018
#	Description: A Python class which implements a download client. Facilitates the transfer of files
#			over TCP connection from a remote server running a complementary protocol. The
#			protocol uses a control connection and a data connection. 
#			The main client thread manages the control connection, with a worker thread 
#			handling the data connection. DownloadClient is the client which requests the control
#			connection, and the 'server' which listens for data connection requests from the server.
#			Client has both single command, and shell modes, as specified by cmd_mode parameter
#			of constructor.
#

# Modules
import socket
import sys
import os
import select
import signal
import threading

# Global constants
IN_BUFFER_SIZE = 4096
OUT_BUFFER_SIZE = 128
SERVER_KILL_MESSAGE = "@@SERVER_KILL"
ERROR_INVALID_COMMAND = "@@ERROR_INVALID_COMMAND"
ERROR_BAD_FILENAME = "@@ERROR_BAD_FILENAME"
END_DATA_MESSAGE = "@@END_DATA"
GET_RES_SENTINEL = "@@GET"
LIST_RES_SENTINEL = "@@LIST"


class DownloadClient:

    # Constructor defines several class-scoped variables
    def __init__(self, server_address, server_port, client_data_address, client_data_port, cmd_mode, cmd_arg=None):
		self.server_address = server_address
		self.server_port = server_port
		self.client_data_address = client_data_address
		self.client_data_port = client_data_port
		self.AWAIT_FILE = False
		self.AWAIT_LIST = False
		self.await_file_name = ""
		self.KILL_RECEIVED = False
		self.SERVER_DISCONNECT = False
		self.cmd_mode = cmd_mode
		self.cmd_arg = cmd_arg

    # Startup, called externally to launch client
    def startup(self):
		self.data_worker_thread = threading.Thread(
			target=self.dataWorkerThreadFn)
		self.data_worker_thread.start()
		self.establishControlConnection()
		if (self.cmd_mode == "shell"):		# shell mode
			self.commandLoop()
		elif (self.cmd_mode == "list"):		# single command (list)
			self.singleService()
		elif (self.cmd_mode == "get" and self.cmd_arg != None):	# single command (get)
			self.singleService(self.cmd_arg)
		self.clientTearDown()

    # Initialize socket for control connection
    def establishControlSocket(self):
		self.client_cmd_socket = socket.socket(
			socket.AF_INET, socket.SOCK_STREAM)

    # Establish the control connection
    def establishControlConnection(self):
		self.establishControlSocket()
		self.client_cmd_socket.connect((self.server_address, self.server_port))
		self.client_cmd_socket.send(str(self.client_data_address).encode())	# send data address for data connection
		addr_ack = self.client_cmd_socket.recv(10).decode()
		self.client_cmd_socket.send(str(self.client_data_port).encode())	# send data port for data connection
		port_ack = self.client_cmd_socket.recv(10).decode()

    # Main loop in thread which manages control connection
    def commandLoop(self):
		event = True
		check_if_readable = [sys.stdin, self.client_cmd_socket]
		while(not self.KILL_RECEIVED):
			try:
				readable, w, e = select.select(check_if_readable, [], [], 0.01)
				if (event):
					print("Please enter a command ($ -l or $ -g FILENAME)")
					event = False
				if (sys.stdin in readable):
					status = self.handleClientCommand()		    # returns True if success
					event = True
				# check for status messages from server
				if (self.client_cmd_socket in readable):
					self.handleStatusMessageFromServer()
			except KeyboardInterrupt:
				self.KILL_RECEIVED = True

    # Essentially runs one iteration of commandLoop() when the user has not entered shell mode
    def singleService(self, cmd_arg=None):
		if (self.cmd_mode == "list"):
			self.AWAIT_LIST = True					# set flag
			self.client_cmd_socket.send("-l".encode())		# send message
		elif (self.cmd_mode == "get"):
			query = "-g " + cmd_arg					# build query
			self.await_file_name = cmd_arg				
			self.AWAIT_FILE = True					# set flags
			self.BAD_FILENAME = False
			self.client_cmd_socket.send(query.encode())		# send message
		else:
			return
		check_if_readable = [self.client_cmd_socket]
		while (not self.KILL_RECEIVED and not self.SERVER_DISCONNECT):
			try:
				readable, w, e = select.select(check_if_readable, [], [], 0.01)
				# check for status messages from server over control connection
				if (self.client_cmd_socket in readable):
					self.handleStatusMessageFromServer()
			except:
				sys.stderr.write("Client Error")

    # Handles a single command, called from commandLoop()
    def handleClientCommand(self):
		command = raw_input()			# grab input
		args = command.split(" ")		# parse and handle arguments
		if (args[0] in ["-g", "-l"]):
			if (args[0] == "-g"):
				self.await_file_name = command.split(" ")[1]
			if (len(self.await_file_name) > 0):
				self.AWAIT_FILE = True
			if (args[0] == "-l"):
				self.AWAIT_LIST = True
			self.BAD_FILENAME = False
			self.client_cmd_socket.send(command.encode())	# send command
		else:
			self.handleClientCommandError(command)
		return True

    # Handle client-side command errors: bad command or duplicate filename
    def handleClientCommandError(self, command):
		sys.stderr.write("%s is not a valid command\n" % (command))

    # Handle status messages sent from server over control connection
    def handleStatusMessageFromServer(self):
		# status messages come over command socket
		status_message = self.client_cmd_socket.recv(IN_BUFFER_SIZE).decode()
		# sys.stdout.write("Status message from server: %s\n" % (status_message))
		if (status_message in [SERVER_KILL_MESSAGE, "\0"]):
			sys.stdout.write("Kill message received from server\n")
			self.handleServerDisconnect()
		if (status_message == ERROR_BAD_FILENAME):
			sys.stdout.write("Server failed to locate file\n")
			self.BAD_FILENAME = True

    # Main method executed by worker thread, which manages data connection and receives data sent from server
    def dataWorkerThreadFn(self):
		# sys.stdout.write("Data worker thread spawned\n")
		# Initialize the welcome socket and bind it to port, start listening for connection requests from server
		self.client_welcome_socket = socket.socket(
			socket.AF_INET, socket.SOCK_STREAM)
		self.client_welcome_socket.setsockopt(
			socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
		self.client_welcome_socket.bind(('', self.client_data_port))
		check_if_readable = [self.client_welcome_socket]
		self.client_welcome_socket.listen(1)
		# sys.stdout.write("Data socket listening on %s:%s\n" % (self.client_data_address, self.client_data_port))
		while ((not self.KILL_RECEIVED) and (not self.SERVER_DISCONNECT)):
			readable, w, e = select.select(check_if_readable, [], [], 0.01)
			if (self.client_welcome_socket in readable):
				self.client_data_socket = self.w_establishDataConnection()
				#sys.stdout.write("Established data connection\n")
			if (self.AWAIT_FILE):
				self.w_handleGetCommandResponse()
			elif (self.AWAIT_LIST):
				self.w_handleListCommandResponse()
				self.client_data_socket.close()
			if (self.cmd_mode != "shell"):
				self.KILL_RECEIVED = True		# kill after one iteration if not in shell mode
		# sys.stdout.write("Closing welcome socket\n")
		self.client_welcome_socket.close()

    # Set up data connection, called from dataWorkerThreadFn when server sends a connection request to data welcome socket
    # Returns a new socket which provides a handle on the data connection
    def w_establishDataConnection(self):
		#sys.stdout.write("Establishing data connection\n")
		client_data_socket, addr = self.client_welcome_socket.accept()
		client_data_socket.settimeout(60)
		client_data_socket.send("Data connection established!".encode())
		return client_data_socket

    # Handle response to a GET command, called from dataWorkerThreadFn() when the AWAIT_FILE flag is set		
    def w_handleGetCommandResponse(self):
		if (not self.BAD_FILENAME):
			sys.stdout.write("Receiving %s from server\n" %
			     (self.await_file_name))
		DUPLICATE_FILENAME = False
		out_file_name = self.await_file_name
		if (out_file_name in os.listdir(".")):
			sys.stderr.write(
		    		"Client error, duplicate filename %s. (Discarding data received. Please wait, This may take a minute)\n" 
				% (out_file_name))
			DUPLICATE_FILENAME = True
		out_file = open(out_file_name, "w")		# open the output file
		END_DATA_RECEIVED = False
		check_if_readable = [self.client_data_socket]
		long_message = ""				# concatenate messages received in long_message buffer
		while ((not END_DATA_RECEIVED) and (not self.SERVER_DISCONNECT)):
			readable, w, e = select.select(check_if_readable, [], [], 0.01)
			if (self.client_data_socket in readable):
				message = self.client_data_socket.recv(
					IN_BUFFER_SIZE).decode()
				long_message += message
				if (long_message.find(END_DATA_MESSAGE) != -1):				# END_DATA signal sent from server
					if ((not DUPLICATE_FILENAME) and (not self.BAD_FILENAME)):	# write to file
						out_file.write(
							long_message.split(END_DATA_MESSAGE)[0])
					END_DATA_RECEIVED = True
					self.client_data_socket.send(END_DATA_MESSAGE.encode())		# ACK the END_DATA signal
					self.AWAIT_FILE = False
				elif (len(long_message) >= IN_BUFFER_SIZE + len(message)):		# drain long_message 
					if ((not DUPLICATE_FILENAME) and (not self.BAD_FILENAME)):	# write to file
						out_file.write(long_message[:len(message)])
						long_message = long_message[len(message):]
		if (self.BAD_FILENAME and (not DUPLICATE_FILENAME)):
			os.remove(out_file_name)		
		out_file.close()

    # Handle response to a List command, called from command dataWorkerThreadFn() when AWAIT_LIST flag is set
    def w_handleListCommandResponse(self):
		sys.stdout.write("Receiving directory structure from server\n")
		END_DATA_RECEIVED = False
		long_message = ""							# concatenate messages in long_message buffer
		while ((not END_DATA_RECEIVED) and (not self.SERVER_DISCONNECT)):
			message = self.client_data_socket.recv(IN_BUFFER_SIZE).decode()
			long_message += message
			if (long_message.find(END_DATA_MESSAGE) != -1):				# END_DATA signal received
				sys.stdout.write("%s\n" %
						 (long_message.split(END_DATA_MESSAGE)[0]))	# print results
				END_DATA_RECEIVED = True
				self.client_data_socket.send(END_DATA_MESSAGE.encode())		# ACK the END_DATA signal
				self.AWAIT_LIST = False
			elif (len(long_message) >= IN_BUFFER_SIZE + len(message)):		# drain long_message buffer
				sys.stdout.write("%s\n" % (long_message[0:len(message)]))	#   (unlikely to ever occur)
				long_message = long_message[len(message):]
    
    # Gracefully tear down the client, disconnecting from server and joining data worker thread
    def clientTearDown(self):
		# print("Client tear down")
		self.clientDisconnect()
		# print("Joining data worker")
		self.data_worker_thread.join(1)
		exit(1)
        
    # Gracefully disconnect from server by closing control socket
    def clientDisconnect(self):
		self.client_cmd_socket.close()
  
    # Handle the case where server terminates the control connection
    def handleServerDisconnect(self):
		self.SERVER_DISCONNECT = True
		self.clientTearDown()	
