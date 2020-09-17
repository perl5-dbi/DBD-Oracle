#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ oracle_test_dsn db_handle /;

use Test::More;

use DBI;
use DBD::Oracle qw(ORA_RSET SQLCS_NCHAR);

$| = 1;

my $dbh    = db_handle( { PrintError => 0 } );

if ($dbh) {

    # ORA-00900: invalid SQL statement
    # ORA-06553: PLS-213: package STANDARD not accessible
    my $tst =
      $dbh->prepare(q{declare foo char(50); begin RAISE INVALID_NUMBER; end;});
    if ( $dbh->err
        && ( $dbh->err == 900 || $dbh->err == 6553 || $dbh->err == 600 ) )
    {
        diag("Your Oracle server doesn't support PL/SQL") if $dbh->err == 900;
        diag('Your Oracle PL/SQL is not properly installed')
          if $dbh->err == 6553 || $dbh->err == 600;
        plan skip_all =>
'Oracle server either does not support pl/sql or it is not properly installed';
    }
    plan tests => 86;
}
else {
    plan skip_all => "Unable to connect to Oracle \n";
}

my ( $csr, $p1, $p2, $tmp, @tmp );

#DBI->trace(4,"trace.log");

# --- test raising predefined exception
ok(
    $csr = $dbh->prepare(
        q{
    begin RAISE INVALID_NUMBER; end;}
    ),
    'prepare raising predefined exception'
);

# ORA-01722: invalid number
ok( !$csr->execute, 'execute predefined exception' );
is( $DBI::err, 1722, 'err expected 1722 error' );
is( $DBI::err, 1722, 'err does not get cleared' );

# --- test raising user defined exception
ok(
    $csr = $dbh->prepare(
        q{
    DECLARE FOO EXCEPTION;
    begin raise FOO; end;}
    ),
    'prepare user defined expcetion'
);

# ORA-06510: PL/SQL: unhandled user-defined exception
ok( !$csr->execute, 'execute user defined exception' );
is( $DBI::err, 6510, 'user exception 6510 error' );

# --- test raise_application_error with literal values
ok(
    $csr = $dbh->prepare(
        q{
    declare err_num number; err_msg char(510);
    begin RAISE_APPLICATION_ERROR(-20101,'app error'); end;}
    ),
    'prepare raise application error with literal values'
);

# ORA-20101: app error
ok( !$csr->execute, 'execute raise application error with literal values' );
is( $DBI::err, 20101, 'expected 20101 error' );
like( $DBI::errstr, qr/app error/, 'app error' );

# --- test raise_application_error with 'in' parameters
ok(
    $csr = $dbh->prepare(
        q{
    declare err_num varchar2(555); err_msg varchar2(510);
    --declare err_num number; err_msg char(510);
    begin
        err_num := :1;
        err_msg := :2;
        raise_application_error(-20000-err_num, 'msg is '||err_msg);
    end;
}
    ),
    'prepare raise application error with in params'
);

ok( !$csr->execute( 42, 'hello world' ),
    'execute raise application error with in params' );
is( $DBI::err, 20042, 'expected 20042 error' );
like( $DBI::errstr, qr/msg is hello world/, 'hello world msg' );

# --- test named numeric in/out parameters
ok(
    $csr = $dbh->prepare(
        q{
    begin
        :arg := :arg * :mult;
    end;}
    ),
    'prepare named numeric in/out params'
);

$p1 = 3;
ok( $csr->bind_param_inout( ':arg', \$p1, 50 ), 'bind arg' );
ok( $csr->bind_param( ':mult', 2 ), 'bind mult' );
ok( $csr->execute, 'execute named numeric in/out params' );
is( $p1, 6, 'expected 3 * 3 = 6' );

# execute 10 times from $p1=1, 2, 4, 8, ... 1024
$p1 = 1;
eval {
    foreach ( 1 .. 10 ) { $csr->execute || die $DBI::errstr; }
};
my $ev = $@;
ok( !$ev, 'execute named numeric in/out params 10 times' );
is( $p1, 1024, 'expected p1 = 1024' );

