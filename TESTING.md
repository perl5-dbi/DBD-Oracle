# Variables used in tests

**Connecting to Oracle**

`ORACLE_USERID` - important

Which user & password to use when running tests against a real Oracle database

Should be 'user/password' or a longer string with connection details.

``` bash
$ export ORACLE_USERID='scott/tiger'
```

`ORACLE_USERID_2`

Provides a second set of user credentials when needed

`ORACLE_DSN`

DSN details when connecting to real Oracle for tests

``` bash
$ export ORACLE_DSN='dbi:Oracle:testdb'
```

`DBI_DSN`

If `ORACLE_DSN` is not provided, this will be used. Otherwise falls back to internal default.

**Creation of tables, views, functions etc**

`DBD_ORACLE_SEQ` - important

Appended to table, view, function (etc) that are created during testing to help
prevent collisions

`DBD_SKIP_TABLE_DROP`

Skips dropping temporary tables when tests complete. Use to examine the mess after failed tests.

**Other**

`NLS_DATE_FORMAT`

Sets the date format as normal

# Test all script

The mkta.pl script might be useful as for testing against multiple DB's quickly

# Vars that need more detail

`NLS_NCHAR`

`NLS_LANG`

???

`DBI_USER`

`DBI_PASS`

`DBD_ALL_TESTS` - Forces some tests to run, that otherwise wouldn't
