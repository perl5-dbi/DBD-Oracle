#!perl
# Author: Martin J. Evans
# This should be an exact copy of the same file in DBD::ODBC
# If you change this file please let me know.
package ExecuteArray;
use Test::More;
use Data::Dumper;
use DBI;
our $VERSION = '0.01';

my $table = 'PERL_DBD_execute_array'.($ENV{DBD_ORACLE_SEQ}||'');
my $table2 = 'PERL_DBD_execute_array2'.($ENV{DBD_ORACLE_SEQ}||'');
my @p1 = (1,2,3,4,5);
my @p2 = qw(one two three four five);
my $fetch_row = 0;
my @captured_error;                  # values captured in error handler

sub error_handler
{
    @captured_error = @_;
    note('***** error handler called *****');
    0;                          # pass errors on
}

sub new {
    my ($class, $dbh, $dbi_version) = @_;
    my $self = {};

    $dbh = setup($dbh, $dbi_version);
    $self->{_dbh} = $dbh;

    # find out how the driver supports row counts and parameter status
    $self->{_param_array_row_counts} = $dbh->get_info(153);
    # a return of 1 is SQL_PARC_BATCH which means:
    #   Individual row counts are available for each set of parameters. This is
    #   conceptually equivalent to the driver generating a batch of SQL
    #   statements, one for each parameter set in the array. Extended error
    #   information can be retrieved by using the SQL_PARAM_STATUS_PTR
    #   descriptor field.
    # a return of 2 is SQL_PARC_NO_BATCH which means:
    #   There is only one row count available, which is the cumulative row
    #   count resulting from the execution of the statement for the entire
    #   array of parameters. This is conceptually equivalent to treating
    #   the statement together with the complete parameter array as one
    #   atomic unit. Errors are handled the same as if one statement
    #   were executed.
    return bless ($self, $class);
}

sub dbh {
    my $self = shift;
    return $self->{_dbh};
}

sub setup {
    my ($dbh, $dbi_version) = @_;

    $dbh = enable_mars($dbh, $native);
    $dbh->{HandleError} = \&error_handler;
    if ($dbi_version) {
        $dbh->{odbc_disable_array_operations} = 1;
    }
    #$dbh->{ora_verbose} = 5;
    $dbh->{RaiseError} = 1;
    $dbh->{PrintError} = 0;
    $dbh->{ChopBlanks} = 1;
    $dbh->{AutoCommit} = 1;

    return $dbh;
}

sub create_table
{
    my ($self, $dbh) = @_;

    eval {
        $dbh->do(qq/create table $table (a integer not null primary key, b char(20))/);
    };
    if ($@) {
        diag("Failed to create test table $table - $@");
        return 0;
    }
    eval {
        $dbh->do(qq/create table $table2 (a integer not null primary key, b char(20))/);
    };
    if ($@) {
        diag("Failed to create test table $table2 - $@");
        return 0;
    }
    my $sth = $dbh->prepare(qq/insert into $table2 values(?,?)/);
    for (my $row = 0; $row < @p1; $row++) {
        $sth->execute($p1[$row], $p2[$row]);
    }
    1;
}

sub drop_table
{
    my ($self, $dbh) = @_;

    eval {
        local $dbh->{PrintError} = 0;
        local $dbh->{PrintWarn} = 0;
        $dbh->do(qq/DROP TABLE $table PURGE/);
        $dbh->do(qq/DROP TABLE $table2 PURGE/);
    };
    note("Table dropped");
}

# clear the named table of rows
sub clear_table
{
    $_[0]->do(qq/delete from $_[1]/);
}

# check $table contains the data in $c1, $c2 which are arrayrefs of values
sub check_data
{
    my ($dbh, $c1, $c2) = @_;

    my $data = $dbh->selectall_arrayref(qq/select * from $table order by a/);
    my $row = 0;
    foreach (@$data) {
        is($_->[0], $c1->[$row], "row $row p1 data");
        is($_->[1], $c2->[$row], "row $row p2 data");
        $row++;
    }
}

sub check_tuple_status
{
    my ($self, $tsts, $expected) = @_;

    note(Data::Dumper->Dump([$tsts], [qw(ArrayTupleStatus)]));

    BAIL_OUT('expected data must be specified')
        if (!$expected || (ref($expected) ne 'ARRAY'));

    is(ref($tsts), 'ARRAY', 'tuple status is an array') or return;
    if (!is(scalar(@$tsts), scalar(@$expected), 'status arrays same size')) {
        diag(Dumper($tsts));
        diag(Dumper($expected));
        return;
    }

    my $row = 0;
    foreach my $s (@$expected) {
        if (ref($s)) {
            unless ($self->{_param_array_row_counts} == 2) {
                is(ref($tsts->[$row]), 'ARRAY', 'array in array tuple status');
                is(scalar(@{$tsts->[$row]}), 3, '3 elements in array tuple status error');
            }
        } else {
            if ($tsts->[$row] == -1) {
                pass("row $row tuple status unknown");
            } else {
                is($tsts->[$row], $s, "row $row tuple status");
            }
        }
        $row++;
    }
    return;
}

