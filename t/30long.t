#!perl

use strict;
use warnings;

use lib 't/lib';
use DBDOracleTestLib qw/ table drop_table oracle_test_dsn db_handle
                        show_db_charsets long_test_cols create_table
                        cmp_ok_byte_nice db_ochar_is_utf client_ochar_is_utf8
                        nice_string db_nchar_is_utf client_nchar_is_utf8
/;

use DBI;
use DBD::Oracle qw(:ora_types SQLCS_NCHAR SQLCS_IMPLICIT ORA_OCI);
use Test::More;

*BAILOUT = sub { die "@_\n" }
  unless defined &BAILOUT;

my @test_sets = (
    [ 'LONG',     0,           0 ],
    [ 'LONG RAW', ORA_LONGRAW, 0 ],
    [ 'CLOB',     ORA_CLOB,    0 ],
    [ 'BLOB',     ORA_BLOB,    0 ],
);
push @test_sets, [ 'NCLOB', ORA_CLOB, 0 ]
  unless ORA_OCI() < 9.0 or $ENV{DBD_ALL_TESTS};

my $table = table();
my $use_utf8_data;    # set per test_set below
my %warnings;

my @skip_unicode;
push @skip_unicode, 'Perl < 5.6 ' if $] < 5.006;
push @skip_unicode, 'Oracle client < 9.0 '
  if ORA_OCI() < 9.0 and !$ENV{DBD_ALL_TESTS};

# Set size of test data (in 10KB units)
#        Minimum value 3 (else tests fail because of assumptions)
#        Normal  value 8 (to test old 64KB threshold well)
my $sz = 8;

my ( $p1, $p2, $tmp, @tmp );

my $dbh = db_handle( { PrintError => 0 } )
 or plan skip_all => 'Unable to connect to Oracle';

my $tests_per_set = 97;

my $ora_server_version = $dbh->func('ora_server_version');

show_db_charsets($dbh);

foreach (@test_sets) {
    my ( $type_name, $type_num, $test_no_type ) = @$_;
    subtest $type_name => sub {
        plan tests => $tests_per_set;
        $use_utf8_data = use_utf8_data( $dbh, $type_name );
        note(
            qq(
        =========================================================================
        Running long test for $type_name ($type_num) use_utf8_data=$use_utf8_data
    )
        );
        run_long_tests( $dbh, $type_name, $type_num );
        run_long_tests( $dbh, $type_name, 0 ) if $test_no_type;
      }
}

done_testing();

### END OF TESTS, ONLY FUNCTIONS BELOW ###

END {
    eval{ drop_table($dbh) }
}

sub use_utf8_data {
    my ( $dbh, $type_name ) = @_;

    return 0
      unless ( $type_name =~ m/^CLOB/i
        and db_ochar_is_utf($dbh) && client_ochar_is_utf8() )
      or ( $type_name =~ m/^NCLOB/i
        and db_nchar_is_utf($dbh) && client_nchar_is_utf8() );

    return 1 unless @skip_unicode;

    warn "Skipping Unicode data tests: @skip_unicode\n"
      if !$warnings{use_utf8_data}++;
}

