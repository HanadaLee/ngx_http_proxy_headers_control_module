# ngx_http_proxy_headers_control_module

An nginx module for fine-grained control over HTTP headers in proxy traffic — both request headers sent to upstream servers and response headers returned to clients.

## Features

- **Request header control** — modify headers before they are sent to the upstream server
- **Response header control** — modify headers from the upstream before they reach the client
- **Multiple operations** — `set`, `clear`, `add`, `append`, `rewrite`
- **Conditional execution** — use named `expr`/`when` rules when `ngx_expr_module` is built, or legacy `if=`/`if!=` otherwise
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

To enable named conditions, build both addons statically in the same nginx configuration:

```bash
./configure \
    --add-module=/path/to/ngx_expr_module \
    --add-module=/path/to/ngx_http_proxy_filter_module \
    --add-module=/path/to/ngx_http_proxy_headers_control_module
```

Conditional syntax is selected at compile time. With `ngx_expr_module`, use `expr` and `when`; `if=` and `if!=` parameters are rejected. Without it, `when` is unavailable and the legacy parameters remain supported.

## Directives

### proxy_request_header_control

**Syntax:** `proxy_request_header_control <operation> [-n] <header-name> [<value>]`

**Default:** —  
**Context:** `http`, `server`, `location`, `if in location`, `http when`, `server when`, `location when`

Controls headers sent to the upstream server.

> **Note on the `Host` header:** Modifying the `Host` request header is not allowed. If you need to change the upstream `Host`, use the built-in [`proxy_set_header`](https://nginx.org/en/docs/http/ngx_http_proxy_module.html#proxy_set_header) directive instead.

### proxy_response_header_control

**Syntax:** `proxy_response_header_control <operation> [-n] <header-name> [<value>]`

**Default:** —  
**Context:** `http`, `server`, `location`, `if in location`, `http when`, `server when`, `location when`

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

    # With ngx_expr_module
    expr debug_enabled str_eq $arg_debug 1;
    when debug_enabled {
        proxy_request_header_control set X-Debug true;
    }

    # Without ngx_expr_module, use this instead:
    # proxy_request_header_control set X-Debug true if=$arg_debug;
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

With `ngx_expr_module`, define a named condition and place one or more rules in a `when` block. A `when` block may reference multiple condition names, and a name may be negated with `!`:

```nginx
expr has_special_value !is_empty $special_value;
expr is_internal str_eq $http_x_internal 1;

when has_special_value !is_internal {
    proxy_request_header_control set X-Special $special_value;
    proxy_request_header_control clear X-Internal;
}
```

Without `ngx_expr_module`, use the legacy complex-value parameters:

- `if=$var` — apply if the variable is non-empty and not `"0"`
- `if!=$var` — apply if the variable is empty or `"0"`

```nginx
proxy_request_header_control set X-Special $special_value if=$special_value;
proxy_request_header_control clear X-Internal if=$http_x_internal;
```

## Inheritance

Rules defined in outer contexts (`http` → `server` → `location`) are inherited and merged. Child rules execute before inherited parent rules. An unconditional child exact-header rule without `-n` disables parent rules for the same header. A rule associated with `when`, or with legacy `if=`/`if!=`, does not disable the parent fallback when its condition does not match.

## License

See [LICENSE](./LICENSE).
