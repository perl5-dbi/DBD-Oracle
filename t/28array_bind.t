#!perl

## ----------------------------------------------------------------------------
## 26array_bind.t
## By Alexander V Alekseev
## and John Scoles, The Pythian Group
##
## ----------------------------------------------------------------------------
##  Checking bind_param_inout to an varchar2_table and number_table
##  Checking bind_param_inout_array with execute_array
##
## ----------------------------------------------------------------------------

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/
    set_nls_lang_charset set_nls_nchar oracle_test_dsn db_handle
/;

use Encode;
use Devel::Peek;

use DBI;
use DBD::Oracle qw(:ora_types ORA_OCI);

use Test::More;

use Data::Dumper;
$Data::Dumper::Useqq = 1;

my $dbh;

my $utf8_charset = ( ORA_OCI >= 9.2 ) ? 'AL32UTF8' : 'UTF8';
my $eight_bit_charset = 'WE8ISO8859P1';

sub db_connect {
    my $utf8 = shift;

    # Make sure we really are overriding the environment settings.
    my ( $charset, $ncharset );
    if ($utf8) {
        set_nls_lang_charset($eight_bit_charset);
        set_nls_nchar($eight_bit_charset);
        $charset  = $utf8_charset;
        $ncharset = $utf8_charset;
    }
    else {
        set_nls_lang_charset($utf8_charset);
        set_nls_nchar($utf8_charset);
        $charset  = $eight_bit_charset;
        $ncharset = $eight_bit_charset;
    }

    my $p = {
        AutoCommit       => 1,
        PrintError       => 0,
        FetchHashKeyName => 'NAME_lc',
        ora_envhp => 0,    # force fresh environment (with current NLS env vars)
    };
    $p->{ora_charset}  = $charset  if $charset;
    $p->{ora_ncharset} = $ncharset if $ncharset;

    my $dbh = db_handle( $p );
    return $dbh;
}

sub test_varchar2_table_3_tests($) {
    my $dbh       = shift;
    my $statement = q|
        DECLARE
                tbl SYS.DBMS_SQL.VARCHAR2_TABLE;
        BEGIN
                tbl := :mytable;
                :cc := tbl.count();
                tbl(1) := 'def';
                tbl(2) := 'ijk';
                :mytable := tbl;
        END;
        |;

    my $sth = $dbh->prepare($statement);

    if ( !defined($sth) ) {
        BAIL_OUT( 'Prepare(varchar2) error: ' . $dbh->errstr );
    }

    my @arr = ( 'abc', 'cde', 'lalala' );

    if (
        not $sth->bind_param_inout(
            ':mytable',
            \\@arr,
            5,
            {
                ora_type                => ORA_VARCHAR2_TABLE,
                ora_maxarray_numentries => 2
            }
        )
      )
    {
        BAIL_OUT( 'bind  :mytable (VARCHAR2) error: ' . $dbh->errstr );
    }
    my $cc;
    if ( not $sth->bind_param_inout( ':cc', \$cc, 100 ) ) {
        BAIL_OUT( 'bind :cc (at VARCHAR2) error: ' . $dbh->errstr );
    }

    if ( not $sth->execute() ) {
        BAIL_OUT( 'Execute (at VARCHAR2) failed: ' . $dbh->errstr );
    }

    ok( $cc == 2,          'VARCHAR2_TABLE input count correctness' );
    ok( scalar(@arr) == 2, 'VARCHAR2_TABLE output count correctness' );
    ok( ( ( $arr[0] eq 'def' ) and ( $arr[1] eq 'ijk' ) ),
        'VARCHAR2_TABLE output content' )
      or diag( "arr[0]='", $arr[0], "', arr[1]='", $arr[1], "', arr=",
        Data::Dumper::Dumper( \@arr ) );
}

