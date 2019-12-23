# Copyright (c) 1994-2006 Tim Bunce. Ireland.
# Copyright (c) 2006-2008 John Scoles (The Pythian Group). Canada.

use 5.6.0;

use strict;
use warnings;

use ExtUtils::MakeMaker 5.16, qw(&WriteMakefile $Verbose);
use Getopt::Long;
use Config;
use Cwd;
use File::Find;
use Pod::Usage;

# DBI must be installed before we can build a DBD.
# For those not using Dynamic loading this means building a
# new static perl in the DBI directory by saying 'make perl'
# and then using _that_ perl to make this one.
use DBI 1.623;
use DBI::DBD;	# DBD creation tools


# Some MakeMaker's forged some FileHandle methods
require FileHandle unless defined &FileHandle::new;

BEGIN {
    return unless $^O eq 'VMS';
    eval q{
        use vmsish;
        use VMS::Filespec;
        1;
    } or die $@;
}

my $dbi_arch_dir = dbd_dbi_arch_dir();
my $so = $Config{so}; # typically 'so', 'dylib' on Darwin/OSX
my $osvers = $Config{osvers}; $osvers =~ s/^\s*(\d+\.\d+).*/$1/; # drop sub-sub-version: 2.5.1 -> 2.5
my $exe_ext = ($^O eq 'VMS') ? '.pl' : '';
my $BELL = "\a";
# put this here as it might change
$| = 1;

my %opts = (
    NAME => 'DBD::Oracle',
    VERSION_FROM => 'lib/DBD/Oracle.pm',
    PREREQ_PM => { "Test::Simple" => 0.90, # actually Test::More pkg in T::S dist
                   "DBI"          => 1.623},
    OBJECT => '$(O_FILES)',
    DEFINE => '',
    DIR  => [],
    clean => {	FILES	=> 'xstmp.c Oracle.xsi dll.base dll.exp sqlnet.log libOracle.def mk.pm DBD_ORA_OBJ.*' },
    dist  => {
	DIST_DEFAULT	=> 'clean distcheck disttest tardist',
	PREOP		=> '$(MAKE) -f Makefile.old distdir',
	COMPRESS	=> 'gzip -v9', SUFFIX => 'gz',
    },
    META_MERGE => {
       configure_requires => { "DBI" => '1.623' },
       build_requires => {"DBI" => '1.623',
                          "ExtUtils::MakeMaker" => 0,
                          "Test::Simple" => '0.90'},
       resources => {
           bugtracker => 'https://github.com/perl5-dbi/DBD-Oracle/issues',
           homepage => 'https://metacpan.org/pod/DBD::Oracle',
           repository => {
               type => 'git',
               url => 'git://github.com/perl5-dbi/DBD-Oracle.git',
               web => 'http://github.com/perl5-dbi/DBD-Oracle',
           },
       },
    },
);
my $eumm = $ExtUtils::MakeMaker::VERSION;
$eumm =~ tr/_//d;

if ($eumm >= 5.43) {
    $opts{AUTHOR} = 'Tim Bunce (dbi-users@perl.org)';
    $opts{ABSTRACT_FROM} = 'lib/DBD/Oracle.pm';
    $opts{PREREQ_PM} = { DBI => 1.623 };
    $opts{CAPI} = 'TRUE' if $Config{archname} =~ /-object\b/i;
}

$opts{LICENSE} = 'perl' if $eumm >= 6.3002;
$opts{CCFLAGS} = "-P $Config{ccflags}" if $Config{cc} eq 'bcc32';  # force C++
$opts{LINKTYPE} = 'static' if $Config{dlsrc} =~ /dl_none/;

my(@MK, %MK, $MK_TEXT, %MK_expanding);	# parsed macros from Oracle's makefiles
my %mk_target_deps;
my %mk_target_rules;

=head1 OPTIONS

=over

=item -b

Try to use Oracle's own 'build' rule. Defaults to true.

=item -r

With I<-b>, use this names build rule (eg -r=build64).

=item -m

Path to 'oracle.mk'

=item -h

Path to oracle header files.

=item -p

Alter preference for oracle.mk.

=item -n

Oracle .mk macro name to use for library list to link with.

=item -c

Don't encourage use of shared library.

=item -l

Try direct-link to libclntsh.

=item -g

Enable debugging (-g for compiler and linker).

=item -s

Find a symbol in oracle libs, Don't build a Makefile.

=item -S

Find a symbol in oracle & system libs, Don't build a Makefile.

=item -v

Be more verbose.

=item -d

Much more verbose for debugging.

=item -f

Include text of oracle's .mk file within generated Makefile.

=item -F

Force - ignore errors.

=item -W

Just write a basic default Makefile (won't build).

=item -w

Enable many gcc compiler warnings.

=item -V

Force assumption of specified Oracle version
If == 8 then we don't use the new OCI_INIT code
and we force our emulation of OCILobWriteAppend.

=back

=cut

# Options (rarely needed)
# to turn off an option prefix with 'no', ie 'perl Makefile.PL -nob'
#$::opt_ic10 = 1;   # Build for Oracle 10g instantclient
$::opt_b = 1;	# try to use Oracle's own 'build' rule
$::opt_r = '';	# With -b above, use this names build rule (eg -r=build64)
$::opt_m = '';	# path to oracle.mk file to read
$::opt_h = '';	# path to oracle header files
$::opt_p = '';	# alter preference for oracle.mk
$::opt_n = '';	# Oracle .mk macro name to use for library list to link with
$::opt_c = 0;	# don't encourage use of shared library
$::opt_l = 0;	# try direct-link to libclntsh
$::opt_g = '';	# enable debugging (-g for compiler and linker)
$::opt_s = '';	# Find a symbol in oracle libs, Don't build a Makefile
$::opt_S = '';	# Find a symbol in oracle & system libs, Don't build a Makefile
$::opt_v = 0;	# be more verbose
$::opt_d = 0;	# much more verbose for debugging
$::opt_f = 0;	# include text of oracle's .mk file within generated Makefile
$::opt_F = 0;	# force - ignore errors
$::opt_W = 0;	# just write a basic default Makefile (won't build)
$::opt_w = 0;	# enable many gcc compiler warnings
$::opt_V = 0;   # force assumption of specified Oracle version
		# If == 8 then we don't use the new OCI_INIT code
		# and we force our emulation of OCILobWriteAppend

Getopt::Long::config( qw( no_ignore_case ) );
GetOptions(qw(b! r=s v! d! g! p! l! c! f! F! W! w! m=s h=s n=s s=s S=s V=s ))
  or die pod2usage( -verbose => 99, -sections => [ 'OPTIONS' ] );

$::opt_g &&= '-g';	# convert to actual string
$::opt_v = 1 if $::opt_d;
$Verbose = 1 if $::opt_v;
my $is_developer = (-d ".svn" && -f "MANIFEST.SKIP");

if ($::opt_W) {
    open MK_PM, ">/dev/null" or die "Unable to create 'mk.pm': $!";
    exit WriteMakefile( dbd_edit_mm_attribs(\%opts) )
}

# --- Introduction

print <<"END_BLURB" unless $::opt_s;
Configuring DBD::Oracle for perl $] on $^O ($Config{archname})

If you encounter any problem, a collection of troubleshooting
guides are available under lib/DBD/Oracle/Troubleshooting.
'DBD::Oracle::Troubleshooting' is the general troubleshooting
guide, while platform-specific troubleshooting hints
live in their labelled sub-document (e.g., Win32
hints are gathered in 'lib/DBD/Oracle/Troubleshooting/Win32.pod').

END_BLURB


# --- Where is Oracle installed...

my $ORACLE_ENV  = ($^O eq 'VMS') ? 'ORA_ROOT' : 'ORACLE_HOME';
my $OH = $ENV{$ORACLE_ENV} || '';
$OH = win32_oracle_home($OH) if ($^O eq 'MSWin32') or ($^O =~ /cygwin/i);
$OH = unixify $OH if $^O eq 'VMS';
$OH =~ s:/$::;

if (!$OH) {
  $OH = find_oracle_home() || die qq{
      The $ORACLE_ENV environment variable is not set and I couldn\'t guess it.
      It must be set to hold the path to an Oracle installation directory
      on this machine (or a machine with a compatible architecture).
      See the appropriate troubleshooting guide for your OS for more information.
      ABORTED!
  \n};
  $ENV{$ORACLE_ENV} = $OH;
  print "\n";
  print "WARNING: Setting $ORACLE_ENV env var to $OH for you.\a\n";
  print "WARNING: If these tests fail you may have to set ORACLE_HOME yourself!\n";
  sleep 5;
}
-l $OH and $OH = Cwd::abs_path ($OH); # Oracle really dislikes symbolic links
die qq{  The $ORACLE_ENV environment variable value ($OH) is not valid.
  It must be set to hold the path to an Oracle installation directory
  on this machine (or a machine with a compatible architecture).
  For an Instant Client install, the directory should include an sdk subdirectory.
  See the appropriate troubleshooting guide for your OS for more information.
  ABORTED!
} unless (-d $OH and $^O eq 'VMS')
      or -d "$OH/sdk/." 	# Instant Client with SDK
      or -d "$OH/lib/." 	# normal Oracle installation
      or glob("$OH/libclntsh.$so*") # pre-sdk instant client or rpm
      or -e "$OH/oci.dll";	# Windows Instant Client

print "Installing on a $^O, Ver#$osvers\n";
print "Using Oracle in $OH\n";

# $client_version => Major.Minor, $client_version_full => Major.Minor.X.Y.Z
my ($client_version, $client_version_full) = get_client_version($::opt_V);

die "DBD::Oracle no longer supports Oracle client versions before 9.2 \n Try a version before 1.25 for 9 and 1.18 for 8!"
    if $client_version < 9;


check_macos_symbol_table();

symbol_search() if $::opt_s or $::opt_S;


# --- How shall we link with Oracle? Let me count the ways...

my $mkfile;	# primary .mk file to use
my @mkfiles;	# $mkfile plus any files it 'includes'
my $linkwith = "";
my $linkwith_msg = "";
my $need_ldlp_env;

# Do these in advance to enable automatic -l
my @libclntsh = glob("$OH/libclntsh.$so*");

if ($^O eq 'VMS') {
    my $OCIINCLUDE = join " ",	vmsify("$OH/rdbms/"),
				vmsify("$OH/rdbms/public"),
				vmsify("$OH/rdbms/demo/"),
				vmsify("$OH/rdbms/demo/oci_demo/"),
				vmsify("$OH/netconfig/demo/"); # eg nzt.h in 8.1.7 on VMS
    $opts{INC}  = "$OCIINCLUDE $dbi_arch_dir";
    $opts{OBJECT} = 'oracle.obj dbdimp.obj oci7.obj oci8.obj' if $] < 5.005;

    unless ($ENV{PERL_ENV_TABLES}) {
	print qq{
    The logical PERL_ENV_TABLES is not set.

    This may mean that some of the UTF functionallity tests may fail,
    and that some logicals may be set in the Oracle Table.

    To ensure that testing the package only sets logicals in your process
    table, please set this logical:

       \$ DEFINE PERL_ENV_TABLES LNM\$PROCESS
	\a\n};
        sleep 3;
    }
}

