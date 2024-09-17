# pepa

This project is designed to redirect TCP/IP sockets.
It writes everything received from the socket to a named pipe file descriptor,
reads data from another file descriptor (another pipe),
and writes it to the socket.

To compile, type "make" and run it without any arguments to print a help message. Enjoy!


What's new
18/09/2024

Added:
1. Config file is supported now. To use it, specify it with --config or -C oprion
2. The State Machine is changed. Now the SHVA server added after OUT and IN connections are established.
   THe number of IN connctions ("readers") is specified in the config file
3. Added the option to insert PEPA ID and a random "ticket" into every buffer transfered by PEPA
