/*****************************************************************************
 *  $Id: enc_v1.c,v 1.4 2003/04/23 22:04:45 dun Exp $
 *****************************************************************************
 *  This file is part of the Munge Uid 'N' Gid Emporium (MUNGE).
 *  For details, see <http://www.llnl.gov/linux/munge/>.
 *  UCRL-CODE-2003-???.
 *
 *  Copyright (C) 2003 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License;
 *  if not, write to the Free Software Foundation, Inc., 59 Temple Place,
 *  Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "auth.h"
#include "base64.h"
#include "cipher.h"
#include "conf.h"
#include "cred.h"
#include "enc_v1.h"
#include "lookup.h"
#include "mac.h"
#include "md.h"
#include "munge_defs.h"
#include "munge_msg.h"
#include "random.h"
#include "str.h"


/*****************************************************************************
 *  Extern Variables
 *****************************************************************************/

extern conf_t conf;                     /* defined in munged.c               */


/*****************************************************************************
 *  Static Prototypes
 *****************************************************************************/

static int enc_v1_validate_msg (munge_msg_t m);
static int enc_v1_init (munge_cred_t c);
static int enc_v1_authenticate (munge_cred_t c);
static int enc_v1_timestamp (munge_cred_t c);
static int enc_v1_pack_outer (munge_cred_t c);
static int enc_v1_pack_inner (munge_cred_t c);
static int enc_v1_mac (munge_cred_t c);
static int enc_v1_compress (munge_cred_t c);
static int enc_v1_encrypt (munge_cred_t c);
static int enc_v1_armor (munge_cred_t c);
static int enc_v1_fini (munge_cred_t c);


/*****************************************************************************
 *  Extern Functions
 *****************************************************************************/

/*  FIXME: Revisit init/fini.
 */
/*  FIXME: Change process_msg return type to void?
 */
int
enc_v1_process_msg (munge_msg_t m)
{
    munge_cred_t c = NULL;              /* aux data for processing this cred */
    int          rc = -1;               /* return code                       */

    assert (m != NULL);
    assert (m->head.version == 1);
    assert (m->head.type = MUNGE_MSG_ENC_REQ);

    if (enc_v1_validate_msg (m) < 0)
        ;
    else if (!(c = cred_create (m)))
        ;
    else if (enc_v1_init (c) < 0)
        ;
    else if (enc_v1_authenticate (c) < 0)
        ;
    else if (enc_v1_timestamp (c) < 0)
        ;
    else if (enc_v1_pack_outer (c) < 0)
        ;
    else if (enc_v1_pack_inner (c) < 0)
        ;
    else if (enc_v1_mac (c) < 0)
        ;
    else if (enc_v1_compress (c) < 0)
        ;
    else if (enc_v1_encrypt (c) < 0)
        ;
    else if (enc_v1_armor (c) < 0)
        ;
    else if (enc_v1_fini (c) < 0)
        ;
    else if (_munge_msg_send (m) != EMUNGE_SUCCESS)
        ;
    else /* success */
        rc = 0;

    cred_destroy (c);
    return (rc);
}


/*****************************************************************************
 *  Static Functions
 *****************************************************************************/

