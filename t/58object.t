#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use DBI;
use DBD::Oracle qw(ORA_RSET SQLCS_NCHAR);

use Test::More;

$| = 1;

if($^O eq 'cygwin')
{
    DBD::Oracle::ora_cygwin_set_env('NLS_DATE_FORMAT', 'YYYY-MM-DD"T"HH24:MI:SS');
}
else
{
    $ENV{NLS_DATE_FORMAT} = 'YYYY-MM-DD"T"HH24:MI:SS';
}

# create a database handle
my $dbh = eval{ db_handle( {
            RaiseError  => 1,
            AutoCommit  => 1,
            PrintError  => 0,
            ora_objects => 1
        })};

plan skip_all => 'Unable to connect to Oracle' unless $dbh;

plan tests => 65;

my $dbuser = $ENV{ORACLE_USERID} || 'scott/tiger';
my ($schema) = $dbuser =~ m{^([^/]*)};

# Test ora_objects flag
is $dbh->{ora_objects} => 1, 'ora_objects flag is set to 1';

$dbh->{ora_objects} = 0;
is $dbh->{ora_objects} => 0, 'ora_objects flag is set to 0';

# check that our db handle is good
isa_ok( $dbh, 'DBI::db' );

ok(
    $schema = $dbh->selectrow_array(
        q|select sys_context('userenv', 'current_schema') from dual|),
    'Fetch current schema name'
);

my $obj_prefix = 'dbd_test_' . ( $ENV{DBD_ORACLE_SEQ} || '' );
my $super_type = "${obj_prefix}_type_A";
my $sub_type   = "${obj_prefix}_type_B";
my $table      = "${obj_prefix}_obj_table";
my $outer_type = "${obj_prefix}_outer_type";
my $inner_type = "${obj_prefix}_inner_type";
my $list_type  = "${obj_prefix}_list_type";
my $nest_table = "${obj_prefix}_nest_table";
my $list_table = "${obj_prefix}_list_table";

sub sql_do_ok {
    my ( $dbh, $sql, $title ) = @_;
    $title = $sql unless defined $title;
    ok( $dbh->do($sql), $title ) or diag $dbh->errstr;
}

sub drop_test_objects {
    for my $obj (
        "TABLE $list_table",
        "TABLE $nest_table",
        "TYPE $list_type",
        "TYPE $outer_type",
        "TYPE $inner_type",
        "TABLE $table",
        "TYPE $sub_type",
        "TYPE $super_type"
      )
    {
        #do not warn if already there
        eval {
            local $dbh->{PrintError} = 0;
            $dbh->do(qq{drop $obj});
        };
    }
}

&drop_test_objects;

# get the user's privileges
my $privs_sth = $dbh->prepare('SELECT PRIVILEGE from session_privs');
$privs_sth->execute;
my @privileges = map { $_->[0] } @{ $privs_sth->fetchall_arrayref };

my $ora8      = $dbh->func('ora_server_version')->[0] < 9;
my $final     = $ora8 ? '' : 'FINAL';
my $not_final = $ora8 ? '' : 'NOT FINAL';

