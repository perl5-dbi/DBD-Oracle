#!perl

# https://github.com/perl5-dbi/DBD-Oracle/issues/171
# https://github.com/perl5-dbi/DBD-Oracle/pull/202

use strict;
use warnings;
use utf8;

use Test::More;

use lib 't/lib';
use DBDOracleTestLib qw/oracle_test_dsn db_handle/;
require bytes;

sub main() {
    $ENV{NLS_LANG} = 'American_America.AL32UTF8';
    my $dbh = db_handle( { PrintError => 0 } );
    if ($dbh) {
        plan(tests => 2);
    } else {
        plan(skip_all => 'Unable to connect to Oracle');
    }

    my $debug= 0;
    if ($ENV{DEBUG}) {
        $debug= $ENV{DEBUG} - 0;
    }

    my $sql= "SELECT 'alpha' nam FROM dual".
      " UNION SELECT 'beta'  nam FROM dual";
    my $sth = $dbh->prepare($sql);
    $sth->execute();
    while (my $row = $sth->fetchrow_arrayref()) {
        if ($debug>=1) {
            printf "fetched \"%s\" len=%d bytes::len=%d\n", $row->[0],
                length($row->[0]), bytes::length($row->[0]);
        }
        ok (length($row->[0])==bytes::length($row->[0]));
        my $trlen= 5;
        my $stmp= substr($row->[0], 0, $trlen);
        if ($debug>=1) {
            printf "truncated(0,%d)=\"%s\" len=%d bytes::len=%d\n", $trlen, $stmp,
                length($stmp), bytes::length($stmp);
        }
    }
    $sth->finish();

    $dbh->disconnect();
}

main();
