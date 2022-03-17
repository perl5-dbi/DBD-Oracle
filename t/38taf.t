#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use DBI;
use DBD::Oracle(qw(:ora_fail_over));

#use Devel::Peek qw(SvREFCNT Dump);

use Test::More;
$| = 1;

# create a database handle
my $dbh = db_handle()
  or plan skip_all => 'Unable to connect to Oracle';

$dbh->disconnect;

if ( !$dbh->ora_can_taf ) {

    $dbh = db_handle( { PrintError => 0, RaiseError => 0, ora_taf_function => 'taf' } );
    my $ev = $dbh->errstr;
    like( $ev, qr/You are attempting to enable TAF/, "'$ev' (expected)" );
}
else {
    ok $dbh = db_handle( { ora_taf_function => 'taf' } );

    is( $dbh->{ora_taf_function}, 'taf', 'TAF callback' );

    my $x = sub { };

    #   diag(SvREFCNT($x));
    #   diag(Dump($x));
    $dbh->{ora_taf_function} = $x;
    is( ref( $dbh->{ora_taf_function} ), 'CODE', 'TAF code ref' );

    #   diag(SvREFCNT($x));
}

done_testing();
