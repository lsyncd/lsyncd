#!/bin/bash
# vim:ts=4:sw=4:et:

DEFAULT_FNAME='/tmpmediasync/.syncts'
DEFAULT_DELTA=120
DEFAULT_DELTA_WARNING=150
DEFAULT_DELTA_CRITICAL=300

PROGNAME=$(basename $0)
LOGGER="logger -t $PROGNAME --"
STUBTEXT="lsyncd local part sync"

#nagios constants
STATE_OK=0
STATE_WARNING=1
STATE_CRITICAL=2
STATE_UNKNOWN=3
STATE_DEPENDENT=4

ncode=$STATE_OK
ntext=""


function mylog() {
    if [ -z "$1" ];then return 0;fi #exiting if msg is empty
    if [ "x$1" == "x-e" ];then
        echo "$2"
        $LOGGER "$2"
    else
        $LOGGER "$1"
    fi
}


print_usage() {

    echo "Usage: $PROGNAME -f <path to filename> [-w <value] [-c <value>]"
    echo ""
    echo "-f     file to read timestamp from. default file is $DEFAULT_FNAME"
    echo "-w     delta warning level. If delta greater than this value, warning status will be returned. Default value: $DEFAULT_DELTA_WARNING"
    echo "-c     delta critical level. If delta greater than this value, critical status will be returned. Default value: $DEFAULT_DELTA_CRITICAL"
    echo "       use -h to show this help"
}

print_help() {
    echo ""
    print_usage
    echo ""
    echo "this script should read timestamp from file, which may be used to check if lsyncd is synchronizing remote storage with local server in time"
}
# parsing arguments

while getopts ":hf:w:c:" Option; do
  case $Option in
    h)
      print_help
      exit $STATE_UNKNOWN
      ;;
    f)
      tsfile="${OPTARG}"
      ;;
    D)
      deltaval="${OPTARG}"
      ;;
    w)
      deltawarn="${OPTARG}"
      ;;
    c)
      deltacrit="${OPTARG}"
      ;;
    *)
      print_help
      exit $STATE_UNKNOWN
      ;;
  esac
done
shift $(($OPTIND - 1))


tsfile=${tsfile:-$DEFAULT_FNAME}
deltaval=${deltaval:-$DEFAULT_DELTA}
deltawarn=${deltawarn:-$DEFAULT_DELTA_WARNING}
deltacrit=${deltacrit:-$DEFAULT_DELTA_CRITICAL}

curts=$(date +%s)

filets=$(cut -f 1 < $tsfile)
curdelta=$(( $curts - $filets ))
tshuman=$(date -d @${filets})

ntext="difference is $curdelta seconds, last update on: $tshuman"


if [ $curdelta -ge $deltawarn ] ;then
    ncode=$STATE_WARNING
fi
if [ $curdelta -ge $deltacrit ];then
    ncode=$STATE_CRITICAL
fi

case $ncode in
    $STATE_OK)
        nprefix="OK"
        ;;
    $STATE_WARNING)
        nprefix="WARNING"
        ;;
    $STATE_CRITICAL)
        nprefix="CRITICAL"
        ;;
    $STATE_UNKNOWN)
        nprefix="UNKNOWN"
        ;;
    $STATE_DEPENDENT)
        nprefix="DEPENDENT"
        ;;
    *)
        nprefix="unknown code"
        ;;
esac

ntext="$STUBTEXT: $ntext"
echo "${nprefix}: ${ntext}"
exit $ncode

