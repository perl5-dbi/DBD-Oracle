#!/usr/bin/env perl

use strict;
use warnings;

package DBDOracleTestLib;
use Test::More;

use Exporter 'import';

use vars qw( @EXPORT_OK );

@EXPORT_OK = (
    qw/ db_handle extra_wide_rows long_test_cols
        oracle_test_dsn show_test_data test_data
        select_test_count select_rows
        cmp_ok_byte_nice show_db_charsets
        db_ochar_is_utf db_nchar_is_utf
        client_ochar_is_utf8 client_nchar_is_utf8
        set_nls_nchar set_nls_lang_charset
        insert_test_count nice_string force_drop_table
        create_table table drop_table insert_rows dump_table
/,
);

use Carp;
use Data::Dumper;
use DBI;
use DBD::Oracle qw(ORA_OCI ora_env_var);

require utf8;

# perl 5.6 doesn't define utf8::is_utf8()
unless ( defined &{'utf8::is_utf8'} ) {
    die "Can't run this test using Perl $] without DBI >= 1.38"
      unless $DBI::VERSION >= 1.38;
    *utf8::is_utf8 = sub {
        my $raw = shift;
        return 0 if !defined $raw;
        my $v = DBI::neat($raw);
        return 1 if $v =~ m/^"/;    # XXX ugly hack, sufficient here
        return 0 if $v =~ m/^'/;    # XXX ugly hack, sufficient here
        carp "Emulated utf8::is_utf8 is unreliable for $v ($raw)";
        return 0;
      }
}

