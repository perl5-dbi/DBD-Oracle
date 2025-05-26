#!/usr/bin/env perl

use strict;
use warnings;
use Time::HiRes qw| usleep |;
use Test::More;
use Data::Dumper;

local $Data::Dumper::Indent = 1;
local $Data::Dumper::Terse  = 1;

$ENV{DBD_ORACLE_DUMP} = 0;

our $VERSION      = 0.1;
our $VERBOSE      = 0;
our $ORACLE_HOME  = $ENV{ORACLE_HOME};

my $TEST_START = Time::HiRes::time();

sub section
{
  my $msg = shift;
  note '+ --------------------------------------------- +';
  note " $msg";
  note '+ --------------------------------------------- +';
  return;
}

sub abort
{
  my $msg = shift;
  printf STDERR "\n";
  printf STDERR "# + --------------------------------------------- +\n";
  printf STDERR "#   %s\n", $msg;
  printf STDERR "# + --------------------------------------------- +\n";
  printf STDERR "\n";
  note sprintf 'Completed in %5.3fs', Time::HiRes::time() - $TEST_START;
  done_testing();
  exit 1;
}

## Noise hides real issues (if there are any)
local $SIG{__WARN__} = sub { warn $_[0] unless $_[0] =~ m/^Subroutine/xi };

PERL_NOTICE:
{
  note qx|perl -V|  if $VERBOSE;
}

sub bark_thread_count
{
  my $expected = shift || 2;
  my $proc = sprintf '/proc/%s/status', $$;
  if ( -f $proc && open my $_PROC, '<', $proc )
  {
    is $_, $expected, 'Expected thread count=' . $expected for map { ( split ' ' )[1] } grep { m=Threads= } <$_PROC>;
    $_PROC && ( $_PROC->close or warn $! )
  }
  return;
}

ORACLE_READY:
{
  section 'ORACLE - READY';
  bark_thread_count(1);
  my $dbh = Child::Queue->do_connect( { PrintError => 0 } ) or plan skip_all => "Unable to connect to oracle\n";
  if ( $dbh )
  {
    is $dbh->do(qq|ALTER SESSION SET NLS_DATE_FORMAT         = 'YYYY-MM-DD"T"HH24:MI:SS"Z"'|), '0E0', 'ALTER SESSION SET NLS_DATE_FORMAT';
    is $dbh->do(qq|ALTER SESSION SET NLS_TIMESTAMP_FORMAT    = 'YYYY-MM-DD"T"HH24:MI:SS"Z"'|), '0E0', 'ALTER SESSION SET NLS_TIMESTAMP_FORMAT';
    is $dbh->do(qq|ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT = 'YYYY-MM-DD"T"HH24:MI:SS"Z"'|), '0E0', 'ALTER SESSION SET NLS_TIMESTAMP_TZ_FORMAT';
    note Dumper( $dbh->selectall_arrayref(qq|SELECT SYSTIMESTAMP AT TIME ZONE 'UTC' FROM DUAL|));
  }
  $dbh = undef;
  # Not important but an indication SEGV is eminent
  # bark_thread_count(2);
}

QUEUE_BASICS:
{
  section 'QUEUE - BASICS';

  my $queue = Child::Queue->new( -DEPTH => 8 );

  is  $queue->depth,     8, 'Queue depth';
  is  $queue->size,      0, 'Queue size';
  is  $queue->running,   0, 'Queue running';
  ok  $queue->isIdle,       'Queue is idle';
  ok !$queue->isBusy,       'Queue is not busy';
  ok  $queue->hasSlots,     'Queue has slots';
  ok !$queue->isFull,       'Queue is not full';
  ok  $queue->enqueue(1),   'Enqueue 1';
  is  $queue->size,      1, 'Queue size';
  ok  $queue->enqueue(2),   'Enqueue 2';
  is  $queue->size,      2, 'Queue size';
  is  $queue->running,   0, 'Queue running';
  is  $queue->dequeue,   1, 'Dequeue 1';
  is  $queue->size,      1, 'Queue size';
  is  $queue->dequeue,   2, 'Dequeue 2';
  is  $queue->size,      0, 'Queue size';
  ok  $queue->isIdle,       'Queue is idle';
  ok !$queue->isBusy,       'Queue is not busy';
  ok  $queue->hasSlots,     'Queue has slots';
}


FORK_SEGV:
{
# last FORK_SEGV if 1;

  section 'FORK - SEGV';

  my $queue = Child::Queue->new( -DEPTH => 8 );
  my $jobs  = 80;

  is  $queue->depth,     8, 'Queue depth';
  is  $queue->size,      0, 'Queue size';
  is  $queue->running,   0, 'Queue running';
  ok  $queue->isIdle,       'Queue is idle';
  ok !$queue->isBusy,       'Queue is not busy';
  ok  $queue->hasSlots,     'Queue has slots';
  ok !$queue->isFull,       'Queue is not full';


  for my $i ( 1 .. $jobs )
  {
    my $job = sprintf 'JOB-%03d', $i;
    ok  $queue->enqueue($job),    'Enqueue ' . $job;
  }

  is  $queue->size,      $jobs,   'Queue size';
  is  $queue->running,   0,       'Queue running - zero';

  ok  $queue->startone($queue->dequeue), 'Start one child ->> 1';
  is  $queue->size,     $jobs-1,  'Queue size verified';
  ok  $queue->startone($queue->dequeue), 'Start one child ->> 2';
  is  $queue->size,     $jobs-2,  'Queue size verified';
  ok  $queue->run,                'queue->run - start -DEPTH children';
  is  $queue->running, 8,         'Queue running - 8 children started';
  ok  $queue->isFull,             'Queue is full';

  # note Dumper($Child::Queue::WORKSET);

  while ( $queue->isBusy )
  {
    usleep(50000);
    $queue->run if $queue->hasSlots && $queue->size;
    usleep(15000);
  }

  is  $queue->size,      0,       'Queue size - all jobs done';
  is  $queue->running,   0,       'Queue running - zero';
  ok  $queue->isIdle,       'Queue is idle';
  ok !$queue->isBusy,       'Queue is not busy';
  ok  $queue->hasSlots,     'Queue has slots';
  ok !$queue->isFull,       'Queue is not full';
}


