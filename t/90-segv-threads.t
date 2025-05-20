#!/usr/bin/env perl

use 5.030003;
use strict;
use warnings;

BEGIN { eval 'use threads; use threads::shared 1.51;' }

use Test::More;
use Config;
use Time::HiRes qw| usleep |;
use Data::Dumper;

# Check if Perl is compiled with thread support
if (!$Config{useithreads}) {
  plan skip_all => "this $^O perl $] not configured to support iThreads";
  done_testing();
  exit 1
}

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

{
  DB::Queue->do_connect( { PrintError => 0 } ) or plan skip_all => "Unable to connect to oracle\n";
}

## Noise hides real issues (if there are any)
local $SIG{__WARN__} = sub { warn $_[0] unless $_[0] =~ m/^Subroutine/xi };

PERL_NOTICE:
{
  note qx|perl -V|  if $VERBOSE;
}

THREADS_ALONE:
{
  last THREADS_ALONE if 1;

  section 'Threads stress testing';

# is threads->tid,  0,  'main-thread identified';

  for ( 1 .. 2 )
  {
    {
      my $queue = DB::Queue->new;

      is ref $queue, 'DB::Queue', 'isa DB::Queue';

      ok  ! $queue->isEnabled,          '! q->isEnabled';
      ok    $queue->enable(5),          '  q->enable(X)';
      ok    $queue->isEnabled,          '  q->isEnabled';
      ok    $queue->disable,            '  q->disable';
    }

    ok    DB::Queue->new->isDisabled,  '  q->isDisabled';
  }
}

sub thread_worker { DB::Queue::_THREAD_WORKER(@_); }

THREADS_SEGV:
{
# last THREADS_SEGV if 1;

  section 'Threads + DB->ping stress testing';

  my $onemore;    ## to be the last but used only once
  my $do_onemore = 1;
  my $do_first   = 0; ## 1 = OKAY; 0 = SEGV

  ## <= 2 OKAY; > 2 SEGV!!! unless one $do_onemore is enabled to control disconnect order
  my $size = 3 - $do_onemore;

  sub finish_onemore
  {
    if ( $onemore && $onemore->{THRD} )
    {
    # note 'SNEEK IN ANOTHER (BEGIN)';
    # my ( $in, $ou ) = ( Thread::Queue->new, Thread::Queue->new );
    # my $thr = threads->create( \&thread_worker, $in, $ou );
    # $in->enqueue( DB::Msg::Ping->new );
    # threads->yield;
    # note 'SNEEK IN ANOTHER (END)';
    # usleep 200000;

      local $ENV{DBD_ORACLE_DUMP} = 1;

      note 'EXIT THE-ONE-MORE thread';
    # DBI->trace(6);
      my ( $Qin, $thr ) = ( $onemore->{Q_IN}, $onemore->{THRD} );
      $Qin->enqueue( DB::Msg::Exit->new );
      sleep 1;
      $thr->join;
      note 'EXIT THE-ONE-MORE thread (joined)';
    }

    $onemore = undef;

    return
  }

  for my $loop ( 1 .. 3 )
  {
    note "START LOOP $loop" if $VERBOSE;

    {
      my $queue = DB::Queue->new;

      is ref $queue, 'DB::Queue', 'isa DB::Queue';

      ok  ! $queue->isEnabled,          '! q->isEnabled';
      ok    $queue->enable($size),      '  q->enable(X)';
      ok    $queue->isEnabled,          '  q->isEnabled';
      ok  ! $queue->ping,               '  q->ping';

      while ( $queue->ping < $size ) { $queue->run; usleep 5000 }

      is    $queue->ping, $size,  '  ALL->connected';

      if ( $do_onemore && ! $onemore )
      {
        $onemore = {};

        my ( $Qin, $Qou ) = ( Thread::Queue->new, Thread::Queue->new );
        my $thr = threads->create( \&thread_worker, $Qin, $Qou );

        $onemore->{Q_IN} = $Qin;
        $onemore->{Q_OU} = $Qou;
        $onemore->{THRD} = $thr;

        $Qin->enqueue( DB::Msg::Ping->new );

        note '+ ------------------------------------------ +';
        note '  Ping->one-more (NEW)';
        note '+ ------------------------------------------ +';
      # sleep 4;
      }
    # else
    # {
    #   note '+ ------------------------------------------ +';
    #   note '  Ping->one-more (PRE-EXISTS)';
    #   note '+ ------------------------------------------ +';
    # # sleep 4;
    # }

      note "  END LOOP $loop" if $VERBOSE;
    # sleep 4;
    # note 'Manual Disable: ', $queue->disable;
      finish_onemore if $do_first;
    }

    ok( DB::Queue->new->isDisabled,  '  q->isDisabled (auto-cleanup DESTROY)' );
    note qx/ps -o rss,size,pid,cmd -p $$/ if $VERBOSE;
  }

  finish_onemore;
}

