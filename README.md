# pepa

# WARNING
This branch is obsolete.
Checkout and use the brabch "pepa-ng-4"


This project is designed to redirect TCP/IP sockets.
It writes everything received from the socket to a named pipe file descriptor,
reads data from another file descriptor (another pipe),
and writes it to the socket.

To compile, type "make" and run it without any arguments to print a help message.
To compile static version, type "make clean all -f Makefile.static"
Enjoy!

