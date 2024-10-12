#!/bin/bash

./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --monitor 5 --divider "k" --config pepa.config $@
#  ./pepa-ng --shva '127.0.0.1:7887' --out '127.0.0.1:9779' --in '127.0.0.1:3748' --log 7 -c --divider "k" --config pepa.config $@
