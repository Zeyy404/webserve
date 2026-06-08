# Webserv Mandatory Checklist

This checklist tracks only mandatory requirements (no bonus scope).

## Done

- [x] Build with C++98 flags and strict warnings (`-Wall -Wextra -Werror -std=c++98`)
- [x] Config file loading from argument or default path
- [x] Basic config parsing for:
  - [x] `server` blocks
  - [x] `location` blocks
  - [x] `listen`, `server_name`, `root`, `index`, `client_max_body_size`, `error_page`
  - [x] `allowed_methods`, `autoindex`, `upload_path`, `return`, `cgi_extension`
- [x] Non-blocking listening sockets
- [x] Single `select()`-based event loop for socket I/O
- [x] Read/write operations on sockets gated by readiness (`FD_ISSET`)
- [x] Accept client connections and manage connection lifecycle
- [x] Basic client timeout handling
- [x] HTTP request parsing baseline:
  - [x] request line
  - [x] headers
  - [x] body via `Content-Length`
  - [x] chunked transfer decoding
- [x] HTTP response building baseline:
  - [x] status line
  - [x] headers
  - [x] body serialization
  - [x] common status messages
- [x] Route matching (longest prefix style)
- [x] Method restriction checks from route config
- [x] GET static file serving baseline
- [x] Basic autoindex HTML generation
- [x] Basic redirect response (`301` with `Location`)
- [x] Basic error responses (`404`, `405`, etc.)
- [x] `client_max_body_size` enforcement (`413`)
- [x] `POST` upload flow to configured `upload_path`
- [x] `DELETE` removes regular files and returns `204`
- [x] Malformed request line/header and invalid `Content-Length` return `400`
- [x] HTTP/1.1 requests without `Host` return `400`
- [x] CGI request detection and dispatch through the main `select()` loop
- [x] CGI environment setup, request body forwarding, output parsing, timeout/failure handling
- [x] Configured custom error page lookup for project `www/error_pages`
- [x] Default config listens on distinct ports without bind conflicts
- [x] Smoke-tested with `curl` for:
  - [x] `GET /` returns `200`
  - [x] missing file returns `404`
  - [x] disallowed method returns `405`
  - [x] raw upload returns `201` and stores the file
  - [x] multipart upload returns `201` and stores the file
  - [x] chunked upload returns `201` with decoded body
  - [x] DELETE uploaded file returns `204`
  - [x] oversized body returns `413`
  - [x] CGI GET/POST return `200`

## Left To Do (Mandatory)

### Core HTTP correctness

- [x] Strict request validation (malformed request line/headers -> `400`)
- [x] Correct header validation rules for HTTP/1.1 (duplicate header policy and full invalid-form audit)
- [x] Proper chunked transfer decoding (not only completeness detection)
- [x] Better request state handling for partial/incremental reads and pipelining safety

### Methods and behavior

- [x] Full `POST` support for CGI and uploads
- [x] Full `DELETE` support for regular files
- [x] Implement request body size enforcement (`client_max_body_size` -> `413`)
- [x] Correct method behavior per route and per resource (status code accuracy)

### Uploads

- [x] Implement actual upload write flow to configured `upload_path`
- [x] Validate upload errors and return proper status codes

### CGI (mandatory)

- [x] Implement CGI request detection and dispatch
- [x] Fork/exec CGI process correctly
- [x] Build CGI environment variables from request/server context
- [x] Forward request body to CGI when required
- [x] Parse CGI output into valid HTTP response
- [x] Add CGI timeout and failure handling

### Static files and routing completeness

- [x] Harden filesystem path resolution against plain and URL-encoded traversal edge cases
- [x] Finalize index resolution behavior for directories
- [x] Ensure autoindex behavior is fully compliant with route config
- [x] Improve redirection handling semantics (status variants and headers)

### Error pages and status accuracy

- [x] Serve configured custom error pages consistently
- [x] Audit and correct status codes for all edge cases

### Connection management and resilience

- [x] Keep-alive lifecycle correctness across multiple requests per connection
- [x] Ensure requests never hang indefinitely (timeouts + parse failure exits)
- [x] Robust cleanup on disconnects, short reads/writes, and socket errors

### Multi-server / listen behavior

- [x] Handle duplicate or overlapping listen definitions more cleanly in provided config
- [x] Ensure expected behavior when multiple server configs share address/port

### Mandatory quality and evaluation readiness

- [x] Compare core behaviors against NGINX for parity in key scenarios
- [x] Add repeatable test checklist/scripts for evaluator demos (GET/POST/DELETE, uploads, CGI, error pages)
- [x] Stress test for stability under concurrent clients
- [x] Verify no blocking socket/CGI pipe I/O occurs outside readiness checks
- [x] Validate full mandatory path end-to-end with provided config files

## Explicitly Out of Scope

- [ ] Bonus features (cookies, sessions, extra CGI types)