static int
enc_v1_validate_msg (munge_msg_t m)
{
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (m != NULL);
    assert (m->head.version == 1);

    m1 = m->pbody;

    /*  Reset message type for the response.
     */
    m->head.type = MUNGE_MSG_ENC_RSP;
    /*
     *  Validate cipher type.
     */
    if (m1->cipher == MUNGE_CIPHER_DEFAULT) {
        m1->cipher = conf->def_cipher;
    }
    else if (m1->cipher == MUNGE_CIPHER_NONE) {
        ; /* disable encryption */
    }
    else if (!(lookup_cipher (m1->cipher))) {
        return (_munge_msg_set_err (m, EMUNGE_BAD_CIPHER,
            strdupf ("Invalid cipher type %d", m1->cipher)));
    }
    /*  Validate compression type.
     */
    if (m1->zip == MUNGE_ZIP_DEFAULT) {
        m1->zip = conf->def_zip;
    }
    else if (m1->zip == MUNGE_ZIP_NONE) {
        ; /* disable compression */
    }
    else if (m1->zip >= MUNGE_ZIP_LAST_ENTRY) {
        return (_munge_msg_set_err (m, EMUNGE_BAD_ZIP,
            strdupf ("Invalid compression type %d", m1->zip)));
    }
    /*  Validate message authentication code type.
     *  Note that MUNGE_MAC_NONE is not valid -- MACs are REQUIRED!
     */
    if (m1->mac == MUNGE_MAC_DEFAULT) {
        m1->mac = conf->def_mac;
    }
    else if (!(lookup_mac (m1->mac))) {
        return (_munge_msg_set_err (m, EMUNGE_BAD_MAC,
            strdupf ("Invalid mac type %d", m1->mac)));
    }
    assert (m1->mac != MUNGE_MAC_NONE);

    /*  Validate realm.
     *
     *  FIXME: Validate realm and set default string if needed.
     *         Validate that the realm string is NUL-terminated.
     *         The realm string may need to be stored in the
     *         aux cred struct in order to be de-allocated.
     */
    return (0);
}


static int
enc_v1_init (munge_cred_t c)
{
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (c != NULL);

    m1 = c->msg->pbody;

    /*  Generate salt.
     */
    c->salt_len = MUNGE_CRED_SALT_LEN;
    random_pseudo_bytes (c->salt, c->salt_len);

    /*  Generate cipher initialization vector (if needed).
     */
    c->iv_len = cipher_iv_size (lookup_cipher (m1->cipher));
    if (c->iv_len > 0) {
        c->iv_len = cipher_iv_size (lookup_cipher (m1->cipher));
        assert (c->iv_len <= sizeof (c->iv));
        random_pseudo_bytes (c->iv, c->iv_len);
    }
    return (0);
}


static int
enc_v1_authenticate (munge_cred_t c)
{
/*  Ascertains the UID/GID of the client process.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (c != NULL);
    assert (c->msg);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  Determine identity of peer.
     */
    if (auth_peer_get (c->msg->sd, &(m1->uid), &(m1->gid)) < 0) {
        return (_munge_msg_set_err (c->msg, EMUNGE_SNAFU,
            strdup ("Unable to determine identity of client")));
    }
    return (0);
}


static int
enc_v1_timestamp (munge_cred_t c)
{
/*  Queries the current time.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  Set the "encode" time.
     */
    m1->time0 = time(NULL);
    m1->time1 = 0;
    if (m1->time0 == ((time_t) -1)) {
        return (_munge_msg_set_err (c->msg, EMUNGE_SNAFU,
            strdup ("Unable to query current time")));
    }
    return (0);
}