# --- test undef parameters
ok(
    $csr = $dbh->prepare(
        q{
        declare foo char(500);
        begin foo := :arg; end;}
    ),
    'prepare undef parameters'
);
my $undef;
ok( $csr->bind_param_inout( ':arg', \$undef, 10 ), 'bind arg' );
ok( $csr->execute, 'execute undef parameters' );

# --- test named string in/out parameters
ok(
    $csr = $dbh->prepare(
        q{
    declare str varchar2(1000);
    begin
        :arg := nvl(upper(:arg), 'null');
        :arg := :arg || :append;
    end;}
    ),
    'prepare named string in/out parameters'
);

undef $p1;
$p1 = 'hello world';
ok( $csr->bind_param_inout( ':arg', \$p1, 1000 ), 'bind arg' );
ok( $csr->bind_param( ':append', '!' ), 'bind append' );
ok( $csr->execute, 'execute named string in/out parameters' );
is( $p1, 'HELLO WORLD!', 'expected HELLO WORLD' );

# execute 10 times growing $p1 to force realloc
eval {
    for ( 1 .. 10 ) {
        $p1 .= ' xxxxxxxxxx';
        $csr->execute || die $DBI::errstr;
    }
};
$ev = $@;
ok( !$ev, 'execute named string in/out parameters 1- times' );
my $expect = 'HELLO WORLD!' . ( ' XXXXXXXXXX!' x 10 );
is( $p1, $expect, 'p1 as expected' );

# --- test binding a null and getting a string back
undef $p1;
ok( $csr->execute, 'execute binding a null' );
is( $p1, 'null!', 'get a null string back' );

$csr->finish;

ok(
    $csr = $dbh->prepare(
        q{
    begin
        :out := nvl(upper(:in), 'null');
    end;}
    ),
    'prepare nvl'
);

#$csr->trace(3);
my $out;
ok( $csr->bind_param_inout( ':out', \$out, 1000 ), 'bind out' );
ok( $csr->bind_param( ':in', 'foo', DBI::SQL_CHAR() ), 'bind in' );
ok( $csr->execute, 'execute nvl' );
is( $out, 'FOO', 'expected FOO' );

ok( $csr->bind_param( ':in', '' ), 'bind empty string' );
ok( $csr->execute, 'execute empty string' );
is( $out, 'null', 'returned null string' );

# --- test out buffer being too small
ok(
    $csr = $dbh->prepare(
        q{
    begin
        select rpad('foo',200) into :arg from dual;
    end;}
    ),
    'prepare test output buffer too small'
);

#$csr->trace(3);
undef $p1;    # force buffer to be freed
ok( $csr->bind_param_inout( ':arg', \$p1, 20 ), 'bind arg' );

# Execute fails with:
#        ORA-06502: PL/SQL: numeric or value error
#        ORA-06512: at line 3 (DBD ERROR: OCIStmtExecute)
$tmp = $csr->execute;

#$tmp = undef if DBD::Oracle::ORA_OCI()>=8; # because BindByName given huge max len
ok( !defined $tmp, 'output buffer too small' );

# rebind with more space - and it should work
ok( $csr->bind_param_inout( ':arg', \$p1, 200 ), 'rebind arg with more space' );
ok( $csr->execute, 'execute rebind with more space' );
is( length($p1), 200, 'expected return length' );

# --- test plsql_errstr function
#$csr = $dbh->prepare(q{
#    create or replace procedure perl_dbd_oracle_test as
#    begin
#          procedure filltab( stuff out tab ); asdf
#    end;
#});
#ok(0, ! $csr);
#if ($dbh->err && $dbh->err == 6550) {        # PL/SQL error
#        warn "errstr: ".$dbh->errstr;
#        my $msg = $dbh->func('plsql_errstr');
#        warn "plsql_errstr: $msg";
#        ok(0, $msg =~ /Encountered the symbol/, "plsql_errstr: $msg");
#}
#else {
#        warn "plsql_errstr test skipped ($DBI::err)\n";
#        ok(0, 1);
#}
#die;

