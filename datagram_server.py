import socket
import sys
IP_ADDRESS = "127.0.0.1"
PORT = 12345

def main():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sockfd:
        sockfd.bind((IP_ADDRESS, PORT))
        print(f"Datagram server listening on {IP_ADDRESS}:{PORT}")
        try:
            while True:
                print("listener: waiting to recvfrom...")
                data, addr = sockfd.recvfrom(1024)
                print(f"listener: got packet from {addr}")
                print(f"listener: packet is {len(data)} bytes long")
                print(f"listener: packet contaions {data.decode()}")
        except KeyboardInterrupt:
            print("\nServer shutting down.")
            sockfd.close()
        except OSError as e:
            print(f"Server error: {e}")
            sockfd.close()
            sys.exit(1)
            

if __name__ == "__main__":
    main()