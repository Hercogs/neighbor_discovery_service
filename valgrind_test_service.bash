#!/bin/bash

sudo valgrind \
    --leak-check=full \
    --show-leak-kinds=all \
    --track-origins=yes \
    --verbose \
    --log-file=valgrind_service.txt \
    ./build/app_background_service 