#!/usr/bin/perl

# Tests for proxy header controls with ngx_condition_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT http_content /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy rewrite ngx_condition_module
	ngx_http_proxy_filter_module ngx_http_proxy_headers_control_module/)
	->plan(35);

$t->write_file_expand('nginx.conf', <<'EOF');

%%TEST_GLOBALS%%

daemon off;

events {
}

http {
    %%TEST_GLOBALS_HTTP%%

    proxy_request_header_control set X-Inherited parent;
    proxy_response_header_control set X-Response-Inherited parent;

    server {
        listen       127.0.0.1:8081;
        server_name  backend;

        add_header X-Existing original always;
        add_header X-Multi first always;
        add_header X-Multi second always;
        add_header X-Rewrite original always;
        add_header X-Debug-One one always;
        add_header X-Debug-Two two always;

        location / {
            return 200 "$http_x_set|$http_x_clear|$http_x_add|$http_x_missing|$http_x_append|$http_x_rewrite|$http_x_absent|$http_x_pass|$http_x_wild_one|$http_x_wild_two|$http_x_chain|$http_x_cond|$http_x_inherited|$http_x_if";
        }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        condition special str_eq $arg_mode special;

        location = /ops {
            proxy_request_header_control set X-Set $arg_value;
            proxy_request_header_control clear X-Clear;
            proxy_request_header_control add X-Add added;
            proxy_request_header_control add X-Missing created;
            proxy_request_header_control append X-Append appended;
            proxy_request_header_control rewrite X-Rewrite rewritten;
            proxy_request_header_control rewrite X-Absent ignored;
            proxy_request_header_control pass X-Pass;
            proxy_request_header_control clear X-Wild-*;
            proxy_request_header_control set -n X-Chain first;
            proxy_request_header_control rewrite X-Chain second;

            proxy_response_header_control set X-Existing $arg_value;
            proxy_response_header_control clear X-Debug-*;
            proxy_response_header_control add X-Added added;
            proxy_response_header_control append X-Multi appended;
            proxy_response_header_control rewrite X-Rewrite rewritten;
            proxy_response_header_control rewrite X-Absent ignored;

            proxy_pass http://127.0.0.1:8081;
        }

        location = /conditional {
            when special {
                proxy_request_header_control set X-Cond selected;
                proxy_response_header_control set X-Response-Cond selected;
            }

            proxy_request_header_control set X-Cond fallback;
            proxy_response_header_control set X-Response-Cond fallback;
            proxy_pass http://127.0.0.1:8081;
        }

        location = /order {
            proxy_request_header_control set X-Cond first;
            proxy_response_header_control set X-Response-Cond first;

            when special {
                proxy_request_header_control set X-Cond second;
                proxy_response_header_control set X-Response-Cond second;
            }

            proxy_pass http://127.0.0.1:8081;
        }

        location = /inherit {
            proxy_pass http://127.0.0.1:8081;
        }

        location = /override {
            proxy_request_header_control set X-Inherited child;
            proxy_response_header_control set X-Response-Inherited child;
            proxy_pass http://127.0.0.1:8081;
        }

        location = /conditional-inherit {
            when special {
                proxy_request_header_control set X-Inherited selected;
                proxy_response_header_control set X-Response-Inherited selected;
            }

            proxy_pass http://127.0.0.1:8081;
        }

        location = /if-context {
            if ($arg_apply) {
                proxy_request_header_control set X-If applied;
            }

            proxy_pass http://127.0.0.1:8081;
        }
    }
}

EOF

$t->run();

###############################################################################

my $ops = response('/ops?value=dynamic', <<'EOF');
X-Set: old-one
X-Set: old-two
X-Clear: remove
X-Add: existing
X-Append: base
X-Rewrite: old
X-Pass: keep
X-Wild-One: one
X-Wild-Two: two
X-Chain: old
EOF

my @field = split /\|/, http_content($ops), -1;