# insert might return 'mas' which means the caller said the test
# required Multiple Active Statements and the driver appeared to not
# support MAS.
#
# ref is a hash ref:
#   error (0|1) whether we expect an error
#   raise (0|1) means set RaiseError to this
#   commit (0|1) do the inserts in a txn
#   tuple arrayref of what we expect in the tuple status
#      e.g., [1,1,1,1,[]]
#      where the empty [] signifies we expect an error for this row
#      where 1 signifies we the expect row count for this row
#   affected - the total number of rows affected for insert/update
#
sub insert
{
    my ($self, $dbh, $sth, $ref) = @_;

    die "need hashref arg" if (!$ref || (ref($ref) ne 'HASH'));
    note("insert " . join(", ", map {"$_ = ". DBI::neat($ref->{$_})} keys %$ref ));
    # DBD::Oracle supports MAS don't compensate for it not
    if ($ref->{requires_mas} && $dbh->{Driver}->{Name} eq 'Oracle') {
        delete $ref->{requires_mas};
    }
    @captured_error = ();

    if ($ref->{raise}) {
        $sth->{RaiseError} = 1;
    } else {
        $sth->{RaiseError} = 0;
    }

    my (@tuple_status, $sts, $total_affected);
    my $tuple_status_arg = {};
    $tuple_status_arg->{ArrayTupleStatus} = \@tuple_status unless $ref->{notuplestatus};

    $sts = 999999;              # to ensure it is overwritten
    $total_affected = 999998;
    if ($ref->{array_context}) {
        eval {
            if ($ref->{params}) {
                ($sts, $total_affected) =
                    $sth->execute_array($tuple_status_arg,
                                        @{$ref->{params}});
            } elsif ($ref->{fetch}) {
                ($sts, $total_affected) =
                    $sth->execute_array(
                        {%{$tuple_status_arg},
                         ArrayTupleFetch => $ref->{fetch}});
            } else {
                ($sts, $total_affected) =
                    $sth->execute_array($tuple_status_arg);
            }
        };
    } else {
        eval {
            if ($ref->{params}) {
                $sts =
                    $sth->execute_array($tuple_status_arg,
                                        @{$ref->{params}});
            } else {
                $sts =
                    $sth->execute_array($tuple_status_arg);
            }
        };
    }
    if ($ref->{error} && $ref->{raise}) {
        ok($@, 'error in execute_array eval');
    } else {
        if ($ref->{requires_mas} && $@) {
            diag("\nThis test died with $@");
            diag("It requires multiple active statement support in the driver and I cannot easily determine if your driver supports MAS. Ignoring the rest of this test.");
            foreach (@tuple_status) {
                if (ref($_)) {
                    diag(join(",", @$_));
                }
            }
            return 'mas';
        }
        ok(!$@, 'no error in execute_array eval') or note($@);
    }
    $dbh->commit if $ref->{commit};

    if (!$ref->{raise} || ($ref->{error} == 0)) {
        if (exists($ref->{sts})) {
            is($sts, $ref->{sts},
               "execute_array returned " . DBI::neat($sts) . " rows executed");
        }
        if (exists($ref->{affected}) && $ref->{array_context}) {
            is($total_affected, $ref->{affected},
               "total affected " . DBI::neat($total_affected))
        }
    }
    if ($ref->{raise}) {
        if ($ref->{error}) {
            ok(scalar(@captured_error) > 0, "error captured");
        } else {
            is(scalar(@captured_error), 0, "no error captured");
        }
    }
    if ($ref->{sts}) {
        is(scalar(@tuple_status), (($ref->{sts} eq '0E0') ? 0 : $ref->{sts}),
           "$ref->{sts} rows in tuple_status");
    }
    if ($ref->{tuple} && !exists($ref->{notuplestatus})) {
        $self->check_tuple_status(\@tuple_status, $ref->{tuple});
    }
    return;
}
# simple test on ensure execute_array with no errors:
# o checks returned status and affected is correct
# o checks ArrayTupleStatus is correct
# o checks no error is raised
# o checks rows are inserted
# o run twice with AutoCommit on/off
# o checks if less values are specified for one parameter the right number
#   of rows are still inserted and NULLs are placed in the missing rows
# checks binding via bind_param_array and adding params to execute_array
# checks binding no parameters at all
sub simple
{
    my ($self, $dbh, $ref) = @_;

    note('simple tests ' . join(", ", map {"$_ = $ref->{$_}"} keys %$ref ));

    note("  all param arrays the same size");
    foreach my $commit (1,0) {
        note("    Autocommit: $commit");
        clear_table($dbh, $table);
        $dbh->begin_work if !$commit;

        my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
        $sth->bind_param_array(1, \@p1);
        $sth->bind_param_array(2, \@p2);
        $self->insert($dbh, $sth,
                      { commit => !$commit, error => 0, sts => 5, affected => 5,
                        tuple => [1, 1, 1, 1, 1], %$ref});
        check_data($dbh, \@p1, \@p2);
    }

    note "  Not all param arrays the same size";
    clear_table($dbh, $table);
    my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);

    $sth->bind_param_array(1, \@p1);
    $sth->bind_param_array(2, [qw(one)]);
    $self->insert($dbh, $sth, {commit => 0, error => 0,
                               raise => 1, sts => 5, affected => 5,
                               tuple => [1, 1, 1, 1, 1], %$ref});
    check_data($dbh, \@p1, ['one', undef, undef, undef, undef]);

    note "  Not all param arrays the same size with bind on execute_array";
    clear_table($dbh, $table);
    $sth = $dbh->prepare(qq/insert into $table values(?,?)/);

    $self->insert($dbh, $sth, {commit => 0, error => 0,
                               raise => 1, sts => 5, affected => 5,
                               tuple => [1, 1, 1, 1, 1], %$ref,
                               params => [\@p1, [qw(one)]]});
    check_data($dbh, \@p1, ['one', undef, undef, undef, undef]);

    note "  no parameters";
    clear_table($dbh, $table);
    $sth = $dbh->prepare(qq/insert into $table values(?,?)/);

    $self->insert($dbh, $sth, {commit => 0, error => 0,
                               raise => 1, sts => '0E0', affected => 0,
                               tuple => [], %$ref,
                               params => [[], []]});
    check_data($dbh, \@p1, ['one', undef, undef, undef, undef]);
}

