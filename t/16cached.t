#!perl
#written by Andrey A Voropaev (avorop@mail.ru)

use strict;
use warnings;

use Test::More;
use DBI;
use FindBin qw($Bin);
use lib 't/lib';
use DBDOracleTestLib qw/ db_handle /;

my $dbh;
$| = 1;
SKIP: {
    $dbh = db_handle();

    #  $dbh->{PrintError} = 1;
    plan skip_all => 'Unable to connect to Oracle' unless $dbh;

    note("Testing multiple cached connections...\n");

    plan tests => 1;

    system("perl -MExtUtils::testlib $Bin/cache2.pl");
    ok($? == 0, "clean termination with multiple cached connections");
}

__END__

