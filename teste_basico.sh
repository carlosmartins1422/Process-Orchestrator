#!/bin/bash

make clean
make

./bin/controller 2 FIFO &
controller_pid=$!

sleep 1

./bin/runner -e 1 sleep 2 &
runner1_pid=$!

./bin/runner -e 2 sleep 1 &
runner2_pid=$!

wait "$runner1_pid"
wait "$runner2_pid"

./bin/runner -s
wait "$controller_pid"

echo "Teste terminado."