note sprintf 'Completed in %5.3fs', Time::HiRes::time() - $TEST_START;
done_testing();


## QUEUE

package DB::Queue;

use strict;
use warnings;
use threads::shared 1.51;
use Thread::Queue;
use Time::HiRes qw| usleep |;
use DBI;
use Test::More;
use Data::Dumper;

use lib 't/lib';
use DBDOracleTestLib qw/ db_handle /;

our $VERSION;
our $VERBOSE;
our $ENABLED;
our $TCOUNT;
our $QUEUE_IN;
our $QUEUE_OU;
our $STATUS;
our $THREADS;

our $ONETHR :shared;

BEGIN {
  $VERSION  = 0.1;
  $VERBOSE  = $main::VERBOSE || 0;
  $ONETHR   = 1;
  $ENABLED  = 0;
  $QUEUE_IN = [];
  $QUEUE_OU = [];
  $STATUS   = {};
  $THREADS  = [];

# DBI->trace(9);
}

sub CLONE {
  $ENABLED  = 0;
  $STATUS   = {};
  $THREADS  = [];
  $QUEUE_IN = [];
  $QUEUE_OU = [];
}

DESTROY { __PACKAGE__->disable; }
END     { __PACKAGE__->disable; }

sub new
{
  return bless {}, shift;
}

sub isEnabled
{
  return $ENABLED && $ENABLED > 0
}

sub isDisabled { return ! isEnabled() }

sub disable
{
  my $self = shift;

# printf "# %s->disable\n", threads->tid;

  if ( threads->tid == 0 && scalar @ $THREADS )
  {
  # printf "# DISABLE %s threads\n", scalar @ $THREADS;

    while ( scalar @ $THREADS )
    {
      my ( $qI, $qO ) = ( shift( @ $QUEUE_IN ), shift( @ $QUEUE_OU ));
      my $thr     = shift @ $THREADS;
      my $status  = delete $STATUS->{ $thr->tid };

      $qI && $qI->enqueue( DB::Msg::Exit->new );

      if ( $thr )
      {
        while ( ! $thr->is_joinable ) { usleep( 20000 ); }
        note 'join ', $thr->tid if $VERBOSE;
        $thr->join;
      }

      threads->yield;
    }

    $ENABLED = 0;
  }

  return $self->isDisabled;
}

sub enable
{
  my $self = shift;
  my $threads = shift;

  if ( $threads && $self->isDisabled )
  {
    for my $cnt ( 1 .. $threads )
    {
      my ( $Qin, $Qou ) = ( Thread::Queue->new, Thread::Queue->new );
      push @ $QUEUE_IN, $Qin;
      push @ $QUEUE_OU, $Qou;

      my $thr = threads->create( \&_THREAD_WORKER, $Qin, $Qou );
      push @ $THREADS, $thr;
      $STATUS->{ $thr->tid } = 0;

      $ENABLED++;
    }
  }

  return $self->isEnabled;
}

sub ping
{
  my $self = shift;
  my $conn = 0;

  for my $queue ( @ $QUEUE_IN )
  {
    $queue->enqueue( DB::Msg::Ping->new );
  }

  for my $state ( values % $STATUS ) { $state && $conn++ }

  return $conn;
}

