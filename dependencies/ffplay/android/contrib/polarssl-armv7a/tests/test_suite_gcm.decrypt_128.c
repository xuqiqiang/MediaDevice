#include "fct.h"
#include <polarssl/config.h>

#include <polarssl/gcm.h>

#ifdef _MSC_VER
#include <basetsd.h>
typedef UINT32 uint32_t;
#else
#include <inttypes.h>
#endif

/*
 * 32-bit integer manipulation macros (big endian)
 */
#ifndef GET_UINT32_BE
#define GET_UINT32_BE(n,b,i)                            \
{                                                       \
    (n) = ( (uint32_t) (b)[(i)    ] << 24 )             \
        | ( (uint32_t) (b)[(i) + 1] << 16 )             \
        | ( (uint32_t) (b)[(i) + 2] <<  8 )             \
        | ( (uint32_t) (b)[(i) + 3]       );            \
}
#endif

#ifndef PUT_UINT32_BE
#define PUT_UINT32_BE(n,b,i)                            \
{                                                       \
    (b)[(i)    ] = (unsigned char) ( (n) >> 24 );       \
    (b)[(i) + 1] = (unsigned char) ( (n) >> 16 );       \
    (b)[(i) + 2] = (unsigned char) ( (n) >>  8 );       \
    (b)[(i) + 3] = (unsigned char) ( (n)       );       \
}
#endif

int unhexify(unsigned char *obuf, const char *ibuf)
{
    unsigned char c, c2;
    int len = strlen(ibuf) / 2;
    assert(!(strlen(ibuf) %1)); // must be even number of bytes

    while (*ibuf != 0)
    {
        c = *ibuf++;
        if( c >= '0' && c <= '9' )
            c -= '0';
        else if( c >= 'a' && c <= 'f' )
            c -= 'a' - 10;
        else if( c >= 'A' && c <= 'F' )
            c -= 'A' - 10;
        else
            assert( 0 );

        c2 = *ibuf++;
        if( c2 >= '0' && c2 <= '9' )
            c2 -= '0';
        else if( c2 >= 'a' && c2 <= 'f' )
            c2 -= 'a' - 10;
        else if( c2 >= 'A' && c2 <= 'F' )
            c2 -= 'A' - 10;
        else
            assert( 0 );

        *obuf++ = ( c << 4 ) | c2;
    }

    return len;
}

void hexify(unsigned char *obuf, const unsigned char *ibuf, int len)
{
    unsigned char l, h;

    while (len != 0)
    {
        h = (*ibuf) / 16;
        l = (*ibuf) % 16;

        if( h < 10 )
            *obuf++ = '0' + h;
        else
            *obuf++ = 'a' + h - 10;

        if( l < 10 )
            *obuf++ = '0' + l;
        else
            *obuf++ = 'a' + l - 10;

        ++ibuf;
        len--;
    }
}

/**
 * This function just returns data from rand().
 * Although predictable and often similar on multiple
 * runs, this does not result in identical random on
 * each run. So do not use this if the results of a
 * test depend on the random data that is generated.
 *
 * rng_state shall be NULL.
 */
static int rnd_std_rand( void *rng_state, unsigned char *output, size_t len )
{
#if !defined(__OpenBSD__)
    size_t i;

    if( rng_state != NULL )
        rng_state  = NULL;

    for( i = 0; i < len; ++i )
        output[i] = rand();
#else
    if( rng_state != NULL )
        rng_state = NULL;

    arc4random_buf( output, len );
#endif /* !OpenBSD */

    return( 0 );
}

/**
 * This function only returns zeros
 *
 * rng_state shall be NULL.
 */
static int rnd_zero_rand( void *rng_state, unsigned char *output, size_t len )
{
    if( rng_state != NULL )
        rng_state  = NULL;

    memset( output, 0, len );

    return( 0 );
}

typedef struct
{
    unsigned char *buf;
    size_t length;
} rnd_buf_info;

/**
 * This function returns random based on a buffer it receives.
 *
 * rng_state shall be a pointer to a rnd_buf_info structure.
 * 
 * The number of bytes released from the buffer on each call to
 * the random function is specified by per_call. (Can be between
 * 1 and 4)
 *
 * After the buffer is empty it will return rand();
 */
static int rnd_buffer_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_buf_info *info = (rnd_buf_info *) rng_state;
    size_t use_len;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    use_len = len;
    if( len > info->length )
        use_len = info->length;

    if( use_len )
    {
        memcpy( output, info->buf, use_len );
        info->buf += use_len;
        info->length -= use_len;
    }

    if( len - use_len > 0 )
        return( rnd_std_rand( NULL, output + use_len, len - use_len ) );

    return( 0 );
}

/**
 * Info structure for the pseudo random function
 *
 * Key should be set at the start to a test-unique value.
 * Do not forget endianness!
 * State( v0, v1 ) should be set to zero.
 */
typedef struct
{
    uint32_t key[16];
    uint32_t v0, v1;
} rnd_pseudo_info;

/**
 * This function returns random based on a pseudo random function.
 * This means the results should be identical on all systems.
 * Pseudo random is based on the XTEA encryption algorithm to
 * generate pseudorandom.
 *
 * rng_state shall be a pointer to a rnd_pseudo_info structure.
 */
static int rnd_pseudo_rand( void *rng_state, unsigned char *output, size_t len )
{
    rnd_pseudo_info *info = (rnd_pseudo_info *) rng_state;
    uint32_t i, *k, sum, delta=0x9E3779B9;
    unsigned char result[4], *out = output;

    if( rng_state == NULL )
        return( rnd_std_rand( NULL, output, len ) );

    k = info->key;

    while( len > 0 )
    {
        size_t use_len = ( len > 4 ) ? 4 : len;
        sum = 0;

        for( i = 0; i < 32; i++ )
        {
            info->v0 += (((info->v1 << 4) ^ (info->v1 >> 5)) + info->v1) ^ (sum + k[sum & 3]);
            sum += delta;
            info->v1 += (((info->v0 << 4) ^ (info->v0 >> 5)) + info->v0) ^ (sum + k[(sum>>11) & 3]);
        }

        PUT_UINT32_BE( info->v0, result, 0 );
        memcpy( out, result, use_len );
        len -= use_len;
        out += 4;
    }

    return( 0 );
}


