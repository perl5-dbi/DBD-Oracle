#!perl

## ----------------------------------------------------------------------------
## 31lob_extended.t
## By John Scoles, The Pythian Group
## ----------------------------------------------------------------------------
##  This run through some bugs that have been found in earlier versions of DBD::Oracle
##  Checks to ensure that these bugs no longer come up
##  Basically this is testing the use of LOBs when returned via stored procedures with bind_param_inout
## ----------------------------------------------------------------------------

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn drop_table create_table db_handle /;

use Test::More;

use DBI;
use Config;
use DBD::Oracle qw(:ora_types);

$| = 1;

my $dbh    = db_handle( { PrintError => 0 } );

if ($dbh) {
    plan tests => 30;
    $dbh->{LongReadLen} = 7000;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}

my ( $table, $data0, $data1 ) = setup_test($dbh);

my $PLSQL = <<"PLSQL";
BEGIN
  OPEN ? FOR SELECT x FROM $table;
END;
PLSQL

$dbh->{RaiseError} = 1;

#
# bug in DBD::Oracle 1.21 where if ora_auto_lobs is not set and we attempt to
# fetch from a table containing lobs which has more than one row
# we get a segfault. This was due to prefetching more than one row.
#
{
    my $testname = 'ora_auto_lobs prefetch';

    my ( $sth1, $ev );

    eval { $sth1 = $dbh->prepare( $PLSQL, { ora_auto_lob => 0 } ); };
    ok( !$@, "$testname - prepare call proc" );
    my $sth2;
    ok( $sth1->bind_param_inout( 1, \$sth2, 500, { ora_type => ORA_RSET } ),
        "$testname - bind out cursor" );
    ok( $sth1->execute, "$testname - execute to get out cursor" );

    my ($lobl);

    ($lobl) = $sth2->fetchrow;
    test_lob( $dbh, $lobl, $testname, 6000, $data0 );
    ($lobl) = $sth2->fetchrow;
    test_lob( $dbh, $lobl, $testname, 6000, $data1 );

    ok( $sth2->finish, "$testname - finished returned sth" );
    ok( $sth1->finish, "$testname - finished sth" );
}

#
# prior to DBD::Oracle 1.22 if ora_auto_lob was set on a statement which
# was used to return a cursor on a result-set containing lobs, the lobs
# were not automatically fetched.
#
{
    my $testname = 'ora_auto_lobs not fetching';

    my ( $sth1, $ev, $lob );

    # ora_auto_lobs is supposed to default to set
    eval { $sth1 = $dbh->prepare($PLSQL); };
    ok( !$@, "$testname prepare call proc" );
    my $sth2;
    ok( $sth1->bind_param_inout( 1, \$sth2, 500, { ora_type => ORA_RSET } ),
        "$testname - bind out cursor" );
    ok( $sth1->execute, "$testname - execute to get out cursor" );

    ($lob) = $sth2->fetchrow;
    ok( $lob, "$testname - fetch returns something" );
    isnt( ref $lob, 'OCILobLocatorPtr', "$testname - not a lob locator" );
    is( $lob, $data0, "$testname, first lob matches" );

    ($lob) = $sth2->fetchrow;
    ok( $lob, "$testname - fetch returns something" );
    isnt( ref $lob, 'OCILobLocatorPtr', "$testname - not a lob locator" );
    is( $lob, $data1, "$testname, second lob matches" );

    ok( $sth2->finish, "$testname - finished returned sth" );
    ok( $sth1->finish, "$testname - finished sth" );
}

sub test_lob {
    my ( $h, $lobl, $testname, $size, $data ) = @_;

    ok( $lobl, "$testname - lob locator retrieved" );
    is( ref($lobl), 'OCILobLocatorPtr', "$testname - is a lob locator" );

  SKIP: {
        skip 'did not receive a lob locator', 4
          unless ref($lobl) eq 'OCILobLocatorPtr';

        my ( $lob_length, $lob, $ev );

        eval { $lob_length = $h->ora_lob_length($lobl); };
        $ev = $@;
        diag($ev) if $ev;
        ok( !$ev, "$testname - first lob length $lob_length" );
        is( $lob_length, $size, "$testname - correct lob length" );
        eval { $lob = $h->ora_lob_read( $lobl, 1, $lob_length ); };
        $ev = $@;
        diag($ev) if ($ev);
        ok( !$ev, "$testname - read lob" );

        is( $lob, $data, "$testname - lob returned matches lob inserted" );
    }
}

sub setup_test {
    my ($h) = @_;
    my ( $table, $sth, $ev );

    eval { $table = create_table( $h, { cols => [ [ 'x', 'clob' ] ] }, 1 ) };
    BAIL_OUT("test table not created- $@") if $@;
    ok( !$ev, 'created test table' );

    eval { $sth = $h->prepare(qq/insert into $table (idx, x) values(?,?)/); };
    BAIL_OUT("Failed to prepare insert into $table - $@") if $@;
    my $data0 = 'x' x 6000;
    my $data1 = 'y' x 6000;
    eval {
        $sth->execute( 1, $data0 );
        $sth->execute( 2, $data1 );
    };
    BAIL_OUT("Failed to insert test data into $table - $@") if $@;
    ok( !$ev, 'created test data' );

    return ( $table, $data0, $data1 );
}

END {
    return unless $dbh;

    local $dbh->{PrintError} = 0;
    local $dbh->{RaiseError} = 1;

    eval { drop_table($dbh); };
    if ($@) {
        diag("table $table possibly not dropped - check - $@\n")
          if $dbh->err ne '942';
    }
}

