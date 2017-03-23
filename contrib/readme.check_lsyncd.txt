This two scripts intended to be used for Nagios monitoring of file sync process.

check_lsyncd_updatefile.sh - updates some file contents and put current timestamp ( date +%s ) there, thus emitting signal. Should be used from cron on source storage, say every one minute.

check_lsyncd_fileupdated.sh - reads specified file, extracts timestamp from it and compares with current time. If time delta is greater than threshhold, warning or critical exit status will be set. Should be used on destination storages, via, for example, NRPE.
