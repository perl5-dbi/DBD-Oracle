#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn client_ochar_is_utf8 table drop_table
db_handle /;

use Test::More;
use DBI;
use DBD::Oracle qw(:ora_types ORA_OCI);
use Data::Dumper;
use Math::BigInt;

$| = 1;

my @test_sets =
  ( [ 'CHAR(10)', 10 ], [ 'VARCHAR(10)', 10 ], [ 'VARCHAR2(10)', 10 ], );

# Set size of test data (in 10KB units)
#        Minimum value 3 (else tests fail because of assumptions)
#        Normal  value 8 (to test 64KB threshold well)
my $sz = 8;

my $tests         = 3;
my $tests_per_set = 11;
$tests += @test_sets * $tests_per_set;

my $t      = 0;
my $failed = 0;
my %ocibug;

my $table = table();
my $dbh   = db_handle( { PrintError => 0 } );

if ($dbh) {
    plan tests => $tests;
}
else {
    plan skip_all => "Unable to connect to oracle\n";
}

# test simple select statements with [utf8]

my $utf8_test = ( $] >= 5.006 )
  && client_ochar_is_utf8(
  )    # for correct output (utf8 bind vars should be fine regardless)
  && ( $dbh->ora_can_unicode() & 2 );
diag('Including unicode data in test') if $utf8_test;

unless ( create_test_table( 'str CHAR(10)', 1 ) ) {
    BAIL_OUT("Unable to create test table ($DBI::errstr)\n");
    print "1..0\n";
    exit 0;
}

my ( $sth, $p1, $p2, $tmp, @tmp );

for (@test_sets) {
    run_select_tests(@$_);
}

my $ora_server_version = $dbh->func('ora_server_version');
SKIP: {
    skip 'Oracle < 10', 1 if ( $ora_server_version->[0] < 10 );
    my $data = $dbh->selectrow_array(
        q!
       select to_dsinterval(?) from dual
       !, {}, '1 07:00:00'
    );
    ok( ( defined $data and $data eq '+000000001 07:00:00.000000000' ),
        'ds_interval' );
}

# FIXME - maybe remove this
if (0) {

    # UNION ALL causes Oracle 9 (not 8) to describe col1 as zero length
    # causing "ORA-24345: A Truncation or null fetch error occurred" error
    # Looks like an Oracle bug
    $dbh->trace(9);
    ok 0, $sth = $dbh->prepare(
        qq{
        SELECT :HeadCrncy FROM DUAL
        UNION ALL
        SELECT :HeadCrncy FROM DUAL}
    );
    $dbh->trace(0);
    ok 0, $sth->execute('EUR');
    ok 0, $tmp = $sth->fetchall_arrayref;
    use Data::Dumper;
    die Dumper $tmp;
}

# $dbh->{USER} is just there so it works for old DBI's before Username was added
my @pk =
  $dbh->primary_key( undef, $dbh->{USER} || $dbh->{Username}, uc $table );
ok( @pk, 'primary key on table' );
is( join( ',', @pk ), 'DT,IDX', 'DT,IDX' );

exit 0;

END {
    eval { drop_table($dbh) }
}

sub run_select_tests {
    my ( $type_name, $field_len ) = @_;

    my $data0;
    if ($utf8_test) {
        $data0 = eval q{ "0\x{263A}xyX" }
          ;   #this includes the smiley from perlunicode (lab) BTW: it is busted
    }
    else {
        $data0 = "0\177x\0X";
    }
    my $data1 = '1234567890';
    my $data2 = '2bcdefabcd';

  SKIP: {
        if ( !create_test_table( "lng $type_name", 1 ) ) {

            # typically OCI 8 client talking to Oracle 7 database
            diag("Unable to create test table for '$type_name' data ($DBI::err)"
            );
            skip $tests_per_set;
        }

        $sth = $dbh->prepare("insert into $table values (?, ?, SYSDATE)");
        ok( $sth, "prepare for insert of $type_name" );
        ok( $sth->execute( 40, $data0 ), 'insert 8bit or utf8' );
        ok( $sth->execute( Math::BigInt->new(41), $data1 ),
            'bind overloaded value' );
        ok( $sth->execute( 42, $data2 ), 'insert data2' )
          or diag '$sth->errstr: ' . $sth->errstr;

        ok( !$sth->execute( 43, '12345678901234567890' ),
            'insert string too long' );

        ok( $sth = $dbh->prepare("select * from $table order by idx"),
            'prepare select ordered by idx' );
        ok( $sth->execute, 'execute' );

        # allow for padded blanks
        $sth->{ChopBlanks} = 1;
        ok( $tmp = $sth->fetchall_arrayref, 'fetchall' );
        my $dif;
        if ($utf8_test) {
            $dif = DBI::data_diff( $tmp->[0][1], $data0 );
            ok( !defined($dif) || $dif eq '', 'first row matches' );
            diag($dif) if $dif;
        }
        else {
            is( $tmp->[0][1], $data0, 'first row matches' );
        }
        is( $tmp->[1][1], $data1, 'second row matches' );
        is( $tmp->[2][1], $data2, 'third row matches' );

    }
}    # end of run_select_tests

# end.

sub create_test_table {
    my ( $fields, $drop ) = @_;
    my $sql = qq{create table $table (
        idx integer,
        $fields,
        dt date,
        primary key (dt, idx)
    )};
    $dbh->do(qq{ DROP TABLE $table PURGE }) if $drop;
    $dbh->do($sql);
    if ( $dbh->err && $dbh->err == 955 ) {
        $dbh->do(qq{ DROP TABLE $table PURGE });
        warn "Unexpectedly had to drop old test table '$table'\n"
          unless $dbh->err;
        $dbh->do($sql);
    }
    return 0 if $dbh->err;
    return 1;
}

__END__
