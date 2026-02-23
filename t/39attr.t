#!perl
#
# Test you can set and retrieve some attributes after connect
# MJE wrote this after discovering the code to set these attributes
# was duplicated in connect/login6 and STORE and it did not need to be
# because DBI passes attributes to STORE for you.
#

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use DBI;
use DBD::Oracle(qw(ORA_OCI));

#use Devel::Peek qw(SvREFCNT Dump);

use Test::More;

$| = 1;

#use Devel::Leak;
#use Test::LeakTrace;

#no_leaks_ok {
do_it();

#} -verbose;

sub do_it {

    #my $handle;
    #my $count = Devel::Leak::NoteSV($handle);

    my $dbh = db_handle()
      or plan skip_all => 'Unable to connect to Oracle';

    note( "\n" . 'Oracle SERVER version: '
          . join( '.', @{ $dbh->func('ora_server_version') } ) );
    note( 'Oracle CLIENT version: ' . ORA_OCI() );

  SKIP: {
        my @attrs = (
            qw(ora_module_name
              ora_client_info
              ora_client_identifier
              ora_action)
        );
        my @attrs112 = (qw(ora_driver_name));

        skip( 'Oracle OCI too old', 1 + @attrs + @attrs112 ) if ORA_OCI() < 11;

        foreach my $attr (@attrs) {
            $dbh->{$attr} = 'fred';
            is( $dbh->{$attr}, 'fred', "attribute $attr set and retrieved" );
        }

      SKIP: {
            skip 'Oracle OCI too old', 1 + @attrs112 if ORA_OCI() < 11.2;

            like( $dbh->{ora_driver_name}, qr/DBD/, 'Default driver name' );

            foreach my $attr (@attrs) {
                $dbh->{$attr} = 'fred';
                is( $dbh->{$attr}, 'fred',
                    "attribute $attr set and retrieved" );
            }
        }
    }

    for my $attr (
        qw(ora_oci_success_warn
        ora_objects)
      )
    {
        $dbh->{$attr} = 1;
        is( $dbh->{$attr}, 1, "attribute $attr set and retrieved" );
    }

    $dbh->disconnect;

    #Devel::Leak::CheckSV($handle);
}

done_testing();
