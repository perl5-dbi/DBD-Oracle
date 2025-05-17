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

    plan tests => 3;

    note 'Testing multiple cached connections...';

    ok -d $Bin, "t/ directory exists";
    ok -f "$Bin/cache2.pl", "t/cache2.pl exists";

    system("perl -MExtUtils::testlib $Bin/cache2.pl");
    ok($? == 0, "clean termination with multiple cached connections");
}

__END__

