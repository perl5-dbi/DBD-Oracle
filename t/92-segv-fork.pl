#!/usr/bin/env perl

use strict;
use warnings;
use lib 't/lib';
use DBDOracleTestLib qw| db_handle |;
use Time::HiRes qw| usleep |;

our $VERSION = 0.01;

## GOAL: Test for segfaults in parent processes receieving SIGCHLD
##   An application I maintain, dispatches childern to perform work
##   that the parent process does not have time to perform. It has completed
##   the work it needed, places the remaing task into a queeue, and the queue is used
##   by the parent process for dispatching child proceses.
##   The parent reaps the children and launches new children as needed
##   until the queue is empty. The parent process is long running
##   performing DB work it must itself perform.

##  We dont have any real work here so we'll emulate the work.
##   This program is the child process. A test program that forks us
##   will run to emulate the work being dispatched.
##
##  1. Connecting to DB
##  2. Read data.
##  3. Pretending to do work for a random period of time in the range of 2-5 seconds
##      (which approxily matches the time the actual tool I maintain takes to do the task)
##  4. Disconnect from DB
##  5. Exit with a success exit code.
##      The parent does not care if we succeeded or not, it just needs to know
##      that we have completed the work and available for reaping.
##      allowing for another task to be dispatched.

local $Data::Dumper::Indent = 1;
local $Data::Dumper::Terse  = 1;

my $job = @ARGV ? shift : 'DEFAULT-JOB';
my $dbh = db_handle({ AutoCommit => 0, RaiseError => 0, PrintError => 1 });

exit(1) unless $dbh;
exit(2) unless $dbh->ping;

my $sth = $dbh->prepare("SELECT '${job}: The Quick Brown Fox Jumped Over The Lazy Dogs Back' FROM DUAL");

exit(3) unless $sth;
exit(4) unless $sth->execute;

my $row = $sth->fetchrow_arrayref;

exit(5) unless $sth->finish;
exit(6) unless scalar @ $row == 1;
# printf "# [ %s ]\n", $row->[];

my $usleep = int(rand(100000)) + 2000000; # 1-3 seconds (to speed up test!)
# printf "# %02.2f seconds\n", $usleep / 1000000;
usleep($usleep);

exit(7) unless $dbh->disconnect;

## Trigger OS into sending SIGCHLD to the parent process.
exit(0);

## vim: set ts=2 sw=2 expandtab number:
## END
