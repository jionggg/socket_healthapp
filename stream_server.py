import socket
import os
import sys

HOST = "127.0.0.1"
PORT = 12345
BACKLOG = 5
def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sockfd:
        sockfd.bind((HOST, PORT))
        sockfd.listen(BACKLOG)

        print(f"Listening on {HOST}:{PORT}")
        try:
            while True:
                new_fd, addr = sockfd.accept()
                pid = os.fork()

                if pid == 0:  # child
                    sockfd.close()          # child doesn't need listener
                    new_fd.send(b"Hello, World!")
                    new_fd.close()
                    sys.exit(0)             # exit child immediately
                else:
                    new_fd.close()            # parent doesn't need connected socket
        except KeyboardInterrupt:
            print("\nServer shutting down.")
            sockfd.close()
    
                
if __name__ == "__main__":
    main()