# error test to ensure correct behavior for execute_array when it errors:
# o execute_array of 5 inserts with last one failing
#  o check it raises an error
#  o check caught error is passed on from handler for eval
#  o check returned status and affected rows
#  o check ArrayTupleStatus
#  o check valid inserts are inserted
#  o execute_array of 5 inserts with 2nd last one failing
#  o check it raises an error
#  o check caught error is passed on from handler for eval
#  o check returned status and affected rows
#  o check ArrayTupleStatus
#  o check valid inserts are inserted
sub error
{
    my ($self, $dbh, $ref) = @_;

    die "need hashref arg" if (!$ref || (ref($ref) ne 'HASH'));

    note('error tests ' . join(", ", map {"$_ = $ref->{$_}"} keys %$ref ));
    {
        note("Last row in error");

        clear_table($dbh, $table);
        my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
        my @pe1 = @p1;
        $pe1[-1] = 1;
        $sth->bind_param_array(1, \@pe1);
        $sth->bind_param_array(2, \@p2);
        $self->insert($dbh, $sth, {commit => 0, error => 1, sts => undef,
                                   affected => undef, tuple => [1, 1, 1, 1, []],
                                   %$ref});
        check_data($dbh, [@pe1[0..4]], [@p2[0..4]]);
    }

    {
        note("2nd last row in error");
        clear_table($dbh, $table);
        my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
        my @pe1 = @p1;
        $pe1[-2] = 1;
        $sth->bind_param_array(1, \@pe1);
        $sth->bind_param_array(2, \@p2);
        $self->insert($dbh, $sth, {commit => 0, error => 1, sts => undef,
                                   affected => undef, tuple => [1, 1, 1, [], 1], %$ref});
        check_data($dbh, [@pe1[0..2],$pe1[4]], [@p2[0..2], $p2[4]]);
    }
}

sub fetch_sub
{
    note("fetch_sub $fetch_row");
    if ($fetch_row == @p1) {
        note('returning undef');
        $fetch_row = 0;
        return;
    }

    return [$p1[$fetch_row], $p2[$fetch_row++]];
}