is($field[0], 'dynamic', 'set replaces duplicate request headers');
is($field[1], '', 'clear removes a request header');
is($field[2], 'existing', 'add preserves an existing request header');
is($field[3], 'created', 'add creates a missing request header');
is($field[4], 'base, appended', 'append adds another request header value');
is($field[5], 'rewritten', 'rewrite changes an existing request header');
is($field[6], '', 'rewrite does not create a missing request header');
is($field[7], 'keep', 'pass leaves a request header unchanged');
is($field[8], '', 'wildcard clear removes the first matching header');
is($field[9], '', 'wildcard clear removes the second matching header');
is($field[10], 'second', '-n allows the next exact rule to execute');
is($field[12], 'parent', 'unrelated request rules inherit from http context');

is(header_value($ops, 'X-Existing'), 'dynamic',
	'set replaces an upstream response header');
unlike($ops, qr/^X-Debug-(?:One|Two):/mi,
	'wildcard clear removes matching response headers');
is(header_value($ops, 'X-Added'), 'added',
	'add creates a missing response header');
is_deeply([header_values($ops, 'X-Multi')], [qw/first second appended/],
	'append preserves response duplicates and adds a value');
is(header_value($ops, 'X-Rewrite'), 'rewritten',
	'rewrite changes an existing response header');
ok(!defined header_value($ops, 'X-Absent'),
	'rewrite does not create a missing response header');
is(header_value($ops, 'X-Response-Inherited'), 'parent',
	'unrelated response rules inherit from http context');

my $selected = response('/conditional?mode=special');
is(field($selected, 11), 'selected', 'matching condition selects request rule');
is(header_value($selected, 'X-Response-Cond'), 'selected',
	'matching condition selects response rule');

my $fallback = response('/conditional?mode=other');
is(field($fallback, 11), 'fallback', 'condition miss selects request fallback');
is(header_value($fallback, 'X-Response-Cond'), 'fallback',
	'condition miss selects response fallback');

my $order = response('/order?mode=special');
is(field($order, 11), 'first',
	'first unconditional request rule wins over a later condition');
is(header_value($order, 'X-Response-Cond'), 'first',
	'first unconditional response rule wins over a later condition');

my $inherited = response('/inherit');
is(field($inherited, 12), 'parent', 'request rule inherits unchanged');
is(header_value($inherited, 'X-Response-Inherited'), 'parent',
	'response rule inherits unchanged');

my $overridden = response('/override');
is(field($overridden, 12), 'child', 'child request rule disables parent rule');
is(header_value($overridden, 'X-Response-Inherited'), 'child',
	'child response rule disables parent rule');

my $conditional_child = response('/conditional-inherit?mode=special');
is(field($conditional_child, 12), 'selected',
	'matching conditional child request rule wins');
is(header_value($conditional_child, 'X-Response-Inherited'), 'selected',
	'matching conditional child response rule wins');

my $parent_fallback = response('/conditional-inherit?mode=other');
is(field($parent_fallback, 12), 'parent',
	'parent request rule remains when child condition misses');
is(header_value($parent_fallback, 'X-Response-Inherited'), 'parent',
	'parent response rule remains when child condition misses');

is(field(response('/if-context?apply=1'), 13), 'applied',
	'directive works in location if context');
is(field(response('/if-context?apply=0'), 13), '',
	'location if rule is absent when its rewrite condition misses');

###############################################################################

sub field {
	my ($response, $index) = @_;

	return (split /\|/, http_content($response), -1)[$index];
}


sub header_value {
	my ($response, $name) = @_;
	my ($value) = header_values($response, $name);

	return $value;
}


sub header_values {
	my ($response, $name) = @_;

	return $response =~ /^\Q$name\E:\s*(.*?)\x0d?$/gmi;
}


sub response {
	my ($uri, $headers) = @_;
	$headers ||= '';

	return http(<<EOF);
GET $uri HTTP/1.1
Host: localhost
${headers}Connection: close

EOF
}

###############################################################################
