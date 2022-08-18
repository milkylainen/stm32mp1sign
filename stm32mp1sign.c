// SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause
/*
 * Copyright (C) 2022, Christian Melki
 */

#define _DEFAULT_SOURCE
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <getopt.h>
#include <errno.h>
#include <endian.h>

#include <sys/mman.h>
#include <fcntl.h>

/* Usage of deprecated functions.
 * Want this to build with older openssl.
 * Don't require 3.0+ functions.
 */
#define OPENSSL_API_COMPAT 0x10101000L
#include <openssl/opensslv.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/bn.h>
#include <openssl/obj_mac.h>

#define UNUSED                          __attribute__((unused))
#define HEADER_MAGIC                    "STM2"
/* The ec pubkeys for allowed curves are 65 bytes.
 * 1 byte describing format and 2*32 byte,
 * x concatenated y points in an ecsig struct.
 */
#define EC_POINT_UNCOMPRESSED_LEN       65
/* The CPU hashes header from offset 0x48,
 * ie. from member header_version and all of the data.
 */
#define STM32_HASH_OFFSET               offsetof(struct stm32_header, header_version)

/* The stm32 header is often defined to be 0x100 bytes.
 * This struct is 0xFF bytes. However, some implementations
 * carry a padding of uint32_t x[83/4] (?!?).
 * Other definitions do uint8_t x[83] (like this one).
 * Most of them carry a member "binary_type"
 * after the padding, which extends the struct to 259 bytes.
 * Either way, this is a mess.
 * We don't have any need of poking anything behind
 * the ecdsa_public_key member, so lets just skip
 * everything after the padding and never touch it,
 * keeping this struct below 0x100.
 * Also add packed, which seems to be missing in
 * a lot of implementations in the wild.
 */
struct __attribute((packed)) stm32_header {
        uint32_t magic_number;
        uint8_t image_signature[64];
        uint32_t image_checksum;
        uint8_t  header_version[4];
        uint32_t image_length;
        uint32_t image_entry_point;
        uint32_t reserved1;
        uint32_t load_address;
        uint32_t reserved2;
        uint32_t version_number;
        uint32_t option_flags;
        uint32_t ecdsa_algorithm;
        uint8_t ecdsa_public_key[64];
        uint8_t padding[83];
};

static void
usage(char *argv[])
{
        printf("%s usage:\n", argv[0]);
        printf("---------------------\n");
        printf("%s --image <file> --key <file> [--password <string>]\n", argv[0]);
        printf("%s --help\n", argv[0]);
        printf("where:\n");
        printf("--image       ; Path to stm32image file to sign. This modifies the file\n");
        printf("--key         ; Path to the private key used to sign hash. Must contain private and public key.\n");
        printf("--password    ; Not mandatory. Private key password. If not used, program will ask interactively.\n");
        printf("--help        ; This help.\n");
}

static unsigned char *
stm32image_load(int fd, off_t *len)
{
        unsigned char *data;

        if (fd < 0 || !len) {
                fprintf(stderr, "Invalid input.\n");
                goto err_out;
        }

        if ((*len = lseek(fd, 0, SEEK_END)) == (off_t)-1) {
                fprintf(stderr, "Cannot seek to end.\n");
                goto err_out;
        }
        if (*len <= (off_t)sizeof(struct stm32_header)) {
                fprintf(stderr, "Image file too small for stm32 header.\n");
                goto err_out;
        }
        if (lseek(fd, 0, SEEK_SET) == (off_t)-1) {
                fprintf(stderr, "Cannot seek to start.\n");
                goto err_out;
        }
        if ((data = mmap(NULL, *len, PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_POPULATE, fd, 0)) == MAP_FAILED) {
                fprintf(stderr, "mmap failed: %s\n", strerror(errno));
                goto err_out;
        }
        /* Not overly rigorous checks.
         * Assuming header was generated by something sane already.
         */
        if (memcmp(data, HEADER_MAGIC, strlen(HEADER_MAGIC))) {
                fprintf(stderr, "Invalid stm32 header magic.\n");
                goto err_out;
        }

        return data;

 err_out:
        if (len) *len = 0;
        return NULL;
}

static int
openssl_pw_cb(char *buf, int size, int rwflag UNUSED, void *u UNUSED)
{
        int len;
        char *passwd;

        passwd = getpass("Privkey password: ");
        len = strlen(passwd);
        if (len <= 0 || len > size) {
                return 0;
        }
        memcpy(buf, passwd, len);

        return len;
}