sub run
{
  my $self = shift;
  my $msg;

  for my $queue ( @ $QUEUE_OU )
  {
    while ( $msg = $queue->dequeue_nb )
    {
      if ( $msg->isState )
      {
        $STATUS->{ $msg->tid } = $msg->isConnected;
        next;
      }

      warn 'unexpected: ' . ref $msg;
    }
  }

  return;
}

QUEUE_BACKEND:
{
  my $tid;
  my $queue_in;
  my $queue_ou;
  my $dbh;

  sub _THREAD_WORKER
  {
    $tid = threads->tid;
    $queue_in = shift;
    $queue_ou = shift;

  # printf "# %2d IN: %s\n", $tid, ref $queue_in;
  # printf "# %2d OU: %s\n", $tid, ref $queue_ou;

    BUSY:
    while (1)
    {
      my $msg;

      while ( defined( $msg = $queue_in->dequeue_nb ))
      {
        ## CASE - PING
        if ( $msg->isPing )
        {
        # printf "# tid-%s PING\n", $tid;
          _connect();
        # $queue_ou->enqueue( DB::Msg::Ping::ACK->new( $dbh && $dbh->ping ));
          $queue_ou->enqueue( DB::Msg::Ping::ACK->new( $dbh ? 1 : 0 ));
          next;
        }

        ## CASE - EXIT
        if ( $msg->isExit )
        {
          _disconnect();
        # $queue_ou->enqueue( DB::Msg::Ping::ACK->new( 0 ));
          last BUSY;
        }

        printf STDERR "# Unexpected %s\n", ref $msg;
      }

      usleep 50000;
    }

  # printf "# tid-%s EXIT\n", $tid;

    return 1;
  }

  sub do_connect
  {
    shift if $_[0] && ( ref($_[0]) eq __PACKAGE__ || $_[0] eq __PACKAGE__ );
    return db_handle(@_);
  }

  sub _connect
  {
    if ( ! $dbh )
    {
      lock $ONETHR;
      printf "# CONNECT-ENTER %d\n", $tid if $VERBOSE;
      $dbh = do_connect();
      printf "# CONNECT-EXIT  %d\n", $tid if $VERBOSE;
    # threads->yield;
    # usleep 250000;
    }

  # threads->yield;

    return;
  }

  sub _disconnect
  {
    if ( $dbh )
    {
      lock $ONETHR;
      printf "# DISCONNECT-ENTER %d\n", $tid  if $VERBOSE;
      $dbh->disconnect;
      $dbh = undef;
      printf "# DISCONNECT-EXIT  %d\n", $tid  if $VERBOSE;
    # threads->yield;
    # usleep 250000;
    }

  # threads->yield;

    return;
  }
}


package DB::Msg;

use strict;
use warnings;

sub new { return bless {}, shift }
sub isExit  { return 0 }
sub isPing  { return 0 }
sub isState { return 0 }

package DB::Msg::Exit;

use strict;
use warnings;

our @ISA;
BEGIN { push @ISA, 'DB::Msg' }

sub new { return (shift)->SUPER::new }
sub isExit  { return 1 }
sub isPing  { return 0 }
sub isState { return 0 }


package DB::Msg::Ping;

our @ISA;
BEGIN { push @ISA, 'DB::Msg' }

use strict;
use warnings;

sub new { return (shift)->SUPER::new }
sub isExit  { return 0 }
sub isPing  { return 1 }
sub isState { return 0 }

package DB::Msg::Ping::ACK;

use strict;
use warnings;

our @ISA;
BEGIN { push @ISA, 'DB::Msg' }

sub new
{
  my $self = (shift)->SUPER::new;
  $self->{TID} = threads->tid;
  $self->{CONNECTED} = shift;
  return $self;
}

sub isExit  { return 0 }
sub isPing  { return 0 }
sub isState { return 1 }

sub tid         { return $_[0]->{TID} }
sub isConnected { return $_[0]->{CONNECTED} }

## vim: number expandtab tabstop=2 shiftwidth=2
## END