sub run_long_tests {
    my ( $dbh, $type_name, $type_num ) = @_;
    my ($sth);
    my $append_len;
  SKIP:
    {    #it all

        # relationships between these lengths are important # e.g.
        my %long_data;
        my @long_data;
        $long_data[2] =
          ( '2bcdefabcd' x 1024 ) x ( $sz - 1 );  # 70KB  > 64KB && < long_data1
        $long_data[1] =
          ( '1234567890' x 1024 ) x ($sz);        # 80KB >> 64KB && > long_data2
        $long_data[0] = ( "0\177x\0X" x 2048 ) x (1);    # 10KB  < 64KB

        if ($use_utf8_data) {    # make $long_data0 be UTF8
            my $utf_x = "0\x{263A}xyX";    #lab: the ubiquitous smiley face
            $long_data[0] = ( $utf_x x 2048 ) x (1);    # 10KB  < 64KB
            if ( length( $long_data[0] ) > 10240 ) {
                note
                  "known bug in perl5.6.0 utf8 support, applying workaround\n";
                my $utf_z = "0\x{263A}xyZ";
                $long_data[0] = $utf_z;
                $long_data[0] .= $utf_z foreach ( 1 .. 2047 );
            }
            if ( $type_name eq 'BLOB' ) {

                # convert string from utf-8 to byte encoding XXX
                $long_data[0] = pack 'C*', ( unpack 'C*', $long_data[0] );
            }
        }
        my $be_utf8 =
            ( $type_name eq 'BLOB' )  ? 0
          : ( $type_name eq 'CLOB' )  ? client_ochar_is_utf8()
          : ( $type_name eq 'NCLOB' ) ? client_nchar_is_utf8()
          :                             0;    # XXX umm, what about LONGs?

        # special hack for long_data[0] since RAW types need pairs of HEX
        $long_data[0] = '00FF' x ( length( $long_data[0] ) / 2 )
          if $type_name =~ /RAW/i;

        my @len_data = map { length($_) } @long_data;

        # warn if some of the key aspects of the data sizing are tampered with
        warn "long_data[0] is > 64KB: $len_data[0]\n"
          if $len_data[0] > 65535;
        warn "long_data[1] is < 64KB: $len_data[1]\n"
          if $len_data[1] < 65535;
        warn
"long_data[2] is not smaller than $long_data[1] ($len_data[2] > $len_data[1])\n"
          if $len_data[2] >= $len_data[1];

        my $tdata = {
            cols => long_test_cols($type_name),
            rows => []
        };

        skip "Unable to create test table for '$type_name' data ($DBI::err).",
          $tests_per_set
          if ( !create_table( $dbh, $tdata, 1 ) );

        # typically OCI 8 client talking to Oracle 7 database

        note(" --- insert some $type_name data (ora_type $type_num)\n");
        my $sqlstr = "insert into $table values (?, ?, SYSDATE)";
        ok( $sth = $dbh->prepare($sqlstr), "prepare: $sqlstr" );
        my $bind_attr = { ora_type => $type_num };

        # The explicit SQLCS_IMPLICIT is needed in some odd cases
        $bind_attr->{ora_csform} =
          ( $type_name =~ /^NCLOB/ ) ? SQLCS_NCHAR : SQLCS_IMPLICIT;

        $sth->bind_param( 2, undef, $bind_attr )
          or die "$type_name: $DBI::errstr"
          if $type_num;

        ok( $sth->execute( 40, $long_data{40} = $long_data[0] ),
            'insert long data 40' );
        ok( $sth->execute( 41, $long_data{41} = $long_data[1] ),
            'insert long data 41' );
        ok( $sth->execute( 42, $long_data{42} = $long_data[2] ),
            'insert long data 42' );
        ok( $sth->execute( 43, $long_data{43} = undef ),
            'insert long data undef 43' );    # NULL

        array_test($dbh);

        note(
" --- fetch $type_name data back again -- truncated - LongTruncOk == 1\n"
        );
        $dbh->{LongReadLen} = 20;
        $dbh->{LongTruncOk} = 1;
        note(
            "LongReadLen $dbh->{LongReadLen}, LongTruncOk $dbh->{LongTruncOk}\n"
        );

        # This behaviour isn't specified anywhere, sigh:
        my $out_len = $dbh->{LongReadLen};
        $out_len *= 2 if ( $type_name =~ /RAW/i );

        $sqlstr = "select * from $table order by idx";
        ok( $sth = $dbh->prepare($sqlstr), "prepare: $sqlstr" );
        $sth->trace(0);
        ok( $sth->execute, "execute: $sqlstr" );
        ok( $tmp = $sth->fetchall_arrayref, "fetch_arrayref for $sqlstr" );
        $sth->trace(0);
      SKIP: {
            if ( $DBI::err && $DBI::errstr =~ m/ORA-01801:/ ) {

                # ORA-01801: date format is too long for internal buffer
                skip
" If you're using Oracle <= 8.1.7 then this error is probably\n"
                  . " due to an Oracle bug and not a DBD::Oracle problem.\n", 5;
            }
            cmp_ok( @$tmp, '==', 4, 'four rows' );

            for my $i ( 0 .. 2 ) {
                my $v = $tmp->[$i][1];
                cmp_ok_byte_nice(
                    $v,
                    substr( $long_data[$i], 0, $out_len ),
                    "truncated to LongReadLen $out_len"
                );
                if ( $type_name eq 'BLOB' ) {
                    ok( !utf8::is_utf8($v), 'BLOB non-UTF8' );
                }
                else {
                    # allow result to have UTF8 flag even if source data didn't
                    # (not ideal but would need better test data)
                    ok( utf8::is_utf8($v) >= utf8::is_utf8( $long_data[$i] ),
                        "$type_name UTF8 setting" );
                }
            }
            ok( !defined $tmp->[3][1], 'last row undefined' )
              ;    # NULL # known bug in DBD::Oracle <= 1.13
        }

        note(
" --- fetch $type_name data back again -- truncated - LongTruncOk == 0\n"
        );

        $dbh->{LongReadLen} =
          $len_data[1] - 10;    # so $long_data[0] fits but long_data[1] doesn't
        $dbh->{LongReadLen} /= 2 if $type_name =~ m/RAW/i;

        $dbh->{LongTruncOk} = 0;

        note
          "LongReadLen $dbh->{LongReadLen}, LongTruncOk $dbh->{LongTruncOk}\n";

        $sqlstr = "select * from $table order by idx";

        ok $sth = $dbh->prepare($sqlstr), "prepare $sqlstr";
        ok $sth->execute, "execute $sqlstr";
        ok $tmp = $sth->fetchrow_arrayref, "fetchrow_arrayref $sqlstr";
        ok $tmp->[1] eq $long_data[0], "length tmp->[1] " . length( $tmp->[1] );

        {
            local $sth->{PrintError} = 0;
            ok(
                !defined $sth->fetchrow_arrayref,
                'truncation error not triggered '
                  . "(LongReadLen $dbh->{LongReadLen}, data "
                  . length( $tmp->[1] || 0 ) . ")"
            );
            $tmp = $sth->err || 0;
            ok(
                ( $tmp == 1406 || $tmp == 24345 ),
                "tmp==1406 || tmp==24345 tmp actually=$tmp"
            );
        }
        $sth->finish;

        note(
" --- fetch $type_name data back again -- complete - LongTruncOk == 0\n"
        );
        $dbh->{LongReadLen} = $len_data[1] + 1000;
        $dbh->{LongTruncOk} = 0;
        note(
            "LongReadLen $dbh->{LongReadLen}, LongTruncOk $dbh->{LongTruncOk}\n"
        );

        $sqlstr = "select * from $table order by idx";
        ok( $sth = $dbh->prepare($sqlstr), "prepare: $sqlstr" );
        ok( $sth->execute, "execute $sqlstr" );

        for my $i ( 0 .. 2 ) {
            my $result = $sth->fetchrow_arrayref;
            ok $result, "fetchrow_arrayref $sqlstr";
            is $result->[1] => $long_data[$i],
              cdif( $result->[1], $long_data[$i],
                "Len " . length( $result->[1] ) );
        }
        $sth->finish;

      SKIP: {
            skip 'blob_read tests for LONGs - not currently supported', 15
              if $type_name =~ /LONG/i;

            #$dbh->trace(4);
            note(" --- fetch $type_name data back again -- via blob_read\n\n");

            $dbh->{LongReadLen} = 1024 * 90;
            $dbh->{LongTruncOk} = 1;
            $sqlstr = "select idx, lng, dt from $table order by idx";
            ok( $sth = $dbh->prepare($sqlstr), "prepare $sqlstr" );
            ok( $sth->execute, "execute $sqlstr" );

            note("fetch via fetchrow_arrayref\n");
            ok( $tmp = $sth->fetchrow_arrayref,
                "fetchrow_arrayref 1: $sqlstr" );
            cmp_ok_byte_nice( $tmp->[1], $long_data[0],
                "truncated to LongReadLen $out_len" );

            note("read via blob_read_all\n");
            cmp_ok(
                blob_read_all( $sth, 1, \$p1, 4096 ),
                '==',
                length( $long_data[0] ),
                "blob_read_all = length(\$long_data[0])"
            );
            ok( $p1 eq $long_data[0], cdif( $p1, $long_data[0] ) );
            $sth->trace(0);

            ok( $tmp = $sth->fetchrow_arrayref,
                "fetchrow_arrayref 2: $sqlstr" );
            cmp_ok(
                blob_read_all( $sth, 1, \$p1, 12345 ),
                '==',
                length( $long_data[1] ),
                "blob_read_all = length(long_data[1])"
            );
            ok( $p1 eq $long_data[1], cdif( $p1, $long_data[1] ) );

            ok( $tmp = $sth->fetchrow_arrayref,
                "fetchrow_arrayref 3: $sqlstr" );
            my $len = blob_read_all( $sth, 1, \$p1, 34567 );

            cmp_ok(
                $len, '==',
                length( $long_data[2] ),
                "length of long_data[2] = $len"
            );
            cmp_ok_byte_nice( $p1, $long_data[2], "3rd row via blob_read_all" );

            note(   "result is "
                  . ( utf8::is_utf8($p1) ? "UTF8" : "non-UTF8" )
                  . "\n" );

            if ($be_utf8) {
                ok( utf8::is_utf8($p1), 'result should be utf8' );
            }
            else {
                ok( !utf8::is_utf8($p1), 'result should not be utf8' );
            }
        }    #skip

      SKIP: {
            skip( "ora_auto_lob tests for $type_name" . "s - not supported",
                49 )
              unless ( $type_name =~ /LOB/i );

            note(" --- testing ora_auto_lob to access $type_name LobLocator\n\n"
            );
            my $data_fmt = '%03d foo!';

            $sqlstr = qq{
                    SELECT lng, idx FROM $table ORDER BY idx
                    FOR UPDATE -- needed so lob locator is writable
                };
            my $ll_sth = $dbh->prepare( $sqlstr, { ora_auto_lob => 0 } )
              ;    # 0: get lob locator instead of lob contents

            ok $ll_sth , "prepare $sqlstr";

            ok $ll_sth->execute, "execute $sqlstr";

            while ( my ( $lob_locator, $idx ) = $ll_sth->fetchrow_array ) {
                note( "$idx: " . DBI::neat($lob_locator) . "\n" );
                last if !defined($lob_locator) && $idx == 43;

                ok $lob_locator, '$lob_locator is true';
                is ref $lob_locator => 'OCILobLocatorPtr',
                  '$lob_locator is a OCILobLocatorPtr';
                ok(
                    ( ref $lob_locator and $$lob_locator ),
                    '$lob_locator deref ptr is true'
                );

                # check ora_lob_chunk_size:
                my $chunk_size =
                  $dbh->func( $lob_locator, 'ora_lob_chunk_size' );
                ok !$DBI::err, 'DBI::errstr';

                my $data = sprintf $data_fmt, $idx;    #create a little data

                note(   'length of data to be written at offset 1: '
                      . length($data)
                      . "\n" );
                ok( $dbh->func( $lob_locator, 1, $data, 'ora_lob_write' ),
                    'ora_lob_write' );
            }

            is( $ll_sth->rows, 4 );

            note(
" --- round again to check contents after $type_name write updates...\n"
            );
            ok( $ll_sth->execute, "execute (again 1) $sqlstr" );
            while ( my ( $lob_locator, $idx ) = $ll_sth->fetchrow_array ) {
                note( "$idx locator: " . DBI::neat($lob_locator) . "\n" );
                next if !defined($lob_locator) && $idx == 43;
                diag("DBI::errstr=$DBI::errstr\n") if $DBI::err;

                my $content = $dbh->func( $lob_locator, 1, 20, 'ora_lob_read' );
                diag("DBI::errstr=$DBI::errstr\n") if $DBI::err;
                ok( $content, 'content is true' );
                note( "$idx content: " . nice_string($content) . "\n" )
                  ;    #.DBI::neat($content)."\n";
                cmp_ok( length($content), '==', 20, 'lenth(content)' );

                # but prefix has been overwritten:
                my $data = sprintf $data_fmt, $idx;
                ok( substr( $content, 0, length($data) ) eq $data,
                    'length(content)=length(data)' );

                # ora_lob_length agrees:
                my $len = $dbh->func( $lob_locator, 'ora_lob_length' );
                ok( !$DBI::err, 'DBI::errstr' );
                cmp_ok(
                    $len, '==',
                    length( $long_data{$idx} ),
                    'length(long_data{idx}) = length of locator data'
                );

                # now trim the length
                $dbh->func( $lob_locator, $idx, 'ora_lob_trim' );
                ok( !$DBI::err, 'DBI::errstr' );

                # and append some text
              SKIP: {
                    $append_len = 0;
                    skip(
'ora_lob_append() not reliable in Oracle 8 (Oracle bug #886191)',
                        1
                      )
                      if ORA_OCI() < 9
                      or $ora_server_version->[0] < 9;

                    my $append_data = '12345';
                    $append_len = length($append_data);
                    $dbh->func( $lob_locator, $append_data, 'ora_lob_append' );
                    ok( !$DBI::err, 'ora_lob_append DBI::errstr' );

                    # XXX ought to test data was actually appended
                }

            }    #while fetchrow
            is( $ll_sth->rows, 4 );

            note(" --- round again to check the $type_name length...\n");
            ok( $ll_sth->execute, "execute (again 2) $sqlstr" );
            while ( my ( $lob_locator, $idx ) = $ll_sth->fetchrow_array ) {
                note( "$idx locator: " . DBI::neat($lob_locator) . "\n" );
                next if !defined($lob_locator) && $idx == 43;
                my $len = $dbh->func( $lob_locator, 'ora_lob_length' );

                #lab: possible logic error here w/resp. to len
                ok( !$DBI::err, 'DBI::errstr' );
                cmp_ok( $len, '==', $idx + $append_len, 'len == idx+5' );
            }
            is( $ll_sth->rows, 4 );

        }    #skip for LONG types

    }    #skip it all (tests_per_set)

    $sth->finish if $sth;
    drop_table($dbh);

}    # end of run_long_tests

