#	$OpenBSD: putty-transfer.sh,v 1.7 2020/01/23 11:19:12 dtucker Exp $
#	Placed in the Public Domain.

tid="putty transfer data"

if test "x$REGRESS_INTEROP_PUTTY" != "xyes" ; then
	echo "putty interop tests not enabled"
	exit 0
fi

if [ "`${SSH} -Q compression`" = "none" ]; then
	comp="0"
else
	comp="0 1"
fi

for c in $comp; do
	verbose "$tid: compression $c"
	rm -f ${COPY}
	cp ${OBJ}/.putty/sessions/localhost_proxy \
	    ${OBJ}/.putty/sessions/compression_$c
	echo "Compression=$c" >> ${OBJ}/.putty/sessions/kex_$k
	env HOME=$PWD ${PLINK} -load compression_$c -batch \
	    -i ${OBJ}/putty.rsa2 cat ${DATA} > ${COPY}
	if [ $? -ne 0 ]; then
		fail "ssh cat $DATA failed"
	fi
	cmp ${DATA} ${COPY}		|| fail "corrupted copy"

	for s in 10 100 1k 32k 64k 128k 256k; do
		trace "compression $c dd-size ${s}"
		rm -f ${COPY}
		dd if=$DATA obs=${s} 2> /dev/null | \
			env HOME=$PWD ${PLINK} -load compression_$c \
			    -batch -i ${OBJ}/putty.rsa2 \
			    "cat > ${COPY}"
		if [ $? -ne 0 ]; then
			fail "ssh cat $DATA failed"
		fi
		cmp $DATA ${COPY}	|| fail "corrupted copy"
	done
done
rm -f ${COPY}