elsif (($^O eq 'MSWin32') or ($^O =~ /cygwin/i)) {

    my $OCIDIR = "";
    find( sub {
	print "Found $_ directory\n" if /^OCI\d*$/i;
	$OCIDIR = $_ if /^OCI\d*$/i && $OCIDIR lt $_;
	$File::Find::prune = 1 if -d $_ && $_ !~ /^\./;
    }, $OH );

    $OCIDIR = 'sdk' if !$OCIDIR && -d "$OH/sdk"; # Instant Client SDK

    die "OCI directory not found, please install OCI in $OH" if ! $OCIDIR;
    print "Using OCI directory '$OCIDIR'\n";

    if ($Config{cc} =~ /gcc/i) {

      system "dlltool --input-def oci.def --output-lib liboci.a"
        unless -f "liboci.a";

      die "Could not find or create liboci.a.\n" unless -f "liboci.a";

      my $pwd = cwd();
      $opts{LIBS} = [ "-L$pwd -loci" ];
    } else {
      my %OCILIB;
      my $oci_compiler_dir;
      my @oci_compiler_dirs =
	  map { -d "$OH/$OCIDIR/lib/$_" ? "$OH/$OCIDIR/lib/$_": () }
	  $Config{cc} eq 'bcc32' ? qw(BORLAND BC) : qw(MSVC);
      find( sub {
	$File::Find::prune = 1 if -d $_ && $_ !~ /^\./;
	return unless /^(OCI|ORA).*\.LIB$/i;
	($oci_compiler_dir = $File::Find::dir) =~ s:^.*/::;
	print "Found $OCIDIR/lib/$oci_compiler_dir/$_ library\n";
	$OCILIB{uc($_)} = $_;
      }, @oci_compiler_dirs );
      # sort the version numbered libs into assending order
      my @OCILIB = sort grep { /(OCI|ORA)\d\d+\./i } keys %OCILIB;
      # prefer the non-versioned library if present
      push @OCILIB, "OCI.LIB"    if $OCILIB{'OCI.LIB'};
      my $OCILIB = pop @OCILIB || '';
      $OCILIB =~ s/\.LIB$//i;

      die qq{
      Unable to find required Oracle OCI files for the build.  Please check
      that you have your OCI installed in your oracle home ($OH)
      directory and that it has the following files (and probably more):

	$OH\\$OCIDIR\\include\\oratypes.h
	$OH\\$OCIDIR\\lib\\$oci_compiler_dir\\$OCILIB.lib

      Please install OCI or send comments back to dbi-users\@perl.org
      if you have an OCI directory other than $OCIDIR.

      Alternatively, if you\'re using ActiveState perl on Windows try
        ppm install ftp://ftp.esoftmatic.com/outgoing/DBI/5.8.3/DBI.ppd
        ppm install ftp://ftp.esoftmatic.com/outgoing/DBI/5.8.3/DBD-Oracle.ppd

      } unless  (-e "$OH/$OCIDIR/include/oratypes.h"
	      && -e "$OH/$OCIDIR/lib/$oci_compiler_dir/$OCILIB.lib")
	      or $::opt_F;

      print "Using $OCIDIR/lib/$oci_compiler_dir/$OCILIB.lib\n";
      $opts{LIBS} = [ "-L$OH/$OCIDIR/LIB/$oci_compiler_dir $OCILIB" ];
    };

    my $OCIINCLUDE = "-I$OH/$OCIDIR/include -I$OH/rdbms/demo";
    $opts{INC}  = "$OCIINCLUDE -I$dbi_arch_dir";
}

# --- UNIX Variants ---

elsif ($::opt_l || !defined $mkfile and # use -l to enable this direct-link approach
        @_=grep { m:/lib(cl(ie)?ntsh|oracle).\w+$:o } <$OH/lib/lib*>
    ) {
    # --- the simple modern way ---
    foreach(@_) { s:\Q$OH/lib/::g }
    print "Found direct-link candidates: @_\n";
    my $lib = ("@_" =~ m:lib(cl(ie)?ntsh)\.:) ? $1 : "oracle";
    my $syslibs = read_sysliblist();
    print "Oracle sysliblist: $syslibs\n";
    my $libdir = ora_libdir();
    $opts{dynamic_lib} = { OTHERLDFLAGS => "$::opt_g" };
    my @h_dirs = find_headers();
    if ($client_version_full =~ /^8.0.6/ && $^O eq 'hpux') {
        $linkwith_msg = "-lextp -l$lib.";
        $opts{LIBS} = [ "-L$OH/$libdir -lextp -l$lib $syslibs" ];
        push @h_dirs, "$OH/network/public";
    }
    else {
        $linkwith_msg = "-l$lib.";
        $opts{LIBS} = [ "-L$OH/$libdir -l$lib $syslibs" ];
    }
    my $inc = join " ", map { "-I$_" } @h_dirs;
    $opts{INC}  = "$inc -I$dbi_arch_dir";
    check_ldlibpthname();
}

# --- special case for Oracle 10g instant client (note lack of ../lib/...)

elsif (@libclntsh) {

    print "Looks like an Instant Client installation, okay\n";

    # the libclntsh.$so (without version suffix) may be missing
    # we need it to link to so try to create it
    eval {
	print "You don't have a libclntsh.$so file, only @libclntsh\n";
	my $libclntsh_v = (grep { /\d$/ } sort @libclntsh)[0]; # tacky but sufficient
	print "So I'm going to create a $OH/libclntsh.$so symlink to $libclntsh_v\n";
        symlink($libclntsh_v, "$OH/libclntsh.$so")
	    or warn "Can't create symlink $OH/libclntsh.$so to $libclntsh_v: $!\n";
    } unless -e "$OH/libclntsh.$so";

    check_ldlibpthname($OH);

    my $syslibs = read_sysliblist();
    print "Oracle sysliblist: $syslibs\n";

    $opts{dynamic_lib} = { OTHERLDFLAGS => "$::opt_g" };

    my $lib = "clntsh";
    $linkwith_msg = "-l$lib.";
    $opts{LIBS} = [ "-L$OH -l$lib $syslibs" ];

    my $inc = join " ", map { "-I$_" } find_headers();
    $opts{INC}  = "$inc -I$dbi_arch_dir";
}

elsif (defined $mkfile and $mkfile =~ /\bdemo_xe.mk$/) { # Oracle XE

    print "Looks like Oracle XE ($mkfile)\n";

    fetch_oci_macros($mkfile);
    $MK{CCINCLUDES} = '-I$(ICINCHOME)'; # undo odd refinition in demo_xe.mk

    # From linux Oracle XE (10.2.0):
    #	ICINCHOME=$(ORACLE_HOME)/rdbms/public/
    #	ICLIBHOME=$(ORACLE_HOME)/lib/
    #	ICLIBPATH=-L$(ICLIBHOME)
    #	THREADLIBS=-lpthread    [initially -lthread then redefined]
    #	CCLIB=$(ICLIBPATH) -lclntsh $(THREADLIBS)
    #	CCINCLUDES = -I$(ICINCHOME)   [see above]
    #   CCFLAGS=$(CCINCLUDES) -DLINUX -D_GNU_SOURCE -D_REENTRANT -g [initially without -DLINUX -D_GNU_SOURCE]
    my $cclib   = expand_mkvars($MK{CCLIB}, 0, 1);
    my $ccflags = expand_mkvars($MK{CCFLAGS}, 0, 1);

    $linkwith_msg = "$cclib";
    $opts{LIBS} = [ $cclib ];
    $opts{INC}  = "-I$dbi_arch_dir $ccflags";
    $opts{dynamic_lib} = { OTHERLDFLAGS => "$::opt_g" };
    check_ldlibpthname();
}

