#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle force_drop_table drop_table table /;

use DBI;
use DBD::Oracle qw(ORA_RSET SQLCS_NCHAR);

use Test::More;
$| = 1;

## ----------------------------------------------------------------------------
## 56embbeded.t
## By John Scoles, The Pythian Group
## ----------------------------------------------------------------------------
##  Just a few checks to see if I can select embedded objects with Oracle::DBD
##  Nothing fancy.
## ----------------------------------------------------------------------------

# create a database handle
my $dbh = eval{ db_handle( { RaiseError => 1, AutoCommit => 1, PrintError => 0 } )};

if ($dbh) {
    plan tests => 4;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}

# check that our db handle is good
isa_ok( $dbh, 'DBI::db' );

# get the user's privileges
my $privs_sth = $dbh->prepare('SELECT PRIVILEGE from session_privs');
$privs_sth->execute;
my @privileges = map { $_->[0] } @{ $privs_sth->fetchall_arrayref };

SKIP: {

   skip q{don't have permission to create type} => 3
      unless grep { $_ eq 'CREATE TYPE' } @privileges;
   skip q{don't have permission to create table} => 3
      unless grep { $_ eq 'CREATE TABLE' } @privileges;

   my $table = table('table_embed');
   my $type = $table . 'a_type';

   #do not warn if already there
   eval {
      local $dbh->{PrintError} = 0;
      force_drop_table( $dbh, $table );
      $dbh->do(qq{DROP TYPE $type });
   };
   $dbh->do(qq{CREATE OR REPLACE TYPE $type AS varray(10) OF varchar(30) });

   $dbh->do(qq{ CREATE TABLE $table ( aa_type $type) });

   $dbh->do("insert into $table values ($type('1','2','3','4','5'))");

   # simple execute
   my $sth;
   ok( $sth = $dbh->prepare("select * from $table"),
      '... Prepare should return true' );
   my $problems;
   ok( $sth->execute(), '... Select should return true' );

   while ( my ($a) = $sth->fetchrow() ) {
      $problems = scalar(@$a);
   }

   cmp_ok( scalar($problems), '==', 5, '... we should have 5 items' );

   drop_table($dbh, $table);

   $dbh->do("drop type $type") unless $ENV{DBD_SKIP_TABLE_DROP};

}
