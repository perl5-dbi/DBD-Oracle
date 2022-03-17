#!perl

use strict;
use warnings;
# This needs to be the very very first thing
BEGIN { eval 'use threads; use threads::shared;' }
use Config qw(%Config);
use Test::More;
use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;
use DBD::Oracle qw(ora_shared_release);

$| = 1;
if ( !$Config{useithreads} || "$]" < 5.008 ) {
    plan skip_all => "this $^O perl $] not configured to support iThreads";
    exit(0);
}
if ( $DBI::VERSION <= 1.601 ) {
    plan skip_all => 'DBI version '
      . $DBI::VERSION
      . ' does not support iThreads. Use version 1.602 or later.';
    exit(0);
}
my $dbh    = db_handle( { PrintError => 0 } );

if ($dbh) {
    $dbh->disconnect;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}
my $last_session : shared;
my $holder : shared;

for my $i ( 0 .. 4 ) {
    threads->create(
        sub {
            my $dbh    = db_handle( { ora_dbh_share => \$holder, PrintError => 0 } );
            if($dbh)
            {
                my $session = session_id($dbh);

                if ( $i > 0 ) {
                    is $session, $last_session,
                      "session $i matches previous session";
                }
                else {
                    ok $session, "session $i created",;
                }

                $last_session = $session;
                $dbh->disconnect();
            }
            else
            {
                ok 0, "no connection " . $DBI::errstr;
            }
        }
    )->join;
}
ora_shared_release($holder);

# now the same, but let shared variable be destroyed
threads->create(
    sub {
        my $other : shared;
            for my $i ( 0 .. 4 ) {
                threads->create(
                    sub {
                        my $dbh    = db_handle( { ora_dbh_share => \$other, PrintError => 0 } );
                        if($dbh)
                        {
                            my $session = session_id($dbh);

                            if ( $i > 0 ) {
                                is $session, $last_session,
                                  "session $i matches previous session";
                            }
                            else {
                                ok $session, "session $i created",;
                            }

                            $last_session = $session;
                            $dbh->disconnect();
                        }
                        else
                        {
                            ok 0, "no connection " . $DBI::errstr;
                        }
                    }
                )->join;
            }
        ora_shared_release($other);
    }
)->join;
done_testing;

sub session_id {
    my $dbh = shift;
    my ($s) = $dbh->selectrow_array("select userenv('sid') from dual");
    return $s;
}