sub array_test {
    my ($dbh) = @_;
    return 0;    # FIXME disabled
    eval {
        $dbh->{RaiseError} = 1;
        $dbh->trace(0);
        my $sth = $dbh->prepare(
            qq{
           UPDATE $table set idx=idx+1 RETURNING idx INTO ?
        }
        );
        my ( $a, $b );
        $a = [];
        $sth->bind_param_inout( 1, \$a, 2 );
        $sth->execute;
        note("a=$a\n");
        note("a=@$a\n");
    };
    die "RETURNING array: $@";
}

sub print_substrs {
    my ( $dbh, $len ) = @_;
    my $tsql = "select substr(lng,1,$len),idx from $table order by idx";
    note("-- prepare: $tsql\n");
    my $tsth = $dbh->prepare($tsql);
    $tsth->execute();
    while ( my ( $d, $i ) = $tsth->fetchrow_array() ) {
        last if not defined $d;
        note("$i: $d\n");
    }
}

sub print_lengths {
    my ($dbh) = @_;
    my $tsql = "select length(lng),idx from $table order by idx";
    note("-- prepare: $tsql\n");
    my $tsth = $dbh->prepare($tsql);
    $tsth->execute();
    while ( my ( $l, $i ) = $tsth->fetchrow_array() ) {
        last if not defined $l;
        note("$i: $l\n");
    }
}

