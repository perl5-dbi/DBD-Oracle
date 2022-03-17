#!perl

use strict;
use warnings;
# This needs to be the very very first thing
BEGIN { eval 'use threads; use threads::shared;' }

$| = 1;

## ----------------------------------------------------------------------------
## 16drcp.t
## By Andrey A. Voropaev
## ----------------------------------------------------------------------------
use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;
use DBD::Oracle qw(OCI_SPOOL_ATTRVAL_NOWAIT);
use DBI;
use Test::More;

{
    my $dbh    = db_handle( { PrintError => 0 } );

    if ($dbh) {
        $dbh->disconnect;
    }
    else {
        plan skip_all => 'Unable to connect to Oracle';
    }
}

{
    my $dbh   = db_handle( { ora_drcp=>1, ora_drcp_max => 2, PrintError => 0 } );
    ok defined $dbh, 'first connection from pool';
    my $dbh1   = db_handle( { ora_drcp=>1, PrintError => 0 } );
    ok defined $dbh1, 'second connection from pool';
    is $dbh->{ora_drcp_used}, 2, 'count of used connections is 2';
    $dbh->{ora_drcp_mode} = OCI_SPOOL_ATTRVAL_NOWAIT;
    my $dbh2   = db_handle( { ora_drcp=>1, PrintError => 0 } );
    ok !defined $dbh2, 'third connection from pool not allowed';

    $dbh->do(q(alter session set NLS_DATE_FORMAT='yyyy.mm.dd'));
    $dbh->{ora_drcp_tag} = 's1';
    $dbh1->do(q(alter session set NLS_DATE_FORMAT='dd.mm.yyyy'));
    $dbh1->{ora_drcp_tag} = 's2';

    $dbh->disconnect();
    $dbh1->disconnect();
}
{
    my $dbh   = db_handle( { ora_drcp=>1, ora_drcp_tag=> 's1', PrintError => 0 } );
    if ($dbh) {
        my $found_tag = $dbh->{ora_drcp_tag};
        ok((defined $found_tag && $found_tag eq 's1'), 's1 session from pool');
        my $sth = $dbh->prepare('select sysdate from dual');
        $sth->execute();
        my $x = $sth->fetchall_arrayref();
        ok($x->[0][0] =~ /^\d{4}\.\d\d\.\d\d$/, "date in format yyyy.mm.dd");
        $dbh->disconnect();
    }
    else {
        ok 0, 'finding session s1';
    }
}
{
    my $dbh   = db_handle( { ora_drcp=>1, ora_drcp_tag=> 's2', PrintError => 0 } );
    if ($dbh) {
        my $found_tag = $dbh->{ora_drcp_tag};
        ok((defined $found_tag && $found_tag eq 's2'), 's2 session from pool');
        my $sth = $dbh->prepare('select sysdate from dual');
        $sth->execute();
        my $x = $sth->fetchall_arrayref();
        ok($x->[0][0] =~ /^\d\d\.\d\d\.\d{4}$/, "date in format dd.mm.yyyy");
        $dbh->disconnect();
    }
    else {
        ok 0, 'finding session s2';
    }
}

eval{
    my @sts : shared;;
    my $th1 = threads->create(
        sub{ chk('s1', qr(\d{4}\.\d\d\.\d\d), $sts[0]) }
    );
    my $th2 = threads->create(
        sub {chk('s2', qr(\d\d\.\d\d\.\d{4}), $sts[1]) }
    );
    $th1->join();
    $th2->join();
    ok($sts[0], 'first thread');
    ok($sts[1], 'second thread');
};

done_testing;

sub chk
{
    my $tag = shift;
    my $p = shift;
    my $dbh   = db_handle( { ora_drcp=>1, ora_drcp_tag=> $tag, PrintError => 0 } );
    if ($dbh) {
        my $found_tag = $dbh->{ora_drcp_tag};
        my $sth = $dbh->prepare('select sysdate from dual');
        $sth->execute();
        my $x = $sth->fetchall_arrayref();
        $_[0] = $found_tag eq $tag && $x->[0][0] =~ /^$p$/;
        $dbh->disconnect();
    }
    else {
        $_[0] = 0;
    }
}
