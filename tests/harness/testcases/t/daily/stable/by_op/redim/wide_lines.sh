#!/bin/bash
#
if [ "$#" -ne 2 ] ; then
    echo requires 2 arguments: numlines numchars/line
    exit 1;
fi

NUMLINES=$1
NUMCHARS=$2

DDCOUNT=`expr $NUMCHARS \* 3 / 4`

# make two lines likely to be different (to defeat rle if it were applied to strings)
# the original version read /dev/urandom to generate every line, but that was
# wasteful of test time (24s cputime for args 6000 1000), hence this "optimization"
# of doing only two lines an appending them in pairs (4s for args 6000 1000 lines)
dd if=/dev/urandom bs=1 count=$DDCOUNT  2>/dev/null | base64 -w 0  > /tmp/$$_line_even
dd if=/dev/urandom bs=1 count=$DDCOUNT  2>/dev/null | base64 -w 0  > /tmp/$$_line_odd
echo >> /tmp/$$_line_even
echo >> /tmp/$$_line_odd

cat /tmp/$$_line_even /tmp/$$_line_odd > /tmp/$$_line_pair
NUMLINES_EVEN=$(expr $NUMLINES / 2 '*' 2)
LINE=0
while [ $LINE -lt $NUMLINES_EVEN ] ; do
    cat /tmp/$$_line_pair ;
    LINE=`expr $LINE + 2`
done

# when NUMLINES odd, there is one more line to do
if [ $LINE -lt $NUMLINES ] ; then
    cat /tmp/$$_line_even ; ech
    LINE=`expr $LINE + 1`
fi

STATUS=0
if [ $LINE != $NUMLINES ] ; then
    echo "$0, error, did not produce correct number of lines" >&2
    STATUS=1
fi

rm /tmp/$$_line_even
rm /tmp/$$_line_odd
rm /tmp/$$_line_pair

exit $STATUS

