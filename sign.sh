#!/bin/bash
# set -x

REQ_PROGRAM_LIST=(
    "getopt"
    "stm32mp1sign"
    "cert_create"
    "fiptool"
)

ARGUMENT_LIST=(
    "rot-key:"
    "rot-key-pwd:"
    "fsbl:"
    "fip:"
    "outdir:"
    "help"
)

FIP_BINARY_LIST=(
    "fw-config.bin"
    "hw-config.bin"
    "nt-fw.bin"
    "tos-fw.bin"
    "tos-fw-extra1.bin"
    "tos-fw-extra2.bin"
)

FIP_CERT_LIST=(
    "nt-fw-cert.crt"
    "nt-fw-key-cert.crt"
    "tb-fw-cert.crt"
    "tos-fw-cert.crt"
    "tos-fw-key-cert.crt"
    "trusted-key-cert.crt"
)

usage ()
{
    echo "example: $0 --rot-key /path/key.pem --rot-key-pwd qwerty --fsbl /path/fsbl.stm32 --fip /path/fip.bin --outdir /path/to/dir"
    echo "This script will:"
    echo "1. Sign the fsbl"
    echo "2. Unpack the fip to the outdir"
    echo "2. Create a TF-A chain of trust stack with certs and keys required to build a working trusted board boot"
    echo "3. Output all keys and certs plus a new fip into the output directory assigned to it"
    echo "where:"
    echo "rot-key		: Path to root of trust key. This key will be used for signing the fsbl and creating the chain of trust for the TF-A trusted board boot"
    echo "rot-key-pwd		: Password for the private encrypted rot-key. If this is missing and the key is encrypted then the program will ask for a password"
    echo "fsbl			: Path to first stage bootloader (fsbl). Usually provided by BL2 (Trusted Firmware) as a stm32 header wrapped binary"
    echo "fip			: Path to the Firmware Image Package (FIP)"
    echo "outdir		: Path to the output directory"
    exit 1;
}

needs_arg()
{
    if [ -z "$1" ]; then
	echo "No arg for --$1 option"
	exit 1
    fi
}

# Check for needed programs in path.
for PROGRAM in "${REQ_PROGRAM_LIST[@]}"; do
    command -v ${PROGRAM} > /dev/null 2>&1
    if [ $? -ne 0 ]; then
	echo "Missing a needed program in path: ${PROGRAM}"
	exit 1
    fi
done

opts=$(getopt \
	   --longoptions "$(printf "%s," "${ARGUMENT_LIST[@]}")" \
	   --name "$(basename "$0")" \
	   --options "" \
	   -- "$@"
    )

eval set -- $opts

ROT_KEY=""
ROT_KEY_PWD=""
FSBL=""
FIP=""
OUTDIR=""

while [[ $# -gt 0 ]]; do
    case "$1" in
	--rot-key)
	    ROT_KEY="$2"
	    if [ ! -f ${ROT_KEY} ]; then
		echo "rot_key: Unable to find or use file"
		exit 1
	    fi
	    echo "rot_key: ${ROT_KEY}"
	    shift 2
	    ;;
	--rot-key-pwd)
	    ROT_KEY_PWD="$2"
	    shift 2
	    ;;
	--fsbl)
	    FSBL="$2"
	    if [ ! -f ${FSBL} ]; then
		echo "fsbl: Unable to find or use file"
		exit 1
	    fi
	    echo "fsbl: ${FSBL}"
	    shift 2
	    ;;
	--fip)
	    FIP="$2"
	    if [ ! -f ${FIP} ]; then
		echo "fip: Unable to find or use file"
		exit 1
	    fi
	    echo "fip: ${FIP}"
	    shift 2
	    ;;
	--outdir)
	    OUTDIR="$2"
	    if [ ! -d ${OUTDIR} ]; then
		mkdir -p ${OUTDIR} > /dev/null 2>&1
		if [ $? -ne 0 ]; then
		    echo "outdir: Unable to create output directory"
		    exit 1
		fi
	    fi
	    echo "outdir: ${OUTDIR}"
	    shift 2
	    ;;
	--help)
	    usage
	    ;;
	*)
	    break
	    ;;
    esac