else {	# --- trawl the guts of Oracle's make files looking the how it wants to link
    #Lincoln: pick the right library path
    check_ldlibpthname();
    my $libdir = ora_libdir();
    my @ora_libs = <$OH/$libdir/lib*>;
    if (@ora_libs < 6) { # just a helpful hint
	warn "\nYou don't seem to have many Oracle libraries installed. If the"
	    ."\nbuild fails you probably need to install more Oracle software.\n\n";
	sleep 6;
    }

    # can we give the shared library a helping hand?
    my @shared = grep { m:/lib(cl(ie)?ntsh|oracle).\w+$:o } @ora_libs;
    # show original value of ORA_CLIENT_LIB if defined ...
    print "\$ORA_CLIENT_LIB=$ENV{ORA_CLIENT_LIB}\n"
		if defined $ENV{ORA_CLIENT_LIB};
    # ... before we then set it how it probably should be set
    # XXX but we still need to write it into the generated Makefile.
    $ENV{ORA_CLIENT_LIB} = 'shared'
		if !defined $ENV{ORA_CLIENT_LIB}
		&& ($opts{LINKTYPE}||'') ne 'static' && @shared && !$::opt_c;

    my $linkvia = fetch_oci_macros($mkfile) if -f $mkfile;

    my $libhome = expand_mkvars($MK{LIBHOME}, 0, 1) || "$OH/lib";
    $linkwith = expand_mkvars($linkvia, 0, 1);

    #now use logic based of oracle version
    $linkwith =~ s!/lib\b!/$libdir!g;
    $libhome =~ s!/lib\b!/$libdir!g;
    #print "linkwith=$linkwith\n";


    my @build_rules = grep { $mk_target_rules{$_} } qw(build build64 build32);
    my $build_target = "build";
    if (@build_rules && $::opt_b) {
	print "\n";

	$build_target = "build32" if $mk_target_rules{build32};
	$build_target = "build64" if $mk_target_rules{build64} && perl_is_64bit();
	$build_target = $::opt_r if $::opt_r;

	print "Attempting to discover Oracle OCI $build_target rules\n";

	# create dummy C file to keep 'make $mkfile' happy
	my $DBD_ORA_OBJ = 'DBD_ORA_OBJ';
        open DBD_ORA_C, ">$DBD_ORA_OBJ.c"
	    or die "Can't create temporary $DBD_ORA_OBJ.c file in current directory: $!\n";
        print DBD_ORA_C "int main() { return 1; }\n";
        close DBD_ORA_C;
	sleep 2; #
	system("make $DBD_ORA_OBJ.o CC='$Config{cc}'"); # make a valid .o file.

	my $make = "$Config{make} -f $mkfile $build_target"
		." ECHODO=echo ECHO=echo GENCLNTSH='echo genclntsh' CC=true"
		." OPTIMIZE= CCFLAGS="
		." EXE=DBD_ORA_EXE OBJS=$DBD_ORA_OBJ.o";
	print "by executing: [$make]\n";
	my @cmds = `$make 2>&1`;
	chomp @cmds;
	print "returned:\n[".join("]\n[",@cmds)."]\n" if $::opt_v;
	warn "WARNING: Oracle build rule discovery failed ($?)\n" if $?;
	warn "Add path to $Config{make} command into your PATH environment variable.\n"
		if $? && "@cmds" =~ /make.*not found/; # hint

	my @filtered_cmds;
        while (my $line = shift @cmds) {
	    # join lines split with \'s
	    while ($line =~ s/\\$/ /)  { $line .= shift @cmds; }
	    # remove any echo's as the following line should be the result of the echo
	    next if $line =~ /^\s*\S*echo\s+/;
	    next if $line =~ /^\s*\S*make\s+/; # remove recursive calls to make
	    next if $line =~ /^\s*\S*make(\[\d\])?:/; # remove message rom "make:" or "make[x]:"

	    next if $line =~ /^\s*$/; # remove any blank lines
	    push @filtered_cmds, $line;
	}
	print "reduced to:\n[".join("]\n[",@filtered_cmds)."]\n"
	    if $::opt_v && "@filtered_cmds" ne "@cmds";
	@cmds = @filtered_cmds;

	my @prolog; push @prolog, shift @cmds while @cmds && $cmds[0] !~ /DBD_ORA_EXE/;
	print "Oracle oci build prolog:\n \t[", join("]\n\t[", @prolog), "]\n" if @prolog;
	print "Oracle oci build command:\n\t[", join("]\n\t[", @cmds  ), "]\n";
	if (@cmds == 1 && (my $build = shift @cmds) =~ /DBD_ORA_EXE/) {
	    $build =~ s/\s*true\s+//;		# remove dummy compiler
	    $build =~ s/$DBD_ORA_OBJ.o//;	# remove dummy object file
	    $build =~ s/\S+\s+DBD_ORA_EXE//;	# remove dummy exe file and preceding flag
	    $build =~ s/-o build\S*//;		# remove -o target that confuses gcc at least on Sun
	    $linkwith = $build;
	    # delete problematic crt?.o on solaris
	    $linkwith = del_crtobj($linkwith, 1) if $^O eq 'solaris';
	}
	else {
	    print "WARNING: Unable to interpret Oracle build commands from $mkfile.\a\n";
	    print "(Will continue by using fallback approach.)\n";
	    sleep 2;
	    $::opt_b = 0;
	}
	unlink "$DBD_ORA_OBJ.c", "$DBD_ORA_OBJ.o"
		unless $^O eq 'darwin'; # why?
	print "\n";
    }
    else {
	print "WARNING: Oracle $mkfile doesn't define a 'build' rule.\n" if $::opt_b;
	$::opt_b = 0;
	print "\n";
	print "WARNING: I will now try to guess how to build and link DBD::Oracle for you.$BELL\n";
	print "         This kind of guess work is very error prone and Oracle-version sensitive.\n";
	print "         It is possible that it won't be supported in future versions of DBD::Oracle.\n";
	print "         *PLEASE* notify dbi-users about exactly _why_ you had to build it this way.\n";
	print "\n";
	sleep 6;
    }

    $linkwith =~ s/-Y P,/-YP,/g if $Config{gccversion};
    $linkwith =~ s:-R /:-R/:g if $^O eq 'solaris';

    # modifications (mostly) by Lincoln Baxter
    if ( ($^O eq 'hpux') && ($osvers > 10) && (
        $Config{'cc'} eq 'cc' or $Config{'gccversion'}) )
    {
        # these get dragged in from demo_rdbms.mk where Oracle uses them
        # the linker bitches about them not being valid options
        # in this context

        # (on my system, using gcc, the flags can't be removed entirely;
        # instead, they have to be converted to the ld-compatible equivs.
        # -- Sweth (<svc@sweth.net>)
        if ( $Config{'cc'} eq 'cc' ) {
           $linkwith =~ s/-Wl,\+[sn]//g;
        } elsif ( $Config{'gccversion'} ) {
           $linkwith =~ s/-Wl,(\+[sn])/$1/g;
        };

        # Oracle 8.0.5 drags in these which also cause link errors:
        # this produces a good link with the 32bit version of oracle
        # (64bit version of 8.0.5 fails to link
        # someone else will have to hack that one out)
        $linkwith =~ s/\+D[AS]2\.0[W]* / /g;

        #Lincoln: pick the right library path
        my $libdir = ora_libdir();
        $linkwith =~ s!/lib\b!/$libdir!g;

        # A number of folks have had to add this library to resolve
        # undefined symbol errors reported at runtime by ld.sl (the
        # library loader) libqsmashr defines LhrStringInsert()...
        # other libraries may have to be added (Lincoln Baxter <lbaxter@fleetcc.com>)
        # we check for the library's existence first... (8.0.5 does not have it)
        if ( -r "$OH/$libdir/libqsmashr.sl" ) {
           $linkwith =~ s/$/ -lqsmashr/ if ( $linkwith !~ m/-lqsmshr/ );
        }

        if ( ($Config{'libswanted'} !~ m/\bcl\b/) or $Config{'libswanted'} !~ m/\bpthread\b/ ) {
            print "WARNING: Oracle is built with multi-threading libraries\n"
                . "         You will most likely need to rebuild perl from sources\n"
                . "         with the following libraries: -lcl -lpthread\n" ;
        }

       if ( $osvers >= 11 and
            $linkwith =~ m/-lcl\b/ && $linkwith =~ m/-l:libcl.a/
        ) {  # from h.m.brand@hccnet.nl
            print "WARNING: Stripping -lcl from the link flags, because it\n",
                  "         seems to conflict with -l:libcl.a\n" ;
            $linkwith =~ s/\s*-lcl\b//g;
        }
        # Lincoln: Jay Strauss's Oracle 8.1.6 fix... I did this manually
        #          when I still had 8.1.6 to deal with (its in the README):
        #
        # Jay:     8.1.6 gets Unresolved symbols on LhtStrInsert
        #           $linkwith =~ s/$/ -lclntsh/ if ( $linkwith !~ = m/-lclntsh/ );
        #          Tim - Not to be disobedient but at this point of the code
        #          $client_version_full = 0, so I can't do Oracle version 8.1.6 logic
        #          So it seems easier to modify if I do it like below rather than above
        #          obviously its your choice
        #
        # Lincoln: I don't see how the code above could work if $inspdver{RDMS} == 0
        #          so I will add this Jays prefered way but inside a test like that
        #          above... if he tests this and it works great... if someone else is
        #          has a problem it is easy to comment out the test. I like Jay's test
        #          for existence first... And I added an addition test of $OH to see
        #          if it looks like 8.1.6 (just incase Jay was right).
        #
        # Jay:     Add Librarys where one gets Unresolved symbols
        #
        if ( ( $osvers >= 11 and $client_version_full =~ /^8\.1\.6/ )
        or   ( $osvers >= 11 and $OH =~ m,/8\.1\.6, ) ) {
            my @extraLib = qw[libqsmashr.sl libclntsh.sl];
            foreach my $extraLib (@extraLib) {
                if (-r "$OH/lib/$extraLib") {
                        $extraLib =~ s/^lib([^\.]+).*$/\-l$1/;
                        $linkwith =~ s/$/ $extraLib/ if ( $linkwith !~ m/$extraLib/ );
                }
            }
        }

        if ($osvers >= 11 and $linkwith =~ m/-l:libcl.a/) {
	    print "WARNING: stripping -l:libcl.a from liblist (conflict with ld looking for shared libs)\n";
	    $linkwith =~ s/\s*-l:libcl.a\b//g;
	}

        #lincoln: this is bringing back everything we thought we removed... (like libcl.a)
        #         I wonder if this should targetted less specifically than only HPUX 11
        #         For now this should be relatively safe...
        if ( $osvers >= 11 and
            $linkwith =~ s/(`cat[^`]+sysliblist`)//g
        ) {
            print "WARNING: Stripped $1 \n",
                  "         from link command, because it contains libraries not \n",
                  "         compiled with +z or +Z (PIC) causing link to fail.\n",
                  "         Furthermore, we should have already grabbed these libraries\n";;
        }

        my $ccf = join " ", map { $_ || '' } @Config{qw(ccflags ccldflags cccdlflags)};
        if ($Config{cc} =~ /gcc/i) {
	    print "WARNING: perl was not built with -fpic or -fPIC in compiler flags.\n",
		  "         You may need to rebuild perl from sources.\n",
		  "         See instructions in DBD::Oracle::Troubleshooting::Hpux\n"
		unless $ccf =~ m/-fpic\b/i;
        }
        else {
            print "WARNING: perl was not built with +z or +Z in compiler flags.\n",
                  "         You may need to rebuild perl from sources.\n",
                  "         See instructions in DBD::Oracle::Troubleshooting::Hpux\n"
        	unless $ccf =~ m/\+[zZ]/;
        }
    }


    if ($::opt_b) {	# The simple approach
	$opts{dynamic_lib} = { OTHERLDFLAGS => "$::opt_g $linkwith" };
	$linkwith_msg = "OTHERLDFLAGS = $linkwith [from '$build_target' rule]";
    }
    else {		# the not-so-simple approach!
	# get a cut down $linkwith to pass to MakeMaker liblist
	my $linkwith_s = expand_mkvars($linkwith, 1, 1);

	# convert "/full/path/libFOO.a" into "-L/full/path -lFOO"
	# to cater for lack of smarts in MakeMaker / Liblist
	# which ignores /foo/bar.a entries!
	my $lib_ext_re = "(a|$Config{dlext}|$Config{so})";
	$linkwith_s =~ s!(\S+)/lib(\w+)\.($lib_ext_re)\b!-L$1 -l$2!g;

	# Platform specific fix-ups:
	# delete problematic crt?.o on solaris
	$linkwith_s = del_crtobj($linkwith_s) if $^O eq 'solaris';
	$linkwith_s =~ s/-l:lib(\w+)\.sl\b/-l$1/g;	# for hp-ux
	# this kind of stuff should be in a ./hints/* file:
	$linkwith_s .= " -lc" if $Config{osname} eq 'dynixptx'
			      or $Config{archname} =~ /-pc-sco3\.2v5/;
	if ($^O eq 'solaris' and $linkwith_s =~ /-lthread/
		and $osvers >= 2.3 and $osvers <= 2.6
	) {
	    print "WARNING: Solaris 2.5 bug #1224467 may cause '_rmutex_unlock' error.\n";
	    print "Deleting -lthread from link list as a possible workround ($osvers).\n";
	    $linkwith_s =~ s/\s*-lthread\b/ /g;
	}

	# extract object files, keep for use later
	my @linkwith_o;
	push @linkwith_o, $1 while $linkwith_s =~ s/(\S+\.[oa])\b//;
	# also extract AIX .exp files since they confuse MakeMaker
	push @linkwith_o, $1 while $linkwith_s =~ s/(-bI:\S+\.exp)\b//;

	$linkwith_msg = "@linkwith_o $linkwith_s [from $linkvia]";
	$opts{LIBS} = [ "-L$libhome $linkwith_s" ];
	$opts{dynamic_lib} = { OTHERLDFLAGS => "$::opt_g @linkwith_o \$(COMPOBJS)" };
    }

    my $OCIINCLUDE = expand_mkvars($MK{INCLUDE} || '', 0, 0);
    my @h_dirs = find_headers();
    my $inc = join " ", map { "-I$_" } @h_dirs;
    $opts{INC}  = "$inc $OCIINCLUDE -I$dbi_arch_dir";
}


# --- Handle special cases ---

if ($::opt_g && $^O eq "MSWin32" && $Config::Config{cc} eq "cl") {
   $opts{LDDLFLAGS} = $Config::Config{lddlflags} . ' -debug'
}

$opts{DEFINE} .= ' -Wall -Wno-comment' if $Config{gccversion};

$opts{DEFINE} .= ' -Xa' if $Config{cc} eq 'clcc';	# CenterLine CC







$opts{DEFINE} .= ' -DUTF8_SUPPORT' if ($] >= 5.006);

# Use OCIEnvNlsCreate if available for best unicode behaviour
#$opts{DEFINE} .= ' -DNEW_OCI_INIT' if $client_version >= 9.2;
$opts{DEFINE} .= ($^O ne 'VMS')
	? " -DORA_OCI_VERSION=\\\"$client_version_full\\\""
	: " -DORA_OCI_VERSION=\"$client_version_full\"";
# force additional special behavior for oci 8.  For now, this means
# emulating OciLobWriteAppend
# use this if, for some reason the default handling for this function
# doesn't work or you are building a binary release for distribution
# to machines with older client libraries.


print "\nclient_version=$client_version\n\n";

$opts{DEFINE} .= " -DORA_OCI_102" if ($::opt_V && $client_version == 10.2)
				or ( $client_version >= 10.2);

$opts{DEFINE} .= " -DORA_OCI_112" if ($::opt_V && $client_version == 11.2)
				or ( $client_version >= 11.2);

print "\nDEFINE=$opts{DEFINE}\n\n";
# OCIStmntFetch2() is a feature of OCI 9.0.0
$opts{DEFINE} .= " -DUSE_ORA_OCI_STMNT_FETCH " if ( $client_version < 9.0 );


# Set some private WriteMakefile options if this is 'me' :-)
if ($is_developer){  # a reasonable guess
    $BELL = "<BELL>" if ($ENV{LOGNAME}||'') eq 'timbo';
    $::opt_g = '-g';
    if ($Config{gccversion}) {
	$opts{DEFINE} .= ' -Wall -Wcast-align -Wpointer-arith';
	$opts{DEFINE} .= ' -Wbad-function-cast -Wcast-qual';
	#$opts{DEFINE} .= ' -Wconversion'; # very noisy so remove to see what people say
	$opts{DEFINE} .= ' -Wimplicit  -Wimplicit-int -Wimplicit-function-declaration -Werror-implicit-function-declaration -Wimport  -Winline -Wlong-long -Wmissing-braces -Wmissing-format-attribute  -Wmissing-noreturn -Wmultichar  -Wpacked -Wparentheses  -Wpointer-arith  -Wreturn-type  -Wsequence-point -Wsign-compare  -Wswitch  -Wtrigraphs  -Wundef -Wuninitialized -Wunreachable-code -Wunused  -Wunused-function  -Wunused-label  -Wunused-parameter -Wunused-value  -Wunused-variable  -Wwrite-strings -Wbad-function-cast  -Wmissing-declarations -Wnested-externs'
		if $::opt_w;
    }
    $opts{dynamic_lib}->{OTHERLDFLAGS} .= " $::opt_g";
}

# HP-UX 9 cannot link a non-PIC object file into a shared library.
# Since the # .a libs that Oracle supplies contain non-PIC object
# files, we sadly have to build static on HP-UX 9 :(
if ($^O eq 'hpux') {
    if ($osvers < 10) {
	print "WARNING: Forced to build static not dynamic on $^O $osvers.$BELL\n";
	$opts{LINKTYPE} = 'static';
    }
    else {
	print "WARNING: If you have trouble, see DBD::Oracle::Troubleshooting::Hpux...\n"
	          ."    you may have to build your own perl, or go hunting for libraries\n";
    }
    print "WARNING: If you have trouble, try perl Makefile.PL -l\n" unless $::opt_l;
    sleep 5;
    $opts{DEFINE} .= ' $(HP64DEFINES)' if ($Config{archname} =~ /-thread\b/i and perl_is_64bit());
    # linker doesn't understand +DD64 flag Weiguo Sun <wesun@cisco.com>
    $opts{dynamic_lib}->{OTHERLDFLAGS} =~ s/\+DD64\b// if $opts{dynamic_lib}->{OTHERLDFLAGS};
    # see also const_cccmd for -Aa to -Ae flag change
}


# --- check for Test::Harness bug

print "\nChecking for functioning wait.ph\n";
eval { package WAIT; local $^W = 0; require 'wait.ph' };
if (!$@ && !defined(&WAIT::WCOREDUMP)) {
    print "You have a wait.ph file generated by perl h2ph utility.\n";
    print "(I found it at $INC{'wait.ph'})\n";
    print "It does not define a WCOREDUMP function. That's probably an error.\n";
    print "If a DBD::Oracle test fails then you will probably see a message\n";
    print "from Test::Harness about WCOREDUMP being undefined. You can either ignore\n";
    print "it or try to fix your wait.ph file. The message DOES NOT reflect the\n";
    print "cause of the test failure, it's just a problem interpreting the failure.\n";
}
print "\n";


# --- display summary information

# log key platform information to help others help you quickly if needed
print "\n";
print "System: perl$] @Config{qw(myuname)}\n";
print "Compiler:   @Config{qw(cc optimize ccflags)}\n";
print "Linker:     ". (find_bin('ld')||"not found") ."\n" unless $^O eq 'VMS';
print "Sysliblist: ".read_sysliblist()."\n";
print "Oracle makefiles would have used these definitions but we override them:\n"
	if $MK{CFLAGS} || $MK{LDFLAGS} || $MK{LDSTRING};
print "  CC:       $MK{CC}\n"	if $MK{CC};
print "  CFLAGS:   $MK{CFLAGS}\n"                    if $MK{CFLAGS};
print "           [".mkvar('CFLAGS',0,1,0).  "]\n" if $MK{CFLAGS};
print "  CLIBS:    $MK{CLIBS}\n"                     if $MK{CLIBS};
print "           [".mkvar('CLIBS',0,1,0).   "]\n" if $MK{CLIBS};
if ($mk_target_rules{build} && !$::opt_b) {
    my $rules = join "\n", '', @{ $mk_target_rules{build} };
    $rules = expand_mkvars($rules, 0, 0, 1, 1) if $rules =~ /^\s*\$\(\w+\)\s*$/;
print "  build:    $rules\n";
print "           [".expand_mkvars($rules,0,1,0).   "]\n";
}
print "  LDFLAGS:  $MK{LDFLAGS}\n"                   if $MK{LDFLAGS};
print "           [".mkvar('LDFLAGS',0,1,0). "]\n" if $MK{LDFLAGS};
print "  LDSTRING: $MK{LDSTRING}\n"                  if $MK{LDSTRING};
print "           [".mkvar('LDSTRING',0,1,0)."]\n" if $MK{LDSTRING};
print "Linking with $linkwith_msg\n" if $linkwith_msg;
print "\n";

# --- display extra notes and warnings

if ($^O eq 'aix' and $osvers >= 4 and $Config{cc} ne 'xlc_r') {
    print "\n";
    print "WARNING: You will may need to rebuild perl using the xlc_r compiler.\a\n";
    print "         The important thing is that perl and DBD::Oracle be built with the same compiler.\n";
    print "         You may also need to: ORACCENV='cc=xlc_r'; export ORACCENV\n";
    print "         Also see DBD::Oracle::Troubleshooting::Aix for gcc instructions and read about the -p option.\n";
    sleep 5;
}

if ($Config{archname} !~ /-thread\b/i) {
    print "\n";
    print "WARNING: If you have problems you may need to rebuild perl with threading enabled.$BELL\n";
    sleep 5;
}

if ($Config{usemymalloc} eq 'y') {
    print "\n";
    print "WARNING: If you have problems you may need to rebuild perl with -Uusemymalloc.$BELL\n";
    sleep 5;
}

print "WARNING: Your GNU C compiler is very old. Please upgrade.\n"
    if ($Config{gccversion} and $Config{gccversion} =~ m/^(1|2\.[1-5])/);

if ($opts{LINKTYPE} && $opts{LINKTYPE} eq 'static') {
    print "** Note: DBD::Oracle will be built *into* a NEW perl binary. You MUST use that new perl.\n";
}


# --- write the name file etc ---

# create this before WriteMakefile so MakeMaker knows it's here
open(MK_PM, ">mk.pm") or die "Unable to create mk.pm: $!";

WriteMakefile( dbd_edit_mm_attribs(\%opts) );

check_security() unless $^O eq 'VMS' or $^O eq 'MSWin32' or $^O =~ /cygwin/i;

print "\n";

vms_logical_names_sanity_check();

exit 0;

# === utility functions ================================

sub vms_logical_names_sanity_check {
    return unless $^O eq 'VMS';

   unless ( $ENV{PERL_ENV_TABLES} ) { # perl_env_tables not set  report
     print <<'END_TXT';

   The logical PERL_ENV_TABLES is not set.

   This means that any logical names set when testing the package
   will be set in the first logical name table that occurs in the
   LNM$FILE_DEV list.

   Please read the Readme.VMS file for further information.

END_TXT

     return;
   }

    return if grep { $ENV{$_} eq 'LNM$JOB' }
              grep { /^PERL_ENV_TABLES;\d+$/ }
              keys %ENV;

# perl_env_tables set but the element we want is missing
          print <<'END_TXT';

   The logical PERL_ENV_TABLES is set, but without LNM$JOB.

   Testing the package can fail because of inability to correctly
   translate SYS$SCRATCH for temporary storage, as SYS$SCRATCH is
   set at the JOB level.

   To ensure that testing the package correctly translates SYS$SCRATCH,
   please ensure that LNM$JOB is part of PERL_ENV_TABLES like this:

      $ DEFINE PERL_ENV_TABLES LNM$PROCESS, LNM$JOB , CRTL_ENV
END_TXT

}



sub find_oracle_home {
    print "Trying to find an ORACLE_HOME\n";

    my @path = split /\Q$Config{path_sep}/, $ENV{PATH};
    print "using PATH @path\n" if $::opt_v;
    # instant client has libclntsh in same dir as sqlplus
    my @oh = grep { (glob("$_/libclntsh.$so*"))[0] } @path;

    if (!@oh) { # failing that, try LD_LIBRARY_PATH or equiv
	my (undef, undef, @ldlibpth) = ldlibpth_info(1);
	print "using ldlib @ldlibpth\n" if $::opt_v;
	@oh = grep { (glob("$_/libclntsh.$so*"))[0] } @ldlibpth;
	# for instant client @oh may be actual ORACLE_HOME
	# but for non-IC ORACLE_HOME may be dir above a /lib*
	s:/lib\w*/?$:: for @oh; # remove possible trailing lib dir
    }
    if (!@oh) { # else try the traditional kind of install
	# this should work for non-instant-client installs ($OH/bin & $OH/lib*)
	@oh = grep { (glob("$_/../lib*/libclntsh.$so*"))[0] } @path;
	s:/[^/]/?$:: for @oh;
    }
    if (!@oh && lc($^O) eq 'linux') { # Try the standard Linux RPM location
   my @loh = glob("/usr/lib/oracle/*/*/lib/libclntsh.$so*");
   @loh = sort { $a cmp $b } @loh;
   my $loh = pop(@loh);
   $loh =~ s/\/lib\/libclntsh.*$//g;
   push(@oh,$loh);
    }
    print "Found @oh\n" if @oh;
    return $oh[0];
}


sub win32_oracle_home {
  my $oh = shift;

  my ($req_ok, $hkey, $Val, $Keys);
  my $default_home;
  if ( ! $oh ) {
      if ( $Config{osname} eq "MSWin32") {
	  # Win32::TieRegistry is prefered, but it requires Win32API::Registry
	  # which is not available in mingw or cygwin
	  eval {
	      require Win32::TieRegistry;
	      $Win32::TieRegistry::Registry->Delimiter("/");
	      $hkey = $Win32::TieRegistry::Registry->{"LMachine/SOFTWARE/Oracle/"}
                  and $req_ok = 1;
	  };
	  eval { # older name of Win32::TieRegistry
	      require Tie::Registry;
	      $Tie::Registry::Registry->Delimiter("/");
	      $hkey = $Tie::Registry::Registry->{"LMachine/SOFTWARE/Oracle/"}
                  and $req_ok = 1;
	  } unless $req_ok;
	  eval {
	     $default_home = $hkey->{ORACLE_HOME} || '';
	  };
	  eval {
	      $Val = sub {
		  # Return value
		  my ($hkey) = @_;
		  return $hkey->{ORACLE_HOME} || '';
	      };
	      $Keys = sub {
		  # Return list of sub-folder keys
		  my ($hkey) = @_;
		  # MAC: %$hkey and related method calls don't work under
		  #      perl5db, so don't try single stepping through here
		  return map {m:/$: ? $hkey->{$_} : ()} keys %$hkey;
	      };
	  } if $hkey;
      }

      # Win32::Registry imports some symbols into main::
      # this is not commonly wanted, so try this as a last resort
      # MAC: it is available under mingw and might be available under cygwin
      #      If cygwin doesn't have it, move the rest inside the other if block
      eval {
	  require Win32::Registry;
	  $main::HKEY_LOCAL_MACHINE->Open('SOFTWARE\\ORACLE', $hkey);
	  my $dummy = $main::HKEY_LOCAL_MACHINE; # avoid single use complaint
	  eval {
	     my $hval;
	     $hkey->GetValues($hval);
	     $default_home = $hval->{ORACLE_HOME}[2] || '';
	  };
	  $Val = sub {
	      # Return value
	      my ($hkey) = @_;
	      my $hval;
	      $hkey->GetValues($hval);
	      return $hval->{ORACLE_HOME}[2] || '';
	  };
	  $Keys = sub {
	      # Return list of sub-folder keys
	      my ($hkey) = @_;
	      my @hkey;
	      $hkey->GetKeys(\@hkey);
	      @hkey = map { $hkey->Open($_, $_); $_ } @hkey;
	      return @hkey;
	  };
      } unless $req_ok;

      # Workaround Win32::TieRegistry FETCH error during global destruction.
      undef $Win32::TieRegistry::Registry if $Win32::TieRegistry::Registry;

      # Look for ORACLE_HOME in all ORACLE sub-folders, use last one found
      #     before 8.1.5, there should be only one
      eval {
	  my ($oh1, %oh);
	  my @hkey = ($hkey);
	  # JLU: December 5, 2002: if the "default" home is set and has
	  # an OCI directory, then use it.
	  if ($default_home && -d $default_home && -d $default_home . "/oci") {
	     $oh = $default_home;
	  } else {
	     # use previous logic if default home doesn't have OCI
	     # directory
	     while (@hkey) {
		$hkey = shift @hkey;
	      $oh   = $oh1, $oh{$oh1} = 1
		  if ($oh1 = &$Val($hkey)) && -d $oh1;
	      push @hkey, &$Keys($hkey);
	     }
	     if (1 < keys %oh) {
	        # JLU: 8/21/01 Oracle's default home is the first one in
	        # the path, at least with 8i
		print "\n\007Multiple Oracle homes: ", join(" ", sort keys %oh), "\n\n";
		my @path = split(";", $ENV{PATH});
		my $dir;
		foreach $dir (@path) {
	           # the path will be c:\path\to\home\bin, so remove \bin if it's there.
		   $dir =~ s/\\bin$//;
		   if (defined($oh{$dir})) {
		      print "$dir is first in the PATH, so we'll use that as Oracle's default home.\n\n";
		      $oh = $dir;
		      last;
		   }
		}
	     }
	  }
      } if defined $Keys;
  }

  $oh =~ s:\\:/:g if $oh;
  return $oh
}



sub read_sysliblist {
    my $syslibs = (-f "$OH/lib/sysliblist")
	? read_file("$OH/lib/sysliblist")
	: (-f "$OH/rdbms/lib/sysliblist") ? read_file("$OH/rdbms/lib/sysliblist") : '';
    if ($^O eq "hpux") {
       $syslibs =~ s/-l:lib(\w+).(sl|a)\b/-l$1/g;
       $syslibs =~ s/\s*-ldld\b//g;
       $linkwith =~ m/-lcl\b/ or
           $syslibs =~ s/\s*-lcl\b//g;
    }
    return $syslibs;
}


sub perl_is_64bit {
    return defined $Config{use64bitall} ;
}
sub ora_libdir {
    my $libdir = 'lib' ;
    if ( $client_version >= 9 ) {
        $libdir = 'lib32' if ! perl_is_64bit() and -d "$OH/lib32";
        $libdir = 'lib/64' if perl_is_64bit() and -d "$OH/lib/64";
           # Solaris OIC 12+ from pkg
    }
    else {
        $libdir = 'lib64' if   perl_is_64bit() and -d "$OH/lib64";
    }

    return $libdir;
}

sub del_crtobj {
    my $orig = shift;
    my $verbose = shift || $::opt_v;
    my $str = $orig;
    # E.g. for changing the COMPOBJS line (and sometimes LDSTRING)
    # old: COMPOBJS=$(COMPOBJ)/crti.o $(COMPOBJ)/crt1.o $(COMPOBJ)/__fstd.o
    # new: COMPOBJS=$(COMPOBJ)/__fstd.o
    my @del;
    push @del, $1 while $str =~ s:([^\s=]*\bcrt[1in]\.o)\b::;
    if ($orig ne $str) {
	print "Deleted @del from link args.\n" if $verbose;
	print "del_crtobj: $orig\n          : $str\n@del\n" if $::opt_v;
    }
    return $str;
}


sub find_mkfile {

    my @mk_oci32 = (
    	'rdbms/demo/demo_xe.mk',
        'rdbms/demo/demo_rdbms32.mk',
        'rdbms/demo/demo_rdbms.mk',
        'rdbms/lib/ins_rdbms.mk', #Oracle 11 full client
        'sdk/demo/demo.mk' #OIC .mk location
    );
    my @mk_oci64 = (
	'rdbms/demo/demo_xe.mk',
	'rdbms/lib/oracle.mk',
	'rdbms/demo/oracle.mk',
	'rdbms/demo/demo_rdbms.mk',
	'rdbms/demo/demo_rdbms64.mk',
	'rdbms/lib/ins_rdbms.mk', #Oracle 11 full client
    'sdk/demo/demo.mk'        # git issue 20 - path on mac os
    );
    my @mk_oci = perl_is_64bit() ? @mk_oci64 : @mk_oci32;

    # Add build.mk from /usr/share/oracle based on the oracle home location if
    # oracle home is under /usr/lib/oracle (Linux RPM install).
    # The 11g instant client only contains build.mk located in
    # /usr/share/oracle/
    push @mk_oci, "/usr/share/oracle/$1/demo.mk" if ($OH =~ m|^/usr/lib/oracle/(.*)|);

    my @mkplaces = ($::opt_p) ? (@mk_oci) : (@mk_oci);
    if ($::opt_m) {
	$::opt_m = cwd()."/$::opt_m" unless $::opt_m =~ m:^/:;
	die "-m $::opt_m: not found" unless -f $::opt_m;
	unshift @mkplaces, $::opt_m;
    }
    my $mkfile;
    foreach my $place (@mkplaces) {
	$place = "$OH/$place"
	    unless $place =~ m:^[/\.]:; # abs or relative path
	next unless -f $place;
	$mkfile ||= $place;	# use first one found
	print "Found $place\n";
    }
    unless ($^O eq 'MSWin32' || $^O eq 'VMS' || ($mkfile && -f $mkfile) || $::opt_F) {
	$::opt_l or return ($mkfile = undef);
	die qq{
	Unable to locate an oracle.mk or other suitable *.mk
	file in your Oracle installation.  (I looked in
	@mkplaces under $OH)

	The oracle.mk (or demo_rdbms.mk) file is part of the Oracle
	RDBMS product.  You need to build DBD::Oracle on a
	system which has one of these Oracle components installed.
	(Other *.mk files such as the env_*.mk files will not work.)
	Alternatively you can use Oracle Instant Client.

	In the unlikely event that a suitable *.mk file is installed
	somewhere non-standard you can specify where it is using the -m option:
		perl Makefile.PL -m /path/to/your.mk

	See the appropriate troubleshooting guide for your OS for more information and some alternatives.
	};
    }

    print "Using $mkfile\n";
    return $mkfile;
}


sub fetch_oci_macros {
    my $file = shift;

    # Read $file makefile, extract macro definitions from it
    # and store them in $MK, @MK and %MK.

    # Don't include the following definitions in the generated
    # makefile (note that %MK stills gets these values).
    my @edit = qw(
	SHELL CC CPP CFLAGS CCFLAGS OPTIMIZE ASFLAGS RCC LD LDFLAGS
	AR AS CHMOD ECHO EXE OBJS PERL OBJ_EXT LIB_EXT VERSION
    );
    my %edit; @edit{@edit} = ('$_ = ""') x @edit;

    $edit{ORA_NLS} = $edit{ORA_NLS33} = $edit{ORA_NLS32} = q{
	print "Deleting $_\n",
	    "  because it is not already set in the environment\n",
	    "  and it can cause ORA-01019 errors.\n";
	$_ = '';
    } unless $ENV{ORA_NLS} || $ENV{ORA_NLS33} || $ENV{ORA_NLS32}
	|| 1; # Old problem? Let's try without it for a while

    $edit{COMPOBJS} = q{
	# Firstly a Solaris specific edit:
	$_ = del_crtobj($_) if $^O eq 'solaris';

	# Delete any object files in COMPOBJS that don't actually exist
	my $of;
	foreach $of (split(/=|\s+/)) {
	    next if !$of or $of eq "COMPOBJS";
	    my $obj = expand_mkvars($of,0,0);
	    next if -e $obj;
	    print "Deleting $of from COMPOBJS because $obj doesn't exist.\n";
	    s:\Q$of::;
	}
    };

    # deal with (some subversions) of Oracle8.0.3's incompatible use of OBJ_EXT
    my $incompat_ext = ($MK{OBJ_EXT} && $MK{OBJ_EXT} !~ /^\./);
    warn "OBJ_EXT correction enabled ($MK{OBJ_EXT})\n" if $incompat_ext;
    # Don't include compiler options for these compilers
    my @ignore_def = qw( BUILD_CCC296 BUILD_ICC );
    my %ignore_def; @ignore_def{@ignore_def} = ('1') x @ignore_def;

    my $mkver = 0;
    my $lastline = '';
    my @lines = read_inc_file($file);
    for(1; $_ = shift(@lines); $lastline = $_){
	# Join split lines but retain backwack and newlines:
	$_ .= shift @lines while(m/\\[\r\n]+$/);
	chomp;
	push @MK, '' if $_ eq '' and $lastline ne ''; # squeeze multiple blank lines
	next unless $_;

	if ($incompat_ext) {
	    s/\.(\$\(OBJ_EXT\))/$1/g;
	    s/\.(\$\(LIB_EXT\))/$1/g;
	}
	# skip compiler options for undesirable compilers
	m/^ifdef (\w+)/ and do {
		if ($ignore_def{$1}) {
			$_ = shift @lines until m/^endif/;
			next;
		}
	};


        if (m!^([-\w/+.\$()\s]+)\s*:+\s*([^=]*)!) {     # skip targets
            my @tgts = split(/ /, $1);  # multiple target names in Oracle9i's demo_rdbms.mk
	    for (@tgts) { $mk_target_deps{$_} = $2 || '' }
	    my @rules;
            while (@lines && $lines[0] =~ m!^\t! && chomp $lines[0]) {
                my $tmp_line = shift @lines;
                while($tmp_line =~ m!\\$!) {	# continuations!
                    $tmp_line =~ s/\s+\\$/ /;
                    $tmp_line .= shift @lines;
                    chomp($tmp_line);
                }
		push @rules, $tmp_line;
		#print "target @tgts => $mk_target_deps{$tgt} => @{$mk_target_rules{$tgt}}\n";
            }
	    for (@tgts) { push @{ $mk_target_rules{$_} ||= [] }, @rules }
            next;
        }

	next if m!^\t!;					# skip target build rules
	next if m/^\s*\.SUFFIXES/;

	unless($MK{mkver}) {	# still want to get version number
	    my $line = $_; $line =~ s/[\\\r\n]/ /g;
	    $MK{mkver} = $mkver = $1
		if $line =~ m/\$Header:.*?\.mk.+(\d+\.\d+)/;
	}

	# We always store values into %MK before checking %edit
	# %edit can edit this in addition to $_ if needed.
	my $name;
	if (m/^\s*(\w+)\s*(\+?)=\s*/) {
	    $name = $1;
	    my $append = $2;
	    my $value  = $';
	    $value =~ s/^\@`/`/;
	    if ($append) {
		my $expanded = expand_mkvars($value, 0, 1);
		print "Appending '$expanded' to $name\n" if $::opt_v;
		$value = $MK{$name} ? "$MK{$name} $expanded" : $expanded;
	    }
	    elsif ($MK{$name} && $MK{$name} ne $value) {
		print "$name macro redefined by Oracle\n from $MK{$name}\n   to $value\n"
			if $::opt_d;
	    }
	    $MK{$name} = $value;
	    $MK{$name} =~ s/^([^#]*)#.*/$1/; # remove comments

	    if (exists $edit{$name}) {
		my $pre = $_;
		eval $edit{$name};	# execute code to edit $_
		print "Edit $name ($edit{$name}) failed: $@\n" if $@;
		if ($_ ne $pre and $::opt_v) {
		    $_ ? print "Edited $name definition\n from: $pre\n   to: $_\n"
		       : print "Deleted $name definition: $pre\n";
		}
	    }
	}

	push(@MK, $_);
    }

    # --- now decide what to link with ---
    my $linkvia;

    if ($::opt_n) {
	$linkvia = "\$($::opt_n)" if $MK{$::opt_n};
	warn "Can't use '$::opt_n': not defined by .mk files\n"
		unless $linkvia;
    }

    # modern Oracle .mk files define OCISTATICLIBS and OCISHAREDLIBS
    if (!$linkvia && ($MK{OCISHAREDLIBS} || $MK{OCISTATICLIBS})) {
	$linkvia = '';
	if ($MK{OCISTATICLIBS} &&
		(  ($opts{LINKTYPE}||'') eq 'static'
		|| "@ARGV" =~ m/\bLINKTYPE=static\b/
		|| $::opt_c)
    ) {
	    $linkvia .= '$(DEF_ON) '   if $MK{DEF_ON};
	    $linkvia .= '$(SSCOREED) ' if $MK{SSCOREED};
	    $linkvia .= '$(OCISTATICLIBS)';
	}
	else {
	    $linkvia .= '$(SSDBED) '	if $MK{SSDBED};
	    $linkvia .= '$(DEF_OPT) '	if $MK{DEF_OPT};
	    if ($client_version_full =~ /^8\.0\./ and $^O eq 'dec_osf' and $osvers >= 4.0) {
		$linkvia .= '$(SCOREPT) $(NAETAB) $(NAEDHS) $(LLIBRDBMS_CLT) $(LLIBMM) ';
		$linkvia .= '$(NETLIBS) $(CORELIBS) $(LLIBCOMMON) $(LLIBEPC) ';
		$need_ldlp_env = "LD_LIBRARY_PATH";
	    }
	    $linkvia .= '$(OCISHAREDLIBS)';
	}
    }

    $linkvia = '$(LIBCLNTSH)' if !$linkvia && $MK{LIBCLNTSH};

    # The oracle.mk file tends to define OCILDLIBS
    $linkvia = '$(OCILDLIBS)' if !$linkvia && $MK{OCILDLIBS};

    # Now we get into strange land of twisty turny macros
    if (!$linkvia && $MK{PROLDLIBS}) {	# Oracle 7.3.x
	# XXX tweak for threaded perl? - use PROLLSsharedthread
	if ($MK{PROLDLIBS} =~ /thread/i && $MK{PROLLSshared}) {
	    $linkvia = '$(PROLLSshared)';
	}
	else {
	    $linkvia = '$(PROLDLIBS)';
	}
    }
    elsif (!$linkvia && int($mkver) == 1) {
	if ($MK{LLIBOCIC}) {
	    $linkvia = '$(LLIBOCIC) $(TTLIBS)';
	} else {
	    print "WARNING: Guessing what to link with.\n";
	    $linkvia = '-locic $(TTLIBS)';	# XXX GUESS HACK
	}
    }
    elsif (!$linkvia && $MK{CCLIB}) {	# Oracle XE
	$linkvia = '$(CCLIB)';
    }
    unless ($linkvia){
	die "ERROR parsing $file: Unable to determine what to link with.\n"
	."Please send me copies of these files (one per mail message):\n@mkfiles\n";
    }
    $MK_TEXT = join("\n", @MK);
    return $linkvia;
}


sub read_inc_file {
    my $file = shift;
    my $fh;
    unless ($fh = new FileHandle "<$file") {
	# Workaround more oracle bungling (Oracle 7.3.2/Solaris x86)
	my $alt; ($alt = $file) =~ s/\.dk\.mk$/\.mk/;
	$fh = new FileHandle "<$alt";
	die "Unable to read $file: $!" unless $fh;
    }
    print "Reading $file\n";
    my @lines;
    push(@mkfiles, $file);
    while(<$fh>) {
	# soak up while looking for include directives
	push(@lines, $_), next
	    unless /^\s*include\s+(.*?)\s*$/m;
	my $inc_file = $1;
	# deal with "include $(ORACLE_HOME)/..."
	# (can't use expand_mkvars() here)
	$inc_file =~ s/\$\((ORACLE_HOME|ORACLE_ROOT)\)/$ENV{$ORACLE_ENV}/og;
	push(@lines, read_inc_file($inc_file));
    }
    print "Read a total of ".@lines." lines from $file (including inclusions)\n" if $::opt_v;
    return @lines;
}


my %expand_shellescape;
sub expand_shellescape {
    my($orig, $level) = @_;
    my $cmd = $orig;
    my $debug = $::opt_d || $::opt_v;
    print "Evaluating `$orig`\n"
	if $debug && !$expand_shellescape{$orig};
    # ensure we have no $(...) vars left - strip out undefined ones:
    $cmd =~ s/\$[({](\w+)[})]/mkvar("$1", 1, 0, $level+1)/ge;
    print "  expanded `$cmd`\n" if $debug and $cmd ne $orig;
    my $result = `$cmd`;
    $result =~ s/\s+$/ /; # newlines etc to single space
    print "  returned '$result'\n"
	if $debug && !$expand_shellescape{$orig};
    $expand_shellescape{$orig} = $result;
    $result;
}

sub expand_mkvars {
    my ($string, $strip, $backtick, $level, $maxlevel) = @_;

	 return if(!defined $string);

    $level ||= 1;
    local($_) = $string;
    print "$level Expanding $_\n" if $::opt_d;
    # handle whizzo AIX make feature used by Oracle
    s/\$[({] (\w+) \? ([^(]*?) : ([^(]*?) [})]/
	my ($vname, $vT, $vF) = ($1,$2,$3);
	$MK{$vname} = (mkvar($vname, 1, $backtick, $level+1)) ? $vT : $vF
    /xge; # can recurse
    s/\$[({] (\w+) [})]/
	mkvar("$1", $strip, $backtick, $level+1, $maxlevel)
    /xge; # can recurse
    s/`(.*?[^\\])`/expand_shellescape("$1", $level+1)/esg if $backtick; # can recurse
    s/\s*\\\n\s*/ /g;	# merge continuations
    s/\s+/ /g;	# shrink whitespace
    print "$level Expanded $string\n  to       $_\n\n" if $::opt_d and $_ ne $string;
    $_;
}


sub mkvar {
    my($var, $strip, $backtick, $level, $maxlevel) = @_;
    my  $default = $strip ? '' : "\$($var)";
    print "$level Variable: $var\n" if $::opt_d;
    return '$(LIBHOME)' if $var eq 'LIBHOME' && !$strip; # gets noisy
    return $ENV{$ORACLE_ENV} if $var eq 'ORACLE_HOME';
    my $val = $MK{$var};
    if (!defined $val and exists $ENV{$var}) {
	$val = $ENV{$var};
	print "Using value of $var from environment: $val\n"
		unless $var eq 'LD_LIBRARY_PATH';
    }
    return $default unless defined $val;
    if ($MK_expanding{$var}) {
	print "Definition of \$($var) includes \$($var).\n";
	return "\$($var)";
    }
    local($MK_expanding{$var}) = 1;
    return $val if $maxlevel && $level >= $maxlevel;
    return expand_mkvars($val, $strip, $backtick, $level+1, $maxlevel); # can recurse
}


sub read_file {
    my $file = shift;
    unless (open(ROL, "<$file")) {
	warn "WARNING: Unable to open $file: $!\n";
	return "";
    }
    my $text = join "", <ROL>;
    $text =~ s/\n+/ /g;
    close ROL;
    return $text;
}


sub find_bin{
    my $bin = shift;
    my $path_sep = $Config{path_sep};
    foreach (split(/\Q$path_sep/, $ENV{PATH})){
      return "$_/$bin" if -x "$_/$bin";
      {
        # let's try harder
        # see rt#84530 for why we don't go straight for it
        use filetest 'access';
        return "$_/$bin" if -x "$_/$bin";
      }
    }
    return undef;
}


sub find_headers {

    # compensate for case where final .0 isn't in the install directory name
    (my $client_version_trim = $client_version_full) =~ s/\.0$//;

    my @try = grep { -d $_ } (	# search the ORACLE_HOME we're using first
       # --- Traditional full-install locations
       "$OH/rdbms/public", # prefer public over others
       "$OH/rdbms",
       "$OH/plsql", # oratypes.h sometimes here (eg HPUX 11.23 Itanium Oracle 9.2.0),
       # --- Oracle SDK Instant Client locations
       "$OH/sdk/include",
       "$OH/include",
       # --- Oracle RPM Instant Client locations
       map { ( $_, $_."64"   ) }
       map { ( $_, "/usr$_" ) }
       map { "/include/oracle/$_/client" }
       	$client_version,
       	$client_version_trim,
       	$client_version_full,

       #"/include/oracle/$client_version_full/client",       # Instant Client for RedHat FC3
       #"/include/oracle/$client_version_trim/client",       # Instant Client for RedHat FC3
       #"/usr/include/oracle/$client_version/client64",      # Instant Client 11.1 and up
       #"/usr/include/oracle/$client_version/client",        # Instant Client 11.1 and up
       #"/usr/include/oracle/$client_version_full/client64", # Instant Client 64
       #"/usr/include/oracle/$client_version_full/client",   # Instant Client for RedHat FC4
       #"/usr/include/oracle/$client_version_trim/client64", # Instant Client 64
       #"/usr/include/oracle/$client_version_trim/client",   # Instant Client for RedHat FC4
    );

   # Add /usr/include/oracle based on the oracle home location if oracle home is under
   # /usr/lib/oracle ( Linux RPM install ).  The 11g instant client reports
   # client_version as 11.1.0.6 from sqlplus, but installs under 11.1.0.1.
   push @try, "/usr/include/oracle/$1" if ($OH =~ m|^/usr/lib/oracle/(.*)|);

   unshift @try, $::opt_h if $::opt_h;
   @try = grep { -d $_ } @try;

    my %h_file;
    if (@try) {
      find( sub {
         return unless /^o(ci.{3,4}|ratypes)\.h$/i;
         my $dir = $File::Find::dir;
         $h_file{$_} ||= $dir; # record first one found
         print "Found $dir/$_\n" if $::opt_d;
       }, @try);
    }

    my %h_dir = reverse %h_file; # distinct first found dirs
    my @h_dir = keys %h_dir;

    print "Found header files in @h_dir.\n" if @h_dir;

    if (!$h_file{'oratypes.h'} || !$h_file{'ocidfn.h'}) {
	print "\n\n*********************************************************\n";
	print "I can't find the header files I need in your Oracle installation.\n";
	print "You probably need to install some more Oracle components.\n";
	print "For Instant Client that means the SDK package.\n";
	print "I'll keep going, but the compile will probably fail.\n";
	print "See the appropriate troubleshooting guide for your OS for more information.$BELL\n";
	print "*********************************************************\n\n";
	sleep 5;
    }
    return @h_dir;
}


sub get_client_version {
    my ($force_version) = @_;

    my $client_version_full = '';

    my $sqlplus_exe = ($^O eq 'Win32' || $^O eq 'MSWin32' || $^O eq 'cygwin') ? "sqlplus.exe" : "sqlplus";

    # When building under Cygwin, ORACLE_HOME must be a native Windows
    # path so Oracle itself can use it, but it needs to be translated
    # to a Cygwin path so it can be joined into the PATH.
    # Otherwise, the colon in the drive specification (e.g. "c:") is
    # treated as a separator.
    my $OH_path = $OH;
    chomp($OH_path = `/usr/bin/cygpath -u $OH_path`) if $^O eq 'cygwin' && $OH;

    # if we have an ORACLE_HOME then prepend it to the PATH
    local $ENV{PATH} = join $Config{path_sep}, "$OH_path/bin", $OH_path, $ENV{PATH} if $OH;
    print "PATH=$ENV{PATH}\n" if $::opt_v;

    if (find_bin($sqlplus_exe)) {
	local $ENV{SQLPATH} = ""; # avoid $SQLPATH/login.sql causing sqlplus to hang
	# Try to use the _SQLPLUS_RELEASE predefined variable from sqlplus
	# Documented in the SQL*Plus reference guide:
	#  http://download-west.oracle.com/docs/cd/B12037_01/server.101/b12170/ch13.htm#i2675128
	# Output is in the following format:
	#   DEFINE _SQLPLUS_RELEASE = "902000400" (CHAR)       Representing 9.2.0.4.0
	#   DEFINE _SQLPLUS_RELEASE = "1001000200" (CHAR)      Representing 10.1.0.2.0
	open FH, ">define.sql" or warn "Can't create define.sql: $!";
	print FH "DEFINE _SQLPLUS_RELEASE\nQUIT\n";
	close FH;
	my $sqlplus_release = `$sqlplus_exe -S /nolog \@define.sql 2>&1`;
	if ($sqlplus_release =~ /(SP2-0750)|(SP2-0642)/) {


	                my $x = $ENV{ORACLE_HOME};
	                delete $ENV{ORACLE_HOME};
	                $sqlplus_release = `$sqlplus_exe -S /nolog \@define.sql 2>&1`;
	                $ENV{ORACLE_HOME} = $x;
        }
	unlink "define.sql";
	print $sqlplus_release; # the _SQLPLUS_RELEASE may not be on first line:
	if ($sqlplus_release =~ /DEFINE _SQLPLUS_RELEASE = "(\d?\d)(\d\d)(\d\d)(\d\d)(\d\d)"/) {
	    $client_version_full = sprintf("%d.%d.%d.%d", $1, $2, $3, $4);
	}
	else {
	    my $ldlib_note = ($Config{ldlibpthname})
	    	? "Specifically, your $Config{ldlibpthname} environment variable"
		: "Many systems need an environment variable (such as LD_LIBRARY_PATH, DYLD_LIBRARY_PATH)";
	    warn qq{
	    If sqlplus failed due to a linker/symbol/relocation/library error or similar problem
	    then it's likely that you've not configured your environment correctly.
	    $ldlib_note
	    set to include the directory containing the Oracle libraries.
	    \a\n};
    	    sleep 5;
	}
    }
    else {
	warn "Can't find sqlplus. Pity, it would have helped.\n";
    }

    if (!$client_version_full && $OH && open INST, "<$OH/install/unix.rgs") {
	local $/ = undef;
	<INST> =~ m/^(rdbms|sql\*plus)\s+([\d.]+)/m;
	$client_version_full = $2 if $2;
	close INST;
    }

    if (!$client_version_full && $OH && -x "$OH/orainst/inspdver" ) {
	open INST, "$OH/orainst/inspdver |"; # client only install does not have this
	my @inspdver = <INST>;
	close INST;
	foreach (@inspdver) {
	    $client_version_full = $1 if m/^(\d+\.\d+\.\d+)\S*\s+.*RDBMS/;
	    next unless $::opt_v
		or 	m/RDBMS/i	or m/PL.SQL/i
		or 	m/Precomp/i	or m/Pro\*C/i;
	    print $_;
	}
    }
    if (!$client_version_full) {
	print "I'm having trouble finding your Oracle version number... trying harder\n"
	    unless $force_version;
	if ( $OH =~ m![^\d\.]((?:8|9|1\d)\.\d+\.\d+(\.\d+)?)! ) { #decode it from $OH if possible
	    $client_version_full = $1;
	}
	elsif ( "$OH/" =~ m!\D(8|9|10)(\d)(\d?)\D!) { # scary but handy
	    $client_version_full = join ".", $1, $2, ($3||'0');
	}
	elsif ( "$OH/" =~ m!/10g!) { # scary but handy
	    $client_version_full = "10.0.0.0";
	}
   elsif ( "$OH/" =~ m!/usr/lib/oracle/(\d+\.\d)/!) { # Linux RPM
       $client_version_full = "$1.0.0";
   }
    }

    if ($force_version && $force_version ne $client_version_full) {
	print "Forcing Oracle version to be treated as $force_version\n";
	$client_version_full = $force_version;
    }

    if ($client_version_full && $client_version_full !~ m/^(7|8|9|1\d)\.\d+/
    ) {
	print "Oracle version seems to be $client_version_full but that looks wrong so I'll ignore it.\n";
	$client_version_full = "";
    }

    if (!$client_version_full) {

      # set a supported client version as default
      $client_version_full = "9.2.0.4.0";

      print qq{
WARNING: Could not determine Oracle client version, defaulting to
version $client_version_full. Some features of DBD::Oracle may not work.
Oracle version-based logic in Makefile.PL may produce erroneous
results. You can use "perl Makefile.PL -V X.Y.Z" to specify your
client version.\n
};
      # pause for focus
      sleep 3;
    }

    # hack up a simple floating point form of the version: 8.1.6.2 => 8.1
    ($client_version = $client_version_full) =~ s/^(\d+\.\d+).*/$1/;

    print "Oracle Version $client_version_full ($client_version)\n";

    return $client_version unless wantarray;
    return ($client_version, $client_version_full);
}


sub symbol_search {
    $::opt_s ||= $::opt_S;
    print "Searching for symbol '$::opt_s' in $OH ...\n";
    my $dlext = $Config{dlext};
    system(qq{	cd $OH; for i in lib/*.[ao] lib/*.$dlext */lib/*.[ao];
	do echo "  searching oracle \$i ..."; PATH=/usr/ccs/bin:\$PATH nm \$i | grep $::opt_s; done
    });
    if ($::opt_S) {
	my @libpth = split ' ', $Config{libpth};
	print "Searching for symbol '$::opt_s' in @libpth ...\n";
	@libpth = map { ("$_/lib*.$dlext", "$_/lib*.a") } @libpth;
	system(qq{	cd $OH; for i in @libpth;
	    do echo "  searching \$i ..."; PATH=/usr/ccs/bin:\$PATH nm \$i | grep $::opt_s; done
	});
    }
    print "Search done.\n";
    print "(Please only include the 'interesting' parts when mailing.)\n";
    exit;
}


# =====================================================================


{
    package MY; # SUPER needs package context, $self is not sufficient

    use strict;
    use Config;

    sub libscan {
       my($self, $path) = @_;
       return '' if $path =~ m/\.pl$/;
       $path;
    }


    sub post_initialize {
	my $self = shift;

	print "\nNote: \$ORACLE_HOME/lib must be added to your $need_ldlp_env environment variable\n",
	      "before running \"make test\" and whenever DBD::Oracle is used.\n\n"
	    if $need_ldlp_env && ($ENV{$need_ldlp_env}||'') !~ m:\Q$OH/lib\b:;

	eval {	# This chunk is for Oracle::OCI
	    require Data::Dumper;
	    print main::MK_PM Data::Dumper->Purity(1)->Terse(0)->Indent(1)->Useqq(1)
		->Dump([\%opts, $self], [qw(dbd_oracle_mm_opts dbd_oracle_mm_self)]);
	};
	if ($@) {
	    warn "Can't dump config to mk.pm so you won't be able to build Oracle::OCI later if you wanted to: $@\n";
	    print main::MK_PM qq{die "You need to reinstall DBD::Oracle after installing Data::Dumper\n"; };
	}
	close main::MK_PM or die "Error closing mk.pm: $!\n";

	foreach (qw(mk.pm Oracle.h dbdimp.h ocitrace.h)) {
	    $self->{PM}->{$_} = '$(INST_ARCHAUTODIR)/'.$_;
	}

	# Add $linkwith to EXTRALIBS for those doing static linking
	$self->{EXTRALIBS} .= " -L\$(LIBHOME)";
	$self->{EXTRALIBS} .= " $linkwith" if $linkwith;

	'';
    }


    sub postamble {
	return main::dbd_postamble(@_);
    }


    sub const_loadlibs {
	my $self = shift;

	# ExtUtils::MM_Unix v1.50 (invoked by ExtUtils::MakeMaker)
	# requires that $self->{LD_RUN_PATH} be defined and not be
	# an empty string for Makefile to specify its use during the
	# build. This is required by both SUPER::const_loadlibs
	# and SUPER::dynamic_lib. hence it is best if we define
	# or modify $self->{LD_RUN_PATH} here *before* calling
	# SUPER::const_loadlibs.


	# edit LD_RUN_PATH ...
	my ($ldrp) = $self->{LD_RUN_PATH};
	# remove redundant /lib or /usr/lib as it can cause problems
	$ldrp =~ s!:(/usr)?/lib$!! if $ldrp;
    # if it's empty then set it manually
    #Lincoln: if pick the right library path
    my $libdir = main::ora_libdir();
    $ldrp ||= "$OH/$libdir:$OH/rdbms/$libdir";
	$self->{LD_RUN_PATH} = $ldrp;

	local($_) = $self->SUPER::const_loadlibs(@_);

	print "Ignoring LD_RUN_PATH='$ENV{LD_RUN_PATH}' in environment\n" if $ENV{LD_RUN_PATH};
	print "LD_RUN_PATH=$ldrp\n";
	return $_;
    }


    sub post_constants {
	my $self = shift;
	return '' unless $::opt_f;
	# Oracle Definitions, based on $(ORACLE_HOME)/proc/lib/proc.mk
	'
###################################################################
#
ORACLE_HOME = '.$OH.'

# The following text has been extracted from '.join("\n#\t", '', @mkfiles).'

'.$MK_TEXT.'

# End of extract from '."@mkfiles".'
#
###################################################################
';
    }


    sub const_cccmd {
	my ($self) = shift;
	print "Using DBD::Oracle $self->{VERSION}.\n";

	local($_) = $self->SUPER::const_cccmd(@_);
	# If perl Makefile.PL *-g* then switch on debugging
	if ($::opt_g) {
	   if ($^O eq "MSWin32" and $Config::Config{cc} eq 'cl') {
	      s/\s-/ -Zi -/;
	      s/-O1//;
	   } else {
	    s/\s-O\d?\b//;	# delete optimise option
	    s/\s-/ -g -/;	# add -g option
	   }
	}
	# are we using the non-bundled hpux compiler?
	if ($^O eq "hpux" and $Config::Config{ccflags} =~ /-Aa\b/) {
	    print "Changing -Aa to -Ae for HP-UX in ccmd.\n"
	     if s/-Aa\b/-Ae/g;	# allow "long long" in oratypes.h
	}

	$_;
    }

    sub cflags {
	my ($self) = shift;
	local($_) = $self->SUPER::cflags(@_);
	# If perl Makefile.PL *-g* then switch on debugging
	if ($::opt_g) {
	   if ($^O eq "MSWin32" and $Config::Config{cc} eq 'cl') {
	      s/\s-/ -Zi -/;
	      s/-O1//;

	   } else {
	      s/\s-O\d?\b//;	# delete optimise option
  	      s/\s-/ -g -/;	# add -g option
	   }
	}
	# are we using the non-bundled hpux compiler?
	if ($^O eq "hpux" and $Config::Config{ccflags} =~ /-Aa\b/) {
	    print "Changing -Aa to -Ae for HP-UX in cflags.\n"
	     if s/-Aa\b/-Ae/g;	# allow "long long" in oratypes.h
	}
	$_;
    }

    sub dynamic_lib {
	my($self) = shift;

	unless ($^O eq 'VMS') {
		my $m = $self->SUPER::dynamic_lib(@_);
		if ($^O eq 'darwin') {
			$m =  "NMEDIT = nmedit\n" . $m .
			      "\t\$(NMEDIT) -R ./hints/macos_bundle.syms \$(INST_DYNAMIC) || true\n";
		}
		elsif (($^O eq 'hpux') and ($osvers <11)) {
		 	$m =~ s/LD_RUN_PATH=(\S+)\s+(\S+)/$2 -Wl,+b $1/;

		}
		return ($m);
	}

	# special code for VMS only
	my(%attribs) = @_;
	return '' unless $self->needs_linking(); #might be because of a subdir
	return '' unless $self->has_link_code();

	my $OtherText;
	my($otherldflags) = $attribs{OTHERLDFLAGS} || "";
	my($inst_dynamic_dep) = $attribs{INST_DYNAMIC_DEP} || "";
	my @m;
	push @m, "OTHERLDFLAGS = $otherldflags\n";
	push @m, "INST_DYNAMIC_DEP = $inst_dynamic_dep\n";
    if ($] < 5.00450) {
	push @m, '
$(INST_DYNAMIC) : $(INST_STATIC) $(PERL_INC)perlshr_attr.opt rtls.opt $(INST_ARCHAUTODIR).exists $(EXPORT_LIST) $(PERL_ARCHIVE) $(INST_DYNAMIC_DEP)
	$(NOECHO) $(MKPATH) $(INST_ARCHAUTODIR)
	$(NOECHO) If F$TrnLNm("PerlShr").eqs."" Then Define/NoLog/User PerlShr Sys$Share:PerlShr.',$Config::Config{'dlext'},'
	Lnproc $(MMS$TARGET)$(OTHERLDFLAGS) $(BASEEXT).opt/Option,rtls.opt/Option,$(PERL_INC)perlshr_attr.opt/Option i
';
  } else {
	push @m, '
$(INST_DYNAMIC) : $(INST_STATIC) $(PERL_INC)perlshr_attr.opt $(INST_ARCHAUTODIR).exists $(EXPORT_LIST) $(PERL_ARCHIVE) $(INST_DYNAMIC_DEP)
	$(NOECHO) $(MKPATH) $(INST_ARCHAUTODIR)
	$(NOECHO) If F$TrnLNm("PerlShr").eqs."" Then Define/NoLog/User PerlShr Sys$Share:PerlShr.',$Config::Config{'dlext'},'
	Lnproc $(MMS$TARGET)$(OTHERLDFLAGS) $(BASEEXT).opt/Option,$(PERL_INC)perlshr_attr.opt/Option i
';

  }
	push @m, $self->dir_target('$(INST_ARCHAUTODIR)');
	join('',@m);
    }

}


sub ldlibpth_info {
    my $verbose = shift;

    my ($ldlibpthname, $val);

    if ($^O eq "hpux") { # hpux is odd again: can use two env vars
        my @envs = grep { $ENV{$_} } qw( LD_LIBRARY_PATH SHLIB_PATH );
        $ldlibpthname = join "/", @envs;
        $val          = join $Config{path_sep}, @ENV{@envs}
    }
    else {
        $ldlibpthname = $Config{ldlibpthname} or return;
        $val          = $ENV{$ldlibpthname} || '';
    }

    print "Your $ldlibpthname env var is set to '$val'\n" if $verbose;

    return ( $ldlibpthname, $val, split /\Q$Config{path_sep}/, $val );
}


sub check_ldlibpthname {
    my $libdir = shift || join '/', $OH, ora_libdir();

    $libdir =~ s:[\\/]$::;  # cut final / or \

    my ($ldlibpthname, $val, @dirs) = ldlibpth_info(1);

    my $warn_name = $ldlibpthname;

    # It is also ok if we are using symbolic links
    return 1 if grep { s:[\\/]$::; $_ eq $libdir }
                map  { $_, Cwd::abs_path($_) } @dirs;

    # on solaris, it can be under LD_LIBRARY_PATH_(32|64)
    if ( $^O eq 'solaris' ) {
        my $ld_library_path_name = 'LD_LIBRARY_PATH_'
                                 . ( perl_is_64bit() ? '64' : '32' );

        $warn_name .= " or $ld_library_path_name";

        my @dirs = split quotemeta($Config{path_sep}),
                         $ENV{$ld_library_path_name};

        s#[\\/]$## for @dirs;  # cut potential final / or \

        return 1 if grep { $_ eq $libdir } @dirs;
    }

    warn "WARNING: Your $warn_name env var doesn't include '$libdir' but probably needs to.\n";
}


sub check_security {
    # check for the SUID/SGID bits on ORACLE_HOME/bin/oratclsh
    # if set, this allows a user to fork a root shell!
    # Get the octal portion of perms that indicates
    # SUID and SGID, and warn if either is set

    my @files = map { ($_,$_.'0') } qw(
	oratclsh lsnrctl oemevent onrsd osslogin tnslsnr
	tnsping trcasst trcroute cmctl cmadmin cmgw names namesctl otrccref
	otrcfmt otrcrep otrccol
    );

    my @bad;
    foreach (@files) {
	my $file = "$ENV{ORACLE_HOME}/bin/$_";
	my ($mode) = (stat($file))[2];
	next unless defined $mode;
	push @bad, $file if ($mode & 04000 and $mode & 00111)
			 or ($mode & 02000 and $mode & 00111);
    }
    return unless @bad;

    print "\n";
    warn "***  WARNING - YOUR ORACLE INSTALLATION HAS A SECURITY PROBLEM.$BELL\n";
    warn "     This is just a warning. It does not affect DBD::Oracle in any way.\n\n";
    sleep 6;
}

sub check_macos_symbol_table {
    # Check for symbol table problem in libclntsh.dylib.9.0 on MacOS X
    return unless $^O eq 'darwin';
    my $oracle_lib = "$OH/lib/libclntsh.dylib";

    return unless -f $oracle_lib;

    open my $nm_fh, '-|', "nm $oracle_lib"
        or die "couldn't run 'nm $oracle_lib': $!";

    while ( <$nm_fh> ) {
        return if /^\s+U _(dlsym|dlclose)/;
    }

	warn <<"END_WARNING";
WARNING: symbol table may need modification in Oracle library:
    $oracle_lib
If the build fails in the linking stage, manual modification is
required - see DBD::Oracle::Troubleshooting::Macos
END_WARNING

    return;
}

__END__

