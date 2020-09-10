#!/bin/bash

set -ex

if [ "$ORACLEV" == "11.2" ]; then
    export LONGV="11.2.0.4.0"
fi
if [ "$ORACLEV" == "12.2" ]; then
    export LONGV="12.2.0.1.0"
fi
if [ "$ORACLEV" == "18.3" ]; then
    export LONGV="18.3.0.0.0"
fi
if [ "$ORACLEV" == "18.5" ]; then
    export LONGV="18.5.0.0.0"
fi
if [ "$ORACLEV" == "19.6" ]; then
    export LONGV="19.6.0.0.0"
fi

SUFFIX=""
if [ -n "$ORACLEV" ]; then
    if [[ "$ORACLEV" == "18.3" || "$ORACLEV" == "18.5" || "$ORACLEV" == "19.6" ]]; then
        SUFFIX="dbru"
    fi
    echo "Installing Oracle SDK $ORACLEV"
    mkdir /etc/oracle
    mkdir -p "/usr/lib/oracle/$ORACLEV/client/bin"
    mkdir -p "/usr/lib/oracle/$ORACLEV/client/lib"
    mkdir -p "/usr/include/oracle/$ORACLEV/client"
    mkdir -p "/usr/share/oracle/$ORACLEV/client"
    pushd `pwd`
    cd /usr/lib/oracle
    for i in "basic" "sdk" "sqlplus"; do
        # Repo intended for Dockerfiles, see https://github.com/bumpx/oracle-instantclient/blob/master/README.md
        wget --quiet "https://github.com/bumpx/oracle-instantclient/raw/master/instantclient-$i-linux.x64-$LONGV$SUFFIX.zip"
    done
    for i in `ls *zip`; do unzip $i; done
fi
if [[ "$ORACLEV" == "12.2" || "$ORACLEV" == "18.3" || "$ORACLEV" == "18.5" || "$ORACLEV" == "19.6" ]]; then
    STUB=$(echo $ORACLEV | sed 's/\./_/')
    MAJOR=$(echo $ORACLEV | sed 's/\.[0-9]//')
    echo "# Moving contents of instantclient-basic-linux.x64-$LONGV$SUFFIX.zip"
    find "instantclient_$STUB"
    mv "instantclient_$STUB/adrci"   "$ORACLEV/client/bin/"
    mv "instantclient_$STUB/genezi"  "$ORACLEV/client/bin/"
    mv "instantclient_$STUB/uidrvci" "$ORACLEV/client/bin/"
    mv instantclient_$STUB/{libclntshcore.so.$MAJOR.1,libclntsh.so.$MAJOR.1,libipc1.so,libmql1.so,libnnz$MAJOR.so,libocci.so.$MAJOR.1,libociei.so,libocijdbc$MAJOR.so,liboramysql$MAJOR.so,ojdbc8.jar,xstreams.jar} $ORACLEV/client/lib/
    if [ "$MAJOR" != "19" ]; then
        mv "instantclient_$STUB/libons.so" "$ORACLEV/client/lib/"
    fi
    echo "# Moving contents of instantclient-sqlplus-linux.x64-$LONGV.zip"
    mv "instantclient_$STUB/sqlplus" "$ORACLEV/client/bin/"
    mv "instantclient_$STUB/glogin.sql" "instantclient_$STUB/libsqlplus.so" "instantclient_$STUB/libsqlplusic.so" "$ORACLEV/client/lib/"
    echo "# Moving contents of instantclient-sdk-linux.x64-$LONGV$SUFFIX.zip"
    mv instantclient_$STUB/sdk/include/*h /usr/include/oracle/$ORACLEV/client/
    mv instantclient_$STUB/sdk/demo/* /usr/share/oracle/$ORACLEV/client/
    mv instantclient_$STUB/sdk/ott /usr/share/oracle/$ORACLEV/client/
    mv instantclient_$STUB/sdk/ottclasses.zip $ORACLEV/client/lib/ottclasses.zip
    ln -s "libclntshcore.so.$MAJOR.1" "$ORACLEV/client/lib/libclntshcore.so"
    ln -s "libclntsh.so.$MAJOR.1"     "$ORACLEV/client/lib/libclntsh.so"
    ln -s "libocci.so.$MAJOR.1"       "$ORACLEV/client/lib/libocci.so"
    echo "# FYI What wasnt moved from Oracle zip files:"
    find "instantclient_$STUB"
    echo "# Clean up"
    rm -rf "instantclient_$STUB"
fi
if [ "$ORACLEV" = "11.2" ]; then
    echo "# Moving contents of instantclient-basic-linux.x64-$LONGV.zip"
    mv instantclient_11_2/adrci $ORACLEV/client/bin/
    mv instantclient_11_2/genezi $ORACLEV/client/bin/
    mv instantclient_11_2/uidrvci $ORACLEV/client/bin/
    mv instantclient_11_2/{libclntsh.so.11.1,libnnz11.so,libocci.so.11.1,libociei.so,libocijdbc11.so,ojdbc5.jar,ojdbc6.jar,xstreams.jar} $ORACLEV/client/lib/
    echo "# Moving contents of instantclient-sqlplus-linux.x64-$LONGV.zip"
    mv instantclient_11_2/sqlplus $ORACLEV/client/bin/
    mv instantclient_11_2/glogin.sql instantclient_11_2/libsqlplus.so instantclient_11_2/libsqlplusic.so $ORACLEV/client/lib/
    echo "# Moving contents of instantclient-sdk-linux.x64-$LONGV.zip"
    mv instantclient_11_2/sdk/include/*h /usr/include/oracle/$ORACLEV/client/
    mv instantclient_11_2/sdk/demo/* /usr/share/oracle/$ORACLEV/client/
    mv instantclient_11_2/sdk/ott /usr/share/oracle/$ORACLEV/client/
    mv instantclient_11_2/sdk/ottclasses.zip $ORACLEV/client/lib/ottclasses.zip
    ln -s libclntsh.so.11.1 $ORACLEV/client/lib/libclntsh.so
    ln -s libocci.so.11.1 $ORACLEV/client/lib/libocci.so
    echo "# FYI What wasnt moved from Oracle zip files:"
    find instantclient_11_2
    echo "# Clean up"
    rm -rf instantclient_11_2
fi
if [ -n "$ORACLEV" ]; then
    echo "# Place paths in ENV"
    echo "export ORACLE_HOME=/usr/lib/oracle/$ORACLEV/client" >> /etc/profile.d/oracle.sh
    echo "export PATH=\$PATH:\$ORACLE_HOME/bin" >> /etc/profile.d/oracle.sh
    echo "export TNS_ADMIN=/etc/oracle" >> /etc/profile.d/oracle.sh
    echo "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:\$ORACLE_HOME/lib" >> /etc/profile.d/oracle.sh
    echo "/usr/lib/oracle/$ORACLEV/client/lib" > /etc/ld.so.conf.d/oracle-instantclient.conf
    echo "# Make sure stuff is +x"
    chmod +x $ORACLEV/client/bin/*
    ldconfig
    popd
    cat /etc/profile
fi
