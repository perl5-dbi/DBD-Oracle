# Docker Development Helper

This will take a default perl container and install an Oracle XE database and an Oracle SDK to make
developing easier.

## Usage

Build then run this throw away docker image with:
```
cd DBD-Oracle
docker build -f maint/Dockerfile  . -t testoracle
docker run -it testoracle
perl Makefile.PL
make
make test
```

## Options

- Adjust the FROM line to pick a perl version and distro. See also https://hub.docker.com/_/perl

- Adjust the two variables below to set the Oracle XE server version and client sdk version.
```
ENV ORACLEDBV=11.2 \
    ORACLEV=19.8
```
- Set `ORACLEV=""` to skip the SDK install

