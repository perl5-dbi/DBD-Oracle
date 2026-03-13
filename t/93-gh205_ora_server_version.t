#!perl
# gh#205 - ora_server_version() returns undef on first call because the
#          sub falls off the end without a return statement after computing
#          and caching the version.
# Also verifies that ora_server_version is correctly stored/fetched via
# the XS STORE/FETCH handlers in dbdimp.c.

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ db_handle /;

use Test::More import => [ qw( cmp_ok diag is is_deeply like ok plan skip ) ];

my $dbh = db_handle( { PrintError => 0 } )
    or plan skip_all => 'Unable to connect to Oracle';

plan tests => 8;

# Clear any cached value so we exercise the first-call code path
# (the XS connect may have pre-populated it).
delete $dbh->{ora_server_version};

my $ver = DBD::Oracle::db::ora_server_version($dbh);

ok( defined $ver, 'ora_server_version returns defined value on first call' )
    or diag "returned undef; v\$version may not be accessible";
is( ref $ver, 'ARRAY', 'return value is an array ref' )
    or diag "got: " . (defined $ver ? "'$ver'" : "undef");

SKIP: {
    skip 'ora_server_version returned undef or not an arrayref', 6
        unless defined $ver && ref $ver eq 'ARRAY';

    cmp_ok( scalar @$ver, '==', 5, 'version array has 5 elements' );
    like( $ver->[0], qr/^\d+$/, 'major version is numeric' );

    # Value should be cached in the dbh attribute (via XS STORE/FETCH)
    my $cached = $dbh->{ora_server_version};
    ok( defined $cached, 'ora_server_version is readable from dbh attribute' )
        or diag "attribute read returned undef; XS FETCH handler may be missing";
    is( ref $cached, 'ARRAY', 'cached attribute is an array ref' );

    # Second call should return the cached value and be identical
    my $ver2 = DBD::Oracle::db::ora_server_version($dbh);
    is_deeply( $ver2, $ver, 'second call returns same cached value' );

    # Verify we can write and read back (round-trip through XS STORE/FETCH)
    my $fake = [99, 88, 77, 66, 55];
    $dbh->{ora_server_version} = $fake;
    my $readback = $dbh->{ora_server_version};
    is_deeply( $readback, $fake, 'ora_server_version round-trips through STORE/FETCH' );
}
