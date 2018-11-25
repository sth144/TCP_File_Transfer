#!/usr/bin/python

#
#	Title: Download Client Initialization Example Script
#	Author: Sean Hinds
#	Date: 25 Nov, 2018
#	Description: Parses user input to initialize an instance of DownloadClient class, which connects to a remote
#			server to retrieve directory info and download text files. Resolves server IP address using DNS.
#	Usage:	$ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT} -l		# for LIST command
#		$ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT} -g {FILE_NAME}	# for GET command
#		$ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT}		# for shell mode
#

import sys, socket

from download_client import DownloadClient

# Catch errors in command line input
def usageError():
	sys.stdout.write("Usage: $ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT} -l				# for LIST command\n")
	sys.stdout.write("	or	 $ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT} -g {FILE_NAME}	# for GET command\n")
	sys.stdout.write("	or	 $ ./client.py {SERVER_HOSTNAME} {SERVER_COMMAND_PORT} {CLIENT_DATA_PORT}					# for shell mode\n")
	exit(1)

# Parse arguments
info = []
cmd_mode, file_name = "", None

if ("-l" in sys.argv):			# validate LIST command arguments
	if (len(sys.argv) != 5):
		usageError()
	cmd_mode = "list"
	cmd_index = sys.argv.index("-l")
	for i in range(0, 5):
		if (i != cmd_index):
			info.append(sys.argv[i])
elif ("-g" in sys.argv):		# validate GET command argumeents
	if (len(sys.argv) != 6):
		usageError()
	cmd_mode = "get"
	cmd_index = sys.argv.index("-g")
	if (cmd_index + 1 >= len(sys.argv)):
		usageError()
	file_name = sys.argv[cmd_index + 1]
	for i in range(0, 6):
		if (i != cmd_index and i != cmd_index + 1):
			info.append(sys.argv[i])
else:					# validate shell mode arguments
	if (len(sys.argv) != 4):
		usageError()
	cmd_mode = "shell"
	info = sys.argv

# set server address info
server_hostname = info[1]
server_address = socket.gethostbyname(server_hostname)	# resolve server IP address from hostname using DNS
server_cmd_port = int(info[2])

# Dynamically resolve localhosts external IP address, referred to the following StackOverflow post for inspiration:
#	https://stackoverflow.com/questions/166506/finding-local-ip-addresses-using-pythons-stdlib
test_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
try:
	test_socket.connect(("10.255.255.255", 1))
	client_data_address = test_socket.getsockname()[0]
except:
	sys.stderr.write("Couldn't resolve localhost's external IP address")
	client_data_address = "127.0.0.1"	# if failed to resolve host external IP address dynamically
test_socket.close()

# set client data port
client_data_port = int(info[3])

# Confirm for user that address info (client and server) successfully resolved 
sys.stdout.write("Initiating download client with \nserver socket ")
sys.stdout.write("\t%s:%s, and \ndata socket %s:%s\n" % 
	(server_address, server_cmd_port, client_data_address, client_data_port))

# Instantiate DownloadClient object
file_client = DownloadClient(server_address, server_cmd_port, client_data_address, client_data_port, cmd_mode, file_name) 

# Initialize DownloadClient 
file_client.startup()
