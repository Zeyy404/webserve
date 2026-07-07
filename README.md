*This project has been created as part of the 42 curriculum by hmensah-, zsalih, myda-chi.*

# Webserv

## Description

Webserv is an HTTP/1.1 web server written from scratch in C++98, in the spirit of NGINX. It is a single-threaded, event-driven server: one `select()` loop multiplexes every socket and CGI pipe, all file descriptors are non-blocking, and no read or write is ever performed without a prior readiness notification.

The goal of the project is to understand how the web actually works at the protocol level — parsing raw HTTP requests off a TCP socket, routing them according to a configuration file, serving static content, executing CGI scripts, and writing well-formed responses back — while keeping the server resilient: it must never crash, block, or hang, no matter what clients throw at it.

### Features

- Serves fully static websites (HTML, CSS, JS, images) with correct MIME types
- `GET`, `POST`, `DELETE` (and `HEAD`) methods
- File uploads (multipart/form-data or raw body) to a configurable storage location
- CGI execution selected by file extension (Python and PHP demonstrated), with `PATH_INFO` support, request body delivered on the child's stdin, and output read until EOF
- Chunked request bodies are decoded before being handed to handlers/CGI
- NGINX-style configuration: multiple `server` blocks and listen ports, per-route allowed methods, root mapping, index files, autoindex (directory listing), redirections, upload path, CGI handlers, `client_max_body_size`, custom error pages
- Default error pages when none are configured
- Client and CGI timeouts so a request can never hang indefinitely
- **Bonus:** cookie-based session management (login/logout, per-user upload tracking) and multiple CGI types

## Instructions

### Compilation

```bash
make          # builds the `webserv` binary (c++ -Wall -Wextra -Werror -std=c++98)
make clean    # remove object files
make fclean   # remove objects + binary
make re       # rebuild from scratch
```

### Running

```bash
./webserv [configuration file]
# with no argument, config/default.conf is used (run from the repo root)
./webserv config/default.conf
```

Then open `http://127.0.0.1:8080/` in a browser. The default configuration starts three servers (ports 8080, 8081, 8082) with different roots, limits, and route rules.

### Trying the features

```bash
# static site
curl -i http://127.0.0.1:8080/

# upload a file (201 Created)
curl -i -F "file=@myfile.txt" http://127.0.0.1:8080/uploads

# delete it
curl -i -X DELETE http://127.0.0.1:8080/uploads/myfile.txt

# CGI
curl -i "http://127.0.0.1:8080/cgi-bin/echo.py?hello=world"

# sessions (bonus): log in, then see your uploads
curl -i -c jar.txt -d "username=me" http://127.0.0.1:8080/login
curl -i -b jar.txt http://127.0.0.1:8080/my-uploads
```

A Python test suite (`test_webserve.py`) and a session demo script (`test_sessions.sh`) are provided.

### Configuration

See `config/default.conf` for a commented example. Supported directives include `listen host:port`, `server_name`, `root`, `index`, `client_max_body_size`, `error_page`, and per-`location` rules: `allowed_methods`, `root`, `index`, `autoindex`, `upload_path`, `return` (redirection), and `cgi_extension <ext> <interpreter>`.

## Resources

- [RFC 7230 — HTTP/1.1: Message Syntax and Routing](https://datatracker.ietf.org/doc/html/rfc7230)
- [RFC 7231 — HTTP/1.1: Semantics and Content](https://datatracker.ietf.org/doc/html/rfc7231)
- [RFC 3875 — The Common Gateway Interface (CGI/1.1)](https://datatracker.ietf.org/doc/html/rfc3875)
- [NGINX documentation](https://nginx.org/en/docs/) — reference behaviour for configuration and headers
- [MDN — HTTP overview](https://developer.mozilla.org/en-US/docs/Web/HTTP/Overview)
- `man 2 select`, `man 2 socket`, `man 2 accept`, `man 2 fcntl` — the I/O multiplexing primitives
- [Beej's Guide to Network Programming](https://beej.us/guide/bgnet/)

### AI usage

AI tools were used in this project for **research** (clarifying HTTP/1.1 and CGI RFC semantics, `select()`-based event-loop design questions, comparing expected behaviour with NGINX) and for **code review** (auditing the implementation against the subject's constraints — non-blocking I/O rules, status-code accuracy, CGI environment variables — and spotting edge-case bugs). All code was written, understood, and validated by the team.