sub blob_read_all {
    my ( $sth, $field_idx, $blob_ref, $lump ) = @_;

    $lump ||= 4096;    # use benchmarks to get best value for you
    my $offset = 0;
    my @frags;
    while (1) {
        my $frag = $sth->blob_read( $field_idx, $offset, $lump );
        last unless defined $frag;
        my $len = length $frag;
        last unless $len;
        push @frags, $frag;
        $offset += $len;

        #print "blob_read_all: offset $offset, len $len\n";
    }
    $$blob_ref = join q||, @frags;
    return length($$blob_ref);
}

sub unc {
    my @str = @_;
    foreach (@str) { s/([\000-\037\177-\377])/ sprintf "\\%03o", ord($_) /eg; }
    return join q(), @str unless wantarray;
    return @str;
}

sub cdif {
    my ( $s1, $s2, $msg ) = @_;
    $msg = ($msg) ? ", $msg" : q();
    my ( $l1, $l2 ) = ( length($s1), length($s2) );
    return "Strings are identical$msg" if $s1 eq $s2;
    return
      "Strings are of different lengths ($l1 vs $l2)$msg" # check substr matches?
      if $l1 != $l2;
    my $i;
    for ( $i = 0 ; $i < $l1 ; ++$i ) {
        my ( $c1, $c2 ) =
          ( ord( substr( $s1, $i, 1 ) ), ord( substr( $s2, $i, 1 ) ) );
        next if $c1 == $c2;
        return sprintf "Strings differ at position %d (\\%03o vs \\%03o)$msg",
          $i, $c1, $c2;
    }
    return "(cdif error $l1/$l2/$i)";
}

__END__
