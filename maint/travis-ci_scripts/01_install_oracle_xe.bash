#!/bin/bash

set -ex

# 11.2
if [ "$ORACLEDBV" = "11.2" ]; then

# wget --content-disposition https://packagecloud.io/mojotech/cloud/packages/debian/jessie/oracle-xe_11.2.0-1.0_amd64.deb/download.deb

## This is a pile of sucky suck suck

pwd
wget --quiet https://raw.githubusercontent.com/wnameless/docker-oracle-xe-11g/master/assets/oracle-xe_11.2.0-1.0_amd64.debaa
wget --quiet https://raw.githubusercontent.com/wnameless/docker-oracle-xe-11g/master/assets/oracle-xe_11.2.0-1.0_amd64.debab
wget --quiet https://raw.githubusercontent.com/wnameless/docker-oracle-xe-11g/master/assets/oracle-xe_11.2.0-1.0_amd64.debac
cat oracle-xe_11.2.0-1.0_amd64.deba* > oracle-xe_11.2.0-1.0_amd64.deb
sha256sum oracle-xe_11.2.0-1.0_amd64.deb
dpkg --install oracle-xe_11.2.0-1.0_amd64.deb

# Hack needed because oracle configuration looks for awk in /bin instead of $PATH
ln -s /usr/bin/awk /bin/awk
# Oracle also needs this:
mkdir -p /var/lock/subsys

# Docker containers and stuff dont work with memory_target
perl -pi -e 's/^(memory_target=.*)/#$1/' /u01/app/oracle/product/11.2.0/xe/config/scripts/init.ora
perl -pi -e 's/^(memory_target=.*)/#$1/' /u01/app/oracle/product/11.2.0/xe/config/scripts/initXETemp.ora

# These seem to be needed too
echo -e "pga_aggregate_target=200540160\nsga_target=601620480" >> /u01/app/oracle/product/11.2.0/xe/config/scripts/init.ora
echo -e "pga_aggregate_target=200540160\nsga_target=601620480" >> /u01/app/oracle/product/11.2.0/xe/config/scripts/initXETemp.ora

# Now configure Oracle XE
printf 8080\\n1521\\nadminpass\\nadminpass\\ny\\n | /etc/init.d/oracle-xe configure

# Replace the containers hostname with 0.0.0.0
sed -i 's/'$(hostname)'/0.0.0.0/g' /u01/app/oracle/product/11.2.0/xe/network/admin/listener.ora
sed -i 's/'$(hostname)'/0.0.0.0/g' /u01/app/oracle/product/11.2.0/xe/network/admin/tnsnames.ora

sqlplusbin="/u01/app/oracle/product/11.2.0/xe/bin/sqlplus"
# ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" $sqlplusbin -H
USER1=`echo $ORACLE_USERID|sed 's/\/.*//'`
USER2=`echo $ORACLE_USERID_2|sed 's/\/.*//'`
PASS1=`echo $ORACLE_USERID|sed 's/.*\///'`
PASS2=`echo $ORACLE_USERID_2|sed 's/.*\///'`

echo "create user $USER1 identified by $PASS1;" \
    | ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" \
    $sqlplusbin -L -S SYSTEM/adminpass
ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" $sqlplusbin -L -S SYSTEM/adminpass <<< "grant connect,resource to $USER1;"

echo "create user $USER2 identified by $PASS2;" \
    | ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" \
    $sqlplusbin -L -S SYSTEM/adminpass
ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" $sqlplusbin -L -S SYSTEM/adminpass <<< "grant connect,resource to $USER2;"

# ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" $sqlplusbin -L -S SYSTEM/adminpass @/dev/stdin <<< "create database dbusasci character set US7ASCII national character set utf8 undo tablespace undotbs1 default temporary tablespace temp;"
# ORACLE_SID=XE ORACLE_HOME="/u01/app/oracle/product/11.2.0/xe" $sqlplusbin -L -S SYSTEM/adminpass @/dev/stdin <<< "create database dbutf character set AL32UTF8 national character set AL16UTF16 undo tablespace undotbs1 default temporary tablespace temp;"


fi


if [ "$ORACLEDBV" = "18" ]; then

echo "TODO See https://github.com/fuzziebrain/docker-oracle-xe/blob/master/Dockerfile"

fi
