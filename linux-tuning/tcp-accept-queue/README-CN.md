# Accept 队列溢出时 Linux 内核的行为

我们已经知道 Accept 队列溢出后 Linux 内核将直接丢弃新到达的 SYN 报文。借助以下命令，我们可以观察到 SYN 报文的丢弃次数增加：

```
watch -n 1 -d 'netstat -s | grep "LISTEN"'
```

但是，如果服务端已经回复了 SYN-ACK 报文，那么 Accept 队列溢出时 Linux 内核将如何处理客户端返回的 ACK 报文呢？

常见的说法是此时服务端的行为取决于 `net.ipv4.tcp_abort_on_overflow` 这个内核参数的值：

- `net.ipv4.tcp_abort_on_overflow = 0`，表示当 Accept 队列溢出时，服务端将丢弃第三次握手的 ACK 报文，并视作从未收到该 ACK 报文，因此服务端将重传 SYN-ACK 报文。如果 Accept 队列能够在 SYN-ACK 报文达到最大重传次数前出现空位，那么 TCP 连接仍然可以成功建立。
- `net.ipv4.tcp_abort_on_overflow = 1`，表示服务端将在 Accept 队列溢出时直接向客户端返回 RST 报文来关闭连接。

但在实际的验证过程中，我们发现不同的操作系统在这方面可能有着不同的实现。以上行为在 CentOS 中得到了验证，但是在 Ubuntu 中这个内核参数似乎并不会影响系统的行为。

所以我们提供了 [client.py](./client.py) 和 [server.py](./server.py) 这两个示例代码，这可以帮助我们模拟 Accept 队列溢出，以便我们观察操作系统的行为。

## 客户端与服务端代码介绍

为了让 Accept 队列尽快溢出，我们在服务端代码中将 backlog 设置为 1，即 Accept 队列的最大长度为 1。同时，服务端只监听端口，不会调用 `accept()` 函数从 Accept 队列获取连接，因此只要客户端连接数量超过 2，就会使 Accept 队列溢出。

客户端代码通过协程的方式并发地发起指定数量的 TCP 连接，默认为 5：

```
for i in range(number):
    task = asyncio.create_task(tcp_client(host, port))
    tasks.append(task)
```

## 操作步骤

### 服务端

#### STEP 1

首先，我们需要为服务端增加一个 200ms 的延迟，这可以避免客户端还没有发出所有的 SYN 报文，服务端的 Accept 队列就已经溢出：

```
sudo tc qdisc add dev eth0 root netem delay 200ms
```

运行以下命令可以删除已经添加的网络延迟：

```
sudo tc qdisc delete dev eth0 root
```

#### STEP 2

另起一个终端窗口，运行以下命令，借助计数我们可以确认 Accept 队列是否发生溢出，以及溢出的次数：

```
watch -n 1 -d 'netstat -s | grep "overflowed"'
```

#### STEP 3

另起一个终端窗口，运行以下抓包命令，它将抓取 eth0 接口上所有发往或者来自本机 12345 端口的报文，并写入 packets.pcap：

```
tcpdump -s 0 -nn host localhost and port 12345 -i eth0 -w packets.pcap
```

#### STEP 5

将 `net.ipv4.tcp_abort_on_overflow` 修改为我们想要的值：

```
sysctl -w net.ipv4.tcp_abort_on_overflow=1
```

#### STEP 6

运行服务端代码：

```
python3 server.py
```

### 客户端

#### STEP 1

运行客户端代码：

```
python3 client.py
```

## 验证

如果看到以下计数增加，说明 Accept 队列发生溢出：

```
$ watch -n 1 -d 'netstat -s | grep "overflowed"'
    <Number> times the listen queue of a socket overflowed
```

接下来，我们就可以在 Wireshark 中打开 packets.pcap，分析客户端与服务端的报文收发情况。

最后，修改 `net.ipv4.tcp_abort_on_overflow` 的值，重复以上步骤，以确认这个参数在不同取值时的系统表现。
