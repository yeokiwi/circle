#!/usr/bin/env python3
#
# softreset.py - Send a software reset command to a Circle application,
#                which uses the addon/softreset library
#
# Usage: python3 softreset.py HOST [PASSWORD] [--port PORT] [--ping]
#

import argparse
import socket
import sys

DEFAULT_PORT = 5050

def main():
	parser = argparse.ArgumentParser(description="Reboot a Raspberry Pi running Circle over the network")
	parser.add_argument("host", help="IP address or host name of the Raspberry Pi")
	parser.add_argument("password", nargs="?", default="", help="password (if configured)")
	parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="UDP port (default %d)" % DEFAULT_PORT)
	parser.add_argument("--ping", action="store_true", help="only check, if the system is alive")
	parser.add_argument("--timeout", type=float, default=2.0, help="reply timeout in seconds")
	args = parser.parse_args()

	if args.ping:
		command = "PING"
	else:
		command = "REBOOT"
		if args.password:
			command += " " + args.password

	sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
	sock.settimeout(args.timeout)

	try:
		sock.sendto((command + "\n").encode(), (args.host, args.port))
		reply, _ = sock.recvfrom(256)
	except socket.timeout:
		print("No reply from %s:%d" % (args.host, args.port))
		return 1
	except OSError as e:
		print("Error: %s" % e)
		return 1
	finally:
		sock.close()

	reply = reply.decode(errors="replace").strip()
	print(reply)

	return 0 if reply in ("OK", "PONG") else 1

if __name__ == "__main__":
	sys.exit(main())
