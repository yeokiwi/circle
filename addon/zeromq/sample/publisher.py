#!/usr/bin/env python3
#
# publisher.py - publishes messages for the SUB socket of the Circle sample
#
# The Circle sample connects to this script on port 5557 (set "zmqpub=<ip>"
# in cmdline.txt to the IP address of the computer running this script).
#
# Usage: python3 publisher.py
# Requires: pip install pyzmq
#
import time
import zmq

ctx = zmq.Context()
sock = ctx.socket(zmq.PUB)
sock.bind("tcp://*:5557")

count = 0
while True:
    count += 1
    sock.send_multipart([b"pc.hello", ("Hello Circle %d" % count).encode()])
    sock.send_multipart([b"other", b"this is not subscribed by Circle"])
    time.sleep(1)
