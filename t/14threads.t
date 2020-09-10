#!perl

use strict;
use warnings;

$| = 1;

## ----------------------------------------------------------------------------
## 14threads.t
## By Jeffrey Klein,
## ----------------------------------------------------------------------------

# This needs to be the very very first thing
BEGIN { eval 'use threads; use threads::shared;' }

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

my $use_threads_err = $@;
use DBI;
use Config qw(%Config);
use Test::More;

BEGIN {
    if ( !$Config{useithreads} || $] < 5.008 ) {
        plan skip_all => "this $^O perl $] not configured to support iThreads";
    }
    elsif ( $DBI::VERSION <= 1.601 ) {
        plan skip_all => 'DBI version '
          . $DBI::VERSION
          . ' does not support iThreads. Use version 1.602 or later.';
    }
    die $use_threads_err if $use_threads_err;    # need threads
}

use DBI;

use Test::More;

my $dbh    = db_handle( { PrintError => 0 } );

if ($dbh) {
    $dbh->disconnect;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}

my $last_session : shared;
our @pool : shared;

# run five threads in sequence
# each should get the same session

# TESTS: 5

for my $i ( 0 .. 4 ) {
    threads->create(
        sub {
            my $dbh = get_dbh_from_pool();

            my $session = session_id($dbh);

            if ( $i > 0 ) {
                is $session, $last_session,
                  "session $i matches previous session";
            }
            else {
                ok $session, "session $i created",;
            }

            $last_session = $session;
            free_dbh_to_pool($dbh);
        }
    )->join;

}

# TESTS: 1
is scalar(@pool), 1, 'one imp_data in pool';

# get two sessions in same thread
# TESTS: 2
threads->create(
    sub {
        my $dbh1 = get_dbh_from_pool();
        my $s1   = session_id($dbh1);

        my $dbh2 = get_dbh_from_pool();
        my $s2   = session_id($dbh2);

        ok $s1 ne $s2, 'thread gets two separate sessions';

        free_dbh_to_pool($dbh1);

        my $dbh3 = get_dbh_from_pool();
        my $s3   = session_id($dbh3);

        is $s3, $s1, 'get same session after free';

        free_dbh_to_pool($dbh2);
        free_dbh_to_pool($dbh3);
    }
)->join;

# TESTS: 1
is scalar(@pool), 2, 'two imp_data in pool';

#trade dbh between threads
my @thr;
my @sem;
use Thread::Semaphore;

# create locked semaphores
for my $i ( 0 .. 2 ) {
    push @sem, Thread::Semaphore->new(0);
}

undef $last_session;

# 3 threads, 3 iterations
# TESTS: 9
for my $t ( 0 .. 2 ) {
    $thr[$t] = threads->create(
        sub {
            my $partner = ( $t + 1 ) % 3;

            for my $i ( 1 .. 3 ) {
                $sem[$t]->down;

                my $dbh     = get_dbh_from_pool();
                my $session = session_id($dbh);
                if ( defined $last_session ) {
                    is $session, $last_session,
                      "thread $t, loop $i matches previous session";
                }
                else {
                    ok $session, "thread $t, loop $i created session";
                }
                $last_session = $session;
                free_dbh_to_pool($dbh);

                # signal next thread
                $sem[$partner]->up;
            }
        }
    );
}

# start thread 0!
$sem[0]->up;

$_->join for @thr;

# TESTS: 1
empty_pool();

is scalar(@pool), 0, 'pool empty';

done_testing;

exit;

sub get_dbh_from_pool {
    my $imp = pop @pool;

    # if pool is empty, $imp is undef
    # in that case, get new dbh
    return connect_dbh($imp);
}

sub free_dbh_to_pool {
    my $imp = $_[0]->take_imp_data or return;
    push @pool, $imp;
}

sub empty_pool {
    get_dbh_from_pool() while @pool;
}

sub connect_dbh {
    my $imp_data = shift;
    return db_handle( { dbi_imp_data => $imp_data } );
}

sub session_id {
    my $dbh = shift;
    my ($s) = $dbh->selectrow_array("select userenv('sid') from dual");
    return $s;
}
__END__
