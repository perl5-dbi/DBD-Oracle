#!perl
# reports on all interesting versions

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use Test::More tests => 2;

use DBD::Oracle qw/ ORA_OCI /;

my $oci_version = ORA_OCI();

note 'OCI client library version: ', $oci_version;

ok $oci_version;

SKIP: {
    my $dbh = db_handle( { PrintError => 0 } )
      or note <<'END_NOTE' or skip q{Can't connect to Oracle Database} => 1;

Can't connect to an Oracle instance.

Without a database connection, most of DBD::Oracle's test suite will
be skipped. To let the tests use a database, set up the
environment variables ORACLE_USERID and ORACLE_DSN. E.g.:

    $ export ORACLE_USERID='scott/tiger'
    $ export ORACLE_DSN='dbi:Oracle:testdb'

END_NOTE

    my $sth =
      $dbh->prepare(q{select * from v$version where banner like 'Oracle%'});
    $sth->execute;

    my $version = join ' ', $sth->fetchrow;

    $sth->finish;

    note 'Database version: ', $version;

    ok $version, 'Version exists';
}
