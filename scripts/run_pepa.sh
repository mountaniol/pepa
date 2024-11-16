#!/bin/bash

# ./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --monitor 5 --divider "k" 
# ./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config $@
# ./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config --monitor 5 $@
#./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config --monitor 1 $@
#./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config --monitor 1 $@
#./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config --monitor 1 $@
./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config --monitor 1 $@
#./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "b" --config pepa.config --monitor 1 --dump $@
