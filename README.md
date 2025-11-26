## 🚀 Building and Running the HTTP Proxy

This project is built directly using **GCC** (the GNU Compiler Collection) without relying on any external build system.

-----

## 🛠️ Compilation

To compile the source code, open your terminal in the project directory and execute the following command. This command uses strict error checking and links the necessary threads library.

```bash
gcc -Wall -Wextra -Werror -o server server.c http_handler.c utils.c network.c -lpthread
```

## 🏃 Running the Server

Once compiled, run the executable and specify the port number you want the proxy to listen on (e.g., `9000`).

```bash
./server 9000
```

The server will now be listening for incoming HTTP requests on `127.0.0.1:9000`.

-----

## 🧪 Testing the Proxy

Use the `curl` command-line tool, explicitly directing it to use your running proxy server.

### Standard Test (Using DNS Lookup)

This requests `http://example.com` and forces `curl` to send the traffic through your proxy.

```bash
curl -v --proxy 127.0.0.1:9000 http://example.com
```

### Specific IP and Host Header Test (Advanced)

Use this to test connectivity to a specific IP address while providing the required `Host` header for the upstream server (crucial for virtual hosting).

```bash
curl -v --proxy 127.0.0.1:9000 --header "Host: httpforever.com" http://146.190.62.39
```

  * **`-v`**: Enables **verbose output**, allowing you to see the full request/response headers and debug the transaction.
  * **`--proxy 127.0.0.1:9000`**: Directs `curl` to use your running program as the HTTP intermediary.




## Roadmap

### Features

- [ ] Connection pooling for upstream
- [ ] Handle multiple requests
- [ ] Handle non replying hosts that does not serve HTTP
- [ ] Handle timeout
- [ ]  Configuration file support
- [ ]  Basic caching
- [ ]  Logging with levels
- [ ]  Health check endpoints

### Robustness

- [ ] Refactor code to handle HTTP request responses, error management
- [ ]  Handle DNS failures gracefully
- [ ]  Upstream health checks
- [ ]  Retry logic for failed connections
- [ ]  Request timeouts
- [ ] Sanitize header, remove useless headers from proxy or else