=head binmode STDOUT, ':utf8'

 Wide character in print at t/nchar_test_lib.pl line 134 (#1)
    (W utf8) Perl met a wide character (>255) when it wasn't expecting
    one.  This warning is by default on for I/O (like print).  The easiest
    way to quiet this warning is simply to add the :utf8 layer to the
    output, e.g. binmode STDOUT, ':utf8'.  Another way to turn off the
    warning is to add no warnings 'utf8'; but that is often closer to
    cheating.  In general, you are supposed to explicitly mark the
    filehandle with an encoding, see open and perlfunc/binmode.
=cut

eval { binmode STDOUT, ':utf8' };    # Fails for perl 5.6
diag("Can't set binmode(STDOUT, ':utf8'): $@") if $@;
eval { binmode STDERR, ':utf8' };    # Fails for perl 5.6
diag("Can't set binmode(STDERR, ':utf8'): $@") if $@;

# Test::More duplicates STDOUT/STDERR at the start but does not copy the IO
# layers from our STDOUT/STDERR. As a result any calls to Test::More::diag
# with utf8 data will show warnings. Similarly, if we pass utf8 into
# Test::More::pass, ok, etc etc. To get around this we specifically tell
# Test::More to use our newly changed STDOUT and STDERR for failure_output
# and output.
my $tb = Test::More->builder;
binmode( $tb->failure_output, ':utf8' );
binmode( $tb->output,         ':utf8' );

sub long_test_cols {
    my ($type) = @_;
    return [ [ lng => $type ], ];
}


sub extra_wide_rows {

    # Non-BMP characters require use of surrogates with UTF-16
    # So U+10304 becomes U+D800 followed by U+DF04 (I think) in UTF-16.
    #
    # When encoded as standard UTF-8, which Oracle calls AL32UTF8, it should
    # be a single UTF-8 code point (that happens to occupy 4 bytes).
    #
    # When encoded as "CESU-8", which Oracle calls "UTF8", each surrogate
    # is treated as a code point so you get 2 UTF-8 code points
    # (that happen to occupy 3 bytes each). That is not valid UTF-8.
    # See http://www.unicode.org/reports/tr26/ for more information.
    return unless ORA_OCI >= 9.2;    # need AL32UTF8 for these to work
    return (
        [ "\x{10304}", 'SMP Plane 1 wide char' ],    # OLD ITALIC LETTER E
        [ "\x{20301}", 'SIP Plane 2 wide char' ]
        ,    # CJK Unified Ideographs Extension B
    );
}

{

my $char_cols =
    [ [ ch => 'varchar2(20)' ], [ descr => 'varchar2(50)' ], ];

my $nchar_cols =
    [ [ nch => 'nvarchar2(20)' ], [ descr => 'varchar2(50)' ], ];

my $wide_data =
    [
        [ "\x{03}",   'control-C'   ],
        [ 'a',        'lowercase a' ],
        [ 'b',        'lowercase b' ],
        [ "\x{263A}", 'smiley face' ],

        # These are not safe for db's with US7ASCII
        #       [ "\x{A1}", "upside down bang" ],
        #       [ "\x{A2}", "cent char"        ],
        #       [ "\x{A3}", "british pound"    ],
    ];

sub _narrow_data    # Assuming WE8ISO8859P1 or WE8MSWIN1252 character set
{
    my $highbitset = [

        # These non-unicode strings are not safe if client charset is utf8
        # because we have to let oracle assume they're utf8 but they're not
        [ chr(161), 'upside down bang' ],
        [ chr(162), 'cent char'        ],
        [ chr(163), 'british pound'    ],
    ];
    [
        [ 'a',    'lowercase a' ],
        [ 'b',    'lowercase b' ],
        [ chr(3), 'control-C'   ],
        ( _nls_local_has_utf8() ) ? () : @$highbitset
    ];
}

my $tdata_hr = {
    narrow_char => {
        cols => $char_cols,
        rows => _narrow_data()
    },
    narrow_nchar => {
        cols => $nchar_cols,
        rows => _narrow_data()
    },
    wide_char => {
        cols => $char_cols,
        rows => $wide_data
    },
    wide_nchar => {
        cols => $nchar_cols,
        rows => $wide_data
    },
};

sub test_data {
    my ($which) = @_;
    my $test_data = $tdata_hr->{$which} or die;
    $test_data->{dump} = 'DUMP(%s)';
    if ( $ENV{DBD_ORACLE_TESTLOB} ) {    # XXX temp. needs reworking
                                         # Nvarchar -> Nclob and varchar -> clob
        $test_data->{cols}[0][1] =~ s/varchar.*/CLOB/;
        $test_data->{dump} = 'DUMP(DBMS_LOB.SUBSTR(%s))';
    }
    return $test_data;
}

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

    $p ||= {
            AutoCommit => 1,
            PrintError => 0,
            ora_envhp  => 0,    # force fresh environment (with current NLS env vars)
        };

    my $dsn    = oracle_test_dsn();
    my $dbuser = $ENV{ORACLE_USERID} || 'scott/tiger';
    my $dbpass = $ENV{ORACLE_PASSWD} || '';
    my $dbh    = DBI->connect( $dsn, $dbuser, $dbpass, $p );
    return $dbh

}

sub show_test_data {
    my ($tdata) = @_;
    my $rowsR   = $tdata->{rows};
    my $cnt     = 0;
    my $vcnt    = 0;
    foreach my $recR (@$rowsR) {
        $cnt++;
        my $v           = $$recR[0];
        my $byte_string = _byte_string($v);
        my $nice_string = nice_string($v);
        my $out = sprintf( "row: %3d: nice_string=%s byte_string=%s (%s, %s)\n",
            $cnt, $nice_string, $byte_string, $v, DBI::neat($v) );
        note($out);
    }
    return $cnt;
}

sub table {
    my $table = shift || 'dbd_ora__drop_me';
    $table .= ( $ENV{DBD_ORACLE_SEQ} || '' );
    die "Test table name '$table' is too long for Oracle < 12.1"
        if length $table > 30;
    # In Oracle 12.2 and above the maximum object name length is 128 bytes.
    # In Oracle 12.1 and below the maximum object name length is 30 bytes.
    return $table
}

sub force_drop_table {
    my $dbh = shift;
    return unless $dbh;
    my $tname = shift || table();
    local $dbh->{PrintError} = 0;
    if ($dbh->{Active}) {
        $dbh->do(qq{ DROP TABLE $tname PURGE })
    }
}

sub drop_table {
    my @args = @_;
    return if $ENV{'DBD_SKIP_TABLE_DROP'};
    force_drop_table( @args )
}

sub _insert_handle {
    my ( $dbh, $tcols ) = @_;
    my $table = table();
    my $sql   = "insert into $table ( idx, ";
    my $cnt   = 1;
    for my $col (@$tcols) {
        $sql .= $$col[0] . ', ';
        $cnt++;
    }
    $sql .= 'dt ) values( ' . '?, ' x $cnt . 'sysdate )';
    my $h = $dbh->prepare($sql);
    ok( $h, "prepared: $sql" );
    return $h;
}

sub insert_test_count {
    my ($tdata) = @_;
    my $rcnt    = @{ $tdata->{rows} };
    my $ccnt    = @{ $tdata->{cols} };
    return 1 + $rcnt * 2 + $rcnt * $ccnt;
}

sub insert_rows    #1 + rows*2 +rows*ncols tests
{
    my ( $dbh, $tdata, $csform ) = @_;
    my $trows = $tdata->{rows};
    my $tcols = $tdata->{cols};
    my $table = table();

    # local $dbh->{TraceLevel} = 4;
    my $sth = _insert_handle( $dbh, $tcols );

    my $cnt = 0;
    foreach my $rowR (@$trows) {
        my $colnum = 1;
        my $attrR = $csform ? { ora_csform => $csform } : {};
        ok( $sth->bind_param( $colnum++ , $cnt ), 'bind_param idx' );
        for ( my $i = 0 ; $i < @$rowR ; $i++ ) {
            my $note = 'withOUT attribute ora_csform';
            my $val  = $$rowR[$i];
            my $type = $$tcols[$i][1];

            #print "type=$type\n";
            my $attr = {};
            if ( $type =~ m/^nchar|^nvar|^nclob/i ) {
                $attr = $attrR;
                $note = $attr
                  && $csform ? "with attribute { ora_csform => $csform }" : q||;
            }
            ok(
                $sth->bind_param( $colnum++ , $val, $attr ),
                'bind_param ' . $$tcols[$i][0] . " $note"
            );
        }
        $cnt++;
        ok( $sth->execute, "insert row $cnt: $rowR->[-1]" );
    }
}

sub dump_table {
    my ( $dbh, @cols ) = @_;
    return;    # not needed now select_handle() includes a DUMP column
    my $table  = table();
    my $colstr = '';
    foreach my $col (@cols) {
        $colstr .= ', ' if $colstr;
        $colstr .= "dump($col)";
    }
    my $sql = "select $colstr from $table order by idx";
    print "dumping $table\nprepared: $sql\n";
    my $colnum = 0;
    my $data   = eval { $dbh->selectall_arrayref($sql) } || [];
    my $cnt    = 0;
    while ( my $aref = shift @$data ) {
        $cnt++;
        my $colnum = 0;
        for my $col (@cols) {
            print "row $cnt: ";
            print "$col=" . $$aref[$colnum] . "\n";
            $colnum++;
        }
    }
}

sub _select_handle    #1 test
{
    my ( $dbh, $tdata ) = @_;
    my $table = table();
    my $sql   = 'select ';
    for my $col ( @{ $tdata->{cols} } ) {
        $sql .= $$col[0] . ', ';
    }
    $sql .= sprintf "$tdata->{dump}, ", $tdata->{cols}[0][0];
    $sql .= "dt from $table order by idx";
    my $h = $dbh->prepare($sql);
    ok( $h, "prepared: $sql" );
    return $h;
}

sub select_test_count {
    my ($tdata) = @_;
    my $rcnt    = @{ $tdata->{rows} };
    my $ccnt    = @{ $tdata->{cols} };
    return 2 + $ccnt + $rcnt * $ccnt * 2;
}

sub select_rows    # 1 + numcols + rows * cols * 2
{
    my ( $dbh, $tdata, $csform ) = @_;
    my $table = table();
    my $trows = $tdata->{rows};
    my $tcols = $tdata->{cols};
    my $sth   = _select_handle( $dbh, $tdata )
      or do { fail(); return };
    my @data   = ();
    my $colnum = 0;
    foreach my $col (@$tcols) {
        ok(
            $sth->bind_col( $colnum + 1, \$data[$colnum] ),
            'bind column ' . $$tcols[$colnum][0]
        );
        $colnum++;
    }
    my $dumpcol = sprintf $tdata->{dump}, $tdata->{cols}[0][0];

#ok( $sth->bind_col( $colnum+1 ,\$data[$colnum] ),  "bind column DUMP(" .$tdata->{cols}[0][0] .")" );
    $sth->bind_col( $colnum + 1, \$data[$colnum] );
    my $cnt = 0;
    $sth->execute();
    while ( $sth->fetch() ) {
        my $row   = $cnt + 1;
        my $error = 0;
        my $i     = 0;
        for ( $i = 0 ; $i < @$tcols ; $i++ ) {
            my $res      = $data[$i];
            my $charname = $trows->[$cnt][1] || '';
            my $is_utf8  = utf8::is_utf8($res) ? ' (uft8)' : q||;
            my $description =
              "row $row: column: $tcols->[$i][0] $is_utf8 $charname";

            $error +=
              not cmp_ok_byte_nice( $res, $$trows[$cnt][$i], $description );

            #$sth->trace(0) if $cnt >= 3 ;
        }
        if ($error) {
            warn "#    row $row: $dumpcol = " . $data[$i] . "\n";
        }
        $cnt++;
    }

    #$sth->trace(0);
    my $trow_cnt = @$trows;
    cmp_ok( $cnt, '==', $trow_cnt, 'number of rows fetched' );
}

sub cmp_ok_byte_nice {
    my ( $got, $expected, $description ) = @_;
    my $ok1 = cmp_ok( _byte_string($got), 'eq', _byte_string($expected),
        "byte_string test of $description" );
    my $ok2 = cmp_ok( nice_string($got), 'eq', nice_string($expected),
        "nice_string test of $description" );
    return $ok1 && $ok2;
}

sub create_table {
    my ( $dbh, $tdata, $drop ) = @_;
    my $tcols = $tdata->{cols};
    my $table = table();
    my $sql   = "create table $table ( idx integer, ";
    foreach my $col (@$tcols) {
        $sql .= $$col[0] . ' ' . $$col[1] . ', ';
    }
    $sql .= ' dt date )';

    drop_table($dbh) if $drop;

    #$dbh->do(qq{ DROP TABLE $table PURGE }) if $drop;
    $dbh->do($sql);
    if ( $dbh->err && $dbh->err == 955 ) {
        $dbh->do(qq{ DROP TABLE $table PURGE });
        warn "Unexpectedly had to drop old test table '$table'\n"
          unless $dbh->err;
        $dbh->do($sql);
    }
    elsif ( $dbh->err ) {
        return;
    }
    else {
        #$sql =~ s/ \( */(\n\t/g;
        #$sql =~ s/, */,\n\t/g;
        note("$sql\n");
    }
    return $table;

    #    ok( not $dbh->err, "create table $table..." );
}

sub show_db_charsets {
    my ($dbh) = @_;
    my $out;
    my $ora_server_version = join '.',
      @{ $dbh->func('ora_server_version') || [] };
    my $paramsH = $dbh->ora_nls_parameters();
    $out =
      sprintf
"Database $ora_server_version CHAR set is %s (%s), NCHAR set is %s (%s)\n",
      $paramsH->{NLS_CHARACTERSET},
      db_ochar_is_utf($dbh) ? 'Unicode' : 'Non-Unicode',
      $paramsH->{NLS_NCHAR_CHARACTERSET},
      db_nchar_is_utf($dbh) ? 'Unicode' : 'Non-Unicode';
    note($out);
    my $ora_client_version = ORA_OCI();
    $out =
      sprintf
      "Client $ora_client_version NLS_LANG is '%s', NLS_NCHAR is '%s'\n",
      ora_env_var('NLS_LANG')  || '<unset>',
      ora_env_var('NLS_NCHAR') || '<unset>';
    note($out);
}

sub db_ochar_is_utf { return shift->ora_can_unicode & 2 }
sub db_nchar_is_utf { return shift->ora_can_unicode & 1 }

sub client_ochar_is_utf8 {
    my $NLS_LANG = ora_env_var('NLS_LANG') || q();
    $NLS_LANG =~ s/.*\.//;
    return $NLS_LANG =~ m/utf8/i;
}

sub client_nchar_is_utf8 {
    my $NLS_LANG = ora_env_var('NLS_LANG') || q();
    $NLS_LANG =~ s/.*\.//;
    my $NLS_NCHAR = ora_env_var('NLS_NCHAR') || $NLS_LANG;
    return $NLS_NCHAR =~ m/utf8/i;
}

sub _nls_local_has_utf8 {
    return client_ochar_is_utf8() || client_nchar_is_utf8();
}

sub set_nls_nchar {
    my ( $cset, $verbose ) = @_;
    if ( defined $cset ) {
        $ENV{NLS_NCHAR} = "$cset";
    }
    else {
        undef $ENV{NLS_NCHAR};    # XXX windows? (perhaps $ENV{NLS_NCHAR}=""?)
    }

    # Special treatment for environment variables under Cygwin -
    # see comments in dbdimp.c for details.
    DBD::Oracle::ora_cygwin_set_env( 'NLS_NCHAR', $ENV{NLS_NCHAR} || '' )
      if $^O eq 'cygwin';
    note(
        defined ora_env_var('NLS_NCHAR')
        ?    # defined?
          "set \$ENV{NLS_NCHAR}=$cset\n"
        : "set \$ENV{NLS_LANG}=undef\n"
      )      # XXX ?
      if defined $verbose;
}

sub set_nls_lang_charset {
    my ( $lang, $verbose ) = @_;

    $ENV{NLS_LANG} = $lang ? "AMERICAN_AMERICA.$lang" : q();

    note sprintf( q|set $ENV{NLS_LANG}='%s'|, $ENV{NLS_LANG} );

    # Special treatment for environment variables under Cygwin -
    # see comments in dbdimp.c for details.
    DBD::Oracle::ora_cygwin_set_env( 'NLS_LANG', $ENV{NLS_LANG} || '' )
      if $^O eq 'cygwin';
}

sub _byte_string {
    my $ret = join( '|', unpack( 'C*', $_[0] ) );
    return $ret;
}

sub nice_string {
    my @raw_chars = ( utf8::is_utf8( $_[0] ) )
      ? unpack( 'U*', $_[0] )     # unpack unicode characters
      : unpack( 'C*', $_[0] );    # not unicode, so unpack as bytes
    my @chars = map {
        $_ > 255
          ?                       # if wide character...
          sprintf( "\\x{%04X}", $_ )
          :                       # \x{...}
          chr($_) =~ /[[:cntrl:]]/
          ?                       # else if control character ...
          sprintf( "\\x%02X", $_ )
          :                       # \x..
          chr($_)                 # else as themselves
    } @raw_chars;

    for my $c (@chars) {
        if ( $c =~ m/\\x\{08(..)}/ ) {
            $c .= q|='| . chr( hex($1) ) . q(');
        }
    }
    my $ret = join( q||, @chars );

}

sub view_with_sqlplus {
    my ( $use_nls_lang, $tdata ) = @_;
    my $table   = table();
    my $tcols   = $tdata->{cols};
    my $sqlfile = 'sql.txt';
    my $cols    = 'idx,nch_col';
    open my $F, '>', $sqlfile or die "could open $sqlfile";
    print $F $ENV{ORACLE_USERID} . "\n";
    my $str = qq(
col idx form 99
col ch_col form a8
col nch_col form a16
select $cols from $table;
);
    print $F $str;
    print $F "exit;\n";
    close $F;

    my $nls = 'unset';
    $nls = ora_env_var('NLS_LANG') if ora_env_var('NLS_LANG');
    local $ENV{NLS_LANG} = '' if not $use_nls_lang;
    print "From sqlplus...$str\n  ...with NLS_LANG = $nls\n";
    system("sqlplus -s \@$sqlfile");
    unlink $sqlfile;
}

1;
