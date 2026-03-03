import asyncio
import json
import time

async def send_rpc(reader, writer, method, params, req_id):
    request = {
        "jsonrpc": "2.0",
        "method": method,
        "params": params,
        "id": req_id
    }
    writer.write((json.dumps(request) + "\n").encode())
    await writer.drain()
    response = await reader.readline()
    return json.loads(response.decode())

async def test_client(client_id, delay_test=False):
    reader, writer = await asyncio.open_connection("127.0.0.1", 8765)
    print(f"Client {client_id} connected")
    
    try:
        # Test 1: Method not found
        resp = await send_rpc(reader, writer, "unknown_method", {}, 1)
        print(f"Client {client_id} - Test 1 (unknown): {resp.get('error', {}).get('code')} == -32601")
        assert resp.get("error", {}).get("code") == -32601

        # Test 2: register_node
        resp = await send_rpc(reader, writer, "register_node", {"node_id": f"node_{client_id}", "ip": "127.0.0.1"}, 2)
        print(f"Client {client_id} - Test 2 (register): Success")

        # Test 2.5: get_active_nodes (Validators routing to NodeRegistry)
        resp = await send_rpc(reader, writer, "get_active_nodes", {}, 25)
        print(f"Client {client_id} - Test 2.5 (get_active_nodes): {resp.get('result')}")
        assert f"node_{client_id}" in resp.get("result", [])

        if delay_test:
            # Test 3: set_processing_delay (only first client sets it)
            if client_id == 0:
                print(f"Client {client_id} - Setting delay to 500ms")
                await send_rpc(reader, writer, "set_processing_delay", {"delay_ms": 500}, 3)
            
            await asyncio.sleep(0.5) # Wait for delay to be set
            
            print(f"Client {client_id} - Starting delay test...")
            start = time.time()
            resp = await send_rpc(reader, writer, "heartbeat", {"node_id": f"node_{client_id}"}, 4)
            end = time.time()
            duration = end - start
            print(f"Client {client_id} - Heartbeat took {duration:.2f}s (expected ~0.5s)")
            assert duration >= 0.45

    finally:
        writer.close()
        await writer.wait_closed()

async def run_tests():
    print("Starting RPC Server tests...")
    
    # Run 3 clients concurrently
    await asyncio.gather(
        test_client(0, True),
        test_client(1, True),
        test_client(2, True)
    )
    print("Tests finished")

if __name__ == "__main__":
    asyncio.run(run_tests())
