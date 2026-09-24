#!/usr/bin/env python3
#
# subscriber.py - receives the messages published by the Circle sample
#
# Usage: python3 subscriber.py <ip-of-raspberry-pi> [topic]
# Requires: pip install pyzmq
#
import sys
import zmq

host = sys.argv[1] if len(sys.argv) > 1 else "192.168.0.250"
topic = sys.argv[2] if len(sys.argv) > 2 else "circle."

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("tcp://%s:5556" % host)
sock.setsockopt_string(zmq.SUBSCRIBE, topic)

while True:
    frames = sock.recv_multipart()
    print(" | ".join(f.decode(errors="replace") for f in frames))
