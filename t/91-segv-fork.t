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

{
  DB::Fork->do_connect( { PrintError => 0 } ) or plan skip_all => "Unable to connect to oracle\n";
}

## Noise hides real issues (if there are any)
local $SIG{__WARN__} = sub { warn $_[0] unless $_[0] =~ m/^Subroutine/xi };

PERL_NOTICE:
{
  note qx|perl -V|  if $VERBOSE;
}

FORK_BASICS:
{
# last FORK_BASICS if 1;

  section 'FORK - BASICS';

  ok $$,  'PARENT PID=' . $$;

  my $parent_dbh;
  my $forker    = DB::Fork->new;
  my $children  = 4;
  my $passes    = 5;

  $parent_dbh = DB::Fork->do_connect;

  is ref $forker, 'DB::Fork',                 ' f isa DB::Fork';
  ok  $forker->isParent,                      ' f->isParent';

  for my $pass ( 1 .. $passes )
  {
    ok $pass, 'PASS: ' . $pass;
    ok  $forker->isDisabled,                    ' f->isDisabled';
    is  $forker->enable($children), $children,  ' f->enable(N)';
    ok  $forker->isEnabled,                     ' f->isEnabled';

    usleep 75000;
    is  $forker->ping, $children,               ' f->ping';
    usleep 75000;
    ok  $forker->disable,                       ' f->disable';
  }
}

FORK_SEGV:
{
  last FORK_SEGV if 1;

  section 'FORK + DB::Oracle DEGV';

}

note sprintf 'Completed in %5.3fs', Time::HiRes::time() - $TEST_START;
done_testing();


## QUEUE

package DB::Fork;

use strict;
use warnings;
use Time::HiRes qw| usleep |;
use DBI;
use Test::More;
use Data::Dumper;

use lib 't/lib';
use DBDOracleTestLib qw/ db_handle /;


our $VERSION;
our $VERBOSE;
our $ENABLED;
our $CHILDREN;
our $PARENT;

our $ONETHR :shared;

BEGIN {
  $VERSION  = 0.1;
  $VERBOSE  = $main::VERBOSE || 0;
  $CHILDREN = [];
  $PARENT   = $$;

# DBI->trace(9);
}

DESTROY { __PACKAGE__->disable; }
END     { __PACKAGE__->disable; }

sub new { return bless {}, shift; }

sub isParent    { return $PARENT && $PARENT == $$ }
sub isEnabled   { return $ENABLED && $ENABLED > 0 }

sub isDisabled  { return ! isEnabled() }

sub disable
{
  my $self = shift;

  if ( isEnabled )
  {
    printf "# DISABLE %s children\n", scalar @ $CHILDREN;

    while ( @ $CHILDREN )
    {
      my $child_pid = shift @ $CHILDREN;

      is kill( 'USR2', $child_pid ), 1,  'kill USR2 ' . $child_pid;
      is waitpid( $child_pid, 0), $child_pid, 'wait ' . $child_pid . ' 0';
    }

    $ENABLED = 0;
  }

  return $self->isDisabled;
}

sub enable
{
  my $self = shift;
  my $children = shift;

  if ( $children && $self->isDisabled )
  {
    for my $cnt ( 1 .. $children )
    {
      FORK:
      {
        my $pid = fork();

        last FORK if ( ! defined $pid );

        ## CHILD
        if ( $pid == 0 )
        {
          $ENABLED  = 0;
          $CHILDREN = [];
          exit _FORK_WORKER();
        }

        ## I'm the parent!
        push @ $CHILDREN, $pid;
        $ENABLED++;
        ok $pid, 'Forked child ' . $pid;
      }
    }
  }

# usleep 500000;

  return $ENABLED;
}


sub ping
{
  my $self = shift;
  my $conn = 0;
  my $child_pid;
  my $olimit = 3 * scalar @ $CHILDREN;
  my $signaled = {};

  local $SIG{USR1} = sub
  {
    return unless $child_pid;
    $signaled->{$child_pid} = $child_pid;
    $conn++;
    ok $child_pid, sprintf 'PING ACK by %s from %s', $$, $child_pid;
  };

  ok 1, sprintf 'SIGNAL %d children to PING', scalar @ $CHILDREN;

  while ( $conn < scalar @ $CHILDREN && $olimit )
  {
    ## Signal Next
    $child_pid = ( grep { ! exists $signaled->{$_} } @ $CHILDREN )[0];
    my $limit = 50;

    last unless $child_pid;

    ## USR1 == ping
    usleep 100000;
    ok kill( 'USR1', $child_pid ), 'kill USR1(ping) ' . $child_pid;

    while ( $limit-- && ! exists $signaled->{ $child_pid } )
    {
      usleep 200000;
    }
  }

  return $conn;
}


QUEUE_BACKEND:
{
  my $dbh;
  my $do_ping;
  my $do_exit;

  sub _USER1 { printf "# USR1=PING on-child=%d received\n", $$; return ( $do_ping = 1 ); }
  sub _USER2 { printf "# USR2=EXIT on-child=%d received\n", $$; return ( $do_exit = 1 ); }

  sub _FORK_WORKER
  {
    $do_ping = $do_exit = 0;

    printf "# PID=%d (START)\n", $$;

    local $SIG{USR1} = \&_USER1;
    local $SIG{USR2} = \&_USER2;

    BUSY:
    while (1)
    {
      ## CASE - PING
      if ( $do_ping )
      {
        printf "# pid=%s PING received (hold on, this is going to be a bumpy ride!)\n", $$;
        _connect();
        printf "# PARENT=%s CHILD=%d %s=kill USR1\n", $PARENT, $$, kill( 'USR1', $PARENT );
        $do_ping = 0;
        next;
      }

      ## CASE - EXIT
      if ( $do_exit )
      {
        _disconnect();
        $do_exit = 0;
        last BUSY;
      }

    # sleep 2;
      usleep 50000;
    }

    printf "# pid-%s EXIT\n", $$;

    return 0;
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
      printf "# CONNECT-ENTER pid=%d\n", $$ if $VERBOSE;
      $dbh = do_connect();
      printf "# PING=%d pid=%d\n", $dbh->ping, $$;
      printf "# CONNECT-EXIT  pid=%d\n", $$ if $VERBOSE;
    }

    return;
  }

  sub _disconnect
  {
    if ( $dbh )
    {
      printf "# DISCONNECT-ENTER pid=%d\n", $$  if $VERBOSE;
      $dbh->disconnect;
      $dbh = undef;
      printf "# DISCONNECT-EXIT  pid=%d\n", $$  if $VERBOSE;
    }

    return;
  }
}

1;

## vim: number expandtab tabstop=2 shiftwidth=2
## END
