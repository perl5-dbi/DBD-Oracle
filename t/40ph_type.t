#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle drop_table force_drop_table table /;

use Test::More;

use DBI qw(neat);
use DBD::Oracle qw(ORA_OCI);
use vars qw($tests);

$|  = 1;
$^W = 1;

# XXX ought to extend tests to check 'blank padded comparision semantics'
my @tests = (

    # type: oracle internal type to use for placeholder values
    # name: oracle name for type above
    # chops_space: set true if type trims trailing space characters
    # embed_nul:   set true if type allows embedded nul characters
    # (also SKIP=1 to skip test, ti=N to trace insert, ts=N to trace select)
    { type => 1, name => 'VARCHAR2', chops_space => 1, embed_nul => 1, }
    ,    # current DBD::Oracle
    {
        type        => 5,
        name        => 'STRING',
        chops_space => 0,
        embed_nul   => 0,
        SKIP        => 1,
        ti          => 8
    },    # old Oraperl
    { type => 96, name => 'CHAR', chops_space => 0, embed_nul => 1, },
    {
        type        => 97,
        name        => 'CHARZ',
        chops_space => 0,
        embed_nul   => 0,
        SKIP        => 1,
        ti          => 8
    },
);

$tests = 1;
$_->{SKIP} or $tests += 8 for @tests;

my $dbh = db_handle(
    {
        AutoCommit       => 0,
        PrintError       => 0,
        FetchHashKeyName => 'NAME_lc',
    }
);

if ($dbh) {
    plan tests => $tests;
}
else {
    plan skip_all => 'Unable to connect to Oracle';
}

eval {
    require Data::Dumper;
    $Data::Dumper::Useqq  = $Data::Dumper::Useqq  = 1;
    $Data::Dumper::Terse  = $Data::Dumper::Terse  = 1;
    $Data::Dumper::Indent = $Data::Dumper::Indent = 1;
};

my ( $sth, $tmp );
my $table = table();

# drop table but don't warn if not there
eval { force_drop_table($dbh, $table) };

ok(
    $dbh->do(
        "CREATE TABLE $table (name VARCHAR2(2), vc VARCHAR2(20), c CHAR(20))"),
    'create test table'
);

my $val_with_trailing_space = 'trailing ';
my $val_with_embedded_nul   = "embedded\0nul";

for my $test_info (@tests) {
    next if $test_info->{SKIP};

    my $ph_type = $test_info->{type} || die;
    my $name    = $test_info->{name} || die;
    note("\ntesting @{[ %$test_info ]} ...\n\n");

  SKIP: {
        skip "skipping tests", 12 if ( $test_info->{SKIP} );

        $dbh->{ora_ph_type} = $ph_type;
        ok( $dbh->{ora_ph_type} == $ph_type, 'set ora_ph_type' );

        $sth = $dbh->prepare("INSERT INTO $table(name,vc,c) VALUES (?,?,?)");
        $sth->trace( $test_info->{ti} ) if $test_info->{ti};
        $sth->execute( 'ts', $val_with_trailing_space,
            $val_with_trailing_space );
        $sth->execute( 'en', $val_with_embedded_nul, $val_with_embedded_nul );
        $sth->execute( 'es', '', '' );    # empty string
        $sth->trace(0) if $test_info->{ti};

        $dbh->trace( $test_info->{ts} ) if $test_info->{ts};
        $tmp = $dbh->selectall_hashref(
            qq{
          SELECT name, vc, length(vc) as len, nvl(vc,'ISNULL') as isnull, c
          FROM $table}, 'name'
        );
        ok( keys(%$tmp) == 3, 'right keys' );
        $dbh->trace(0) if $test_info->{ts};
        $dbh->rollback;

        delete $_->{name} foreach values %$tmp;
        note( Data::Dumper::Dumper($tmp) );

        # check trailing_space behaviour
        my $expect = $val_with_trailing_space;
        $expect =~ s/\s+$// if $test_info->{chops_space};
        my $ok = ( $tmp->{ts}->{vc} eq $expect );
        if ( !$ok && $ph_type == 1 && $name eq 'VARCHAR2' ) {
            note
" Placeholder behaviour for ora_type=1 VARCHAR2 (the default) varies with Oracle version.\n"
              . " Oracle 7 didn't strip trailing spaces, Oracle 8 did, until 9.2.x\n"
              . " Your system doesn't. If that seems odd, let us know.\n";
            $ok = 1;
        }
        ok(
            $ok,
            sprintf(
                " using ora_type %d expected %s but got %s for $name",
                $ph_type, neat($expect), neat( $tmp->{ts}->{vc} )
            )
        );

        # check embedded nul char behaviour
        $expect = $val_with_embedded_nul;
        $expect =~ s/\0.*// unless $test_info->{embed_nul};
        is(
            $tmp->{en}->{vc},
            $expect,
            sprintf(
                " expected %s but got %s for $name",
                neat($expect), neat( $tmp->{en}->{vc} )
            )
        );

        # check empty string is NULL (irritating Oracle behaviour)
        ok( !defined $tmp->{es}->{vc},  'vc defined' );
        ok( !defined $tmp->{es}->{c},   'c defined' );
        ok( !defined $tmp->{es}->{len}, 'len defined' );
        is( $tmp->{es}->{isnull}, 'ISNULL', 'ISNULL' );

        exit 1 if $test_info->{ti} || $test_info->{ts};
    }
}

$dbh && $dbh->disconnect;

END { eval { drop_table($dbh,$table); } }


__END__
