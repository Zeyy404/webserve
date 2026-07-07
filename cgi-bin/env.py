#!/usr/bin/env python3
"""Dump the CGI meta-variables the server provides (RFC 3875)."""

import os
import sys

body = sys.stdin.read()

print("Content-Type: text/plain")
print()
for name in ("REQUEST_METHOD", "SCRIPT_NAME", "SCRIPT_FILENAME", "PATH_INFO",
             "PATH_TRANSLATED", "QUERY_STRING", "CONTENT_LENGTH", "CONTENT_TYPE",
             "SERVER_NAME", "SERVER_PORT", "SERVER_PROTOCOL", "REMOTE_ADDR",
             "GATEWAY_INTERFACE", "REQUEST_URI"):
    print(name.lower() + "=" + os.environ.get(name, ""))
print("body_length=" + str(len(body)))