FCT_BGN()
{
#ifdef POLARSSL_GCM_C


    FCT_SUITE_BGN(test_suite_gcm)
    {

        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d785dafea3e966731ef6fc6202262584" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d91a46205ee94058b3b8403997592dd2" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3b92a17c1b9c3578a68cffea5a5b6245" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "aec963833b9098de1ababc853ab74d96" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4e0ffd93beffd732c6f7d6ad606a2d24" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e9fcedc176dfe587dc61b2011010cdf1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c4fb9e3393681da9cec5ec96f87c5c31" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "845e910bc055d895879f62101d08b4c7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "99fb783c497416e4b6e2a5de7c782057" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2a930f2e09beceacd9919cb76f2ac8d3" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "340d9af44f6370eff534c653033a785a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0c1e5e9c8fe5edfd11f114f3503d63" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fe71177e02073b1c407b5724e2263a5e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "83c23d20d2a9d4b8f92da96587c96b18" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "43b2ca795420f35f6cb39f5dfa47a2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b02392fd7f228888c281e59d1eaa15fb" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "2726344ba8912c737e195424e1e6679e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a10b601ca8053536a2af2cc255d2b6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "21895cbafc16b7b8bf5867e88e0853d4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "f987ce1005d9bbd31d2452fb80957753" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "952a7e265830d58a6778d68b9450" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9bb9742bf47f68caf64963d7c10a97b0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "34a85669de64e1cd44731905fddbcbc5" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e9b6be928aa77b2de28b480ae74c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4e9708e4b37e2e1b5feaf4f5ab54e2a6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "1c53a9fdd23919b036d99560619a9939" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6611b50d6fbca83047f9f5fe1768" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "82fede79db25f00be96eb050a22cea87" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e9c50b517ab26c89b83c1f0cac50162c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d0c0ce9db60b77b0e31d05e048" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1d98566fca5201abb12914311a8bd532" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "590aef4b46a9023405d075edab7e6849" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a1cfd1a27b341f49eda2ca8305" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3038771820c2e1319f02a74b8a7a0c08" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e556d9f07fb69d7e9a644261c80fac92" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4d2f005d662b6a8787f231c5e1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0fb7eef50de598d7d8b508d019a30d5a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a2a2617040116c2c7e4236d2d8278213" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "68413c58df7bb5f067197ca0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8cc58b609204215c8ab4908286e56e5c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "fb83ea637279332677b5f68081173e99" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a2a9160d82739a55d8cd419f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "81a5fd184742a478432963f6477e8f92" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "da297cbb53b11d7c379e0566299b4d5a" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "200bee49466fdda2f21f0062" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "f604ac66d626959e595cbb7b4128e096" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "269d2a49d533c6bb38008711f38e0b39" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "468200fa4683e8be" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2e308ba7903e925f768c1d00ff3eb623" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "335acd2aa48a47a37cfe21e491f1b141" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4872bfd5e2ff55f6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1304e2a5a3520454a5109df61a67da7a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "dbe8b452acf4fa1444c3668e9ee72d26" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "83a0d3440200ca95" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ecf1ec2c9a8f2e9cc799f9b9fddb3232" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ddf0b695aef5df2b594fcaae72b7e41c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "2819aedf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9ab5c8ca905b5fe50461f4a68941144b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "96dd3927a96e16123f2e9d6b367d303f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6e0c53ef" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b5fc7af605721a9cfe61c1ee6a4b3e22" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6b757d4055823d1035d01077666037d6" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e8c09ddd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "03c0b4a6e508a8490db0d086a82c9db7" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ac52f6c1a05030321fa39f87e89fdb5e" );
            add_len = unhexify( add_str, "33316ca79d10a79f4fd038593e8eef09625089dc4e0ffe4bc1f2871554fa6666ab3e7fe7885edef694b410456f3ec0e513bb25f1b48d95e4820c5972c1aabb25c84c08566002dadc36df334c1ce86847964a122016d389ac873bca8c335a7a99bcef91e1b985ae5d488a2d7f78b4bf14e0c2dc715e814f4e24276057cf668172" );
            unhexify( tag_str, "756292d8b4653887edef51679b161812" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b228d3d15219ea9ad5651fce02c8374d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5c7eafaead029c3fe3cf3835fe758d0e" );
            add_len = unhexify( add_str, "8c35dd805c08686b9b4d460f81b4dcb8c46c6d57842dc3e72ba90952e2bebf17fe7184445b02f801800a944486d662a127d01d3b7f42679052cdc73ce533129af8d13957415c5495142157d6ce8a68aa977e56f562fed98e468e42522767656ce50369471060381bb752dd5e77c79677a4cadffa39e518e30a789e793b07ea21" );
            unhexify( tag_str, "a4dde1ab93c84937c3bbc3ad5237818d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "776afcbabedd5577fe660a60f920b536" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5bbb7f1b14084e520408dd87b97705e9" );
            add_len = unhexify( add_str, "44631fc9d4a07416b0dfb4e2b42071e3e2be45502c9ddf72b3e61810eeda31a7d685ebb2ee43a2c06af374569f439ee1668c550067de2dece9ec46ee72b260858d6033f814e85275c5ae669b60803a8c516de32804fa34d3a213ccfaf6689046e25eeb30b9e1608e689f4d31cc664b83a468a51165f5625f12f098a6bf7ddab2" );
            unhexify( tag_str, "a5347d41d93b587240651bcd5230264f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "20abeafa25fc4ea7d0592cb3e9b4d5fe" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3aba79a58c5aa664856b41d552c7a8d3" );
            add_len = unhexify( add_str, "98cfecaae9eb9a7c3b17e6bc5f80d8a4bf7a9f4fa5e01b74cae15ee6af14633205aafe3b28fb7b7918e12322ea27352056a603746d728a61361134a561619400ff2bf679045bac2e0fbc2c1d41f8faba4b27c7827bceda4e9bf505df4185515dd3a5e26f7639c8ad5a38bc5906a44be062f02cc53862678ae36fa3de3c02c982" );
            unhexify( tag_str, "2a67ad1471a520fe09a304f0975f31" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2bc73fba942ff105823b5dccf6befb1c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "902c3e3b69b1ef8395d7281ff74cce38" );
            add_len = unhexify( add_str, "4adec0b4ac00325a860044d9f9519daa4f7c163229a75819b0fd7d8e23319f030e61dfa8eadabff42ea27bc36bdb6cad249e801ca631b656836448b7172c11126bad2781e6a1aa4f62c4eda53409408b008c057e0b81215cc13ddabbb8f1915f4bbab854f8b00763a530ad5055d265778cd3080d0bd35b76a329bdd5b5a2d268" );
            unhexify( tag_str, "ebdd7c8e87fe733138a433543542d1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "356a4c245868243d61756cabe86da887" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b442f2ec6d45a17144c258fd59fe5b3b" );
            add_len = unhexify( add_str, "12cccc3c60474b0a1579c5006c2134850724fa6c9da3a7022d4f65fd238b052bdf34ea34aa7dbadad64996065acee588ab6bd29726d07ed24ffae2d33aadf3e66ebb87f57e689fd85128be1c9e3d8362fad1f8096ee391f75b576fb213d394cef6f091fc5488d9aa152be69475b9167abd6dd4fd93bbbc7b8ca316c952eb19c6" );
            unhexify( tag_str, "ed26080dcb670590613d97d7c47cf4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "dfa7e93aff73600fc552324253066e2c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c20001e93f1cd05253c277a9445d61e4" );
            add_len = unhexify( add_str, "a64d1e20058a1f7e698622a02f7ff8dc11886717ede17bbdc3c4645a66a71d8b04346fb389a251ffb0a7f445a25faf642bb7e4697d2cacf925e78c4be98457996afb25b0516b50f179441d1923312364947f8f1e0f5715b43bd537727bf943d7b4679b0b0b28b94e56e7bbf554d9cf79fcee4387f32bb6f91efdd23620035be6" );
            unhexify( tag_str, "6ba5e4dace9a54b50b901d9b73ad" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2ecea80b48d2ecd194a7699aa7d8ccfc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8b4db08bafc23b65ae50a2d20661d270" );
            add_len = unhexify( add_str, "efc2ca1a3b41b90f8ddf74291d68f072a6e025d0c91c3ce2b133525943c73ebadc71f150be20afeb097442fa51be31a641df65d90ebd81dcbaf32711ed31f5e0271421377ffe14ddafea3ca60a600588d484856a98de73f56a766ae60bae384a4ae01a1a06821cf0c7a6b4ee4c8f413748457b3777283d3310218fb55c107293" );
            unhexify( tag_str, "246a9d37553088b6411ebb62aa16" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d38fee3fd3d6d08224c3c83529a25d08" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a942ccb11cf9468186fabfc18c899801" );
            add_len = unhexify( add_str, "1c92a4ce0a1dae27e720d6f9b1e460276538de437f3812ab1177cf0273b05908f296f33ba0f4c790abe2ce958b1d92b930a0d81243e6ad09ef86ee8e3270243095096537cb1054fcfcf537d828b65af9b6cf7c50f5b8470f7908f314d0859107eed772ee1732c78e8a2e35b2493f3e8c1e601b08aeab8d9729e0294dca168c62" );
            unhexify( tag_str, "803a08700ec86fdeb88f7a388921" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1899b0cbae41d705c6eed3226afb5bc0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "82d0910aa53e300a487d880d018d0dea" );
            add_len = unhexify( add_str, "6bf5583cc1007d74f3529db63b8d4e085400ccf3725eab8e19cb145f3910c61465a21486740a26f74691866a9f632af9fae81f5f0bffedf0c28a6ce0fd520bb4db04a3cd1a7d29d8801e05e4b9c9374fd89bcb539489c2f7f1f801c253a1cc737408669bcd133b62da357f7399a52179125aa59fae6707d340846886d730a835" );
            unhexify( tag_str, "c5d58870fee9ce157f5ec1fa8f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8b95323d86d02754f4c2874b42ec6eb0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "4f76084acbdef9999c71dcc794238d7c" );
            add_len = unhexify( add_str, "ebc75788377c0b264818a6f97c19cf92c29f1c7cdeb6b5f0a92d238fa4614bc35d0cfe4ec9d045cd628ff6262c460679ac15b0c6366d9289bbd217e5012279e0af0fb2cfcbdf51fe16935968cbb727f725fe5bcd4428905849746c8493600ce8b2cfc1b61b04c8b752b915fed611d6b54ef73ec4e3950d6db1807b1ce7ed1dcc" );
            unhexify( tag_str, "c4724ff1d2c57295eb733e9cad" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "30da555559eb11cf7e0eff9d99e9607d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7799275bf12335f281ec94a870f90a0b" );
            add_len = unhexify( add_str, "e735d556e15aec78d9736016c8c99db753ed14d4e4adaaa1dd7eaad702ea5dc337433f8c2b45afdf2f385fdf6c55574425571e079ca759b6235f877ed11618ff212bafd865a22b80b76b3b5cf1acfd24d92fd41607bbb7382f26cd703757088d497b16b32de80e1256c734a9b83356b6fced207177de75458481eaef59a431d7" );
            unhexify( tag_str, "3c82272130e17c4a0a007a908e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ed2ac74af896c5190c271cfa6af02fd2" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e0226e2d8da47badad1fb78b9a797f27" );
            add_len = unhexify( add_str, "8f11353ae476ff923013e6e736ffc9d23101a1c471ccc07ad372a8430d6559c376075efce2e318cdf4c9443dbf132e7e6da5524045028c97e904633b44c4d189a4b64237ac7692dd03c0e751ce9f04d0fdbd8a96074cd7dfa2fd441a52328b4ac3974b4902db45663f7b6f24947dba618f8b9769e927faf84c9f49ad8239b9fb" );
            unhexify( tag_str, "db8af7a0d548fc54d9457c73" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0225b73fe5fbbe52f838d873173959d8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "02a048764f48d9aed1147ee922395bbf" );
            add_len = unhexify( add_str, "9b46a57b06e156c877e94c089814493ead879397dab3dfcab2db349ef387efcd0cc339a7e79131a2c580188fc7429044a465b8329d74cd8f47272a4ed32582b1c5c7e3d32341ae902ea4923dc33df8062bc24bb51a11d2ecc82f464f615041387f9c82bd2135d4e240fe56fa8a68e6a9a417e6702430a434b14d70cf02db3181" );
            unhexify( tag_str, "e2c2ce4022c49a95c9ac9026" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "89ca3771a0ef3287568b4ac036120198" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7e83d2ffa8af8c554cfd71a0db56ef5b" );
            add_len = unhexify( add_str, "1bd7a9d6262882bd12c62bd50942965b3cdcadf5e0fab2dc4d0daf0ee4b16e92c6e2464c0caa423cdce88e4d843490609716ec5e44c41672c656ac0e444d3622557ea8420c94deae3ad190ddaf859f6f8c23e4e2e32a46d28df23de4f99bd6c34f69e06eddfdfa5f263dbe8baf9d4296b2c543e4c4847271e7590374edf46234" );
            unhexify( tag_str, "06b2bf62591dc7ec1b814705" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a41a297bd96e224942998fe2192934a1" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6827f2c5a0b7ecd6bbc696abb0adf556" );
            add_len = unhexify( add_str, "f32041abd8543415cbac423d945dda5378a16a7e94d9ab5dbd2d32eb1c5048cc7c8e4df3ca84ec725f18c34cfdeaa7595392aabfd66d9e2f37c1165369cd806cd9d2110def6f5fad4345e5a6e2326c9300199438fcc078cd9fcf4d76872cac77fc9a0a8ac7e4d63995078a9addecf798460ff5910861b76c71bccfb6b629d722" );
            unhexify( tag_str, "49a4917eef61f78e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a9372c058f42e0a1d019bdb528313919" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8d03f423230c8f00a5b6b712d426a2af" );
            add_len = unhexify( add_str, "cfef4e70fcc1821eeccf7c7b5eb3c0c3b5f72dc762426e0bd26242f8aa68c5b716ab97eded5e5720caccc1965da603d556d8214d5828f2cf276d95bf552d47313876796221f62ccb818a6d801088755d58cfb751bfed0d5a19718d4e0f94b850e0279b3a69295d1837cba958a6cc56e7594080b9e5b954a199fdc9e54ddc8583" );
            unhexify( tag_str, "b82cd11cd3575c8d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6302b7338f8fa84195ad9abbacd89b4e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e1bed5c53547cbc85f3411fbb43bb08b" );
            add_len = unhexify( add_str, "bcd329c076e8da2797d50dcdcf271cecf3ce12f3c136ed746edc722f907be6133276ee099038fdc5d73eec812739c7489d4bcc275f95451b44890416e3ffe5a1b6fa3986b84eee3adad774c6feaecb1f785053eeda2cfc18953b8547866d98918dbe0a6abc168ac7d77467a367f11c284924d9d186ef64ef0fd54eacd75156d2" );
            unhexify( tag_str, "5222d092e9e8bd6c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "78b5c28d62e4b2097873a1180bd5a3a5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c93902c2819ee494f0fc4b259ee65dd8" );
            add_len = unhexify( add_str, "e6b1192674a02083a6cf36d4ba93ba40a5331fadf63fd1eb2efa2ee9c0d8818472aaaf2b4705746011753f30f447c8f58dd34d29606daf57eadc172529837058cb78a378b19da8d63c321f550dfa256b5fd9f30e93d8f377443bfcd125f86a079a1765d2010be73d060f24eebae8d05e644688b2149bc39e18bd527bc066f2ba" );
            unhexify( tag_str, "eae48137" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3d84130578070e036c9e3df5b5509473" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3b9b4950523a19c6866fd2b0cde541fd" );
            add_len = unhexify( add_str, "a764931e1b21a140c54a8619aacdb4358834987fb6e263cec525f888f9e9764c165aaa7db74f2c42273f912daeae6d72b232a872ac2c652d7cd3af3a5753f58331c11b6c866475697876dbc4c6ca0e52a00ba015ee3c3b7fb444c6e50a4b4b9bbe135fc0632d32a3f79f333d8f487771ed12522e664b9cf90e66da267f47a74d" );
            unhexify( tag_str, "79987692" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "08428605ab4742a3e8a55354d4764620" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "128f5f4a817e4af04113847a223adeb0" );
            add_len = unhexify( add_str, "464b484ed79d93a48e0f804e04df69d7ca10ad04ba7188d69e6549ab50503baaec67e0acba5537d1163c868fd3e350e9d0ae9123046bc76815c201a947aa4a7e4ed239ce889d4ff9c8d043877de06df5fc27cf67442b729b02e9c30287c0821ef9fa15d4cccbc53a95fa9ec3ed432ca960ebbf5a169ccada95a5bf4c7c968830" );
            unhexify( tag_str, "3eb3e3a2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0dd358bc3f992f26e81e3a2f3aa2d517" );
            pt_len = unhexify( src_str, "87cc4fd75788c9d5cc83bae5d764dd249d178ab23224049795d4288b5ed9ea3f317068a39a7574b300c8544226e87b08e008fbe241d094545c211d56ac44437d41491a438272738968c8d371aa7787b5f606c8549a9d868d8a71380e9657d3c0337979feb01de5991fc1470dfc59eb02511efbbff3fcb479a862ba3844a25aaa" );
            iv_len = unhexify( iv_str, "d8c750bb443ee1a169dfe97cfe4d855b" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "a81d13973baa22a751833d7d3f94b3b1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "77949b29f085bb3abb71a5386003811233056d3296eb093370f7777dadd306d93d59dcb9754d3857cf2758091ba661f845ef0582f6ae0e134328106f0d5d16b541cd74fdc756dc7b53f4f8a194daeea9369ebb1630c01ccb307b848e9527da20a39898d748fd59206f0b79d0ed946a8958033a45bd9ae673518b32606748eb65" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "77949b29f085bb3abb71a5386003811233056d3296eb093370f7777dadd306d93d59dcb9754d3857cf2758091ba661f845ef0582f6ae0e134328106f0d5d16b541cd74fdc756dc7b53f4f8a194daeea9369ebb1630c01ccb307b848e9527da20a39898d748fd59206f0b79d0ed946a8958033a45bd9ae673518b32606748eb65" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "43b5f18227e5c74288dbeff03801acd6" );
            pt_len = unhexify( src_str, "f58d630f10cfca61d4644d4f6505bab629e8e8faf1673e64417f9b79e622966a7011cfb3ff74db5cebf09ad3f41643d4437d213204a6c8397e7d59b8a5b1970aed2b6bb5ea1933c72c351f6ba96c0b0b98188f6e373f5db6c5ebece911ec7a1848abd3ae335515c774e0027dab7d1c07d047d3b8825ff94222dbaf6f9ab597ee" );
            iv_len = unhexify( iv_str, "08ee12246cf7edb81da3d610f3ebd167" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "82d83b2f7da218d1d1441a5b37bcb065" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9a433c612d7e1bdff881e4d63ba8b141" );
            pt_len = unhexify( src_str, "ce10758332f423228b5e4ae31efda7677586934a1d8f05d9b7a0dc4e2010ec3eaacb71a527a5fff8e787d75ebd24ad163394c891b33477ed9e2a2d853c364cb1c5d0bc317fcaf4010817dbe5f1fd1037c701b291b3a66b164bc818bf5c00a4c210a1671faa574d74c7f3543f6c09aaf117e12e2eb3dae55edb1cc5b4086b617d" );
            iv_len = unhexify( iv_str, "8b670cf31f470f79a6c0b79e73863ca1" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8526fd25daf890e79946a205b698f287" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8e9d75c781d63b29f1816859f7a0e0a0" );
            pt_len = unhexify( src_str, "a9f1883f58e4ef78377992101ab86da0dafcefa827904dd94dff6f6704b1e45517165a34c5555a55b04c6992fb6d0840a71bd262fe59815e5c7b80fe803b47d5ba44982a3f72cb42f591d8b62df38c9f56a5868af8f68242e3a15f97be8ef2399dbace1273f509623b6f9e4d27a97436aebf2d044e75f1c62694db77ceac05de" );
            iv_len = unhexify( iv_str, "748a3b486b62a164cedcf1bab9325add" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "131e0e4ce46d768674a7bcacdcef9c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fe6b8553002c69396d9976bb48d30779" );
            pt_len = unhexify( src_str, "786f4801b16de7a4931ab143b269c7acc68f1ed9b17a95e8929ccec7d53413059fd4267bedbf079d9d69e90314c1345bc9cb9132f1af69323157ddf7533ced42b4b7bd39004f14d326f5b03bc19084d231d93bcab328312d99b426c1e86e8e049d380bb492e2e32ad690af4cf86838d89a0dfdcbc30e8c9e9039e423a234e113" );
            iv_len = unhexify( iv_str, "595b17d0d76b83780235f5e0c92bd21f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8879de07815a88877b0623de9be411" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b15dc7cd44adcb0783f30f592e5e03ccd47851725af9fe45bfc5b01ae35779b9a8b3f26fec468b188ec3cad40785c608d6bfd867b0ccf07a836ec20d2d9b8451636df153a32b637e7dcdbd606603d9e53f6e4c4cc8396286ce64b0ea638c10e5a567c0bc8e808080b71be51381e051336e60bf1663f6d2d7640a575e0752553b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b15dc7cd44adcb0783f30f592e5e03ccd47851725af9fe45bfc5b01ae35779b9a8b3f26fec468b188ec3cad40785c608d6bfd867b0ccf07a836ec20d2d9b8451636df153a32b637e7dcdbd606603d9e53f6e4c4cc8396286ce64b0ea638c10e5a567c0bc8e808080b71be51381e051336e60bf1663f6d2d7640a575e0752553b" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "14898c56009b459172fef9c17993b54f" );
            pt_len = unhexify( src_str, "e7ba6ef722273238b975d551f95d3e77e9b75b24c547b86eafb457d409803bdf6e1443839d8604ee497020e1a3dbd687a819b17fdde0fcf240ce2129792792a58bfcd825773001ee959bf9ec8d228e27ce1cd93d7fb86769a3793361b6f82bf7daf284afc1ece657a1ee6346ea9294880755b9b623563ad2657ba2286488a2ef" );
            iv_len = unhexify( iv_str, "0862f8f87289988711a877d3231d44eb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "36938974301ae733760f83439437c4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "3fd56897a62743e0ab4a465bcc9777d5fd21ad2c9a59d7e4e1a60feccdc722b9820ec65cb47e1d1160d12ff2ea93abe11bc101b82514ead7d542007fee7b4e2dd6822849cd3e82d761ff7cf5ce4f40ad9fec54050a632a401451b426812cf03c2b16a8667a88bb3f7497e3308a91de6fd646d6a3562c92c24272411229a90802" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "3fd56897a62743e0ab4a465bcc9777d5fd21ad2c9a59d7e4e1a60feccdc722b9820ec65cb47e1d1160d12ff2ea93abe11bc101b82514ead7d542007fee7b4e2dd6822849cd3e82d761ff7cf5ce4f40ad9fec54050a632a401451b426812cf03c2b16a8667a88bb3f7497e3308a91de6fd646d6a3562c92c24272411229a90802" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fe5253d4b071793b081ebc122cc2a5f8" );
            pt_len = unhexify( src_str, "b57a0bd7714ae95e77fa9452e11a7ed4a2bec60f81ad6ddb956d4b1cb5dfc277dcb4034d501801b26733b5e08c710c3cfdccc1b208dc7a92cd7ebe166320582bcaff64cc943c36fbe7008f004e5db70c40de05fa68b0c9d4c16c8f976130f20702b99674cd2f4c93aeaeb3abca4b1114dbc3a4b33e1226ad801aa0e21f7cc49b" );
            iv_len = unhexify( iv_str, "49e82d86804e196421ec19ddc8541066" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e8b8ae34f842277fe92729e891e3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "c4a31c7ec820469f895d57579f987733337ec6547d78d17c44a18fab91f0322cfe05f23f9afaf019cf9531dec2d420f3591d334f40d78643fd957b91ab588a7e392447bd702652017ede7fb0d61d444a3b3cc4136e1d4df13d9532eb71bcf3ff0ae65e847e1c572a2f90632362bc424da2249b36a84be2c2bb216ae7708f745c" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "c4a31c7ec820469f895d57579f987733337ec6547d78d17c44a18fab91f0322cfe05f23f9afaf019cf9531dec2d420f3591d334f40d78643fd957b91ab588a7e392447bd702652017ede7fb0d61d444a3b3cc4136e1d4df13d9532eb71bcf3ff0ae65e847e1c572a2f90632362bc424da2249b36a84be2c2bb216ae7708f745c" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b3502d6f0d172246e16503cdf5793296" );
            pt_len = unhexify( src_str, "09268b8046f1558794e35cdc4945b94227a176dd8cb77f92f883542b1c4be698c379541fd1d557c2a07c7206afdd49506d6a1559123de1783c7a60006df06d87f9119fb105e9b278eb93f81fd316b6fdc38ef702a2b9feaa878a0d1ea999db4c593438f32e0f849f3adabf277a161afb5c1c3460039156eec78944d5666c2563" );
            iv_len = unhexify( iv_str, "6ce994689ff72f9df62f386a187c1a13" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "21cdf44ff4993eb54b55d58e5a8f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5fb33dd73db309b9dfd3aee605cd94bf" );
            pt_len = unhexify( src_str, "f4e011f8c99038c46854b427475f23488077ebf051c4b705a1adfdd493a0a10af7a7e9453965b94f52f61ae62ce9243a82a2dbf9c5a285db3fe34ed34ed08b5926f34c48171195f7062d02a6e6e795322a0475017371cb8f645cdcac94afc66dc43e7583bdf1c25790f4235076a53de6c64f3bc5004e5a9ce4783fbf639fad97" );
            iv_len = unhexify( iv_str, "3f6486f9e9e645292e0e425bac232268" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7ee5e0e2082b18d09abf141f902e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0503cb531f1c967dae24f16dd651d544988a732020134896a0f109222e8639bf29ff69877c6ef4ac3df1b260842f909384e3d4409b99a47112681c4b17430041ca447a903a6c1b138f0efbb3b850d8290fceac9723a32edbf8e2d6e8143b1cbc7bf2d28d1b6c7f341a69918758cc82bbab5d898fa0f572d4ceaa11234cb511ec" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0503cb531f1c967dae24f16dd651d544988a732020134896a0f109222e8639bf29ff69877c6ef4ac3df1b260842f909384e3d4409b99a47112681c4b17430041ca447a903a6c1b138f0efbb3b850d8290fceac9723a32edbf8e2d6e8143b1cbc7bf2d28d1b6c7f341a69918758cc82bbab5d898fa0f572d4ceaa11234cb511ec" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a958fe3b520081b638d9e4c7d5da7ac7" );
            pt_len = unhexify( src_str, "dfa9487378c7d8af9c8dbd9e533cd81503d9e4e7dab43133bad11fd3050a53a833df9cc3208af1a86110567d311d5fc54b0d627de433c381b10e113898203ac5225140f951cdb64c6494592b6453f9b6f952ec5ece732fb46c09a324f26b27cdad63588006bb5c6c00b9aa10d5d3b2f9eaab69beeddd6f93966654f964260018" );
            iv_len = unhexify( iv_str, "c396109e96afde6f685d3c38aa3c2fae" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "06ca91004be43cf46ed4599e23" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ec319fb143eac8215b51541daec268f2" );
            pt_len = unhexify( src_str, "d298d988e74927736237eb8ab09d7a86b854fa2fd1f7f3be83b417ac10aa9291f4af5b3fbaf75a296ac32369ad57ded3984b84711953e477de3035ba430a30ffb84c941936e6c8d2cae8d80159876f87dd682747f2dccc36d7c32ab227032b8ac70b313fa4202ea236e3ec4d9e4d8b48cf3b90b378edc5b1dbeec929549344f8" );
            iv_len = unhexify( iv_str, "8a4684f42a1775b03806574f401cff78" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e91acb1bfda191630b560debc9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "27ce4a622959930f4059f247d29d1438257093cc973bf1bae4e0515da88b9a7e21ec59c7e4d062035cdf88b91254d856b11c8c1944865fa12922227ded3eecccaa36341ecf5405c708e9ea173f1e6cdf090499d3bb079910771080814607a1efe62ec6835dc0333d19dd39dd9ea9f31cd3632128536149a122050bb9365b521d" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "27ce4a622959930f4059f247d29d1438257093cc973bf1bae4e0515da88b9a7e21ec59c7e4d062035cdf88b91254d856b11c8c1944865fa12922227ded3eecccaa36341ecf5405c708e9ea173f1e6cdf090499d3bb079910771080814607a1efe62ec6835dc0333d19dd39dd9ea9f31cd3632128536149a122050bb9365b521d" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "14a3e69f351ac39b4297749a90c1365c" );
            pt_len = unhexify( src_str, "051224f7b208549dcfda5f9d56ce5f0a072ef1f23f3810c693516c92622be6ed4d7a9e0f9450980ba490b2e9e3468ea7eef10bc9ebd673d91f32b748c1bf2c50cc4ebb59fc409c6d780bba00700d563ce1dc9927a6c860095a42ed053f3d640debfbfa7a4e6d5de234af19755000d95e7f414f1f78285ee165410c020038286b" );
            iv_len = unhexify( iv_str, "eb1c6c04437aa5a32bcc208bb3c01724" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "e418815960559aefee8e0c3831" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "797310a6ed9ce47cdc25f7f88f5dbbf6f8f4837701704d7afced250585922744598d6f95ba2eecf86e030cc5ee71b328fc1c4f2d4df945d1b91a2803d6ae8eba6881be5fe0f298dd0c0279e12720ede60b9e857ccca5abe9b4d7ee7f25108beebbfe33f05c0d9903bf613c2e7ed6a87b71b5e386d81b3ae53efd01055bbcccc2" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "797310a6ed9ce47cdc25f7f88f5dbbf6f8f4837701704d7afced250585922744598d6f95ba2eecf86e030cc5ee71b328fc1c4f2d4df945d1b91a2803d6ae8eba6881be5fe0f298dd0c0279e12720ede60b9e857ccca5abe9b4d7ee7f25108beebbfe33f05c0d9903bf613c2e7ed6a87b71b5e386d81b3ae53efd01055bbcccc2" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c34827771fc3918d1cee09ba9401b832" );
            pt_len = unhexify( src_str, "ce79701b661066e53191c9acdaf677ad41622314898d7216e3f113e2e6e215d26d8bd139827f06ab3ea5c4105694e87db1dd6cec10e1f86a8744d4c541f08e40319e22ab42fc1a6c89edfd486b6f142c6bbbf84a73912e0b2e55b79db306ccabf839855afdd889e52ae981520c89e7dc29bb2adb1906cca8c93fcb21290a095b" );
            iv_len = unhexify( iv_str, "2379bbd39a1c22bc93b9b9cc45f3840b" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "26e1f6cf0d9e0f36dfd669eb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b1f9bd2006ec550b7b9913d383200b5d" );
            pt_len = unhexify( src_str, "6d9fc8f586d50d6e0128172ae147844e80136905d3a297497a9566ca7c7445029028f14c9950acee92a5c12a9150f5e024e01c7505dd83937542b0b1288de9c292ae8ad918a09b2edf8493540b74c73d2794f2eb6eed18eba520ddea9567462c83330f33d7892fcde0b10c73a4e26ab1bef037cec7e0190b95188e9a752fee6f" );
            iv_len = unhexify( iv_str, "ca28fa6b64bb3b32ef7d211f1c8be759" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c87aac7ad0e85dbb103c0733" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8b2cef1a92aa0af2b00fb2a99855d5bc" );
            pt_len = unhexify( src_str, "fd09525ef3c65ab5823e1b6c36b4a9449a3975c5d3a9e7e33c61fb32edcbb8e8c915b6202e3fbce87d73cc3b66d83d9ea7e1e353cc7468f08626932cf0235563e2a28953ee5a0afadb1c3cb513b1f1fc9a8a6cf326174b877448672f7731dd6430a51619da1a169ab302da5af5b38802f8bbf5890b5d9b45deda799679501dc4" );
            iv_len = unhexify( iv_str, "08d87b7acee87d884667f6b1e32e34d0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3bd7685318010b0c5fe3308b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "583e64631c218549923e8ad33b728d07f23b0f19d2aff1ad7e20d564c591db0e117caa8f21e3f3345e3d84f0ccbb27274cddf9274410fc342cb2a5d4aea4e925d0dd5350389ee0dea23a842ff3f5c1198374a96f41e055f999cfbc2f47ceaa883da8eb6ff729f583eff1f91bd3f3254d4e81e60d9993b3455e67f405708e4422" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "583e64631c218549923e8ad33b728d07f23b0f19d2aff1ad7e20d564c591db0e117caa8f21e3f3345e3d84f0ccbb27274cddf9274410fc342cb2a5d4aea4e925d0dd5350389ee0dea23a842ff3f5c1198374a96f41e055f999cfbc2f47ceaa883da8eb6ff729f583eff1f91bd3f3254d4e81e60d9993b3455e67f405708e4422" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "175c306f8644b0c4b894ae3d0971505e" );
            pt_len = unhexify( src_str, "fbe7ced7048f83e3a075661c4924eb77da1b4d6019d504afb942d728b31fd3b17557bd101c08453540a5e28d3505aeb8801a448afac2d9f68d20c0a31c7ef22bd95438851789eef1bebe8d96ac29607025b7e1366fecd3690ba90c315528dc435d9a786d36a16808d4b3e2c7c5175a1279792f1daccf51b2f91ac839465bb89a" );
            iv_len = unhexify( iv_str, "9860268ca2e10974f3726a0e5b9b310f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "f809105e5fc5b13c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "08c0edcfe342a676ccdc04bdf854b4b0" );
            pt_len = unhexify( src_str, "1fc8ef8480c32d908b4bcbfa7074a38e915c20ed7a1c608422087e89442d7c5af6fe9c9a716c55793248062d8e6c6e8e904e2804da3a43701e4c78ecdb67e0b25308afc6d9b463356439cd095cff1bdf0fd91ab301c79fd257046cba79a5d5cd99f2502ad968420e4d499110106072dc687f434db0955c756a174a9024373c48" );
            iv_len = unhexify( iv_str, "4a7b70753930fe659f8cc38e5833f0c7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9ab1e2f3c4606376" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "983458c3f198bc685d98cea2b23cf71f0eb126e90937cab3492a46d9dc85d76bbb8035c6e209c34b2a7187df007faabe9f3064dc63f1cb15bf5a10655e39b94732e0c6583d56327e9701344e048887a81b256181cdfa9ec42ebc990875e4852240ddcb3cbc4ea4e6307075fd314f7190f3553267bd68b19e954e310ec3f8dbab" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "983458c3f198bc685d98cea2b23cf71f0eb126e90937cab3492a46d9dc85d76bbb8035c6e209c34b2a7187df007faabe9f3064dc63f1cb15bf5a10655e39b94732e0c6583d56327e9701344e048887a81b256181cdfa9ec42ebc990875e4852240ddcb3cbc4ea4e6307075fd314f7190f3553267bd68b19e954e310ec3f8dbab" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "241067a0301edf0f825d793e03383ea1" );
            pt_len = unhexify( src_str, "6984bb9830843529fad7f5e7760db89c778d62c764fcd2136ffb35d7d869f62f61d7fef64f65b7136398c1b5a792844528a18a13fba40b186ae08d1153b538007fc460684e2add8a9ed8dd82acbb8d357240daaa0c4deb979e54715545db03fe22e6d3906e89bdc81d535dae53075a58f65099434bfeed943dbc6024a92aa06a" );
            iv_len = unhexify( iv_str, "a30994261f48a66bb6c1fc3d69659228" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "36c3b4a732ba75ae" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "03cccb5357bd2848332d1696f2ff90cb" );
            pt_len = unhexify( src_str, "5e2f18cbc1e773df9f28be08abb3d0b64d545c870c5778ac8bb396bef857d2ac1342ae1afb3bf5d64e667bf837458415d48396204fe560e3b635eb10e560e437f2d0396952998fd36e116cd047c1d7f6fc9901094454d24165c557a8816e0d0a8e0ce41e040ba6f26ca567c74fc47d9738b8cd8dae5dfc831c65bc1ba9603a07" );
            iv_len = unhexify( iv_str, "e0754022dfb1f813ccaf321558790806" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c75f0246" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "4e5e53c84a05d5a5348bac7b2611cf62" );
            pt_len = unhexify( src_str, "489c00c05dec06f282924c680f621ab99ac87f7d33ebbb4ca0eee187ec177d30d2b4afb4ee9f0dc019cf1a4da16d84b7f5f5c7fce72a32461db115b5a5a433024fd5ed3d47161836bb057a0189ed768f95e45fa967d0cc512fc91b555808c4033c945e8f2f7d36428dcb61f697e791b74e5c79b2bcb9cb81bec70d8119cd8d76" );
            iv_len = unhexify( iv_str, "47e40543b7d16bc9122c40b106d31d43" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "81eec75d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2c94008bf377f90b7a1c0d2ea38f730c" );
            pt_len = unhexify( src_str, "7b3d619d115de9970b2df4e1f25194940b3f3da04c653231e8e6946de9dc08ae5ba37e2a93c232e1f9445f31c01333045f22bd832e3b5f9833f37070fafb0ef1c44cc5637058ab64d9e07bb81b32852d4cf749a3ddbfdb494f8de8bb4e31f46033f8a16bc22e2595d023845505ea5db74dd69ab4ca940078b09efb4ff19bdb66" );
            iv_len = unhexify( iv_str, "abfe92931a8411a39986b74560a38211" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "47d42e78" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "69eedf3777e594c30e94e9c5e2bce467" );
            pt_len = unhexify( src_str, "5114e9983c96fecec3f7304ca42f52aa16cb7c6aadfb62ad537c93a3188835ca0703dad34c73cf96435b668b68a7a1d056931959316e8d3ab956bf64c4e07479c7767f9d488b0c0c351333ccf400b7e0be19a0fd173e3f2a1ae313f27e516952260fd2da9ab9daca478ebb93cd07d0b7503b32364d8e308d904d966c58f226bb" );
            iv_len = unhexify( iv_str, "a3330638a809ba358d6c098e4342b81e" );
            add_len = unhexify( add_str, "df4e3f2b47cf0e8590228fcf9913fb8a5eb9751bba318fd2d57be68c7e788e04fabf303699b99f26313d1c4956105cd2817aad21b91c28f3b9251e9c0b354490fa5abfcea0065aa3cc9b96772eb8af06a1a9054bf12d3ae698dfb01a13f989f8b8a4bb61686cf3adf58f05873a24d403a62a092290c2481e4159588fea6b9a09" );
            unhexify( tag_str, "5de3068e1e20eed469265000077b1db9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "208e6321238bf5c6e2ef55a4b8f531cbbfb0d77374fe32df6dd663486cf79beeed39bb6910c3c78dd0cc30707a0a12b226b2d06024db25dcd8a4e620f009cafa5242121e864c7f3f4360aaf1e9d4e548d99615156f156008418c1c41ff2bbc007cecf8f209c73203e6df89b32871de637b3d6af2e277d146ae03f3404d387b77" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "208e6321238bf5c6e2ef55a4b8f531cbbfb0d77374fe32df6dd663486cf79beeed39bb6910c3c78dd0cc30707a0a12b226b2d06024db25dcd8a4e620f009cafa5242121e864c7f3f4360aaf1e9d4e548d99615156f156008418c1c41ff2bbc007cecf8f209c73203e6df89b32871de637b3d6af2e277d146ae03f3404d387b77" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "45cc35311eedf0ba093bf901931a7036" );
            pt_len = unhexify( src_str, "5dc8d7525eaad035c19714ae1b1e538cb66a4089027245351e0ad9297410fb3a0c1155407c10a8bb95a9ca624a9c9925dac003ee78926c6e90ff4ccdba10e8a78bda1c4478162a0e302de5ff05fb0f94c89c3c7429fb94828bdcd97d21333c2ee72963ee6f056ce272b8bab007e653a42b01d1d2041ba627f169c8c0d32e6dae" );
            iv_len = unhexify( iv_str, "fed5084de3c348f5a0adf4c2fd4e848a" );
            add_len = unhexify( add_str, "6e210914e4aed188d576f5ad7fc7e4cf7dd8d82f34ea3bcbdb7267cfd9045f806978dbff3460c4e8ff8c4edb6ad2edba405a8d915729d89aab2116b36a70b54f5920a97f5a571977e0329eda6c696749be940eabfc6d8b0bbd6fbdb87657b3a7695da9f5d3a7384257f20e0becd8512d3705cc246ee6ca1e610921cf92603d79" );
            unhexify( tag_str, "266a895fc21da5176b44b446d7d1921d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9edb5231ca4a136b4df4ae22b8588f9f" );
            pt_len = unhexify( src_str, "493df801c57f8bb591955712d92d3fc34518f0599fec8533b2b4473364e1df4f560c12444cf50eeb584676b7e955c742189de6b50b8e012dfa6642f3679fb02bc6d8e08d1db88c8ae955a7946263e06494e17f8df246b672942661e5563302252208f2e00a0d77068a020e26082c291a75a06f63c41e2830292a418b2b5fd9dd" );
            iv_len = unhexify( iv_str, "c342e9bdabe7be922b2695f5894e032c" );
            add_len = unhexify( add_str, "a45c7f8032ac5144deef8d5380f033aea2786b0592720a867f4831eaccc6b85d3fd568aedc6e472e017455b0b5b30cf7a08ea43ca587f35e1646ecd9b4dc774d11e350c82c65692be1e9541cbd72a283bdcf93dc7115545f373747b4f8d5915ed0c42fbeefd3e9bd86003d65efc2361fde5b874ddabcf8265e6b884615102eff" );
            unhexify( tag_str, "5ed3ea75c8172fa0e8755fef7b4c90f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "56696e501fac1e8d5b83ef911ed11337d5d51ff5342a82993dd5340bb9632e6606eef68ec5fe8cec6b34ebbc596c279e6cbc9221c4cde933f6d93ae014e3c4ca49593f35eaa638606d059519bac3a3373519e6184e7227d2aa62170c36479fe239cb698bfca863925a4c9fb1338685a55a6dfd3bd9c52d8ae12be8551fce6e1a" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "56696e501fac1e8d5b83ef911ed11337d5d51ff5342a82993dd5340bb9632e6606eef68ec5fe8cec6b34ebbc596c279e6cbc9221c4cde933f6d93ae014e3c4ca49593f35eaa638606d059519bac3a3373519e6184e7227d2aa62170c36479fe239cb698bfca863925a4c9fb1338685a55a6dfd3bd9c52d8ae12be8551fce6e1a" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d5fdcb8f5225090e63fae9b68f92c7cb" );
            pt_len = unhexify( src_str, "d39b9cba95e3a3aab9bc1d03ff475c04faeb5b7f0510777f39e5a05756606eb7ddd154aac035d9ddaf3535629821dd8f014dedd52cd184f52fc706e3c89a3a271398c9125d9a624dafb297a56022ca2ea331ea7359ab5e65f8e14814788e64e0a886a9b1a0144bf268fdcf9d94c3d10a0452f40111da9df108252e9039eacea3" );
            iv_len = unhexify( iv_str, "581c818282a0905df5ffff652e5604e9" );
            add_len = unhexify( add_str, "f1ae6cd7b07f261105f555cf812a1d5bf8dd9aac07666318acffa11abb77d0238156663acbf7543825b45c6e9cddb481a40995ecd78bb5f4cba5df7c7efb00fc19c7f45e94d37697aca8ef368b99165393b6107f900194c797cd3289cb097eb5915f2abfd6aa52dd1effffdde448e30075a1c053246db54b0ec16eadca1c0071" );
            unhexify( tag_str, "827e66b5b70dce56215cfb86c9a642" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "cec11a12e47fd443f878e8e9fe23c65f29dd2d53cec59b799bcb0928de8e2f92fe85c27cec5c842ef30967b919accafe0c0d731b57f0bb5685d90a3061cb473e50e8aeca1346d1f47f7db06941f83f21ba5976d97c28cab547d8c1f38387a04b8a0b212da55b75fbaf9562eeeabd78eadcbab66457f0cd4e0d28133a64cb063f" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "cec11a12e47fd443f878e8e9fe23c65f29dd2d53cec59b799bcb0928de8e2f92fe85c27cec5c842ef30967b919accafe0c0d731b57f0bb5685d90a3061cb473e50e8aeca1346d1f47f7db06941f83f21ba5976d97c28cab547d8c1f38387a04b8a0b212da55b75fbaf9562eeeabd78eadcbab66457f0cd4e0d28133a64cb063f" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "036198cd3a3ab9319684d0f811cf2992" );
            pt_len = unhexify( src_str, "6b95b9e82a695fb7b466ce3adb536f525d8314f95eada39efb49baf121093ce7d5439f0d8223e03530b85accd388a70650ca9f7e63eb32afecb7b1916ed9b762128cc641caf3e08e027c3d88481d653b6b15172e977dfb9b3f88465911aee162501cbf8501ce2b66ee151bbfdc23225f638f18750c239d62471663e5ee2a5856" );
            iv_len = unhexify( iv_str, "47dffc6b3b80ffef4b943bde87b9cf3c" );
            add_len = unhexify( add_str, "ec4de476cd337f564a3facb544d0ff31cd89af4c3d9a28543e45156189f8eff8f804494dda83a1fb2c30ce858884a01ec63db59268452b1eea0f0d48280bb7340eaacc84509469dd94d303774d053d7ab4fb5f6c26581efeb19165f8cb09d58ec314d09ab8356731e87fd081f661e7b2d1a7c3aa4af5448a12b742e7b210b0b0" );
            unhexify( tag_str, "6cf68a374bea08a977ec8a04b92e8b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "5c2f7c408167be3d266ff634e1993fe291aef7efae245fa0b6b5bde886a810c866ae6a078286684d1b66116e636e285f03646e09f3c4ed7b184e7c171ba84f3bfd9500c6f35964a404892b4cdcdd3f697fc5b01934a86019810987a9fea7efca016049873f1072f62df3c17f57ea1d88ccd8757f7e3c5d96e8a18d5366a39ea9" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "5c2f7c408167be3d266ff634e1993fe291aef7efae245fa0b6b5bde886a810c866ae6a078286684d1b66116e636e285f03646e09f3c4ed7b184e7c171ba84f3bfd9500c6f35964a404892b4cdcdd3f697fc5b01934a86019810987a9fea7efca016049873f1072f62df3c17f57ea1d88ccd8757f7e3c5d96e8a18d5366a39ea9" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c9fbbff8f25f951ba874dfc5ff38584e" );
            pt_len = unhexify( src_str, "ca401071396da00376add467490abc6e6a7d8a85852026979f7013a09cf689113c8d833560cd6c5b8fdaa8fdd818e773ac13954839a0a2c91efeaf4e0e14de43308419a8b86fa2ae600a88a6bd39dfaabc16a3c7c1b77a5c2aab7f7caceb2f8595324125efbb7c96ba16c47d0bd10568b24bf445d72d683268466e68e46df500" );
            iv_len = unhexify( iv_str, "1c1fc752673be6d4ff4cc749fc11e0fe" );
            add_len = unhexify( add_str, "abfde0b60acfe265b62ed68ebebc1f5f725f155c4b8a8aeec8d704701c51ff7817060c1b0ce6b80d6efc9836c9ea2bc022ec67db4cd34e945e3a1b153fd2e0f7ac84bb4b07e04cbb529ee24014b16067f9f082b940c9d5e54024d3e5e910310457478560721587da7b5343d89eec5a8fce389c01185db15e7faa9a3fa32e8ab9" );
            unhexify( tag_str, "ff0b2c384e03b50e7e829c7a9f95aa" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "239637fac6e180e71b2c9fa63ce8805f453d81499623ec2deba9b033350250662897867bffaf0c314244baf9e1fe3e1bb7c626d616bfbf3e0ac09a32aaf718b432337c9dc57c2d6fc4a0a09bdc05b9184d1b90c7193b7869f91e2caa8b3b35c10c6621ffae4c609bdf4e4e3f06e930541c381451ef58f4f30a559d2b79b0e6b6" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "239637fac6e180e71b2c9fa63ce8805f453d81499623ec2deba9b033350250662897867bffaf0c314244baf9e1fe3e1bb7c626d616bfbf3e0ac09a32aaf718b432337c9dc57c2d6fc4a0a09bdc05b9184d1b90c7193b7869f91e2caa8b3b35c10c6621ffae4c609bdf4e4e3f06e930541c381451ef58f4f30a559d2b79b0e6b6" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3a314ec178da96311e42334a616fb38b" );
            pt_len = unhexify( src_str, "518b3f5384ab54f80497d55be7a5d6902bc7718386212c2ec7537db331514b3838f104bf9054e03039a4cfb73f41e5d0a9648e569ed738cea8d33917430dff6afa8f07a75e324b9262fa196a4439dcd66b0535ee5bea0d292600227c2a79ed03be0671740e5cb7b306d855612bd3abcbf02cf7e7cecbb6cdbb33d57b4e3234a2" );
            iv_len = unhexify( iv_str, "d7ea27c819e3eb2666611bb1c7fc068d" );
            add_len = unhexify( add_str, "db8dcc31a5681f13d56abd51bd2dcb0d2b171628186e215a68bf16167b4acd00c3441973c3fa62fa2698ee5c6749fc20e542364d63c40756d8bcff780269e5201bafdced3cdc97931d8203873431882c84522c151b775285d0a3c5d7667254c74724ff0ea9d417aa6c62835865dfded34edd331c0c235a089427672c5a9211c9" );
            unhexify( tag_str, "1e774647b1ca406e0ed7141a8e1e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e818372a63b7e2c23b524e29ba752bdb" );
            pt_len = unhexify( src_str, "c1bf1b702a95ceaa6b48a1cdd888ae51f58a9fc3232bd6c784529a83301c6d0cdda6e605ad9a2563f54a8d59f624ae7c589e48b85041a010dcb6fb8739d43e79a456fc0e8574af086df78680460c3cdc4e00dc3b9d4e76b0de26e9aec546705249fa7e7466c01001c2667eaf2813be1f0f116916f34843a06b201d653aa1b27e" );
            iv_len = unhexify( iv_str, "36e617e787cb25e154f73af1da68cb06" );
            add_len = unhexify( add_str, "71801d69796c2ce36b043c157aec9fd2e06fd1ec596126d10c26b6d44e3dc36c4fa30a030d65c382b6ddfd958e71fe9c16732e595137a3d6764c15480fc3358e9a113ba492b31274663f5842df5d1cc6bad70e83b34675a4411e2e70755aede0ff5035601be130562e27a20283d6f144ff1bdb5276dec05fad80d51b28d50688" );
            unhexify( tag_str, "3744262bc76f283964c1c15dc069" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9a04f16882ff45816739d1b6697ce8b7" );
            pt_len = unhexify( src_str, "6a4f3dbb3371f64258fd1f831349e745a4e19a33aad794b1de3788729618beed619586092120e9e5dc3ac6e0d52f991f7be61afbfaa4399ac716ad79a2734827254b1627791dc92a128a6f43426b8085dee94242e83176a3d762658f18ecc1e37e3e1531648c9caed212ea2cf3b3843cb92cb07730f30fe2dca3925470fadd06" );
            iv_len = unhexify( iv_str, "66f504d9a9128ad7fb7f1430d37c4784" );
            add_len = unhexify( add_str, "f641c53c83c4fb1ff8044bfa97cdf63fe75d8159d65b3e5ad585b89c083a53cf4a2f7a58eaeaf45fa71f2c07bc5725a6b03307d7f32884a133a4c803700bf1e12564b98b71f63b434ddf13ad2c467dda25ffa6effcafa72452b20c34cfae71e47096f8745b487e9f1945f5bec83f7ec2709a13b504d92315b1b727a78902be84" );
            unhexify( tag_str, "fbb37084396394fecd9581741f3c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "38cf029a4b20607030586cd2d82146e6" );
            pt_len = unhexify( src_str, "f4c9f4476561c9ebdac71b282ae6e2f9f03547da98e66d4d857720db2fcc9ed1f363858db34c9dcaca0109d7c81db24150493115f2bb6985efa8686e3d2ab719d33b230aa4c5c70696bf42f225fb3c6704711c054a882d89b320884a78cb59cd2100496edf4010487597fb9135d8ca79693a43843e9626fd6c64a8722b3a27dc" );
            iv_len = unhexify( iv_str, "6330084319e2bf32cd5240f4826944bc" );
            add_len = unhexify( add_str, "80746cfb0127c592f8164d751b0e14a5b379056a884cece7ee4e9b80538d7ff6be56a3b19c135786722aaf315123b47672b0251e87ea45f0fd3601cf93f9efa6cbd9ad537f54d57f1e187f821faac24096ecec19d137c9f4cf145c278af4cd8de01c7758784fda06f1cc62d92ae1977786f3d0645714ab4ab6f48c8794b12f73" );
            unhexify( tag_str, "7b021de5cda915ba58f90ceef4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cf4d81fc5997c744a572bed71f4ae609" );
            pt_len = unhexify( src_str, "f3d65d70326e641fbe7fd945fe9cf66c74f17d0d1020ae8ac488f39b7285c99d8632bc2201960f3d77daccfecc04428abe0853aa8d82b90a93127c72b2d2af53f7f1bd0afb99d50f0b3b24e934ec98eddb278b2c65866442cebf10208c7ce1b7ecf764858480b2a269b106fa6d2428d5ad17612e53e62ccc7ad1184663aeb9a7" );
            iv_len = unhexify( iv_str, "bc4e20c56931c967ce8e3b8f5f1c392f" );
            add_len = unhexify( add_str, "b6b8294abf7da5703f864721f7904d3821f5568bf4b269e44edef4f1c95ddc172d83a06c0ad9f7f1fd2e292c17a876392bc5bb705d370b2f16ff721bef7648f423346fd3a4d762676e6fcf2d690553a47224af29afed0f452d263be90eb8150a13d720f1db6f1abc1c2ec18cfbf93b8ed3c5aa7cfc1dcb514d69f90409687a4d" );
            unhexify( tag_str, "0a86142a0af81c8df64ba689f4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d88ad40b42ead744f1b7a36685658be1" );
            pt_len = unhexify( src_str, "e99d2566fe6bcb2a04d167605db7c0f1e5567ff2d8d3292c15bbccc5d1e872bcb15a30b3bb8b1eb45e02fba15946e6bca310583a6740845a0f74f4ebfd5c59ced46875823e369e0447cc3e5d03dae530adf3c9846362c94e7f9d17207bf92d4d59981d8fd904eb8b96a0a23eb0f8d7e7a87e8e8892a2451524da6841ce575c27" );
            iv_len = unhexify( iv_str, "52c3158f5bd65a0a7ce1c5b57b9b295e" );
            add_len = unhexify( add_str, "dde2663335c40e5550ae192b843fa9fb4ef357b5c09d9f39dafda3296a4d14031817ee4dc1a201d677597d81e37050cd3dc86c25adbd551e947a080b6c47ec7be8a927ef7920bd1bb81f2c59801a2b9d745d33344cbe4838bcf2eb8dce53ab82c75c9bbab8e406597f6908aaa81fbbdef25aa69116c8f7a8cdc9958435aa32ac" );
            unhexify( tag_str, "7643b3534eb5cb38331ed2e572" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "6f87f6be2f4e7421aa26fe321045d1e23066a02158634bef35890581c92367d0bc232940de30974c70a66c60137a9f3924d12db1e5bc1b0e7131ea3620a25eb805b7d670263b82c8bbfcd6839305025390fc17d42d82daebe1b24f73ff9aa4617e3866785dded88f8b55ef89b2798ea2641a592a46428d9020f9bf853c194576" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "6f87f6be2f4e7421aa26fe321045d1e23066a02158634bef35890581c92367d0bc232940de30974c70a66c60137a9f3924d12db1e5bc1b0e7131ea3620a25eb805b7d670263b82c8bbfcd6839305025390fc17d42d82daebe1b24f73ff9aa4617e3866785dded88f8b55ef89b2798ea2641a592a46428d9020f9bf853c194576" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c3ce86a212a30e724b4c624057db4e79" );
            pt_len = unhexify( src_str, "3582ef7a9565c9a8e4496750ee5ca3e3a80df6238f7b7608e3394ec56d1360777921da039ede34abcedd01081babd496ba4de74a7de501181d6bb2022a6cc7f79d89a4c6a97676fb0f2b42f70e2d0bc1eaac364c3646df4f611c1d6b09737451b81b5a4da73c05fb58391c74e44498b80b26f1c29562d23c39b5d3f086b280cb" );
            iv_len = unhexify( iv_str, "9e03f0dd4cb2b3d830a6925e4400ed89" );
            add_len = unhexify( add_str, "92c48a39d93ea3308f55f6650d33fdf17a902076d582a94a82ac99496de9f62312292b844bbca5a683ef0f0710bbc1c7f89cbcca8f9c0299f154590d32059bd99fca5d78c450ede0d11d55075947caf2151218ce7a06c1e81985a7781a3444054170b457fd7ba816026310112abb47c8eddfd3ab7f679a0f60efc6c6dd3b759e" );
            unhexify( tag_str, "3230fe94b6ccd63e605f87d0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "052347a4273cddba65b2a0b961477f07edee440a9117ab204359d2dd45ad2a6dad3b60ead891e7da6d79f3017ac90f95725a0089f04d25ce537bf53b7ea8e1ea58692d34c221db141e2a9fd7211adcee03ef8b5bf3c5d36311d20bb3d81f70f7e7272d0e2b6d12293b1a2c31b70f140a8f08d98c6231a3c429c3d0a10b2e1c1c" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "052347a4273cddba65b2a0b961477f07edee440a9117ab204359d2dd45ad2a6dad3b60ead891e7da6d79f3017ac90f95725a0089f04d25ce537bf53b7ea8e1ea58692d34c221db141e2a9fd7211adcee03ef8b5bf3c5d36311d20bb3d81f70f7e7272d0e2b6d12293b1a2c31b70f140a8f08d98c6231a3c429c3d0a10b2e1c1c" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a0155360b84420b5bf4fb410ea02f31e" );
            pt_len = unhexify( src_str, "ecdb51522fc440f7471ea6a31f7c1ef1ec2153e5bcf6303297dbf8ddb3830b45ed9866157375ce4bdeb5e32fcbc6607984fccd7e6552628736608ab13072856d432ceccd3e90d1bb52ca9ada9cee90eb89ac10e887a1978fd0fb3d7bb20caaf35539e150be8044b725b8427c4c4a910f79980865d36344a8784bcc3d58460acb" );
            iv_len = unhexify( iv_str, "46f0386be7363887e7e357376305eab5" );
            add_len = unhexify( add_str, "611bc290f91798ad84f0a5ecb5a7cb8fa35e9ab6a5a51c9869a68a076e96f92c9c117595f92cbac5d33343fa2accd2541473907cbc54792c5e215ae857424c921b04ca4b81376bbedbfcc0e565c118f2aced08f247698eed5e2d202c48245161cabeac9fa195219f9799fa253e339561e13012167f1d02b4012b7791b7c863ba" );
            unhexify( tag_str, "ac5addcc10cae6c1345520f1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "694f621f594d96b16c32254ff06f3f9c" );
            pt_len = unhexify( src_str, "e61476b8b7f101ca6005f25af2b9bee795d62720bbbf59357057ca7cd473e00f0d465255fce8d6164657603323549fb4e3d33fa51054b1a70cc7e492916dea85453e9107fe781bfeb4a622c5b2306a8dddef99386dc50745003aa7220cd7f32fb0a060fa7682576769a48f9169c7d11fe0a8a61b95f5d6dfcf216f7d0c652a84" );
            iv_len = unhexify( iv_str, "542db4e107485a3cd24c7ad337a4f1b5" );
            add_len = unhexify( add_str, "27b7bfa5eb34ba376e515e58ab8b6556c396820d0074a1fe3b984945dcf5251ca450456ccb4bb66ec739b03fdc5f72d24553e843255adc012d1f1c95aa3cdac5d12926465354217203052cbd4869a8b5be2e01d0fe66b5a6a8da0a2ce351557e2991ce77baa812b9c67b8e1c5a1fc348710e1a73a0fd49acfd538b7db6bef8b3" );
            unhexify( tag_str, "0bdef4d771a1740381e7db97" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "8b27a338fd2153d304f04655e09bd9bdf4468890ecce1e3b51de2c9a25a8d9336a9acd753ce270b1fe8d50196feac68145e0fd59c9cb3aa7c1e8af03494bc4279c6e287c849f3c775ada584ae173100946ae6921ef7c96bbc6f216093548702cf1867bb1bf1f4c9e90a34230a2b2aeb584622dd615023a43a406e64428bd9170" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "8b27a338fd2153d304f04655e09bd9bdf4468890ecce1e3b51de2c9a25a8d9336a9acd753ce270b1fe8d50196feac68145e0fd59c9cb3aa7c1e8af03494bc4279c6e287c849f3c775ada584ae173100946ae6921ef7c96bbc6f216093548702cf1867bb1bf1f4c9e90a34230a2b2aeb584622dd615023a43a406e64428bd9170" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "78826a5215a1d5e1b39cad5a06861f8f" );
            pt_len = unhexify( src_str, "0fe2c798d7015d3e2f8725648d95729c45d357dc0c89fc63b9df5a68d3e65419540f663e9190793a29c58c495d5c6a731782acf119e2df8a96fb180ad772c301d098dbc5e3560ac45b6631a01cef7eed6db51f223775d601d2e11b9baa55e2f0651344777e5a03f6738a2013626a891b5f134f07b16598b8cbe3aeaefa1c2a26" );
            iv_len = unhexify( iv_str, "feb9d740fd1e221e328b5ef5ed19eff5" );
            add_len = unhexify( add_str, "ca9411b368d8295210d7a04da05a351d287f2f67d978ef1bb936de9f8065473f6fa11495da2eab13a1002231c86411d5409bbc718e2042ee99e013b1df1ef786e9fc1f2d43293c854128184efb9317c4ef82a002eac8b28fcd91d8a714a3aa25fc3c0ae4af9f4bcf5ad19a30cd8ec4b1785df70aa92074da419abe433dd4c435" );
            unhexify( tag_str, "a724bbb295a02883" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d450f5253251121606e56687952bf2f1" );
            pt_len = unhexify( src_str, "479b4f421bd8ac7f615c4a507da187cb5d4b1f1e2c6113d1f9678c1ba92dc5e17c5b525d7f3208733223eb82af0820b8476e9b08ca714ce044417b24d2238720cb8ffdc69db558cbaff52e3651b400e16c9d5ac8ed8949a19c35516f80394a04bd1cfdced7b204f779d792086e00b2ebca2f55a1140e85f5ee9ac7cfc5a31747" );
            iv_len = unhexify( iv_str, "fe7ff90b020fc77d7fcd90bc583850ac" );
            add_len = unhexify( add_str, "a3bca9ff25a60006eb18f993dcdc99681e414e27605264dfd25652195d7fe1489550afd07fc7346b88d93b59eb6642913646e93bf50ee1db5dd30106cf181124d8ad01c72ed99038c9798620abdf5c78c419b08c97f982b34d9e9105d9aa4538afcd37f62e2412f14f7a248fcd60abaf2b66cd4554767f99030f1a495d56a5ae" );
            unhexify( tag_str, "6446398aff73ed23" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "90a59f6b0abf932311f0b65623c17740" );
            pt_len = unhexify( src_str, "be5a948a771a8df12adaf74d702f064a75f6483c03203365fbde7d184844fe6dee0b84cf344be05b1d163817ba1516fcb87b9167ed81f884ada73b0058e2b38cba515bbbe462f4c21f8de1d41bca2cf4340aa659f9f07886c2bb620d9c3295318c07fa3c17fe8242409359c08bcb337e5cf268880839b6a20f4ee4b3f04e7024" );
            iv_len = unhexify( iv_str, "20778bea82a6717038e7064f48a31981" );
            add_len = unhexify( add_str, "4022d04f1454a72d2efe57533bd32757595220b20f3a37d166cec0412fb1eb2588f939ecd906c805f4827338669888e9f730905001eb1b136b95e306edf70d9ba1e5cd0aa13a25a1f28ab55cff36f9cd7036c735e3b285d26002ad2ed1074b566e252ea3ec8a9ce10882375dc3f1d9676e301dcb179eaae991120b796cc35648" );
            unhexify( tag_str, "dc77c1d7e0902d48" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6be4ef629f0b38194c74f7b66418922d" );
            pt_len = unhexify( src_str, "b67ea20a320f4ec0e4185c62a4ad79a3c97a8189a5e4d1deff9d3edff0f9a9323532853c1a2a2c1e62e4d1afebfcdf1d8461921ea601750380e63b912d8b7389198f976851d88a19f1aa32c97143668ad00838d98da1c4f2be0e6e2dc964d170d7f7ad2e2997982e5ca110e744b6e10c24ca18eadff6b129b1f290c8a7e0a593" );
            iv_len = unhexify( iv_str, "fb77a4b9b246271abfc656433f87628c" );
            add_len = unhexify( add_str, "e5d5227725a19a3050fbf2a97a6e854bc1218b94a4a3403b721ace3447daff68fff5553a26edd41219e68fb61fb9e964d0a3c29796251ae4eb942187cdc55d13a09dfb487e93d9e2072d7271456a77c6ccb81154443eea176314d6e3a08619b52cd880f1c28ae5214ac0090a3855dbd74f87389fe8afebd464330fb683dff81a" );
            unhexify( tag_str, "3d8fc6fb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c50e37244931e8debc12b3d561c83ba2" );
            pt_len = unhexify( src_str, "b9abf0796f2d2f774735546cf809030f65ed0c7f6bd469ef2fe0ef32aa0225b57fbce07c36017bbc1806a81ff1a429278160a07643f864485b4e0e35d57553dc1a131e32aa10f1f91d663b10f0a418f472ed7b4bca54fd7ffdbb22c4d7764d94a7ffd04730614459431eb64335b9b65363de292c04275d40a7b968c0f5c486e9" );
            iv_len = unhexify( iv_str, "6c0b1fd7ab424a6883c36457d1b5521f" );
            add_len = unhexify( add_str, "516dc25f6452ae169ce293c5cee440de47353ca5ba770dca0f04175950e87a2d4c3f84fbc6eeacaac436853492929680066f959e74de4b736ab924d8367b90aaa6e9492561ad4b5aa78b6737d562e960edc3b983e2e01a186e9f22896f48d8dfcfb6a42cfe2c6006c687a27772820a1e8875bdf09e8104248ce4db883376bc04" );
            unhexify( tag_str, "7d4393f0" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "962509e494f10269b70ebad02b0cd799d1d41191a734863ef502aff3d3ba48dc2acf9da9a3fc3f40be4d210dc5e128bc00499aec57aa0a4669863165428687b88d46fad41e36af8ea6605586eaa5c0736d0d53b9d523e0cb5a0b285048e060a73cbf4b587d2cd787debdb2b4c8cda731a61a15b19fe8b561fbdd3a7373853ae1" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "962509e494f10269b70ebad02b0cd799d1d41191a734863ef502aff3d3ba48dc2acf9da9a3fc3f40be4d210dc5e128bc00499aec57aa0a4669863165428687b88d46fad41e36af8ea6605586eaa5c0736d0d53b9d523e0cb5a0b285048e060a73cbf4b587d2cd787debdb2b4c8cda731a61a15b19fe8b561fbdd3a7373853ae1" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "8531ddb03977383405baf2ee9ca7d64b" );
            pt_len = unhexify( src_str, "d90c9e26509bdba9b1dea8d2b94f2b1881d22c2bd756ad23cd61944710a1c1f2807170ed47a6870ae654e44757fcb3822ef28b37946cafc07284f8a0c22ae3552954f0d87b8d8c825bd546935b494cacb4262d9e2a88f254f200ad31367d8b3715afbabea5f34214ffedb14d7c84806022aba2dc8f88a314ffbb24017d1a9b9f" );
            iv_len = unhexify( iv_str, "baf623867d6a25fd85d1f08e599c0566" );
            add_len = unhexify( add_str, "18f92cdd37dcd7f99b06838f3f68748aba367baabaebd0da9ee787d70e752fa07dea553a43b643b8d8f460175c0746675205e20a7a98acfcac864d7c4cf5ab4c41c031738c76882acda003c5af47b1c4df8894a827a317935d970d4afaee17715c9cfd1883e8c345f19d1f89e229b8edba6b4f53b86d8da1c0f159afb83b6b33" );
            unhexify( tag_str, "2fc9de46" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "862dd5b362cfa556ca37e73cff7f4a0e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "81530a243655a60d22d9ab40d2520447" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "3b9b2af54e610ed0b3dda96961dd8783" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3452b7bc100c334292e08343f139b9d0" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8f92739a30fe4ba24079f5d42753d6ac" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0eeca69f8b95e1a902cc3ab1aaa8e2af" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "31a0cbaf21b943f8badc939e94eac7eb" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d5bb2c4eaec47088230972ae34fcda9c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "580e728512c8e44fbb3fe2c498e05323" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9e8fca537746e7cbff97f1dcd40a3392" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "43e9f2bf186b2af8cc022e7c7412d641" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4465a3f9d9751789bcef5c7c58cbc5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "35b5854ca83792ad691dbda1a66790fb" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "cff61cf9b32ea30cf7e3692aa6e74bed" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "726793199df533dd9055b0ac7c939d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "07259267c1c6a015437a5d8cfa92f9e6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "18b9cf2ad7ace6ec1c8366b72878cf20" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4340f6263f0ba2d82c2eb79cb0cc7e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fa1df8955aa3ef191900b06e7c1b7d46" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6928c138c98a4350c318fbdccd3f44ba" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7c89d9e77515d271b6ed54c9c4e3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c04200ce41ce77d772babb206315ec7d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "a885d58f0f38f9ff26d906fa1bfb12f4" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9ee0d025421f2bf18caf563953fb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "650df049461be341c3099bd1613dcead" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8a4ff6327b49d297248ce2d5bd38afa8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "13f067ef0d7b448d56e70d282fed" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ee61b5bf5060fcc637dc833926898508" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b2dcf21f9ffa4a883044d29f087f9b85" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "9ab1d66666d4dea3cbb5982238" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "01cc56ca7e64db7fbef66236a5c49493" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8ea5b63004189792cc040ef18b37e550" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d685aeb54aa129a21bed17766e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812800104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "134dd72ac8e28ab46720c2f42284a303" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "c6368e4c0ba0ec90fa7488af9997a4c7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "4ad9cdf19ff7d7fd7e273efced" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "180c04b2bde6901edcda66085f73ecd9" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "9193b206beade4cb036f01a9db187cb8" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "530f5e9ed0879ccef3a7b360" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "aaac85742a55ffa07e98106d6d6b1004" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "630cd8ab849253c4da95ac80324ecc28" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "37911820c810e3700c3a9321" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "ab663c4f8f2fdc7d5eabf6ef26169b4e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "86e6100669929e329a1d258cd3552dc9" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "958d6141f7fb2b2dc7d851a6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0dd756d49fd25380c4026ea03cafc2da" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "6a6f7e39b0d730ea1670e13d16c12c28" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "872ef05a28da5ea1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "bd8a834b288bdc7578b6c6ab36f5d068" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "aa77de0af5fa4dd1ed2ada5cb94813a0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "c5c094e83755f2b6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "020d280dbd06939bbb5e6edc6f6d39c6" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "09aea6f0e57598452719d6f63b6fe5a0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "05d6c56ba601e85b" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e47f41a27a2722df293c1431badc0f90" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "227c036fca03171a890806b9fa0c250d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "86c22189" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9d3e112114b94e26e93d3855d4be26bd" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "99b98525160c4bb2029da5553ff82b59" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "33bee715" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5b4b7688588125349fbb66004a30d5d4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b4ae363edb529d8b927c051cf21a2d9d" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6a920617" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c4b6c5b8e21c32f36b0ae4ef3b75d5cd" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "3d1036bf0000e6f1b77a799f2ef32dec" );
            add_len = unhexify( add_str, "1cf2b6cbe86a87b4b5bb3cc50024aeb27c48143658d47b41f2f20b87ed67bd6fc3b85a3a803f66d3576608f5d6ce6cad11e02fe12de5390722dccb8242e1dd140051bef51aa9716c860d45d45bca6effbb1a4797e6e7406a04db5d823766c0f011ebc28e9a8cd4446ec8a75ea8bdc1b2fdbb5cc364fa9877886e30404593df34" );
            unhexify( tag_str, "a49725014c214ef7cc2d28b9b2b53da7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "63c3f81500746eaf383fe3975d84f849" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "0799d4152fd73c1604b4610cf7171fe1" );
            add_len = unhexify( add_str, "cb8248e5f904cc9ccccf6f273fe621eee1b4d7ed98480f9e806a48b84e2d6a733772ecf8fb7fe91805715cddab2b462b89f6e6c7cf873f65031f13c357d5f57b00b7c391c39e78ad1ed94be236ca0ae316bce11bc33c5d701fdfc58abbe918b9c42f7b3d6e89d46f9784b388a6e6daf47730b9fa665d755a17e89932fa669c44" );
            unhexify( tag_str, "c53d01e53ee4a6ea106ea4a66538265e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b0c88b191ce6e8e4a3941f7960b7eae5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e2a899961c332c815685c553351fa519" );
            add_len = unhexify( add_str, "308bf10570af48d632911f3641dea60d78046211c01a63bb8e4e5cbddfff8841d2f2b11e18ccb2170805ef4cacf7804d64e0feef40731a1704907f33b77788c18ccf35b224ec3046a67664ac9a3481d2385b6ddeec6da4f32423f94ea9663a5c51cc388cef33744a8159b4fb654dfdb5092718bf926c824be31197f07f276b5f" );
            unhexify( tag_str, "92604d37407aff33f8b677326cbb94fc" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c818dfa0885a09f65ef78712f5ce6609" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ca279284723530fdd68ae880e0ce775c" );
            add_len = unhexify( add_str, "2a562abdbb483ca5f355f9cc1c5e607bdd624a078a76b717ce0f8f35d0d4c54b629f372f15d20c848d01420c6af5a7040d42063704a17b46259dcc53723caf2d4bf556143ff9117c752fa4f22c9c155c99b7bf5949d089cdafd562165b9cbf53ff51cec21f49128c8a599718bbcdb4a5d705d20509c44c8945e2a133164b9942" );
            unhexify( tag_str, "20e9a3a98d71d460743e1efaab13c6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "2354c6b6afaa883e7ce91faca4981f8b" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "604f2730c756c8c39a0527093bc2feb5" );
            add_len = unhexify( add_str, "959b4b0b9ce2e9120b327d2d090117553999ee10bdd384a546fc6de0957ef4b447daf07b3d07ef7dbc811f36b0fc09a175d26e4d1263cb5e21eda5ecab85d763807bb20b3cb6ac3f31d548dff00aae058d434ebcf6f7e3a37f11324134f453dd0ea7f51094863486426ff1706129a5a93c53d8c5ccb56cafa5881981fe233cb0" );
            unhexify( tag_str, "3588c9aa769897dfa328549fbbd10a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "b0af48e6aebbb6ff5b7c92bd140b085f" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d210d6502a5221ac1274a9c7f5a81725" );
            add_len = unhexify( add_str, "d725311ca10eb4b4aa24e6dd19c5e72dc34fc1ff53feb25d924a9b7d8d72205790ca4b1275bd93ad60c27a5587a45659bca07c111e9748fb683a03465153ffd735b7d134b479674ab8596f0596496fe2090f623fd1e4dd730c5283d8b172db8a25df42d9b34f388ed32676a56b8ba03347e47379702654508ccd0a21ff03516e" );
            unhexify( tag_str, "e6222f068a1e18f09ba6c771eabd86" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "a05fe482fe164b2eca7f6c3e377b39d8" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "145327bcc10335fccb93afbf4b17e6e7" );
            add_len = unhexify( add_str, "ea6f2e93b5e1bf127d40440b8d6397405246b1b48eebe16964f18928f6b4b8ee2c36322d7126905c1a5b816996e340404b586edc2d77afac11a6c1266511f9eff1a320b035442d4078f8e42ca63cf26d12a971a7adf4645d1bd9a8e4d0a20722f7c2d529beaecc4033f7738075e1cdc6d8a929da5582540678935b82e7b7ba68" );
            unhexify( tag_str, "3900bde9fa9ae2cbeee54d04f224" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "dacbadf819eb16a63f6f091d13ed04d4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "b9ebce724b0dcb0989ac2d8e7ff8aaec" );
            add_len = unhexify( add_str, "7dc6e2189d8a96f3507e352e05e8fd1b4bab988c2f1c706115887119f63b78084f015d85f6b460901a02880103e4d36e8f6527dfd74e4a3acd3f578c0cc726b528875f701ff8b66e5c11b4689c346a098e123bebfa253362cb86829be73c2b85a6881fa976aa730fabb76775027feec7fd920a6c8965a4a509ea812d7c413a95" );
            unhexify( tag_str, "8988fca83c8cfb1f8feefac46f04" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "969244c7444f3f3bf193b28f8e8e96dc" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "49b2845a1a1c87fa66eb8f78c05ac029" );
            add_len = unhexify( add_str, "1414a07e86d8b61d1eff43e1ff4ab42c1c95e159058b74c731e3007d21a5eb78bc17b7e920363a3974aeb8608813dc9a4655199b6703ed337450702d8ab16a89776831b2c7c811fec3acc23598a0aa01680a7bf42a4e258145beb08c9f0eacf2bb5f56d26bea3ad11e1a956a630b80f3d22bf35592b4704f7c464b08b06dd7f8" );
            unhexify( tag_str, "a291c7527385f037f62e60fd8a96" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "525abe490c8434802b69439c590a5290" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "141f79f0501316e66451c41c7af0f0cd" );
            add_len = unhexify( add_str, "be440db66d3f81be467605a7b2805ec1df5e71e1b1b04bd7a4d05e912f5aa1912ba08de72df18613b32b7edf78963c48c80c25178b3b19262b85bb829f5377e0b368b500d6d3b442f54172d4ca4500eb5b4d478b602e5dc11d090539455087ce1e5b9ea74355fc06e9b60cbf25a9804d3f8c623fff130abc48bc2d8d116b8366" );
            unhexify( tag_str, "038c7e95f790e6ca5ce73f9551" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "51644e025659de983f5c8156516b812e" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "614837c743d0974e9cca497f13038c02" );
            add_len = unhexify( add_str, "60c5d062ade2c5c2dec68b734dd3e58ec474a586d1c4797fdfa2337800510134cb27a10d501927632af3c1febc275010c0d2e5abee630cd2bc792963fa82a42286ab047b934a261927311b40f5f953bfd661427921147cac7613d95ee86e16326ef67c1ed097e8fb87a78753d785de34e03a182232786079cb6be00182e41c9e" );
            unhexify( tag_str, "77e3deba2c7f9386f85bc4a801" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812801024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "08566ca7310302dfb84d76ea0525ba20" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "5f20ec9c35c08aa7f1c0e8a20fdbd2b3" );
            add_len = unhexify( add_str, "5d84e32768b8d1e7e3c426b3118d48e35491bf1bb454b359c8429220216efd8826be94fe1919409a128ccd8125a594f1691c9421fc3dbbb3f757bf2355bb0d074ceec165eb70e26eb53fa2cb5d84dfae06babb557805ef7b8c61c1bc76137571bcc5e84bf5987dc49013831d78bd497ccc49cde7dca2cb75e7ab967da8c6ce81" );
            unhexify( tag_str, "873f037fc05252a44dc76f8155" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "dfb54db96383fa911bf5b4fa1218ef9a" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "7e849e24983f63f1194b396bbd2d55e0" );
            add_len = unhexify( add_str, "d3fb689c5818810dd104693f3306a10b27178444af26798a194f7c2ab31ff3a172904b951942b1a26c8ae5b5b1ee2d86dc78bb72a335fde350766d7d9aef6f549871dd46b04b2cc319fcdd47be437d431ad18cab82d51ca9fa57f4108a8de622a92f87d28c0349fab27757fd773413f559a8c00d30e258c1f6cd96f9759bd957" );
            unhexify( tag_str, "dada7fc7fed58db462854ef6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "389cf888474e9403e5f4d0e22ffec439" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ef57794cf6fac9f9cea3e8499b53b1d6" );
            add_len = unhexify( add_str, "7ea7f7f4763ad208eb6199285b6b2819756c4e3caf2d0ac6f5076ae6785fecdcc4b138a51860ff8b87aaac3a18c2df778a4818308d458dba28f5017513e1454f60be20dae68736ea6d48b1f9deadb517df63140acbd329fbfbc9b82f3ca1862c9e998f0faff1d3ae60b005bf66829f5cf0c5fa03efbdd92d39351e3954be0257" );
            unhexify( tag_str, "92726d90ad26130e65f2beb4" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e55abb2ca36c822bf2a030ac703cb8b4" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "d86f7177e8ec90f9e9edf10175d5012d" );
            add_len = unhexify( add_str, "777a9d93091de56324c10712243f5541722e0b27e1f303fef6faa387a8666161ab354dbea6c43c82a24e8623bfec39aab13164add6be0dfd55d23204c0975b4ba6fbda51363befde482a9ccc1eb9f151e6ad59c77a1e24dd268389e4686f198a936dd603044a3fb653d63cff80597f5a2913c8a2ec1b7d9dce5728dd56c78c2c" );
            unhexify( tag_str, "65025250343ed8c09b3fceed" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "586114f3b1dc087e1b2739b28c592dfe" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "ae5a38ddd455505284434a4bcfe81ef2" );
            add_len = unhexify( add_str, "531ff8c285e532d961f49bd210a5523cd9b19a697a3a3fb26db940a496f253862405b1e825daeda7eb0445c98022b8342c8f8ea20301618483f8ab04b6ebccd7e7fc57878fb544a5bf78fa896f50ac30126ff8afca8a86388666b64c643d16812729bfd7e5c03ba52f7e6ea4c6a685404f7bcbd956964417fa0ea9a6d7290c41" );
            unhexify( tag_str, "467a815610faeb82" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cbfe806bddb7f06b3826b097550c68f5" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "04c1b6c9fd2ab76fc2adfe15d3421bbb" );
            add_len = unhexify( add_str, "cfa86d02599652cb4ffff027b9c6ef2336dc9fe946f64fa5ce83f624e144563d4738381bc5371c3cb55cf41ceda07e62cb635ff37246bfa428785229c6e869d5df69d7949a8577889a29e3d05b788ddd43608d9c14e3f1b51ce2085b9a976fe843e3396a74922babe6797d5f01c37ead623b5b582505bcd29edf8a6ea36b0fc7" );
            unhexify( tag_str, "0697ac372a9acafd" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "96ce3a095a91effdd91d616f1f02ddcd" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "579d6633ec6687afa24ef874899b58e0" );
            add_len = unhexify( add_str, "3ff3c0038148ed391b6a10aad623a82fe9209c5ba74482f11506d597b5fc7af977235d8ee9e28cf2160346ddd0e33a5bd1fb67b87dad7167fdd4b2b4000d8460ef7b3e1b59b9d61d06cfbe7945379ed6b650de86f396a38cc70d47b8a349f067d00144c903c276b323be6a929a7d7dd8ae7d254d640cdc1176f98e01a1d8c82f" );
            unhexify( tag_str, "55a0f61032e048f3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "24ece168c2971cf2b404ea206dc9e29d" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "e9db62a42491664a6c46cbb0b2bafc92" );
            add_len = unhexify( add_str, "3579f6c0cb3d2a5d0c4548855c7c052d36b6a8dfc60f4ca1b4bbe28ed87306119e71982dd84c4205ceba918d675472753df1b5192d3693dbf6a061c6056e312135ffc5ff426895a7e30f7f675d2cb21de06eea5e3761b94deef7537b985d324864c9ff6ab6e230a1006720f98c958912b604a6d03e3979887c07be3ceaafc78f" );
            unhexify( tag_str, "d2b15a23" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d3c3cf993f6740a019e61ce13c29955c" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "af900ac348082ff32d2e0ab886079516" );
            add_len = unhexify( add_str, "2ddd0e8c99661f0757f04aa79a1ffa24ad48fbe5da68b9e71f7a0cf1b4f2ca9b757695900b7549d48847ae49950dc9b270b1569d29dcbef412216737bd83509c17ae41c34ccda318939cb37a0a380762993a7568c0b07794e78746173dd5c0d921cd50de4b548c1589e142c3dadbad42161aaeda2310f3c6d5c722d9ac69e96d" );
            unhexify( tag_str, "f2d3a6ff" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281280102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "5f1e5bd45ee8bb207ebbd730510ff218" );
            pt_len = unhexify( src_str, "" );
            iv_len = unhexify( iv_str, "8846424a194f5de858556e6be5b65d7f" );
            add_len = unhexify( add_str, "e968947fc0e49136e730b97f6b16e393d5e4fdf3e4803a23af79211ef59f29167c60ead72fd489da32d2ffa43b2bca2074f9d1b4f5396ca65004b0806cb7c6dfa751fb6afbee3e443f3c9b0e3df6722e0d1320441400c5ca508afb657c2b7f1669b0de21761dccab9a40fc513768bd1f552692626ce35078a2e0e12f5d930647" );
            unhexify( tag_str, "0d6c15da" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3997050377cfbb802cc438d973661688" );
            pt_len = unhexify( src_str, "b02f0dd373e42c65e8e1db2dd76a432e0b2bf6e630c8aaf0d48af51b3709b175de9a19b3245ae75818274c771c06fae225c4f8b002236712336e805ab006449eb29cc5e29abd82b06c32d4c36ee99acb9a6d7d9eae6ec6ec263c002a22c4a898c74f6abd6d92112367ca7ffe82787c5b39e7012ba22825d3612af3d41e8008a8" );
            iv_len = unhexify( iv_str, "c95c84c263bdfd5f1de66e7e616cf3fb" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "b35b3cf6ed59ccb69dbc9b47a3f284ae" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c58583f6479d9bc9f1bffddefee66e59" );
            pt_len = unhexify( src_str, "564a9f700cbc1f895e4f4fa6426f73b4956896a15e6127e7560d74e3fd0b980d2ee45b7a6a3884fa613d91d13921e3f90967d7132bdafcd146dd8ff7147ed1964c2bdb3e12f4133d3dbbc3bf030ff37b1d2147c493ce885068d9ba5bebae24903aaac004aa0ab73fe789e4150e75ddc2bde2700db02e6398d53e88ac652964ac" );
            iv_len = unhexify( iv_str, "cee448b48d3506ff3ecc227a87987846" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "361fc2896d7ee986ecef7cbe665bc60c" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "9cce7db3fc087d8cb384f6b1a81f03b3fafa2e3281e9f0fcf08a8283929f32439bb0d302516f0ab65b79181fc223a42345bad6e46ff8bcb55add90207f74481227f71a6230a3e13739ef2d015f5003638234b01e58537b7cfab5a8edac19721f41d46948987d1bb1b1d9485a672647bb3b5cb246a1d753a0d107bff036ac7d95" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "9cce7db3fc087d8cb384f6b1a81f03b3fafa2e3281e9f0fcf08a8283929f32439bb0d302516f0ab65b79181fc223a42345bad6e46ff8bcb55add90207f74481227f71a6230a3e13739ef2d015f5003638234b01e58537b7cfab5a8edac19721f41d46948987d1bb1b1d9485a672647bb3b5cb246a1d753a0d107bff036ac7d95" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0bc2bde877e881aea512068105694968" );
            pt_len = unhexify( src_str, "1a6369a45e8ef2846c42d54f92d0d140a94f9633432782dcbf094f1444a1d006acd07ef6076cd0faee226f9ff14adc1fb23e3c63ed818c9a743efbe16624981663e5a64f03f411dcd326e0c259bcadca3b3dd7660ed985c1b77f13a3b232a5934f8b54e46f8368c6e6eb75f933196fa973e7413e4b1442b9dee5e265b44255ed" );
            iv_len = unhexify( iv_str, "05f0c34ab2e8e8026b0a23719344b71f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "46bab9fc2dbe87b8f6ca0ed4d73e5368" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e14f45ba5d1eb52e0412240da5d7b5f9" );
            pt_len = unhexify( src_str, "9a85fda19ce923f093a0c25b0c52f5d9534828af7c7687d22307004ae2d10c4592242c0f2704070307ab55b137780d1e2013a19396ab43ff6a295b63fdcf323456d149758f9a2bb37f1418d62ea6368b24d5067b9c63d2968e06d6586c7e3275faffa005f7c7bfef51303e4c2b2ed4564acd17d50efac9f5e3e7f16ce589c39b" );
            iv_len = unhexify( iv_str, "d7f8ef12f66f8b7c60aea02ef6ff688f" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "beede05e4928c808bc660f3de95634" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "4ad5b9ace0c0c7c07df2900faf37a902899471e7aa4a0a1ad5387f8f56d73f78f619be79a4e253f95b15d52895a05bae9ecffa916d35efacd8baf1c704d2aa4a38c234efc4dcfb191ec0fa0b522328fa5b5dff55e8c443fee660ebe3d8ad85de157a889aefc823720030a4cd6ba94a6309dd61806f0abb27772432018bc61701" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "4ad5b9ace0c0c7c07df2900faf37a902899471e7aa4a0a1ad5387f8f56d73f78f619be79a4e253f95b15d52895a05bae9ecffa916d35efacd8baf1c704d2aa4a38c234efc4dcfb191ec0fa0b522328fa5b5dff55e8c443fee660ebe3d8ad85de157a889aefc823720030a4cd6ba94a6309dd61806f0abb27772432018bc61701" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "9a64579f3601b0022d357b601cd876ab" );
            pt_len = unhexify( src_str, "88be1f4bc8c81b8a9d7abc073cb2751e209ab6b912c15dc094002f95a57a660b9f08b1b34f5947223205b579e704d70a9ecb54520ce3491e52965be643f729516f5cb018beeedc68a7d66c0d40a3f392ec7729c566ce1e9f964c4c0bd61b291ccb96e3d1fac18a401a302f3775697c71edb8ff5a8275a815eba9dd3b912e3759" );
            iv_len = unhexify( iv_str, "515efc6d036f95db7df56b1bbec0aff2" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "13ea92ba35fced366d1e47c97ca5c9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "7fc8565760c168d640f24896c69758355b17310dbc359f38b73fc7b57fe3f4b6ecad3f298be931c96a639df3c5744f7e932b32d222f5534efb8eb5d5b98d218dce3efef5c8c7ce65738bf63412d0a8ed209071218a6fa2f7be79b38d0b2f5b571ec73f1a91721bd409b1722b313683e97d53df19ded95fd471124fa5f294a4bb" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "7fc8565760c168d640f24896c69758355b17310dbc359f38b73fc7b57fe3f4b6ecad3f298be931c96a639df3c5744f7e932b32d222f5534efb8eb5d5b98d218dce3efef5c8c7ce65738bf63412d0a8ed209071218a6fa2f7be79b38d0b2f5b571ec73f1a91721bd409b1722b313683e97d53df19ded95fd471124fa5f294a4bb" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1bda4acfd10ab635f357935bb0ab7020" );
            pt_len = unhexify( src_str, "c9ac8d4ef7d83848fdc03664957c28b9b76710797d5db1c21e713e85eb0898892223e52be1644fc7362c95026ebb9c9ca74d7d3739eff10cab1eda00c36628dae0b98d119a14635800e37cd340faa6fbba9c3d41d52722cc3969612b1a8c5ca9a68773f5ee654506cb88ea65fb1eddf5ab6312d0170dc03324e483342448b854" );
            iv_len = unhexify( iv_str, "48b77c587616ffaa449533a91230b449" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8325e4394c91719691145e68e56439" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "1287ad3719508a9be70c19e3b134a2eaa4415d736c55922e9abcfd7f621ea07ffb9b78d8a9668c74bbd548b5e6519ea12609d2d6197c8bd3da9c13c46628f218e7ff81884ff7eb34664ab00f86e09cd623bec248d8898ef054fce8f718a0e0978e8b5d037709c524114ec37809ac3fd1604e223e08f594e7aa12097f7dc1850b" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "1287ad3719508a9be70c19e3b134a2eaa4415d736c55922e9abcfd7f621ea07ffb9b78d8a9668c74bbd548b5e6519ea12609d2d6197c8bd3da9c13c46628f218e7ff81884ff7eb34664ab00f86e09cd623bec248d8898ef054fce8f718a0e0978e8b5d037709c524114ec37809ac3fd1604e223e08f594e7aa12097f7dc1850b" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d21cf24bc5bd176b4b0fd4c8477bb70d" );
            pt_len = unhexify( src_str, "2e7108fd25c88b799263791940594ec80b26ccd53455c837b2e6cf4e27fcf9707af3f0fe311355e1b03ac3b5ee0af09fb6fb9f0311f8545d40a658119e6a87ba8ba72cc5fdb1386bc455c8fec51a7c0fec957bed4d6441180741197962d51b17c393b57553e53602f2a343a0871ea2dc4b1506663b2768ce271b89c4ed99eec6" );
            iv_len = unhexify( iv_str, "208cb9dced20b18edddb91596e902124" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "7edfb9daf8ca2babcc02537463e9" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3d02e2b02170986944487cba8448f998" );
            pt_len = unhexify( src_str, "bc1d7553f4a28754cf59ed6f7a901901f04ce62a449db2b45ad60329d0341bb9ba421c783c28a9200b41da8ab6328d826293134a7d0c9a5775dd2735e7767efda4ad183566e0847d6d978abd1a8ab13b16b8323acef05ced3b571631e1e24ad44d65e6ffa64e03c9970e94bacb9f721aba06cda6a08806a3be63dddd8029301d" );
            iv_len = unhexify( iv_str, "6336077bb83eff1c9ea715de99b372cd" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0466bb2957281f64b59eafed3509" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "5f395958f2f7acafb1bca6d3a6ec48b717f2ceeac1b77e1b0edc09a09e4a299d2ec722cc7daf34c8f4121a93c80b2adb20a2fc95afd09320f91085c93c8b082dd703814c9777501d23bf9b328f07f04652592dc5a3f4321626a695b8db8e65c8617c809eb2978d8c9a882ffa82a4bb707c1a8f9a965bdacce5c041bafc94a1c6" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "5f395958f2f7acafb1bca6d3a6ec48b717f2ceeac1b77e1b0edc09a09e4a299d2ec722cc7daf34c8f4121a93c80b2adb20a2fc95afd09320f91085c93c8b082dd703814c9777501d23bf9b328f07f04652592dc5a3f4321626a695b8db8e65c8617c809eb2978d8c9a882ffa82a4bb707c1a8f9a965bdacce5c041bafc94a1c6" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "cd1ad1de0521d41645d13c97a18f4a20" );
            pt_len = unhexify( src_str, "588c2617517329f3e1e7ba6206a183dc9232e6a4fa8c8b89532d46235af1e542acaa7eae4d034f139b00449076ba2ef9a692cae422998878dabdac60993dce9880d280bec1419803ba937366e5285c4a7f31a5f232f8d3ef73efe7267b3ef82a02f97d320ebc9db6219fbdf1c7f611e8e5164e9ecf25b32f9c07dfa12aa705af" );
            iv_len = unhexify( iv_str, "413873a0b063ad039da5513896233286" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "d4dbe9cae116553b0cbe1984d176" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "bd519b7e6921e6026784cd7b836c89bc1fa98e4013b41d2bf091ef0d602e44a70df89816c068d37f0c6377af46c8bfa73ec0d5bc0b61966f23e55a15a83cea49f37cc02213b4996f9353ee2b73a798b626e524b9c15937ecf98a4eded83fb62e6deea1de31e0a7f1d210f6d964bc3e69b269da834720fd33487874489b8932a8" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "bd519b7e6921e6026784cd7b836c89bc1fa98e4013b41d2bf091ef0d602e44a70df89816c068d37f0c6377af46c8bfa73ec0d5bc0b61966f23e55a15a83cea49f37cc02213b4996f9353ee2b73a798b626e524b9c15937ecf98a4eded83fb62e6deea1de31e0a7f1d210f6d964bc3e69b269da834720fd33487874489b8932a8" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1cb120e9cd718b5119b4a58af0644eff" );
            pt_len = unhexify( src_str, "4c8e8fb8c87ff6b994ae71bfbf0fa4529f03bad86edf9d27cf899ea93a32972640697e00546136c1dbc7e63662200951b6479c58ae26b1bd8c3b4f507c0d945d615183196868ec4f4865d1d00bb919a00184e9663f6cb9a7a0ddfc73ee2901f7a56ef2074d554f48cef254be558fca35651be405f91c39e0367762b4715d05fa" );
            iv_len = unhexify( iv_str, "5a7087989bfe2f6eddcb56fde4d72529" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "95d8bd12af8a5ab677309df0fb" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "315b206778c28ed0bfdd6e66088a5c39" );
            pt_len = unhexify( src_str, "6186f57a85b65f54efbf9974a193012b1396fc0ca887227e1865f1c915ac2af9bbd55969f7de57ce9fb87604cf11c7bc822b542f745be8a101877a810ed72bf4544d0acb91f0f9d3c30b6a18c48b82557433d0db930e03bcecc6fb53530bfd99ee89f9e154aa1a3e2a2c2a7a9e08c9aed1deab7fae8ea5a31158b50bca2f5e79" );
            iv_len = unhexify( iv_str, "7ec6f47ec56dda5b52bbdaa6ad2eb6da" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "930750c53effc7b84aa10b2276" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810240104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "e886de1c907c97e7db8ec80a79df90f8" );
            pt_len = unhexify( src_str, "c64cc9596d7c738746ab800f688eec190a4c802c55b2528931d74d294496892b81f53d3073d48f9bef1d58ce3be26547474cdda2868abeab71aff566fff613b4e5bfed1be1d2fff35d8ffa33302d3da1c82e421aa3a23848f31e26d90c0cb2ac2ae136ada73404ed3e0e1d3e7cb355a11cd2a4f9393b4d5eac988104fe1cf959" );
            iv_len = unhexify( iv_str, "612cacbf33266353d0a29a24532f3c0c" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "76634e58d8f3a48f15875ac1d6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "7001d7395efb432e2804cc65c0ba5d4719ce84177ce46292c4fd62a5596bd2bab1d5c44217ac43235bd94489c43d01618a11f047d2e247062c3b88d6e59adaa1f46514fb33b7843483920bee60a41f3cb312322c305d25251b4704fb66da58637c95a9d539731434f60ef44fe3cd6d37e2c8e7089880a563938dcc98b43f08fd" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "7001d7395efb432e2804cc65c0ba5d4719ce84177ce46292c4fd62a5596bd2bab1d5c44217ac43235bd94489c43d01618a11f047d2e247062c3b88d6e59adaa1f46514fb33b7843483920bee60a41f3cb312322c305d25251b4704fb66da58637c95a9d539731434f60ef44fe3cd6d37e2c8e7089880a563938dcc98b43f08fd" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3b936e09a6477f3bd52030a29df5001d" );
            pt_len = unhexify( src_str, "65cf11d1afad19b34f282f98f140315992392f5d4eed4265085b29e1e5553f4783fec681ba2d368486ba6a54c00e71c82c08ca3d097904f021ce4b0acba2d2a7005e28e5f8750ea3d18a4f78363c37583e85104234498942c639a0564b0d80055c21cb7735dd44348298291ab602f345b1d74d624750c0177fbd5cca6f99223b" );
            iv_len = unhexify( iv_str, "f93105be83fa5e315d73acfdcf578de7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "91b55bb5e3f3f1abcf335db5" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "dc9e2095de7b1b48481b56bf6a3604cd" );
            pt_len = unhexify( src_str, "ed61ff94a3f84c72147faefa615e2df00324fb01790cf9764c72c1b8ba47f17866a1fd64ee5c2f53865d1bc24ec93165a6774466a59603199ee476c1f2da7d932c8943d126aa172d532d8475a484d42bb45fcf92766feafd7f3e2e3d42d22f6f84a90e7e688232f799d80cd2cc152ddd21ecfb137701ecafcb2b65abe2e4e6f4" );
            iv_len = unhexify( iv_str, "9e5268db19a1b51c0496a160ca76f8f7" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "0fa9588536fca71bb44260f7" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "ef562e301fcf923ff1a1acd3aff9b1c963058228655fe8a66cab01396547dbd2aa1f79a22eefc62944b86d1a31ebe2d17130175b8c003d6755b0eb8b79895b0f7f8046c5ae888a067ba17bc8e11a8f6e5023a9cd42f6461966c28e505b371c0f72a2606bff430a58016e99713d25ce11f10391fb4a922e27989422c6a64f9107" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "ef562e301fcf923ff1a1acd3aff9b1c963058228655fe8a66cab01396547dbd2aa1f79a22eefc62944b86d1a31ebe2d17130175b8c003d6755b0eb8b79895b0f7f8046c5ae888a067ba17bc8e11a8f6e5023a9cd42f6461966c28e505b371c0f72a2606bff430a58016e99713d25ce11f10391fb4a922e27989422c6a64f9107" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024096_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3f93901fd7cc88db3ba76a158d658c7b" );
            pt_len = unhexify( src_str, "16402fded879fcbfe9405902aa63ca2a520889e0045f687455469b7bb867829a01208b8dc5dcc852d8ee478993c30e6d9ec6408773b367821310a0ae171d38d71e06981ff6e845acffbc794142b87c748e12484c0636419d79be3d798cde59e9dae0a4a4a4346596427e6b235ad52e6a1b02d6f4df0c7de35fc390cae36aef14" );
            iv_len = unhexify( iv_str, "7e98de461e6d96c0ce6c8d8b3854cf49" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "86c9a70e4bab304ae46e6542" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "1b4c09569b42c469b3ab6b39312c214502ec09f5fe2fed1d1933d13cdc6a7b77a5d135123fa69d9207d6844b0357b26b7a2f53b33a5cd218dacda87b78b09cf259e48e74076812c432e2d0833fb269721f9347c96e158500f9b2283342a35c8de0a022edce711118d72d8fbaa354bfb0ffee465844ef2d37e24ec2cea8556648" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "1b4c09569b42c469b3ab6b39312c214502ec09f5fe2fed1d1933d13cdc6a7b77a5d135123fa69d9207d6844b0357b26b7a2f53b33a5cd218dacda87b78b09cf259e48e74076812c432e2d0833fb269721f9347c96e158500f9b2283342a35c8de0a022edce711118d72d8fbaa354bfb0ffee465844ef2d37e24ec2cea8556648" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "42289f3d3cd5838e250ef54b128e60d1" );
            pt_len = unhexify( src_str, "3edae1d554b67d2036f5fdbdb2945cc112f100adc1b47009c2e23f6a2eaee78d1f39ce8a98f715853cc29fc793fb6981ec3036834188dea7d668185ccc8642071b15de1332f6a59c8a9b4399733eb4b3d8f224af57ba6b4a8e64494bb6630b9d28e7ec3349064350febcef6a3ad1d6cca1b1da74f3d2921c2b28a2dd399c3416" );
            iv_len = unhexify( iv_str, "e557389a216ad724aafdab0180e1892e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "6f78bc809f31393e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "25c476659cc7b343a69088baf868a811ba37daca85c4093105bf98235a90aeca015ab034da008af0982f9b2e80df804c186a9b2e97f74cffd70ebb7771d874fcaf12f6d01c44a8b0ec2898cf4493cf09a16a88a65cd77909bbf0430c9603869bd5f20d56cb51d8a3f0a032fc30d925c96599d296b1ec41c2912bda426adea4fb" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "25c476659cc7b343a69088baf868a811ba37daca85c4093105bf98235a90aeca015ab034da008af0982f9b2e80df804c186a9b2e97f74cffd70ebb7771d874fcaf12f6d01c44a8b0ec2898cf4493cf09a16a88a65cd77909bbf0430c9603869bd5f20d56cb51d8a3f0a032fc30d925c96599d296b1ec41c2912bda426adea4fb" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3d772eabb7f19475665ca2a7e693bcfc" );
            pt_len = unhexify( src_str, "e9fc4d86f5b857fa6057b73f967351e06f87288c40a95b9e378c84f1a4c0f4b80ed0a0b44ff90a8973be4199c0c4006fc4f5ea19d5f1fe8b9c8c01f4675ab85afab0592bb3daba36bb4fc7ed9eea867e9d8cc50c19fb62a5a57956e9efacebac5e9f849649d35a329bd68de97bb6e5ff7bef477a86765c2c9ec15e24cbba5c6e" );
            iv_len = unhexify( iv_str, "0747cbb486a013453fde1ca6abb11dbe" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "8e761ffaea68f967" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024064_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "fb7fd753ee6eaaf283a42a121dab4e43" );
            pt_len = unhexify( src_str, "fd5cecb2c0287cb8229e97d9cc4b9885f428710528884ce663ed1728cd44cb2df93e56ef17ace0678d1e341366c652f4ba7ee45797d39be4a05c1151e5cde499e13e5d45549b5d95a174d03616d06ef96e9d7b2b6bb0d79a726b253dd64223a5f09611671b234ccf9b383952f8888814b2c167e774cfbf54e9c6b99a753f4fa9" );
            iv_len = unhexify( iv_str, "8164929fb54485377ecccc9b9621af5e" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "40a2fa7f4370afb2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "6208d068be60f7b04b80fc611062e6caaef9a5cf59f850d174b7446c78c039ea9aefe4885e19c2b33911d32ce1fe3c48ddffa4b03e450fd35da03f40c4e7c5bb3b1c3f3049dbfad3ac81ca1b79cafbaa172f4900e3829d38edea3b64000f93924a801259bc4b2523445c64bc23bfee190b952468507fa4baf6dc2bec66fcf0d8" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "6208d068be60f7b04b80fc611062e6caaef9a5cf59f850d174b7446c78c039ea9aefe4885e19c2b33911d32ce1fe3c48ddffa4b03e450fd35da03f40c4e7c5bb3b1c3f3049dbfad3ac81ca1b79cafbaa172f4900e3829d38edea3b64000f93924a801259bc4b2523445c64bc23bfee190b952468507fa4baf6dc2bec66fcf0d8" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "30d757fd73a0fd5fa49159ad0653296d" );
            pt_len = unhexify( src_str, "17d485b258f80d8924e35291118cfdcffd86c47851b65f0b06a7c1f5202de82f3f460fc61b1aa38fdba7c8ded375c92cf005afe63e59d362c0960044af39241b81ca24e85c5faa43903229355b7313fee21b992ef3931d9d2407b32b3cf72dd7acbc7948395eb513cb2fd428b215ba2bd1e29c62f45d0ce231884f62480c6d8f" );
            iv_len = unhexify( iv_str, "b35b8df0aebd0608517f2830e0e70cd0" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "954c0e99" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "022618d2598f79104e918a09c937a82b3db59243b5e13de731fcb912e4366105797ce47f6dce7f08073f2f41e5c15fd6b1ec4b5861469a4880c3b0bd769b78c696ff29c28c9349d5a46a6e5ad9211bd4b708a8c0b6928ebbb0dac1c0a5f5ce6b05de6a50073128566a23f09cc1b826aa5803f9f750aa4debf59f24ae9f98c9b5" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "022618d2598f79104e918a09c937a82b3db59243b5e13de731fcb912e4366105797ce47f6dce7f08073f2f41e5c15fd6b1ec4b5861469a4880c3b0bd769b78c696ff29c28c9349d5a46a6e5ad9211bd4b708a8c0b6928ebbb0dac1c0a5f5ce6b05de6a50073128566a23f09cc1b826aa5803f9f750aa4debf59f24ae9f98c9b5" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d9d3cfd5900de5d5e2109e7721cfeef6" );
            pt_len = unhexify( src_str, "e4243cc37cc32dfcedf9bb76890e706af6ab1e06b290b8ccfe2a55e5dabe68cb390f7636dc9676b431d4dc8ad3f6d989e510194294ab7ab0556789046743cf374d8b6462f5f95a17f3f44337d6c69ee47b0e1ad7e5ce6f9b224c54099a104e70d2d06af869b921ea47febe08f90c591ed49c1f12003afceabd2c7bba458a0111" );
            iv_len = unhexify( iv_str, "b4b9dfb013de6f7c44779e5a9daaf5e5" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "2b81e8ce" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024032_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "68dc138f19354d73eaa1cf0e79231d74" );
            pt_len = unhexify( src_str, "ce345567a76bc30d8b4fd2239788221cfa75e1a310aeeeb8c355f8eea57d80967f3047fbd4e6173fac5caeb22151fa607065953c4c35e0537b9e3788cc80de9eedf2a340698bde99a6a1bdc81265319da3e52f7a53883b7f21749237fcfd3cd4f149bb2be7a4ddd9ef0544cfe0789040d1dc951b6447304942f03ab0beae8866" );
            iv_len = unhexify( iv_str, "e7147749560f491420a2d893c075bb76" );
            add_len = unhexify( add_str, "" );
            unhexify( tag_str, "70a83f6f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "64b021612c78b3e192e8349d48b77d02927e7fd70c7160d37cb8ef472f6bcd9df9d93431627c1c80875e208724ae05f94fdd2e005e9707b78a1bf3bbca7beec4b03ddd4d9de6235ffd6d84a8b9a1842e104c1e22df4566f6c4d3d4e3d96a56b9b8a5cdce9da70aa236109b289266036f285564060b204dfd7ac915eea0dd0b1e" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "64b021612c78b3e192e8349d48b77d02927e7fd70c7160d37cb8ef472f6bcd9df9d93431627c1c80875e208724ae05f94fdd2e005e9707b78a1bf3bbca7beec4b03ddd4d9de6235ffd6d84a8b9a1842e104c1e22df4566f6c4d3d4e3d96a56b9b8a5cdce9da70aa236109b289266036f285564060b204dfd7ac915eea0dd0b1e" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "7362c86344e0aefb0cf0d04768f9c05d" );
            pt_len = unhexify( src_str, "8baffc7836004deb87c0111d47c182512bf861874021ddfcd559acf2c4a51cf5bc4bfdee2d039b9c005b6af95a2607643dcf4d9cd9d62412f709334556db22fc91d7b40438505d6806ccb2f2c21ae731bc1f1c825d28a71ab27095a39985e96ccd07cfb2e75243ccafd474494a2338c324ef533ca5f17d2ac1b1883140342ced" );
            iv_len = unhexify( iv_str, "7e8d12c2f0dcf4f792247134234ac94b" );
            add_len = unhexify( add_str, "86d2b5debc3b10495da353d6821f6cad380776d805bd8660b08dcdb1acd87026e4f344b547a4db47b5f44cded314bec4ce9a417ce40a2acd5a21460c42dfcd27483abf3f38dd8cc5fa523b6768a26513df5896435baa97781cff1966e2e3d6ec6d0a9cdc013de5a50e4d46831667055bad04f784024a82f9cd087ae4cd37dd64" );
            unhexify( tag_str, "9594da428fd8c1b13ecb23afa2c1af2e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "e2c424f42aedd56f0e17a39d43ad19c8e2731efc7a25f077aef51d55280b10e667e338bd981b82a975ef62bf53bc52496b6995d33c90c7ae14767c126826e3f32bd23f444ddcfd7a0dd323b0ae2c22defad04ce63892b45c176bd0b86f5fa057a3dc371359744cb80bbfb4a195755136a0ea90b4044a45bc1b069f3cb3695c04" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "e2c424f42aedd56f0e17a39d43ad19c8e2731efc7a25f077aef51d55280b10e667e338bd981b82a975ef62bf53bc52496b6995d33c90c7ae14767c126826e3f32bd23f444ddcfd7a0dd323b0ae2c22defad04ce63892b45c176bd0b86f5fa057a3dc371359744cb80bbfb4a195755136a0ea90b4044a45bc1b069f3cb3695c04" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "58748bb204ccb7bdafdbf739b6c19a3e" );
            pt_len = unhexify( src_str, "b72902c9ebb72a86be539b19a52fd9af00aa4de081d90c0d8ad580ebb5900177a036f40a1e9b43e3a07d715466526d6d7544e5a5551805b62463f956cd519fc99182c2d54bd62fc7ffc6e5ebf1503859b706da11a1b6c707a67a70789dbfc10ef726bd360f9f2347326e068e757c8443ddc9308a171e682359ae1bfe87194ab5" );
            iv_len = unhexify( iv_str, "93ac298c73c88e127a4d9dd81bf24e3d" );
            add_len = unhexify( add_str, "8f168fc4d1da13bdbefae3f9d6ac1d8cb19fcec1f43f727951af0a466d8826649a46c3cb50c045ea83849fce0eedbc042a1a435e6d9d59017997a2d5459b940078b8a7f3b6b0ff279ff8c560248296a17240ff1b0643d1f436b6e3f2079363fc49fb45f410debbdde083b92057916368cb807d603cb82e2c0dc01658bff7f1ab" );
            unhexify( tag_str, "efba4589d4a03555766bbc3b421dd60f" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "d5c97a659f016904ff76286f810e8e92da6f8db2c63d8a42e617760780637e32105503440cdf04d1fe67813312f1479fda8d746c8b0b080591eba83850382f600e9d8680516c6579669f0b3d0a30323510f9de1c92512790b8347751994d022156cae64da0808a649d163a0e99e869fdf224b7c1a6a8fbc613d5917eca8ee08c" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "d5c97a659f016904ff76286f810e8e92da6f8db2c63d8a42e617760780637e32105503440cdf04d1fe67813312f1479fda8d746c8b0b080591eba83850382f600e9d8680516c6579669f0b3d0a30323510f9de1c92512790b8347751994d022156cae64da0808a649d163a0e99e869fdf224b7c1a6a8fbc613d5917eca8ee08c" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024128_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 128 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6cc13cbd62428bb8658dd3954fe9181f" );
            pt_len = unhexify( src_str, "2c9ec982d1cfb644ddbc53c0759b10493206d5186affc6882fbb2ba3aa430f9bae1209db2d78dcc125f3c909a54dd84fdff96c71e678216a58390ef4308bdd90f94f7109c4edefa76a74fda64b201b7a435bbabc27298f3eaa4c2d1393bd584f811fff52638f6ad2f6d86a8c3c9c030d9d4264c8c079592a36178d25991cff09" );
            iv_len = unhexify( iv_str, "86740da7ce4efbed70af55e1d6c10fdf" );
            add_len = unhexify( add_str, "be561ac15e3cfda624b422af97c26719c140bb50e4a993d636efe9c7f1963fb9047a0762169b571a698ff310bc417e34d4039b7562a95af710ccc1b197964a376c986fd2ed8ac4b0c7b4e843c37a41366f2f483c821a1823f317416c7e4f32eed9b9dc2ae1a2f3ed32c4b3187358a2329aa42191b7c2fe87b6e27ff20303cb29" );
            unhexify( tag_str, "76b990a1e010e5f088f6ae90bec40b32" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0b9a5f5d2e6852b75b9cf26c1b310b2200e56dafcf3c941478862cdf9737ac8e2cb9b38d41bd4a1872ea1b4cfd51a1a0b9b743aca439eefa10de8459a0a7a221c5429b3dee393f17031ca6c399df8e05657c3db55be9c9dd29e690042a4ed8db732efce7c58d6b20a2a0f7c79e42e5ada43b87ab00f481c20cac1b35514dcdc9" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0b9a5f5d2e6852b75b9cf26c1b310b2200e56dafcf3c941478862cdf9737ac8e2cb9b38d41bd4a1872ea1b4cfd51a1a0b9b743aca439eefa10de8459a0a7a221c5429b3dee393f17031ca6c399df8e05657c3db55be9c9dd29e690042a4ed8db732efce7c58d6b20a2a0f7c79e42e5ada43b87ab00f481c20cac1b35514dcdc9" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "286d3f5080cfe88538571188fbeb2dd5" );
            pt_len = unhexify( src_str, "55135928997711360622eda1820c815aa22115204b1e9bb567e231ac6ea2594b4d652627b6816bdc6c40a4411fd6b12fab9a1f169d81c476dbf77151bff13f98ca0d1dc0a68ea681652be089fadbc66c604284eebfc8ce4cf10f4ca6bda0e0f6634023db6e3f0f1de626c3249a28a642ecc9ec5ff401e941fa8a3c691566c0ae" );
            iv_len = unhexify( iv_str, "da6140bd4dc6456ddab19069e86efb35" );
            add_len = unhexify( add_str, "5d350a04562a605e9082ebd8faec6c27e561425849e7f0f05f5049859c2c1bd2c4682ebf9773fab6177d2601fd5a086cefc3adef5a2f8f6b5dc9e649e98dd0a3d1a2524419f01305bd0fcfff52d84a20d1b14dea2138dcc54eea2bf263c6fe27c3e7255f1f359d0d00fb1b350d7a04965af30027632520197e85eb41de6bb286" );
            unhexify( tag_str, "d90d34094d740214dd3de685010ce3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "726ae113a096769b657f973ea6d2d5dd" );
            pt_len = unhexify( src_str, "90636012ba8c51d16f8f6df3d3bcabc3f09aeffbe2a762f62e677913188045b861b2e7d9a7bd93dcee46e9e4832e497a6f79db52b4e45c8dab20fa568ff9c4ace55be3216f514a3284768a25d86b1c7da5377622f3e90ed4c7bd4571715af4d0a2ab5181d0475f699202e4406bb9cfdbd4fa7f22d0dd744d36b3223134658496" );
            iv_len = unhexify( iv_str, "2f9900226c97585d200dd20a279c154a" );
            add_len = unhexify( add_str, "761663c3fcbf1db12bc25546b2425b8229b3153e75f79fa63958819caee3febff74603d99264b5a82ef5980439bef89301ae3206a1d01a3bbd7a6c99d27d1e934cc725daeb483f826c2c9d788fd1f67a627864cf8b5f94df777bb59ef90cb6781a2000e6f0baa4f1ea4754b47bb7cbd2699f83634e4d8ab16b325b2c49f13499" );
            unhexify( tag_str, "d095bfb8990d4fd64752ee24f3de1e" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "9f7759c6d24fd9aa0df02a7c0cc5f17e61622c63195f85dfafa5d820d3ad218c7288ec017821100f1fade10f9bb447a4a01e3698b045548c7619a08f2304e2818a9bf55e70b40f8b994b7dcf0cb243848cf3f6fdfec3ebbb147d01df84a3ec62cd8fa5d78ad9f2f28cd288a35eb49a5172339e9872e8e7e3350b0d69f59acd07" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "9f7759c6d24fd9aa0df02a7c0cc5f17e61622c63195f85dfafa5d820d3ad218c7288ec017821100f1fade10f9bb447a4a01e3698b045548c7619a08f2304e2818a9bf55e70b40f8b994b7dcf0cb243848cf3f6fdfec3ebbb147d01df84a3ec62cd8fa5d78ad9f2f28cd288a35eb49a5172339e9872e8e7e3350b0d69f59acd07" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024120_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 120 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "73a9eeda721c6f292e6b399e2647f8a6" );
            pt_len = unhexify( src_str, "215fc7e52abe4c751ca2f7f9a5cbde9ab8b44b8d4054bb62dcea6df5b936145ca6ec83a2b78b070638fd6e5ea3bad5d0caf1b8f755f391c3e0962a92337e3eba575585eb83680075fc818860388c587746af78d5fc75ccd0a63f1612abb1ba0f04a2228ca27fbddba4878f9b2683683f516b6d6fe4f6622e603bd3c5ad45e332" );
            iv_len = unhexify( iv_str, "c1e80eb723960049cc4448b66433f1cf" );
            add_len = unhexify( add_str, "fb2a0b1f817404e74aee0a6ec8f2cd86f0c9114ed367b2690c44ad80f9d3377d7fd5066beaf1daa739d27ed3fba98379188016b1fe901204a174f9ffca370c181aece5e5d40939a0d460913b40b895e78a3b80ddf3d613c05e4e27bfd161ea2ef42271a2679f2cdca5b728ffb2319781c946a4f3ecacf486b754b30bb04ea60b" );
            unhexify( tag_str, "e08161262234d0d5be22f09e5646bf" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "b5e286183f16dd9403bec6786bd4836cc6add47947ef111fb1d5503c18c333c8fe60959502f58390d0e0f69fbe5fee13c72aed65fe6e32f6ea45877fe44f8a556aa5157b112e572197c1c350b7943c6cf2e9146018599524d27599f09c86027f2c5927e4a20c63833870e8369baa36ecc07cdb3ced520b5ae46869ff357ca089" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "b5e286183f16dd9403bec6786bd4836cc6add47947ef111fb1d5503c18c333c8fe60959502f58390d0e0f69fbe5fee13c72aed65fe6e32f6ea45877fe44f8a556aa5157b112e572197c1c350b7943c6cf2e9146018599524d27599f09c86027f2c5927e4a20c63833870e8369baa36ecc07cdb3ced520b5ae46869ff357ca089" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "90dbda7397d8fc46215a1218a6ffd0d8" );
            pt_len = unhexify( src_str, "4f82a1eca6c9184240f50f7e0cfec07ec772cad5276d93043c462d8364addd9a652eed385ccc6b0faa6ca679ab3a4c3d0be6a759425fd38316ee6a1b1b0c52c1bb3b57a9bd7c8a3be95c82f37800c2e3b42dde031851937398811f8f8dc2a15bfd2d6be99a572d56f536e62bc5b041d3944da666081cd755ec347f464214bf33" );
            iv_len = unhexify( iv_str, "7be477d14df5dc15877ae537b62e1a56" );
            add_len = unhexify( add_str, "7358ddf1310a58871a2f76705f1cf64223c015c4d1574104d2e38783bb866205042f05c86e76c47a2516ce284911f1d2cbee079982dd77167e328b8324eec47c9244cc5668cf908c679bb586d4dd32c6c99ed99a6b571cf18b00689463e7a88cea6ea32d288301a10a9139ed6092ffe298e25b8cfb6b4be8217f16076dcd0a90" );
            unhexify( tag_str, "776d871944159c51b2f5ec1980a6" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "0c85174d428fc1c7c89ca5d1b8aaba25" );
            pt_len = unhexify( src_str, "3735cbfb8000260021d1938d2a18e7737f378ecddb11a46ce387bf04e20bbfcc902457637fd152ab87017185601f32a7f906057123b6c2da31a1069c93e3cacc59a359aebd3e31b302e1a1f7d5d8f1b2917a8fe79181fa633b925ce03a1198dac48f4c959076b55bc6b3d50188af2c6aa33d83698aa8db22649f39825ba54775" );
            iv_len = unhexify( iv_str, "b3c9dfa4c55388a128fbf62aa5927361" );
            add_len = unhexify( add_str, "3f552d45b61cf05ae2aa92668e89f3338a15ec7c5b7113b6571cfcd9e4c4a962043ccd9323f828dd645e8a91b007ce2112b7f978ad22ee9821698a4f2559d987ae4421452ad2e8d180953297156426d4540aff2104d8637b56b034a3a1823cf962bffbc465fe6148097975a8821ca7487e6e6c7ff4ee4de899fe67345676bb1c" );
            unhexify( tag_str, "1e7dec83830183d56f443a16471d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "3d98cabca4afb7c1f6b8eeed521f4666ae252ac12d17ebf4a710b9a22d839b69458387ba4bbec2f6400e0cff80fbe4682c24efcd3b8c594d9b515ca7842c9d5988c42b59b6526c29a99256451e2927f5b956ef262f97c733dfa8bff73644473b9a8562bdfca748f4733ddce94a60024dfbfcde62fb3cbd7c3d955012d5338b91" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "3d98cabca4afb7c1f6b8eeed521f4666ae252ac12d17ebf4a710b9a22d839b69458387ba4bbec2f6400e0cff80fbe4682c24efcd3b8c594d9b515ca7842c9d5988c42b59b6526c29a99256451e2927f5b956ef262f97c733dfa8bff73644473b9a8562bdfca748f4733ddce94a60024dfbfcde62fb3cbd7c3d955012d5338b91" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024112_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 112 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "d89f06eb07744d43d44734faf9751d07" );
            pt_len = unhexify( src_str, "36cc3b2f563305208a03378f7dc036119f7de3fee77cefac06515853d36609a622382ed026c59783fbc0d9910767874c516e10c7bf3e3d104f73b3463c8d93a63418c76cb0d05e62e9c8642cb4f32caced2620912cb6c79e5110a27d5fba1ef3b4d0578077858526c5e4254365f2b2ab47a45df4af08980b3b7a9b66dff5b38c" );
            iv_len = unhexify( iv_str, "185f8d033713ee629e93561cf8d5acb8" );
            add_len = unhexify( add_str, "743bcb671d0aa1c547b5448d64d7c6b290777625ba28f25ca0fbf1fc66495a2fde0648a8db51039b0e7340d993aef8afb48269e660cb599837d1e46f72727762d887ee84c073d6136d1b0bc7d4c78f5673a4a6b73375937e8d54a47304845f38ca6b4f51cf14136a0826016535dc5ed003e38c3ac362b9d58ba8b555a05a1412" );
            unhexify( tag_str, "fcad48076eb03ebe85c6d64f6357" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "6150f14dc53f391e815acfabed9f9e20" );
            pt_len = unhexify( src_str, "fd8f337017e1b60d6618e6e4ad37c1f230cdeb78891579c2c63d4e6a4f7d2cb7252e99de333c73db45958808c08e91359c885a7385ab6f9ed98a27927a5b83c3a456ce2e01869712675e527155ba1e339ac14a3ccd7a4b87360902f2b8381308fe5a4eac5c90d0b84da4bf5b907de6ff3139cffd23b49a78750006100183032a" );
            iv_len = unhexify( iv_str, "7e92dd558bd2662c3a539dfe21a352cf" );
            add_len = unhexify( add_str, "9b4624e9118e6aa5dc65b69856638f77fd3f9f562046f50ba92a64e988258637932af7979f000505b84a71ff5dd7b60bad62586b1a8837a61c15a1a1ba7f06668272c28169915d7f06297b6c2a96c8c44203a422bfd25500c82e11274ffe07706365bfd3da34af4c4dd8ad7b620de7284a5af729bea9c4ed2631bdcba2ebdb7d" );
            unhexify( tag_str, "922a7b48ad5bf61e6d70751cfe" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "f272a3ee9b981f97785cc6fad350e516d72d402dae0d8a531c064ec64598b2a5760f9b279c10aa1ff71bec07300ab0373187138e7a103fc4130105afa6b6346f3d368b40d6f542375de97878ad4d976d64c5c4968a17be2b1757a17c03100231c34721250cd37cc596678764083ade89ae3b1a2151ff9151edcd7ba0eb8a4649" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "f272a3ee9b981f97785cc6fad350e516d72d402dae0d8a531c064ec64598b2a5760f9b279c10aa1ff71bec07300ab0373187138e7a103fc4130105afa6b6346f3d368b40d6f542375de97878ad4d976d64c5c4968a17be2b1757a17c03100231c34721250cd37cc596678764083ade89ae3b1a2151ff9151edcd7ba0eb8a4649" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3e8216072ed6fcde0fe0f636b27ed718" );
            pt_len = unhexify( src_str, "3b50f2a8dca9f70178503d861d9e37f5edfafc80ee023bfed390a477372986e4794175ec22ac038c3461aba50c9b2379cab48512946efdfe2cb9c12a858b373a5309324f410e6a05e88ba892759dbee6e486dc9665f66cb5950ea7e71317fa94abbebd67a3948746a998173fbbb4f14f9effbdf66d3b6e346053496a4b1934ce" );
            iv_len = unhexify( iv_str, "23a122cf363c3117b8c663388c760ee4" );
            add_len = unhexify( add_str, "28ce0b4a44fa83323e060f3ff6436b8829d4f842090296bdc952b6d4a6b1b1a66be06168c63c4643e6ac186f7ffd8d144f603b2d4bc0d65be48121676f9fa1f359029c512bebfd75075ff357bc55f20fc76d9f2477c9930f16408f9f09c5ae86efa2529d2f1449ceeb635b83ca13662860ef9ac04a3d8ab4605eccd2d9ae5a71" );
            unhexify( tag_str, "531a65cc5dfeca671cc64078d1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_12812810241024104_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 104 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "1af434b73a1210b08595ffa686079832" );
            pt_len = unhexify( src_str, "13f6c1c2d4edcf1438a7b4e85bcd1c84a989831a64d205e7854fce8817ddfceab67d10506ccf6ed9ce50080ef809e28e46cba7b0c96be6a811f59cd09cb3b7b3fe5073ee6763f40aee61e3e65356093f97deef5a8721d995e71db27a51f60a50e34ac3348852c445188cfc64337455f317f87535d465c6f96006f4079396eba3" );
            iv_len = unhexify( iv_str, "ae318f3cb881d1680f6afbf6713a9a2f" );
            add_len = unhexify( add_str, "3763c9241be0d9d9a9e46e64b12e107d16cca267ff87844c2325af910cc9a485c7015d95bbe62398864d079fb2b577ba0cfad923c24fa30691ad7d767d651eed4a33d0be8f06fed43f58b2e0bb04959f10b9e8e73bd80d3a6a8c8ce637bfbdb9d02c2b0a3dd8317c4997822031a35d34b3b61819b425c10c64e839b29874ddfb" );
            unhexify( tag_str, "2ae7350dd3d1909a73f8d64255" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "3cd2a770300ce4c85740666640936a0fe48888788702fc37e7a8296adb40b862ec799f257a16821adaa7315bd31e8dec60e4a8faeb8ba2ee606340f0219a6440e9c1d3168425e58fac02e8a88865f30649913d988353ab81f42a5ad43f960055f0877acda20f493208c2c40754fbf4ccee040975aa358ea3fe62cbd028c1611a" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "3cd2a770300ce4c85740666640936a0fe48888788702fc37e7a8296adb40b862ec799f257a16821adaa7315bd31e8dec60e4a8faeb8ba2ee606340f0219a6440e9c1d3168425e58fac02e8a88865f30649913d988353ab81f42a5ad43f960055f0877acda20f493208c2c40754fbf4ccee040975aa358ea3fe62cbd028c1611a" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "04036d2f5273c6ff5b8364aa595359c9" );
            pt_len = unhexify( src_str, "acf79b6099490af938fb5fd8913255b3daa22786b03356cdf3e0ffaf570f9f866047b8e15c9953f893d97e7098265297396868ebc383be8547e8ec9d974b6a65b5dc5147cdadef2e2ad96696e84e44f364c2ba18c8aabe21f99489957b2b5484bf3fb4fecaf5ddaa1d373e910059c978918a3d01b955de2adb475914bf2c2067" );
            iv_len = unhexify( iv_str, "edc433c381140dff929d9df9f62f4cb6" );
            add_len = unhexify( add_str, "404acfeeea342aeea8c8b7449af9e20ddf5b85dc7770d2144a4dd05959613d04d0cfece5a21cbb1a9175ddc9443ffacd2085332eb4c337a12a7bb294c95960e7c0bde4b8ab30a91e50267bbd0b8d2a4ed381409ea2e4c84f9a2070a793ce3c90ea8a4b140651b452674f85d5b76d0055df115608bf3a3c60996108023ebabe65" );
            unhexify( tag_str, "71f818f1a2b789fabbda8ec1" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "4729cb642304de928b9dca32bb3d7b7836dd3973bbccf3f013c8ff4b59eca56f5d34d1b8f030a7b581b2f8fdc1e22b76a4cbc10095559876736d318d6c96c5c64cbd9fbd1d8eb4df38a2d56640d67d490d03acc1cd32d3f377eb1907bbd600f21d740b578080ba9c6ddc7dc6c50cdcee41fec51499cb944713c0961fc64f5a70" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "4729cb642304de928b9dca32bb3d7b7836dd3973bbccf3f013c8ff4b59eca56f5d34d1b8f030a7b581b2f8fdc1e22b76a4cbc10095559876736d318d6c96c5c64cbd9fbd1d8eb4df38a2d56640d67d490d03acc1cd32d3f377eb1907bbd600f21d740b578080ba9c6ddc7dc6c50cdcee41fec51499cb944713c0961fc64f5a70" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "59fe44c6e28d025b2ad05e6e867051ab" );
            pt_len = unhexify( src_str, "20e66bae1215de9a87a0b878d39015d17e0d4542a1aaba2000cefbd5f892c26a410f55f0d7dc2f6b66690f2997032985e5516e068bfc6ec8a3669f566e280b0cefded519023b735ee3bcbfc5b6ce8203b727933a750f9bd515ec448c1f3a030aa0f40e607727a3239ebbe655d46b38a3d867e481ccf0fadbf0d59b665d2ed6b5" );
            iv_len = unhexify( iv_str, "eb0c30320029433f66d29b3fd5c6563b" );
            add_len = unhexify( add_str, "49b7418b87374b462d25309b1c06e3132a3c8f4a4fcf29fed58e0902509426be712639db21c076df7b83dcfcc2c2c8fcc88576f4622a4366eb42f84ebf760e3eb22b14f8b5ff83f06a6f04a924eaab05b912e126e80da22461abf7f1925fd72ebdf2aea335a044726e7c2ebbb2b8aeebab4f7de5e186b50f275b700794d895d8" );
            unhexify( tag_str, "296c4cdaeb94beb2847dc53d" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102496_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 96 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "c314264cee0e6db30ebe9b2f6d4991b2" );
            pt_len = unhexify( src_str, "d436ff9abfb044a332c4e009b591719a67b12a5366da0a66edf19605c34daa37588e15dd3da0d1a097215e469439de79cca74e04cd4904e5b4a6cb4e0ea54e6ba4e624ed6bd48be32d1ef68ffea1639a14e91a5914c2346ea526df95cbd4ad1b8ee842da210b35b6315c3075ecc267d51643c4b39202d0ad793cbb0045ebdc19" );
            iv_len = unhexify( iv_str, "4cd4431bb6dea8eb18ae74e4c35a6698" );
            add_len = unhexify( add_str, "0eeafbfd04f9a0ea18e5bdc688c7df27183f346187e9574b61222006f2b3e12e8d9d9bf1f0f15949ee1a7ee8e5c80ee903b8ba2860e15ccb999929f280200b159c2adca481748d0632a7b40601c45055f8cb5126148e6cbab2c76f543537ab54eb276188343cea3c4ab0d7b65b8754e55cfe3f6a5c41b6ea3c08b81fcecc968a" );
            unhexify( tag_str, "fda18d2f795d900f057fe872" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "cb9e0fb0ac13ca730b79e34745584b362d0716c344e4de90d8352b21117471ba12c97f193150b33774baee5e4a0f11b10428eaf0106c958e16aa46c5f6f3d99eed93d1b9ba3957bed05a8b9cc8c5511cf813a66dc7d773cb735b0523d8d6b0b80639b031ddc375f714c6dd50055320cd7ed44a471c8d5645c938a9005d0b5050" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "cb9e0fb0ac13ca730b79e34745584b362d0716c344e4de90d8352b21117471ba12c97f193150b33774baee5e4a0f11b10428eaf0106c958e16aa46c5f6f3d99eed93d1b9ba3957bed05a8b9cc8c5511cf813a66dc7d773cb735b0523d8d6b0b80639b031ddc375f714c6dd50055320cd7ed44a471c8d5645c938a9005d0b5050" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "26072018bd0bda524b5beb66a622c63e" );
            pt_len = unhexify( src_str, "91c524b359dae3bc49117eebfa610672af1e7754054607317d4c417e7b1a68453f72d355468f825aeb7fde044b20049aed196ec6646cce1eeeccf06cb394286272b573220cdb846613ebc4683442dccc7a19ec86ef1ec971c115726584ae1f4008f94e47d1290d8b6b7a932cfe07165fd2b94e8f96d15f73bf72939c73f4bd11" );
            iv_len = unhexify( iv_str, "c783d6d3b8392160e3b68038b43cf1f4" );
            add_len = unhexify( add_str, "8ae7c809a9dc40a6732a7384e3c64abb359c1b09dcb752e5a6b584873e3890230c6fc572b9ad24d849766f849c73f060fc48f664c1af9e6707e223691b77e170966ed164e0cc25ede3fbc3541c480f75b71e7be88fe730d8b361ea2733c6f37e6a59621de6004e020894b51dfb525973d641efe8d5fd9077a0bbc9dc7933a5de" );
            unhexify( tag_str, "edffe55c60235556" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "201751d3da98bd39ff4e5990a56cfea7" );
            pt_len = unhexify( src_str, "2965af0bde3565a00e61cebbfe0b51b5b5ee98dbbfff7b1b5bf61da5ba537e6f4cf5fa07d2b20e518232c4961e6bc3ae247b797429da5d7eee2fc675b07066ac2e670261c6e9a91d920c7076101d86d5ef422b58e74bdc1e0b1d58298d3ee0f510ee3a3f63a3bbc24a55be556e465c20525dd100e33815c2a128ac89574884c1" );
            iv_len = unhexify( iv_str, "6172468634bf4e5dda96f67d433062d7" );
            add_len = unhexify( add_str, "ae2d770f40706e1eaa36e087b0093ec11ed58afbde4695794745e7523be0a1e4e54daade393f68ba770956d1cfb267b083431851d713249ffe4b61227f1784769ce8c9127f54271526d54181513aca69dc013b2dfb4a5277f4798b1ff674bca79b3dec4a7a27fcf2905ae0ce03f727c315662cd906e57aa557d1023cce2acd84" );
            unhexify( tag_str, "66c247e5ad4e1d6a" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "efd064d4b4ef4c37b48ddf2fa6f5facc5e9cc4c3255b23a1e3765fabb5a339fa0eda754a5381b72989fc1323ff9a6bbaecd904eb4835e5a511b922927574673061ed8de23299ea1456054e7ebb62869878c34fb95e48c8385b5ebceecb962654cf1586b3f54e7887ce31850363e9a22be9e6fbc22e694db81aa055490495dbf2" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "efd064d4b4ef4c37b48ddf2fa6f5facc5e9cc4c3255b23a1e3765fabb5a339fa0eda754a5381b72989fc1323ff9a6bbaecd904eb4835e5a511b922927574673061ed8de23299ea1456054e7ebb62869878c34fb95e48c8385b5ebceecb962654cf1586b3f54e7887ce31850363e9a22be9e6fbc22e694db81aa055490495dbf2" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102464_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 64 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "3bc0dcb5261a641a08e6cb00d23e4deb" );
            pt_len = unhexify( src_str, "d533ad89a1a578db330c01b4e04d08238b020e36aebe87cf2b0bf0b01f1ce4197be8b0596e475a95946918152e8b334ba89f60486c31f0bd8773ca4ff1319fe92197088b131e728d64405441c4fb5466641f0b8682e6cb371f8a8936140b16677f6def8b3dd9cbf47a73f553f1dca4320ad76f387e92f910f9434543f0df0626" );
            iv_len = unhexify( iv_str, "16fa19f69fceed9e97173207158755a5" );
            add_len = unhexify( add_str, "92ddd3b98f08fc8538f6106f6434a1efa0a7441cc7f6fd0841103c2e4dd181ea0c9a4811b3cb1bad1986a44d8addabc02dd6980daf7d60405b38dadc836bb1d0620ceab84e0134aca7c30f9f9490436b27acfd7052f9d7f0379b8e7116571017add46b9976f4b41431d47bae6f5f34dc42410793bc26c84bfe84fb53ae138c85" );
            unhexify( tag_str, "f5289e1204ace3b2" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "be0c30deeffbe51706247928132002b24d29272eee6b9d618483868e67280236632fa1ae06f3ef793f67bd01b1b01f70a827367c1cd28f778910457c7cbd977dfefff1f84a522247e19b2fd01fa22ce67cef9503d45c80a5084741f04108f2462b7cdd06a8f1f044fea2b05e920bcc061fbc6910175d732f45102a63c76ae48c" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "be0c30deeffbe51706247928132002b24d29272eee6b9d618483868e67280236632fa1ae06f3ef793f67bd01b1b01f70a827367c1cd28f778910457c7cbd977dfefff1f84a522247e19b2fd01fa22ce67cef9503d45c80a5084741f04108f2462b7cdd06a8f1f044fea2b05e920bcc061fbc6910175d732f45102a63c76ae48c" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_0)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "239c15492d6deec979e79236baca4635" );
            pt_len = unhexify( src_str, "d64886ce5f5b4adb7fe8f95904bc1461749c931655b02819ffdd0ae31bad4175125aa68962f8e36ec834a7d53a191a74c937e81ec93ad9ce0d3b286d3c11ff1733c0b7780130768c120b1833933561cf07399ca49b912370ae34f0e49b9c8cb9920eddc6816ab2ae261c6d7f70058a9b83a494026f249e58c4c613eefafe6974" );
            iv_len = unhexify( iv_str, "916b8b5417578fa83d2e9e9b8e2e7f6b" );
            add_len = unhexify( add_str, "b39eb732bc296c555cc9f00cf4caaf37d012329f344a6b74a873baf0d8dde9631f5e57b45b957d6aec0f7978e573dd78b43d459b77756037cd64d10d49966eb3a2a08d0f4d5e4f5dcb8713f4e4756acdf9925c5fc6120c477f6dffc59b0b47a3d5efd32b8c9052b321bb9b5129e5c6a095d8de563601b34608456f58d7221f2d" );
            unhexify( tag_str, "fc08cbbe" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "95c169721ea007c3f292e4ec7562a426d9baa7d374fd82e1e48d1eaca93d891d5ffa9acf5e3bd82e713ac627141e26a8b654920baffab948401cc3c390d6eea9d7b78c4fcb080b0aa9222e4d51bf201ccfd9328995831435e065d92ad37ee41c7c4366cc1efe15c07fc0470608866aeea96997772ecf926934c5d02efe05f250" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "95c169721ea007c3f292e4ec7562a426d9baa7d374fd82e1e48d1eaca93d891d5ffa9acf5e3bd82e713ac627141e26a8b654920baffab948401cc3c390d6eea9d7b78c4fcb080b0aa9222e4d51bf201ccfd9328995831435e065d92ad37ee41c7c4366cc1efe15c07fc0470608866aeea96997772ecf926934c5d02efe05f250" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_1)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "db68a96e216b0dd9945f14b878487e03" );
            pt_len = unhexify( src_str, "5634196a32d4cbfa7a2f874a1e0f86287d2942090e0cc6a82bd5caf40136a27ddf524a17713ce4af04ca6cb640a7205cce4ac9cb2d0ab380d533e1e968089ea5740c0fcbfa51f2424008e0b89dc7b3396b224cfaed53b3ac0604879983d3e6e6d36053de4866f52976890f72b8f4b9505e4ebdd04c0497048c3ce19336133ea4" );
            iv_len = unhexify( iv_str, "8a1a72e7bb740ec37ea4619c3007f8ae" );
            add_len = unhexify( add_str, "1b4f37190a59a4fff41d348798d1829031204fd7ac2a1be7b5ea385567e95e2ace25bf9e324488dd3ab8ce7f29d4c9a4f4b1a8a97f774871ee825e2c17700128d3c55908d3b684a1f550fdb8b38149ff759c21debdd54e49d64d3e8aac803dfd81600464ed484749bb993f89d4224b3d7d55c756b454466ff9fd609019ed5e83" );
            unhexify( tag_str, "9251d3e3" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "0c6bb3ee5de5cbb4b39d85d509bcacb3dda63fa50897936531339882962e8dc54c285c8944768d12096d4a3c2b42ffa92603cee2da9b435ec52908fca6d38ed74f898fe0ffa761f96038ff7dfeccc65bb841c3457b8de1e97d9bee82e2911602ee2dc555b33a227424dea86d610d37c447776295b412b412903ad2cede5170b6" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "0c6bb3ee5de5cbb4b39d85d509bcacb3dda63fa50897936531339882962e8dc54c285c8944768d12096d4a3c2b42ffa92603cee2da9b435ec52908fca6d38ed74f898fe0ffa761f96038ff7dfeccc65bb841c3457b8de1e97d9bee82e2911602ee2dc555b33a227424dea86d610d37c447776295b412b412903ad2cede5170b6" ) == 0 );
                }
            }
        }
        FCT_TEST_END();


        FCT_TEST_BGN(gcm_nist_validation_aes_1281281024102432_2)
        {
            unsigned char key_str[128];
            unsigned char src_str[128];
            unsigned char dst_str[257];
            unsigned char iv_str[128];
            unsigned char add_str[128];
            unsigned char tag_str[128];
            unsigned char output[128];
            gcm_context ctx;
            unsigned int key_len;
            size_t pt_len, iv_len, add_len, tag_len = 32 / 8;
            int ret;
        
            memset(key_str, 0x00, 128);
            memset(src_str, 0x00, 128);
            memset(dst_str, 0x00, 257);
            memset(iv_str, 0x00, 128);
            memset(add_str, 0x00, 128);
            memset(tag_str, 0x00, 128);
            memset(output, 0x00, 128);
        
            key_len = unhexify( key_str, "659b9e729d12f68b73fdc2f7260ab114" );
            pt_len = unhexify( src_str, "fd0732a38224c3f16f58de3a7f333da2ecdb6eec92b469544a891966dd4f8fb64a711a793f1ef6a90e49765eacaccdd8cc438c2b57c51902d27a82ee4f24925a864a9513a74e734ddbf77204a99a3c0060fcfbaccae48fe509bc95c3d6e1b1592889c489801265715e6e4355a45357ce467c1caa2f1c3071bd3a9168a7d223e3" );
            iv_len = unhexify( iv_str, "459df18e2dfbd66d6ad04978432a6d97" );
            add_len = unhexify( add_str, "ee0b0b52a729c45b899cc924f46eb1908e55aaaeeaa0c4cdaacf57948a7993a6debd7b6cd7aa426dc3b3b6f56522ba3d5700a820b1697b8170bad9ca7caf1050f13d54fb1ddeb111086cb650e1c5f4a14b6a927205a83bf49f357576fd0f884a83b068154352076a6e36a5369436d2c8351f3e6bfec65b4816e3eb3f144ed7f9" );
            unhexify( tag_str, "8e5a6a79" );
        
            fct_chk( gcm_init( &ctx, key_str, key_len * 8 ) == 0 );
            if( 0 == 0 )
            {
                ret = gcm_auth_decrypt( &ctx, pt_len, iv_str, iv_len, add_str, add_len, tag_str, tag_len, src_str, output );
        
                if( strcmp( "FAIL", "FAIL" ) == 0 )
                {
                    fct_chk( ret == POLARSSL_ERR_GCM_AUTH_FAILED );
                }
                else
                {
                    hexify( dst_str, output, pt_len );
        
                    fct_chk( strcmp( (char *) dst_str, "FAIL" ) == 0 );
                }
            }
        }
        FCT_TEST_END();

    }
    FCT_SUITE_END();

#endif /* POLARSSL_GCM_C */

}
FCT_END();