static EC_KEY *
openssl_load_privkey(const char *privkey_path, const char *pw)
{
        BIO *bio_privkey = NULL;
        EVP_PKEY *privkey = NULL;
        EC_KEY *eckey = NULL;

        if (!privkey_path || !privkey_path[0]) {
                fprintf(stderr, "Invalid input.\n");
                goto err_out;
        }

        if (!(bio_privkey = BIO_new_file(privkey_path, "r"))) {
                fprintf(stderr, "Unable to load privkey %s.\n", privkey_path);
                goto err_out;
        }
        privkey = PEM_read_bio_PrivateKey(bio_privkey, NULL,
                                          pw ? NULL : openssl_pw_cb,
                                          pw ? (char *)pw : NULL);
        if (!privkey) {
                fprintf(stderr, "Unable to load privkey %s.\n",
                        privkey_path);
                goto err_out;
        }
#if OPENSSL_VERSION_NUMBER >= 0x30000000L
        if (!EVP_PKEY_is_a(privkey, "EC")) {
                fprintf(stderr, "Privkey %s is not an EC type.\n",
                        privkey_path);
                goto err_out;
        }
        if (!EVP_PKEY_can_sign(privkey)) {
                fprintf(stderr, "Privkey %s can't be used to sign.\n",
                        privkey_path);
                goto err_out;
        }
#endif
        if (!(eckey = EVP_PKEY_get1_EC_KEY(privkey))) {
                fprintf(stderr, "Unable to get EC key.\n");
                goto err_out;
        }

        if (privkey) EVP_PKEY_free(privkey);
        if (bio_privkey) BIO_free(bio_privkey);
        return eckey;

 err_out:
        if (privkey) EVP_PKEY_free(privkey);
        if (bio_privkey) BIO_free(bio_privkey);
        return NULL;
}

static uint8_t *
openssl_get_pubkey(EC_KEY *eckey, size_t *len, int *alg)
{
        int nid = 0;
        BN_CTX *ctx = NULL;
        const EC_POINT *public_key = NULL;
        const EC_GROUP *group = NULL;
        uint8_t *buffer = NULL;

        if (!eckey || !len | !alg) {
                fprintf(stderr, "Invalid input.\n");
                goto err_out;
        }

        if (!(public_key = EC_KEY_get0_public_key(eckey))) {
                fprintf(stderr, "Unable to get EC pubkey.\n");
                goto err_out;
        }
        if (!(ctx = BN_CTX_new())) {
                fprintf(stderr, "Unable to allocate bignum context.\n");
                goto err_out;
        }
        if (!(group = EC_KEY_get0_group(eckey))) {
                fprintf(stderr, "Unable to get EC group.\n");
                goto err_out;
        }
        if (!EC_GROUP_get_asn1_flag(group)) {
                fprintf(stderr, "Unable to get EC parameters.\n");
                goto err_out;
        }
        /* Only allow these curves */
        nid = EC_GROUP_get_curve_name(group);
        if (nid == NID_X9_62_prime256v1) {
                *alg = 1;
        } else if (nid == NID_brainpoolP256r1) {
                *alg = 2;
        } else {
                fprintf(stderr, "Invalid EC curve in use.\n");
                goto err_out;
        }
        /* Use point2oct twice.
         * Get length, allocate storage, repeat to get contents.
         */
        if (!(*len = EC_POINT_point2oct(group, public_key,
                                        EC_KEY_get_conv_form(eckey), NULL,
                                        0, ctx))) {
                fprintf(stderr, "Unable to get EC pubkey length.\n");
                goto err_out;
        }
        if (!(buffer = OPENSSL_malloc(*len))) {
                fprintf(stderr, "Unable to allocate pubkey buffer.\n");
                goto err_out;
        }
        if (!(*len = EC_POINT_point2oct(group, public_key,
                                        EC_KEY_get_conv_form(eckey), buffer,
                                        *len, ctx))) {
                fprintf(stderr, "Unable to get EC pubkey length.\n");
                goto err_out;
        }

        if (ctx) BN_CTX_free(ctx);
        return buffer;

 err_out:
        if (ctx) BN_CTX_free(ctx);
        if (len) *len = 0;
        if (alg) *alg = 0;
        return NULL;
}

