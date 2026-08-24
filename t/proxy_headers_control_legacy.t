#!/usr/bin/perl

# Tests for legacy proxy header predicates without ngx_condition_module.

###############################################################################

use warnings;
use strict;

use Test::More;

BEGIN { use FindBin; chdir($FindBin::Bin); }

use Test::Nginx qw/ :DEFAULT http_content /;

###############################################################################

select STDERR; $| = 1;
select STDOUT; $| = 1;

my $t = Test::Nginx->new()->has(qw/http proxy rewrite
	ngx_http_proxy_filter_module ngx_http_proxy_headers_control_module/);

plan(skip_all => 'legacy predicate build required')
	if $t->has_module('ngx_condition_module');

$t->plan(12);

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

        location / {
            return 200 "$http_x_positive|$http_x_negative|$http_x_inherited";
        }
    }

    server {
        listen       127.0.0.1:8080;
        server_name  localhost;

        location = /predicates {
            proxy_request_header_control set X-Positive yes if=$arg_apply;
            proxy_request_header_control set X-Negative yes if!=$arg_skip;
            proxy_response_header_control set X-Response-Positive yes if=$arg_apply;
            proxy_response_header_control set X-Response-Negative yes if!=$arg_skip;
            proxy_pass http://127.0.0.1:8081;
        }

        location = /inherit {
            proxy_request_header_control set X-Inherited child if=$arg_apply;
            proxy_response_header_control set X-Response-Inherited child if=$arg_apply;
            proxy_pass http://127.0.0.1:8081;
        }
    }
}

EOF

$t->run();

###############################################################################

my $hit = response('/predicates?apply=1&skip=0');
is(field($hit, 0), 'yes', 'if= applies request rule to a truthy value');
is(field($hit, 1), 'yes', 'if!= applies request rule to a false value');
is(header_value($hit, 'X-Response-Positive'), 'yes',
	'if= applies response rule to a truthy value');
is(header_value($hit, 'X-Response-Negative'), 'yes',
	'if!= applies response rule to a false value');

my $miss = response('/predicates?apply=0&skip=1');
is(field($miss, 0), '', 'if= skips request rule for zero');
is(field($miss, 1), '', 'if!= skips request rule for a truthy value');
ok(!defined header_value($miss, 'X-Response-Positive'),
	'if= skips response rule for zero');
ok(!defined header_value($miss, 'X-Response-Negative'),
	'if!= skips response rule for a truthy value');

my $child = response('/inherit?apply=1');
is(field($child, 2), 'child', 'matching legacy child request rule wins');
is(header_value($child, 'X-Response-Inherited'), 'child',
	'matching legacy child response rule wins');

my $parent = response('/inherit?apply=0');
is(field($parent, 2), 'parent',
	'parent request rule remains when legacy child condition misses');
is(header_value($parent, 'X-Response-Inherited'), 'parent',
	'parent response rule remains when legacy child condition misses');

###############################################################################

sub field {
	my ($response, $index) = @_;

	return (split /\|/, http_content($response), -1)[$index];
}


sub header_value {
	my ($response, $name) = @_;
	my ($value) = $response =~ /^\Q$name\E:\s*(.*?)\x0d?$/mi;

	return $value;
}


sub response {
	my ($uri) = @_;

	return http(<<EOF);
GET $uri HTTP/1.1
Host: localhost
Connection: close

EOF
}

###############################################################################