static int
enc_v1_pack_outer (munge_cred_t c)
{
/*  Packs the "outer" credential data into MSBF (ie, big endian) format.
 *  The "outer" part of the credential does not undergo cryptographic
 *    transformations (ie, compression and encryption).  It includes:
 *    cred version, cipher type, compression type, mac type, realm length,
 *    unterminated realm string (if realm_len > 0), and the cipher's
 *    initialization vector (if encrypted).
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */
    unsigned char       *p;             /* ptr into packed data              */

    assert (c != NULL);
    assert (c->outer_mem == NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    c->outer_mem_len += sizeof (c->version);
    c->outer_mem_len += sizeof (m1->cipher);
    c->outer_mem_len += sizeof (m1->zip);
    c->outer_mem_len += sizeof (m1->mac);
    c->outer_mem_len += sizeof (m1->realm_len);
    c->outer_mem_len += m1->realm_len;
    c->outer_mem_len += c->iv_len;
    if (!(c->outer_mem = malloc (c->outer_mem_len))) {
        return (_munge_msg_set_err (c->msg, EMUNGE_NO_MEMORY, NULL));
    }
    p = c->outer = c->outer_mem;
    c->outer_len = c->outer_mem_len;

    assert (sizeof (c->version) == 1);
    *p = c->version;
    p += sizeof (c->version);

    assert (sizeof (m1->cipher) == 1);
    *p = m1->cipher;
    p += sizeof (m1->cipher);

    assert (sizeof (m1->zip) == 1);
    *p = m1->zip;
    p += sizeof (m1->zip);

    assert (sizeof (m1->mac) == 1);
    *p = m1->mac;
    p += sizeof (m1->mac);

    assert (sizeof (m1->realm_len) == 1);
    *p = m1->realm_len;
    p += sizeof (m1->realm_len);

    if (m1->realm_len > 0) {
        memcpy (p, m1->realm, m1->realm_len);
        p += m1->realm_len;
    }
    if (c->iv_len > 0) {
        memcpy (p, c->iv, c->iv_len);
        p += c->iv_len;
    }
    assert (p == (c->outer + c->outer_len));
    return (0);
}


static int
enc_v1_pack_inner (munge_cred_t c)
{
/*  Packs the "inner" credential data into MSBF (ie, big endian) format.
 *  The "inner" part of the credential may be subjected to cryptographic
 *    transformations (ie, compression and encryption).  It includes:
 *    salt, encode time, uid, gid, data length, and data (if present).
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */
    unsigned char       *p;             /* ptr into packed data              */
    uint32_t             u;             /* tmp for packing into into MSBF    */

    assert (c != NULL);
    assert (c->inner_mem == NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    c->inner_mem_len += c->salt_len;
    c->inner_mem_len += sizeof (m1->time0);
    c->inner_mem_len += sizeof (m1->uid);
    c->inner_mem_len += sizeof (m1->gid);
    c->inner_mem_len += sizeof (m1->data_len);
    c->inner_mem_len += m1->data_len;
    if (!(c->inner_mem = malloc (c->inner_mem_len))) {
        return (_munge_msg_set_err (c->msg, EMUNGE_NO_MEMORY, NULL));
    }
    p = c->inner = c->inner_mem;
    c->inner_len = c->inner_mem_len;

    assert (c->salt_len > 0);
    memcpy (p, c->salt, c->salt_len);
    p += c->salt_len;

    assert (sizeof (m1->time0) == 4);
    u = htonl (m1->time0);
    memcpy (p, &u, sizeof (m1->time0));
    p += sizeof (m1->time0);

    assert (sizeof (m1->uid) == 4);
    u = htonl (m1->uid);
    memcpy (p, &u, sizeof (m1->uid));
    p += sizeof (m1->uid);

    assert (sizeof (m1->gid) == 4);
    u = htonl (m1->gid);
    memcpy (p, &u, sizeof (m1->gid));
    p += sizeof (m1->gid);

    assert (sizeof (m1->data_len) == 4);
    u = htonl (m1->data_len);
    memcpy (p, &u, sizeof (m1->data_len));
    p += sizeof (m1->data_len);

    if (m1->data_len > 0) {
        memcpy (p, m1->data, m1->data_len);
        p += m1->data_len;
    }
    assert (p == (c->inner + c->inner_len));
    return (0);
}


static int
enc_v1_mac (munge_cred_t c)
{
/*  Computes the Message Authentication Code (MAC) over the entire message
 *    (ie, both "outer" and "inner" data).
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */
    const EVP_MD        *md;            /* message digest algorithm          */
    mac_ctx              x;             /* message auth code context         */
    int                  n;             /* all-purpose int                   */

    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  MAC type already checked by enc_v1_validate().
     */
    md = lookup_mac (m1->mac);
    assert (md != NULL);

    /*  Init MAC.
     */
    c->mac_len = mac_size (md);
    assert (c->mac_len <= sizeof (c->mac));
    memset (c->mac, 0, c->mac_len);

    /*  Compute MAC.
     */
    if (mac_init (&x, md, conf->mac_key, conf->mac_key_len) < 0) {
        goto err1;
    }
    if (mac_update (&x, c->outer, c->outer_len) < 0) {
        goto err2;
    }
    if (mac_update (&x, c->inner, c->inner_len) < 0) {
        goto err2;
    }
    if (mac_final (&x, c->mac, &n) < 0) {
        goto err2;
    }
    if (mac_cleanup (&x) < 0) {
        goto err1;
    }
    assert (c->mac_len == n);

    return (0);

err2:
    mac_cleanup (&x);
err1:
    return (_munge_msg_set_err (c->msg, EMUNGE_SNAFU,
        strdup ("Unable to mac credential")));
}


static int
enc_v1_compress (munge_cred_t c)
{
/*  Compresses the "inner" credential data.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  Is compression disabled?
     */
    if (m1->zip == MUNGE_ZIP_NONE)
        return (0);

    /*  FIXME: Not implemented.
     */
    return (_munge_msg_set_err (c->msg, EMUNGE_SNAFU,
        strdup ("Compression not supported")));
}


static int
enc_v1_encrypt (munge_cred_t c)
{
/*  Encrypts the "inner" credential data.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */
    const EVP_MD        *md;            /* message digest algorithm          */
    const EVP_CIPHER    *ci;            /* symmetric cipher algorithm        */
    int                  buf_len;       /* length of ciphertext buffer       */
    unsigned char       *buf;           /* ciphertext buffer                 */
    unsigned char       *buf_ptr;       /* ptr into ciphertext buffer        */
    cipher_ctx           x;             /* cipher context                    */
    int                  n, m;          /* all-purpose ints                  */

    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  Is encryption disabled?
     */
    if (m1->cipher == MUNGE_CIPHER_NONE) {
        return (0);
    }
    /*  MAC/Cipher types already checked by enc_v1_validate().
     */
    md = lookup_mac (m1->mac);
    assert (md != NULL);
    ci = lookup_cipher (m1->cipher);
    assert (ci != NULL);

    /*  Compute DEK.
     *  msg-dek = MAC (msg-mac) using DEK subkey
     */
    c->dek_len = md_size (md);
    assert (c->dek_len <= sizeof (c->dek));
    assert (c->dek_len >= cipher_key_size (ci));

    if (mac_block (md, conf->dek_key, conf->dek_key_len,
                       c->dek, &n, c->mac, c->mac_len) < 0) {
        _munge_msg_set_err (c->msg, EMUNGE_SNAFU,
            strdup ("Unable to compute dek"));
        goto err1;
    }
    assert (n == c->dek_len);

    /*  Allocate memory for ciphertext.
     *  Ensure enough space by allocating an additional cipher block.
     */
    buf_len = c->inner_len + cipher_block_size (ci);
    if (!(buf = malloc (buf_len))) {
        _munge_msg_set_err (c->msg, EMUNGE_NO_MEMORY, NULL);
        goto err1;
    }
    /*  Encrypt "inner" data.
     */
    if (cipher_init (&x, ci, c->dek, c->iv, CIPHER_ENCRYPT) < 0) {
        goto err2;
    }
    buf_ptr = buf;
    n = 0;
    if (cipher_update (&x, buf_ptr, &m, c->inner, c->inner_len) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (cipher_final (&x, buf_ptr, &m) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (cipher_cleanup (&x) < 0) {
        goto err2;
    }
    assert (n <= buf_len);

    /*  Replace "inner" plaintext with ciphertext.
     */
    if (c->inner_mem) {
        assert (c->inner_mem_len > 0);
        memset (c->inner_mem, 0, c->inner_mem_len);
        free (c->inner_mem);
    }
    c->inner_mem = buf;
    c->inner_mem_len = buf_len;
    c->inner = buf;
    c->inner_len = n;

    return (0);

err3:
    cipher_cleanup (&x);
err2:
    memset (buf, 0, buf_len);
    free (buf);
    _munge_msg_set_err (c->msg, EMUNGE_SNAFU,
        strdup ("Unable to encrypt credential"));
err1:
    return (-1);
}


static int
enc_v1_armor (munge_cred_t c)
{
/*  Armors the credential allowing it to be sent over virtually any transport.
 *  The armor consists of PREFIX + BASE64 [ OUTER + MAC + INNER ] + SUFFIX.
 */
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */
    int                  prefix_len;    /* prefix string length              */
    int                  suffix_len;    /* prefix string length              */
    int                  buf_len;       /* length of armor'd data buffer     */
    unsigned char       *buf;           /* armor'd data buffer               */
    unsigned char       *buf_ptr;       /* ptr into armor'd data buffer      */
    base64_ctx           x;             /* base64 context                    */
    int                  n, m;          /* all-purpose ints                  */

    /*  FIXME: Add support for *_HALF MACs.
     */
    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    prefix_len = strlen (MUNGE_CRED_PREFIX);
    suffix_len = strlen (MUNGE_CRED_SUFFIX);

    /*  Allocate memory for armor'd data.
     */
    n = 0;
    n += c->outer_len;
    n += c->mac_len;
    n += c->inner_len;

    buf_len = 0;
    buf_len += prefix_len;
    buf_len += suffix_len;
    buf_len += base64_encode_length (n);

    if (!(buf = malloc (buf_len))) {
        _munge_msg_set_err (c->msg, EMUNGE_NO_MEMORY, NULL);
        goto err1;
    }
    buf_ptr = buf;

    /*  Add the prefix string.
     */
    if (prefix_len > 0) {
        strcpy (buf_ptr, MUNGE_CRED_PREFIX);    /* strcpy() is safe here */
        buf_ptr += prefix_len;
    }
    /*  Base64-encode the chewy-internals of the credential.
     *  The data will be NUL-terminated by in the process.
     */
    if (base64_init (&x) < 0) {
        goto err2;
    }
    n = 0;
    if (base64_encode_update (&x, buf_ptr, &m, c->outer, c->outer_len) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (base64_encode_update (&x, buf_ptr, &m, c->mac, c->mac_len) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (base64_encode_update (&x, buf_ptr, &m, c->inner, c->inner_len) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (base64_encode_final (&x, buf_ptr, &m) < 0) {
        goto err3;
    }
    buf_ptr += m;
    n += m;
    if (base64_cleanup (&x) < 0) {
        goto err2;
    }
    n++;                                /* count the terminating NUL char */

    /*  Add the suffix string.
     */
    if (suffix_len > 0) {
        strcpy (buf_ptr, MUNGE_CRED_SUFFIX);    /* strcpy() is safe here */
        buf_ptr += suffix_len;
    }
    assert ((buf_ptr - buf) < buf_len);

    /*  Replace "outer" data with armor'd data.
     */
    if (c->outer_mem) {
        assert (c->outer_mem_len > 0);
        memset (c->outer_mem, 0, c->outer_mem_len);
        free (c->outer_mem);
    }
    c->outer_mem = buf;
    c->outer_mem_len = buf_len;
    c->outer = buf;
    c->outer_len = buf_ptr - buf + 1;

    return (0);

err3:
    base64_cleanup (&x);
err2:
    memset (buf, 0, buf_len);
    free (buf);
    _munge_msg_set_err (c->msg, EMUNGE_SNAFU,
        strdup ("Unable to base64-encode credential"));
err1:
    return (-1);
}


static int
enc_v1_fini (munge_cred_t c)
{
    struct munge_msg_v1 *m1;            /* munge msg (v1 format)             */

    assert (c != NULL);
    assert (c->msg != NULL);
    assert (c->msg->head.version == 1);

    m1 = c->msg->pbody;

    /*  Place credential in message "data" payload for transit.
     *  Note that the old 'm1->data' lies within m->pbody's malloc region,
     *    so it will be free'd by the msg destructor when pbody is free'd.
     */
    m1->data = c->outer;
    m1->data_len = c->outer_len;

    return (0);
}