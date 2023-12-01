import socket
import time

def start_server(host, port, backlog):
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    server_socket.bind((host, port))
    server_socket.listen(backlog)

    print(f'Server is listening on {host}:{port}')

    while True:
        time.sleep(3)

if __name__ == '__main__':
    start_server('0.0.0.0', 12345, 1)
