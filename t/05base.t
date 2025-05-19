#!perl

use strict;
use warnings;

# Base DBD Driver Test
use Test::More tests => 6;

require_ok('DBI');

eval { DBI->import };

is( $@ => '', 'Successfully import DBI' );

is( ref DBI->internal => 'DBI::dr', 'internal' );

my $drh = eval {

    # This is a special case. install_driver should not normally be used.
    DBI->install_driver('Oracle');
};

is( $@ => '', q|install_driver('Oracle') doesnt fail| )
  or diag "Failed to load Oracle extension and/or shared libraries";

SKIP: {
    skip 'install_driver failed - skipping remaining', 2 if $@;

    is(
        ref $drh => 'DBI::dr',
        'install_driver(Oracle) returns the correct object'
    ) or diag '$drh wrong object type, found: ' . ref $drh;

    ok( do { $drh && $drh->{Version} }, 'version found in $drh object' );
}
