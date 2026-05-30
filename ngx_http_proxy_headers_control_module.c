
/*
 * Copyright (C) Hanada
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

#if !defined(NGX_HTTP_PROXY_FILTER) || !(NGX_HTTP_PROXY_FILTER)
#error "ngx_http_proxy_headers_control_module requires NGX_HTTP_PROXY_FILTER"
#endif

#include <ngx_http_proxy_filter_module.h>


typedef enum {
    NGX_HTTP_PROXY_HEADERS_CONTROL_SET = 0,
    NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR,
    NGX_HTTP_PROXY_HEADERS_CONTROL_ADD,
    NGX_HTTP_PROXY_HEADERS_CONTROL_APPEND,
    NGX_HTTP_PROXY_HEADERS_CONTROL_REWRITE,
    NGX_HTTP_PROXY_HEADERS_CONTROL_PASS
} ngx_http_proxy_headers_control_opcode_e;


typedef struct {
    ngx_uint_t                              *bits;
    ngx_uint_t                               size;
} ngx_http_proxy_headers_control_bitmap_t;


typedef struct {
    ngx_array_t                             *request_headers;
    ngx_array_t                             *response_headers;
    ngx_uint_t                               request_headers_cnt;
    ngx_uint_t                               response_headers_cnt;
} ngx_http_proxy_headers_control_loc_conf_t;


typedef struct {
    ngx_http_complex_value_t                 value;
    ngx_uint_t                               hash;
    ngx_str_t                                key;
    ngx_http_proxy_headers_control_opcode_e  opcode;
    ngx_http_complex_value_t                *filter;
    ngx_flag_t                               negative;
    ngx_flag_t                               next;
    ngx_uint_t                               id;
    unsigned                                 wildcard:1;
    unsigned                                 request:1;
} ngx_http_proxy_headers_control_header_val_t;


static void *ngx_http_proxy_headers_control_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_proxy_headers_control_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_proxy_headers_control_merge_one_array(ngx_conf_t *cf,
    ngx_array_t **curr_headers, ngx_array_t *prev_headers,
    ngx_uint_t *curr_cnt, ngx_uint_t prev_cnt);

static char *ngx_http_proxy_headers_control_parse_directive(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf, ngx_uint_t request);
static ngx_int_t ngx_http_proxy_headers_control_parse_header(ngx_conf_t *cf,
    ngx_str_t *cmd_name, ngx_str_t *key, ngx_str_t *value,
    ngx_http_proxy_headers_control_header_val_t *hv);
static ngx_int_t ngx_http_proxy_headers_control_exec(ngx_http_request_t *r,
    ngx_list_t *headers, ngx_array_t *rules, ngx_uint_t rules_cnt,
    ngx_uint_t *changed);
static ngx_int_t ngx_http_proxy_headers_control_exec_header(
    ngx_http_request_t *r, ngx_list_t *headers,
    ngx_http_proxy_headers_control_header_val_t *hv, ngx_uint_t *changed);
static ngx_int_t ngx_http_proxy_headers_control_set_header(
    ngx_http_request_t *r, ngx_list_t *headers,
    ngx_http_proxy_headers_control_header_val_t *hv, ngx_str_t *value);
static ngx_int_t ngx_http_proxy_headers_control_create_header(
    ngx_http_request_t *r, ngx_list_t *headers,
    ngx_http_proxy_headers_control_header_val_t *hv, ngx_str_t *value);
static ngx_int_t ngx_http_proxy_headers_control_match_header(
    ngx_table_elt_t *h, ngx_http_proxy_headers_control_header_val_t *hv);
static ngx_int_t ngx_http_proxy_headers_control_bitmap_init(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t size,
    ngx_pool_t *pool);
static void ngx_http_proxy_headers_control_bitmap_set(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t bit);
static ngx_int_t ngx_http_proxy_headers_control_bitmap_isset(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t bit);

static char *ngx_http_proxy_request_headers_control(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static char *ngx_http_proxy_response_headers_control(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_proxy_headers_control_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_proxy_request_headers_control_filter(
    ngx_http_request_t *r, ngx_http_proxy_filter_ctx_t *ctx);
static ngx_int_t ngx_http_proxy_response_headers_control_filter(
    ngx_http_request_t *r, ngx_http_proxy_filter_ctx_t *ctx);


static ngx_command_t  ngx_http_proxy_headers_control_commands[] = {

    { ngx_string("proxy_request_header_control"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_2MORE,
      ngx_http_proxy_request_headers_control,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("proxy_response_header_control"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF
                        |NGX_CONF_2MORE,
      ngx_http_proxy_response_headers_control,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_proxy_headers_control_module_ctx = {
    NULL,                                           /* preconfiguration */
    ngx_http_proxy_headers_control_init,            /* postconfiguration */

    NULL,                                           /* create main conf */
    NULL,                                           /* init main conf */

    NULL,                                           /* create server conf */
    NULL,                                           /* merge server conf */

    ngx_http_proxy_headers_control_create_loc_conf, /* create location conf */
    ngx_http_proxy_headers_control_merge_loc_conf   /* merge location conf */
};


