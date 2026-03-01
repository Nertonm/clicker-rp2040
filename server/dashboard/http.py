import asyncio
import json
import os
from pathlib import Path

class DashboardHTTPServer:
    def __init__(self, host="0.0.0.0", port=8080):
        self.host = host
        self.port = port
        self.base_path = Path(__file__).parent

    async def start(self):
        """Inicia o servidor HTTP manual."""
        return await asyncio.start_server(self.handle_request, self.host, self.port)

    async def handle_request(self, reader, writer):
        try:
            data = await asyncio.wait_for(reader.read(4096), timeout=5.0)
            if not data:
                return
            
            request_line = data.decode('utf-8', errors='ignore').split('\r\n')[0]
            parts = request_line.split(' ')
            if len(parts) < 2:
                return
            
            method, path = parts[0], parts[1]
            if method != 'GET':
                await self.send_response(writer, 405, b"Method Not Allowed", "text/plain")
                return

            if path in ('/', '/index.html'):
                file_path = self.base_path / "templates" / "index.html"
                await self.serve_file(writer, file_path, "text/html")
            elif path == "/api/violations":
                from infra.db import get_recent_violations
                # Limite de 100 para auditoria via HTTP
                violations = await get_recent_violations(limit=100)
                body = json.dumps({
                    "violations": violations,
                    "total": len(violations)
                }).encode('utf-8')
                await self.send_response(writer, 200, body, "application/json")
                return
            elif path.startswith('/static/'):
                # Sanitização básica de path
                relative_path = path.lstrip('/')
                file_path = self.base_path / relative_path
                
                content_type = "text/plain"
                if path.endswith(".css"): content_type = "text/css"
                elif path.endswith(".js"): content_type = "application/javascript"
                
                await self.serve_file(writer, file_path, content_type)
            else:
                await self.send_response(writer, 404, b"Not Found", "text/plain")

        except asyncio.TimeoutError:
            pass
        except Exception:
            pass
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    async def serve_file(self, writer, file_path, content_type):
        if not file_path.exists() or not file_path.is_file():
            await self.send_response(writer, 404, b"Not Found", "text/plain")
            return

        with open(file_path, "rb") as f:
            body = f.read()
            await self.send_response(writer, 200, body, content_type)

    async def send_response(self, writer, status_code, body, content_type):
        status_text = {200: "OK", 404: "Not Found", 405: "Method Not Allowed"}.get(status_code, "Error")
        headers = (
            f"HTTP/1.1 {status_code} {status_text}\r\n"
            f"Content-Type: {content_type}; charset=utf-8\r\n"
            f"Content-Length: {len(body)}\r\n"
            "Connection: close\r\n"
            "\r\n"
        )
        writer.write(headers.encode('utf-8') + body)
        await writer.drain()
