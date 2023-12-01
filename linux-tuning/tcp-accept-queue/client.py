import asyncio

async def tcp_client(host, port):
    _, writer = await asyncio.open_connection(host, port)
    print('Connected')
    writer.write('hello'.encode())
    while True:
        await asyncio.sleep(1)

async def main(host, port, number):
    tasks = []
    for i in range(number):
        task = asyncio.create_task(tcp_client(host, port))
        tasks.append(task)
    await asyncio.gather(*tasks)

asyncio.run(main('Your Hostname', 12345, 5))