static ECDSA_SIG *
openssl_do_ecdsa_sha256_sign(EC_KEY *eckey, unsigned char *data,
                             unsigned long datalen)
{
        ECDSA_SIG *ecsig = NULL;

        if (!eckey || !data || !datalen) {
                fprintf(stderr, "Invalid input.\n");
                goto err_out;
        }

        if (!(ecsig = ECDSA_do_sign(SHA256(data, datalen, NULL),
                                    SHA256_DIGEST_LENGTH, eckey))) {
                fprintf(stderr, "Unable to generate ECDSA signature.\n");
                goto err_out;
        }

 err_out:
        return ecsig;
}

int
main(int argc, char *argv[])
{
        struct stm32_header *h = NULL;
        char *privkey_path = NULL;
        char *password = NULL;
        EC_KEY *eckey = NULL;
        ECDSA_SIG *ecsig = NULL;
        unsigned char *data = NULL;
        off_t datalen;
        uint8_t *buf = NULL;
        size_t len;
        int alg, c, fd = -1;

        static struct option options[] = {
                {"key", required_argument, 0, 'k'},
                {"password", required_argument, 0, 'p'},
                {"image", required_argument, 0, 'i'},
                {"help", no_argument, 0, 'h'},
                {0, 0, 0, 0}
        };

        while (1) {
                c = getopt_long(argc, argv, "k:p:i:h", options, NULL);
                if (c == -1)
                        break;
                switch (c) {
                case 'k':
                        privkey_path = strdup(optarg);
                        break;
                case 'p':
                        password = strdup(optarg);
                        break;
                case 'i':
                        fd = open(optarg, O_RDWR);
                        if (fd < 0) {
                                fprintf(stderr,
                                        "Error: Cannot open %s: %s\n",
                                        optarg, strerror(errno));
                                goto err_out;
                        }
                        break;
                case 'h':
                        usage(argv);
                        goto err_out;
                        break;
                default:
                        fprintf(stderr, "%s: unknown option\n", argv[0]);
                        usage(argv);
                        goto err_out;
                        break;
                }
        }

        if (fd < 0) {
                fprintf(stderr, "%s: Missing stm32 image file.\n",
                        argv[0]);
                usage(argv);
                goto err_out;
        }

        if (!privkey_path) {
                fprintf(stderr, "%s: Missing privkey path or password.\n",
                        argv[0]);
                usage(argv);
                goto err_out;
        }

        /* Load and validate image magic. */
        if (!(data = stm32image_load(fd, &datalen))) {
                goto err_out;
        }
        /* Load privkey. Must contain pubkey */
        if (!(eckey = openssl_load_privkey(privkey_path, password))) {
                goto err_out;
        }
        /* Get pubkey from privkey. */
        buf = openssl_get_pubkey(eckey, &len, &alg);
        if (!buf || buf[0] != POINT_CONVERSION_UNCOMPRESSED ||
            len != EC_POINT_UNCOMPRESSED_LEN) {
                fprintf(stderr, "EC pubkey invalid length.\n");
                goto err_out;
        }
        /* Slap the header over the data so we can modify it. */
        h = (struct stm32_header *)data;
        /* Copy pubkey to header.
         * First byte is the type declaration. Skip it.
         * Raw bignum. Two points on curve. X concatenated with Y.
         */
        memcpy(h->ecdsa_public_key, &buf[1], len - 1);
        /* option:
         * 0: signed.
         * 1: not signed.
         */
        h->option_flags = htole32(0);
        /* Algorithm:
         * 1: prime256v1
         * 2: brainpoolP256r1
         */
        h->ecdsa_algorithm = htole32(alg);
        /* Do ECDSA signature with sha256
         * from correct offset in header to end of data.
         */
        if (!(ecsig =
              openssl_do_ecdsa_sha256_sign(eckey,
                                           &data[STM32_HASH_OFFSET],
                                           datalen - STM32_HASH_OFFSET))) {
                goto err_out;
        }
        /* Copy signature to header.
         * Raw bignum. Two numbers. R concatenated with S.
         */
        ;
        BN_bn2bin(ECDSA_SIG_get0_r(ecsig),
                  &((h->image_signature)[0]));
        BN_bn2bin(ECDSA_SIG_get0_s(ecsig),
                  &((h->image_signature)[32]));

        EC_KEY_free(eckey);
        ECDSA_SIG_free(ecsig);
        OPENSSL_free(buf);
        munmap(data, datalen);
        exit(EXIT_SUCCESS);

 err_out:
        if (eckey) EC_KEY_free(eckey);
        if (ecsig) ECDSA_SIG_free(ecsig);
        if (buf) OPENSSL_free(buf);
        if (data) munmap(data, datalen);
        exit(EXIT_FAILURE);
}