#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn /;

use Test::More;
use DBI;

$| = 1;

my $dbuser   = $ENV{ORACLE_USERID}   || 'scott/tiger';
my $dbuser_2 = $ENV{ORACLE_USERID_2} || '';

if ( $dbuser_2 eq '' ) {
    plan skip_all => "ORACLE_USERID_2 not defined.\n";
}

# strip off @ on userid_2, as the reauth presumes current server
$dbuser_2 =~ s/@.*//;
( my $uid1 = uc $dbuser ) =~ s:/.*::;
( my $uid2 = uc $dbuser_2 ) =~ s:/.*::;
if ( $uid1 eq $uid2 ) {
    plan skip_all => "ORACLE_USERID_2 not unique.\n";
}

my $dsn = oracle_test_dsn();
my $dbh = DBI->connect( $dsn, $dbuser, $ENV{ORACLE_PASSWD} || '' );

if ($dbh) {
    plan tests => 3;
}
else {
    plan skip_all => "Unable to connect to Oracle\n";
}

is( ( $dbh->selectrow_array('SELECT USER FROM DUAL') )[0], $uid1, 'uid1' );
ok( $dbh->func( $dbuser_2, $ENV{ORACLE_PASSWD} || '', 'reauthenticate' ), 'reauthenticate' );
is( ( $dbh->selectrow_array('SELECT USER FROM DUAL') )[0], $uid2, 'uid2' );