# --- test dbms_output_* functions
$dbh->{PrintError} = 1;
ok( $dbh->func( 30000, 'dbms_output_enable' ), 'dbms_output_enable' );

#$dbh->trace(3);
my @ary = ( 'foo', ( 'bar' x 15 ), 'baz', 'boo' );
ok( $dbh->func( @ary, 'dbms_output_put' ), 'dbms_output_put' );

@ary = scalar $dbh->func('dbms_output_get');    # scalar context
ok( @ary == 1 && $ary[0] && $ary[0] eq 'foo', 'dbms_output_get foo' );

@ary = scalar $dbh->func('dbms_output_get');    # scalar context
ok( @ary == 1 && $ary[0] && $ary[0] eq 'bar' x 15, 'dbms_output_get bar' );

@ary = $dbh->func('dbms_output_get');           # list context
is( join( ':', @ary ), 'baz:boo', 'dbms_output_get baz:boo' );
$dbh->{PrintError} = 0;

#$dbh->trace(0);

# --- test cursor variables
if (1) {
    my $cur_query = q{
        SELECT object_name, owner
        FROM all_objects
        WHERE object_name LIKE :p1
        ORDER BY object_name
    };
    my $cur1 = 42;

    #$dbh->trace(4);
    my $parent = $dbh->prepare(
        qq{
        BEGIN OPEN :cur1 FOR $cur_query; END;
    }
    );
    ok( $parent, 'prepare cursor' );
    ok( $parent->bind_param( ':p1', 'V%' ), 'bind p1' );
    ok(
        $parent->bind_param_inout(
            ':cur1', \$cur1, 0, { ora_type => ORA_RSET }
        ),
        'bind cursor'
    );
    ok( $parent->execute(), 'execute for cursor' );
    my @r;
    push @r, @tmp while @tmp = $cur1->fetchrow_array;
    ok( @r > 0, 'rows: ' . @r );

    #$dbh->trace(0); $parent->trace(0);

    # compare results with normal execution of query
    my $s1 = $dbh->selectall_arrayref( $cur_query, undef, 'V%' );
    my @s1 = map { @$_ } @$s1;
    is( join( ' ', sort @r ), join( ' ', sort @s1 ), 'ref = sql' );

    # --- test re-bind and re-execute of same 'parent' statement
    my $cur1_str = "$cur1";

    #$dbh->trace(4); $parent->trace(4);
    ok( $parent->bind_param( ':p1', 'U%' ), 'bind p1' );
    ok( $parent->execute(), 'execute for cursor' );

    # must be ref to new handle object
    isnt( "$cur1", $cur1_str, 'expected ref to new handle' );
    @r = ();
    push @r, @tmp while @tmp = $cur1->fetchrow_array;

    #$dbh->trace(0); $parent->trace(0); $cur1->trace(0);
    my $s2 = $dbh->selectall_arrayref( $cur_query, undef, 'U%' );
    my @s2 = map { @$_ } @$s2;
    is( "@r", "@s2", 'ref = sql' );
}

SKIP: {
   # test bind_param_inout of param that's not assigned to in executed statement
   # Github Issue #70
   # Also see http://www.mail-archive.com/dbi-users@perl.org/msg18835.html

   # Known bad OCI versions
   my @bad_oci_vers = (9.2,18.3,18.5,19.6);

   skip 'Client version is known to have issue', 4
     if grep { $_ == DBD::Oracle::ORA_OCI() } @bad_oci_vers;

   my $sth = $dbh->prepare(
      q(
      BEGIN
 --     :p1 := :p1 ;
 --     :p2 := :p2 ;
        IF  :p2 != :p3 THEN
            :p1 := 'AAA' ;
            :p2 := 'Z' ;
        END IF ;
      END ;)
   );

   my ( $p1, $p2, $p3 ) = ( 'Hello', 'Y', 'Y' );
   $sth->bind_param_inout( ':p1', \$p1, 30 );
   $sth->bind_param_inout( ':p2', \$p2, 1 );
   $sth->bind_param_inout( ':p3', \$p3, 1 );
   note("Before p1=[$p1] p2=[$p2] p3=[$p3]\n");
   ok( $sth->execute, 'test bind_param_inout for non assigned' );
   is( $p1, 'Hello', 'p1 ok' );
   is( $p2, 'Y',     'p2 ok' );
   is( $p3, 'Y',     'p3 ok' );
   note("After p1=[$p1] p2=[$p2] p3=[$p3]\n");
}

