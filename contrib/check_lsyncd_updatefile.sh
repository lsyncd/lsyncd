#!/bin/bash
DEFAULT_FNAME='/data/nfs/media/.syncts'
PROGNAME=$(basename $0)
LOGGER="logger -t $PROGNAME --"

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

    echo "Usage: $PROGNAME -f <path to filename>"
    echo ""
    echo "-f     file to put timestamp into. default file is $DEFAULT_FNAME"
    #echo "-I     should be used for first run, to make subversion client save password and such"
    echo "       use -h to show this help"
}

print_help() {
    echo ""
    print_usage
    echo ""
    echo "this script should put current timestamp into file, which may be used to check if lsyncd is alive or not"
}
# parsing arguments

while getopts ":hf:" Option; do
  case $Option in
    h)
      print_help
      exit 0
      ;;
    f)
      tsfile="${OPTARG}"
      ;;
    *)
      print_help
      exit 0
      ;;
  esac
done
shift $(($OPTIND - 1))


tsfile=${tsfile:-$DEFAULT_FNAME}

curts=$(date +%s)
mylog "updating $tsfile with $curts timestamp"
echo $curts > $tsfile
if [ $? -ne 0 ];then
    mylog -e "update of timestamp file failed!"
fi
