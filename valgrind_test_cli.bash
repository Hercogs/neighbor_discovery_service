#!/bin/bash

sudo valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file=valgrind_cli.txt \
    ./build/app_cli \
    "$@"