sub test_number_table_3_tests {
    my $dbh       = shift;
    my $statement = q|
        DECLARE
                tbl SYS.DBMS_SQL.NUMBER_TABLE;
        BEGIN
                tbl := :mytable;
                :cc := tbl.count();
                tbl(4) := -1;
                tbl(5) := -2;
                :mytable := tbl;
        END;
        |;

    my $sth = $dbh->prepare($statement);

    if ( !defined($sth) ) {
        BAIL_OUT( 'Prepare(NUMBER_TABLE) error: ' . $dbh->errstr );
    }

    my @arr = ( 1, '2E0', '3.5' );

    # note, that ora_internal_type defaults to SQLT_FLT for ORA_NUMBER_TABLE .

    if (
        not $sth->bind_param_inout(
            ':mytable',
            \\@arr,
            10,
            {
                ora_type                => ORA_NUMBER_TABLE,
                ora_maxarray_numentries => ( scalar(@arr) + 2 ),
                ora_internal_type       => SQLT_INT
            }
        )
      )
    {
        BAIL_OUT( 'bind(NUMBER_TABLE) :mytable error: ' . $dbh->errstr );
    }
    my $cc = undef;
    if ( not $sth->bind_param_inout( ':cc', \$cc, 100 ) ) {
        BAIL_OUT( 'bind(NUMBER_TABLE) :cc error: ' . $dbh->errstr );
    }

    if ( not $sth->execute() ) {
        BAIL_OUT( 'Execute(NUMBER_TABLE) failed: ' . $dbh->errstr );
    }

    # print        "Result: cc=",$cc,"\n",
    # "\tarr=",Data::Dumper::Dumper(\@arr),"\n";

    #Result: cc=3
    #        arr=$VAR1 = [
    #          '5',
    #          '8',
    #          '3.5',
    #          '-1',
    #          '-2'
    #        ];

    ok( $cc == 3,          'NUMBER_TABLE input count correctness' );
    ok( scalar(@arr) == 5, 'NUMBER_TABLE output count correctness' );
    my $result = 1;
    my @r = ( 1, 2, 3, -1, -2 );
    for ( my $i = 0 ; $i < scalar(@arr) ; $i++ ) {
        if ( $r[$i] != $arr[$i] ) {
            $result = 0;
            last;
        }
    }
    ok( $result, 'NUMBER_TABLE output content' )
      or diag(
        'arr=',
        Data::Dumper::Dumper( \@arr ),
        "\nThough must be: ",
        Data::Dumper::Dumper( \@r )
      );
}

