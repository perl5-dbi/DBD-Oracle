#!perl
# gh#84 - ORA_DATE can't be used as bind param
#
# Binding a placeholder with ora_type => ORA_DATE used to croak with:
#   "Can't bind :p1, ora_type 12 not supported by DBD::Oracle"
# because ORA_DATE was missing from the oratype_bind_ok() whitelist.

use strict;
use warnings;
use English qw(-no_match_vars);

use lib 't/lib';
use DBDOracleTestLib qw/ db_handle drop_table force_drop_table table /;

use Test::More import => [ qw( is ok plan ) ];
use DBI;
use DBD::Oracle qw(:ora_types);

$OUTPUT_AUTOFLUSH = 1;

my $dbh = db_handle( { PrintError => 0 } );

if ( !$dbh ) {
    plan skip_all => 'Unable to connect to Oracle';
}

plan tests => 7;

my $table = table();

# Clean up any leftover table
eval { force_drop_table($dbh, $table) };

ok(
    $dbh->do("CREATE TABLE $table (id NUMBER, dt DATE)"),
    'create test table with DATE column'
);

# Set a known NLS_DATE_FORMAT so our date string is parsed correctly
$dbh->do("ALTER SESSION SET NLS_DATE_FORMAT = 'YYYY-MM-DD'");

# -----------------------------------------------------------------------
# Test 1: bind_param with ora_type => ORA_DATE should not croak
# -----------------------------------------------------------------------

my $sth = $dbh->prepare("INSERT INTO $table (id, dt) VALUES (?, ?)");
ok( $sth, 'prepare INSERT with DATE placeholder' );

eval {
    $sth->bind_param( 1, 1 );
    $sth->bind_param( 2, '2025-01-15', { ora_type => ORA_DATE } );
};
is( $@, '', 'bind_param with ora_type => ORA_DATE does not croak (gh#84)' );

ok( $sth->execute, 'execute INSERT with ORA_DATE bound param' );

# -----------------------------------------------------------------------
# Test 2: The bound DATE value round-trips correctly via SELECT
# -----------------------------------------------------------------------

my $sel = $dbh->prepare(
    "SELECT id, TO_CHAR(dt, 'YYYY-MM-DD') AS dt_str FROM $table WHERE id = ?"
);
ok( $sel, 'prepare SELECT' );
ok( $sel->execute(1), 'execute SELECT' );

my $row = $sel->fetchrow_hashref;
is( $row->{dt_str} || $row->{DT_STR}, '2025-01-15',
    'DATE value round-trips correctly through ORA_DATE bind' );

END {
    eval { drop_table($dbh, $table) } if $dbh;
}

__END__
