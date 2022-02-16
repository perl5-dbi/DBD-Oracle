#!perl
#written by Andrey A Voropaev (avorop@mail.ru)

use strict;
use warnings;

use Test::More;
use DBI;
use DBD::Oracle qw(ORA_OCI);
use Encode;
use lib 't/lib';
use DBDOracleTestLib qw/ db_handle drop_table table force_drop_table /;

my $dbh1;
my $dbh2;
$| = 1;
SKIP: {
    plan skip_all =>
      'Unable to run multiple cset test, perl version is less than 5.6'
      unless ( $] >= 5.006 );

    $dbh1 = db_handle({
        RaiseError => 0,
        PrintError => 0,
        AutoCommit => 1,
        ora_charset => 'WE8MSWIN1252',
    });

    #  $dbh->{PrintError} = 1;
    plan skip_all => 'Unable to connect to Oracle' unless $dbh1;

    plan skip_all => 'Oracle charset tests unreliable for Oracle 8 client'
      if ORA_OCI() < 9.0 and !$ENV{DBD_ALL_TESTS};

    note("Testing multiple connections with different charsets...\n");

    $dbh2 = db_handle({
        RaiseError => 0,
        PrintError => 0,
        AutoCommit => 1,
        ora_charset => 'AL32UTF8',
    });

    my $testcount = 3;

    plan tests => $testcount;

    my $tname = table();
    force_drop_table($dbh1);
    $dbh1->do(
        qq{create table $tname (idx number, txt varchar2(50))}
    );
    die "Failed to create test table\n" if($dbh1->err);

    my $sth = $dbh1->prepare(
        qq{insert into $tname (idx, txt) values(?, ?)}
    );
    my $utf8_txt = 'äöüÜÖÄ';
    my $x = $utf8_txt;
    Encode::from_to($x, 'UTF-8', 'Latin1');
    $sth->execute(1, $x);

    $sth = $dbh1->prepare(
        qq{select txt from $tname where idx=1}
    );
    $sth->execute();
    my $r = $sth->fetchall_arrayref();
    ok(must_be_latin1($r, $utf8_txt), "Latin1 support");

    $sth = $dbh2->prepare(
        qq{insert into $tname (idx, txt) values(?, ?)}
    );
    # insert bytes
    $x = $utf8_txt;
    $sth->execute(2, $x);
    # insert characters
    $x = $utf8_txt;
    $sth->execute(3, Encode::decode('UTF-8', $x));

    $sth = $dbh2->prepare(
        qq{select txt from $tname where idx=?}
    );
    $sth->execute(2);
    $r = $sth->fetchall_arrayref();
    ok(must_be_utf8($r, $utf8_txt), "UTF-8 as bytes");
    $sth->execute(3);
    $r = $sth->fetchall_arrayref();
    ok(must_be_utf8($r, $utf8_txt), "UTF-8 as characters");
}

sub must_be_latin1
{
    my $r = shift;
    return unless @$r == 1;
    my $x = $r->[0][0];
    # it shouldn't be encoded
    return if Encode::is_utf8($x);
    Encode::from_to($x, 'Latin1', 'UTF-8');
    return $x eq $_[0];
}

sub must_be_utf8
{
    my $r = shift;
    return unless @$r == 1;
    my $x = $r->[0][0];
    # it should be encoded
    return unless Encode::is_utf8($x);
    return Encode::encode('UTF-8', $x) eq $_[0];
}


END {
    eval {
        drop_table($dbh1)
    };
}

__END__