note sprintf 'Completed in %5.3fs', Time::HiRes::time() - $TEST_START;
done_testing();


## Children QUEUE

package Child::Queue;

use strict;
use warnings;
use Data::Dumper;
use POSIX ":sys_wait_h";

use lib 't/lib';
use DBDOracleTestLib qw/ db_handle /;

our $VERSION;
our $VERBOSE;
our $QUEUE;
our $WORKSET;

sub _SIG_CHLD
{
  my $pid = waitpid(-1, WNOHANG);
  my $code = $? >> 8;
 
  return unless $pid > 0;

  if ( exists $WORKSET->{$pid} )
  {
    my $child = delete $WORKSET->{$pid};
    my $results = $child->finish( $code );
    printf "# Child %d finished with code %d\n", $pid, $results->{CODE};
    print Dumper($results);
  }
  else
  {
    printf "# Child %d finished but not in workset", $pid;
  }
}

BEGIN {
  $VERSION  = 0.1;
  $VERBOSE  = $main::VERBOSE || 0;
  $QUEUE    = [];
  $WORKSET  = {};  # PID => Child::Runner

  $SIG{CHLD} = \&_SIG_CHLD;
}

sub new
{
  my $self = shift;
  my $args = ref $_[0] ? shift : { @_ };
  return bless $args, $self
}

sub depth     { return $_[0]->{-DEPTH} }
sub isBusy    { return $_[0]->size > 0 || $_[0]->running > 0 }
sub isIdle    { return ! $_[0]->isBusy }
sub enqueue   { return push @ $QUEUE, pop }
sub dequeue   { return shift @ $QUEUE }
sub size      { return scalar @ $QUEUE }
sub running   { return scalar keys % $WORKSET }
sub isFull    { return $_[0]->running >= $_[0]->depth }
sub hasSlots  { return ! $_[0]->isFull }

sub do_connect
{
  shift if $_[0] && ( ref($_[0]) eq __PACKAGE__ || $_[0] eq __PACKAGE__ );
  return db_handle(@_);
}

sub startone
{
  my $self = shift;
  my $job  = shift;
  my $child = Child::Runner->new($job);

  ## Make sure it stays set????
  # $SIG{CHLD} = \&_SIG_CHLD;

  if ( ! defined $child->pid )
  {
    warn "Unable to start child for job: $job";
    return;
  }

  $WORKSET->{$child->pid} = $child;
}

sub run
{
  my $self = shift;

  while ( $self->hasSlots && $self->size )
  {
    $self->startone( $self->dequeue );

    # my $job   = shift @ $QUEUE;
    # my $child = Child::Runner->new($job);

    # $WORKSET->{$child->pid} = $child;
  }

  return $self->isFull;
}


package Child::Runner;

use strict;
use warnings;
use IPC::Open3 ();
use Symbol 'gensym';

sub new
{
  my $self = bless {}, shift;
  my $job  = $self->job(shift);
  my ( $in, $out, $err ) = (undef, undef, gensym);
  my $pid = IPC::Open3::open3( $in, $out, $err, $^X, 't/92-segv-fork.pl', $job );

  if ( ! defined $pid )
  {
    warn "Unable to fork: $!";
    return;
  }

  $in->close or warn $! if $in;
  $self->pid($pid);
  $self->out($out);
  $self->err($err);

  return $self;
}

sub finish
{
  my $self  = shift;
  my $code  = shift;
  my $job   = $self->job;
  my $pid   = $self->pid;
  my $out   = $self->out;
  my $err   = $self->err;
  my $results = { -JOB  => $job, -PID  => $pid, -OUT  => [], -ERR  => [] };

  if ( $self->pid )
  {
    my $O = $results->{-OUT};
    my $E = $results->{-ERR};

    while ( my $l = <$out> ) { chomp $l; push @ $O, $l }
    while ( my $l = <$err> ) { chomp $l; push @ $E, $l }

    close $out or warn "Unable to close out: $!";
    close $err or warn "Unable to close err: $!";

    # waitpid( $pid, 0 );
    # $results->{ CODE } = $? >> 8;
    $results->{ CODE } = $code;
  }

  return $results;
}

sub job     { return defined $_[1] ? $_[0]->{_JOB______} = $_[1] : $_[0]->{_JOB______} }
sub pid     { return defined $_[1] ? $_[0]->{_PID______} = $_[1] : $_[0]->{_PID______} }
sub out     { return defined $_[1] ? $_[0]->{_OUT______} = $_[1] : $_[0]->{_OUT______} }
sub err     { return defined $_[1] ? $_[0]->{_ERR______} = $_[1] : $_[0]->{_ERR______} }

1;

## vim: number expandtab tabstop=2 shiftwidth=2
## END