ngx_module_t  ngx_http_proxy_headers_control_module = {
    NGX_MODULE_V1,
    &ngx_http_proxy_headers_control_module_ctx,
    ngx_http_proxy_headers_control_commands,
    NGX_HTTP_MODULE,
    NULL,                                           /* init master */
    NULL,                                           /* init module */
    NULL,                                           /* init process */
    NULL,                                           /* init thread */
    NULL,                                           /* exit thread */
    NULL,                                           /* exit process */
    NULL,                                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static void *
ngx_http_proxy_headers_control_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_proxy_headers_control_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool,
                       sizeof(ngx_http_proxy_headers_control_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    return conf;
}


static ngx_int_t
ngx_http_proxy_headers_control_merge_one_array(ngx_conf_t *cf,
    ngx_array_t **curr_headers, ngx_array_t *prev_headers,
    ngx_uint_t *curr_cnt, ngx_uint_t prev_cnt)
{
    ngx_uint_t                                   i, j;
    ngx_uint_t                                   orig_len, prev_len;
    ngx_uint_t                                   copy_count, pos;
    ngx_http_proxy_headers_control_header_val_t *prev_h, *h;
    ngx_http_proxy_headers_control_bitmap_t      disable_map;

    orig_len = (*curr_headers)->nelts;
    prev_len = prev_headers->nelts;

    h = (*curr_headers)->elts;
    prev_h = prev_headers->elts;

    if (ngx_http_proxy_headers_control_bitmap_init(&disable_map, prev_cnt,
                                                   cf->pool)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (i = 0; i < orig_len; i++) {

        if (h[i].filter || h[i].next || h[i].wildcard) {
            continue;
        }

        for (j = 0; j < prev_len; j++) {
            if (prev_h[j].wildcard) {
                continue;
            }

            if (h[i].key.len != prev_h[j].key.len) {
                continue;
            }

            if (ngx_strncasecmp(h[i].key.data, prev_h[j].key.data,
                                h[i].key.len)
                != 0)
            {
                continue;
            }

            ngx_http_proxy_headers_control_bitmap_set(&disable_map,
                                                      prev_h[j].id);
        }
    }

    copy_count = 0;

    for (j = 0; j < prev_len; j++) {
        if (ngx_http_proxy_headers_control_bitmap_isset(&disable_map,
                                                        prev_h[j].id)
            != NGX_OK)
        {
            copy_count++;
        }
    }

    if (copy_count > 0) {
        if (ngx_array_push_n(*curr_headers, copy_count) == NULL) {
            return NGX_ERROR;
        }

        h = (*curr_headers)->elts;
        pos = orig_len;

        for (j = 0; j < prev_len; j++) {
            if (ngx_http_proxy_headers_control_bitmap_isset(&disable_map,
                                                            prev_h[j].id)
                != NGX_OK)
            {
                h[pos++] = prev_h[j];
            }
        }
    }

    h = (*curr_headers)->elts;

    for (i = 0; i < (*curr_headers)->nelts; i++) {
        h[i].id = i;
    }

    *curr_cnt = (*curr_headers)->nelts;

    return NGX_OK;
}


static char *
ngx_http_proxy_headers_control_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child)
{
    ngx_http_proxy_headers_control_loc_conf_t  *prev = parent;
    ngx_http_proxy_headers_control_loc_conf_t  *conf = child;

    if (conf->request_headers == NULL || conf->request_headers->nelts == 0) {
        conf->request_headers = prev->request_headers;
        conf->request_headers_cnt = prev->request_headers_cnt;

    } else if (prev->request_headers && prev->request_headers->nelts) {
        if (ngx_http_proxy_headers_control_merge_one_array(cf,
                                           &conf->request_headers,
                                           prev->request_headers,
                                           &conf->request_headers_cnt,
                                           prev->request_headers_cnt)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    if (conf->response_headers == NULL || conf->response_headers->nelts == 0) {
        conf->response_headers = prev->response_headers;
        conf->response_headers_cnt = prev->response_headers_cnt;

    } else if (prev->response_headers && prev->response_headers->nelts) {
        if (ngx_http_proxy_headers_control_merge_one_array(cf,
                                           &conf->response_headers,
                                           prev->response_headers,
                                           &conf->response_headers_cnt,
                                           prev->response_headers_cnt)
            != NGX_OK)
        {
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_http_proxy_request_headers_control(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_http_proxy_headers_control_parse_directive(cf, cmd, conf, 1);
}


static char *
ngx_http_proxy_response_headers_control(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf)
{
    return ngx_http_proxy_headers_control_parse_directive(cf, cmd, conf, 0);
}


static char *
ngx_http_proxy_headers_control_parse_directive(ngx_conf_t *cf,
    ngx_command_t *cmd, void *conf, ngx_uint_t request)
{
    ngx_int_t                                    rc;
    ngx_str_t                                   *arg, *cmd_name;
    ngx_str_t                                    key, value, s;
    ngx_uint_t                                   cur;
    ngx_http_compile_complex_value_t             ccv;
    ngx_http_proxy_headers_control_opcode_e      opcode;
    ngx_http_proxy_headers_control_header_val_t *hv;
    ngx_http_proxy_headers_control_loc_conf_t   *hlcf;
    ngx_array_t                                **headers;
    ngx_uint_t                                  *headers_cnt;

    hlcf = conf;
    headers = request ? &hlcf->request_headers : &hlcf->response_headers;
    headers_cnt = request ? &hlcf->request_headers_cnt
                          : &hlcf->response_headers_cnt;
    arg = cf->args->elts;
    cmd_name = &cmd->name;

    if (cf->args->nelts < 3) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0, "%V: too few arguments",
                      cmd_name);
        return NGX_CONF_ERROR;
    }

    if (arg[1].len == 3
        && ngx_strncasecmp(arg[1].data, (u_char *) "set", 3) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_SET;

    } else if (arg[1].len == 5
               && ngx_strncasecmp(arg[1].data, (u_char *) "clear", 5) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR;

    } else if (arg[1].len == 3
               && ngx_strncasecmp(arg[1].data, (u_char *) "add", 3) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_ADD;

    } else if (arg[1].len == 6
               && ngx_strncasecmp(arg[1].data, (u_char *) "append", 6) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_APPEND;

    } else if (arg[1].len == 7
               && ngx_strncasecmp(arg[1].data, (u_char *) "rewrite", 7) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_REWRITE;

    } else if (arg[1].len == 4
               && ngx_strncasecmp(arg[1].data, (u_char *) "pass", 4) == 0)
    {
        opcode = NGX_HTTP_PROXY_HEADERS_CONTROL_PASS;

    } else {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: unknown operation \"%V\"", cmd_name, &arg[1]);
        return NGX_CONF_ERROR;
    }

    if (*headers == NULL) {
        *headers = ngx_array_create(cf->pool, 1,
                  sizeof(ngx_http_proxy_headers_control_header_val_t));
        if (*headers == NULL) {
            return NGX_CONF_ERROR;
        }
    }

    hv = ngx_array_push(*headers);
    if (hv == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(hv, sizeof(ngx_http_proxy_headers_control_header_val_t));
    hv->opcode = opcode;
    hv->request = request;

    cur = 2;

    if (cf->args->nelts > cur
        && arg[cur].len == 2
        && ngx_strncmp(arg[cur].data, "-n", 2) == 0)
    {
        hv->next = 1;
        cur++;
    }

    if (cf->args->nelts <= cur) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: header name is required", cmd_name);
        return NGX_CONF_ERROR;
    }

    key = arg[cur++];
    ngx_str_null(&value);

    if (opcode != NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR
        && opcode != NGX_HTTP_PROXY_HEADERS_CONTROL_PASS)
    {
        if (cf->args->nelts <= cur) {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                          "%V: header value is required", cmd_name);
            return NGX_CONF_ERROR;
        }

        value = arg[cur++];
    }

    if (cf->args->nelts > cur) {

        if (arg[cur].len > 3
            && ngx_strncmp(arg[cur].data, "if=", 3) == 0)
        {
            hv->negative = 0;
            s.len = arg[cur].len - 3;
            s.data = arg[cur].data + 3;

        } else if (arg[cur].len > 4
                   && ngx_strncmp(arg[cur].data, "if!=", 4) == 0)
        {
            hv->negative = 1;
            s.len = arg[cur].len - 4;
            s.data = arg[cur].data + 4;

        } else {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                          "%V: invalid parameter \"%V\"", cmd_name,
                          &arg[cur]);
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

        ccv.cf = cf;
        ccv.value = &s;
        ccv.complex_value = ngx_palloc(cf->pool,
                                       sizeof(ngx_http_complex_value_t));
        if (ccv.complex_value == NULL) {
            return NGX_CONF_ERROR;
        }

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        hv->filter = ccv.complex_value;
        cur++;
    }

    if (cf->args->nelts > cur) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: invalid parameter \"%V\"", cmd_name, &arg[cur]);
        return NGX_CONF_ERROR;
    }

    rc = ngx_http_proxy_headers_control_parse_header(cf, cmd_name, &key,
                                                     &value, hv);
    if (rc != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    if (request && !hv->wildcard && hv->key.len == 4
        && ngx_strncasecmp(hv->key.data, (u_char *) "host", 4) == 0)
    {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: the \"Host\" header is not allowed", cmd_name);
        return NGX_CONF_ERROR;
    }

    hv->id = (*headers_cnt)++;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_parse_header(ngx_conf_t *cf,
    ngx_str_t *cmd_name, ngx_str_t *key, ngx_str_t *value,
    ngx_http_proxy_headers_control_header_val_t *hv)
{
    u_char                           *p;
    ngx_uint_t                        i;
    ngx_http_compile_complex_value_t  ccv;

    if (key->len == 0) {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: empty header name", cmd_name);
        return NGX_ERROR;
    }

    for (i = 0; i < key->len; i++) {
        if (key->data[i] == ' ' || key->data[i] == '\t'
            || key->data[i] == CR || key->data[i] == LF)
        {
            ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                          "%V: header name contains whitespace: \"%V\"",
                          cmd_name, key);
            return NGX_ERROR;
        }
    }

    hv->wildcard = (key->data[key->len - 1] == '*');

    if (hv->wildcard
        && hv->opcode != NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR)
    {
        ngx_log_error(NGX_LOG_ERR, cf->log, 0,
                      "%V: wildcard header names are only supported by clear",
                      cmd_name);
        return NGX_ERROR;
    }

    hv->hash = ngx_hash_key_lc(key->data, key->len);
    hv->key = *key;

    if (hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR
        || hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_PASS
        || value->len == 0)
    {
        ngx_memzero(&hv->value, sizeof(ngx_http_complex_value_t));
        return NGX_OK;
    }

    p = ngx_palloc(cf->pool, value->len + 1);
    if (p == NULL) {
        return NGX_ERROR;
    }

    ngx_memcpy(p, value->data, value->len);
    p[value->len] = '\0';
    value->data = p;
    value->len++;

    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = value;
    ccv.complex_value = &hv->value;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_exec(ngx_http_request_t *r, ngx_list_t *headers,
    ngx_array_t *rules, ngx_uint_t rules_cnt, ngx_uint_t *changed)
{
    ngx_int_t                                     rc;
    ngx_uint_t                                    i, j;
    ngx_http_proxy_headers_control_header_val_t  *h;
    ngx_http_proxy_headers_control_bitmap_t       locked;

    if (changed) {
        *changed = 0;
    }

    if (rules == NULL || rules->nelts == 0) {
        return NGX_DECLINED;
    }

    if (headers == NULL) {
        return NGX_ERROR;
    }

    if (ngx_http_proxy_headers_control_bitmap_init(&locked, rules_cnt, r->pool)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    h = rules->elts;

    for (i = 0; i < rules->nelts; i++) {

        if (!h[i].wildcard) {
            for (j = 0; j < i; j++) {
                if (h[j].wildcard) {
                    continue;
                }

                if (h[i].key.len != h[j].key.len) {
                    continue;
                }

                if (ngx_strncasecmp(h[i].key.data, h[j].key.data,
                                    h[i].key.len)
                    != 0)
                {
                    continue;
                }

                if (ngx_http_proxy_headers_control_bitmap_isset(&locked,
                                                                h[j].id)
                    != NGX_OK)
                {
                    continue;
                }

                break;
            }

            if (j < i) {
                continue;
            }
        }

        rc = ngx_http_proxy_headers_control_exec_header(r, headers, &h[i],
                                                        changed);

        if (rc == NGX_ERROR) {
            return rc;
        }

        if (rc == NGX_DECLINED) {
            continue;
        }

        if (!h[i].wildcard && !h[i].next) {
            ngx_http_proxy_headers_control_bitmap_set(&locked, h[i].id);
        }
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_exec_header(ngx_http_request_t *r,
    ngx_list_t *headers, ngx_http_proxy_headers_control_header_val_t *hv,
    ngx_uint_t *changed)
{
    ngx_str_t  value, val;

    if (hv->filter) {
        if (ngx_http_complex_value(r, hv->filter, &val) != NGX_OK) {
            return NGX_ERROR;
        }

        if (val.len == 0 || (val.len == 1 && val.data[0] == '0')) {
            if (!hv->negative) {
                return NGX_DECLINED;
            }

        } else {
            if (hv->negative) {
                return NGX_DECLINED;
            }
        }
    }

    if (hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_PASS) {
        return NGX_OK;
    }

    if (hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_CLEAR) {
        ngx_str_null(&value);

    } else {
        if (ngx_http_complex_value(r, &hv->value, &value) != NGX_OK) {
            return NGX_ERROR;
        }

        if (value.len) {
            value.len--;
        }
    }

    if (changed) {
        *changed = 1;
    }

    return ngx_http_proxy_headers_control_set_header(r, headers, hv, &value);
}


static ngx_int_t
ngx_http_proxy_headers_control_set_header(ngx_http_request_t *r,
    ngx_list_t *headers, ngx_http_proxy_headers_control_header_val_t *hv,
    ngx_str_t *value)
{
    ngx_uint_t        i;
    ngx_uint_t        matched;
    ngx_list_part_t  *part;
    ngx_table_elt_t  *h;

    if (hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_APPEND) {
        return ngx_http_proxy_headers_control_create_header(r, headers, hv,
                                                            value);
    }

    matched = 0;
    part = &headers->part;
    h = part->elts;

    for (i = 0; /* void */; i++) {

        if (i >= part->nelts) {
            if (part->next == NULL) {
                break;
            }

            part = part->next;
            h = part->elts;
            i = 0;
        }

        if (ngx_http_proxy_headers_control_match_header(&h[i], hv)
            != NGX_OK)
        {
            continue;
        }

        if (hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_ADD) {
            return NGX_OK;
        }

        if (value->len == 0 || matched) {
            h[i].hash = 0;
            h[i].value.len = 0;
            h[i].next = NULL;
            matched = 1;
            continue;
        }

        h[i].hash = hv->hash;
        h[i].value = *value;
        matched = 1;
    }

    if (matched) {
        return NGX_OK;
    }

    if (value->len == 0
        || hv->opcode == NGX_HTTP_PROXY_HEADERS_CONTROL_REWRITE)
    {
        return NGX_OK;
    }

    return ngx_http_proxy_headers_control_create_header(r, headers, hv, value);
}


static ngx_int_t
ngx_http_proxy_headers_control_create_header(ngx_http_request_t *r,
    ngx_list_t *headers, ngx_http_proxy_headers_control_header_val_t *hv,
    ngx_str_t *value)
{
    ngx_table_elt_t  *h;

    h = ngx_list_push(headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(h, sizeof(ngx_table_elt_t));

    h->hash = value->len ? hv->hash : 0;
    h->key = hv->key;
    h->value = *value;

    h->lowcase_key = ngx_pnalloc(r->pool, h->key.len);
    if (h->lowcase_key == NULL) {
        h->hash = 0;
        return NGX_ERROR;
    }

    ngx_strlow(h->lowcase_key, h->key.data, h->key.len);

    h->next = NULL;

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_match_header(ngx_table_elt_t *h,
    ngx_http_proxy_headers_control_header_val_t *hv)
{
    if (h->hash == 0) {
        return NGX_DECLINED;
    }

    if (hv->wildcard) {
        if (hv->request && h->key.len == 4
            && ngx_strncasecmp(h->key.data, (u_char *) "host", 4) == 0)
        {
            return NGX_DECLINED;
        }

        if (h->key.len < hv->key.len - 1) {
            return NGX_DECLINED;
        }

        if (ngx_strncasecmp(h->key.data, hv->key.data, hv->key.len - 1)
            != 0)
        {
            return NGX_DECLINED;
        }

        return NGX_OK;
    }

    if (h->key.len != hv->key.len) {
        return NGX_DECLINED;
    }

    if (ngx_strncasecmp(h->key.data, hv->key.data, hv->key.len) != 0) {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_bitmap_init(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t size,
    ngx_pool_t *pool)
{
    ngx_uint_t  n;

    bm->size = size;
    bm->bits = NULL;

    if (size == 0) {
        return NGX_OK;
    }

    n = (size + NGX_INT_T_LEN - 1) / NGX_INT_T_LEN;

    bm->bits = ngx_pcalloc(pool, n * sizeof(ngx_uint_t));
    if (bm->bits == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_proxy_headers_control_bitmap_set(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t bit)
{
    if (bm->bits && bit < bm->size) {
        bm->bits[bit / NGX_INT_T_LEN]
            |= (ngx_uint_t) 1 << (bit % NGX_INT_T_LEN);
    }
}


static ngx_int_t
ngx_http_proxy_headers_control_bitmap_isset(
    ngx_http_proxy_headers_control_bitmap_t *bm, ngx_uint_t bit)
{
    if (bm->bits == NULL || bit >= bm->size) {
        return NGX_DECLINED;
    }

    if ((bm->bits[bit / NGX_INT_T_LEN]
         & ((ngx_uint_t) 1 << (bit % NGX_INT_T_LEN)))
        == 0)
    {
        return NGX_DECLINED;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_proxy_headers_control_init(ngx_conf_t *cf)
{
    ngx_int_t                 rc;
    ngx_http_proxy_filter_pt  handler;

    handler = ngx_http_proxy_request_headers_control_filter;

    rc = ngx_http_proxy_filter_register(cf, NGX_HTTP_PROXY_REQUEST_FILTER,
                                        handler);
    if (rc != NGX_OK) {
        return rc;
    }

    handler = ngx_http_proxy_response_headers_control_filter;

    return ngx_http_proxy_filter_register(cf, NGX_HTTP_PROXY_HEADER_FILTER,
                                          handler);
}


static ngx_int_t
ngx_http_proxy_request_headers_control_filter(ngx_http_request_t *r,
    ngx_http_proxy_filter_ctx_t *ctx)
{
    ngx_int_t                                    rc;
    ngx_http_proxy_headers_control_loc_conf_t  *conf;

    if (ctx->headers == NULL) {
        return NGX_DECLINED;
    }

    conf = ngx_http_get_module_loc_conf(r,
                                        ngx_http_proxy_headers_control_module);

    rc = ngx_http_proxy_headers_control_exec(r, ctx->headers,
                                             conf->request_headers,
                                             conf->request_headers_cnt,
                                             NULL);
    if (rc != NGX_OK) {
        return rc;
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_proxy_response_headers_control_filter(ngx_http_request_t *r,
    ngx_http_proxy_filter_ctx_t *ctx)
{
    ngx_int_t                                    rc;
    ngx_uint_t                                   changed;
    ngx_http_proxy_headers_control_loc_conf_t  *conf;

    if (ctx->headers == NULL || ctx->headers_in == NULL) {
        return NGX_DECLINED;
    }

    conf = ngx_http_get_module_loc_conf(r,
                                        ngx_http_proxy_headers_control_module);

    rc = ngx_http_proxy_headers_control_exec(r, ctx->headers,
                                             conf->response_headers,
                                             conf->response_headers_cnt,
                                             &changed);
    if (rc != NGX_OK) {
        return rc;
    }

    if (!changed) {
        return NGX_DECLINED;
    }

    ctx->refresh_headers = 1;

    return NGX_DECLINED;
}
