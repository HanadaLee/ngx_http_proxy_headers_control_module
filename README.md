# ngx_http_proxy_headers_control_module

An nginx module for fine-grained control over HTTP headers in proxy traffic — both request headers sent to upstream servers and response headers returned to clients.

## Features

- **Request header control** — modify headers before they are sent to the upstream server
- **Response header control** — modify headers from the upstream before they reach the client
- **Multiple operations** — `set`, `clear`, `add`, `append`, `rewrite`
- **Conditional execution** — apply rules only when conditions are met (`if=`, `if!=`)
- **Wildcard support** — clear multiple headers matching a pattern (`Header-*`)
- **Configuration inheritance** — rules merge naturally across `http`, `server`, and `location` blocks

## Requirements

- nginx with the `ngx_http_proxy_filter_module` enabled
- This module must be compiled statically via `--add-module`

## Building

```bash
# Download nginx source
cd /path/to/nginx-source

# Add this module
./configure --add-module=/path/to/ngx_http_proxy_headers_control_module \
            --with-http_proxy_module
make
make install
```

## Directives

### proxy_request_header_control

**Syntax:** `proxy_request_header_control <operation> [-n] <header-name> [<value>] [if=<condition>|if!=<condition>]`  
**Default:** —  
**Context:** `http`, `server`, `location`, `if in location`

Controls headers sent to the upstream server.

### proxy_response_header_control

**Syntax:** `proxy_response_header_control <operation> [-n] <header-name> [<value>] [if=<condition>|if!=<condition>]`  
**Default:** —  
**Context:** `http`, `server`, `location`, `if in location`

Controls headers received from the upstream server.

## Operations

| Operation | Description |
|-----------|-------------|
| `set`     | Set the header value, replacing any existing value(s) |
| `clear`   | Remove the header entirely. Supports wildcards (e.g., `X-Debug-*`) |
| `add`     | Add the header only if it does not already exist |
| `append`  | Append a new header value (allowing duplicates) |
| `rewrite` | Rewrite the header value only if the header already exists |
| `pass`    | No-op; explicitly passes the header through |

## Examples

### Set a custom request header

```nginx
location /api/ {
    proxy_pass http://backend;
    proxy_request_header_control set X-Real-IP $remote_addr;
}
```

### Conditionally set a header

```nginx
location /api/ {
    proxy_pass http://backend;
    proxy_request_header_control set X-Debug true if=$arg_debug;
}
```

### Clear sensitive response headers

```nginx
location / {
    proxy_pass http://backend;
    proxy_response_header_control clear X-Powered-By;
    proxy_response_header_control clear Server;
}
```

### Clear a group of headers with wildcard

```nginx
location / {
    proxy_pass http://backend;
    proxy_response_header_control clear X-Debug-*;
}
```

### Append a value to an existing header

```nginx
location /api/ {
    proxy_pass http://backend;
    proxy_response_header_control append X-Frame-Options SAMEORIGIN;
}
```

### Only add if missing

```nginx
location /api/ {
    proxy_pass http://backend;
    proxy_request_header_control add X-Request-ID $request_id;
}
```

## Conditional Rules

Use `if=<expression>` or `if!=<expression>` to conditionally apply a rule. The expression is evaluated as a nginx complex value:

- `if=$var` — apply if the variable is non-empty and not `"0"`
- `if!=empty` — apply if the string is empty or `"0"`

```nginx
proxy_request_header_control set X-Special $special_value if=$special_value;
proxy_request_header_control clear X-Internal if=$http_x_internal;
```

## Inheritance

Rules defined in outer contexts (`http` → `server` → `location`) are inherited and merged. If a location defines a rule for the same header as an outer context, the outer rule is overridden (not merged) for that specific header.

## License

See [LICENSE](./LICENSE).
