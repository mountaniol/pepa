#!/bin/bash


LINE="--shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --color --emusleep 100 --emubufmin 120 --emubuf 1024"

#./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --color --emusleep 10
# ./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --color --emusleep 100 --emubufmin 120 --emubuf 1024

if [ "$1" = "gdb" ]
then
	echo
	echo $LINE
	echo
	#gdb -ex=r --args ./emu $LINE
	gdb ./emu
else
	# ./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --color --emusleep 10 --emubufmin 120 --emubuf 1024
	#./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --emusleep 1 --emubufmin 120 --emubuf 1024 --color
	./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --emusleep 1000000 --emubufmin 120 --emubuf 1024 --color
	#./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --emubufmin 120 --emubuf 1024 --color
#	./emu --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --inum 18 --log 7  --config pepa.config --emuin 4 --emubufmin 120 --emubuf 1024 --color
fi