# test insertion via execute_array and ArrayTupleFetch
sub row_wise
{
    my ($self, $dbh, $ref) = @_;

    note("row_size via execute_for_fetch");

    # Populate the first table via a ArrayTupleFetch which points to a sub
    # returning rows
    $fetch_row = 0;             # reset fetch_sub to start with first row
    clear_table($dbh, $table);
    my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
    $self->insert($dbh, $sth,
                  {commit => 0, error => 0, sts => 5, affected => 5,
                   tuple => [1, 1, 1, 1, 1], %$ref,
                   fetch => \&fetch_sub});

    # NOTE: The following test requires Multiple Active Statements. Although
    # I can find ODBC drivers which do this it is not easy (if at all possible)
    # to know if an ODBC driver can handle MAS or not. If it errors the
    # driver probably does not have MAS so the error is ignored and a
    # diagnostic is output. Exceptions are DBD::Oracle which definitely does
    # support MAS.
    # The data pushed into the first table is retrieved via ArrayTupleFetch
    # from the second table by passing an executed select statement handle into
    # execute_array.
    note("row_size via select");
    clear_table($dbh, $table);
    $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
    my $sth2 = $dbh->prepare(qq/select * from $table2/);
    # some drivers issue warnings when mas fails and this causes
    # Test::NoWarnings to output something when we already found
    # the test failed and captured it.
    # e.g., some ODBC drivers cannot do MAS and this test is then expected to
    # fail but we ignore the failure. Unfortunately in failing DBD::ODBC will
    # issue a warning in addition to the fail
    $sth->{Warn} = 0;
    $sth->{Warn} = 0;
    ok($sth2->execute, 'execute on second table') or diag($sth2->errstr);
    ok($sth2->{Executed}, 'second statement is in executed state');
    my $res = $self->insert($dbh, $sth,
           {commit => 0, error => 0, sts => 5, affected => 5,
            tuple => [1, 1, 1, 1, 1], %$ref,
            fetch => $sth2, requires_mas => 1});
    return if $res && $res eq 'mas'; # aborted , does not seem to support MAS
    check_data($dbh, \@p1, \@p2);
}

# test updates
# updates are special as you can update more rows than there are parameter rows
sub update
{
    my ($self, $dbh, $ref) = @_;

    note("update test");

    # populate the first table with the default 5 rows using a ArrayTupleFetch
    $fetch_row = 0;
    clear_table($dbh, $table);
    my $sth = $dbh->prepare(qq/insert into $table values(?,?)/);
    $self->insert($dbh, $sth,
                  {commit => 0, error => 0, sts => 5, affected => 5,
                   tuple => [1, 1, 1, 1, 1], %$ref,
                   fetch => \&fetch_sub});
    check_data($dbh, \@p1, \@p2);

    # update all rows b column to 'fred' checking rows affected is 5
    $sth = $dbh->prepare(qq/update $table set b = ? where a = ?/);
    # NOTE, this also checks you can pass a scalar to bind_param_array
    $sth->bind_param_array(1, 'fred');
    $sth->bind_param_array(2, \@p1);
    $self->insert($dbh, $sth,
                  {commit => 0, error => 0, sts => 5, affected => 5,
                   tuple => [1, 1, 1, 1, 1], %$ref});
    check_data($dbh, \@p1, [qw(fred fred fred fred fred)]);

    # update 4 rows column b to 'dave' checking rows affected is 4
    $sth = $dbh->prepare(qq/update $table set b = ? where a = ?/);
    # NOTE, this also checks you can pass a scalar to bind_param_array
    $sth->bind_param_array(1, 'dave');
    my @pe1 = @p1;
    $pe1[-1] = 10;              # non-existent row
    $sth->bind_param_array(2, \@pe1);
    $self->insert($dbh, $sth,
                  {commit => 0, error => 0, sts => 5, affected => 4,
                   tuple => [1, 1, 1, 1, '0E0'], %$ref});
    check_data($dbh, \@p1, [qw(dave dave dave dave fred)]);

    # now change all rows b column to 'pete' - this will change all 5
    # rows even though we have 2 rows of parameters so we can see if
    # the rows affected is > parameter rows
    $sth = $dbh->prepare(qq/update $table set b = ? where b like ?/);
    # NOTE, this also checks you can pass a scalar to bind_param_array
    $sth->bind_param_array(1, 'pete');
    $sth->bind_param_array(2, ['dave%', 'fred%']);
    $self->insert($dbh, $sth,
                  {commit => 0, error => 0, sts => 2, affected => 5,
                   tuple => [4, 1], %$ref});
    check_data($dbh, \@p1, [qw(pete pete pete pete pete)]);
}

sub enable_mars {
    my $dbh = shift;

    # this test uses multiple active statements
    # if we recognise the driver and it supports MAS enable it
    my $driver_name = $dbh->get_info(6) || '';
    if (($driver_name eq 'libessqlsrv.so') ||
            ($driver_name =~ /libsqlncli/)) {
        my $dsn = $ENV{DBI_DSN};
        if ($dsn !~ /^dbi:ODBC:DSN=/ && $dsn !~ /DRIVER=/i) {
            my @a = split(q/:/, $ENV{DBI_DSN});
            $dsn = join(q/:/, @a[0..($#a - 1)]) . ":DSN=" . $a[-1];
        }
        $dsn .= ";MARS_Connection=yes";
        $dbh->disconnect;
        $dbh = DBI->connect($dsn, $ENV{DBI_USER}, $ENV{DBI_PASS});
    }
    return $dbh;
}

1;
