FROM perl:5.32-threaded-buster

# This will install Oracle XE database and the Oracle SDK to make developing easier
# It leans on the same scripts used by travis, so they can be tweaked here too

# build then run this throw away docker image with:
# cd DBD-Oracle
# docker build -f maint/Dockerfile  . -t testoracle
# docker run -it testoracle
# perl Makefile.PL
# make
# make test

# Adjust the FROM line to pick a perl version and distro. See also https://hub.docker.com/_/perl
# Adjust the two variables below to set the Oracle XE server version and client version

ENV ORACLEDBV=11.2 \
    ORACLEV=19.6

# These are used by DBD::Oracle unit tests
ENV ORACLE_USERID=kermit/foobar \
    ORACLE_USERID_2=mspiggy/barfoo \
    ORACLE_DSN='dbi:Oracle://localhost:1521/XE'

COPY . /usr/src/DBD-Oracle
RUN apt-get update && apt-get install -y libaio-dev && \
    apt-get install -y vim less net-tools && \
    cpanm --verbose --notest --installdeps /usr/src/DBD-Oracle && \
    /usr/src/DBD-Oracle/maint/travis-ci_scripts/01_install_oracle_xe.bash && \
    /usr/src/DBD-Oracle/maint/travis-ci_scripts/02_install_oracle_instantclient.bash && \
    sync && /etc/init.d/oracle-xe stop && apt-get clean

WORKDIR /usr/src/DBD-Oracle

CMD /usr/src/DBD-Oracle/maint/docker-init.sh