# test bind_paraminout the correct way (avoids the above issue if present)
my $sth = $dbh->prepare(
    q(
    BEGIN
        :p1 := :p1 ;
        :p2 := :p2 ;
        IF  :p2 != :p3 THEN
            :p1 := 'AAA' ;
            :p2 := 'Z' ;
        END IF ;
END ;)
);

{
    my ( $p1, $p2, $p3 ) = ( 'Hello', 'Y', 'Y' );
    $sth->bind_param_inout( ':p1', \$p1, 30 );
    $sth->bind_param_inout( ':p2', \$p2, 1 );
    $sth->bind_param_inout( ':p3', \$p3, 1 );
    note("Before p1=[$p1] p2=[$p2] p3=[$p3]\n");
    ok( $sth->execute, 'test bind_param_inout for properly assigned' );
    is( $p1, 'Hello', 'p1 ok' );
    is( $p2, 'Y',     'p2 ok' );
    is( $p3, 'Y',     'p3 ok' );
    note("After p1=[$p1] p2=[$p2] p3=[$p3]\n");
}

SKIP: {
    # test nvarchar2 arg passing to functions
    # http://www.nntp.perl.org/group/perl.dbi.users/24217
    my $ora_server_version = $dbh->func('ora_server_version');
    skip 'Client/server version < 9.0', 15
      if DBD::Oracle::ORA_OCI() < 9.0 || $ora_server_version->[0] < 9;

    my $func_name = 'dbd_oracle_nvctest' . ( $ENV{DBD_ORACLE_SEQ} || '' );
    $dbh->do(
        qq{
        CREATE OR REPLACE FUNCTION $func_name(arg nvarchar2, arg2 nvarchar2)
        RETURN int IS
        BEGIN
          if arg is null or arg2 is null then
             return -1;
          else
             return 1;
          end if;
        END;
    }
    ) or skip "Can't create a function ($DBI::errstr)", 15;
    my $sth = $dbh->prepare(
        qq{SELECT $func_name(?, ?) FROM DUAL},
        {
            # Oracle 8 describe fails with ORA-06553: PLS-561: charset mismatch
            ora_check_sql => 0,
        }
    );
    ok(
        $sth,
        sprintf(
            q/Can't prepare select from function (%s)/, $DBI::errstr || ''
        )
    );
    skip "Can't select from function ($DBI::errstr)", 14 unless $sth;
    for ( 1 .. 2 ) {
        ok( $sth->bind_param( 1, 'foo', { ora_csform => SQLCS_NCHAR } ),
            'bind foo' );
        ok( $sth->bind_param( 2, 'bar', { ora_csform => SQLCS_NCHAR } ),
            'bind bar' );
        ok( $sth->execute(), 'execute' );
        ok( my ($returnVal) = $sth->fetchrow_array, 'fetchrow returns value' );
        is( $returnVal, '1', 'expected return value of 1' );
    }
    ok( $sth->execute( 'baz', undef ), 'execute with baz' );
    ok( my ($returnVal) = $sth->fetchrow_array, 'fetchrow_returns value' );
    is( $returnVal, '-1', 'expected -1 return' );
    ok( $dbh->do(qq{drop function $func_name}), "drop $func_name" );
}

# --- To do
#   test NULLs at first bind
#   NULLs later binds.
#   returning NULLs
#   multiple params, mixed types and in only vs inout

exit 0;

__END__
