# pepa

This project is designed to redirect TCP/IP sockets.
It writes everything received from the socket to a named pipe file descriptor,
reads data from another file descriptor (another pipe),
and writes it to the socket.

To compile, type "make" and run it without any arguments to print a help message. Enjoy!
