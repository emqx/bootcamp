# Linux Kernel Behavior When the Accept Queue Overflows

In the blog [*EMQX Performance Tuning: TCP SYN Queue and Accept Queue*](https://www.emqx.com//blog/emqx-performance-tuning-tcp-syn-queue-and-accept-queue), we introduced the impact of the TCP SYN queue and accept queue on TCP connections and how to increase the size of the SYN queue and accept queue to tune performance.

Through this blog, we learned that when the accept queue overflows, the Linux kernel will directly drop the newly arrived SYN packets. With the following command, we can observe the increase in the number of SYN packet dropped:

```
watch -n 1 -d 'netstat -s | grep "LISTEN"'
```

But if the server has already replied with a SYN-ACK packet, how will the Linux kernel handle the ACK packet returned by the client when the accept queue overflows?

The common saying is that the server's behavior at this time depends on the value of the kernel parameter `net.ipv4.tcp_abort_on_overflow`:

- `net.ipv4.tcp_abort_on_overflow = 0`. Indicates that when the accept queue overflows, the server will drop the ACK packet of the third handshake and treat it as if the ACK packet has never been received, so the server will retransmit the SYN-ACK packet. If a spot appears in the accept queue before the SYN-ACK packet reaches the maximum number of retransmissions, the TCP connection can still succeed.
- `net.ipv4.tcp_abort_on_overflow = 1``. Indicates that the server will return a RST packet directly to the client to close the connection when the accept queue overflows.

However, in the actual verification, we find that different operating systems may have different implementations in this regard. The above behavior has been verified in CentOS, but in Ubuntu, this kernel parameter does not seem to affect the system's behavior.

Considering that there may be behavioral differences between different versions of the operating system, we provide the two example codes, [client.py](./client.py) and [server.py](./server.py). By following the steps below, you can easily simulate the overflow of the accept queue and observe the behavior of the operating system.

## Introduction to Client and Server Code

In order to make the accept queue overflow as soon as possible, we set the backlog to 1 in the server code, that is, the maximum length of the accept queue is 1. At the same time, the server only listens to the port and does not call the `accept()` function to get the connection from the accept queue, so as long as the number of client connections exceeds 2, it will cause the accept queue to overflow.

The client code initiates a specified number of TCP connections concurrently through the coroutine, the default is 5:

```
for i in range(number):
    task = asyncio.create_task(tcp_client(host, port))
    tasks.append(task)
```

## Operation Steps

### Server

#### STEP 1

First, we need to add a 200ms delay to the server, which can prevent the server's accept queue from overflowing before the client has sent all the SYN packets:

```
sudo tc qdisc add dev eth0 root netem delay 200ms
```

The following command can be used to remove the added network delay:

```
sudo tc qdisc delete dev eth0 root
```

#### STEP 2

Open another terminal window and run the following command. With the help of counting, we can confirm whether the accept queue overflows and the number of overflows:

```
watch -n 1 -d 'netstat -s | grep "overflowed"'
```

#### STEP 3

Open another terminal window and run the following packet capture command. It will capture all packets to or from port 12345 on the localhost  the eth0 interface and write them into packets.pcap:

```
tcpdump -s 0 -nn host localhost and port 12345 -i eth0 -w packets.pcap
```

#### STEP 5

Modify `net.ipv4.tcp_abort_on_overflow` to the value we want:

```
sysctl -w net.ipv4.tcp_abort_on_overflow=1
```

#### STEP 6

Run server code:

```
python3 server.py
```

### Client

#### STEP 1

Run client code:

```
python3 client.py
```

## Verification

If you see the following count increase, it means that the accept queue overflows:

```
$ watch -n 1 -d 'netstat -s | grep "overflowed"'
    <Number> times the listen queue of a socket overflowed
```

Next, we can open packets.pcap in Wireshark and analyze the packet sending and receiving between the client and the server.

Finally, modify the value of `net.ipv4.tcp_abort_on_overflow`, repeat the above steps, and confirm the system performance when this parameter takes different values.
