# WebServer

A lightweight C++17 HTTP server using `epoll`, a thread pool, and simple routing.

## Features

- Non-blocking I/O with `epoll`
- Thread pool for concurrent request handling
- Simple path-based routing
- Static file serving
- Basic logging support
- Benchmark helper for maximum concurrent connection testing

## Requirements

- Linux / Ubuntu
- GNU `g++` with C++17 support
- `make`
- `wrk` for benchmarking (optional)

## Build

From the project root:

```bash
make clean
make
```

This produces the executable `webserver`.

## Run

Start the server from the project root:

```bash
./webserver
```

The server listens on port `5005` by default.

## Routes

- `GET /` - home page
- `GET /api/users` - sample JSON list
- `GET /api/users/1` - sample JSON object
- `POST /api/users` - sample created response
- `GET /static/:file` - static file serving from `www/`

### Static files

Place static files under the `www/` directory. Example:

```bash
mkdir -p www
cat > www/test.html <<'HTML'
<!doctype html>
<html><head><meta charset="utf-8"><title>test</title></head>
<body><h1>Static Test</h1><p>OK</p></body>
</html>
HTML
```

Then access:

```bash
curl http://127.0.0.1:5005/static/test.html
```

## Benchmarking

A helper script is included to test maximum concurrent connections.

```bash
chmod +x benchmark_max_connections.sh
./benchmark_max_connections.sh 127.0.0.1:5005 /static/test.html 5000 200 10 4 0.05 10
```

Parameters:

- `127.0.0.1:5005` — target host and port
- `/static/test.html` — request path
- `5000` — maximum concurrency to test
- `200` — step size
- `10` — duration in seconds per test
- `4` — number of `wrk` threads
- `0.05` — acceptable non-200 ratio threshold
- `10` — starting concurrency

## Direct `wrk` example

```bash
wrk -t4 -c100 -d30s --latency http://127.0.0.1:5005/static/test.html
```

## Notes

- If benchmarking high concurrency, ensure the system `ulimit` and kernel TCP settings are sufficient.
- The server uses `epoll` edge-triggered mode and `EPOLLONESHOT` for client sockets.
- For production use, add better error handling, request parsing, and static file optimizations.
