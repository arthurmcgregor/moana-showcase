#!/bin/sh
set -e

# Install PostGIS into template1 as well as the default database. Django's test
# runner creates the throwaway "test_<NAME>" database from template1, so putting
# the extension there is what makes GIS columns work in the test DB — there is no
# CreateExtension migration in this project.
psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname template1 <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS postgis;
EOSQL

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    CREATE EXTENSION IF NOT EXISTS postgis;
EOSQL