sub test_inout_array_tests {
   my $dbh     = shift;

   my $privs_sth = $dbh->prepare('SELECT PRIVILEGE from session_privs');
   $privs_sth->execute;
   my @privileges = map { $_->[0] } @{ $privs_sth->fetchall_arrayref };

   SKIP: {
    skip q{don't have permission to create table} => 9
      unless grep { $_ eq 'CREATE TABLE' } @privileges;
    skip q{don't have permission to create sequence} => 9
      unless grep { $_ eq 'CREATE SEQUENCE' } @privileges;
    skip q{don't have permission to create trigger} => 9
      unless grep { $_ eq 'CREATE TRIGGER' } @privileges;

    my $table   = 'array_io_test__drop_me' . ( $ENV{DBD_ORACLE_SEQ} || '' );
    my $seq     = 'seq_io_test__drop_me' . ( $ENV{DBD_ORACLE_SEQ} || '' );
    my $trigger = 'trg_io_test__drop_me' . ( $ENV{DBD_ORACLE_SEQ} || '' );
    $dbh->do(
"create table $table (id number(12,0), name varchar2(20), value varchar2(2000))"
    );
    $dbh->do("create sequence $seq start with 1");
    $dbh->do(
        qq/
                create or replace trigger $trigger
                before insert
                on $table
                for each row
                DECLARE
                        iCounter $table.id%TYPE;
                BEGIN
                        if INSERTING THEN
                                Select $seq.nextval INTO iCounter FROM Dual;
                                :new.id := iCounter;
                        END IF;
                END;
        /
    );

    my @in_array1 = ( 'one', 'two', 'three', 'four', 'five' );
    my @in_array2 = ( '5',   '4',   '3',     '2',    '1' );
    my @out_array;
    my @tuple_status;

    my $sql =
      "insert into $table (name, value) values (?,?) returning id into ?";

    my $sth = $dbh->prepare($sql);

    $sth->bind_param_array( 1, \@in_array1 );
    $sth->bind_param_array( 2, \@in_array2 );
    ok(
        $sth->bind_param_inout_array(
            3, \@out_array, 0, { ora_type => ORA_VARCHAR2 }
        ),
        '... bind_param_inout_array should return false'
    );

    ok( $sth->execute_array( { ArrayTupleStatus => \@tuple_status } ),
        '... execute_array should return false' );

    cmp_ok( scalar(@tuple_status), '==', 5,
        '... we should have 19 tuple_status' );
    cmp_ok( scalar(@out_array), '==', 5, '... we should have 5 out_array' );
    cmp_ok( $out_array[0],      '==', 1, '... out values should match 1' );
    cmp_ok( $out_array[1],      '==', 2, '... out values should match 2' );
    cmp_ok( $out_array[2],      '==', 3, '... out values should match 3' );
    cmp_ok( $out_array[3],      '==', 4, '... out values should match 3' );
    cmp_ok( $out_array[4],      '==', 5, '... out values should match 5' );

    $dbh->do("drop table $table PURGE")  or warn $dbh->errstr;
    $dbh->do("drop sequence $seq") or die $dbh->errstr;
   }
}

sub test_number_SP {
   my $dbh = shift;

   my $privs_sth = $dbh->prepare('SELECT PRIVILEGE from session_privs');
   $privs_sth->execute;
   my @privileges = map { $_->[0] } @{ $privs_sth->fetchall_arrayref };

   SKIP: {
    skip q{don't have permission to create procedure} => 4
      unless grep { $_ eq 'CREATE PROCEDURE' } @privileges;

    $dbh->do(
        <<'EOF'
                create or replace procedure tox_test_proc0(
                        result   in out varchar2,
                        ids      in     SYS.dbms_sql.number_table
                )
                as
                begin
                        result := '';
                        for i in 1..ids.count loop
                                result := result || to_char(ids(i));
                        end loop;
                end;
EOF
    );

    my $sth = $dbh->prepare('begin tox_test_proc0( ?, ?); end;');

    my $result = '';
    my @array = ( 1, 2, 3, 4, 7 );

    $sth->bind_param_inout( 1, \$result, 5 );
    ok(
        $sth->bind_param(
            2, \@array,
            { ora_type => ORA_NUMBER_TABLE, ora_internal_type => SQLT_INT }
        ),
        '... bind_param_inout_array should bind 12345'
    );
    $sth->execute();
    cmp_ok( $result, '==', '12347', '... we should have 12347 out string' );

    @array = ( 3, 4, 5 );

    $sth->bind_param_inout( 1, \$result, 3 );
    ok(
        $sth->bind_param(
            2, \@array,
            { ora_type => ORA_NUMBER_TABLE, ora_internal_type => SQLT_INT }
        ),
        '... bind_param_inout_array should bind 345'
    );
    $sth->execute();
    cmp_ok( $result, '==', '345', '... we should have 345 out string' );

    $dbh->do('drop procedure tox_test_proc0') or warn $dbh->errstr;
   }
}

SKIP: {
    $dbh = db_connect(0);

    if ($dbh) {
        plan tests => 19;
    }
    else {
        plan skip_all => 'Unable to connect to Oracle' if not $dbh;
    }

    test_varchar2_table_3_tests($dbh);
    test_number_table_3_tests($dbh);
    test_inout_array_tests($dbh);
    test_number_SP($dbh);

}

END {
    eval { local $dbh->{PrintError} = 0; };
}

1;
