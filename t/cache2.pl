#!perl
#written by Andrey A Voropaev (avorop@mail.ru)

use strict;

use DBI;

tst1();
tst2();
tst1();
tst2();

sub tst1
{
    my $dbh = db_handle({
            RaiseError => 0,
            PrintError => 0,
            AutoCommit => 1,
            ora_charset => 'WE8MSWIN1252',
        });
    my $sth = $dbh->prepare(
        q{ select 1 from dual }
    );
    $sth->execute();
    my $r = $sth->fetchall_arrayref();
}

sub tst2
{
    my $dbh = db_handle({
            RaiseError => 0,
            PrintError => 0,
            AutoCommit => 1,
            ora_charset => 'AL32UTF8',
        });
    my $sth = $dbh->prepare(
        q{ select 2 from dual }
    );
    $sth->execute();
    my $r = $sth->fetchall_arrayref();
}


sub oracle_test_dsn {
    my ( $default, $dsn ) = ( 'dbi:Oracle:', $ENV{ORACLE_DSN} );

    $dsn ||= $ENV{DBI_DSN}
      if $ENV{DBI_DSN} && ( $ENV{DBI_DSN} =~ m/^$default/io );
    $dsn ||= $default;

    return $dsn;
}

sub db_handle {

    my $p = shift;
    my $dsn    = oracle_test_dsn();
    my $dbuser = $ENV{ORACLE_USERID} || 'scott/tiger';
    my $dbh    = DBI->connect_cached( $dsn, $dbuser, '', $p );
    return $dbh

}