SKIP: {
    skip q{don't have permission to create type} => 61
      unless grep { $_ eq 'CREATE TYPE' } @privileges;

    sql_do_ok(
        $dbh, qq{ CREATE OR REPLACE TYPE $super_type AS OBJECT (
                num     INTEGER,
                name    VARCHAR2(20)
            ) $not_final }
    );

  SKIP: {
        skip 'Subtypes new in Oracle 9' => 1 if $ora8;
        sql_do_ok(
            $dbh, qq{ CREATE OR REPLACE TYPE $sub_type UNDER $super_type (
                datetime  DATE,
                amount    NUMERIC(10,5)
            ) $not_final }
        );
    }
    sql_do_ok( $dbh, qq{ CREATE TABLE $table (id INTEGER, obj $super_type) } );

    sql_do_ok( $dbh,
        qq{ INSERT INTO $table VALUES (1, $super_type(13, 'obj1')) } );
  SKIP: {
        skip 'Subtypes new in Oracle 9' => 2 if $ora8;
        sql_do_ok(
            $dbh, qq{ INSERT INTO $table VALUES (2, $sub_type(NULL, 'obj2',
                    TO_DATE('2004-11-30 14:27:18', 'YYYY-MM-DD HH24:MI:SS'),
                    12345.6789)) }
        );

        sql_do_ok(
            $dbh, qq{ INSERT INTO $table VALUES (3, $sub_type(5, 'obj3', NULL,
    777.666)) }
        );
    }
    sql_do_ok(
        $dbh, qq{ CREATE OR REPLACE TYPE $inner_type AS OBJECT (
                num     INTEGER,
                name    VARCHAR2(20)
            ) $final }
    );

    sql_do_ok(
        $dbh, qq{ CREATE OR REPLACE TYPE $outer_type AS OBJECT (
                num     INTEGER,
                obj     $inner_type
            ) $final }
    );

    sql_do_ok(
        $dbh, qq{ CREATE OR REPLACE TYPE $list_type AS
                            TABLE OF $inner_type }
    );

    sql_do_ok( $dbh, qq{ CREATE TABLE $nest_table(obj $outer_type) } );

    sql_do_ok( $dbh,
qq{ INSERT INTO $nest_table VALUES($outer_type(91, $inner_type(1, 'one'))) }
    );

    sql_do_ok( $dbh,
qq{ INSERT INTO $nest_table VALUES($outer_type(92, $inner_type(0, null))) }
    );

    sql_do_ok( $dbh,
        qq{ INSERT INTO $nest_table VALUES($outer_type(93, null)) } );

    sql_do_ok(
        $dbh, qq{ CREATE TABLE $list_table ( id INTEGER, list $list_type )
               NESTED TABLE list STORE AS ${list_table}_list }
    );

    sql_do_ok( $dbh,
qq{ INSERT INTO $list_table VALUES(81,$list_type($inner_type(null, 'listed'))) }
    );

    # Test old (backward compatible) interface

    # test select testing objects
    my $sth = $dbh->prepare("select * from $table order by id");
    ok( $sth,            'old: Prepare select' );
    ok( $sth->execute(), 'old: Execute select' );

    my ( @row1, @row2, @row3 );
    @row1 = $sth->fetchrow();
    ok( scalar @row1, 'old: Fetch first row' );
    cmp_ok( ref $row1[1], 'eq', 'ARRAY', 'old: Row 1 column 2 is an ARRAY' );
    cmp_ok( scalar( @{ $row1[1] } ),
        '==', 2, 'old: Row 1 column 2 has 2 elements' );
  SKIP: {
        skip 'Subtypes new in Oracle 9' => 6 if $ora8;
        @row2 = $sth->fetchrow();
        ok( scalar @row2, 'old: Fetch second row' );
        cmp_ok( ref $row2[1], 'eq', 'ARRAY',
            'old: Row 2 column 2 is an ARRAY' );
        cmp_ok( scalar( @{ $row2[1] } ),
            '==', 2, 'old: Row 2 column 2 has 2 elements' );

        @row3 = $sth->fetchrow();
        ok( scalar @row3, 'old: Fetch third row' );
        cmp_ok( ref $row3[1], 'eq', 'ARRAY',
            'old: Row 3 column 2 is an ARRAY' );
        cmp_ok( scalar( @{ $row3[1] } ),
            '==', 2, 'old: Row 3 column 2 has 2 elements' );
    }
    ok( !$sth->fetchrow(), 'old: No more rows expected' );

    #print STDERR Dumper(\@row1, \@row2, \@row3);

    # Test new (extended) object interface

    # enable extended object support
    $dbh->{ora_objects} = 1;

    # test select testing objects - in extended mode
    $sth = $dbh->prepare("select * from $table order by id");
    ok( $sth,            'new: Prepare select' );
    ok( $sth->execute(), 'new: Execute select' );

    @row1 = $sth->fetchrow();
    ok( scalar @row1, 'new: Fetch first row' );
    cmp_ok( ref $row1[1],
        'eq', 'DBD::Oracle::Object',
        'new: Row 1 column 2 is an DBD:Oracle::Object' );
    cmp_ok(
        uc $row1[1]->type_name,
        'eq',
        uc "$schema.$super_type",
        'new: Row 1 column 2 object type'
    );
    is_deeply(
        [ $row1[1]->attributes ],
        [ 'NUM', 13, 'NAME', 'obj1' ],
        'new: Row 1 column 2 object attributes'
    );
  SKIP: {
        skip 'Subtypes new in Oracle 9' => 8 if $ora8;
        @row2 = $sth->fetchrow();
        ok( scalar @row2, 'new: Fetch second row' );
        cmp_ok( ref $row2[1],
            'eq', 'DBD::Oracle::Object',
            'new: Row 2 column 2 is an DBD::Oracle::Object' );
        cmp_ok(
            uc $row2[1]->type_name,
            'eq',
            uc "$schema.$sub_type",
            'new: Row 2 column 2 object type'
        );

        my %attrs = $row2[1]->attributes;

        $attrs{AMOUNT} = sprintf '%9.4f', $attrs{AMOUNT};

        is_deeply(
            \%attrs,
            {
                'NUM',      undef,
                'NAME',     'obj2',
                'DATETIME', '2004-11-30T14:27:18',
                'AMOUNT',   '12345.6789'
            },
            'new: Row 1 column 2 object attributes'
        );

        @row3 = $sth->fetchrow();
        ok( scalar @row3, 'new: Fetch third row' );
        cmp_ok( ref $row3[1],
            'eq', 'DBD::Oracle::Object',
            'new: Row 3 column 2 is an DBD::Oracle::Object' );
        cmp_ok(
            uc $row3[1]->type_name,
            'eq',
            uc "$schema.$sub_type",
            'new: Row 3 column 2 object type'
        );

        %attrs = $row3[1]->attributes;
        $attrs{AMOUNT} = sprintf '%6.3f', $attrs{AMOUNT};

        is_deeply(
            \%attrs,
            {
                'NUM',      5,     'NAME',   'obj3',
                'DATETIME', undef, 'AMOUNT', '777.666'
            },
            'new: Row 1 column 2 object attributes'
        );
    }
    ok( !$sth->fetchrow(), 'new: No more rows expected' );

    #print STDERR Dumper(\@row1, \@row2, \@row3);

  SKIP: {
        skip 'Subtypes new in Oracle 9' => 3 if $ora8;

        # Test DBD::Oracle::Object
        my $obj           = $row3[1];
        my $expected_hash = {
            NUM      => 5,
            NAME     => 'obj3',
            DATETIME => undef,
            AMOUNT   => 777.666,
        };
        my $attrs = $obj->attr_hash;
        $attrs->{AMOUNT} = sprintf '%6.3f', $attrs->{AMOUNT};

        is_deeply( $attrs, $expected_hash, 'DBD::Oracle::Object->attr_hash' );
        is_deeply( $obj->attr, $expected_hash, 'DBD::Oracle::Object->attr' );
        is( $obj->attr('NAME'), 'obj3',
            q|DBD::Oracle::Object->attr(' NAME ')| );
    }

    # try the list table
    $sth = $dbh->prepare("select * from $list_table");
    ok( $sth,            'new: Prepare select with nested table of objects' );
    ok( $sth->execute(), 'new: Execute (nested table)' );

    @row1 = $sth->fetchrow();
    ok( scalar @row1, 'new: Fetch first row (nested table)' );
    is_deeply(
        $row1[1]->[0]->attr,
        { NUM => undef, NAME => 'listed' },
        'Check properties of first (and only) item in nested table'
    );

    ok( !$sth->fetchrow(), 'new: No more rows expected (nested table)' );

    #try the nested table
    $sth = $dbh->prepare("select * from $nest_table");
    ok( $sth,            'new: Prepare select with nested object' );
    ok( $sth->execute(), 'new: Execute (nested object)' );

    @row1 = $sth->fetchrow();
    ok( scalar @row1, 'new: Fetch first row (nested object)' );
    is( $row1[0]->attr->{NUM}, '91', 'Check obj.num' );
    is_deeply(
        $row1[0]->attr->{OBJ}->attr,
        { NUM => '1', NAME => 'one' },
        'Check obj.obj'
    );

    @row2 = $sth->fetchrow();
    ok( scalar @row2, 'new: Fetch second row (nested object)' );
    is( $row2[0]->attr->{NUM}, '92', 'Check obj.num' );
    is_deeply(
        $row2[0]->attr->{OBJ}->attr,
        { NUM => '0', NAME => undef },
        'Check obj.obj'
    );

    @row3 = $sth->fetchrow();
    ok( scalar @row3, 'new: Fetch third row (nested object)' );
    is_deeply( $row3[0]->attr, { NUM => '93', OBJ => undef }, 'Check obj' );

    ok( !$sth->fetchrow(), 'new: No more rows expected (nested object)' );

}

#cleanup
&drop_test_objects unless $ENV{DBD_SKIP_TABLE_DROP};
