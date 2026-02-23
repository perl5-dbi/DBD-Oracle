#!perl

# https://github.com/perl5-dbi/DBD-Oracle/issues/86

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use Test::More;

use DBI qw(:sql_types);
use Devel::Peek qw(Dump);
use B qw( svref_2object SVf_IOK SVf_NOK SVf_POK );

sub is_iv {
    my $sv = svref_2object( my $ref = \$_[0] );
    my $flags = $sv->FLAGS;

    # See http://www.perlmonks.org/?node_id=971411
    my $x = $sv->can('PV') ? $sv->PV : undef;

    if (wantarray) {
        return ( $flags & SVf_IOK, $x );
    }
    else {
        return $flags & SVf_IOK;
    }
}

my $dbh = db_handle(
    {
        PrintError       => 0,
        FetchHashKeyName => 'NAME_lc'
    }
);

plan skip_all => 'Unable to connect to Oracle database' if not $dbh;

plan tests => 1;

TODO: {
    local $TODO = 'Bug is not fixed. Just demonstrate it';
    subtest 'bind_col TYPE stickiness' => \&test_bind_col;
}

sub test_bind_col {
    plan tests => 4;

    # prepare a two-column row so we can exercise the slice/hashref path
    my $s = $dbh->prepare(q/select 1 as one, '2' as two from dual/);
    $s->execute;

    # bind the first column as an integer
    $s->bind_col( 1, undef, { TYPE => SQL_INTEGER, DiscardString => 1 } );

    # fetch as hashref slice (DBI may call bind_col again internally)
    my $hash_rows = $s->fetchall_arrayref( {} );

    is( $hash_rows->[0]{one}, 1, 'fetchall_arrayref({}) returns numeric one' );
    ok( is_iv( $hash_rows->[0]{one} ), 'fetchall_arrayref({}) 1 value is IV' )
        or diag Dump $hash_rows->[0]{one};
    ok( is_iv( $hash_rows->[0]{two} ), 'fetchall_arrayref({}) 2 value is IV' )
        or diag Dump $hash_rows->[0]{two};

    # re-execute and fetch as plain arrayref to compare
    $s->execute;
    my $arr_rows = $s->fetchall_arrayref();

    is( $arr_rows->[0][0], 1, 'fetchall_arrayref() returns numeric one' );
    ok( is_iv( $arr_rows->[0][0] ), 'fetchall_arrayref() 1 value is IV' )
        or diag Dump $arr_rows->[0][0];
    ok( is_iv( $arr_rows->[0][1] ), 'fetchall_arrayref() 2 value is IV' )
        or diag Dump $arr_rows->[0][1];

}
