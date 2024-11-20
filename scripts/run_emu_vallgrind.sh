#!/bin/bash

#valgrind --tool=memcheck -s ./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --emubuf 20480 --emubufmin 8 --emusleep 1000000
#valgrind --leak-check=full ./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 4 --log 7  --config pepa.config --emuin 4 --emubuf 128 --emubufmin 32 --color --emusleep 1000000
valgrind --tool=memcheck ./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 4 --log 7  --config pepa.config --emuin 4 --emubuf 128 --emubufmin 32 --color --emusleep 1000000
memcheck