done

echo ""

# Input parameter checks.
if [ -z "${ROT_KEY}" -o -z "${FIP}" -o -z "${FSBL}" -o -z "${OUTDIR}" ]; then
    echo "Missing input"
    usage
    exit 1
fi

# Copy FSBL to OUTDIR. stm32mp1sign usually modifies in situ.
cp ${FSBL} ${OUTDIR}/$(basename ${FSBL})
FSBL=${OUTDIR}/$(basename ${FSBL})

if [ ! -f ${FSBL} ]; then
    echo "fsbl: Could not make a copy"
    exit 1
fi

# Compound key handling
KEY_HANDLING="--key ${ROT_KEY}"
if [ ! -z ${ROT_KEY_PWD} ]; then
    KEY_HANDLING="${KEY_HANDLING} --password ${ROT_KEY_PWD}"
fi

# Sign FSBL
stm32mp1sign --image ${FSBL} ${KEY_HANDLING}

if [ $? -ne 0 ]; then
    echo "fsbl: ${FSBL} signing failed"
    exit 1
fi

# Unpack existing FIP
fiptool unpack --force --out ${OUTDIR} ${FIP}

if [ $? -ne 0 ]; then
    echo "fip: ${FIP} unpacking failed"
    exit 1
fi

# Check incoming FIP constituents
for FIP_BINARY in "${FIP_BINARY_LIST[@]}"; do
    if [ ! -f "${OUTDIR}/${FIP_BINARY}" ]; then
	echo "fip: Missing a needed constituent: ${OUTDIR}/${FIP_BINARY}"
	exit 1
    fi
done

# Compund key handling.
KEY_HANDLING="--rot-key ${ROT_KEY}"
if [ ! -z ${ROT_KEY_PWD} ]; then
    KEY_HANDLING="${KEY_HANDLING} --rot-key-pwd ${ROT_KEY_PWD}"
fi

# Compound binary parameters and files.
BINARY_HANDLING=""
for FIP_BINARY in "${FIP_BINARY_LIST[@]}"; do
    BINARY_HANDLING="${BINARY_HANDLING} --${FIP_BINARY%.bin} ${OUTDIR}/${FIP_BINARY}"
done

# Remove old certs.
# Compund cert parameters and files.
CERT_HANDLING=""
for CERT in "${FIP_CERT_LIST[@]}"; do
    rm -f ${OUTDIR}/${CERT}
    CERT_HANDLING="${CERT_HANDLING} --${CERT%.crt} ${OUTDIR}/${CERT}"
done

# Need fake tb-fw. FIP does not actually contain this.
touch ${OUTDIR}/tb-fw.bin

# Create certs from constituents.
# -n, create new keys to use for the certs.
# 0, zero out the nonvolatile counters,
# key-alg: ecdsa
# hash-alg: sha256
cert_create -n \
	    --tfw-nvctr 0 \
	    --ntfw-nvctr 0 \
	    --key-alg ecdsa \
	    --hash-alg sha256 \
	    ${KEY_HANDLING} \
	    ${BINARY_HANDLING} \
	    --tb-fw ${OUTDIR}/tb-fw.bin \
	    ${CERT_HANDLING}

if [ $? -ne 0 ]; then
    echo "cert_create, new certs failed"
    exit 1
fi

FIP=${OUTDIR}/$(basename ${FIP%.bin}_Signed.bin)
# Create new fip
fiptool create \
	${BINARY_HANDLING} \
	${CERT_HANDLING} \
	${FIP}

if [ $? -ne 0 ]; then
    echo "fiptool, create failed"
    exit 1
fi

echo ""
echo "fsbl: ${FSBL}"
echo "fip: ${FIP}"
echo "done"
