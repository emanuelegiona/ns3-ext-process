#! /usr/bin/python3

# Copyright (c) 2024 Sapienza University of Rome
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation;
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Author: Emanuele Giona <giona@di.uniroma1.it> <ORCID: 0000-0003-0871-7156>
#

from argparse import ArgumentParser
import socket
import sys

# Special message sent by ns-3 (ExternalProcess)
MSG_KILL = "PROCESS_KILL"



def main(
    port: int,
    attempts: int = 0,
    debug: bool = False
) -> None:
    if debug:
        msg = f"Starting {sys.argv[0]}\n--- ----- ----- ---"
        print(msg, flush=True)

    localhost = "127.0.0.1"
    sock = None
    connected = False

    # Just to avoid connection failures during Valgrind runs
    iteration = 0
    while not connected:
        iteration += 1
        try:
            if sock is None:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((localhost, port))
            connected = True
            if debug:
                msg = f"Connection established to '{localhost}:{port}'"
                print(msg, flush=True)  # Forcing flush might be necessary for output to show in log file
        except Exception as exc:
            connected = False
            if debug:
                msg = f"Failed connection to server hosted on '{localhost}:{port}', attempt {iteration}; reason: {str(exc)}"
                print(msg, flush=True)

        # Connection attempts threshold
        if attempts > 0 and not connected and iteration >= attempts:
            if debug:
                msg = f"Maximum attempts reached for connection to '{localhost}:{port}', exiting"
                print(msg, flush=True)
            exit(1)

    # Socket read & write loop
    iteration = 0
    try:
        while True:
            if debug:
                msg = f"Beginning iteration {iteration}"
                msg += "\n1. Waiting for data..."
                print(msg, flush=True)

            data = sock.recv(1024)
            if not data:
                break

            # Checking for MSG_KILL reception
            try:
                data_as_str = data.decode().strip("\n")
                if data_as_str == MSG_KILL:
                    msg = "KILL received, exiting"
                    print(msg, flush=True)
                    break
            except:
                pass

            msg = f"Received data: {data}"
            print(msg, flush=True)

            if debug:
                msg = "2. Echoing back..."
                print(msg, flush=True)
            sock.sendall(data)

        if debug:
            msg = f"Connection closed from '{localhost}:{port}'"
            print(msg, flush=True)
        sock.close()
    except Exception as exc:
        if debug:
            msg = f"Failed socket recv() or sendall() from '{localhost}:{port}', iteration {iteration}; reason: {str(exc)}"
            print(msg, flush=True)
        exit(1)

    # Perform cleaning up operations, if any
    if debug:
        msg = f"--- ----- ----- ---\nTerminating {sys.argv[0]}"
        print(msg, flush=True)
    exit(0)



if __name__ == "__main__":
    parser = ArgumentParser(prog="echo.py", usage="Echo client implemented with a TCP socket.")
    parser.add_argument("port", help="Port for TCP connection")
    parser.add_argument("--attempts", help="Maximum connection attempts; if 0, no threshold is imposed", default=0)
    parser.add_argument("--debug", help="Turns on debugging prints", default=False)
    args = parser.parse_args()
    main(int(args.port), int(args.attempts), bool(args.debug))
    exit(0)
