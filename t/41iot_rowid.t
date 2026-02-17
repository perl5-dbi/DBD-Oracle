#!perl
#
# Test selecting ROWID from Index Organized Tables (IOTs).
# IOT ROWIDs are UROWIDs (universal ROWIDs) of variable length,
# returned by OCI as type 104 (SQLT_RDD) rather than the fixed-length
# type 11 (ORA_ROWID) used for regular heap tables.
#
# See: https://github.com/perl5-dbi/DBD-Oracle/issues/31
#

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle table drop_table /;

use Test::More;
use DBI;
use DBD::Oracle qw(ORA_OCI);

$| = 1;

my $table = table();
my $dbh   = db_handle( { PrintError => 0, RaiseError => 1, AutoCommit => 1 } );

if ($dbh) {
    plan tests => 10;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}

# -- Setup: create an Index Organized Table with a multi-column PK

eval {
    local $dbh->{PrintError} = 0;
    local $dbh->{RaiseError} = 0;
    $dbh->do(qq{ DROP TABLE $table PURGE });
};

my $create_sql = qq{
    CREATE TABLE $table (
        c1  VARCHAR2(30),
        c2  TIMESTAMP(6),
        c3  NUMBER,
        CONSTRAINT ${table}_pk PRIMARY KEY (c1, c2, c3)
    ) ORGANIZATION INDEX
};

my $created = eval { $dbh->do($create_sql); 1 };

SKIP: {
    skip 'Unable to create IOT test table (may lack privileges)', 10
        unless $created;

    pass('IOT table created');

    # Insert a row
    ok(
        $dbh->do(qq{
            INSERT INTO $table VALUES (
                RPAD('a', 30, 'a'),
                CURRENT_TIMESTAMP,
                1/81
            )
        }),
        'inserted one row into IOT'
    );

    # -- Test 1: select columns without ROWID (baseline - should always work)
    my $sth = $dbh->prepare("SELECT t.* FROM $table t");
    ok($sth, 'prepare select without ROWID');
    ok($sth->execute, 'execute select without ROWID');
    my $rows = $sth->fetchall_arrayref;
    is(scalar @$rows, 1, 'fetched 1 row without ROWID');

    # -- Test 2: select only ROWID from IOT (GH #31 - this was the failing case)
    $sth = $dbh->prepare("SELECT ROWID FROM $table");
    ok($sth, 'prepare select ROWID from IOT');
    ok($sth->execute, 'execute select ROWID from IOT');
    $rows = eval { $sth->fetchall_arrayref };
    ok(defined $rows, 'fetchall ROWID from IOT did not error')
        or diag("Error fetching ROWID from IOT: " . ($sth->errstr || $@));
    is(scalar @$rows, 1, 'fetched 1 ROWID row from IOT');

    # -- Test 3: ROWID value should be a non-empty string
    my $rowid = $rows->[0][0];
    ok(defined $rowid && length($rowid) > 0, 'IOT ROWID is a non-empty string')
        or diag("ROWID value: " . (defined $rowid ? "'$rowid'" : 'undef'));
}

END {
    eval { drop_table($dbh) } if $dbh;
}

__END__
