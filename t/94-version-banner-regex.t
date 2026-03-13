#!perl
# Test that the version regex used in ora_server_version matches
# known Oracle banner formats, including modern Oracle 23ai+ banners.

use strict;
use warnings;

use Test::More;

# Import the regex from the source — single point of truth
use DBD::Oracle;
my $VERSION_RE = $DBD::Oracle::db::VERSION_RE
    or BAIL_OUT('$DBD::Oracle::db::VERSION_RE not defined');

my @cases = (
    {
        banner  => 'Oracle Database 11g Express Edition Release 11.2.0.2.0 - 64bit Production',
        expect  => [11, 2, 0, 2, 0],
        name    => 'Oracle 11g XE',
    },
    {
        banner  => 'Oracle Database 12c Standard Edition Release 12.1.0.2.0 - 64bit Production',
        expect  => [12, 1, 0, 2, 0],
        name    => 'Oracle 12c',
    },
    {
        banner  => 'Oracle Database 19c Enterprise Edition Release 19.0.0.0.0 - Production',
        expect  => [19, 0, 0, 0, 0],
        name    => 'Oracle 19c',
    },
    {
        banner  => 'Oracle Database 21c Express Edition Release 21.0.0.0.0 - Production',
        expect  => [21, 0, 0, 0, 0],
        name    => 'Oracle 21c XE',
    },
    {
        banner  => 'Oracle Database 23ai Free Release 23.0.0.0.0 - Develop, Learn, and Run for Free',
        expect  => [23, 0, 0, 0, 0],
        name    => 'Oracle 23ai Free',
    },
    {
        banner  => 'Oracle AI Database 26ai Free Release 23.26.1.0.0 - Develop, Learn, and Run for Free',
        expect  => [23, 26, 1, 0, 0],
        name    => 'Oracle 26ai Free (actual banner from CI)',
    },
    {
        banner  => 'Personal Oracle Database 10g Release 10.2.0.1.0 - Production',
        expect  => [10, 2, 0, 1, 0],
        name    => 'Personal Oracle 10g',
    },
);

plan tests => scalar @cases * 2;

for my $case (@cases) {
    my @got = $case->{banner} =~ $VERSION_RE;
    ok( scalar @got == 5, "$case->{name}: regex matches banner" )
        or diag "banner: '$case->{banner}'";
    is_deeply( \@got, $case->{expect}, "$case->{name}: correct version extracted" )
        or diag "got: [@got]";
}
